#include "thumbs.h"
#include "paths.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QtMath>

namespace {
constexpr int kBatch = 8;      // images per curl process (they share one connection)
constexpr int kProcesses = 3;  // curl processes at a time

// Runs on a worker thread. Decodes at (just over) the final size (JPEG decoders do this cheaply
// via DCT scaling), crops to the target and rounds the corners.
QImage loadThumb(const QString &file, QSize px, qreal radiusPx) {
    if (px.isEmpty()) return {};
    QImageReader rd(file);
    const QSize src = rd.size();
    if (!src.isValid() || src.isEmpty()) return {};
    const qreal f = qMax(qreal(px.width()) / src.width(), qreal(px.height()) / src.height());
    rd.setScaledSize(QSize(qMax(px.width(), qCeil(src.width() * f)), qMax(px.height(), qCeil(src.height() * f))));
    QImage img = rd.read();
    if (img.isNull()) return {};
    img = img.copy((img.width() - px.width()) / 2, (img.height() - px.height()) / 2, px.width(), px.height());
    if (radiusPx <= 0) return img;
    QImage out(px, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(img));
    p.drawRoundedRect(QRectF(0, 0, px.width(), px.height()), radiusPx, radiusPx);
    p.end();
    return out;
}

// A JPEG that was cut off mid-transfer lacks the end-of-image marker.
bool looksComplete(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 128) return false;
    f.seek(f.size() - 2);
    return f.read(2) == QByteArray("\xFF\xD9", 2);
}
} // namespace

Thumbs &Thumbs::I() {
    static Thumbs t;
    return t;
}

Thumbs::Thumbs() {
    dir = Paths::cacheDir() + QStringLiteral("/thumbs");
    QDir().mkpath(dir);
    pool.setMaxThreadCount(2);
    pool.setExpiryTimeout(10000);
    // Drop thumbnails not used for a month. Not at startup: stat-ing a few thousand files there delays the first frame.
    QTimer::singleShot(20000, this, [this] {
        const QString d = dir;
        pool.start([d] {
            const QDateTime limit = QDateTime::currentDateTime().addDays(-30);
            const QFileInfoList old = QDir(d).entryInfoList(QDir::Files);
            for (const QFileInfo &fi : old)
                if (fi.lastModified() < limit) QFile::remove(fi.filePath());
        });
    });
}

QString Thumbs::pathFor(const QString &key) const {
    // video ids stay readable file names; other keys (URLs) are hashed
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_-]{6,20}$"));
    const QString name = plain.match(key).hasMatch()
        ? key
        : QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return dir + QLatin1Char('/') + name + QStringLiteral(".jpg");
}

void Thumbs::clearMemory() { scaled.clear(); }

QPixmap Thumbs::get(const QString &vid, QSize logical, qreal dpr, qreal radius) {
    return getUrl(vid, QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(vid), logical, dpr, radius);
}

QPixmap Thumbs::getUrl(const QString &vid, const QString &url, QSize logical, qreal dpr, qreal radius) {
    const QString key = vid + QLatin1Char('@') + QString::number(logical.width()) + QLatin1Char('x') +
                        QString::number(logical.height()) + QLatin1Char('@') + QString::number(dpr) + QLatin1Char('r') +
                        QString::number(qRound(radius));
    if (QPixmap *pm = scaled.object(key)) return *pm;
    if (decoding.contains(key) || failed.contains(vid)) return {};

    const QString file = pathFor(vid);
    if (onDisk.contains(vid) || QFile::exists(file)) {
        onDisk.insert(vid);
        decoding.insert(key);
        const QSize px(qRound(logical.width() * dpr), qRound(logical.height() * dpr));
        const qreal radiusPx = radius * dpr;
        pool.start([this, key, vid, file, px, radiusPx, dpr] {
            const QImage img = loadThumb(file, px, radiusPx);
            QMetaObject::invokeMethod(this, [this, key, vid, img, dpr] { decoded(key, vid, img, dpr); }, Qt::QueuedConnection);
        });
        return {};
    }

    if (!pending.contains(vid)) {
        pending.insert(vid);
        waiting.enqueue({vid, url});
        schedulePump();
    }
    return {};
}

