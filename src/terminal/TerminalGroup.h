#pragma once

#include <QFont>
#include <QTabWidget>

#include "sessions/Session.h"

class MultiExec;
class TerminalWidget;
class QToolButton;

// A VS Code-like "editor group": a tab bar with its own content, plus buttons on
// the right side of the tab bar to open a new terminal or split the group.
class TerminalGroup : public QTabWidget {
    Q_OBJECT
public:
    explicit TerminalGroup(MultiExec *multiExec, QWidget *parent = nullptr);

    TerminalWidget *addLocalTerminal();
    TerminalWidget *addSshTerminal(const Session &s);
    void addContentTab(QWidget *content, const QString &title);

    void applyColorScheme(const QString &scheme);
    void applyFont(const QFont &font);
    void applyIcons(); // re-tint the tab-bar buttons + panes after a theme change

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void newTabRequested(TerminalGroup *self);
    void splitRequested(TerminalGroup *self, Qt::Orientation orientation);
    void activated(TerminalGroup *self);
    void emptied(TerminalGroup *self);
    void terminalOpened(TerminalWidget *terminal, Session session);
    void terminalClosing(TerminalWidget *terminal);
    void currentTabChanged();

private slots:
    void handleClose(int index);

private:
    MultiExec *multiExec_;
    QString scheme_;
    QFont font_;
    QToolButton *newButton_ = nullptr;
    QToolButton *splitButton_ = nullptr;
    QToolButton *clearButton_ = nullptr;
};
