#include "core.h"
#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <algorithm>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <cstring>
#include <functional>
#include <memory>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <csignal>
#include <unistd.h>

static QString watchUrl(const QString &vid) { return QStringLiteral("https://www.youtube.com/watch?v=") + vid; }

Core::Core(QObject *parent) : QObject(parent) {
    load();
    saveTimer.setSingleShot(true);
    saveTimer.setInterval(600);
    connect(&saveTimer, &QTimer::timeout, this, &Core::save);

    sleepTimer.setInterval(5000);
    connect(&sleepTimer, &QTimer::timeout, this, &Core::checkSleep);

    connectTimer.setInterval(300);
    connect(&connectTimer, &QTimer::timeout, this, &Core::tryConnect);
    posThrottle.start();
    // mpv is started on first playback and shut down again after a long idle period
    // (an idle mpv costs ~25 MB), then transparently restarted where it left off.
    mpvIdleTimer.setInterval(qEnvironmentVariableIsSet("YTMUSIC_IDLE_MS") ? 1000 : 30000); // env: dev/test knob
    connect(&mpvIdleTimer, &QTimer::timeout, this, &Core::checkMpvIdle);
    mpvIdleTimer.start();
    loaded = true;
}

Core::~Core() { shutdown(); }

void Core::shutdown() {
    if (mpvProc) {
        mpv({QStringLiteral("quit")});
        mpvProc->disconnect();
        connectTimer.stop();
        closeSocket();
        if (!mpvProc->waitForFinished(300)) mpvProc->kill();
        QFile::remove(sockPath);
        mpvProc = nullptr;
    }
    save();
}

// ---------------------------------------------------------------- persistence
// Same keys as the old QML build's state.json, so settings carry over.
static QString statePath() { return Paths::stateDir() + QStringLiteral("/state.json"); }

static QJsonArray tracksToJson(const QVector<Track> &v) {
    QJsonArray a;
    for (const Track &t : v) a.append(t.toJson());
    return a;
}
static QVector<Track> tracksFromJson(const QJsonArray &a) {
    QVector<Track> v;
    for (const QJsonValue &x : a) {
        Track t = Track::fromJson(x.toObject());
        if (!t.vid.isEmpty()) v.append(t);
    }
    return v;
}
static QJsonArray parseArray(const QString &s) { return QJsonDocument::fromJson(s.toUtf8()).array(); }
static QString dumpArray(const QJsonArray &a) { return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)); }

void Core::load() {
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    volume = o.value("volume").toDouble(80);
    pageSize = qBound(10, o.value("pageSize").toInt(25), 100);
    startHidden = o.value("startHidden").toBool(false);
    closeToTray = o.value("closeToTray").toBool(true);
    audioQuality = qBound(0, o.value("audioQuality").toInt(0), 2);
    memoryMode = qBound(0, o.value("memoryMode").toInt(1), 2);
    searchLimit = pageSize;
    shuffle = o.value("shuffle").toBool(false);
    repeat = qBound(0, o.value("repeatMode").toInt(0), 2);
    for (const QJsonValue &h : parseArray(o.value("historyJson").toString())) history << h.toString();
    liked = tracksFromJson(parseArray(o.value("likedJson").toString()));
    queue = tracksFromJson(parseArray(o.value("queueJson").toString()));
    recent = tracksFromJson(parseArray(o.value("recentJson").toString()));
    plays = o.value("plays").toInt();
    feedBuiltAt = qint64(o.value("feedAt").toDouble());
    feedPlaysAtBuild = o.value("feedPlays").toInt();
    for (const QJsonValue &v : parseArray(o.value("feedJson").toString())) {
        const QJsonObject fo = v.toObject();
        FeedSection fs;
        fs.id = fo.value("id").toString();
        fs.title = fo.value("title").toString();
        fs.subtitle = fo.value("subtitle").toString();
        fs.tracks = tracksFromJson(fo.value("tracks").toArray());
        if (!fs.tracks.isEmpty()) feed.append(fs);
    }
    for (const QJsonValue &v : parseArray(o.value("playlistsJson").toString())) {
        const QJsonObject po = v.toObject();
        SavedPlaylist p;
        p.id = po.value("id").toString();
        p.name = po.value("name").toString(QStringLiteral("Playlist"));
        p.source = po.value("source").toString();
        p.tracks = tracksFromJson(po.value("tracks").toArray());
        if (!p.id.isEmpty()) playlists.append(p);
    }
    const int ci = o.value("currentIdx").toInt(-1);
    if (ci >= 0 && ci < queue.size()) {
        current = ci;
        duration = queue[ci].duration;
    }
    rebuildLikedSet();
}

