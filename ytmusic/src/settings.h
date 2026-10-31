#pragma once
#include "core.h"
#include <QAbstractButton>
#include <QDialog>

class QVBoxLayout;
class QFrame;
class QLabel;

namespace UI {
// Application-wide stylesheet in the Noctalia palette for the stock widgets we use:
// menus, dialogs (message boxes, name prompts, file pickers), combo boxes, buttons, tooltips.
QString appStyleSheet();
}

// Pill-shaped on/off toggle in the Noctalia style.
class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {44, 24}; }
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { hovered = true; update(); }
    void leaveEvent(QEvent *) override { hovered = false; update(); }
private:
    bool hovered = false;
};

// Round icon-only button (close, etc.).
class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    IconButton(const QString &icon, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {34, 34}; }
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { hovered = true; update(); }
    void leaveEvent(QEvent *) override { hovered = false; update(); }
private:
    QString icon;
    bool hovered = false;
};

// Frameless, rounded settings window: grouped cards of rows (title + hint on the left, control on the right).
// Every change applies immediately.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Core *core, QWidget *parent);
signals:
    void toast(const QString &text);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    QFrame *section(QVBoxLayout *into, const QString &title);
    void row(QFrame *card, const QString &title, const QString &hint, QWidget *control, QLabel **hintOut = nullptr);
    void restyle();
    Core *core;
};
