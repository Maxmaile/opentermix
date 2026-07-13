#include "sftp/SftpBrowserWidget.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>

#include <dirent.h>
#include <sys/stat.h>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QLocale>
#include <QMimeData>
#include <QSet>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "app/Icons.h"

namespace {
const int NameRole = Qt::UserRole + 1;
const int IsDirRole = Qt::UserRole + 2;
const int IsUpRole = Qt::UserRole + 3;
const int ActionColumn = 3;

// Wrap an arbitrary path in single quotes so the shell treats it literally,
// escaping any embedded single quote the POSIX-safe way ('\'').
QString shellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1String("'"), QLatin1String("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// Content-based "is this text?" check, the same heuristic file(1)/libmagic use:
// read a prefix and reject it as binary if it holds a NUL byte or too many
// control characters. Cheap for local files (a few KB read); not used for
// remote entries, where we would have to fetch the file to look inside.
bool looksLikeTextFile(const QString &absPath)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(4096);
    if (head.isEmpty())
        return true; // empty file: trivially text

    int suspicious = 0;
    for (unsigned char c : head) {
        if (c == 0)
            return false; // NUL => binary
        const bool printable = (c >= 0x20 && c < 0x7f)     // printable ASCII
                               || c == '\t' || c == '\n' || c == '\r'
                               || c == '\f' || c == '\v'
                               || c >= 0x80;                // assume UTF-8 lead/continuation
        if (!printable)
            ++suspicious;
    }
    return suspicious * 100 <= head.size() * 30; // tolerate <= 30% control bytes
}

// Tree item with mc-like ordering: the "up" entry is always first, directories
// come before files, and both hold regardless of the sort column/order.
class FsItem : public QTreeWidgetItem {
public:
    explicit FsItem(QTreeWidget *parent) : QTreeWidgetItem(parent) {}

    bool operator<(const QTreeWidgetItem &other) const override
    {
        Qt::SortOrder order = Qt::AscendingOrder;
        if (const QTreeWidget *tw = treeWidget())
            order = tw->header()->sortIndicatorOrder();

        const bool thisUp = data(0, IsUpRole).toBool();
        const bool otherUp = other.data(0, IsUpRole).toBool();
        if (thisUp || otherUp) {
            if (thisUp && !otherUp)
                return order == Qt::AscendingOrder;
            if (!thisUp && otherUp)
                return order == Qt::DescendingOrder;
            return false;
        }

        const bool thisDir = data(0, IsDirRole).toBool();
        const bool otherDir = other.data(0, IsDirRole).toBool();
        if (thisDir != otherDir)
            return order == Qt::AscendingOrder ? thisDir : otherDir;

        return QTreeWidgetItem::operator<(other);
    }
};
} // namespace