void Core::save() {
    if (!loaded) return;
    QDir().mkpath(Paths::stateDir());
    QJsonObject o;
    o["volume"] = qRound(volume);
    o["pageSize"] = pageSize;
    o["startHidden"] = startHidden;
    o["closeToTray"] = closeToTray;
    o["audioQuality"] = audioQuality;
    o["memoryMode"] = memoryMode;
    o["shuffle"] = shuffle;
    o["repeatMode"] = repeat;
    o["historyJson"] = dumpArray(QJsonArray::fromStringList(history));
    o["queueJson"] = dumpArray(tracksToJson(queue));
    o["likedJson"] = dumpArray(tracksToJson(liked));
    o["recentJson"] = dumpArray(tracksToJson(recent));
    o["plays"] = plays;
    o["feedAt"] = double(feedBuiltAt);
    o["feedPlays"] = feedPlaysAtBuild;
    {
        QJsonArray fa;
        for (const FeedSection &f : feed)
            fa.append(QJsonObject{{"id", f.id}, {"title", f.title}, {"subtitle", f.subtitle}, {"tracks", tracksToJson(f.tracks)}});
        o["feedJson"] = dumpArray(fa);
    }
    {
        QJsonArray pa;
        for (const SavedPlaylist &p : playlists)
            pa.append(QJsonObject{{"id", p.id}, {"name", p.name}, {"source", p.source}, {"tracks", tracksToJson(p.tracks)}});
        o["playlistsJson"] = dumpArray(pa);
    }
    o["currentIdx"] = current;
    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void Core::rebuildLikedSet() {
    likedSet.clear();
    for (const Track &t : std::as_const(liked)) likedSet.insert(t.vid);
}

// ------------------------------------------------------------------------ mpv
void Core::startMpv() {
    sockPath = Paths::runtimeDir() + QStringLiteral("/ytmusic-mpv-%1.sock").arg(QCoreApplication::applicationPid());
    QFile::remove(sockPath);
    mpvProc = new QProcess(this);
    connect(mpvProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        closeSocket();
        connectTimer.stop();
        QFile::remove(sockPath);
        if (mpvProc) mpvProc->deleteLater();
        mpvProc = nullptr;
        if (!idle && !paused) { // died while playing
            idle = true;
            paused = true;
            buffering = false;
            emit playbackChanged();
        }
    });
    connect(mpvProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            status = QStringLiteral("mpv not found. Install mpv to play audio.");
            emit searchStateChanged();
        }
    });
    mpvProc->setProcessChannelMode(QProcess::MergedChannels);
    // never leave an orphaned mpv behind, even if we are killed
    mpvProc->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    QStringList margs =
                   {QStringLiteral("--idle=yes"), QStringLiteral("--no-video"), QStringLiteral("--vid=no"),
                    QStringLiteral("--sid=no"), QStringLiteral("--audio-display=no"), QStringLiteral("--force-window=no"),
                    QStringLiteral("--no-terminal"), QStringLiteral("--ytdl=no"), QStringLiteral("--load-scripts=no"),
                    // mpv's default would read ahead up to 150 MiB of a stream
                    QStringLiteral("--demuxer-max-bytes=6MiB"), QStringLiteral("--demuxer-max-back-bytes=512KiB"),
                    QStringLiteral("--cache-secs=45"), QStringLiteral("--osd-level=0"),
                    
                    QStringLiteral("--input-default-bindings=no"),
                    QStringLiteral("--volume=%1").arg(volume), QStringLiteral("--input-ipc-server=") + sockPath};
    // Developer knob, e.g. YTMUSIC_MPV_ARGS="--ao=null --log-file=/tmp/mpv.log"
    margs += QString::fromLocal8Bit(qgetenv("YTMUSIC_MPV_ARGS")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    mpvProc->start(QStringLiteral("mpv"), margs);
    connectTimer.start();
}

void Core::ensureMpv() {
    if (!mpvProc) startMpv();
}

void Core::stopMpv() {
    if (!mpvProc) return;
    resumeVid = (idle || !nowPlaying()) ? QString() : nowVid();
    resumeAt = position;
    mpv({QStringLiteral("quit")});
    connectTimer.stop();
    closeSocket();
    // the finished handler cleans up
}

void Core::checkMpvIdle() {
    if (!mpvProc) return;
    if (!(idle || paused)) {
        inactiveSince.invalidate();
        return;
    }
    if (!inactiveSince.isValid()) inactiveSince.start();
    if (memoryMode == 2 && !qEnvironmentVariableIsSet("YTMUSIC_IDLE_MS")) return; // never release
    const qint64 limit = qEnvironmentVariableIsSet("YTMUSIC_IDLE_MS") ? qEnvironmentVariableIntValue("YTMUSIC_IDLE_MS")
                         : memoryMode == 0 ? (idle ? 30000 : 120000) : (idle ? 120000 : 600000);
    if (inactiveSince.elapsed() > limit) {
        inactiveSince.invalidate();
        stopMpv();
    }
}

void Core::tryConnect() {
    if (fd >= 0) return;
    const QByteArray path = sockPath.toLocal8Bit();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= int(sizeof(addr.sun_path))) return;
    memcpy(addr.sun_path, path.constData(), size_t(path.size()));
    const int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return;
    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(s);
        return;
    }
    fd = s;
    connectTimer.stop();
    notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
        char buf[4096];
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            closeSocket();
            if (mpvProc && mpvProc->state() == QProcess::Running) connectTimer.start();
            return;
        }
        sockBuf.append(buf, int(n));
        int nl;
        while ((nl = sockBuf.indexOf('\n')) >= 0) {
            mpvLine(sockBuf.left(nl));
            sockBuf.remove(0, nl + 1);
        }
    });
    mpv({QStringLiteral("observe_property"), 1, QStringLiteral("time-pos")});
    mpv({QStringLiteral("observe_property"), 2, QStringLiteral("duration")});
    mpv({QStringLiteral("observe_property"), 3, QStringLiteral("pause")});
    mpv({QStringLiteral("observe_property"), 4, QStringLiteral("mute")});
    if (!pendingLoad.isEmpty()) {
        const QJsonArray cmd = pendingLoad;
        pendingLoad = {};
        mpv(cmd);
    }
}

void Core::closeSocket() {
    delete notifier;
    notifier = nullptr;
    if (fd >= 0) ::close(fd);
    fd = -1;
    sockBuf.clear();
}

void Core::mpv(const QJsonArray &cmd) {
    if (fd < 0) {
        if (cmd.first().toString() == QLatin1String("loadfile")) {
            pendingLoad = cmd;
            ensureMpv();
        }
        return;
    }
    const QByteArray data = QJsonDocument(QJsonObject{{"command", cmd}}).toJson(QJsonDocument::Compact) + '\n';
    ssize_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + off, size_t(data.size() - off));
        if (n <= 0) return; // mpv gone; the reconnect timer will pick it up
        off += n;
    }
}

