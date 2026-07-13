#include "sessions/SessionEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// A label with a trailing red asterisk to mark required fields.
QLabel *requiredLabel(const QString &text)
{
    auto *label = new QLabel(text + QStringLiteral(" <span style=\"color:#d33\">*</span>"));
    label->setTextFormat(Qt::RichText);
    return label;
}

} // namespace

SessionEditorDialog::SessionEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Session"));
    setModal(true);

    alias_ = new QLineEdit(this);
    alias_->setPlaceholderText(tr("my-server (unique name)"));
    aliasHint_ = new QLabel(this);
    aliasHint_->setStyleSheet(QStringLiteral("color:#888;"));
    aliasHint_->setWordWrap(true);
    aliasHint_->hide();
    connect(alias_, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.simplified().contains(QLatin1Char(' '))) {
            aliasHint_->setText(tr(
                "Multiple space-separated names: ssh treats each as a separate "
                "pattern for this Host block. Removing one here removes it from "
                "~/.ssh/config on save."));
            aliasHint_->show();
        } else {
            aliasHint_->hide();
        }
    });
    hostName_ = new QLineEdit(this);
    hostName_->setPlaceholderText(tr("example.com or 10.0.0.5"));
    user_ = new QLineEdit(this);
    user_->setPlaceholderText(tr("optional, e.g. root"));

    port_ = new QSpinBox(this);
    port_->setRange(0, 65535);
    port_->setSpecialValueText(tr("default (22)"));
    port_->setValue(0);

    identity_ = new QLineEdit(this);
    identity_->setPlaceholderText(tr("optional, e.g. ~/.ssh/id_ed25519"));
    auto *browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &SessionEditorDialog::browseIdentity);
    auto *identityRow = new QWidget(this);
    auto *identityLayout = new QHBoxLayout(identityRow);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->addWidget(identity_);
    identityLayout->addWidget(browse);

    proxyJump_ = new QLineEdit(this);
    proxyJump_->setPlaceholderText(tr("optional jump host, e.g. user@gateway"));

    forwardX11_ = new QCheckBox(tr("Forward X11 (ssh -X)"), this);
    compression_ = new QCheckBox(tr("Enable compression (ssh -C)"), this);

    group_ = new QLineEdit(this);
    group_->setPlaceholderText(tr("optional folder name"));

    deployKey_ = new QCheckBox(tr("Deploy a public key to the host"), this);

    publicKey_ = new QLineEdit(this);
    publicKey_->setPlaceholderText(tr("e.g. ~/.ssh/id_ed25519.pub"));
    auto *browsePub = new QPushButton(tr("Browse..."), this);
    connect(browsePub, &QPushButton::clicked, this, &SessionEditorDialog::browsePublicKey);
    publicKeyRow_ = new QWidget(this);
    auto *publicKeyLayout = new QHBoxLayout(publicKeyRow_);
    publicKeyLayout->setContentsMargins(0, 0, 0, 0);
    publicKeyLayout->addWidget(publicKey_);
    publicKeyLayout->addWidget(browsePub);
    publicKeyRow_->setEnabled(false);

    connect(deployKey_, &QCheckBox::toggled, this, [this](bool on) {
        publicKeyRow_->setEnabled(on);
        if (on && publicKey_->text().trimmed().isEmpty()) {
            // Sensible default: the identity's .pub, or a common default key.
            const QString identity = identity_->text().trimmed();
            QString guess;
            if (!identity.isEmpty()) {
                guess = identity + QStringLiteral(".pub");
            } else {
                const QString ssh = QDir::homePath() + QStringLiteral("/.ssh/");
                for (const QString &name : {QStringLiteral("id_ed25519.pub"),
                                            QStringLiteral("id_rsa.pub"),
                                            QStringLiteral("id_ecdsa.pub")}) {
                    if (QFileInfo::exists(ssh + name)) {
                        guess = ssh + name;
                        break;
                    }
                }
            }
            publicKey_->setText(guess);
        }
    });

    auto *form = new QFormLayout;
    form->addRow(requiredLabel(tr("Name (Host)")), alias_);
    form->addRow(QString(), aliasHint_);
    form->addRow(requiredLabel(tr("Host name / IP")), hostName_);
    form->addRow(tr("User"), user_);
    form->addRow(tr("Port"), port_);
    form->addRow(tr("Identity file"), identityRow);
    form->addRow(tr("Jump host"), proxyJump_);
    form->addRow(QString(), forwardX11_);
    form->addRow(QString(), compression_);
    form->addRow(tr("Folder"), group_);
    form->addRow(QString(), deployKey_);
    form->addRow(tr("Public key"), publicKeyRow_);

    error_ = new QLabel(this);
    error_->setStyleSheet("color:#d33;");
    error_->setWordWrap(true);
    error_->hide();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SessionEditorDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(error_);
    layout->addWidget(buttons);

    resize(420, sizeHint().height());
}

