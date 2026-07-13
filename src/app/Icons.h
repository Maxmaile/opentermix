#pragma once

#include <QIcon>
#include <QString>

// Central access to the application's embedded icon set. All icons are
// hardcoded under the ":/icons" resource prefix (Nordic-bluish folders/hosts +
// breeze-dark action icons) - nothing is read from the system icon theme.
//
// Monochrome action icons ship in two tones (":/icons/on-dark" light glyphs for
// the dark UI, ":/icons/on-light" dark glyphs for the light UI); setDark() picks
// the set that reads on the current theme. Full-colour icons (folder, host,
// file) live under ":/icons/color" and are theme-independent.
namespace Icons {

// Remember which UI theme is active so action() returns the right tone. Widgets
// that cached an icon must be refreshed (see the various applyIcons()).
void setDark(bool dark);
bool isDark();

// Monochrome action glyph, tinted for the current theme.
QIcon action(const QString &name);

// Full-colour icon, identical in both themes.
QIcon colored(const QString &name);

} // namespace Icons
