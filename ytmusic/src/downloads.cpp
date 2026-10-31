#include "downloads.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <csignal>
#include <sys/prctl.h>

static const QRegularExpression &idTag() {
    // "... [dQw4w9WgXcQ].m4a"
    static const QRegularExpression re(QStringLiteral("\\[([A-Za-z0-9_-]{11})\\]\\.(m4a|mp3|opus|ogg|webm|aac|flac|wav|mka)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString Downloads::defaultDir() {
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty()) music = QDir::homePath() + QStringLiteral("/Music");
    return music + QStringLiteral("/YT Music");
}

bool Downloads::haveFfmpeg() {
    static const bool have = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    return have;
}

Downloads::Downloads(QObject *parent) : QObject(parent) { dir = defaultDir(); }

Downloads::~Downloads() {
    for (QProcess *p : std::as_const(running)) {
        p->disconnect(this);
        p->kill();
        p->waitForFinished(500);
    }
}

void Downloads::setDir(const QString &d) {
    const QString n = QDir::cleanPath(d.trimmed());
    if (n.isEmpty() || n == dir) return;
    dir = n;
    rescan();
}

void Downloads::rescan() {
    files.clear();
    if (QDir(dir).exists()) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const auto m = idTag().match(path);
            if (m.hasMatch()) files.insert(m.captured(1), path);
        }
    }
    emit changed();
}

bool Downloads::isDownloaded(const QString &vid) const { return files.contains(vid); }

QString Downloads::pathFor(const QString &vid) const {
    const QString p = files.value(vid);
    return (!p.isEmpty() && QFileInfo::exists(p)) ? p : QString();
}

int Downloads::add(const QVector<Track> &tracks) {
    int n = 0;
    for (const Track &t : tracks) {
        if (t.vid.isEmpty() || isActive(t.vid) || !pathFor(t.vid).isEmpty()) continue;
        pending.append(t);
        queuedSet.insert(t.vid);
        ++n;
    }
    if (n) {
        if (running.isEmpty()) batchDone = batchFailed = 0;
        pump();
        emit changed();
    }
    return n;
}

void Downloads::cancel(const QString &vid) {
    for (int i = 0; i < pending.size(); ++i)
        if (pending[i].vid == vid) { pending.removeAt(i); break; }
    queuedSet.remove(vid);
    if (QProcess *p = running.take(vid)) {
        p->disconnect(this);
        p->kill();
        p->deleteLater();
        runningTrack.remove(vid);
        prog.remove(vid);
        finalPath.remove(vid);
        // yt-dlp leaves its half-finished file behind
        QDirIterator it(dir, {QStringLiteral("*[%1]*.part").arg(vid), QStringLiteral("*[%1]*.ytdl").arg(vid)}, QDir::Files);
        while (it.hasNext()) QFile::remove(it.next());
    }
    pump();
    emit changed();
}

void Downloads::cancelAll() {
    pending.clear();
    queuedSet.clear();
    const QStringList vids = running.keys();
    for (const QString &v : vids) cancel(v);
    emit changed();
}

bool Downloads::remove(const QString &vid) {
    const QString p = files.value(vid);
    if (p.isEmpty()) return false;
    const bool ok = QFile::remove(p) || !QFileInfo::exists(p);
    if (ok) files.remove(vid);
    emit changed();
    return ok;
}

void Downloads::pump() {
    while (running.size() < kParallel && !pending.isEmpty()) {
        const Track t = pending.takeFirst();
        queuedSet.remove(t.vid);
        start(t);
    }
}

// "Artist - Title", made safe for every filesystem the folder might end up on.
static QString safeName(const Track &t) {
    QString title = t.title.trimmed();
    QString artist = t.artist.trimmed();
    if (artist.endsWith(QLatin1String(" - Topic"))) artist.chop(8);
    QString s = (artist.isEmpty() || title.contains(artist, Qt::CaseInsensitive)) ? title : artist + QStringLiteral(" - ") + title;
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    s.replace(bad, QStringLiteral("_"));
    s = s.simplified();
    while (s.startsWith(QLatin1Char('.'))) s.remove(0, 1);
    if (s.size() > 150) s = s.left(150).trimmed();
    if (s.isEmpty()) s = QStringLiteral("Track");
    s.replace(QLatin1Char('%'), QStringLiteral("%%"));   // yt-dlp output template escape
    return s;
}

