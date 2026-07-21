#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <atomic>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "sessions/Session.h"

struct SftpEntry {
    QString name;
    bool isDir = false;
    qint64 size = 0;
    quint32 mtime = 0;
};

Q_DECLARE_METATYPE(SftpEntry)

// Runs libssh operations. Intended to live in its own QThread; all public slots
// are invoked through queued connections and results come back as signals.
//
// MVP scope: authentication via SSH agent or on-disk keys only (no password
// prompt yet - that arrives with the Phase 3 password manager). Unknown host
// keys are accepted and stored; a changed host key aborts the connection.
class SftpClient : public QObject {
    Q_OBJECT
public:
    explicit SftpClient(QObject *parent = nullptr);
    ~SftpClient() override;

public slots:
    void connectToHost(Session session);
    void listDir(QString path);
    void download(QString remotePath, QString localPath);
    void upload(QString localPath, QString remotePath);
    void deployPublicKey(Session session, QString publicKeyPath);
    void disconnectFromHost();

    // Not a slot: a plain atomic flag write, safe to call directly from any
    // thread (in particular from the GUI thread while a transfer is blocked
    // inside a tight read/write loop on the worker thread, where queued
    // signals wouldn't be processed until that loop returns).
    void requestCancel();

signals:
    void connected(QString homeDir);
    void listed(QString path, QVector<SftpEntry> entries);
    void error(QString message);
    void info(QString message);
    void transferProgress(QString name, qint64 done, qint64 total);
    void transferFinished(QString name, bool ok);
    void keyDeployed(bool ok, QString message);

private slots:
    // A session left idle for a while (just browsing, no reads/writes) can be
    // silently dropped by a NAT/firewall connection-tracking timeout or the
    // server's own idle timeout; libssh only notices on the next real
    // operation, which then fails with "socket error: disconnected" out of
    // nowhere. Sending a periodic no-op packet - the same trick ssh's own
    // ServerAliveInterval uses - keeps the connection (and any NAT mapping)
    // alive so that doesn't happen.
    void sendKeepalive();

private:
    bool establish(const Session &session);
    bool verifyHost();
    bool authenticate();
    bool ensureSftp();
    void closeAll();

    ssh_session ssh_ = nullptr;
    sftp_session sftp_ = nullptr;
    std::atomic_bool cancelled_{false};
    QTimer *keepaliveTimer_;
};
