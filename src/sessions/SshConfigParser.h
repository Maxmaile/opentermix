#pragma once

#include <QList>
#include <QString>

#include "sessions/Session.h"

// Read/write helpers for ~/.ssh/config.
//
// The parser understands the fields OpenTermix cares about and keeps every other
// option of a Host block verbatim in Session::extraLines. Everything before the
// first Host block (global options) is treated as an opaque "preamble" and is
// preserved on save.
namespace SshConfigParser {

QString defaultConfigPath();

// Parse the given config file. Returns an empty list if the file is missing.
// Folder grouping is OpenTermix metadata kept outside the config (see
// SessionLayout); "groups" is only filled for a one-time migration of the legacy
// "# Group:" / "# OpenTermix-Group:" markers that older versions wrote here.
QList<Session> parse(const QString &path, QString *error = nullptr,
                     QStringList *groups = nullptr);

// Rewrite the config file from the given sessions, in the given order, as plain
// Host blocks (no folder metadata - that lives in SessionLayout). A timestamped
// backup of the previous file is created next to it, and the preamble is kept.
bool save(const QString &path, const QList<Session> &sessions, QString *error = nullptr);

} // namespace SshConfigParser
