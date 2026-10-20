#include "thumbs.h"
#include "paths.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>

Thumbs &Thumbs::I() {
    static Thumbs t;
    return t;
}

Thumbs::Thumbs() {
    dir = Paths::cacheDir() + QStringLiteral("/thumbs");
    QDir().mkpath(dir);
    // Drop thumbnails not used for a month.
    const QDateTime limit = QDateTime::currentDateTime().addDays(-30);
    const QFileInfoList old = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : old)
        if (fi.lastModified() < limit) QFile::remove(fi.filePath());
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

QPixmap Thumbs::get(const QString &vid, QSize logical, qreal dpr) {
    return getUrl(vid, QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(vid), logical, dpr);
}

QPixmap Thumbs::getUrl(const QString &vid, const QString &url, QSize logical, qreal dpr) {
    const QString key = vid + QLatin1Char('@') + QString::number(logical.width()) + QLatin1Char('x') +
                        QString::number(logical.height()) + QLatin1Char('@') + QString::number(dpr);
    if (QPixmap *pm = scaled.object(key)) return *pm;

    const QString file = pathFor(vid);
    if (QFile::exists(file)) {
        const QSize px(qRound(logical.width() * dpr), qRound(logical.height() * dpr));
        QImageReader rd(file);
        const QSize src = rd.size();
        if (!src.isValid()) { QFile::remove(file); return {}; }
        // Decode at (just over) the final size; JPEG decoders do this cheaply via DCT scaling.
        const qreal f = qMax(qreal(px.width()) / src.width(), qreal(px.height()) / src.height());
        rd.setScaledSize(QSize(qCeil(src.width() * f), qCeil(src.height() * f)));
        QImage img = rd.read();
        if (img.isNull()) return {};
        img = img.copy((img.width() - px.width()) / 2, (img.height() - px.height()) / 2, px.width(), px.height());
        auto *pm = new QPixmap(QPixmap::fromImage(std::move(img)));
        pm->setDevicePixelRatio(dpr);
        scaled.insert(key, pm, qMax(1, pm->width() * pm->height() * 4 / 1024));
        return *pm;
    }

    if (!pending.contains(vid) && !failed.contains(vid)) {
        pending.insert(vid);
        waiting.enqueue({vid, url});
        pump();
    }
    return {};
}

void Thumbs::pump() {
    while (running < 4 && !waiting.isEmpty()) {
        const auto job = waiting.dequeue();
        const QString vid = job.first, jurl = job.second;
        const QString file = pathFor(vid), part = file + QStringLiteral(".part");
        auto *p = new QProcess(this);
        ++running;
        connect(p, &QProcess::errorOccurred, this, [this, p, vid](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            failed.insert(vid);
            pending.remove(vid);
            --running;
            p->deleteLater();
        });
        connect(p, &QProcess::finished, this, [this, p, vid, file, part](int code) {
            --running;
            pending.remove(vid);
            if (code == 0 && QFile::exists(part) && QFile::rename(part, file)) emit ready(vid);
            else { failed.insert(vid); QFile::remove(part); }
            p->deleteLater();
            pump();
        });
        p->start(QStringLiteral("curl"), {QStringLiteral("-sfL"), QStringLiteral("--max-time"), QStringLiteral("15"),
                                          QStringLiteral("-o"), part,
                                          jurl});
    }
}
