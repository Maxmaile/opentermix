#pragma once

#include <QMetaType>
#include <QString>

// A single SSH port forward, configured through the Sessions panel's "Tunnel"
// button. Runtime-only (not persisted to ~/.ssh/config or Session): kept as a
// plain struct on purpose, same style as Session.
//
// Field names follow ssh's own -L/-R argv shape ("[bind_addr:]port:host:hostport")
// rather than "local"/"remote", since which side is actually local vs. remote
// flips between -L and -R: bindPort is always the ssh -L/-R/-D "port" argument
// (bound on this machine for Local/Dynamic, bound on the gateway for Remote);
// targetHost/targetPort are always the "host:hostport" destination (unused for
// Dynamic). TunnelDialog relabels the fields per mode so the UI stays clear.
struct TunnelSpec {
    enum class Mode { Local, Remote, Dynamic };

    Mode    mode = Mode::Local;
    quint16 bindPort = 0;   // -L/-R/-D bind port
    QString targetHost;     // unused for Dynamic
    quint16 targetPort = 0; // unused for Dynamic

    bool isValid() const
    {
        if (bindPort == 0)
            return false;
        if (mode == Mode::Dynamic)
            return true;
        return !targetHost.isEmpty() && targetPort != 0;
    }

    // Human-readable one-line summary for menus/labels.
    QString describe() const
    {
        const QString modeName = mode == Mode::Local
            ? QStringLiteral("Local")
            : mode == Mode::Remote ? QStringLiteral("Remote") : QStringLiteral("SOCKS");
        if (mode == Mode::Dynamic)
            return QStringLiteral("%1 (%2)").arg(bindPort).arg(modeName);
        return QStringLiteral("%1 -> %2:%3 (%4)")
            .arg(bindPort)
            .arg(targetHost)
            .arg(targetPort)
            .arg(modeName);
    }
};

Q_DECLARE_METATYPE(TunnelSpec)