void Core::mpvLine(const QByteArray &line) {
    const QJsonObject m = QJsonDocument::fromJson(line).object();
    const QString ev = m.value("event").toString();
    if (ev == QLatin1String("property-change")) {
        const QString name = m.value("name").toString();
        const QJsonValue d = m.value("data");
        if (name == QLatin1String("time-pos") && d.isDouble()) {
            if (buffering || posThrottle.elapsed() >= 250) {
                posThrottle.restart();
                position = d.toDouble();
                const bool wasBuffering = buffering;
                buffering = false;
                errorStreak = 0;
                emit positionChanged();
                if (wasBuffering) {
                    emit playbackChanged();
                    preloadNext(); // audio is flowing: now resolve what comes next
                }
            }
        } else if (name == QLatin1String("duration") && d.isDouble() && d.toDouble() > 0) {
            duration = d.toDouble();
            emit playbackChanged();
        } else if (name == QLatin1String("pause") && d.isBool()) {
            paused = d.toBool();
            emit playbackChanged();
        } else if (name == QLatin1String("mute") && d.isBool()) {
            muted = d.toBool();
            emit volumeChanged();
        }
    } else if (ev == QLatin1String("end-file")) {
        onEndFile(m.value("reason").toString());
    }
}

void Core::onEndFile(const QString &reason) {
    if (reason == QLatin1String("eof")) {
        errorStreak = 0;
        if (repeat == RepeatOne) playIndex(current);
        else next(true);
    } else if (reason == QLatin1String("error")) {
        const Track *t = nowPlaying();
        if (usedDirect && t && !retried) { // resolved URL expired: resolve afresh, once
            retried = true;
            urlCache.remove(t->vid);
            loadTrack(*t);
            return;
        }
        trackFailed();
    }
}

void Core::trackFailed() {
    const Track *t = nowPlaying();
    status = QStringLiteral("Couldn't play \u201c%1\u201d").arg(t ? t->title : QStringLiteral("track"));
    emit searchStateChanged();
    if (++errorStreak < queue.size()) {
        next(true);
    } else {
        idle = true;
        paused = true;
        buffering = false;
        emit playbackChanged();
    }
}

// ------------------------------------------------------------------- preload
// While a track plays, resolve the direct audio URL of the next one so that
// advancing is instant instead of waiting 2-3 s for yt-dlp inside mpv.
void Core::preloadNext() {
    if (queue.isEmpty()) return;
    nextPick = pickNext(true);
    if (nextPick < 0 || nextPick >= queue.size()) return;
    resolveUrl(queue[nextPick].vid);
}

