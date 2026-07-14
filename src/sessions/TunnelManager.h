#pragma once

#include <QList>
#include <QObject>
#include <QProcess>

#include "sessions/Session.h"
#include "sessions/Tunnel.h"

// Public, copy-safe snapshot of one running tunnel. Deliberately has no
// QProcess handle: callers only need to display/identify tunnels (see
// SessionPanel::showTunnelMenu), never reach into the underlying process, so
// there is nothing here that can outlive it and dangle.
struct TunnelInfo {
    int id = 0;
    Session gateway;
    TunnelSpec spec;
};

// Owns headless `ssh -N -L/-R/-D ...` child processes started from the
// Sessions panel's "Tunnel" button. Unlike SftpClient (which blocks on
// libssh and needs its own worker thread), QProcess is already async on the
// GUI thread, so no thread is needed here.
class TunnelManager : public QObject {
    Q_OBJECT
public:
    explicit TunnelManager(QObject *parent = nullptr);
    ~TunnelManager() override; // terminates every still-running tunnel

    // Spawns `ssh -N ...`. Returns a positive id on success, or -1 with
    // *error set if QProcess::start() itself failed (e.g. ssh not found).
    // Async failures (bad port, auth failure, ...) surface later via
    // tunnelFailed().
    int startTunnel(const Session &gateway, const TunnelSpec &spec, QString *error);

    // Stops one tunnel by id. No-op if id is unknown.
    void stopTunnel(int id);

    // Stops every running tunnel (used from the destructor and from
    // SessionPanel::stopAllTunnels() on app shutdown).
    void stopAll();

    QList<TunnelInfo> activeTunnels() const;

signals:
    void tunnelsChanged();                      // started or stopped - refresh menus
    void tunnelFailed(int id, QString message); // async failure after spawn

private:
    // Internal bookkeeping only - never exposed outside this class (see
    // TunnelInfo above for the public-facing equivalent).
    struct RunningTunnel {
        int id = 0;
        Session gateway;
        TunnelSpec spec;
        QProcess *process = nullptr;
    };

    void onProcessFinished(int id, int exitCode, QProcess::ExitStatus status);

    QList<RunningTunnel> tunnels_;
    int nextId_ = 1;
};
