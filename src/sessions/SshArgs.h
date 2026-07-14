#pragma once

#include <QStringList>

#include "sessions/Session.h"

// Shared ssh argv-building helpers for a Session's connection options - used
// by both interactive terminals (TerminalWidget::startSsh) and headless
// tunnels (TunnelManager::startTunnel), so a change to how a Session maps to
// ssh flags only needs to happen in one place.
namespace SshArgs {

QString expandTilde(const QString &path);

// -p/-i/-J/-C, in that order - the common connection options every ssh
// invocation for a Session needs, regardless of what runs afterwards.
QStringList gatewayArgs(const Session &s);

// The trailing target argument: "user@host", or just the alias if there is
// no explicit HostName (relying on ~/.ssh/config to fill in the rest).
QString targetArg(const Session &s);

} // namespace SshArgs