void Core::resolveUrl(const QString &vid) {
    auto it = urlCache.constFind(vid);
    if (it != urlCache.constEnd() && it->when.secsTo(QDateTime::currentDateTime()) < 3600 * 4) return;
    if (resolving.contains(vid)) return;
    auto *p = new QProcess(this);
    resolving.insert(vid, p);
    connect(p, &QProcess::errorOccurred, this, [this, vid, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        resolving.remove(vid);
        status = QStringLiteral("yt-dlp not found. Install yt-dlp to play.");
        if (waitingVid == vid) { waitingVid.clear(); trackFailed(); }
        p->deleteLater();
    });
    connect(p, &QProcess::finished, this, [this, p, vid](int code, QProcess::ExitStatus) {
        resolving.remove(vid);
        const QString url = QString::fromUtf8(p->readAllStandardOutput()).trimmed().section('\n', 0, 0);
        const bool ok = code == 0 && url.startsWith(QLatin1String("http"));
        if (ok) urlCache.insert(vid, {url, QDateTime::currentDateTime()});
        p->deleteLater();
        if (waitingVid == vid) {
            waitingVid.clear();
            if (ok) loadResolved(vid);
            else trackFailed();
        }
    });
    p->setProcessChannelMode(QProcess::SeparateChannels);
    p->start(QStringLiteral("yt-dlp"), {QStringLiteral("-f"), audioQuality == 2 ? QStringLiteral("bestaudio[abr<=64]/worstaudio") : audioQuality == 1 ? QStringLiteral("bestaudio[abr<=128]/bestaudio/best") : QStringLiteral("bestaudio/best"), QStringLiteral("-g"),
                                        QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings"), watchUrl(vid)});
}

void Core::loadResolved(const QString &vid) {
    const auto it = urlCache.constFind(vid);
    if (it == urlCache.constEnd() || nowVid() != vid) return;
    usedDirect = true;
    QString opts = QStringLiteral("pause=no");
    if (resumeVid == vid && resumeAt > 1) opts += QStringLiteral(",start=%1").arg(resumeAt, 0, 'f', 1);
    resumeVid.clear();
    if (speed != 1.0) opts += QStringLiteral(",speed=%1").arg(speed);
    mpv({QStringLiteral("loadfile"), it->url, QStringLiteral("replace"), -1, opts});
}

// mpv itself runs without the youtube-dl hook (smaller, no Lua): we always hand it a
// direct stream URL, either preloaded earlier or resolved right now.
void Core::loadTrack(const Track &t) {
    usedDirect = false;
    waitingVid.clear();
    auto it = urlCache.constFind(t.vid);
    if (it != urlCache.constEnd() && it->when.secsTo(QDateTime::currentDateTime()) < 3600 * 4) {
        loadResolved(t.vid);
    } else {
        waitingVid = t.vid;
        mpv({QStringLiteral("stop")});
        resolveUrl(t.vid);
        if (!resolving.contains(t.vid)) { // spawn failed synchronously
            waitingVid.clear();
        }
    }
}

// --------------------------------------------------------------------- search
static bool trackFromJsonObj(const QJsonObject &j, Track &t) {
    const QString id = j.value("id").toString();
    if (id.isEmpty()) return false;
    t.vid = id;
    t.title = j.value("title").toString(QStringLiteral("Unknown"));
    t.artist = j.value("channel").toString(j.value("uploader").toString(j.value("playlist_uploader").toString()));
    t.duration = j.value("duration").toDouble();
    return true;
}

static QString firstLine(const QByteArray &b) { return QString::fromUtf8(b).trimmed().section('\n', 0, 0); }

void Core::search(const QString &qIn, bool more) {
    const QString q = qIn.trimmed();
    if (q.isEmpty()) return;
    searchIsUrl = QRegularExpression(QStringLiteral("^https?://")).match(q).hasMatch();
    if (searchIsUrl) searchMode = SearchMode::Songs;
    const bool plMode = searchMode == SearchMode::Playlists;
    if (!more) {
        history.removeAll(q);
        for (int i = history.size() - 1; i >= 0; --i)
            if (history[i].compare(q, Qt::CaseInsensitive) == 0) history.removeAt(i);
        history.prepend(q);
        while (history.size() > 12) history.removeLast();
        results.clear();
        playlistResults.clear();
        detailOpen = false;
        searchLimit = pageSize;
        status = QStringLiteral("Searching…");
        scheduleSave();
        emit settingsChanged();
        emit resultsChanged();
    } else {
        searchLimit += pageSize;
    }
    searchQuery = q;
    if (ytProc) {
        ytProc->disconnect(this);
        ytProc->kill();
        ytProc->deleteLater();
    }
    searching = true;
    emit searchStateChanged();

    QStringList args;
    if (searchIsUrl) args << q;
    else if (plMode)
        // YouTube's own "playlists only" results filter
        args << QStringLiteral("https://www.youtube.com/results?search_query=%1&sp=EgIQAw%253D%253D")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(q)));
    else args << QStringLiteral("ytsearch%1:%2").arg(searchLimit).arg(q);
    args << QStringLiteral("--flat-playlist") << QStringLiteral("--dump-json") << QStringLiteral("--no-warnings")
         << QStringLiteral("--ignore-errors");
    if (!searchIsUrl) {
        if (more) args << QStringLiteral("--playlist-items") << QStringLiteral("%1-%2").arg(searchLimit - pageSize + 1).arg(searchLimit);
        else if (plMode) args << QStringLiteral("--playlist-end") << QString::number(searchLimit);
    }

    auto *p = ytProc = new QProcess(this);
    auto buf = std::make_shared<QByteArray>();
    auto parse = [this, buf, plMode](bool flush) {
        int nl;
        while ((nl = buf->indexOf('\n')) >= 0 || (flush && !buf->isEmpty())) {
            const QByteArray line = nl >= 0 ? buf->left(nl) : *buf;
            buf->remove(0, nl >= 0 ? nl + 1 : buf->size());
            const QJsonObject j = QJsonDocument::fromJson(line).object();
            if (plMode) {
                const QString url = j.value("url").toString();
                if (!url.contains(QLatin1String("list="))) continue;
                PlaylistRef r;
                r.url = url;
                r.name = j.value("title").toString(QStringLiteral("Playlist"));
                r.uploader = j.value("channel").toString(j.value("uploader").toString());
                const QJsonArray th = j.value("thumbnails").toArray();
                if (!th.isEmpty()) r.thumbUrl = th.first().toObject().value("url").toString();
                playlistResults.append(r);
            } else {
                Track t;
                if (trackFromJsonObj(j, t)) results.append(t);
            }
        }
        emit resultsChanged();
    };
    connect(p, &QProcess::readyReadStandardOutput, this, [p, buf, parse] {
        buf->append(p->readAllStandardOutput());
        parse(false);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && p == ytProc) {
            status = QStringLiteral("yt-dlp not found. Install yt-dlp to search.");
            searching = false;
            emit searchStateChanged();
        }
    });
    connect(p, &QProcess::finished, this, [this, p, buf, parse] {
        if (p != ytProc) return;
        buf->append(p->readAllStandardOutput());
        parse(true);
        searching = false;
        if (hasResults()) status.clear();
        else {
            const QString e = firstLine(p->readAllStandardError());
            status = e.isEmpty() ? QStringLiteral("No results") : e;
        }
        ytProc = nullptr;
        p->deleteLater();
        emit searchStateChanged();
    });
    p->start(QStringLiteral("yt-dlp"), args);
}

void Core::setSearchMode(SearchMode m) {
    if (m == searchMode) return;
    searchMode = m;
    detailOpen = false;
    if (!searchQuery.isEmpty() && !hasResults() && !searching) search(searchQuery, false);
    else {
        emit resultsChanged();
        emit searchStateChanged();
    }
}

// ----------------------------------------------------------------- playlists
// Runs yt-dlp on a playlist URL, handing every entry to `onTrack`; `done(errorLine)` fires at the end.
static QProcess *runPlaylistProc(QObject *parent, const QString &url, int limit, std::function<void(const Track &)> onTrack,
                                 std::function<void(const QString &)> done) {
    auto *p = new QProcess(parent);
    auto buf = std::make_shared<QByteArray>();
    auto parse = [buf, onTrack](bool flush) {
        int nl;
        while ((nl = buf->indexOf('\n')) >= 0 || (flush && !buf->isEmpty())) {
            const QByteArray line = nl >= 0 ? buf->left(nl) : *buf;
            buf->remove(0, nl >= 0 ? nl + 1 : buf->size());
            Track t;
            if (trackFromJsonObj(QJsonDocument::fromJson(line).object(), t)) onTrack(t);
        }
    };
    QObject::connect(p, &QProcess::readyReadStandardOutput, parent, [p, buf, parse] {
        buf->append(p->readAllStandardOutput());
        parse(false);
    });
    QObject::connect(p, &QProcess::errorOccurred, parent, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) done(QStringLiteral("yt-dlp not found"));
    });
    QObject::connect(p, &QProcess::finished, parent, [p, buf, parse, done] {
        buf->append(p->readAllStandardOutput());
        parse(true);
        done(firstLine(p->readAllStandardError()));
        p->deleteLater();
    });
    p->start(QStringLiteral("yt-dlp"), {url, QStringLiteral("--flat-playlist"), QStringLiteral("--dump-json"),
                                        QStringLiteral("--no-warnings"), QStringLiteral("--ignore-errors"),
                                        QStringLiteral("--playlist-end"), QString::number(limit)});
    return p;
}

