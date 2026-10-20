#pragma once
#include "core.h"
#include <QWidget>

// Bottom bar: now playing, transport, seek and volume. Fully custom-painted.
class PlayerBar : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBar(Core *core, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    enum Id { None, Heart, Shuffle, Prev, Play, Next, Repeat, Mute, SeekBar, VolBar };
    struct Layout {
        QRectF thumb, text, heart, shuffle, prev, play, next, repeat, mute;
        QRectF timeL, seek, timeR, volBar, volPct, volArea;
    };
    Layout layout() const;
    Id hitTest(const QPointF &pt) const;
    void dragTo(Id which, qreal x);

    Core *core;
    Id hover = None, dragging = None;
};
