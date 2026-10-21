#pragma once
#include "core.h"
#include <QHash>
#include <QTimer>
#include <QWidget>

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

    Core *core;
    QString vid, message, plain;
    QVector<Line> lines;
    QVector<QRectF> boxes;      // per line, content coordinates
    qreal contentH = 0, scrollY = 0, targetY = 0;
    qint64 lastWheel = 0;
    QTimer anim;
    QHash<QString, bool> inFlight;
};
