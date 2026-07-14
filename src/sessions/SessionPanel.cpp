#include "sessions/SessionPanel.h"

#include <QAction>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QThread>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

#include "app/Icons.h"
#include "sessions/SessionEditorDialog.h"
#include "sessions/SessionTreeModel.h"
#include "sessions/SshConfigParser.h"
#include "sessions/TunnelDialog.h"
#include "sessions/TunnelManager.h"
#include "sftp/SftpClient.h"

SessionPanel::SessionPanel(QWidget *parent)
    : QWidget(parent)
    , model_(new SessionTreeModel(this))
    , view_(new QTreeView(this))
    , configPath_(SshConfigParser::defaultConfigPath())
    , tunnels_(new TunnelManager(this))
{
    connect(tunnels_, &TunnelManager::tunnelFailed, this, &SessionPanel::onTunnelFailed);
    connect(tunnels_, &TunnelManager::tunnelsChanged, this, &SessionPanel::tunnelsChanged);

    view_->setModel(model_);
    view_->setHeaderHidden(true);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Drag sessions onto folders (or onto empty space to leave a folder).
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::DragDrop);
    view_->setDefaultDropAction(Qt::MoveAction);

    toolbar_ = new QToolBar(this);
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addAction_ = toolbar_->addAction(tr("Add"), this, &SessionPanel::addSession);
    newFolderAction_ = toolbar_->addAction(tr("New folder"), this, &SessionPanel::newFolder);
    editAction_ = toolbar_->addAction(tr("Edit"), this, &SessionPanel::editSession);
    refreshAction_ = toolbar_->addAction(tr("Refresh"), this, &SessionPanel::reload);
    tunnelAction_ = toolbar_->addAction(tr("Tunnel"), this, &SessionPanel::openTunnelDialog);
    tunnelAction_->setEnabled(false); // enabled once a session is selected, see below
    deleteAction_ = toolbar_->addAction(tr("Delete"), this, &SessionPanel::deleteSession);
    for (QAction *a : {addAction_, newFolderAction_, editAction_, refreshAction_, tunnelAction_,
                       deleteAction_})
        a->setToolTip(a->text());
    applyIcons();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar_);
    layout->addWidget(view_);

    connect(view_, &QTreeView::activated, this, &SessionPanel::onActivated);
    connect(view_, &QTreeView::customContextMenuRequested,
            this, &SessionPanel::showContextMenu);
    connect(model_, &SessionTreeModel::sessionDropped, this, &SessionPanel::onSessionDropped);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SessionPanel::updateTunnelActionEnabled);

    reload();
}

void SessionPanel::applyIcons()
{
    addAction_->setIcon(Icons::action(QStringLiteral("add")));
    newFolderAction_->setIcon(Icons::action(QStringLiteral("folder-new")));
    editAction_->setIcon(Icons::action(QStringLiteral("edit")));
    refreshAction_->setIcon(Icons::action(QStringLiteral("refresh")));
    tunnelAction_->setIcon(Icons::action(QStringLiteral("tunnel")));
    deleteAction_->setIcon(Icons::action(QStringLiteral("delete")));
}

void SessionPanel::stopAllTunnels()
{
    tunnels_->stopAll();
}

void SessionPanel::reload()
{
    QString error;
    QStringList configGroups;
    QList<Session> sessions = SshConfigParser::parse(configPath_, &error, &configGroups);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("SSH config"), error);

    // The folder tree is OpenTermix's own metadata. On the very first run it is
    // seeded from any grouping older versions stored in the ssh config; after
    // that the JSON layout file is authoritative and the config is left alone.
    if (!layout_.load()) {
        QHash<QString, QString> seed;
        for (const Session &s : sessions) {
            if (!s.group.isEmpty())
                seed.insert(s.alias, s.group);
        }
        layout_.seedFrom(configGroups, seed);
        layout_.save();
    }

    for (Session &s : sessions)
        s.group = layout_.groupOf(s.alias);

    model_->setSessions(sessions, layout_.folders());
    view_->expandAll();
    updateTunnelActionEnabled(); // reload() can invalidate the current selection
}

void SessionPanel::updateTunnelActionEnabled()
{
    Session s;
    int row;
    tunnelAction_->setEnabled(currentSession(s, row));
}

