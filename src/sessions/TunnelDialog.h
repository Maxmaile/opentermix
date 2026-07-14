#pragma once

#include <QDialog>

#include "sessions/Session.h"
#include "sessions/Tunnel.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

// Settings form for a single new tunnel through an already-chosen gateway
// Session. Mirrors SessionEditorDialog's field/validate/getter convention.
class TunnelDialog : public QDialog {
    Q_OBJECT
public:
    explicit TunnelDialog(const Session &gateway, QWidget *parent = nullptr);

    TunnelSpec spec() const;

private slots:
    void onModeChanged(int index);
    void validateAndAccept();

private:
    void updateFieldVisibility();

    Session gateway_; // read-only, just for the "Through: ..." label

    QFormLayout *form_;
    QLabel *gatewayLabel_;
    QComboBox *mode_;      // Local / Remote / Dynamic (SOCKS)
    QSpinBox *bindPort_;   // relabeled per mode - see Tunnel.h for the naming rationale
    QLineEdit *targetHost_; // hidden for Dynamic
    QSpinBox *targetPort_;  // hidden for Dynamic
    QLabel *error_;
};