void Downloads::start(const Track &t) {
    if (!QDir().mkpath(dir)) {
        emit message(QStringLiteral("Can't create the download folder"));
        return;
    }
    auto *p = new QProcess(this);
    running.insert(t.vid, p);
    runningTrack.insert(t.vid, t);
    prog.insert(t.vid, 0.0);

    QStringList a;
    const bool ff = haveFfmpeg();
    if (format == Mp3 && ff) {
        a << QStringLiteral("-f") << QStringLiteral("bestaudio/best") << QStringLiteral("-x")
          << QStringLiteral("--audio-format") << QStringLiteral("mp3") << QStringLiteral("--audio-quality") << QStringLiteral("0");
    } else {
        // M4A plays everywhere and needs no conversion; fall back to whatever audio there is
        a << QStringLiteral("-f") << QStringLiteral("bestaudio[ext=m4a]/bestaudio/best");
        if (ff) a << QStringLiteral("-x");   // ffmpeg remuxes DASH audio into a normal file (no re-encode)
    }
    if (ff && embedTags)
        a << QStringLiteral("--embed-metadata") << QStringLiteral("--embed-thumbnail") << QStringLiteral("--convert-thumbnails")
          << QStringLiteral("jpg");
    a << QStringLiteral("--no-playlist") << QStringLiteral("--no-warnings") << QStringLiteral("--newline")
      << QStringLiteral("--no-mtime") << QStringLiteral("--progress") << QStringLiteral("--no-simulate")
      << QStringLiteral("--progress-template")
      << QStringLiteral("download:YTMP %(progress.downloaded_bytes)s %(progress.total_bytes)s %(progress.total_bytes_estimate)s")
      << QStringLiteral("--print") << QStringLiteral("after_move:YTMF %(filepath)s")
      << QStringLiteral("-o") << QDir(dir).filePath(safeName(t) + QStringLiteral(" [%(id)s].%(ext)s"))
      << QStringLiteral("https://www.youtube.com/watch?v=") + t.vid;

    const QString vid = t.vid;
    auto buf = std::make_shared<QByteArray>();
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, vid, buf] {
        buf->append(p->readAllStandardOutput());
        int nl;
        bool upd = false;
        while ((nl = buf->indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(buf->left(nl)).trimmed();
            buf->remove(0, nl + 1);
            if (line.startsWith(QLatin1String("YTMP "))) {
                const QStringList f = line.split(QLatin1Char(' '));
                const double got = f.value(1).toDouble();
                double total = f.value(2).toDouble();
                if (total <= 0) total = f.value(3).toDouble();
                if (total > 0) { prog[vid] = qBound(0.0, got / total, 1.0) * 0.95; upd = true; }   // last 5%: post-processing
            } else if (line.startsWith(QLatin1String("YTMF "))) {
                finalPath[vid] = line.mid(5);
            }
        }
        if (upd) emit changed();
    });
    connect(p, &QProcess::errorOccurred, this, [this, vid](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) finished(vid, -1, QStringLiteral("yt-dlp not found"));
    });
    connect(p, &QProcess::finished, this, [this, p, vid](int code, QProcess::ExitStatus st) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
        finished(vid, st == QProcess::NormalExit ? code : -1, err);
    });
    p->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    p->start(QStringLiteral("yt-dlp"), a);
}

void Downloads::finished(const QString &vid, int code, const QString &err) {
    QProcess *p = running.take(vid);
    if (!p) return;
    p->deleteLater();
    const Track t = runningTrack.take(vid);
    prog.remove(vid);
    QString path = finalPath.take(vid);
    if (code >= 0 && (path.isEmpty() || !QFileInfo::exists(path))) {
        // older yt-dlp without "after_move" printing, or only a post-processing step (cover art) failed:
        // the audio file itself may still be there, found by its tag
        path.clear();
        QDirIterator it(dir, {QStringLiteral("*[%1].*").arg(vid)}, QDir::Files);
        while (it.hasNext()) {
            const QString f = it.next();
            if (idTag().match(f).hasMatch()) { path = f; break; }
        }
    }
    if (code >= 0 && !path.isEmpty()) {
        files.insert(vid, path);
        ++batchDone;
    } else {
        ++batchFailed;
        emit message(QStringLiteral("Download failed: %1").arg(err.isEmpty() ? t.title : err.left(90)));
    }
    pump();
    if (running.isEmpty() && pending.isEmpty() && batchDone + batchFailed > 0) {
        if (batchDone == 1 && batchFailed == 0) emit message(QStringLiteral("Downloaded “%1”").arg(t.title));
        else if (batchDone > 0)
            emit message(QStringLiteral("Downloaded %1 song%2%3").arg(batchDone).arg(batchDone == 1 ? "" : "s")
                             .arg(batchFailed ? QStringLiteral(", %1 failed").arg(batchFailed) : QString()));
        batchDone = batchFailed = 0;
    }
    emit changed();
}
