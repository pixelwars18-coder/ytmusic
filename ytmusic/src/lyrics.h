#pragma once
#include "core.h"
#include <QHash>
#include <QTimer>
#include <QWidget>

// The now-playing screen: large artwork, what plays next, and lyrics.
// Lyrics from LRCLIB (lrclib.net): free, no account. Time-synced lines when available.
class LyricsView : public QWidget {
    Q_OBJECT
public:
    explicit LyricsView(Core *core, QWidget *parent = nullptr);
    void toggle();
    void closeView();
signals:
    void closed();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    struct Line { double sec; QString text; };
    void load();                                   // for the playing track
    void apply(const QString &vid, const QString &lrc, const QString &plain, bool found);
    void fetch(const Track &t);
    QVector<Line> parseLrc(const QString &lrc) const;
    void layoutLines();
    int currentIndex() const;
    QString cachePath(const QString &vid) const;
    // Wide enough: artwork, title and "up next" on the left, lyrics on the right. Otherwise lyrics only.
    bool split() const { return width() >= 640; }
    qreal leftW() const { return split() ? qBound<qreal>(250, width() * 0.40, 400) : 0; }
    qreal lyrX() const { return split() ? leftW() + 36 : 40; }
    qreal lyrW() const { return qMax<qreal>(120, width() - lyrX() - 40); }

    Core *core;
    QString vid, message, plain;
    QVector<Line> lines;
    QVector<QRectF> boxes;      // per line, content coordinates
    qreal contentH = 0, scrollY = 0, targetY = 0;
    qint64 lastWheel = 0;
    QTimer anim;
    QHash<QString, bool> inFlight;
};
