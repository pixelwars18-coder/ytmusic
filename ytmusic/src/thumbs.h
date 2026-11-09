#pragma once
#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QThreadPool>

// YouTube thumbnails. Downloads go through short-lived `curl` processes into an
// on-disk cache (no TLS stack linked into this binary); several images share one
// curl (one connection) so a page of results arrives together. Images are decoded
// on a worker thread straight to the size and corner radius they are drawn at, so
// painting a row is a plain blit and scrolling never waits on a JPEG decode.
class Thumbs : public QObject {
    Q_OBJECT
public:
    static Thumbs &I();
    // Returns a cropped (and, with `radius`, round-cornered) pixmap of the requested logical
    // size, or a null pixmap while it is still being fetched / decoded (ready() fires when it arrives).
    QPixmap get(const QString &vid, QSize logical, qreal dpr, qreal radius = 0);
    // Same for an arbitrary image URL (playlist covers, large artwork); `key` identifies it in the caches.
    QPixmap getUrl(const QString &key, const QString &url, QSize logical, qreal dpr, qreal radius = 0);
    void clearMemory();
signals:
    void ready(const QString &key);
private:
    Thumbs();
    QString pathFor(const QString &key) const;
    void schedulePump();
    void pump();
    void decoded(const QString &cacheKey, const QString &key, const QImage &img, qreal dpr);
    QCache<QString, QPixmap> scaled{8 * 1024}; // KB
    QSet<QString> pending, failed, retried;
    QSet<QString> onDisk;                    // keys known to be in the disk cache (saves a stat per paint)
    QSet<QString> decoding;                  // cache keys being decoded right now
    QQueue<QPair<QString, QString>> waiting; // key, url
    int running = 0;
    bool pumpQueued = false;
    QString dir;
    QThreadPool pool;
};
