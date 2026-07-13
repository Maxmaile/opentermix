#pragma once

#include <QWidget>

#include "sessions/Session.h"
#include "sessions/SessionLayout.h"

class SessionTreeModel;
class QAction;
class QTreeView;
class QModelIndex;
class QPoint;

// Left-dock widget: a toolbar plus a tree of sessions read from ~/.ssh/config.
class SessionPanel : public QWidget {
    Q_OBJECT
public:
    explicit SessionPanel(QWidget *parent = nullptr);

    void reload();
    void applyIcons(); // re-tint toolbar icons after a theme change

signals:
    void connectRequested(const Session &s);
    void sftpRequested(const Session &s);

private slots:
    void addSession();
    void editSession();
    void deleteSession();
    void newFolder();
    void renameFolder(const QString &oldName);
    void deleteFolder(const QString &name);
    void onSessionDropped(int sessionRow, const QString &group);
    void onActivated(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);

private:
    bool currentSession(Session &out, int &row) const;
    bool currentFolder(QString &name) const;
    void saveConfig(const QList<Session> &sessions); // write ~/.ssh/config (host data)
    QStringList aliasesExcept(const QString &alias) const; // for uniqueness checks
    void moveSessionToGroup(int sessionRow, const QString &group);
    void deployKey(const Session &s, const QString &publicKeyPath);

    SessionTreeModel *model_;
    QTreeView *view_;
    QString configPath_;
    SessionLayout layout_; // folders + grouping, kept out of ~/.ssh/config

    QAction *addAction_ = nullptr;
    QAction *newFolderAction_ = nullptr;
    QAction *editAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *deleteAction_ = nullptr;
};
