#include "sessions/SshConfigParser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

// Split a config line into a lowercase key and the raw value. ssh_config accepts
// both "Key value" and "Key=value". A value may be wrapped in matching double
// quotes so it can contain whitespace (e.g. IdentityFile "/path with spaces").
void splitKeyValue(const QString &line, QString &key, QString &value)
{
    const QString s = line.trimmed();
    int i = 0;
    while (i < s.size() && !s[i].isSpace() && s[i] != '=')
        ++i;
    key = s.left(i).toLower();
    while (i < s.size() && (s[i].isSpace() || s[i] == '='))
        ++i;
    value = s.mid(i).trimmed();
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.mid(1, value.size() - 2);
}

// Re-quote a value for writing if it contains whitespace, so it round-trips
// through ssh_config as a single token instead of being silently truncated.
QString quoteIfNeeded(const QString &v)
{
    return v.contains(' ') ? ('"' + v + '"') : v;
}

bool isHostKey(const QString &lowerKey) { return lowerKey == "host"; }

// Everything before the first "Host" line, kept verbatim so that global options
// (Include, Match, ControlMaster, ...) survive a save.
QString readPreamble(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QString preamble;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
            QString key, value;
            splitKeyValue(trimmed, key, value);
            if (isHostKey(key))
                break;
        }
        preamble += line;
        preamble += '\n';
    }
    return preamble;
}

} // namespace

namespace SshConfigParser {

QString defaultConfigPath()
{
    return QDir::homePath() + "/.ssh/config";
}

QList<Session> parse(const QString &path, QString *error, QStringList *groups)
{
    QList<Session> sessions;

    QFile f(path);
    if (!f.exists())
        return sessions; // Not an error: config simply not created yet.

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(path, f.errorString());
        return sessions;
    }

    // A folder ("group") is metadata that must never reorder the Host blocks, so
    // OpenTermix stores it per host as a "# OpenTermix-Group: name" comment inside the
    // block, and empty folders as standalone "# OpenTermix-Folder: name" markers.
    // The legacy positional "# Group: name" header (applied to the hosts that
    // follow it) is still read so older configs keep working until the next save.
    QString pendingGroup; // legacy header carried to subsequent hosts
    bool haveSession = false;
    Session current;
    QSet<QString> seenKeys; // recognised keys already set in the current block;
                            // ssh_config(5) uses the first occurrence, not the last

    auto recordFolder = [&](const QString &name) {
        if (groups && !name.isEmpty() && !groups->contains(name))
            groups->append(name);
    };

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString trimmed = raw.trimmed();

        if (trimmed.isEmpty())
            continue;

        if (trimmed.startsWith('#')) {
            const QString body = trimmed.mid(1).trimmed();
            if (body.startsWith("OpenTermix-Group:", Qt::CaseInsensitive)) {
                // Per-host marker: belongs to the block currently being read.
                const QString g = body.mid(QString("OpenTermix-Group:").size()).trimmed();
                if (haveSession)
                    current.group = g;
                recordFolder(g);
            } else if (body.startsWith("OpenTermix-Folder:", Qt::CaseInsensitive)) {
                // Standalone empty-folder marker.
                recordFolder(body.mid(QString("OpenTermix-Folder:").size()).trimmed());
            } else if (body.startsWith("Group:", Qt::CaseInsensitive)) {
                // Legacy positional header: applies to the hosts that follow it.
                pendingGroup = body.mid(QString("Group:").size()).trimmed();
                recordFolder(pendingGroup);
            } else if (haveSession) {
                // Any other in-block comment: preserve it verbatim so a save()
                // doesn't silently drop the user's own annotations.
                current.extraLines.append(trimmed);
            }
            continue;
        }

        QString key, value;
        splitKeyValue(trimmed, key, value);

        if (isHostKey(key)) {
            if (haveSession)
                sessions.append(current);
            current = Session();
            current.alias = value;
            current.group = pendingGroup; // legacy default; a per-host marker overrides
            haveSession = true;
            seenKeys.clear();
            continue;
        }

