#include "terminal/TerminalArea.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

#include "terminal/TerminalGroup.h"
#include "terminal/TerminalWidget.h"

TerminalArea::TerminalArea(MultiExec *multiExec, QWidget *parent)
    : QWidget(parent)
    , multiExec_(multiExec)
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);

    // Empty-state placeholder with a single "open terminal" button.
    placeholder_ = new QWidget(this);
    auto *placeholderLayout = new QVBoxLayout(placeholder_);
    placeholderLayout->addStretch();
    auto *hint = new QLabel(tr("No open terminals"), placeholder_);
    hint->setObjectName("PlaceholderHint");
    hint->setAlignment(Qt::AlignCenter);
    auto *openButton = new QPushButton(tr("Open terminal"), placeholder_);
    openButton->setObjectName("PlaceholderButton");
    connect(openButton, &QPushButton::clicked, this, [this] { newLocalTab(); });
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(openButton);
    buttonRow->addStretch();
    placeholderLayout->addWidget(hint);
    placeholderLayout->addSpacing(10);
    placeholderLayout->addLayout(buttonRow);
    placeholderLayout->addStretch();

    central_ = placeholder_;
    layout_->addWidget(placeholder_);

    connect(qApp, &QApplication::focusChanged, this, &TerminalArea::onFocusChanged);
}

TerminalArea::~TerminalArea()
{
    // Child groups are destroyed by the base ~QWidget after our members (groups_)
    // are already gone, so cut their signals to us first to avoid use-after-free
    // in the destroyed()/focusChanged() handlers during teardown.
    disconnect(qApp, nullptr, this, nullptr);
    for (TerminalGroup *g : groups_)
        disconnect(g, nullptr, this, nullptr);
}

TerminalGroup *TerminalArea::createGroup()
{
    auto *g = new TerminalGroup(multiExec_, this);
    g->applyColorScheme(scheme_);
    g->applyFont(font_);

    connect(g, &TerminalGroup::newTabRequested, this, [this](TerminalGroup *grp) {
        setActive(grp);
        grp->addLocalTerminal();
    });
    connect(g, &TerminalGroup::splitRequested, this,
            [this](TerminalGroup *grp, Qt::Orientation o) {
                setActive(grp);
                splitGroup(grp, o);
            });
    connect(g, &TerminalGroup::activated, this, [this](TerminalGroup *grp) { setActive(grp); });
    connect(g, &TerminalGroup::emptied, this, [this](TerminalGroup *grp) { removeGroup(grp); });
    connect(g, &TerminalGroup::terminalOpened, this,
            [this](TerminalWidget *t, Session s) { emit terminalOpened(t, s); });
    connect(g, &TerminalGroup::terminalClosing, this,
            [this](TerminalWidget *t) { emit terminalClosing(t); });
    connect(g, &TerminalGroup::tunnelsTabClosing, this, [this] { emit tunnelsTabClosing(); });
    connect(g, &TerminalGroup::currentTabChanged, this, [this, g] {
        if (g == active_)
            emitCurrentTerminal();
    });
    connect(g, &QObject::destroyed, this, [this](QObject *o) {
        groups_.removeAll(static_cast<TerminalGroup *>(o));
    });

    groups_.append(g);
    return g;
}

TerminalGroup *TerminalArea::ensureActiveGroup()
{
    if (active_ && groups_.contains(active_))
        return active_;
    if (!groups_.isEmpty()) {
        setActive(groups_.first());
        return active_;
    }
    auto *g = createGroup();
    setCentral(g);
    setActive(g);
    return g;
}

TerminalWidget *TerminalArea::newLocalTab()
{
    return ensureActiveGroup()->addLocalTerminal();
}

TerminalWidget *TerminalArea::newSshTab(const Session &s)
{
    return ensureActiveGroup()->addSshTerminal(s);
}

void TerminalArea::openContentTab(QWidget *content, const QString &title)
{
    ensureActiveGroup()->addContentTab(content, title);
}

void TerminalArea::closeContentTab(QWidget *content)
{
    // Same "walk up to the owning QTabWidget" approach MainWindow::openSettings()
    // uses to re-find an already-open tab; here it's used to remove one
    // programmatically (e.g. the last tunnel stopped) without going through
    // TerminalGroup::handleClose()'s user-facing close confirmation.
    for (QWidget *p = content->parentWidget(); p; p = p->parentWidget()) {
        if (auto *tabs = qobject_cast<QTabWidget *>(p)) {
            const int idx = tabs->indexOf(content);
            if (idx >= 0) {
                tabs->removeTab(idx);
                content->deleteLater();
            }
            break;
        }
    }
}

