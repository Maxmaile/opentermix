#pragma once

#include <QList>
#include <QObject>

class TerminalWidget;

// Multi-execution ("type into all terminals at once"). When enabled, an
// application-wide event filter mirrors keystrokes from the focused terminal to
// every other registered terminal.
class MultiExec : public QObject {
    Q_OBJECT
public:
    explicit MultiExec(QObject *parent = nullptr);

    void registerTerminal(TerminalWidget *t);
    bool isEnabled() const { return enabled_; }

public slots:
    void setEnabled(bool on);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QList<TerminalWidget *> terminals_;
    bool enabled_ = false;
};
