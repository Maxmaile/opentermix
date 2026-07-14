#include "sessions/TunnelsTabWidget.h"

#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int GatewayColumn = 0;
constexpr int DetailsColumn = 1;
constexpr int ActionColumn = 2;
} // namespace

TunnelsTabWidget::TunnelsTabWidget(QWidget *parent)
    : QWidget(parent)
    , table_(new QTableWidget(this))
{
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Gateway"), tr("Tunnel"), QString()});
    table_->horizontalHeader()->setSectionResizeMode(GatewayColumn, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(DetailsColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ActionColumn, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(table_);
}

void TunnelsTabWidget::setTunnels(const QList<TunnelInfo> &tunnels)
{
    table_->setRowCount(tunnels.size());
    for (int row = 0; row < tunnels.size(); ++row) {
        const TunnelInfo &t = tunnels.at(row);
        table_->setItem(row, GatewayColumn, new QTableWidgetItem(t.gateway.displayName()));
        table_->setItem(row, DetailsColumn, new QTableWidgetItem(t.spec.describe()));

        auto *stopButton = new QPushButton(tr("Stop"), table_);
        const int id = t.id;
        connect(stopButton, &QPushButton::clicked, this, [this, id] { emit stopRequested(id); });
        table_->setCellWidget(row, ActionColumn, stopButton);
    }
}

bool TunnelsTabWidget::isEmpty() const
{
    return table_->rowCount() == 0;
}