void Core::openPlaylist(const PlaylistRef &r) {
    if (detailProc) {
        detailProc->disconnect(this);
        detailProc->kill();
        detailProc->deleteLater();
        detailProc = nullptr;
    }
    detailOpen = true;
    detail = {};
    detail.title = r.name;
    detail.url = r.url;
    detail.loading = true;
    emit resultsChanged();
    detailProc = runPlaylistProc(
        this, r.url, 500, [this](const Track &t) { detail.tracks.append(t); emit resultsChanged(); },
        [this](const QString &err) {
            detail.loading = false;
            if (detail.tracks.isEmpty()) detail.status = err.isEmpty() ? QStringLiteral("Empty playlist") : err;
            detailProc = nullptr;
            emit resultsChanged();
        });
}

void Core::closeDetail() {
    if (detailProc) {
        detailProc->disconnect(this);
        detailProc->kill();
        detailProc->deleteLater();
        detailProc = nullptr;
    }
    detailOpen = false;
    emit resultsChanged();
}

void Core::saveRemotePlaylist(const QString &url, const QString &name) {
    if (detailOpen && detail.url == url && !detail.loading && !detail.tracks.isEmpty()) {
        createPlaylist(name, detail.tracks, url);
        emit message(QStringLiteral("Saved “%1” (%2 songs)").arg(name).arg(detail.tracks.size()));
        return;
    }
    emit message(QStringLiteral("Saving “%1”…").arg(name));
    auto tracks = std::make_shared<QVector<Track>>();
    runPlaylistProc(this, url, 500, [tracks](const Track &t) { tracks->append(t); },
                    [this, tracks, name, url](const QString &err) {
                        if (tracks->isEmpty()) {
                            emit message(err.isEmpty() ? QStringLiteral("Playlist is empty") : err);
                            return;
                        }
                        createPlaylist(name, *tracks, url);
                        emit message(QStringLiteral("Saved “%1” (%2 songs)").arg(name).arg(tracks->size()));
                    });
}

QString Core::createPlaylist(const QString &name, const QVector<Track> &tracks, const QString &source) {
    SavedPlaylist p;
    p.id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    p.name = name.trimmed().isEmpty() ? QStringLiteral("New playlist") : name.trimmed();
    p.source = source;
    p.tracks = tracks;
    playlists.prepend(p);
    scheduleSave();
    emit listsChanged();
    return p.id;
}

int Core::addToPlaylist(const QString &id, const QVector<Track> &add) {
    for (SavedPlaylist &p : playlists) {
        if (p.id != id) continue;
        QSet<QString> have;
        for (const Track &t : std::as_const(p.tracks)) have.insert(t.vid);
        int n = 0;
        for (const Track &t : add)
            if (!have.contains(t.vid)) { p.tracks.append(t); have.insert(t.vid); ++n; }
        scheduleSave();
        emit listsChanged();
        return n;
    }
    return 0;
}

void Core::removeFromSaved(int index) {
    for (SavedPlaylist &p : playlists) {
        if (p.id != savedOpenId) continue;
        if (index >= 0 && index < p.tracks.size()) p.tracks.removeAt(index);
        scheduleSave();
        emit listsChanged();
        return;
    }
}

void Core::renamePlaylist(const QString &id, const QString &name) {
    for (SavedPlaylist &p : playlists)
        if (p.id == id) p.name = name.trimmed().isEmpty() ? QStringLiteral("Playlist") : name.trimmed();
    scheduleSave();
    emit listsChanged();
}

void Core::deletePlaylist(const QString &id) {
    for (int i = 0; i < playlists.size(); ++i)
        if (playlists[i].id == id) { playlists.removeAt(i); break; }
    if (savedOpenId == id) savedOpenId.clear();
    scheduleSave();
    emit listsChanged();
}

void Core::openSaved(const QString &id) { savedOpenId = id; emit listsChanged(); }
void Core::closeSaved() { savedOpenId.clear(); emit listsChanged(); }

// ------------------------------------------------------------------ playback
void Core::playIndex(int i) {
    if (i < 0 || i >= queue.size()) return;
    current = i;
    position = 0;
    duration = queue[i].duration;
    idle = false;
    buffering = true;
    nextPick = -1;
    retried = false;
    loadTrack(queue[i]);
    scheduleSave();
    emit trackChanged();
    emit playbackChanged();
    emit positionChanged();
    emit trackStarted(queue[i]);
    // listening history for the "For you" page
    const Track played = queue[i];
    for (int k = 0; k < recent.size(); ++k)
        if (recent[k].vid == played.vid) { recent.removeAt(k); break; }
    recent.prepend(played);
    while (recent.size() > 60) recent.removeLast();
    ++plays;
}

void Core::playFrom(Tab t, int i, bool doShuffle) { playTracks(list(t), i, doShuffle); }

void Core::playTracks(const QVector<Track> &src, int i, bool doShuffle) {
    if (src.isEmpty()) return;
    queue = src;
    if (doShuffle) {
        setShuffle(true);
        i = int(QRandomGenerator::global()->bounded(queue.size()));
    }
    scheduleSave();
    emit listsChanged();
    playIndex(i);
}

void Core::enqueue(Tab t, int i) {
    const QVector<Track> &src = list(t);
    if (i < 0 || i >= src.size()) return;
    queue.append(src[i]);
    scheduleSave();
    emit listsChanged();
    if (current < 0) playIndex(queue.size() - 1);
}

void Core::playNext(Tab t, int i) {
    const QVector<Track> &src = list(t);
    if (i < 0 || i >= src.size()) return;
    if (current < 0) {
        enqueue(t, i);
        return;
    }
    queue.insert(current + 1, src[i]);
    nextPick = -1;
    scheduleSave();
    emit listsChanged();
    if (!shuffle && !paused) preloadNext();
}

