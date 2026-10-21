#include "lyrics.h"
#include "paths.h"
#include "ui_common.h"
#include "theme.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

using namespace UI;
namespace {
// "Artist - Song (Official Video)" + "ArtistVEVO"  ->  (artist, song), as LRCLIB wants them.
QPair<QString, QString> cleanNames(QString title, QString uploader) {
    static const QRegularExpression brackets(
        QStringLiteral("\\s*[(\\[【][^)\\]】]*(official|video|audio|lyric|visuali[sz]er|explicit|remaster|hd|4k|mv|m/v|clip|live|full song|topic|feat|ft\\.)[^)\\]】]*[)\\]】]"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trail(QStringLiteral("\\s*[|｜]\\s*.*$"));
    static const QRegularExpression topic(QStringLiteral("\\s*-\\s*topic$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression vevo(QStringLiteral("vevo$"), QRegularExpression::CaseInsensitiveOption);
    title.remove(brackets);
    title.remove(trail);
    title = title.trimmed();
    uploader.remove(topic);
    uploader.remove(vevo);
    uploader = uploader.trimmed();
    const int dash = title.indexOf(QStringLiteral(" - "));
    if (dash > 0) { uploader = title.left(dash).trimmed(); title = title.mid(dash + 3).trimmed(); }
    return {uploader, title};
}

QString nonNull(const QJsonValue &v) { return v.isString() ? v.toString() : QString(); }

// Community entries are sometimes junk (a single word); don't show those.
bool usable(const QString &lrc, const QString &plain) {
    int timed = 0;
    for (const QString &l : lrc.split(QLatin1Char('\n'))) {
        const int i = l.indexOf(QLatin1Char(']'));
        if (i >= 0 && !l.mid(i + 1).trimmed().isEmpty()) ++timed;
    }
    return timed >= 3 || plain.size() >= 80;
}

// curl GET; `done` gets the body (empty on any failure).
void httpGet(QObject *parent, const QUrl &url, std::function<void(const QByteArray &)> done) {
    auto *p = new QProcess(parent);
    QObject::connect(p, &QProcess::finished, parent, [p, done] {
        const QByteArray body = p->exitStatus() == QProcess::NormalExit && p->exitCode() == 0 ? p->readAllStandardOutput() : QByteArray();
        p->deleteLater();
        done(body);
    });
    QObject::connect(p, &QProcess::errorOccurred, parent, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { p->deleteLater(); done({}); }
    });
    p->start(QStringLiteral("curl"), {QStringLiteral("-sfL"), QStringLiteral("--max-time"), QStringLiteral("15"),
                                      QStringLiteral("-A"), QStringLiteral("YTMusic personal player (github.com/pixelwars18-coder/ytmusic)"),
                                      url.toString(QUrl::FullyEncoded)});
}
} // namespace

LyricsView::LyricsView(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    hide();
    setMouseTracking(true);
    anim.setInterval(16);
    connect(&anim, &QTimer::timeout, this, [this] {
        const qreal d = targetY - scrollY;
        scrollY = qAbs(d) < 0.5 ? targetY : scrollY + d * 0.18;
        if (scrollY == targetY) anim.stop();
        update();
    });
    connect(core, &Core::positionChanged, this, [this] {
        if (!isVisible() || lines.isEmpty()) return;
        const int idx = currentIndex();
        if (idx >= 0 && QDateTime::currentMSecsSinceEpoch() - lastWheel > 3000) {
            targetY = qMax<qreal>(0, boxes[idx].center().y() - height() / 3.0);
            if (!anim.isActive()) anim.start();
        }
        update();
    });
    connect(core, &Core::trackChanged, this, [this] { if (isVisible()) load(); });
}

QString LyricsView::cachePath(const QString &v) const { return Paths::cacheDir() + QStringLiteral("/lyrics/") + v + QStringLiteral(".json"); }

void LyricsView::toggle() {
    if (isVisible()) { closeView(); return; }
    if (!core->nowPlaying()) { message = QStringLiteral("Play a song to see its lyrics"); lines.clear(); plain.clear(); vid.clear(); }
    else load();
    raise();
    show();
}

void LyricsView::closeView() { hide(); anim.stop(); emit closed(); }

void LyricsView::load() {
    const Track *t = core->nowPlaying();
    if (!t) return;
    vid = t->vid;
    lines.clear(); plain.clear(); boxes.clear(); scrollY = targetY = 0;
    QFile f(cachePath(vid));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const bool none = o.value("none").toBool();
        const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(o.value("at").toDouble());
        if (!none || age < 3600) { apply(vid, o.value("lrc").toString(), o.value("plain").toString(), !none); return; }
    }
    message = QStringLiteral("Looking for lyrics…");
    update();
    fetch(*t);
}

void LyricsView::apply(const QString &v, const QString &lrc, const QString &pl, bool found) {
    if (v != vid) return;
    lines.clear(); plain.clear(); boxes.clear();
    if (!found) message = QStringLiteral("No lyrics found for this song");
    else if (!lrc.trimmed().isEmpty()) { lines = parseLrc(lrc); message.clear(); }
    else { plain = pl; message.clear(); }
    layoutLines();
    update();
}

QVector<LyricsView::Line> LyricsView::parseLrc(const QString &lrc) const {
    static const QRegularExpression tag(QStringLiteral("\\[(\\d+):(\\d+)(?:[.:](\\d+))?\\]"));
    QVector<Line> out;
    for (const QString &raw : lrc.split(QLatin1Char('\n'))) {
        QRegularExpressionMatchIterator it = tag.globalMatch(raw);
        QVector<double> stamps;
        int end = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            const QString frac = m.captured(3);
            stamps << m.captured(1).toInt() * 60.0 + m.captured(2).toInt() + (frac.isEmpty() ? 0.0 : (frac + QStringLiteral("00")).left(3).toInt() / 1000.0);
            end = m.capturedEnd();
        }
        const QString text = raw.mid(end).trimmed();
        if (stamps.isEmpty() || text.isEmpty()) continue;
        for (double s : std::as_const(stamps)) out.append({s, text});
    }
    std::sort(out.begin(), out.end(), [](const Line &a, const Line &b) { return a.sec < b.sec; });
    return out;
}

void LyricsView::fetch(const Track &t) {
    if (inFlight.value(t.vid)) return;
    inFlight[t.vid] = true;
    const auto names = cleanNames(t.title, t.artist);
    const QString vidCopy = t.vid;
    const double dur = t.duration;
    auto finish = [this, vidCopy](const QString &lrc, const QString &pl, bool found, bool networkOk) {
        inFlight.remove(vidCopy);
        if (!networkOk) { if (vid == vidCopy) { message = QStringLiteral("Couldn't reach the lyrics service"); update(); } return; }
        QDir().mkpath(Paths::cacheDir() + QStringLiteral("/lyrics"));
        QFile f(cachePath(vidCopy));
        if (f.open(QIODevice::WriteOnly)) {
            QJsonObject o{{"at", double(QDateTime::currentSecsSinceEpoch())}};
            if (found) { o["lrc"] = lrc; o["plain"] = pl; } else o["none"] = true;
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        }
        apply(vidCopy, lrc, pl, found);
    };
    if (names.second.isEmpty()) { finish({}, {}, false, true); return; }

    // 1) exact lookup, 2) search by track + artist, 3) free-text search
    auto pick = [dur](const QByteArray &body, QString *lrc, QString *pl) {
        const QJsonArray arr = QJsonDocument::fromJson(body).array();
        double bestScore = 1e9;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString s = nonNull(o.value("syncedLyrics")), p = nonNull(o.value("plainLyrics"));
            if (!usable(s, p)) continue;
            const double d = o.value("duration").toDouble();
            const double gap = (dur > 0 && d > 0) ? qAbs(d - dur) : 15;
            if (gap > 12) continue;
            const double score = gap * 2 + (s.isEmpty() ? 5 : 0);
            if (score < bestScore) { bestScore = score; *lrc = s; *pl = p; }
        }
        return bestScore < 1e9;
    };
    auto step3 = [this, names, pick, finish]() {
        QUrl u(QStringLiteral("https://lrclib.net/api/search"));
        QUrlQuery q; q.addQueryItem(QStringLiteral("q"), names.first + QLatin1Char(' ') + names.second); u.setQuery(q);
        httpGet(this, u, [pick, finish](const QByteArray &b) {
            QString l, p; const bool ok = !b.isEmpty() && pick(b, &l, &p);
            finish(l, p, ok, !b.isEmpty());
        });
    };
    auto step2 = [this, names, pick, finish, step3]() {
        QUrl u(QStringLiteral("https://lrclib.net/api/search"));
        QUrlQuery q; q.addQueryItem(QStringLiteral("track_name"), names.second); q.addQueryItem(QStringLiteral("artist_name"), names.first); u.setQuery(q);
        httpGet(this, u, [pick, finish, step3](const QByteArray &b) {
            QString l, p;
            if (!b.isEmpty() && pick(b, &l, &p)) finish(l, p, true, true); else step3();
        });
    };
    QUrl u(QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("artist_name"), names.first);
    q.addQueryItem(QStringLiteral("track_name"), names.second);
    if (dur > 0) q.addQueryItem(QStringLiteral("duration"), QString::number(qRound(dur)));
    u.setQuery(q);
    httpGet(this, u, [finish, step2](const QByteArray &b) {
        const QJsonObject o = QJsonDocument::fromJson(b).object();
        const QString s = nonNull(o.value("syncedLyrics")), p = nonNull(o.value("plainLyrics"));
        if (usable(s, p)) finish(s, p, true, true); else step2();
    });
}