bool SessionPanel::currentSession(Session &out, int &row) const
{
    row = model_->sessionRow(view_->currentIndex());
    if (row < 0 || row >= model_->sessions().size())
        return false;
    out = model_->sessions().at(row);
    return true;
}

bool SessionPanel::currentFolder(QString &name) const
{
    const QModelIndex index = view_->currentIndex();
    if (!index.isValid() || index.data(SessionTreeModel::IsSessionRole).toBool())
        return false;
    name = index.data(Qt::DisplayRole).toString();
    return !name.isEmpty();
}

void SessionPanel::saveConfig(const QList<Session> &sessions)
{
    QString error;
    if (!SshConfigParser::save(configPath_, sessions, &error))
        QMessageBox::critical(this, tr("SSH config"), error);
}

QStringList SessionPanel::aliasesExcept(const QString &alias) const
{
    QStringList result;
    for (const Session &s : model_->sessions()) {
        if (s.alias != alias)
            result << s.alias;
    }
    return result;
}

void SessionPanel::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New folder"),
                                               tr("Folder name:"), QLineEdit::Normal,
                                               QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (layout_.hasFolder(name)) {
        QMessageBox::information(this, tr("New folder"),
                                 tr("A folder named \"%1\" already exists.").arg(name));
        return;
    }
    layout_.addFolder(name);
    layout_.save();
    reload();
}

void SessionPanel::renameFolder(const QString &oldName)
{
    if (oldName.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename folder"),
                                               tr("Folder name:"), QLineEdit::Normal,
                                               oldName, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == oldName)
        return;
    if (layout_.hasFolder(name)) {
        QMessageBox::information(this, tr("Rename folder"),
                                 tr("A folder named \"%1\" already exists.").arg(name));
        return;
    }
    layout_.renameFolder(oldName, name);
    layout_.save();
    reload();
}

void SessionPanel::deleteFolder(const QString &name)
{
    if (name.isEmpty() || !layout_.hasFolder(name))
        return;
    if (QMessageBox::question(
            this, tr("Delete folder"),
            tr("Delete folder \"%1\"? Its sessions move back to the top level.")
                .arg(name))
        != QMessageBox::Yes)
        return;

    layout_.removeFolder(name); // also ungroups the hosts it held
    layout_.save();
    reload();
}

void SessionPanel::onSessionDropped(int sessionRow, const QString &group)
{
    // The signal fires from inside the view's drop handling; defer the layout
    // update + model rebuild until that has unwound.
    QMetaObject::invokeMethod(
        this, [this, sessionRow, group] { moveSessionToGroup(sessionRow, group); },
        Qt::QueuedConnection);
}

void SessionPanel::moveSessionToGroup(int sessionRow, const QString &group)
{
    const QList<Session> &sessions = model_->sessions();
    if (sessionRow < 0 || sessionRow >= sessions.size())
        return;
    const QString alias = sessions.at(sessionRow).alias;
    if (layout_.groupOf(alias) == group)
        return;
    // Grouping is OpenTermix metadata: update the layout only, never the ssh config.
    layout_.setGroup(alias, group);
    layout_.save();
    reload();
}

void SessionPanel::addSession()
{
    SessionEditorDialog dialog(this);
    dialog.setExistingAliases(aliasesExcept(QString()));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Session s = dialog.session();
    QList<Session> sessions = model_->sessions();
    sessions.append(s);
    saveConfig(sessions);
    // The chosen folder (if any) is layout metadata, not a config field.
    layout_.setGroup(s.alias, s.group);
    layout_.save();
    reload();
    if (dialog.pushPublicKey())
        deployKey(s, dialog.publicKeyPath());
}

void SessionPanel::editSession()
{
    // The Edit button doubles as "rename" when a folder is selected.
    QString folder;
    if (currentFolder(folder)) {
        renameFolder(folder);
        return;
    }

    Session s;
    int row = -1;
    if (!currentSession(s, row)) {
        QMessageBox::information(this, tr("Edit"), tr("Select a session or folder first."));
        return;
    }
    SessionEditorDialog dialog(this);
    dialog.setSession(s);
    dialog.setExistingAliases(aliasesExcept(s.alias));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Session edited = dialog.session();
    QList<Session> sessions = model_->sessions();
    sessions[row] = edited;
    saveConfig(sessions);
    // Carry the folder over in the layout, following a possible alias rename.
    if (edited.alias != s.alias)
        layout_.renameHost(s.alias, edited.alias);
    layout_.setGroup(edited.alias, edited.group);
    layout_.save();
    reload();
    if (dialog.pushPublicKey())
        deployKey(edited, dialog.publicKeyPath());
}

