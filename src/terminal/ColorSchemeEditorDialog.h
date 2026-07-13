#pragma once

#include <QDialog>

#include "terminal/ColorSchemeStore.h"

class QLineEdit;
class QPushButton;

// Editor for a Konsole-format terminal colour scheme: background, foreground and
// the 8 ANSI colours (normal + intense). Saves into the user's custom scheme dir.
class ColorSchemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    ColorSchemeEditorDialog(const QString &name, const TermColorScheme &scheme,
                            bool nameEditable, QWidget *parent = nullptr);

    QString schemeName() const;
    TermColorScheme scheme() const;

private:
    QPushButton *makeSwatch(QColor &target);
    void updateSwatch(QPushButton *button, const QColor &color);

    QLineEdit *name_;
    TermColorScheme scheme_;
    QColor background_;
    QColor foreground_;
    QColor colors_[8];
    QColor intense_[8];
};
