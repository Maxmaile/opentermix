#pragma once

#include <QFont>
#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include "sessions/Session.h"

class MultiExec;
class SessionPanel;
class SettingsWidget;
class SftpBrowserWidget;
class TerminalArea;
class TerminalWidget;
class TunnelsTabWidget;
class QAction;
class QDockWidget;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onConnectRequested(const Session &s);
    void onSftpRequested(const Session &s);
    void onTerminalOpened(TerminalWidget *terminal, const Session &session);
    void onTerminalClosing(TerminalWidget *terminal);
    void onCurrentTerminalChanged(TerminalWidget *terminal);
    void openExternalTerminal();
    void openSettings();
    void chooseFont();
    void toggleTheme(bool dark);
    void changeColorScheme(const QString &scheme);
    void changeLanguage(const QString &code);
    void about();
    void onTunnelsChanged();
    void onTunnelsTabClosing();

private:
    void createActions();
    void createDocks();
    void applyTheme();
    QString resolvedScheme() const;
    void readSettings();
    void restoreLayout();
    void restoreOrOpenTabs();
    void saveSettings();

    TerminalArea *area_;
    SessionPanel *sessions_;
    QStackedWidget *filesStack_;
    QWidget *filesPlaceholder_;
    QHash<TerminalWidget *, SftpBrowserWidget *> browsers_;
    QHash<TerminalWidget *, Session> sessionOf_;
    MultiExec *multiExec_;
    QDockWidget *sessionDock_;
    QDockWidget *sftpDock_;
    QAction *broadcastAction_ = nullptr;
    QAction *darkAction_ = nullptr;
    QPointer<SettingsWidget> settingsWidget_; // the one open Settings tab, if any
    QPointer<TunnelsTabWidget> tunnelsTab_;   // the one open Tunnels tab, if any
    bool dark_ = true;
    QString terminalScheme_;
    QFont terminalFont_;
};
