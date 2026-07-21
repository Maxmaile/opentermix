#include "app/SettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

#include "terminal/ColorSchemeEditorDialog.h"
#include "terminal/ColorSchemeStore.h"

SettingsWidget::SettingsWidget(bool dark, const QString &colorScheme, QWidget *parent)
    : QWidget(parent)
    , language_(new QComboBox(this))
    , startup_(new QComboBox(this))
    , colorScheme_(new QComboBox(this))
    , editSchemeButton_(new QPushButton(tr("Customize..."), this))
    , deleteSchemeButton_(new QPushButton(tr("Delete"), this))
{
    auto *title = new QLabel(tr("Settings"), this);
    title->setObjectName("SettingsTitle");

    // Language.
    language_->addItem(tr("System default"), QString());
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    const QString current = QSettings().value("ui/language").toString();
    const int idx = language_->findData(current);
    language_->setCurrentIndex(idx >= 0 ? idx : 0);
    connect(language_, &QComboBox::currentIndexChanged, this, [this](int i) {
        emit languageChangeRequested(language_->itemData(i).toString());
    });

    auto *languageNote = new QLabel(tr("The language change applies after restart."), this);
    languageNote->setObjectName("SettingsHint");
    languageNote->setWordWrap(true);

    // Startup behaviour: whether to prompt to restore the previous tabs.
    startup_->addItem(tr("Ask every time"), QStringLiteral("ask"));
    startup_->addItem(tr("Restore previous session"), QStringLiteral("restore"));
    startup_->addItem(tr("Start with a new tab"), QStringLiteral("new"));
    const QString startupMode = QSettings().value("session/onStartup",
                                                  QStringLiteral("ask")).toString();
    const int startupIdx = startup_->findData(startupMode);
    startup_->setCurrentIndex(startupIdx >= 0 ? startupIdx : 0);
    connect(startup_, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue("session/onStartup", startup_->itemData(i).toString());
    });

    // Theme.
    auto *darkCheck = new QCheckBox(tr("Dark theme"), this);
    darkCheck->setChecked(dark);
    connect(darkCheck, &QCheckBox::toggled, this, &SettingsWidget::themeChanged);

    // Terminal colour schemes (Konsole ".colorscheme" format via QTermWidget).
    reloadSchemes(colorScheme);
    connect(colorScheme_, &QComboBox::currentIndexChanged, this, [this](int i) {
        updateSchemeButtons();
        emit colorSchemeChanged(colorScheme_->itemData(i).toString());
    });
    connect(editSchemeButton_, &QPushButton::clicked, this, &SettingsWidget::customizeScheme);
    connect(deleteSchemeButton_, &QPushButton::clicked, this, &SettingsWidget::deleteScheme);

    auto *schemeRow = new QWidget(this);
    auto *schemeLayout = new QHBoxLayout(schemeRow);
    schemeLayout->setContentsMargins(0, 0, 0, 0);
    schemeLayout->addWidget(colorScheme_, 1);
    schemeLayout->addWidget(editSchemeButton_);
    schemeLayout->addWidget(deleteSchemeButton_);

    // Font.
    auto *fontButton = new QPushButton(tr("Terminal font..."), this);
    connect(fontButton, &QPushButton::clicked, this, &SettingsWidget::chooseFontRequested);

    auto *form = new QFormLayout;
    form->addRow(tr("Language"), language_);
    form->addRow(QString(), languageNote);
    form->addRow(tr("On startup"), startup_);
    form->addRow(tr("Theme"), darkCheck);
    form->addRow(tr("Console colors"), schemeRow);
    form->addRow(tr("Font"), fontButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->addWidget(title);
    layout->addSpacing(12);
    layout->addLayout(form);
    layout->addStretch();

    updateSchemeButtons();
}

void SettingsWidget::reloadSchemes(const QString &select)
{
    const QString current = currentScheme();
    colorScheme_->clear();
    colorScheme_->addItem(tr("Follow app theme"), QStringLiteral("auto"));
    for (const QString &name : ColorSchemeStore::available()) {
        if (name == QLatin1String("auto"))
            continue;
        colorScheme_->addItem(name, name);
    }

    const QString pick = select.isEmpty() ? current : select;
    const int idx = colorScheme_->findData(pick);
    colorScheme_->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString SettingsWidget::currentScheme() const
{
    return colorScheme_->currentData().toString();
}

void SettingsWidget::updateSchemeButtons()
{
    const QString scheme = currentScheme();
    const bool preset = scheme == QLatin1String("auto");
    editSchemeButton_->setEnabled(!preset);
    deleteSchemeButton_->setEnabled(!preset && ColorSchemeStore::isCustom(scheme));
}

void SettingsWidget::customizeScheme()
{
    const QString scheme = currentScheme();
    if (scheme == QLatin1String("auto"))
        return;

    const bool custom = ColorSchemeStore::isCustom(scheme);
    TermColorScheme loaded = ColorSchemeStore::load(scheme);

    ColorSchemeEditorDialog dlg(scheme, loaded, !custom, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QString newName = dlg.schemeName();
    const TermColorScheme edited = dlg.scheme();

    // QTermWidget's ColorSchemeManager caches a scheme's colours by name for
    // the whole process and has no public way to make it re-read a changed
    // file - it returns the cached copy as soon as a name has been seen once,
    // which happens the moment this dropdown is first populated. Overwriting
    // an existing scheme's file under the same name (editing a custom scheme
    // in place, which keeps its name fixed - see the dialog's nameEditable
    // argument above) would therefore save correctly but not visibly apply
    // until the app restarts. Work around it by saving under a name
    // qtermwidget has never seen and dropping the stale file.
    if (ColorSchemeStore::isCustom(newName)) {
        static const QRegularExpression suffixRe(QStringLiteral("_[0-9]+$"));
        const QString oldName = newName;
        QString base = oldName;
        base.remove(suffixRe);
        int suffix = 2;
        do {
            newName = QStringLiteral("%1_%2").arg(base).arg(suffix++);
        } while (ColorSchemeStore::isCustom(newName));

        ColorSchemeStore::remove(oldName);
        QMessageBox::information(
            this, tr("Color scheme"),
            tr("The terminal library keeps loaded color schemes in memory, so "
              "editing \"%1\" in place would not show up until a restart. "
              "Saved the new colors as \"%2\" instead, applied right away.")
                .arg(oldName, newName));
    }

    const QString path = ColorSchemeStore::save(newName, edited);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Color scheme"),
                             tr("Could not save the color scheme."));
        return;
    }

    reloadSchemes(newName);
    emit colorSchemeChanged(newName);
}

void SettingsWidget::deleteScheme()
{
    const QString scheme = currentScheme();
    if (!ColorSchemeStore::isCustom(scheme))
        return;

    const auto answer = QMessageBox::question(
        this, tr("Color scheme"),
        tr("Delete the custom color scheme \"%1\"?").arg(scheme),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    ColorSchemeStore::remove(scheme);
    reloadSchemes(QStringLiteral("auto"));
    emit colorSchemeChanged(QStringLiteral("auto"));
}
