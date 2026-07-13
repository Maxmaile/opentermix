#include "terminal/ColorSchemeEditorDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ColorSchemeEditorDialog::ColorSchemeEditorDialog(const QString &name,
                                                 const TermColorScheme &scheme,
                                                 bool nameEditable, QWidget *parent)
    : QDialog(parent)
    , name_(new QLineEdit(this))
    , scheme_(scheme)
    , background_(scheme.background)
    , foreground_(scheme.foreground)
{
    setWindowTitle(tr("Terminal color scheme"));
    for (int i = 0; i < 8; ++i) {
        colors_[i] = scheme.colors[i];
        intense_[i] = scheme.intense[i];
    }

    name_->setText(name);
    name_->setEnabled(nameEditable);

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), name_);

    auto *baseBox = new QGroupBox(tr("Base"), this);
    auto *baseLayout = new QFormLayout(baseBox);
    baseLayout->addRow(tr("Background"), makeSwatch(background_));
    baseLayout->addRow(tr("Foreground"), makeSwatch(foreground_));

    auto *ansiBox = new QGroupBox(tr("ANSI colors"), this);
    auto *grid = new QGridLayout(ansiBox);
    grid->addWidget(new QLabel(tr("Normal"), ansiBox), 0, 1, Qt::AlignHCenter);
    grid->addWidget(new QLabel(tr("Intense"), ansiBox), 0, 2, Qt::AlignHCenter);
    static const char *labels[8] = {"Black", "Red", "Green", "Yellow",
                                    "Blue", "Magenta", "Cyan", "White"};
    for (int i = 0; i < 8; ++i) {
        grid->addWidget(new QLabel(tr(labels[i]), ansiBox), i + 1, 0);
        grid->addWidget(makeSwatch(colors_[i]), i + 1, 1);
        grid->addWidget(makeSwatch(intense_[i]), i + 1, 2);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (name_->text().trimmed().isEmpty())
            return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(baseBox);
    layout->addWidget(ansiBox);
    layout->addWidget(buttons);
}

QPushButton *ColorSchemeEditorDialog::makeSwatch(QColor &target)
{
    auto *button = new QPushButton(this);
    button->setFixedSize(64, 24);
    button->setFlat(true);
    updateSwatch(button, target);
    connect(button, &QPushButton::clicked, this, [this, button, &target] {
        const QColor chosen = QColorDialog::getColor(target, this, tr("Pick color"));
        if (chosen.isValid()) {
            target = chosen;
            updateSwatch(button, target);
        }
    });
    return button;
}

void ColorSchemeEditorDialog::updateSwatch(QPushButton *button, const QColor &color)
{
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;")
                              .arg(color.name()));
}

QString ColorSchemeEditorDialog::schemeName() const
{
    QString raw = name_->text().trimmed();
    QString safe;
    for (const QChar &ch : raw) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_'))
            safe.append(ch);
        else if (ch.isSpace())
            safe.append(QLatin1Char('_'));
    }
    return safe.isEmpty() ? tr("Custom") : safe;
}

TermColorScheme ColorSchemeEditorDialog::scheme() const
{
    TermColorScheme s;
    s.description = name_->text().trimmed();
    s.background = background_;
    s.foreground = foreground_;
    for (int i = 0; i < 8; ++i) {
        s.colors[i] = colors_[i];
        s.intense[i] = intense_[i];
    }
    return s;
}