SftpBrowserWidget::SftpBrowserWidget(QWidget *parent)
    : QWidget(parent)
    , thread_(new QThread(this))
    , client_(new SftpClient)
    , tree_(new QTreeWidget(this))
    , pathEdit_(new QLineEdit(this))
    , status_(new QLabel(this))
{
    setAcceptDrops(true);

    client_->moveToThread(thread_);
    connect(thread_, &QThread::finished, client_, &QObject::deleteLater);

    // GUI -> worker
    connect(this, &SftpBrowserWidget::requestConnect, client_, &SftpClient::connectToHost);
    connect(this, &SftpBrowserWidget::requestList, client_, &SftpClient::listDir);
    connect(this, &SftpBrowserWidget::requestDownload, client_, &SftpClient::download);
    connect(this, &SftpBrowserWidget::requestUpload, client_, &SftpClient::upload);
    connect(this, &SftpBrowserWidget::requestDisconnect, client_, &SftpClient::disconnectFromHost);

    // worker -> GUI
    connect(client_, &SftpClient::connected, this, &SftpBrowserWidget::onConnected);
    connect(client_, &SftpClient::listed, this, &SftpBrowserWidget::onListed);
    connect(client_, &SftpClient::error, this, &SftpBrowserWidget::onError);
    connect(client_, &SftpClient::info, this, &SftpBrowserWidget::onInfo);
    connect(client_, &SftpClient::transferProgress, this,
            [this](const QString &name, qint64 done, qint64 total) {
                status_->setText(tr("%1: %2 / %3")
                                     .arg(name,
                                          QLocale::c().formattedDataSize(done),
                                          total > 0 ? QLocale::c().formattedDataSize(total)
                                                    : tr("?")));
            });
    connect(client_, &SftpClient::transferFinished, this,
            [this](const QString &name, bool ok) {
                status_->setText(ok ? tr("Transferred %1").arg(name)
                                    : tr("Failed: %1").arg(name));
                if (ok)
                    refresh();
            });

    thread_->start();

    auto *toolbar = new QToolBar(this);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    upAction_ = toolbar->addAction(tr("Up"), this, &SftpBrowserWidget::goUp);
    refreshAction_ = toolbar->addAction(tr("Refresh"), this, &SftpBrowserWidget::refresh);
    downloadAction_ = toolbar->addAction(tr("Download"), this, &SftpBrowserWidget::downloadSelected);
    hiddenAction_ = toolbar->addAction(tr("Show hidden files"));
    hiddenAction_->setCheckable(true);
    hiddenAction_->setChecked(showHidden_);
    hiddenAction_->setToolTip(tr("Show hidden files"));
    upAction_->setToolTip(tr("Up"));
    refreshAction_->setToolTip(tr("Refresh"));
    downloadAction_->setToolTip(tr("Download"));
    connect(hiddenAction_, &QAction::toggled, this, [this](bool on) {
        showHidden_ = on;
        applyIcons(); // eye <-> eye-slash
        refresh();
    });
    applyIcons();

    pathEdit_->setReadOnly(true);

    tree_->setObjectName(QStringLiteral("FileBrowserTree"));
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified"), QString()});
    tree_->setRootIsDecorated(false);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setSortingEnabled(true);
    // Name and Size stay user-resizable (draggable); Modified fills the leftover
    // width like before; the tiny action column just hugs its button.
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(ActionColumn, QHeaderView::ResizeToContents);
    tree_->setColumnWidth(0, 240);
    tree_->setColumnWidth(1, 90);
    connect(tree_, &QTreeWidget::itemActivated, this, &SftpBrowserWidget::onItemActivated);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(toolbar);
    layout->addWidget(pathEdit_);
    layout->addWidget(tree_);
    layout->addWidget(status_);

    // Show the local filesystem right away, starting at the user's home directory.
    browseLocal(QDir::homePath());
}

SftpBrowserWidget::~SftpBrowserWidget()
{
    // Ask any in-flight transfer to stop so it returns to the worker thread's
    // event loop promptly instead of blocking quit()/wait() below - and the
    // GUI thread with it - for the remainder of a large download/upload.
    client_->requestCancel();
    thread_->quit();
    thread_->wait();
}

void SftpBrowserWidget::applyIcons()
{
    upAction_->setIcon(Icons::action(QStringLiteral("up")));
    refreshAction_->setIcon(Icons::action(QStringLiteral("refresh")));
    downloadAction_->setIcon(Icons::action(QStringLiteral("download")));
    // Eye when hidden files are shown, eye-with-slash when they are filtered out.
    hiddenAction_->setIcon(Icons::action(showHidden_ ? QStringLiteral("visible")
                                                     : QStringLiteral("hidden")));
}

void SftpBrowserWidget::connectTo(const Session &s)
{
    localMode_ = false;
    connected_ = false;
    status_->setText(tr("Connecting to %1...").arg(s.displayName()));
    tree_->clear();
    emit requestConnect(s);
}

QString SftpBrowserWidget::joinPath(const QString &dir, const QString &name) const
{
    if (dir.endsWith('/'))
        return dir + name;
    return dir + '/' + name;
}

