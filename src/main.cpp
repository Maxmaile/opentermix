#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QMetaType>
#include <QSettings>
#include <QTranslator>

#include "app/MainWindow.h"
#include "sessions/Session.h"
#include "sftp/SftpClient.h"
#include "terminal/ColorSchemeStore.h"

int main(int argc, char **argv)
{
    // OpenTermix ships its own icons, but KDE's platform integration still loads the
    // system icon theme at startup and walks its "Inherits=" chain, warning about
    // every parent theme that is not installed (e.g. the user's Nordic-bluish
    // inherits Papirus-Dark, gnome, ...). Those warnings are harmless noise, so
    // silence just that category before the platform theme is created. An explicit
    // QT_LOGGING_RULES from the environment still overrides this if set.
    QLoggingCategory::setFilterRules(QStringLiteral("kf.iconthemes.warning=false"));

    QApplication app(argc, argv);
    QApplication::setApplicationName("OpenTermix");
    QApplication::setOrganizationName("OpenTermix");
    QApplication::setApplicationDisplayName("OpenTermix");

    // Application icon in multiple standard sizes (Qt picks the best match).
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/opentermix_16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/icons/opentermix_32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/icons/opentermix_48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/icons/opentermix_256.png"), QSize(256, 256));
    app.setWindowIcon(icon);

    // Locale: an explicit choice in Settings overrides the system locale.
    const QString languageCode = QSettings().value("ui/language").toString();
    const QLocale locale = languageCode.isEmpty() ? QLocale() : QLocale(languageCode);

    // Load Qt's own translations (dialog buttons, etc.).
    static QTranslator qtTranslator;
    if (qtTranslator.load(locale, "qtbase", "_",
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load OpenTermix translations embedded under ":/i18n" (e.g. opentermix_ru.qm).
    static QTranslator appTranslator;
    if (appTranslator.load(locale, "opentermix", "_", ":/i18n"))
        app.installTranslator(&appTranslator);

    // Custom types travel across the SFTP worker thread via queued signals.
    qRegisterMetaType<Session>("Session");
    qRegisterMetaType<SftpEntry>("SftpEntry");
    qRegisterMetaType<QVector<SftpEntry>>("QVector<SftpEntry>");

    // Konsole-compatible colour schemes (bundled + system Konsole + user custom).
    ColorSchemeStore::registerCustomDir();

    MainWindow window;
    window.show();

    return app.exec();
}
