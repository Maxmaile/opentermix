#include "sessions/SshArgs.h"

#include <QDir>

namespace SshArgs {

QString expandTilde(const QString &path)
{
    if (path == "~")
        return QDir::homePath();
    if (path.startsWith("~/"))
        return QDir::homePath() + path.mid(1);
    return path;
}

QStringList gatewayArgs(const Session &s)
{
    QStringList args;
    if (s.port != 0 && s.port != 22)
        args << "-p" << QString::number(s.port);
    if (!s.identityFile.isEmpty())
        args << "-i" << expandTilde(s.identityFile);
    if (!s.proxyJump.isEmpty())
        args << "-J" << s.proxyJump;
    if (s.compression)
        args << "-C";
    return args;
}

QString targetArg(const Session &s)
{
    return s.alias.isEmpty() ? s.target() : s.alias;
}

} // namespace SshArgs
