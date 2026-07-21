#include "sftp/SftpClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <fcntl.h>
#include <sys/types.h>

namespace {
const int kBufferSize = 32 * 1024;
}

SftpClient::SftpClient(QObject *parent)
    : QObject(parent)
    , keepaliveTimer_(new QTimer(this))
{
    keepaliveTimer_->setInterval(30000);
    connect(keepaliveTimer_, &QTimer::timeout, this, &SftpClient::sendKeepalive);
}

SftpClient::~SftpClient()
{
    closeAll();
}

void SftpClient::closeAll()
{
    keepaliveTimer_->stop();
    if (sftp_) {
        sftp_free(sftp_);
        sftp_ = nullptr;
    }
    if (ssh_) {
        if (ssh_is_connected(ssh_))
            ssh_disconnect(ssh_);
        ssh_free(ssh_);
        ssh_ = nullptr;
    }
}

void SftpClient::sendKeepalive()
{
    if (ssh_ && ssh_is_connected(ssh_))
        ssh_send_ignore(ssh_, "");
}

bool SftpClient::verifyHost()
{
    const int state = ssh_session_is_known_server(ssh_);
    switch (state) {
    case SSH_KNOWN_HOSTS_OK:
        return true;
    case SSH_KNOWN_HOSTS_CHANGED:
        emit error(tr("Host key has CHANGED - possible attack. Aborting."));
        return false;
    case SSH_KNOWN_HOSTS_OTHER:
        // The host is known, but under a different key *type* than the one it
        // just presented - as suspicious as an outright changed key.
        emit error(tr("Host key type has changed since the last connection - "
                      "possible attack. Aborting."));
        return false;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        if (ssh_session_update_known_hosts(ssh_) == SSH_OK) {
            emit info(tr("Added host key to known_hosts."));
            return true;
        }
        emit error(tr("Could not store host key."));
        return false;
    default:
        emit error(tr("Host key verification error: %1").arg(ssh_get_error(ssh_)));
        return false;
    }
}

bool SftpClient::authenticate()
{
    // Probe available methods (also lets the server send its banner). If the
    // server allows unauthenticated access outright, accept it immediately
    // instead of spending further auth attempts: servers may enforce a low
    // MaxAuthTries and disconnect before publickey_auto gets a chance to run.
    if (ssh_userauth_none(ssh_, nullptr) == SSH_AUTH_SUCCESS)
        return true;

    int rc = ssh_userauth_agent(ssh_, nullptr);
    if (rc != SSH_AUTH_SUCCESS)
        rc = ssh_userauth_publickey_auto(ssh_, nullptr, nullptr);

    if (rc == SSH_AUTH_SUCCESS)
        return true;

    emit error(tr("Authentication failed. OpenTermix currently supports SSH agent "
                  "and key-based authentication only."));
    return false;
}

bool SftpClient::ensureSftp()
{
    sftp_ = sftp_new(ssh_);
    if (!sftp_) {
        emit error(tr("sftp_new failed: %1").arg(ssh_get_error(ssh_)));
        return false;
    }
    if (sftp_init(sftp_) != SSH_OK) {
        emit error(tr("sftp_init failed: %1").arg(sftp_get_error(sftp_)));
        sftp_free(sftp_);
        sftp_ = nullptr;
        return false;
    }
    return true;
}

bool SftpClient::establish(const Session &session)
{
    closeAll();

    ssh_ = ssh_new();
    if (!ssh_) {
        emit error(tr("Cannot allocate ssh session."));
        return false;
    }

    // Match by the ssh_config alias (not the resolved HostName), so
    // ssh_options_parse_config() below finds the same "Host <alias>" block a
    // manual `ssh <alias>` would and fills in HostName/IdentityFile/
    // ProxyCommand/etc. from it - see SshArgs::targetArg() for the same fix
    // applied to the terminal/tunnel ssh invocations.
    const QByteArray host = (session.alias.isEmpty() ? session.hostName
                                                      : session.alias)
                                .toUtf8();
    ssh_options_set(ssh_, SSH_OPTIONS_HOST, host.constData());

    if (!session.user.isEmpty()) {
        const QByteArray user = session.user.toUtf8();
        ssh_options_set(ssh_, SSH_OPTIONS_USER, user.constData());
    }
    int port = session.port != 0 ? session.port : 22;
    ssh_options_set(ssh_, SSH_OPTIONS_PORT, &port);

    if (!session.proxyJump.isEmpty()) {
        const QByteArray jump = session.proxyJump.toUtf8();
        ssh_options_set(ssh_, SSH_OPTIONS_PROXYJUMP, jump.constData());
    }

    // Pull in remaining options (IdentityFile, etc.) from ~/.ssh/config.
    ssh_options_parse_config(ssh_, nullptr);

    if (ssh_connect(ssh_) != SSH_OK) {
        emit error(tr("Connection failed: %1").arg(ssh_get_error(ssh_)));
        closeAll();
        return false;
    }

    if (!verifyHost() || !authenticate()) {
        closeAll();
        return false;
    }

    if (!ensureSftp()) {
        closeAll();
        return false;
    }

    keepaliveTimer_->start();
    return true;
}