void Thumbs::decoded(const QString &cacheKey, const QString &vid, const QImage &img, qreal dpr) {
    decoding.remove(cacheKey);
    if (img.isNull()) {
        // unreadable file: fetch it again once, then give up for this session
        QFile::remove(pathFor(vid));
        onDisk.remove(vid);
        if (retried.contains(vid)) failed.insert(vid);
        else retried.insert(vid);
        emit ready(vid);
        return;
    }
    auto *pm = new QPixmap(QPixmap::fromImage(img));
    pm->setDevicePixelRatio(dpr);
    const int cost = qMax(1, pm->width() * pm->height() * 4 / 1024);
    scaled.insert(cacheKey, pm, cost);   // the cache owns pm from here on
    emit ready(vid);
}

// One paint asks for a whole screen of thumbnails, one call at a time: start the
// downloads from the event loop so they can be grouped into a few curl processes.
void Thumbs::schedulePump() {
    if (pumpQueued) return;
    pumpQueued = true;
    QTimer::singleShot(0, this, [this] {
        pumpQueued = false;
        pump();
    });
}

void Thumbs::pump() {
    while (running < kProcesses && !waiting.isEmpty()) {
        QVector<QPair<QString, QString>> batch;   // key, part file
        QStringList args{QStringLiteral("-sfL"), QStringLiteral("--max-time"), QStringLiteral("15")};
        // Split the queue across the free processes so a small page still downloads in parallel.
        const int take = qBound(1, int((waiting.size() + kProcesses - running - 1) / (kProcesses - running)), kBatch);
        while (!waiting.isEmpty() && batch.size() < take) {
            const auto job = waiting.dequeue();
            const QString part = pathFor(job.first) + QStringLiteral(".part");
            batch.append({job.first, part});
            args << QStringLiteral("-o") << part << job.second;
        }
        if (batch.size() > 1) args << QStringLiteral("--parallel") << QStringLiteral("--parallel-max") << QString::number(kBatch);

        auto *p = new QProcess(this);
        ++running;
        connect(p, &QProcess::errorOccurred, this, [this, p, batch](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            for (const auto &job : batch) {
                failed.insert(job.first);
                pending.remove(job.first);
            }
            --running;
            p->deleteLater();
        });
        connect(p, &QProcess::finished, this, [this, p, batch](int code, QProcess::ExitStatus st) {
            --running;
            const bool clean = st == QProcess::NormalExit && code == 0;
            for (const auto &job : batch) {
                const QString &key = job.first, &part = job.second;
                const QString file = pathFor(key);
                pending.remove(key);
                // With several transfers the exit code only describes one of them: judge each file on its own.
                const bool ok = QFileInfo(part).size() > 0 && (clean || looksComplete(part));
                if (ok) QFile::remove(file);
                if (ok && QFile::rename(part, file)) {
                    onDisk.insert(key);
                    emit ready(key);
                } else {
                    failed.insert(key);
                    QFile::remove(part);
                }
            }
            p->deleteLater();
            pump();
        });
        p->start(QStringLiteral("curl"), args);
    }
}

QColor Thumbs::accent(const QString &vid) {
    if (vid.isEmpty()) return QColor();
    const auto it = accents.constFind(vid);
    if (it != accents.constEnd()) return *it;
    const QString file = pathFor(vid);
    if (!QFile::exists(file)) return QColor();
    QImage img(file);
    if (img.isNull()) return QColor();
    img = img.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    qreal r = 0, g = 0, b = 0, wsum = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            const qreal w = 0.05 + c.hsvSaturationF() * c.valueF();   // vivid pixels count most
            r += c.redF() * w; g += c.greenF() * w; b += c.blueF() * w; wsum += w;
        }
    QColor out = QColor::fromRgbF(r / wsum, g / wsum, b / wsum);
    float h, s, v, a;
    out.getHsvF(&h, &s, &v, &a);
    out = QColor::fromHsvF(h < 0 ? 0 : h, qMin<float>(1.0f, qMax(s, 0.45f)), qBound(0.45f, v, 0.85f));   // keep it colourful but not blinding
    accents.insert(vid, out);
    return out;
}
