#pragma once

#include <QWidget>

class QComboBox;
class QPushButton;

// Content of the "Settings" tab (opened like a VS Code settings editor).
class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWidget(bool dark, const QString &colorScheme,
                            QWidget *parent = nullptr);

signals:
    void themeChanged(bool dark);
    void chooseFontRequested();
    void colorSchemeChanged(const QString &scheme);
    void languageChangeRequested(const QString &code); // "" = system, else "en"/"ru"...

private slots:
    void customizeScheme();
    void deleteScheme();

private:
    void reloadSchemes(const QString &select);
    QString currentScheme() const;
    void updateSchemeButtons();

    QComboBox *language_;
    QComboBox *startup_;
    QComboBox *colorScheme_;
    QPushButton *editSchemeButton_;
    QPushButton *deleteSchemeButton_;
};
