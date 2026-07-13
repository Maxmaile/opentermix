#include "terminal/ColorSchemeStore.h"

#include <qtermwidget6/qtermwidget.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

TermColorScheme::TermColorScheme()
{
    // A sensible "WhiteOnBlack"-like default palette (xterm colours).
    static const QColor base[8] = {
        QColor(0, 0, 0),       QColor(178, 24, 24),  QColor(24, 178, 24),
        QColor(178, 104, 24),  QColor(24, 24, 178),  QColor(178, 24, 178),
        QColor(24, 178, 178),  QColor(178, 178, 178)};
    static const QColor bright[8] = {
        QColor(104, 104, 104), QColor(255, 84, 84),   QColor(84, 255, 84),
        QColor(255, 255, 84),  QColor(84, 84, 255),   QColor(255, 84, 255),
        QColor(84, 255, 255),  QColor(255, 255, 255)};
    for (int i = 0; i < 8; ++i) {
        colors[i] = base[i];
        intense[i] = bright[i];
    }
}

namespace {

QColor parseColor(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return QColor();
    return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                  parts[2].trimmed().toInt());
}

QString formatColor(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

namespace ColorSchemeStore {

QString customDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/color-schemes");
}

void registerCustomDir()
{
    const QString dir = customDir();
    QDir().mkpath(dir);
    QTermWidget::addCustomColorSchemeDir(dir);

    // Konsole ships many more schemes in the same ".colorscheme" format.
    // QTermWidget is derived from Konsole and reads them natively.
    const QStringList dataDirs =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &d : dataDirs) {
        const QString konsoleDir = d + QStringLiteral("/konsole/color-schemes");
        if (QDir(konsoleDir).exists())
            QTermWidget::addCustomColorSchemeDir(konsoleDir);
    }
}

QStringList available()
{
    QStringList names = QTermWidget::availableColorSchemes();

    // Pick up user schemes saved after the first availableColorSchemes() scan.
    QDir dir(customDir());
    const QStringList filters{QStringLiteral("*.colorscheme")};
    for (const QString &file : dir.entryList(filters, QDir::Files)) {
        const QString name = QFileInfo(file).completeBaseName();
        if (!names.contains(name, Qt::CaseInsensitive))
            names.append(name);
    }

    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString resolvePath(const QString &name)
{
    // Already a path to an existing file.
    if (name.endsWith(QLatin1String(".colorscheme")) && QFileInfo::exists(name))
        return name;

    const QString file = name + QStringLiteral(".colorscheme");

    // User-defined schemes take precedence.
    const QString custom = customDir() + QLatin1Char('/') + file;
    if (QFileInfo::exists(custom))
        return custom;

    // Then the standard qtermwidget data directories (mirrors its own lookup).
    const QStringList dataDirs =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &d : dataDirs) {
        const QString candidate = d + QStringLiteral("/qtermwidget6/color-schemes/") + file;
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

bool isCustom(const QString &name)
{
    const QString custom = customDir() + QLatin1Char('/') + name + QStringLiteral(".colorscheme");
    return QFileInfo::exists(custom);
}

QColor background(const QString &name)
{
    const QString path = resolvePath(name);
    if (path.isEmpty())
        return QColor();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QColor();

    QString section;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
        } else if (section == QLatin1String("Background")
                   && line.startsWith(QLatin1String("Color="))) {
            return parseColor(line.mid(6));
        }
    }
    return QColor();
}

TermColorScheme load(const QString &name)
{
    TermColorScheme s;
    const QString path = resolvePath(name);
    if (path.isEmpty())
        return s;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return s;

    QString section;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (section == QLatin1String("General")) {
            if (key == QLatin1String("Description"))
                s.description = value;
            continue;
        }
        if (key != QLatin1String("Color"))
            continue;
        const QColor c = parseColor(value);
        if (!c.isValid())
            continue;
        if (section == QLatin1String("Background"))
            s.background = c;
        else if (section == QLatin1String("Foreground"))
            s.foreground = c;
        else if (section.startsWith(QLatin1String("Color"))) {
            const bool intense = section.endsWith(QLatin1String("Intense"));
            QString idxStr = section.mid(5);
            if (intense)
                idxStr.chop(7); // strip "Intense"
            bool ok = false;
            const int idx = idxStr.toInt(&ok);
            if (ok && idx >= 0 && idx < 8) {
                if (intense)
                    s.intense[idx] = c;
                else
                    s.colors[idx] = c;
            }
        }
    }
    if (s.description.isEmpty())
        s.description = name;
    return s;
}

QString save(const QString &name, const TermColorScheme &scheme)
{
    QDir().mkpath(customDir());
    const QString path = customDir() + QLatin1Char('/') + name + QStringLiteral(".colorscheme");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return QString();

    QTextStream out(&f);
    auto writeSection = [&out](const QString &sec, const QColor &c, bool bold) {
        out << QLatin1Char('[') << sec << "]\n";
        out << "Bold=" << (bold ? "true" : "false") << '\n';
        out << "Color=" << formatColor(c) << '\n';
        out << "Transparency=false\n\n";
    };

    writeSection(QStringLiteral("Background"), scheme.background, false);
    writeSection(QStringLiteral("BackgroundIntense"), scheme.background, true);
    for (int i = 0; i < 8; ++i) {
        writeSection(QStringLiteral("Color%1").arg(i), scheme.colors[i], false);
        writeSection(QStringLiteral("Color%1Intense").arg(i), scheme.intense[i], true);
    }
    writeSection(QStringLiteral("Foreground"), scheme.foreground, false);
    writeSection(QStringLiteral("ForegroundIntense"), scheme.foreground, true);

    out << "[General]\n";
    out << "Description=" << (scheme.description.isEmpty() ? name : scheme.description) << '\n';
    out << "Opacity=1\n";
    return path;
}

bool remove(const QString &name)
{
    if (!isCustom(name))
        return false;
    const QString path = customDir() + QLatin1Char('/') + name + QStringLiteral(".colorscheme");
    return QFile::remove(path);
}

} // namespace ColorSchemeStore