int LyricsView::currentIndex() const {
    int idx = -1;
    for (int i = 0; i < lines.size(); ++i) { if (lines[i].sec <= core->position + 0.15) idx = i; else break; }
    return idx;
}

void LyricsView::layoutLines() {
    boxes.clear();
    const qreal w = width() - 80;
    qreal y = 0;
    QFont f = fnt(22, QFont::Bold);
    const QFontMetrics fm(f);
    if (!plain.isEmpty()) {
        const QRect r = QFontMetrics(fnt(16)).boundingRect(QRect(0, 0, int(w), 100000), Qt::TextWordWrap, plain);
        contentH = r.height() + 120;
        return;
    }
    for (const Line &l : std::as_const(lines)) {
        const QRect r = fm.boundingRect(QRect(0, 0, int(w), 10000), Qt::TextWordWrap, l.text);
        boxes.append(QRectF(40, y, w, r.height() + 6));
        y += r.height() + 26;
    }
    contentH = y + 200;
}

void LyricsView::resizeEvent(QResizeEvent *) { layoutLines(); }

void LyricsView::wheelEvent(QWheelEvent *e) {
    lastWheel = QDateTime::currentMSecsSinceEpoch();
    targetY = scrollY = qBound<qreal>(0, scrollY - e->angleDelta().y() * 0.6, qMax<qreal>(0, contentH - height() + 100));
    anim.stop();
    update();
}

