#include "sessions/TunnelManager.h"

#include "sessions/SshArgs.h"

namespace {

QStringList buildTunnelArgs(const Session &gateway, const TunnelSpec &spec)
{
    QStringList args;
    args << "-N" << "-T"; // no remote command, no pty - pure tunnel
    args += SshArgs::gatewayArgs(gateway);
    // forwardX11 intentionally not honored: meaningless on a headless -N tunnel.

    switch (spec.mode) {
    case TunnelSpec::Mode::Local:
        args << "-L"
             << QStringLiteral("%1:%2:%3")
                    .arg(spec.bindPort)
                    .arg(spec.targetHost)
                    .arg(spec.targetPort);
        break;
    case TunnelSpec::Mode::Remote:
        args << "-R"
             << QStringLiteral("%1:%2:%3")
                    .arg(spec.bindPort)
                    .arg(spec.targetHost)
                    .arg(spec.targetPort);
        break;
    case TunnelSpec::Mode::Dynamic:
        args << "-D" << QString::number(spec.bindPort);
        break;
    }

    args << SshArgs::targetArg(gateway);
    return args;
}

} // namespace

TunnelManager::TunnelManager(QObject *parent)
    : QObject(parent)
{
}

TunnelManager::~TunnelManager()
{
    // Ask every process to exit before waiting on any of them, so the total
    // wait is roughly bounded by the slowest one instead of summing 2s per
    // tunnel (see stopAll(), which uses the same two-pass shape).
    for (RunningTunnel &t : tunnels_) {
        if (!t.process)
            continue;
        // Disconnect first: onProcessFinished() must not fire and mutate
        // tunnels_ out from under this loop (waitForFinished() below can pump
        // the event loop and deliver the finished() signal reentrantly).
        t.process->disconnect(this);
        t.process->terminate();
    }
    for (RunningTunnel &t : tunnels_) {
        if (!t.process)
            continue;
        if (!t.process->waitForFinished(2000))
            t.process->kill();
    }
}

int TunnelManager::startTunnel(const Session &gateway, const TunnelSpec &spec, QString *error)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    const int id = nextId_++;
    connect(process, &QProcess::finished, this,
            [this, id](int code, QProcess::ExitStatus status) {
                onProcessFinished(id, code, status);
            });

    process->start(QStringLiteral("ssh"), buildTunnelArgs(gateway, spec));
    // ssh is a local binary already required by the rest of the app (terminal
    // sessions spawn it too), so start-up is normally near-instant; this only
    // guards against it being missing/unexecutable, not network latency.
    if (!process->waitForStarted(2000)) {
        if (error)
            *error = process->errorString();
        process->deleteLater();
        return -1;
    }

    RunningTunnel t;
    t.id = id;
    t.gateway = gateway;
    t.spec = spec;
    t.process = process;
    tunnels_.append(t);
    emit tunnelsChanged();
    return id;
}

void TunnelManager::stopTunnel(int id)
{
    for (int i = 0; i < tunnels_.size(); ++i) {
        if (tunnels_.at(i).id != id)
            continue;

        RunningTunnel t = tunnels_.takeAt(i);
        if (t.process) {
            // Disconnect first: this is a deliberate stop, not a failure, and
            // the entry is already out of tunnels_ - onProcessFinished() has
            // nothing left to do with it.
            t.process->disconnect(this);
            t.process->terminate();
            if (!t.process->waitForFinished(2000))
                t.process->kill();
            t.process->deleteLater();
        }
        emit tunnelsChanged();
        return;
    }
}

void TunnelManager::stopAll()
{
    for (RunningTunnel &t : tunnels_) {
        if (!t.process)
            continue;
        t.process->disconnect(this);
        t.process->terminate();
    }
    for (RunningTunnel &t : tunnels_) {
        if (!t.process)
            continue;
        if (!t.process->waitForFinished(2000))
            t.process->kill();
        t.process->deleteLater();
    }
    tunnels_.clear();
    emit tunnelsChanged();
}

QList<TunnelInfo> TunnelManager::activeTunnels() const
{
    QList<TunnelInfo> result;
    result.reserve(tunnels_.size());
    for (const RunningTunnel &t : tunnels_)
        result.append({t.id, t.gateway, t.spec});
    return result;
}

void TunnelManager::onProcessFinished(int id, int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    int index = -1;
    for (int i = 0; i < tunnels_.size(); ++i) {
        if (tunnels_.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    // Only reached for a process that is still connected to this slot, i.e.
    // one that exited on its own - stopTunnel()/stopAll()/~TunnelManager()
    // all disconnect before terminating, so a deliberate stop never lands
    // here.
    RunningTunnel t = tunnels_.takeAt(index);
    if (exitCode != 0) {
        const QString message = t.process ? QString::fromUtf8(t.process->readAllStandardOutput())
                                          : QString();
        emit tunnelFailed(id, message.isEmpty() ? tr("ssh exited with code %1").arg(exitCode)
                                                : message);
    }
    if (t.process)
        t.process->deleteLater();
    emit tunnelsChanged();
}
