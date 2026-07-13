#pragma once

#include <QList>
#include <QStandardItemModel>

#include "sessions/Session.h"

// Tree model for the session panel: top level are group folders, leaves are
// sessions. Session data lives in the item's UserRole so the view can map a
// clicked index back to a Session.
class SessionTreeModel : public QStandardItemModel {
    Q_OBJECT
public:
    static constexpr int SessionRowRole = Qt::UserRole + 1;
    static constexpr int IsSessionRole = Qt::UserRole + 2;

    explicit SessionTreeModel(QObject *parent = nullptr);

    // "groups" seeds the folder list so empty folders (holding no session) still
    // show up and keep their order.
    void setSessions(const QList<Session> &sessions,
                     const QStringList &groups = QStringList());
    const QList<Session> &sessions() const { return sessions_; }

    // Returns the index into sessions() for a leaf index, or -1 otherwise.
    int sessionRow(const QModelIndex &index) const;

    // Drag-and-drop: sessions can be dragged onto folders (or onto empty space to
    // leave any folder). We do not mutate the tree ourselves - we announce the
    // intent and let the panel rewrite ~/.ssh/config and reload.
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                         int column, const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                      int column, const QModelIndex &parent) override;

signals:
    // Emitted when a session (row into sessions()) is dropped into "group"
    // (empty string means "no folder").
    void sessionDropped(int sessionRow, const QString &group);

private:
    QList<Session> sessions_;
};
