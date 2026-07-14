#pragma once

#include <QWidget>

#include "sessions/Session.h"
#include "sessions/SessionLayout.h"
#include "sessions/TunnelManager.h"

class SessionTreeModel;
class QAction;
class QToolBar;
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
    void stopAllTunnels(); // forwarded to the TunnelManager, e.g. on app shutdown

    // Narrow forwarding to the private TunnelManager, for MainWindow to build
    // the "Tunnels" tab without owning tunnel lifecycle itself.
    QList<TunnelInfo> activeTunnels() const;
    void stopTunnel(int id);

signals:
    void connectRequested(const Session &s);
    void sftpRequested(const Session &s);
    void tunnelsChanged(); // forwarded from TunnelManager::tunnelsChanged

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
    void openTunnelDialog(); // Tunnel toolbar button: dialog for the selected session
    void startTunnelVia(const Session &gateway);
    void onTunnelFailed(int id, const QString &message);
    void updateTunnelActionEnabled(); // toolbar button enabled only with a session selected

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
    TunnelManager *tunnels_;
    QToolBar *toolbar_ = nullptr;

    QAction *addAction_ = nullptr;
    QAction *newFolderAction_ = nullptr;
    QAction *editAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *tunnelAction_ = nullptr;
    QAction *deleteAction_ = nullptr;
};
