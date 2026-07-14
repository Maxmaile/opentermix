#pragma once

#include <QList>
#include <QWidget>

#include "sessions/Tunnel.h"
#include "sessions/TunnelManager.h"

class QTableWidget;

// Central-area tab (alongside terminals and Settings) listing every active
// tunnel with a per-row Stop button. A dumb, data-in/signal-out widget: it
// never touches TunnelManager directly - MainWindow pushes data in via
// setTunnels() and reacts to stopRequested(), same convention as
// SettingsWidget.
class TunnelsTabWidget : public QWidget {
    Q_OBJECT
public:
    explicit TunnelsTabWidget(QWidget *parent = nullptr);

    void setTunnels(const QList<TunnelInfo> &tunnels);
    bool isEmpty() const;

signals:
    void stopRequested(int tunnelId);

private:
    QTableWidget *table_;
};