void SftpBrowserWidget::navigate(const QString &path)
{
    if (localMode_) {
        browseLocal(path);
        return;
    }
    cwd_ = path;
    pathEdit_->setText(path);
    emit requestList(path);
}

void SftpBrowserWidget::browseLocal(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        dir = QDir(QDir::homePath());
    const QString abs = dir.absolutePath();
    const QByteArray absBytes = abs.toUtf8();

    QVector<SftpEntry> entries;

    // Use POSIX readdir + QString::fromUtf8 instead of QDir::entryInfoList
    // to work around a Qt bug where the latter silently stops iterating after
    // encountering a filename with invalid UTF-8 (e.g. bytes 0xFF).
    DIR *d = opendir(absBytes.constData());
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            const QString name = QString::fromUtf8(de->d_name);
            const QFileInfo fi(abs + QLatin1Char('/') + name);

            SftpEntry e;
            e.name = name;
            e.isDir = fi.isDir();
            e.size = fi.size();
            e.mtime = static_cast<quint32>(fi.lastModified().toSecsSinceEpoch());
            entries.append(e);
        }
        closedir(d);
    }

    renderEntries(abs, entries);
    status_->setText(tr("Local: %1").arg(abs));
}

void SftpBrowserWidget::renderEntries(const QString &path, const QVector<SftpEntry> &entries)
{
    cwd_ = path;
    pathEdit_->setText(path);

    tree_->setSortingEnabled(false);
    tree_->clear();

    // mc-style "go up" entry, pinned first by FsItem regardless of sort column.
    if (path != QStringLiteral("/")) {
        auto *up = new FsItem(tree_);
        up->setText(0, QStringLiteral("/.."));
        up->setData(0, NameRole, QStringLiteral(".."));
        up->setData(0, IsDirRole, true);
        up->setData(0, IsUpRole, true);
    }

    // Text files get a "cat" button in the action column; collected here and
    // attached after the sort so the widgets land on their final rows.
    QVector<QPair<QTreeWidgetItem *, QString>> catButtons;

    for (const SftpEntry &e : entries) {
        if (e.name == QStringLiteral(".") || e.name == QStringLiteral(".."))
            continue;
        if (!showHidden_ && e.name.startsWith(QLatin1Char('.')))
            continue;
        auto *item = new FsItem(tree_);
        // Directories are prefixed with '/' so they read as folders.
        item->setText(0, (e.isDir ? QStringLiteral("/") : QString()) + e.name);
        // File sizes are always shown in English units, independent of locale.
        item->setText(1, e.isDir ? QString() : QLocale::c().formattedDataSize(e.size));
        item->setText(2, e.mtime ? QDateTime::fromSecsSinceEpoch(e.mtime).toString(
                                       QStringLiteral("dd:MM:yyyy HH:mm"))
                                 : QString());
        item->setData(0, NameRole, e.name);
        item->setData(0, IsDirRole, e.isDir);

        if (!e.isDir) {
            const QString full = joinPath(path, e.name);
            // Local files: sniff their contents. Remote files: fall back to the
            // extension heuristic (looking inside would mean downloading them).
            const bool textFile = localMode_ ? looksLikeTextFile(full)
                                             : isTextFile(e.name);
            if (textFile)
                catButtons.append({item, full});
        }
    }
    tree_->setSortingEnabled(true);
    tree_->sortItems(0, Qt::AscendingOrder);

    for (const auto &pair : catButtons) {
        const QString fullPath = pair.second;
        // File-preview icon (breeze-dark), theme-independent; runs cat on click.
        auto *button = new QToolButton(tree_);
        button->setIcon(Icons::colored(QStringLiteral("view-file")));
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("Show this file with \"cat\" in the terminal"));
        connect(button, &QToolButton::clicked, this, [this, fullPath] {
            emit runInTerminal(QStringLiteral("cat ") + shellQuote(fullPath)
                               + QLatin1Char('\n'));
        });
        tree_->setItemWidget(pair.first, ActionColumn, button);
    }
}

