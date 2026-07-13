#include "terminal/TerminalGroup.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QTabBar>
#include <QToolButton>

#include "app/Icons.h"
#include "terminal/MultiExec.h"
#include "terminal/TerminalWidget.h"

TerminalGroup::TerminalGroup(MultiExec *multiExec, QWidget *parent)
    : QTabWidget(parent)
    , multiExec_(multiExec)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    // New-tab and split controls on the right of the tab bar (VS Code style).
    auto *corner = new QWidget(this);
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(2, 0, 2, 0);
    cornerLayout->setSpacing(0);

    newButton_ = new QToolButton(corner);
    newButton_->setObjectName("TabNewButton");
    newButton_->setAutoRaise(true);
    newButton_->setFixedSize(26, 24);
    newButton_->setToolTip(tr("New terminal"));
    connect(newButton_, &QToolButton::clicked, this, [this] { emit newTabRequested(this); });

    splitButton_ = new QToolButton(corner);
    splitButton_->setObjectName("TabSplitButton");
    splitButton_->setAutoRaise(true);
    splitButton_->setFixedSize(26, 24);
    splitButton_->setToolTip(tr("Split"));
    splitButton_->setPopupMode(QToolButton::InstantPopup);
    auto *splitMenu = new QMenu(splitButton_);
    splitMenu->addAction(tr("Split Right"), this,
                         [this] { emit splitRequested(this, Qt::Horizontal); });
    splitMenu->addAction(tr("Split Down"), this,
                         [this] { emit splitRequested(this, Qt::Vertical); });
    splitButton_->setMenu(splitMenu);

    clearButton_ = new QToolButton(corner);
    clearButton_->setObjectName("TabClearButton");
    clearButton_->setAutoRaise(true);
    clearButton_->setFixedSize(26, 24);
    clearButton_->setToolTip(tr("Clear terminal"));
    connect(clearButton_, &QToolButton::clicked, this, [this] {
        if (auto *t = qobject_cast<TerminalWidget *>(currentWidget()))
            t->sendText(QStringLiteral("clear\n"));
    });

    applyIcons();

    cornerLayout->addWidget(newButton_);
    cornerLayout->addWidget(splitButton_);
    cornerLayout->addWidget(clearButton_);
    setCornerWidget(corner, Qt::TopRightCorner);

    connect(this, &QTabWidget::tabCloseRequested, this, &TerminalGroup::handleClose);
    connect(this, &QTabWidget::currentChanged, this, [this](int) { emit currentTabChanged(); });

    // Double-clicking an empty spot on the tab bar opens a new terminal (VS Code).
    tabBar()->installEventFilter(this);
}

bool TerminalGroup::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == tabBar() && event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && tabBar()->tabAt(me->pos()) == -1) {
            emit newTabRequested(this);
            return true;
        }
    }
    return QTabWidget::eventFilter(obj, event);
}

TerminalWidget *TerminalGroup::addLocalTerminal()
{
    auto *t = new TerminalWidget(this);
    if (multiExec_)
        multiExec_->registerTerminal(t);
    if (!scheme_.isEmpty())
        t->setColorScheme(scheme_);
    if (!font_.family().isEmpty())
        t->setTerminalFont(font_);
    connect(t, &TerminalWidget::finished, this, [this](TerminalWidget *self) {
        const int i = indexOf(self);
        if (i >= 0)
            handleClose(i);
    });
    emit terminalOpened(t, Session{}); // empty session => local filesystem
    const int idx = addTab(t, tr("Local"));
    setCurrentIndex(idx);
    t->startLocalShell();
    emit activated(this);
    return t;
}

TerminalWidget *TerminalGroup::addSshTerminal(const Session &s)
{
    auto *t = new TerminalWidget(this);
    if (multiExec_)
        multiExec_->registerTerminal(t);
    if (!scheme_.isEmpty())
        t->setColorScheme(scheme_);
    if (!font_.family().isEmpty())
        t->setTerminalFont(font_);
    connect(t, &TerminalWidget::finished, this, [this](TerminalWidget *self) {
        const int i = indexOf(self);
        if (i >= 0)
            handleClose(i);
    });
    emit terminalOpened(t, s);
    // Configured host -> its config name (alias); otherwise the hostname we dial.
    const int idx = addTab(t, s.displayName());
    setCurrentIndex(idx);
    t->startSsh(s);
    emit activated(this);
    return t;
}

void TerminalGroup::addContentTab(QWidget *content, const QString &title)
{
    const int idx = addTab(content, title);
    setCurrentIndex(idx);
    emit activated(this);
}

void TerminalGroup::handleClose(int index)
{
    QWidget *w = widget(index);
    if (auto *t = qobject_cast<TerminalWidget *>(w))
        emit terminalClosing(t);
    removeTab(index);
    if (w)
        w->deleteLater();
    if (count() == 0)
        emit emptied(this);
}

void TerminalGroup::applyColorScheme(const QString &scheme)
{
    scheme_ = scheme;
    for (int i = 0; i < count(); ++i) {
        if (auto *t = qobject_cast<TerminalWidget *>(widget(i)))
            t->setColorScheme(scheme);
    }
}

void TerminalGroup::applyFont(const QFont &font)
{
    font_ = font;
    for (int i = 0; i < count(); ++i) {
        if (auto *t = qobject_cast<TerminalWidget *>(widget(i)))
            t->setTerminalFont(font);
    }
}

void TerminalGroup::applyIcons()
{
    newButton_->setIcon(Icons::action(QStringLiteral("tab-new")));
    splitButton_->setIcon(Icons::action(QStringLiteral("split")));
    clearButton_->setIcon(Icons::action(QStringLiteral("clear")));
    for (int i = 0; i < count(); ++i) {
        if (auto *t = qobject_cast<TerminalWidget *>(widget(i)))
            t->applyIcons();
    }
}