void SessionEditorDialog::browseIdentity()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Select private key"),
                                                      QDir::homePath() + "/.ssh");
    if (!file.isEmpty())
        identity_->setText(file);
}

void SessionEditorDialog::browsePublicKey()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select public key"), QDir::homePath() + "/.ssh",
        tr("Public keys (*.pub);;All files (*)"));
    if (!file.isEmpty())
        publicKey_->setText(file);
}

void SessionEditorDialog::setExistingAliases(const QStringList &aliases)
{
    existingAliases_ = aliases;
}

bool SessionEditorDialog::pushPublicKey() const
{
    return deployKey_->isChecked();
}

QString SessionEditorDialog::publicKeyPath() const
{
    return publicKey_->text().trimmed();
}

void SessionEditorDialog::setSession(const Session &s)
{
    alias_->setText(s.alias);
    hostName_->setText(s.hostName);
    user_->setText(s.user);
    port_->setValue(s.port);
    identity_->setText(s.identityFile);
    proxyJump_->setText(s.proxyJump);
    forwardX11_->setChecked(s.forwardX11);
    compression_->setChecked(s.compression);
    group_->setText(s.group);
    extraLines_ = s.extraLines;
}

Session SessionEditorDialog::session() const
{
    Session s;
    s.alias = alias_->text().trimmed();
    s.hostName = hostName_->text().trimmed();
    s.user = user_->text().trimmed();
    s.port = port_->value();
    s.identityFile = identity_->text().trimmed();
    s.proxyJump = proxyJump_->text().trimmed();
    s.forwardX11 = forwardX11_->isChecked();
    s.compression = compression_->isChecked();
    s.group = group_->text().trimmed();
    s.extraLines = extraLines_;
    return s;
}

void SessionEditorDialog::validateAndAccept()
{
    const QString invalid = "border:1px solid #d33;";
    alias_->setStyleSheet(QString());
    hostName_->setStyleSheet(QString());

    QStringList problems;
    const QString aliasText = alias_->text().trimmed();
    if (aliasText.isEmpty()) {
        alias_->setStyleSheet(invalid);
        problems << tr("Name is required.");
    } else if (existingAliases_.contains(aliasText)) {
        alias_->setStyleSheet(invalid);
        problems << tr("A session named \"%1\" already exists.").arg(aliasText);
    }
    if (hostName_->text().trimmed().isEmpty()) {
        hostName_->setStyleSheet(invalid);
        problems << tr("Host name is required.");
    }

    if (deployKey_->isChecked()) {
        QString path = publicKey_->text().trimmed();
        if (path.startsWith(QStringLiteral("~/")))
            path = QDir::homePath() + path.mid(1);
        if (path.isEmpty()) {
            problems << tr("Choose a public key to deploy.");
        } else if (!QFileInfo::exists(path)) {
            problems << tr("Public key file does not exist.");
        }
    }

    if (!problems.isEmpty()) {
        error_->setText(problems.join(' '));
        error_->show();
        return;
    }
    accept();
}
