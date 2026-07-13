#pragma once

#include <QFont>
#include <QWidget>

#include "sessions/Session.h"

class QTermWidget;
class QLabel;
class QToolButton;

// Thin wrapper around QTermWidget that knows how to launch a local shell or an
// ssh session, and can report whether a given (focused) widget belongs to it.
// It also carries a small header bar (title + close button) that is shown when
// the terminal is one pane of a split view.
class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void startLocalShell();
    void startSsh(const Session &s);

    void sendText(const QString &text);
    void setColorScheme(const QString &name);
    void setTerminalFont(const QFont &font);
    void setTitle(const QString &title);
    void setHeaderVisible(bool visible);
    void applyIcons(); // re-tint the pane's close button after a theme change

    // True if w is this terminal's display (used by the multi-exec filter).
    bool containsWidget(QWidget *w) const;
    void focusTerminal();

signals:
    void finished(TerminalWidget *self);
    void closeRequested(TerminalWidget *self);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void startProgram(const QString &program, const QStringList &args);
    void installShortcutFilter();

    QTermWidget *term_;
    QWidget *header_;
    QLabel *titleLabel_;
    QToolButton *closeButton_;
    QFont font_;
    QString scheme_;
};
