#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QTimer>
#include <QDockWidget>
#include <QFile>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFontInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QVariant>
#include <QVariantMap>

#include "app/Icons.h"
#include "app/SettingsWidget.h"
#include "terminal/ColorSchemeStore.h"
#include "sessions/SessionPanel.h"
#include "sessions/TunnelsTabWidget.h"
#include "sftp/SftpBrowserWidget.h"
#include "terminal/MultiExec.h"
#include "terminal/TerminalArea.h"
#include "terminal/TerminalWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , multiExec_(new MultiExec(this))
{
    // Start from the system's default fixed-width (console) font. On some setups
    // that font is not actually monospaced, which QTermWidget warns about and
    // renders poorly, so fall back to a generic monospace family. A user choice
    // saved in QSettings overrides this in readSettings().
    terminalFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // The platform's reported default fixed font can carry unwanted weight/style
    // bits (seen as Bold Italic on some desktop themes) - normalize to a plain
    // bold, non-italic default regardless of what the theme reports.
    terminalFont_.setBold(true);
    terminalFont_.setItalic(false);
    if (!QFontInfo(terminalFont_).fixedPitch()) {
        terminalFont_.setFamily(QStringLiteral("monospace"));
        terminalFont_.setStyleHint(QFont::Monospace);
        terminalFont_.setFixedPitch(true);
        if (terminalFont_.pointSize() <= 0)
            terminalFont_.setPointSize(11);
    }

    resize(1100, 700);
    setWindowTitle(QStringLiteral("OpenTermix"));

    readSettings(); // dark_, terminalFont_ before widgets are built
    Icons::setDark(dark_); // so widgets pick the right icon tone as they are built

    area_ = new TerminalArea(multiExec_, this);
    setCentralWidget(area_);

    createDocks();
    createActions();

    // Each terminal tab owns a filesystem browser; the dock shows the active one.
    connect(area_, &TerminalArea::terminalOpened, this, &MainWindow::onTerminalOpened);
    connect(area_, &TerminalArea::terminalClosing, this, &MainWindow::onTerminalClosing);
    connect(area_, &TerminalArea::currentTerminalChanged, this,
            &MainWindow::onCurrentTerminalChanged);
    connect(area_, &TerminalArea::tunnelsTabClosing, this, &MainWindow::onTunnelsTabClosing);

    restoreLayout(); // geometry + dock/toolbar state (needs objectNames)
    applyTheme();

    restoreOrOpenTabs();
}

MainWindow::~MainWindow() = default;

void MainWindow::createDocks()
{
    sessions_ = new SessionPanel(this);
    sessionDock_ = new QDockWidget(tr("Sessions"), this);
    sessionDock_->setObjectName("SessionDock");
    sessionDock_->setWidget(sessions_);
    addDockWidget(Qt::LeftDockWidgetArea, sessionDock_);

    filesStack_ = new QStackedWidget(this);
    filesPlaceholder_ = new QLabel(tr("No active terminal"), filesStack_);
    static_cast<QLabel *>(filesPlaceholder_)->setAlignment(Qt::AlignCenter);
    filesStack_->addWidget(filesPlaceholder_);

    sftpDock_ = new QDockWidget(tr("Files"), this);
    sftpDock_->setObjectName("SftpDock");
    sftpDock_->setWidget(filesStack_);
    addDockWidget(Qt::RightDockWidgetArea, sftpDock_);

    connect(sessions_, &SessionPanel::connectRequested, this, &MainWindow::onConnectRequested);
    connect(sessions_, &SessionPanel::sftpRequested, this, &MainWindow::onSftpRequested);
    connect(sessions_, &SessionPanel::tunnelsChanged, this, &MainWindow::onTunnelsChanged);
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("New local tab"), QKeySequence(tr("Ctrl+T")),
                        area_, [this] { area_->newLocalTab(); });
    fileMenu->addAction(tr("Open external terminal"), this, &MainWindow::openExternalTerminal);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), QKeySequence(QKeySequence::Quit), this, &QWidget::close);

    QMenu *sessionMenu = menuBar()->addMenu(tr("&Sessions"));
    sessionMenu->addAction(tr("Refresh from ~/.ssh/config"), sessions_, &SessionPanel::reload);

    QMenu *terminalMenu = menuBar()->addMenu(tr("&Terminal"));
    terminalMenu->addAction(tr("New tab"), this, [this] { area_->newLocalTab(); });
    terminalMenu->addAction(tr("Split Right"), this,
                            [this] { area_->splitActive(Qt::Horizontal); });
    terminalMenu->addAction(tr("Split Down"), this,
                            [this] { area_->splitActive(Qt::Vertical); });
    terminalMenu->addSeparator();
    broadcastAction_ = terminalMenu->addAction(tr("Broadcast input to all terminals"));
    broadcastAction_->setCheckable(true);
    connect(broadcastAction_, &QAction::toggled, multiExec_, &MultiExec::setEnabled);
    terminalMenu->addSeparator();
    terminalMenu->addAction(tr("Terminal font..."), this, &MainWindow::chooseFont);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(sessionDock_->toggleViewAction());
    viewMenu->addAction(sftpDock_->toggleViewAction());
    viewMenu->addSeparator();
    darkAction_ = viewMenu->addAction(tr("Dark theme"));
    darkAction_->setCheckable(true);
    darkAction_->setChecked(dark_);
    connect(darkAction_, &QAction::toggled, this, &MainWindow::toggleTheme);

    // Settings sits directly on the menu bar (next to File, Sessions, ...).
    menuBar()->addAction(tr("Settings"), this, &MainWindow::openSettings);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("About OpenTermix"), this, &MainWindow::about);
}

