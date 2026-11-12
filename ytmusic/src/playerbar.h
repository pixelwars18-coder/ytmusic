#pragma once
#include "core.h"
#include <QWidget>

// Bottom bar: now playing, transport, seek and volume. Fully custom-painted.
class PlayerBar : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBar(Core *core, QWidget *parent = nullptr);
signals:
    void expandRequested();   // artwork / title / lyrics button: open the now-playing screen
    void queueRequested();    // queue button
    void dragRequested();     // empty toolbar area: move the window
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    enum Id { None, Info, Heart, Shuffle, Prev, Play, Next, Repeat, Lyrics, Queue, Mute, SeekBar, VolBar };
    struct Layout {
        QRectF thumb, text, heart, shuffle, prev, play, next, repeat, lyrics, queue, mute;
        QRectF timeL, seek, timeR, volBar, volPct, volArea, lcd;
    };
    Layout layout() const;
    Layout layoutApple() const;
    void paintApple(QPainter &p, const Layout &l);
    Id hitTest(const QPointF &pt) const;
    void dragTo(Id which, qreal x);

    Core *core;
    Id hover = None, dragging = None;
};
