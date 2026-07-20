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

// The trailing target argument: the ssh_config alias (Session::alias), so the
// spawned `ssh` re-reads ~/.ssh/config and matches the same "Host <alias>"
// block ssh itself would - picking up any directive we don't model as a
// dedicated Session field (ProxyCommand, Ciphers, ServerAliveInterval, ...).
// Falls back to "user@host" only if the session has no alias at all (should
// not happen for a session read from a real config file).
QString targetArg(const Session &s);

} // namespace SshArgs
