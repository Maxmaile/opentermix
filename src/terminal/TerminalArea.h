#pragma once

#include <QFont>
#include <QList>
#include <QWidget>

#include "sessions/Session.h"

class MultiExec;
class TerminalGroup;
class TerminalWidget;
class QVBoxLayout;

// Central area that manages VS Code-like terminal groups arranged in a tree of
// splitters. When empty it shows a placeholder with a button to open a terminal.
class TerminalArea : public QWidget {
    Q_OBJECT
public:
    explicit TerminalArea(MultiExec *multiExec, QWidget *parent = nullptr);
    ~TerminalArea() override;

    TerminalWidget *newLocalTab();
    TerminalWidget *newSshTab(const Session &s);
    void openContentTab(QWidget *content, const QString &title);
    void closeContentTab(QWidget *content); // programmatic removal, bypasses any close confirmation
    void splitActive(Qt::Orientation orientation);

    void setColorScheme(const QString &scheme);
    void setTerminalFont(const QFont &font);
    void focusCurrentTerminal();
    void applyIcons(); // re-tint tab-bar/pane icons in every group after a theme change

signals:
    void terminalOpened(TerminalWidget *terminal, Session session);
    void terminalClosing(TerminalWidget *terminal);
    void currentTerminalChanged(TerminalWidget *terminal);
    void tunnelsTabClosing();

private slots:
    void onFocusChanged(QWidget *old, QWidget *now);

private:
    TerminalGroup *ensureActiveGroup();
    TerminalGroup *createGroup();
    void splitGroup(TerminalGroup *g, Qt::Orientation orientation);
    void removeGroup(TerminalGroup *g);
    void setActive(TerminalGroup *g);
    void setCentral(QWidget *w);
    void showPlaceholder();
    TerminalGroup *firstGroupIn(QWidget *w) const;
    TerminalWidget *currentTerminal() const;
    void emitCurrentTerminal();

    MultiExec *multiExec_;
    QVBoxLayout *layout_;
    QWidget *central_ = nullptr;
    QWidget *placeholder_ = nullptr;
    TerminalGroup *active_ = nullptr;
    QList<TerminalGroup *> groups_;
    QString scheme_;
    QFont font_;
};
