#include "app/Icons.h"

namespace {
bool g_dark = true;
}

namespace Icons {

void setDark(bool dark)
{
    g_dark = dark;
}

bool isDark()
{
    return g_dark;
}

QIcon action(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1/%2.svg")
                     .arg(g_dark ? QStringLiteral("on-dark") : QStringLiteral("on-light"), name));
}

QIcon colored(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/color/%1.svg").arg(name));
}

} // namespace Icons