bool SftpBrowserWidget::isTextFile(const QString &name) const
{
    // Extension-based heuristic: cheap and works identically for local and
    // remote listings (we cannot sniff remote file contents cheaply).
    static const QSet<QString> textSuffixes = {
        "txt",  "log",  "md",   "rst",  "conf", "cfg",  "ini",  "cnf",
        "sh",   "bash", "zsh",  "py",   "pl",   "rb",   "php",  "lua",
        "c",    "h",    "cpp",  "cc",   "cxx",  "hpp",  "hh",   "java",
        "js",   "ts",   "jsx",  "tsx",  "go",   "rs",   "sql",  "css",
        "html", "htm",  "xml",  "json", "yaml", "yml",  "toml", "csv",
        "env",  "list", "service", "desktop", "cmake", "make", "mk",
        "gitignore", "patch", "diff", "tex", "properties",
    };
    // Some well-known files carry no extension but are always text.
    static const QSet<QString> textNames = {
        "readme",    "license",   "licence",   "copying",   "authors",
        "changelog", "makefile",  "dockerfile", "cmakelists.txt",
        "todo",      "notes",     "install",   "news",
    };

    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && textSuffixes.contains(name.mid(dot + 1).toLower()))
        return true;
    return textNames.contains(name.toLower());
}

void SftpBrowserWidget::onConnected(QString home)
{
    connected_ = true;
    status_->setText(tr("Connected. Home: %1").arg(home));
    // Open the user's home directory on connect.
    navigate(home.isEmpty() ? QStringLiteral("/") : home);
}

void SftpBrowserWidget::onListed(QString path, QVector<SftpEntry> entries)
{
    renderEntries(path, entries);
}

void SftpBrowserWidget::onError(QString message)
{
    status_->setText(message);
}

void SftpBrowserWidget::onInfo(QString message)
{
    status_->setText(message);
}

void SftpBrowserWidget::onItemActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const QString name = item->data(0, NameRole).toString();
    const bool isDir = item->data(0, IsDirRole).toBool();
    if (name == QStringLiteral("..")) {
        goUp();
        return;
    }
    if (isDir) {
        navigate(joinPath(cwd_, name));
        return;
    }
    if (!localMode_)
        downloadSelected();
}

void SftpBrowserWidget::goUp()
{
    if (cwd_.isEmpty() || cwd_ == QStringLiteral("/"))
        return;
    QString parent = cwd_;
    while (parent.endsWith('/'))
        parent.chop(1);
    const int slash = parent.lastIndexOf('/');
    parent = slash <= 0 ? QStringLiteral("/") : parent.left(slash);
    navigate(parent);
}

void SftpBrowserWidget::refresh()
{
    if (!cwd_.isEmpty())
        navigate(cwd_);
}

void SftpBrowserWidget::downloadSelected()
{
    if (!connected_)
        return;

    QList<QTreeWidgetItem *> selected = tree_->selectedItems();
    if (selected.isEmpty())
        return;

    const QString dir = QFileDialog::getExistingDirectory(this, tr("Download to"),
                                                          QDir::homePath());
    if (dir.isEmpty())
        return;

    for (QTreeWidgetItem *item : selected) {
        if (item->data(0, IsDirRole).toBool())
            continue; // directories not supported yet
        const QString name = item->data(0, NameRole).toString();
        emit requestDownload(joinPath(cwd_, name), joinPath(dir, name));
    }
}

void SftpBrowserWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (connected_ && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void SftpBrowserWidget::dropEvent(QDropEvent *event)
{
    if (!connected_)
        return;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString local = url.toLocalFile();
        QFileInfo info(local);
        if (info.isFile())
            emit requestUpload(local, joinPath(cwd_, info.fileName()));
    }
    event->acceptProposedAction();
}
