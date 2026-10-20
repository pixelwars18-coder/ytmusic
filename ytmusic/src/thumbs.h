#pragma once
#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QSet>

// YouTube thumbnails. Downloads go through a short-lived `curl` process into an
// on-disk cache (no TLS stack linked into this binary); images are decoded
// straight to the size they are drawn at, and only a few MB are kept in memory.
class Thumbs : public QObject {
    Q_OBJECT
public:
    static Thumbs &I();
    // Returns a cropped pixmap of the requested logical size, or a null pixmap
    // while it is still being fetched (ready() fires when it arrives).
    QPixmap get(const QString &vid, QSize logical, qreal dpr);
    // Same for an arbitrary image URL (playlist covers); `key` identifies it in the caches.
    QPixmap getUrl(const QString &key, const QString &url, QSize logical, qreal dpr);
    void clearMemory();
signals:
    void ready(const QString &key);
private:
    Thumbs();
    QString pathFor(const QString &key) const;
    void pump();
    QCache<QString, QPixmap> scaled{3 * 1024}; // KB
    QSet<QString> pending, failed;
    QQueue<QPair<QString, QString>> waiting; // key, url
    int running = 0;
    QString dir;
};