void MainWindow::onConnectRequested(const Session &s)
{
    // Opening the tab creates its own filesystem browser (see onTerminalOpened),
    // which connects over SFTP and shows the remote tree automatically.
    area_->newSshTab(s);
}

void MainWindow::onSftpRequested(const Session &s)
{
    sftpDock_->show();
    sftpDock_->raise();
    area_->newSshTab(s);
}

void MainWindow::onTerminalOpened(TerminalWidget *terminal, const Session &session)
{
    auto *browser = new SftpBrowserWidget;
    filesStack_->addWidget(browser);
    browsers_.insert(terminal, browser);
    sessionOf_.insert(terminal, session);
    // "cat" buttons in the file browser type the command into this terminal.
    // Bound to the terminal as context so it auto-disconnects when it closes.
    connect(browser, &SftpBrowserWidget::runInTerminal, terminal,
            &TerminalWidget::sendText);
    // A configured or ad-hoc host has a display name; empty means a local shell.
    if (!session.displayName().isEmpty())
        browser->connectTo(session);
}

void MainWindow::onTerminalClosing(TerminalWidget *terminal)
{
    sessionOf_.remove(terminal);
    if (SftpBrowserWidget *browser = browsers_.take(terminal)) {
        filesStack_->removeWidget(browser);
        browser->deleteLater();
    }
}

void MainWindow::onCurrentTerminalChanged(TerminalWidget *terminal)
{
    if (terminal && browsers_.contains(terminal))
        filesStack_->setCurrentWidget(browsers_.value(terminal));
    else
        filesStack_->setCurrentWidget(filesPlaceholder_);
}

void MainWindow::openExternalTerminal()
{
    const QStringList candidates = {"x-terminal-emulator", "gnome-terminal",
                                    "konsole", "xfce4-terminal", "alacritty", "xterm"};
    for (const QString &term : candidates) {
        if (QProcess::startDetached(term, {}))
            return;
    }
    QMessageBox::warning(this, tr("External terminal"),
                         tr("No external terminal emulator was found."));
}

void MainWindow::openSettings()
{
    if (settingsWidget_) {
        // Already open: switch to that tab instead of piling up duplicates.
        for (QWidget *p = settingsWidget_->parentWidget(); p; p = p->parentWidget()) {
            if (auto *tabs = qobject_cast<QTabWidget *>(p)) {
                tabs->setCurrentWidget(settingsWidget_);
                break;
            }
        }
        settingsWidget_->setFocus();
        return;
    }
    auto *settings = new SettingsWidget(dark_, terminalScheme_, this);
    settingsWidget_ = settings;
    connect(settings, &SettingsWidget::themeChanged, this, &MainWindow::toggleTheme);
    connect(settings, &SettingsWidget::chooseFontRequested, this, &MainWindow::chooseFont);
    connect(settings, &SettingsWidget::colorSchemeChanged, this, &MainWindow::changeColorScheme);
    connect(settings, &SettingsWidget::languageChangeRequested, this, &MainWindow::changeLanguage);
    area_->openContentTab(settings, tr("Settings"));
}