        if (!haveSession)
            continue; // Global option handled by the preamble.

        static const QSet<QString> kRecognised = {
            "hostname", "user", "port", "identityfile", "proxyjump",
            "forwardx11", "compression",
        };
        if (kRecognised.contains(key)) {
            if (seenKeys.contains(key))
                continue; // ssh_config(5): the first obtained value wins.
            seenKeys.insert(key);
        }

        if (key == "hostname")
            current.hostName = value;
        else if (key == "user")
            current.user = value;
        else if (key == "port")
            current.port = value.toInt();
        else if (key == "identityfile")
            current.identityFile = value;
        else if (key == "proxyjump")
            current.proxyJump = value;
        else if (key == "forwardx11")
            current.forwardX11 = value.compare("yes", Qt::CaseInsensitive) == 0;
        else if (key == "compression")
            current.compression = value.compare("yes", Qt::CaseInsensitive) == 0;
        else
            current.extraLines.append(trimmed);
    }

    if (haveSession)
        sessions.append(current);

    return sessions;
}

bool save(const QString &path, const QList<Session> &sessions, QString *error)
{
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (error)
            *error = QObject::tr("Cannot create directory %1").arg(dir.path());
        return false;
    }
    // ~/.ssh must be private.
    QFile::setPermissions(dir.path(),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const QString preamble = readPreamble(path);

    // Back up the previous config before rewriting it. Abort rather than write
    // if the backup can't be created, since that backup is the only safety net
    // against a bad rewrite.
    if (QFile::exists(path)) {
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QString backup = path + "." + stamp + ".bak";
        int suffix = 1;
        while (QFile::exists(backup)) // same-second collision: two saves in a row
            backup = path + "." + stamp + "-" + QString::number(suffix++) + ".bak";
        if (!QFile::copy(path, backup)) {
            if (error)
                *error = QObject::tr("Cannot create backup %1; not overwriting %2.")
                             .arg(backup, path);
            return false;
        }
    }

    QString out = preamble;
    if (!out.isEmpty() && !out.endsWith('\n'))
        out += '\n';

    // Emits one plain "Host" block. Blank-line separation is handled by callers.
    auto writeHost = [&out](const Session &s) {
        out += "Host " + s.alias + '\n';
        if (!s.hostName.isEmpty())
            out += "    HostName " + quoteIfNeeded(s.hostName) + '\n';
        if (!s.user.isEmpty())
            out += "    User " + quoteIfNeeded(s.user) + '\n';
        if (s.port != 0 && s.port != 22)
            out += "    Port " + QString::number(s.port) + '\n';
        if (!s.identityFile.isEmpty())
            out += "    IdentityFile " + quoteIfNeeded(s.identityFile) + '\n';
        if (!s.proxyJump.isEmpty())
            out += "    ProxyJump " + quoteIfNeeded(s.proxyJump) + '\n';
        if (s.forwardX11)
            out += "    ForwardX11 yes\n";
        if (s.compression)
            out += "    Compression yes\n";
        for (const QString &extra : s.extraLines)
            out += "    " + extra + '\n';
    };

    bool firstBlock = true; // nothing written after the preamble yet

    // Write every host in the order it was given.
    for (const Session &s : sessions) {
        if (s.alias.isEmpty())
            continue;
        if (!firstBlock)
            out += '\n';
        firstBlock = false;
        writeHost(s);
    }

    // Atomic write: temp file + rename.
    const QString tmpPath = path + ".tmp";
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(tmpPath, tmp.errorString());
        return false;
    }
    {
        QTextStream ts(&tmp);
        ts << out;
    }
    tmp.close();
    tmp.setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    // POSIX rename(2) atomically replaces the destination in a single syscall,
    // unlike QFile::rename() (which refuses to overwrite and would otherwise
    // force a remove-then-rename with a window where the file doesn't exist).
    if (::rename(QFile::encodeName(tmpPath).constData(),
                 QFile::encodeName(path).constData()) != 0) {
        if (error)
            *error = QObject::tr("Cannot replace %1: %2")
                         .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace SshConfigParser
