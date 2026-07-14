#include "sessions/TunnelDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

TunnelDialog::TunnelDialog(const Session &gateway, QWidget *parent)
    : QDialog(parent)
    , gateway_(gateway)
{
    setWindowTitle(tr("New tunnel"));
    setModal(true);

    gatewayLabel_ = new QLabel(
        tr("Through: %1 (%2)").arg(gateway_.displayName(), gateway_.target()), this);

    mode_ = new QComboBox(this);
    mode_->addItem(tr("Local forward (-L)"));
    mode_->addItem(tr("Remote forward (-R)"));
    mode_->addItem(tr("Dynamic / SOCKS (-D)"));

    bindPort_ = new QSpinBox(this);
    bindPort_->setRange(0, 65535);
    bindPort_->setValue(0);
    bindPort_->setSpecialValueText(tr("(choose a port)"));

    targetHost_ = new QLineEdit(this);
    targetHost_->setText(QStringLiteral("localhost"));

    targetPort_ = new QSpinBox(this);
    targetPort_->setRange(0, 65535);
    targetPort_->setValue(0);
    targetPort_->setSpecialValueText(tr("(choose a port)"));

    form_ = new QFormLayout;
    form_->addRow(gatewayLabel_);
    form_->addRow(tr("Mode"), mode_);
    form_->addRow(tr("Local port"), bindPort_);
    form_->addRow(tr("Remote host"), targetHost_);
    form_->addRow(tr("Remote port"), targetPort_);

    error_ = new QLabel(this);
    error_->setStyleSheet(QStringLiteral("color:#d33;"));
    error_->setWordWrap(true);
    error_->hide();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TunnelDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form_);
    layout->addWidget(error_);
    layout->addWidget(buttons);

    connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TunnelDialog::onModeChanged);
    updateFieldVisibility();

    resize(380, sizeHint().height());
}

void TunnelDialog::onModeChanged(int)
{
    updateFieldVisibility();
}

void TunnelDialog::updateFieldVisibility()
{
    const auto mode = static_cast<TunnelSpec::Mode>(mode_->currentIndex());

    QLabel *bindPortLabel = qobject_cast<QLabel *>(form_->labelForField(bindPort_));
    QLabel *targetHostLabel = qobject_cast<QLabel *>(form_->labelForField(targetHost_));
    QLabel *targetPortLabel = qobject_cast<QLabel *>(form_->labelForField(targetPort_));

    // ssh -L/-R syntax is "[bind_addr:]port:host:hostport": bindPort_ always
    // maps to that first "port" (bound here for Local/Dynamic, bound on the
    // gateway for Remote); targetHost_/targetPort_ always map to "host:hostport"
    // (the destination reachable from wherever bindPort_ is NOT bound).
    switch (mode) {
    case TunnelSpec::Mode::Local:
        if (bindPortLabel)
            bindPortLabel->setText(tr("Local port"));
        if (targetHostLabel)
            targetHostLabel->setText(tr("Remote host"));
        if (targetPortLabel)
            targetPortLabel->setText(tr("Remote port"));
        form_->setRowVisible(targetHost_, true);
        form_->setRowVisible(targetPort_, true);
        break;
    case TunnelSpec::Mode::Remote:
        if (bindPortLabel)
            bindPortLabel->setText(tr("Remote bind port"));
        if (targetHostLabel)
            targetHostLabel->setText(tr("Connect back to host"));
        if (targetPortLabel)
            targetPortLabel->setText(tr("Local port"));
        form_->setRowVisible(targetHost_, true);
        form_->setRowVisible(targetPort_, true);
        break;
    case TunnelSpec::Mode::Dynamic:
        if (bindPortLabel)
            bindPortLabel->setText(tr("SOCKS port"));
        form_->setRowVisible(targetHost_, false);
        form_->setRowVisible(targetPort_, false);
        break;
    }
}

void TunnelDialog::validateAndAccept()
{
    const auto mode = static_cast<TunnelSpec::Mode>(mode_->currentIndex());

    QStringList problems;
    if (bindPort_->value() <= 0)
        problems << tr("Choose a valid port.");
    if (mode != TunnelSpec::Mode::Dynamic) {
        if (targetHost_->text().trimmed().isEmpty())
            problems << tr("Remote host is required.");
        if (targetPort_->value() <= 0)
            problems << tr("Choose a valid remote port.");
    }

    if (!problems.isEmpty()) {
        error_->setText(problems.join(' '));
        error_->show();
        return;
    }
    accept();
}

TunnelSpec TunnelDialog::spec() const
{
    TunnelSpec s;
    s.mode = static_cast<TunnelSpec::Mode>(mode_->currentIndex());
    s.bindPort = static_cast<quint16>(bindPort_->value());
    if (s.mode != TunnelSpec::Mode::Dynamic) {
        s.targetHost = targetHost_->text().trimmed();
        s.targetPort = static_cast<quint16>(targetPort_->value());
    }
    return s;
}
