#pragma once

#include <QDialog>

#include "sessions/Session.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// Graphical editor for a single session. Required fields (Host alias, HostName)
// are marked with a red asterisk and validated before the dialog closes.
class SessionEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SessionEditorDialog(QWidget *parent = nullptr);

    void setSession(const Session &s);
    Session session() const;

    // Other sessions' full Host values, used to reject a name collision before
    // it silently overwrites another entry on save.
    void setExistingAliases(const QStringList &aliases);

    bool pushPublicKey() const;
    QString publicKeyPath() const;

private slots:
    void browseIdentity();
    void browsePublicKey();
    void validateAndAccept();

private:
    QLineEdit *alias_;
    QLabel *aliasHint_;
    QLineEdit *hostName_;
    QLineEdit *user_;
    QSpinBox *port_;
    QLineEdit *identity_;
    QLineEdit *proxyJump_;
    QCheckBox *forwardX11_;
    QCheckBox *compression_;
    QLineEdit *group_;
    QCheckBox *deployKey_;
    QLineEdit *publicKey_;
    QWidget *publicKeyRow_;
    QLabel *error_;

    QStringList extraLines_;      // preserved unknown options of the edited session
    QStringList existingAliases_; // other sessions' Host values, for uniqueness checks
};
