#pragma once
#include "anim.h"
#include "core.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <functional>

// The window chrome, laid out like VoiceStudio: an icon rail, a library panel, and a top bar.

// 48 px icon rail: logo, the five pages, then lyrics / settings / hide.
class RailBar : public QWidget {
    Q_OBJECT
public:
    explicit RailBar(Core *core, QWidget *parent = nullptr);
    Tab tab = Tab::Home;
signals:
    void tabChanged(Tab t);
    void actionTriggered(int id);   // 7 lyrics, 8 settings
    void hideRequested();
    void dragRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    struct Item { QRectF r; int id; QString icon, tip; int badge; };
    QVector<Item> items() const;
    void kick(int id);
    Core *core;
    int hover = -1;
    Hover *hv;
    QElapsedTimer clock;
    QHash<int, qint64> kickAt;      // item id -> clock time its icon animation started
    QTimer kickTimer;
};

// Library panel: pinned Liked songs + your playlists, or the recently played list.
class SidePanel : public QWidget {
    Q_OBJECT
public:
    explicit SidePanel(Core *core, QWidget *parent = nullptr);
signals:
    void likedRequested();
    void playlistRequested(const QString &id);
    void collapseRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    int rowCount() const;
    QRectF rowRect(int i) const;
    QRectF listArea() const { return QRectF(0, 96, width(), height() - 96); }
    QRectF tabRect(int i) const;
    Core *core;
    bool showRecent = false;
    qreal scroll = 0;
    int hoverRow = -1, hoverTab = -1;
    bool hoverCollapse = false;
    Hover *hv;
};

// Top bar: panel toggle, the page title, and room on the right for the search box.
class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(QWidget *parent = nullptr);
    std::function<QString()> titleFn;
signals:
    void toggleRequested();
    void dragRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    QRectF toggleRect() const { return QRectF(12, 7, 30, 30); }
    bool hoverToggle = false;
    Hover *hv;
};