void Core::enqueueAll(Tab t) {
    queue += list(t);
    scheduleSave();
    emit listsChanged();
    if (current < 0 && !queue.isEmpty()) playIndex(0);
}

void Core::clearQueue() {
    queue.clear();
    current = -1;
    idle = true;
    position = duration = 0;
    nextPick = -1;
    mpv({QStringLiteral("stop")});
    scheduleSave();
    emit listsChanged();
    emit trackChanged();
    emit playbackChanged();
}

void Core::removeFromQueue(int i) {
    if (i < 0 || i >= queue.size()) return;
    queue.removeAt(i);
    nextPick = -1;
    if (i < current) --current;
    else if (i == current) {
        current = -1;
        idle = true;
        mpv({QStringLiteral("stop")});
        emit playbackChanged();
    }
    scheduleSave();
    emit listsChanged();
    emit trackChanged();
}

int Core::pickNext(bool automatic) {
    const int n = queue.size();
    if (n == 0) return -1;
    if (shuffle && n > 1) {
        if (nextPick >= 0 && nextPick < n && nextPick != current) return nextPick;
        int j;
        do { j = int(QRandomGenerator::global()->bounded(n)); } while (j == current);
        return j;
    }
    if (current + 1 < n) return current + 1;
    if (repeat == RepeatAll || !automatic) return 0;
    return -1;
}

void Core::next(bool automatic) {
    const int j = pickNext(automatic);
    nextPick = -1;
    if (j >= 0) {
        playIndex(j);
    } else {
        idle = true;
        paused = true;
        buffering = false;
        emit playbackChanged();
    }
}

void Core::prev() {
    if (position > 5 || current <= 0) seekTo(0);
    else playIndex(current - 1);
}

void Core::togglePlay() {
    if ((current < 0 || idle) && !queue.isEmpty()) {
        playIndex(qMax(current, 0));
    } else if (!mpvProc && nowPlaying()) { // mpv was shut down while paused: resume where we were
        resumeVid = nowVid();
        resumeAt = position;
        const double keep = position;
        playIndex(current);
        position = keep;
    } else {
        mpv({QStringLiteral("cycle"), QStringLiteral("pause")});
    }
}

void Core::play() {
    if (paused || idle || current < 0) togglePlay();
}

void Core::pause() { mpv({QStringLiteral("set_property"), QStringLiteral("pause"), true}); }

void Core::stop() {
    mpv({QStringLiteral("stop")});
    idle = true;
    paused = true;
    position = 0;
    emit playbackChanged();
    emit positionChanged();
}

void Core::setShuffle(bool on) {
    if (shuffle == on) return;
    shuffle = on;
    nextPick = -1;
    scheduleSave();
    emit settingsChanged();
}
void Core::toggleShuffle() { setShuffle(!shuffle); }
void Core::setRepeat(int r) {
    repeat = qBound(0, r, 2);
    scheduleSave();
    emit settingsChanged();
}
void Core::cycleRepeat() { setRepeat((repeat + 1) % 3); }

void Core::setSpeed(double s) {
    speed = qBound(0.5, s, 2.0);
    mpv({QStringLiteral("set_property"), QStringLiteral("speed"), speed});
    emit message(QStringLiteral("Speed %1×").arg(speed, 0, 'g', 3));
}

void Core::startRadio(const Track &t) {
    emit message(QStringLiteral("Starting radio…"));
    auto tracks = std::make_shared<QVector<Track>>();
    runPlaylistProc(this, QStringLiteral("https://www.youtube.com/watch?v=%1&list=RD%1").arg(t.vid), 40,
                    [tracks, vid = t.vid](const Track &x) { if (x.vid != vid) tracks->append(x); },
                    [this, tracks, t](const QString &) {
                        if (tracks->isEmpty()) { emit message(QStringLiteral("No similar songs found")); return; }
                        QVector<Track> all{t};
                        all += *tracks;
                        playTracks(all, 0, false);
                    });
}

void Core::setVolume(double v) {
    volume = qBound(0.0, v, 100.0);
    mpv({QStringLiteral("set_property"), QStringLiteral("volume"), volume});
    if (muted && volume > 0) mpv({QStringLiteral("set_property"), QStringLiteral("mute"), false});
    scheduleSave();
    emit volumeChanged();
}
void Core::toggleMute() { mpv({QStringLiteral("cycle"), QStringLiteral("mute")}); }

void Core::seekTo(double sec) {
    position = qMax(0.0, sec);
    mpv({QStringLiteral("seek"), position, QStringLiteral("absolute")});
    emit positionChanged();
    emit seeked(position);
}
void Core::seekBy(double sec) {
    mpv({QStringLiteral("seek"), sec, QStringLiteral("relative")});
    QTimer::singleShot(250, this, [this] { emit seeked(position); });
}

void Core::toggleLike(const Track &t) {
    if (t.vid.isEmpty()) return;
    if (likedSet.contains(t.vid)) {
        for (int i = 0; i < liked.size(); ++i)
            if (liked[i].vid == t.vid) { liked.removeAt(i); break; }
    } else {
        liked.prepend(t);
    }
    rebuildLikedSet();
    scheduleSave();
    emit listsChanged();
}

void Core::clearRecentPlays() {
    recent.clear();
    feed.clear();
    feedBuiltAt = 0;
    scheduleSave();
    emit feedChanged();
    emit listsChanged();
}

int Core::removeQueueDuplicates() {
    QSet<QString> seen;
    if (current >= 0 && current < queue.size()) seen.insert(queue[current].vid);
    QVector<int> drop;
    for (int i = 0; i < queue.size(); ++i)
        if (i != current) {
            if (seen.contains(queue[i].vid)) drop.append(i);
            else seen.insert(queue[i].vid);
        }
    if (!drop.isEmpty()) removeFromQueueMany(drop);
    return drop.size();
}