void MainWindow::onTunnelsChanged()
{
    const QList<TunnelInfo> tunnels = sessions_->activeTunnels();
    if (tunnels.isEmpty()) {
        if (tunnelsTab_)
            area_->closeContentTab(tunnelsTab_);
        return;
    }
    if (!tunnelsTab_) {
        auto *tab = new TunnelsTabWidget(this);
        tunnelsTab_ = tab;
        connect(tab, &TunnelsTabWidget::stopRequested, sessions_, &SessionPanel::stopTunnel);
        area_->openContentTab(tab, tr("Tunnels"));
    }
    tunnelsTab_->setTunnels(tunnels);
}

void MainWindow::onTunnelsTabClosing()
{
    // TerminalGroup::handleClose() is already removing this tab (the user just
    // confirmed) - clear our tracking pointer first so the tunnelsChanged()
    // fallout from stopAllTunnels() below doesn't try to remove it a second
    // time (see MainWindow::onTunnelsChanged()).
    tunnelsTab_ = nullptr;
    sessions_->stopAllTunnels();
}

void MainWindow::changeLanguage(const QString &code)
{
    QSettings().setValue("ui/language", code);

    const auto answer = QMessageBox::question(
        this, tr("Language"),
        tr("The language change applies after restart. Restart OpenTermix now?"),
        QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        saveSettings();
        QProcess::startDetached(QApplication::applicationFilePath(), QApplication::arguments().mid(1));
        qApp->quit();
    }
}

void MainWindow::chooseFont()
{
    bool ok = false;
    const QFont font = QFontDialog::getFont(&ok, terminalFont_, this, tr("Terminal font"));
    if (ok) {
        terminalFont_ = font;
        area_->setTerminalFont(font);
        // Persist right away rather than only on a clean shutdown (closeEvent),
        // so the choice survives a crash or a killed session too.
        QSettings settings;
        settings.setValue("ui/font", terminalFont_.toString());
    }
}

void MainWindow::toggleTheme(bool dark)
{
    dark_ = dark;
    // Keep the View menu's checkbox in sync no matter which path triggered the
    // change (this action, or the Settings tab's own dark-mode control), and
    // without re-entering this slot via the action's own toggled() signal.
    if (darkAction_ && darkAction_->isChecked() != dark_) {
        const QSignalBlocker blocker(darkAction_);
        darkAction_->setChecked(dark_);
    }
    applyTheme();
}

void MainWindow::changeColorScheme(const QString &scheme)
{
    terminalScheme_ = scheme;
    QSettings().setValue("terminal/colorScheme", scheme);
    applyTheme();
}

// Background colour of each bundled QTermWidget colour scheme. The QSS bleed-fix
// rule (see below) must paint the terminal surface with exactly this colour,
// otherwise empty lines show through in the wrong colour after a selection.
static QString schemeBackground(const QString &scheme)
{
    static const QHash<QString, QString> fallback = {
        {QStringLiteral("WhiteOnBlack"), QStringLiteral("#000000")},
        {QStringLiteral("GreenOnBlack"), QStringLiteral("#000000")},
        {QStringLiteral("DarkPastels"), QStringLiteral("#2c2c2c")},
        {QStringLiteral("BlackOnWhite"), QStringLiteral("#ffffff")},
        {QStringLiteral("SolarizedDark"), QStringLiteral("#002b36")},
        {QStringLiteral("SolarizedLight"), QStringLiteral("#fdf6e3")},
    };
    const QColor bg = ColorSchemeStore::background(scheme);
    if (bg.isValid())
        return bg.name();
    return fallback.value(scheme, QStringLiteral("#000000"));
}

QString MainWindow::resolvedScheme() const
{
    QString s = terminalScheme_;
    if (s.isEmpty() || s == QLatin1String("auto"))
        s = dark_ ? QStringLiteral("WhiteOnBlack") : QStringLiteral("BlackOnWhite");
    return s;
}

void MainWindow::applyTheme()
{
    // Pick the icon tone that reads on this theme, then refresh every cached icon.
    Icons::setDark(dark_);
    if (sessions_)
        sessions_->applyIcons();
    if (area_)
        area_->applyIcons();
    for (SftpBrowserWidget *browser : browsers_)
        browser->applyIcons();

    const QString path = dark_ ? QStringLiteral(":/styles/dark.qss")
                               : QStringLiteral(":/styles/light.qss");
    QFile file(path);
    QString style;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        style = QString::fromUtf8(file.readAll());

    const QString scheme = resolvedScheme();
    // Force the terminal surface to match its colour scheme so the global
    // QWidget background never bleeds through empty rows.
    style += QStringLiteral("\nQTermWidget#TerminalPane, QTermWidget#TerminalPane * "
                            "{ background-color: %1; }\n")
                 .arg(schemeBackground(scheme));
    qApp->setStyleSheet(style);

    area_->setColorScheme(scheme);
    area_->setTerminalFont(terminalFont_);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About OpenTermix"),
                       tr("<b>OpenTermix</b><br>"
                          "An open-source, lightweight MobaXterm-like terminal.<br>"
                          "Built with Qt6, QTermWidget and libssh."));
}

