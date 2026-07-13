#include "terminal/MultiExec.h"

#include <QApplication>
#include <QKeyEvent>

#include "terminal/TerminalWidget.h"

MultiExec::MultiExec(QObject *parent)
    : QObject(parent)
{
}

void MultiExec::registerTerminal(TerminalWidget *t)
{
    if (!t || terminals_.contains(t))
        return;
    terminals_.append(t);
    connect(t, &QObject::destroyed, this, [this](QObject *o) {
        terminals_.removeAll(static_cast<TerminalWidget *>(o));
    });
}

void MultiExec::setEnabled(bool on)
{
    if (on == enabled_)
        return;
    enabled_ = on;
    if (enabled_)
        qApp->installEventFilter(this);
    else
        qApp->removeEventFilter(this);
}

bool MultiExec::eventFilter(QObject *obj, QEvent *event)
{
    if (enabled_ && event->type() == QEvent::KeyPress) {
        QWidget *focus = QApplication::focusWidget();
        if (focus) {
            TerminalWidget *source = nullptr;
            for (TerminalWidget *t : terminals_) {
                if (t->containsWidget(focus)) {
                    source = t;
                    break;
                }
            }
            if (source) {
                const QString text = static_cast<QKeyEvent *>(event)->text();
                if (!text.isEmpty()) {
                    // Snapshot: sendText() must not observe terminals_ being
                    // mutated (via the destroyed() -> removeAll() handler)
                    // while this loop is still iterating over it.
                    const QList<TerminalWidget *> targets = terminals_;
                    for (TerminalWidget *t : targets) {
                        if (t != source)
                            t->sendText(text);
                    }
                }
            }
        }
    }
    return QObject::eventFilter(obj, event);
}