void SftpClient::connectToHost(Session session)
{
    if (!establish(session))
        return;

    QString home = QStringLiteral("/");
    if (char *canonical = sftp_canonicalize_path(sftp_, ".")) {
        home = QString::fromUtf8(canonical);
        ssh_string_free_char(canonical);
    }

    emit connected(home);
    listDir(home);
}

void SftpClient::deployPublicKey(Session session, QString publicKeyPath)
{
    QString localPath = publicKeyPath;
    if (localPath == QStringLiteral("~"))
        localPath = QDir::homePath();
    else if (localPath.startsWith(QStringLiteral("~/")))
        localPath = QDir::homePath() + localPath.mid(1);

    QFile keyFile(localPath);
    if (!keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit keyDeployed(false, tr("Cannot read public key %1: %2")
                                    .arg(localPath, keyFile.errorString()));
        return;
    }
    const QByteArray key = keyFile.readAll().trimmed();
    keyFile.close();
    if (key.isEmpty()) {
        emit keyDeployed(false, tr("The public key file is empty."));
        return;
    }

    // Authenticating as the user proves the account exists and we have access.
    if (!establish(session)) {
        emit keyDeployed(false,
                         tr("Could not connect or authenticate. The user may not exist "
                            "on the host yet, or you have no access to it."));
        return;
    }

    QString home = QStringLiteral("/");
    if (char *canonical = sftp_canonicalize_path(sftp_, ".")) {
        home = QString::fromUtf8(canonical);
        ssh_string_free_char(canonical);
    }
    const QByteArray sshDir = (home + QStringLiteral("/.ssh")).toUtf8();
    const QByteArray akPath = (home + QStringLiteral("/.ssh/authorized_keys")).toUtf8();

    // Make sure ~/.ssh exists.
    if (sftp_attributes da = sftp_stat(sftp_, sshDir.constData())) {
        sftp_attributes_free(da);
    } else if (sftp_mkdir(sftp_, sshDir.constData(), 0700) != SSH_OK) {
        emit keyDeployed(false, tr("Cannot create ~/.ssh: %1").arg(ssh_get_error(ssh_)));
        closeAll();
        return;
    }

    // Inspect an existing authorized_keys so we can append (never overwrite).
    bool existed = false;
    QByteArray existing;
    if (sftp_attributes ka = sftp_stat(sftp_, akPath.constData())) {
        existed = true;
        sftp_attributes_free(ka);
        if (sftp_file rf = sftp_open(sftp_, akPath.constData(), O_RDONLY, 0)) {
            char buf[4096];
            ssize_t n;
            while ((n = sftp_read(rf, buf, sizeof(buf))) > 0)
                existing.append(buf, static_cast<int>(n));
            sftp_close(rf);
        }
    }

    if (existed && existing.contains(key)) {
        emit keyDeployed(true, tr("The key is already present in authorized_keys."));
        closeAll();
        return;
    }

    sftp_file wf = sftp_open(sftp_, akPath.constData(),
                             O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (!wf) {
        emit keyDeployed(false,
                         tr("Cannot open authorized_keys: %1").arg(ssh_get_error(ssh_)));
        closeAll();
        return;
    }

    QByteArray data;
    // Keep entries on separate lines even if the file lacked a trailing newline.
    if (existed && !existing.isEmpty() && !existing.endsWith('\n'))
        data.append('\n');
    data.append(key);
    data.append('\n');

    const ssize_t written = sftp_write(wf, data.constData(), data.size());
    sftp_close(wf);
    if (written != data.size()) {
        emit keyDeployed(false, tr("Failed to write the key to authorized_keys."));
        closeAll();
        return;
    }

    sftp_chmod(sftp_, akPath.constData(), 0600);

    QString message = tr("Public key deployed to %1.").arg(session.displayName());
    if (!existed)
        message += QLatin1Char(' ')
                   + tr("authorized_keys did not exist and was created.");
    emit keyDeployed(true, message);
    closeAll();
}

void SftpClient::listDir(QString path)
{
    if (!sftp_) {
        emit error(tr("Not connected."));
        return;
    }

    sftp_dir dir = sftp_opendir(sftp_, path.toUtf8().constData());
    if (!dir) {
        emit error(tr("Cannot open %1: %2").arg(path, ssh_get_error(ssh_)));
        return;
    }

    QVector<SftpEntry> entries;
    while (sftp_attributes attr = sftp_readdir(sftp_, dir)) {
        if (attr->name) {
            const QString name = QString::fromUtf8(attr->name);
            if (name != "." && name != "..") {
                SftpEntry e;
                e.name = name;
                e.isDir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
                e.size = static_cast<qint64>(attr->size);
                e.mtime = attr->mtime;
                entries.push_back(e);
            }
        }
        sftp_attributes_free(attr);
    }
    sftp_closedir(dir);

    emit listed(path, entries);
}

void SftpClient::download(QString remotePath, QString localPath)
{
    cancelled_.store(false, std::memory_order_relaxed);
    const QString name = QFileInfo(remotePath).fileName();
    if (!sftp_) {
        emit error(tr("Not connected."));
        emit transferFinished(name, false);
        return;
    }

    sftp_file file = sftp_open(sftp_, remotePath.toUtf8().constData(), O_RDONLY, 0);
    if (!file) {
        emit error(tr("Cannot open remote %1: %2").arg(remotePath, ssh_get_error(ssh_)));
        emit transferFinished(name, false);
        return;
    }

    qint64 total = 0;
    if (sftp_attributes attr = sftp_stat(sftp_, remotePath.toUtf8().constData())) {
        total = static_cast<qint64>(attr->size);
        sftp_attributes_free(attr);
    }

    QFile out(localPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit error(tr("Cannot write %1: %2").arg(localPath, out.errorString()));
        sftp_close(file);
        emit transferFinished(name, false);
        return;
    }

    QByteArray buffer(kBufferSize, Qt::Uninitialized);
    qint64 done = 0;
    bool ok = true;
    for (;;) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            ok = false;
            emit error(tr("Transfer cancelled."));
            break;
        }
        const ssize_t n = sftp_read(file, buffer.data(), buffer.size());
        if (n == 0)
            break;
        if (n < 0) {
            ok = false;
            emit error(tr("Read error on %1").arg(remotePath));
            break;
        }
        if (out.write(buffer.constData(), n) != n) {
            ok = false;
            emit error(tr("Write error on %1").arg(localPath));
            break;
        }
        done += n;
        emit transferProgress(name, done, total);
    }

    out.close();
    sftp_close(file);
    emit transferFinished(name, ok);
}

