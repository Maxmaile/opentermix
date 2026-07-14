#include "terminal/TerminalWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProcessEnvironment>
#include <QToolButton>
#include <QVBoxLayout>

#include <qtermwidget.h>

#include "app/Icons.h"
#include "sessions/SshArgs.h"

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
    , term_(new QTermWidget(0, this)) // 0 = do not start a shell yet
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Mini control panel for split panes: title on the left, close on the right.
    header_ = new QWidget(this);
    header_->setObjectName("TerminalHeader");
    auto *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(6, 1, 2, 1);
    headerLayout->setSpacing(4);
    titleLabel_ = new QLabel(tr("Terminal"), header_);
    titleLabel_->setObjectName("TerminalTitle");
    closeButton_ = new QToolButton(header_);
    closeButton_->setObjectName("TerminalClose");
    closeButton_->setAutoRaise(true);
    closeButton_->setToolTip(tr("Close this pane"));
    applyIcons();
    connect(closeButton_, &QToolButton::clicked, this, [this] { emit closeRequested(this); });
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(closeButton_);
    header_->hide(); // only shown when this terminal is part of a split

    layout->addWidget(header_);
    layout->addWidget(term_);

    // Object name lets the stylesheet paint the terminal surface with the same
    // colour the QTermWidget colour scheme uses, so the global QWidget background
    // does not bleed through unpainted rows (which caused a colour shift after
    // selecting empty lines).
    term_->setObjectName(QStringLiteral("TerminalPane"));
    term_->setScrollBarPosition(QTermWidget::ScrollBarRight);
    setColorScheme(QStringLiteral("WhiteOnBlack"));

    connect(term_, &QTermWidget::finished, this, [this] { emit finished(this); });
}

TerminalWidget::~TerminalWidget()
{
    // Stop the QTermWidget from emitting finished() while it is torn down, which
    // would otherwise re-enter the (already dying) tab/group destruction chain.
    if (term_)
        disconnect(term_, nullptr, this, nullptr);
}

void TerminalWidget::applyIcons()
{
    closeButton_->setIcon(Icons::action(QStringLiteral("close")));
}

void TerminalWidget::setTitle(const QString &title)
{
    titleLabel_->setText(title);
}

void TerminalWidget::setHeaderVisible(bool visible)
{
    header_->setVisible(visible);
}

void TerminalWidget::startProgram(const QString &program, const QStringList &args)
{
    term_->setShellProgram(program);
    term_->setArgs(args);
    // setEnvironment() replaces the child's entire environment rather than
    // augmenting it, so build from the real environment (PATH, HOME,
    // SSH_AUTH_SOCK, ...) and only override TERM.
    QStringList env;
    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    const QStringList keys = sysEnv.keys();
    env.reserve(keys.size() + 1);
    for (const QString &key : keys) {
        if (key != QStringLiteral("TERM"))
            env << key + QLatin1Char('=') + sysEnv.value(key);
    }
    env << QStringLiteral("TERM=xterm-256color");
    term_->setEnvironment(env);
    term_->startShellProgram();
    // QTermWidget can fall back to its default font while the shell starts, so
    // re-apply the requested font once the session is up.
    if (!font_.family().isEmpty())
        term_->setTerminalFont(font_);
    if (!scheme_.isEmpty())
        term_->setColorScheme(scheme_);
    installShortcutFilter();
    term_->setFocus();
}

void TerminalWidget::installShortcutFilter()
{
    // QTermWidget's display accepts ShortcutOverride for almost every key combo,
    // so plain QShortcuts never fire and Ctrl+Shift+C reaches the shell as ^C.
    // We intercept key presses directly on the display to provide the usual
    // terminal shortcuts (copy/paste) while leaving everything else untouched.
    term_->installEventFilter(this);
    const QList<QWidget *> children = term_->findChildren<QWidget *>();
    for (QWidget *w : children)
        w->installEventFilter(this);
}

bool TerminalWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool ctrlShift = (ke->modifiers() & Qt::ControlModifier)
                               && (ke->modifiers() & Qt::ShiftModifier);
        if (ctrlShift && ke->key() == Qt::Key_C) {
            term_->copyClipboard();
            return true;
        }
        if (ctrlShift && ke->key() == Qt::Key_V) {
            term_->pasteClipboard();
            return true;
        }
        // Konsole-style extras that mirror common terminals.
        if (ke->modifiers() == Qt::ShiftModifier && ke->key() == Qt::Key_Insert) {
            term_->pasteClipboard();
            return true;
        }
        if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_Insert) {
            term_->copyClipboard();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TerminalWidget::startLocalShell()
{
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty())
        shell = QStringLiteral("/bin/bash");
    setTitle(tr("Local"));
    startProgram(shell, {});
}

void TerminalWidget::startSsh(const Session &s)
{
    QStringList args = SshArgs::gatewayArgs(s);
    if (s.forwardX11)
        args << "-X";
    args << SshArgs::targetArg(s);

    setTitle(s.displayName());
    startProgram(QStringLiteral("ssh"), args);
}

void TerminalWidget::sendText(const QString &text)
{
    term_->sendText(text);
}

void TerminalWidget::setColorScheme(const QString &name)
{
    scheme_ = name.isEmpty() ? QStringLiteral("WhiteOnBlack") : name;
    term_->setColorScheme(scheme_);
}

void TerminalWidget::setTerminalFont(const QFont &font)
{
    font_ = font;
    term_->setTerminalFont(font);
}

bool TerminalWidget::containsWidget(QWidget *w) const
{
    return w && (w == term_ || term_->isAncestorOf(w));
}

void TerminalWidget::focusTerminal()
{
    term_->setFocus();
}
