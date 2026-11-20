#pragma once
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QWidget>

// VoiceStudio's motion: hover / colour changes ease over 150 ms with cubic-bezier(.4, 0, .2, 1).
inline qreal studioEase(qreal t) {
    static const QEasingCurve curve = [] {
        QEasingCurve c(QEasingCurve::BezierSpline);
        c.addCubicBezierSegment(QPointF(0.4, 0.0), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
        return c;
    }();
    return curve.valueForProgress(qBound(0.0, t, 1.0));
}

// Per-key 0..1 levels that glide toward their targets; repaints the owner while anything moves.
class Hover : public QObject {
public:
    explicit Hover(QWidget *owner, int ms = 150) : QObject(owner), w(owner), dur(ms) {
        timer.setInterval(16);
        connect(&timer, &QTimer::timeout, this, [this] { tick(); });
    }
    void set(int key, bool on) {
        const qreal t = on ? 1.0 : 0.0;
        if (target.value(key, 0.0) == t) return;
        target[key] = t;
        if (!level.contains(key)) level[key] = 0.0;
        if (!timer.isActive()) { clock.start(); timer.start(); }
    }
    // eased level for painting
    qreal get(int key) const { return studioEase(level.value(key, 0.0)); }
    void setOnly(int key) {                    // exactly one key is on (e.g. the hovered row); -1 = none
        for (auto it = target.begin(); it != target.end(); ++it) if (it.key() != key) set(it.key(), false);
        if (key != -2 && key != -1) set(key, true);
    }
private:
    void tick() {
        const qreal dt = clock.restart() / qreal(dur);
        bool busy = false;
        for (auto it = level.begin(); it != level.end();) {
            const qreal tg = target.value(it.key(), 0.0);
            qreal v = it.value();
            if (v < tg) v = qMin(tg, v + dt); else if (v > tg) v = qMax(tg, v - dt);
            it.value() = v;
            if (v != tg) busy = true;
            if (v == 0.0 && tg == 0.0) { target.remove(it.key()); it = level.erase(it); } else ++it;
        }
        if (!busy) timer.stop();
        w->update();
    }
    QWidget *w;
    int dur;
    QHash<int, qreal> level, target;
    QTimer timer;
    QElapsedTimer clock;
};

// One-shot 0..1 progress (page enter, fades) with an ease-out feel.
class OneShot : public QObject {
public:
    explicit OneShot(QWidget *owner, int ms = 150) : QObject(owner), w(owner), dur(ms) {
        timer.setInterval(16);
        connect(&timer, &QTimer::timeout, this, [this] { if (!active()) timer.stop(); w->update(); });
    }
    void start() { clock.start(); running = true; timer.start(); w->update(); }
    bool active() const { return running && clock.elapsed() < dur; }
    qreal value() const { return running ? studioEase(qreal(clock.elapsed()) / dur) : 1.0; }   // 0 -> 1
    qreal raw() const { return running ? qMin<qreal>(1.0, qreal(clock.elapsed()) / dur) : 1.0; }
private:
    QWidget *w;
    int dur;
    bool running = false;
    QElapsedTimer clock;
    QTimer timer;
};