// Same format as the Android app's backup (vid/title/artist/duration), so a library can move between them.
QString Core::exportBackup(const QString &path) const {
    QJsonObject root{{"app", "ytmusic"}, {"version", 1}};
    root["liked"] = tracksToJson(liked);
    root["history"] = QJsonArray::fromStringList(history);
    QJsonArray pa;
    for (const SavedPlaylist &p : playlists)
        pa.append(QJsonObject{{"id", p.id}, {"name", p.name}, {"source", p.source}, {"tracks", tracksToJson(p.tracks)}});
    root["playlists"] = pa;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return f.errorString();
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit() ? QString() : f.errorString();
}

QString Core::importBackup(const QString &path, bool *ok) {
    *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return f.errorString();
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value("app").toString() != QLatin1String("ytmusic")) return QStringLiteral("Not a YT Music backup");
    int nLiked = 0, nPl = 0, nTracks = 0;
    for (const Track &t : tracksFromJson(o.value("liked").toArray()))
        if (!likedSet.contains(t.vid)) { liked.append(t); likedSet.insert(t.vid); ++nLiked; }
    for (const QJsonValue &h : o.value("history").toArray())
        if (!history.contains(h.toString())) history << h.toString();
    for (const QJsonValue &v : o.value("playlists").toArray()) {
        const QJsonObject po = v.toObject();
        const QString id = po.value("id").toString();
        const QVector<Track> tr = tracksFromJson(po.value("tracks").toArray());
        SavedPlaylist *have = nullptr;
        for (SavedPlaylist &p : playlists) if (p.id == id) have = &p;
        if (!have) {
            SavedPlaylist p;
            p.id = id;
            p.name = po.value("name").toString(QStringLiteral("Playlist"));
            p.source = po.value("source").toString();
            p.tracks = tr;
            playlists.append(p);
            ++nPl;
            nTracks += tr.size();
        } else {
            QSet<QString> known;
            for (const Track &t : std::as_const(have->tracks)) known.insert(t.vid);
            for (const Track &t : tr) if (!known.contains(t.vid)) { have->tracks.append(t); known.insert(t.vid); ++nTracks; }
        }
    }
    scheduleSave();
    emit listsChanged();
    emit settingsChanged();
    *ok = true;
    return QStringLiteral("Restored %1 liked songs, %2 playlists, %3 playlist songs").arg(nLiked).arg(nPl).arg(nTracks);
}

void Core::setPageSize(int n) {
    pageSize = qBound(10, n, 100);
    scheduleSave();
}

void Core::setCloseToTray(bool on) {
    closeToTray = on;
    scheduleSave();
}

void Core::setAudioQuality(int q) {
    audioQuality = qBound(0, q, 2);
    urlCache.clear(); // cached URLs are for the old format
    scheduleSave();
}

void Core::setMemoryMode(int m) {
    memoryMode = qBound(0, m, 2);
    scheduleSave();
}

void Core::setStartHidden(bool on) {
    startHidden = on;
    scheduleSave();
}

void Core::clearHistory() {
    history.clear();
    scheduleSave();
    emit settingsChanged();
}

// ---------------------------------------------------------------------- sleep
void Core::setSleep(int minutes) {
    sleepMinutes = minutes;
    if (minutes > 0) {
        sleepEnds = QDateTime::currentDateTime().addSecs(minutes * 60);
        sleepTimer.start();
    } else {
        sleepTimer.stop();
    }
    emit settingsChanged();
}

void Core::checkSleep() {
    if (QDateTime::currentDateTime() >= sleepEnds) {
        pause();
        setSleep(0);
    }
}

QString Core::sleepLabel() const {
    if (sleepMinutes <= 0) return {};
    const qint64 left = qMax<qint64>(1, (QDateTime::currentDateTime().secsTo(sleepEnds) + 59) / 60);
    return QStringLiteral("sleep in %1m").arg(left);
}


// -------------------------------------------------------------------- For you
void Core::enqueueTrack(const Track &t) {
    queue.append(t);
    scheduleSave();
    emit listsChanged();
    if (current < 0) playIndex(queue.size() - 1);
}

void Core::playNextTrack(const Track &t) {
    if (current < 0) { enqueueTrack(t); return; }
    queue.insert(current + 1, t);
    nextPick = -1;
    scheduleSave();
    emit listsChanged();
}

void Core::refreshFeedIfStale() {
    const bool stale = feed.isEmpty() || QDateTime::currentMSecsSinceEpoch() - feedBuiltAt > 4LL * 3600 * 1000 || plays - feedPlaysAtBuild >= 5;
    if (stale) refreshFeed();
}

