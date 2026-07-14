#pragma once

#include <QVector>
#include <QWidget>

#include "sessions/Session.h"
#include "sftp/SftpClient.h"

class QAction;
class QLabel;
class QLineEdit;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;

// Right-dock file browser. UI lives here; all blocking libssh work happens in a
// SftpClient on a worker thread. Communication is via queued signals/slots.
class SftpBrowserWidget : public QWidget {
    Q_OBJECT
public:
    explicit SftpBrowserWidget(QWidget *parent = nullptr);
    ~SftpBrowserWidget() override;

    void connectTo(const Session &s);
    void applyIcons(); // re-tint toolbar icons after a theme change

signals:
    void requestConnect(Session s);
    void requestList(QString path);
    void requestDownload(QString remote, QString local);
    void requestUpload(QString local, QString remote);
    void requestDisconnect();
    // Type a ready-to-run command (newline included) into the owning terminal.
    void runInTerminal(QString command);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onConnected(QString home);
    void onListed(QString path, QVector<SftpEntry> entries);
    void onError(QString message);
    void onInfo(QString message);
    void onItemActivated(QTreeWidgetItem *item, int column);
    void goUp();
    void refresh();
    void downloadSelected();
    void uploadSelected();

private:
    void navigate(const QString &path);
    void browseLocal(const QString &path);
    void renderEntries(const QString &path, const QVector<SftpEntry> &entries);
    QString joinPath(const QString &dir, const QString &name) const;
    bool isTextFile(const QString &name) const;

    QThread *thread_;
    SftpClient *client_;
    QTreeWidget *tree_;
    QLineEdit *pathEdit_;
    QLabel *status_;
    QAction *upAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *downloadAction_ = nullptr;
    QAction *uploadAction_ = nullptr;
    QAction *hiddenAction_ = nullptr;
    QString cwd_;
    bool connected_ = false;
    bool localMode_ = true; // browse the local FS until an SSH session connects
    bool showHidden_ = true;
};
