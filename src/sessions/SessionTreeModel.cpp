#include "sessions/SessionTreeModel.h"

#include <QHash>
#include <QMimeData>

#include "app/Icons.h"

namespace {
const QString kSessionMime = QStringLiteral("application/x-opentermix-session-row");
}

SessionTreeModel::SessionTreeModel(QObject *parent)
    : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels({tr("Sessions")});
}

void SessionTreeModel::setSessions(const QList<Session> &sessions, const QStringList &groups)
{
    sessions_ = sessions;
    clear();
    setHorizontalHeaderLabels({tr("Sessions")});

    // A folder icon (Nordic-bluish) marks groups; sessions carry no icon. The
    // folder icon is full-colour and reads in either UI theme, so the tree needs
    // no rebuild when the theme changes.
    const QIcon folderIcon = Icons::colored(QStringLiteral("folder"));

    QHash<QString, QStandardItem *> groupItems;
    auto makeGroup = [&](const QString &name) -> QStandardItem * {
        QStandardItem *&group = groupItems[name];
        if (!group) {
            group = new QStandardItem(folderIcon, name);
            group->setEditable(false);
            group->setSelectable(true); // selectable so the toolbar can act on it
            group->setDragEnabled(false); // folders are drop targets, not draggable
            group->setDropEnabled(true);
            group->setData(false, IsSessionRole);
            invisibleRootItem()->appendRow(group);
        }
        return group;
    };

    // Pre-create folders in the given order so empty ones show and order is stable.
    for (const QString &g : groups) {
        if (!g.isEmpty())
            makeGroup(g);
    }

    for (int row = 0; row < sessions_.size(); ++row) {
        const Session &s = sessions_.at(row);

        QStandardItem *parent = s.group.isEmpty() ? invisibleRootItem() : makeGroup(s.group);

        auto *item = new QStandardItem(s.displayName());
        item->setEditable(false);
        item->setDragEnabled(true);  // sessions can be dragged into folders
        item->setDropEnabled(false); // but nothing can be dropped onto a session
        item->setData(row, SessionRowRole);
        item->setData(true, IsSessionRole);
        QString tip = s.target();
        if (s.port != 0 && s.port != 22)
            tip += ":" + QString::number(s.port);
        item->setToolTip(tip);
        parent->appendRow(item);
    }
}

Qt::ItemFlags SessionTreeModel::flags(const QModelIndex &index) const
{
    // The invisible root has no index; dropping there ungroups a session.
    if (!index.isValid())
        return QStandardItemModel::flags(index) | Qt::ItemIsDropEnabled;
    return QStandardItemModel::flags(index);
}

Qt::DropActions SessionTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList SessionTreeModel::mimeTypes() const
{
    return {kSessionMime};
}

QMimeData *SessionTreeModel::mimeData(const QModelIndexList &indexes) const
{
    for (const QModelIndex &index : indexes) {
        const int row = sessionRow(index);
        if (row >= 0) {
            auto *mime = new QMimeData;
            mime->setData(kSessionMime, QByteArray::number(row));
            return mime;
        }
    }
    return nullptr;
}

bool SessionTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                       const QModelIndex &parent) const
{
    if (!data->hasFormat(kSessionMime))
        return false;
    // Onto empty space (invalid parent) => ungroup; otherwise only onto a folder.
    return !parent.isValid() || !parent.data(IsSessionRole).toBool();
}

bool SessionTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int, int,
                                    const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction)
        return true;
    if (!data->hasFormat(kSessionMime))
        return false;

    const int row = data->data(kSessionMime).toInt();
    QString group;
    if (parent.isValid() && !parent.data(IsSessionRole).toBool())
        group = parent.data(Qt::DisplayRole).toString();

    emit sessionDropped(row, group);
    // We rebuild the whole tree from the rewritten config, so tell the view the
    // drop did not move any rows itself (prevents it from deleting the source).
    return false;
}

int SessionTreeModel::sessionRow(const QModelIndex &index) const
{
    if (!index.isValid())
        return -1;
    if (!index.data(IsSessionRole).toBool())
        return -1;
    bool ok = false;
    const int row = index.data(SessionRowRole).toInt(&ok);
    return ok ? row : -1;
}