// Same recipe as the Android app: YouTube's Mix for a few recent / liked songs, merged and ranked by how many of
// them point at a song ("Made for you"), a shelf per liked song, and a global chart for everything else.
void Core::refreshFeed() {
    if (feedBuilding) return;
    feedBuilding = true;
    emit feedChanged();

    static const QString kCharts = QStringLiteral("https://www.youtube.com/playlist?list=PL4fGSI1pDJn6puJdseH2Rt9sMvt9E2M4i");
    auto *rng = QRandomGenerator::global();

    // seeds for the mix: distinct artists among the latest plays + a couple of random likes
    QVector<Track> seeds;
    QSet<QString> artists;
    auto firstOfArtist = [&artists](const QString &a) { if (artists.contains(a)) return false; artists.insert(a); return true; };
    for (int i = 0; i < qMin(24, recent.size()) && seeds.size() < 4; ++i)
        if (firstOfArtist(recent[i].artist)) seeds.append(recent[i]);
    QVector<Track> likedShuffled = liked;
    std::shuffle(likedShuffled.begin(), likedShuffled.end(), *rng);
    for (const Track &t : std::as_const(likedShuffled)) {
        if (seeds.size() >= 6) break;
        bool already = false;
        for (const Track &s : seeds) already |= s.vid == t.vid;
        if (!already && firstOfArtist(t.artist)) seeds.append(t);
    }
    QVector<Track> because = likedShuffled.mid(0, 2);

    struct Job {
        int pending = 0;
        QHash<QString, QVector<Track>> mixes; // seed vid -> neighbours
        QVector<Track> charts;
        QVector<Track> seeds, because, recentCopy, likedCopy;
        int playsAtStart = 0;
    };
    auto job = std::make_shared<Job>();
    job->seeds = seeds;
    job->because = because;
    job->recentCopy = recent;
    job->likedCopy = liked;
    job->playsAtStart = plays;

    auto finish = [this, job]() {
        if (--job->pending > 0) return;
        QSet<QString> heard, seedIds, used;
        for (const Track &t : std::as_const(job->recentCopy)) heard.insert(t.vid);
        for (const Track &t : std::as_const(job->seeds)) seedIds.insert(t.vid);
        QVector<FeedSection> out;

        if (!job->seeds.isEmpty()) {
            QHash<QString, double> score;
            QHash<QString, Track> byId;
            for (const Track &seed : std::as_const(job->seeds)) {
                const QVector<Track> list = job->mixes.value(seed.vid);
                for (int rank = 0; rank < list.size(); ++rank) {
                    const Track &t = list[rank];
                    if (seedIds.contains(t.vid) || heard.contains(t.vid)) continue;
                    score[t.vid] += double(list.size() - rank) / list.size();
                    byId[t.vid] = t;
                }
            }
            QVector<QPair<double, QString>> ranked;
            for (auto it = score.cbegin(); it != score.cend(); ++it) ranked.append({it.value() + QRandomGenerator::global()->generateDouble() * 1e-3, it.key()});
            std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            QVector<Track> mix;
            for (int i = 0; i < ranked.size() && mix.size() < 30; ++i) mix.append(byId.value(ranked[i].second));
            if (mix.size() >= 5) {
                out.append({QStringLiteral("mix"), QStringLiteral("Made for you"), QStringLiteral("Based on what you listen to"), mix});
                for (const Track &t : std::as_const(mix)) used.insert(t.vid);
            }
        }
        for (const Track &seed : std::as_const(job->because)) {
            QVector<Track> tracks;
            for (const Track &t : job->mixes.value(seed.vid))
                if (!used.contains(t.vid) && !heard.contains(t.vid) && t.vid != seed.vid && tracks.size() < 10) tracks.append(t);
            if (tracks.size() >= 4) {
                QString title = seed.title;
                if (title.size() > 34) title = title.left(33).trimmed() + QStringLiteral("…");
                out.append({QStringLiteral("because-") + seed.vid, QStringLiteral("Because you liked ") + title, seed.artist, tracks});
                for (const Track &t : std::as_const(tracks)) used.insert(t.vid);
            }
        }
        QVector<Track> charts;
        for (const Track &t : std::as_const(job->charts))
            if (!used.contains(t.vid) && t.duration >= 60 && t.duration <= 900 && charts.size() < 25) charts.append(t);
        if (!charts.isEmpty()) out.append({QStringLiteral("trending"), QStringLiteral("Trending music"), QStringLiteral("What everyone is playing right now"), charts});

        if (!out.isEmpty()) {
            feed = out;
            feedBuiltAt = QDateTime::currentMSecsSinceEpoch();
            feedPlaysAtBuild = job->playsAtStart;
            scheduleSave();
        }
        feedBuilding = false;
        emit feedChanged();
    };

    // fetch: one Mix per seed (+ the liked songs used for shelves) and the chart, all in parallel
    QSet<QString> fetched;
    QVector<Track> toFetch = seeds;
    for (const Track &t : std::as_const(because)) if (!seeds.contains(t) ) toFetch.append(t);
    job->pending = 1 + toFetch.size();
    for (const Track &seed : std::as_const(toFetch)) {
        const QString url = QStringLiteral("https://www.youtube.com/watch?v=%1&list=RD%1").arg(seed.vid);
        runPlaylistProc(this, url, 26,
                        [job, seed](const Track &t) { if (t.vid != seed.vid && t.duration >= 30 && t.duration <= 1500) job->mixes[seed.vid].append(t); },
                        [finish](const QString &) { finish(); });
    }
    runPlaylistProc(this, kCharts, 30, [job](const Track &t) { job->charts.append(t); }, [finish](const QString &) { finish(); });
}


// ---------------------------------------------------------------- bulk deletes
void Core::unlike(const QSet<QString> &vids) {
    for (int i = liked.size() - 1; i >= 0; --i)
        if (vids.contains(liked[i].vid)) liked.removeAt(i);
    rebuildLikedSet();
    scheduleSave();
    emit listsChanged();
}

void Core::clearLikes() {
    liked.clear();
    rebuildLikedSet();
    scheduleSave();
    emit listsChanged();
}

void Core::removeFromQueueMany(QVector<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (int i : std::as_const(indices)) removeFromQueue(i);
}

void Core::removeFromSavedMany(QVector<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (SavedPlaylist &p : playlists) {
        if (p.id != savedOpenId) continue;
        for (int i : std::as_const(indices)) if (i >= 0 && i < p.tracks.size()) p.tracks.removeAt(i);
        break;
    }
    scheduleSave();
    emit listsChanged();
}

void Core::deletePlaylists(const QSet<QString> &ids) {
    for (int i = playlists.size() - 1; i >= 0; --i)
        if (ids.contains(playlists[i].id)) playlists.removeAt(i);
    if (ids.contains(savedOpenId)) savedOpenId.clear();
    scheduleSave();
    emit listsChanged();
}

void Core::clearAllPlaylists() {
    playlists.clear();
    savedOpenId.clear();
    scheduleSave();
    emit listsChanged();
}

void Core::clearSaved(const QString &id) {
    for (SavedPlaylist &p : playlists) if (p.id == id) p.tracks.clear();
    scheduleSave();
    emit listsChanged();
}
