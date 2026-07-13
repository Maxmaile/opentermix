#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

// A terminal colour scheme in Konsole's ".colorscheme" format: a background and
// foreground colour plus the 8 base ANSI colours and their "intense" variants.
struct TermColorScheme {
    QString description;
    QColor background{0, 0, 0};
    QColor foreground{255, 255, 255};
    QColor colors[8];        // Color0..Color7 (normal)
    QColor intense[8];       // Color0..Color7 (intense/bold)

    TermColorScheme();
};

// Reads/writes Konsole-compatible ".colorscheme" files and manages the user's
// custom scheme directory (registered with QTermWidget so custom schemes appear
// in availableColorSchemes()).
namespace ColorSchemeStore {

// Directory where user-defined schemes live (created on demand).
QString customDir();

// Registers customDir() with QTermWidget. Call once at startup, before the first
// terminal is created or availableColorSchemes() is queried.
void registerCustomDir();

// All scheme names known to QTermWidget (bundled + custom), sorted.
QStringList available();

// Resolves a scheme name (or path) to an on-disk ".colorscheme" file, or "".
QString resolvePath(const QString &name);

// True if the scheme is user-defined (lives in customDir() and is editable).
bool isCustom(const QString &name);

// Background colour of a scheme, or an invalid QColor if it can't be read.
QColor background(const QString &name);

// Loads a scheme by name/path. Returns defaults if it can't be read.
TermColorScheme load(const QString &name);

// Saves a scheme under customDir()/<name>.colorscheme. Returns the file path.
QString save(const QString &name, const TermColorScheme &scheme);

// Removes a custom scheme file. No-op for bundled schemes.
bool remove(const QString &name);

} // namespace ColorSchemeStore