void SftpClient::upload(QString localPath, QString remotePath)
{
    cancelled_.store(false, std::memory_order_relaxed);
    const QString name = QFileInfo(localPath).fileName();
    if (!sftp_) {
        emit error(tr("Not connected."));
        emit transferFinished(name, false);
        return;
    }

    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly)) {
        emit error(tr("Cannot read %1: %2").arg(localPath, in.errorString()));
        emit transferFinished(name, false);
        return;
    }

    sftp_file file = sftp_open(sftp_, remotePath.toUtf8().constData(),
                               O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!file) {
        emit error(tr("Cannot create remote %1: %2").arg(remotePath, ssh_get_error(ssh_)));
        emit transferFinished(name, false);
        return;
    }

    const qint64 total = in.size();
    qint64 done = 0;
    bool ok = true;
    QByteArray buffer;
    while (!(buffer = in.read(kBufferSize)).isEmpty()) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            ok = false;
            emit error(tr("Transfer cancelled."));
            break;
        }
        const ssize_t n = sftp_write(file, buffer.constData(), buffer.size());
        if (n < 0 || n != buffer.size()) {
            ok = false;
            emit error(tr("Write error on %1").arg(remotePath));
            break;
        }
        done += n;
        emit transferProgress(name, done, total);
    }

    sftp_close(file);
    in.close();
    emit transferFinished(name, ok);
}

void SftpClient::disconnectFromHost()
{
    closeAll();
}

void SftpClient::requestCancel()
{
    cancelled_.store(true, std::memory_order_relaxed);
}