void TerminalArea::splitActive(Qt::Orientation orientation)
{
    splitGroup(ensureActiveGroup(), orientation);
}

void TerminalArea::splitGroup(TerminalGroup *g, Qt::Orientation orientation)
{
    auto *newGroup = createGroup();
    newGroup->addLocalTerminal();

    QWidget *parent = g->parentWidget();
    if (parent == this) {
        auto *sp = new QSplitter(orientation, this);
        sp->setChildrenCollapsible(false);
        central_ = nullptr; // g is about to be reparented out of the layout
        sp->addWidget(g);
        sp->addWidget(newGroup);
        sp->setSizes({1000, 1000});
        setCentral(sp);
    } else if (auto *psp = qobject_cast<QSplitter *>(parent)) {
        const int idx = psp->indexOf(g);
        auto *sp = new QSplitter(orientation);
        sp->setChildrenCollapsible(false);
        psp->insertWidget(idx, sp);
        sp->addWidget(g);
        sp->addWidget(newGroup);
        sp->setSizes({1000, 1000});
    }
    setActive(newGroup);
}

void TerminalArea::removeGroup(TerminalGroup *g)
{
    groups_.removeAll(g);
    if (active_ == g)
        active_ = nullptr;

    QWidget *parent = g->parentWidget();

    if (parent == this) {
        g->deleteLater();
        showPlaceholder();
        return;
    }

    auto *psp = qobject_cast<QSplitter *>(parent);
    if (!psp) {
        g->deleteLater();
        return;
    }

    QWidget *sibling = nullptr;
    for (int i = 0; i < psp->count(); ++i) {
        if (psp->widget(i) != g) {
            sibling = psp->widget(i);
            break;
        }
    }

    QWidget *grand = psp->parentWidget();
    g->setParent(nullptr);
    g->deleteLater();

    if (!sibling) {
        psp->deleteLater();
        showPlaceholder();
        return;
    }

    if (grand == this) {
        setCentral(sibling);
        psp->deleteLater();
    } else if (auto *gsp = qobject_cast<QSplitter *>(grand)) {
        const int idx = gsp->indexOf(psp);
        gsp->insertWidget(idx, sibling);
        psp->deleteLater();
    }

    setActive(firstGroupIn(sibling));
}

TerminalGroup *TerminalArea::firstGroupIn(QWidget *w) const
{
    if (auto *g = qobject_cast<TerminalGroup *>(w))
        return g;
    if (auto *sp = qobject_cast<QSplitter *>(w)) {
        for (int i = 0; i < sp->count(); ++i) {
            if (TerminalGroup *g = firstGroupIn(sp->widget(i)))
                return g;
        }
    }
    return nullptr;
}

void TerminalArea::setActive(TerminalGroup *g)
{
    if (active_ == g)
        return;
    active_ = g;
    for (TerminalGroup *grp : groups_) {
        grp->setProperty("activeGroup", grp == g);
        grp->style()->unpolish(grp);
        grp->style()->polish(grp);
    }
    emitCurrentTerminal();
}

TerminalWidget *TerminalArea::currentTerminal() const
{
    if (!active_)
        return nullptr;
    return qobject_cast<TerminalWidget *>(active_->currentWidget());
}

void TerminalArea::emitCurrentTerminal()
{
    emit currentTerminalChanged(currentTerminal());
}

void TerminalArea::setCentral(QWidget *w)
{
    if (central_ == w)
        return;
    if (central_)
        layout_->removeWidget(central_);
    if (central_ == placeholder_)
        placeholder_->hide();
    central_ = w;
    layout_->addWidget(w);
    w->show();
}

void TerminalArea::showPlaceholder()
{
    setCentral(placeholder_);
    placeholder_->show();
    setActive(nullptr);
}

void TerminalArea::onFocusChanged(QWidget *, QWidget *now)
{
    QWidget *w = now;
    while (w) {
        if (auto *g = qobject_cast<TerminalGroup *>(w)) {
            if (groups_.contains(g))
                setActive(g);
            return;
        }
        w = w->parentWidget();
    }
}

void TerminalArea::applyIcons()
{
    for (TerminalGroup *g : groups_)
        g->applyIcons();
}

void TerminalArea::setColorScheme(const QString &scheme)
{
    scheme_ = scheme;
    for (TerminalGroup *g : groups_)
        g->applyColorScheme(scheme);
}

void TerminalArea::setTerminalFont(const QFont &font)
{
    font_ = font;
    for (TerminalGroup *g : groups_)
        g->applyFont(font);
}

void TerminalArea::focusCurrentTerminal()
{
    if (auto *t = currentTerminal())
        t->focusTerminal();
}