void LyricsView::mousePressEvent(QMouseEvent *e) {
    const QPointF pt = e->position() + QPointF(0, scrollY - 56);
    for (int i = 0; i < boxes.size(); ++i)
        if (boxes[i].adjusted(0, -10, 0, 10).contains(pt)) { core->seekTo(lines[i].sec); lastWheel = 0; return; }
    if (e->position().y() < 56) closeView();
}

void LyricsView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    p.setPen(Qt::NoPen);
    p.setBrush(c.surface);
    p.drawRoundedRect(rect(), 18, 18);

    const Track *t = core->nowPlaying();
    p.setPen(c.onSurface);
    p.setFont(fnt(14, QFont::Medium));
    p.drawText(QRectF(40, 10, width() - 120, 22), Qt::AlignVCenter, t ? QFontMetrics(fnt(14, QFont::Medium)).elidedText(t->title, Qt::ElideRight, width() - 120) : QString());
    p.setPen(c.onSurfaceVariant);
    p.setFont(fnt(12));
    p.drawText(QRectF(40, 30, width() - 120, 18), Qt::AlignVCenter, t ? t->artist : QString());
    p.drawText(QRectF(width() - 70, 10, 50, 22), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("close"));

    p.save();
    p.setClipRect(QRectF(0, 56, width(), height() - 56));
    p.translate(0, 56 - scrollY);
    if (!message.isEmpty()) {
        p.resetTransform();
        p.setPen(c.onSurfaceVariant);
        p.setFont(fnt(15));
        p.drawText(QRectF(0, 56, width(), height() - 56), Qt::AlignCenter, message);
    } else if (!plain.isEmpty()) {
        p.setPen(c.onSurface);
        p.setFont(fnt(16));
        p.drawText(QRectF(40, 20, width() - 80, contentH), Qt::TextWordWrap, plain);
    } else {
        const int cur = currentIndex();
        QFont f = fnt(22, QFont::Bold);
        p.setFont(f);
        for (int i = 0; i < lines.size(); ++i) {
            const QRectF b = boxes[i];
            if (b.bottom() < scrollY - 20 || b.top() > scrollY + height()) continue;
            QColor col = i == cur ? c.primary : c.onSurfaceVariant;
            if (i != cur) col.setAlphaF(0.6);
            p.setPen(col);
            p.drawText(b, Qt::TextWordWrap, lines[i].text);
        }
    }
    p.restore();
}