void MainWindow::readSettings()
{
    QSettings settings;
    dark_ = settings.value("ui/dark", true).toBool();
    terminalScheme_ = settings.value("terminal/colorScheme",
                                     QStringLiteral("WhiteOnBlack")).toString();
    if (settings.contains("ui/font")) {
        QFont font;
        if (font.fromString(settings.value("ui/font").toString()))
            terminalFont_ = font;
    }
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    if (settings.contains("ui/geometry"))
        restoreGeometry(settings.value("ui/geometry").toByteArray());
    if (settings.contains("ui/state"))
        restoreState(settings.value("ui/state").toByteArray());
}

void MainWindow::restoreOrOpenTabs()
{
    const QVariantList tabs = QSettings().value("state/tabs").toList();
    if (tabs.isEmpty()) {
        area_->newLocalTab();
        return;
    }

    // "ask" (default) shows the prompt; "restore"/"new" skip it. The prompt's
    // "Don't ask again" checkbox writes the chosen behaviour into this setting.
    const QString mode = QSettings().value("session/onStartup",
                                           QStringLiteral("ask")).toString();
    bool restore = true;
    if (mode == QLatin1String("restore")) {
        restore = true;
    } else if (mode == QLatin1String("new")) {
        restore = false;
    } else {
        QMessageBox box(QMessageBox::Question, tr("Restore session"),
                        tr("Restore the previous session?"),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::Yes);
        auto *dontAsk = new QCheckBox(tr("Don't ask again"), &box);
        box.setCheckBox(dontAsk);
        restore = (box.exec() == QMessageBox::Yes);
        if (dontAsk->isChecked())
            QSettings().setValue("session/onStartup",
                                 restore ? QStringLiteral("restore") : QStringLiteral("new"));
    }

    if (!restore) {
        area_->newLocalTab();
        return;
    }

    for (const QVariant &v : tabs) {
        const QVariantMap m = v.toMap();
        if (m.value("type").toString() == QLatin1String("ssh")) {
            Session s;
            s.alias = m.value("alias").toString();
            s.hostName = m.value("hostName").toString();
            s.user = m.value("user").toString();
            s.port = m.value("port").toInt();
            s.identityFile = m.value("identityFile").toString();
            s.proxyJump = m.value("proxyJump").toString();
            s.forwardX11 = m.value("forwardX11").toBool();
            s.compression = m.value("compression").toBool();
            area_->newSshTab(s);
        } else {
            area_->newLocalTab();
        }
    }

    if (browsers_.isEmpty()) // nothing usable restored
        area_->newLocalTab();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("ui/dark", dark_);
    settings.setValue("terminal/colorScheme", terminalScheme_);
    settings.setValue("ui/font", terminalFont_.toString());
    settings.setValue("ui/geometry", saveGeometry());
    settings.setValue("ui/state", saveState());

    // Remember the open tabs so we can offer to restore them next launch.
    QVariantList tabs;
    for (auto it = sessionOf_.constBegin(); it != sessionOf_.constEnd(); ++it) {
        const Session &s = it.value();
        QVariantMap m;
        if (s.displayName().isEmpty()) {
            m.insert("type", "local");
        } else {
            m.insert("type", "ssh");
            m.insert("alias", s.alias);
            m.insert("hostName", s.hostName);
            m.insert("user", s.user);
            m.insert("port", s.port);
            m.insert("identityFile", s.identityFile);
            m.insert("proxyJump", s.proxyJump);
            m.insert("forwardX11", s.forwardX11);
            m.insert("compression", s.compression);
        }
        tabs.append(m);
    }
    settings.setValue("state/tabs", tabs);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (sessions_)
        sessions_->stopAllTunnels();
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // When the window becomes active, hand keyboard focus to the terminal so the
    // user can start typing right away without clicking into the console first.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && area_)
        QTimer::singleShot(0, this, [this] { area_->focusCurrentTerminal(); });
}