void SessionPanel::deployKey(const Session &s, const QString &publicKeyPath)
{
    auto *thread = new QThread(this);
    auto *client = new SftpClient;
    client->moveToThread(thread);
    connect(thread, &QThread::finished, client, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(client, &SftpClient::keyDeployed, this, [this, thread](bool ok, QString message) {
        if (ok)
            QMessageBox::information(this, tr("Deploy public key"), message);
        else
            QMessageBox::warning(this, tr("Deploy public key"), message);
        thread->quit();
    });
    thread->start();
    QMetaObject::invokeMethod(client, "deployPublicKey", Qt::QueuedConnection,
                              Q_ARG(Session, s), Q_ARG(QString, publicKeyPath));
}

void SessionPanel::deleteSession()
{
    // The Delete button removes the selected folder when one is selected.
    QString folder;
    if (currentFolder(folder)) {
        deleteFolder(folder);
        return;
    }

    Session s;
    int row = -1;
    if (!currentSession(s, row))
        return;
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Delete session \"%1\"?").arg(s.displayName()))
        != QMessageBox::Yes)
        return;
    QList<Session> sessions = model_->sessions();
    sessions.removeAt(row);
    saveConfig(sessions);
    layout_.removeHost(s.alias);
    layout_.save();
    reload();
}

void SessionPanel::onActivated(const QModelIndex &index)
{
    const int row = model_->sessionRow(index);
    if (row >= 0)
        emit connectRequested(model_->sessions().at(row));
}

void SessionPanel::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = view_->indexAt(pos);
    const int row = model_->sessionRow(index);

    const bool isFolder =
        index.isValid() && !index.data(SessionTreeModel::IsSessionRole).toBool();

    QMenu menu(this);
    if (row >= 0) {
        const Session s = model_->sessions().at(row);
        menu.addAction(tr("Connect"), this, [this, s] { emit connectRequested(s); });
        menu.addAction(tr("Open SFTP"), this, [this, s] { emit sftpRequested(s); });
        menu.addSeparator();
        menu.addAction(tr("Edit"), this, &SessionPanel::editSession);
        menu.addAction(tr("Delete"), this, &SessionPanel::deleteSession);
        menu.addSeparator();
    } else if (isFolder) {
        const QString name = index.data(Qt::DisplayRole).toString();
        menu.addAction(tr("Rename folder"), this, [this, name] { renameFolder(name); });
        menu.addAction(tr("Delete folder"), this, [this, name] { deleteFolder(name); });
        menu.addSeparator();
    }
    menu.addAction(tr("Add"), this, &SessionPanel::addSession);
    menu.addAction(tr("New folder"), this, &SessionPanel::newFolder);
    menu.addAction(tr("Refresh"), this, &SessionPanel::reload);
    menu.exec(view_->viewport()->mapToGlobal(pos));
}

void SessionPanel::openTunnelDialog()
{
    Session s;
    int row;
    if (!currentSession(s, row))
        return; // toolbar button is disabled in this case, but guard anyway
    startTunnelVia(s);
}

QList<TunnelInfo> SessionPanel::activeTunnels() const
{
    return tunnels_->activeTunnels();
}

void SessionPanel::stopTunnel(int id)
{
    tunnels_->stopTunnel(id);
}

void SessionPanel::startTunnelVia(const Session &gateway)
{
    TunnelDialog dialog(gateway, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (tunnels_->startTunnel(gateway, dialog.spec(), &error) < 0)
        QMessageBox::warning(this, tr("Tunnel"), tr("Could not start tunnel: %1").arg(error));
}

void SessionPanel::onTunnelFailed(int /*id*/, const QString &message)
{
    QMessageBox::warning(this, tr("Tunnel"),
                         tr("A tunnel stopped unexpectedly:\n%1").arg(message));
}
