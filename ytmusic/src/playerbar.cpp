#include "playerbar.h"
#include "thumbs.h"
#include "ui_common.h"
#include <QMouseEvent>
#include <QPainter>

using namespace UI;

PlayerBar::PlayerBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setMouseTracking(true);
    setFixedHeight(90);
    setFocusPolicy(Qt::NoFocus);
    // artwork is decoded in the background: repaint when it lands
    connect(&Thumbs::I(), &Thumbs::ready, this, qOverload<>(&QWidget::update));
}

PlayerBar::Layout PlayerBar::layout() const {
    Layout l;
    const qreal W = width(), H = height(), pad = 18;
    const qreal cy = (6 + H) / 2.0;   // controls sit just below the seek line
    // Like music.youtube.com: transport + time on the left, the song after that,
    // queue / lyrics / shuffle / repeat / volume on the right.
    qreal x = pad;
    auto place = [&](qreal w) { QRectF r(x, cy - w / 2, w, w); x += w + 6; return r; };
    l.prev = place(40);
    l.play = place(48);
    l.next = place(40);
    l.timeL = QRectF(x + 6, cy - 9, 92, 18);
    l.timeR = QRectF();
    x = l.timeL.right() + 18;

    const qreal volW = W < 900 ? 84 : 128;
    l.volArea = QRectF(W - pad - volW - 34, 0, volW + 34, H);
    l.mute = QRectF(l.volArea.left(), cy - 17, 34, 34);
    l.volPct = QRectF();
    l.volBar = QRectF(l.mute.right() + 6, cy - 9, volW - 6, 18);
    l.repeat = QRectF(l.volArea.left() - 4 - 34, cy - 17, 34, 34);
    l.shuffle = QRectF(l.repeat.left() - 2 - 34, cy - 17, 34, 34);
    l.queue = QRectF(l.shuffle.left() - 8 - 34, cy - 17, 34, 34);
    l.lyrics = QRectF(l.queue.left() - 2 - 34, cy - 17, 34, 34);

    const qreal infoEnd = l.lyrics.left() - 18;
    l.thumb = QRectF(x, cy - 28, 56, 56);
    l.heart = QRectF(qMax(l.thumb.right() + 90, infoEnd - 34), cy - 17, 34, 34);
    l.text = QRectF(l.thumb.right() + 12, cy - 28, qMax<qreal>(40, l.heart.left() - l.thumb.right() - 18), 56);

    // the seek line runs along the top edge of the bar
    l.seek = QRectF(0, 0, W, 12);
    return l;
}

PlayerBar::Id PlayerBar::hitTest(const QPointF &pt) const {
    const Layout l = layout();
    if (core->nowPlaying() && l.heart.contains(pt)) return Heart;
    if (l.shuffle.contains(pt)) return Shuffle;
    if (l.prev.contains(pt)) return Prev;
    if (l.play.contains(pt)) return Play;
    if (l.next.contains(pt)) return Next;
    if (l.repeat.contains(pt)) return Repeat;
    if (l.lyrics.contains(pt)) return Lyrics;
    if (l.queue.contains(pt)) return Queue;
    if (l.mute.contains(pt)) return Mute;
    if (l.seek.adjusted(0, -4, 0, 4).contains(pt)) return SeekBar;
    if (l.volBar.adjusted(0, -4, 0, 4).contains(pt)) return VolBar;
    if (core->nowPlaying() && (l.thumb.contains(pt) || l.text.contains(pt))) return Info;
    return None;
}

void PlayerBar::dragTo(Id which, qreal x) {
    const Layout l = layout();
    if (which == SeekBar) {
        if (core->duration <= 0) return;
        core->seekTo(qBound(0.0, (x - l.seek.left()) / l.seek.width(), 1.0) * core->duration);
    } else if (which == VolBar) {
        core->setVolume(qBound(0.0, (x - l.volBar.left()) / l.volBar.width(), 1.0) * 100.0);
    }
}

void PlayerBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const Id h = hitTest(e->position());
    switch (h) {
    case Heart: if (auto *t = core->nowPlaying()) core->toggleLike(*t); break;
    case Shuffle: core->toggleShuffle(); break;
    case Prev: core->prev(); break;
    case Play: core->togglePlay(); break;
    case Next: core->next(false); break;
    case Repeat: core->cycleRepeat(); break;
    case Mute: core->toggleMute(); break;
    case Info:
    case Lyrics: emit expandRequested(); break;
    case Queue: emit queueRequested(); break;
    case SeekBar:
    case VolBar:
        dragging = h;
        dragTo(h, e->position().x());
        break;
    case None: break;
    }
}

void PlayerBar::mouseMoveEvent(QMouseEvent *e) {
    if (dragging != None) {
        dragTo(dragging, e->position().x());
        return;
    }
    const Id h = hitTest(e->position());
    if (h != hover) {
        hover = h;
        setCursor(h == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void PlayerBar::mouseReleaseEvent(QMouseEvent *) { dragging = None; update(); }
void PlayerBar::leaveEvent(QEvent *) { hover = None; update(); }

void PlayerBar::wheelEvent(QWheelEvent *e) {
    const Layout l = layout();
    if (l.volArea.contains(e->position())) core->setVolume(core->volume + (e->angleDelta().y() > 0 ? 5 : -5));
}

void PlayerBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Palette &c = P();
    const Layout l = layout();

    {   // docked flat bar (YT Music: #212121); rounded only where it meets the window corners
        QColor bgc = c.surfaceVariant;
        if (!Theme::I().official()) { bgc = Qt::black; bgc.setAlphaF(0.55); }
        p.setPen(Qt::NoPen);
        p.setBrush(bgc);
        p.drawRoundedRect(QRectF(0, -40, width(), height() + 40), 16, 16);
    }

    // now playing
    const Track *t = core->nowPlaying();
    drawThumb(p, l.thumb, t ? t->vid : QString(), 4);
    if (hover == Info) {   // the song block opens the now-playing screen
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(8, 8, 32, 150));
        p.drawRoundedRect(l.thumb, 4, 4);
        Icons::paint(p, QRectF(l.thumb.center().x() - 11, l.thumb.center().y() - 11, 22, 22), QStringLiteral("up"), c.onSurface);
    }
    p.setPen(c.onSurface);
    p.setFont(fnt(14, QFont::DemiBold));
    const QString title = t ? t->title : QStringLiteral("Nothing playing");
    p.drawText(QRectF(l.text.left(), l.text.top() + 10, l.text.width(), 22), Qt::AlignVCenter,
               QFontMetrics(fnt(14, QFont::DemiBold)).elidedText(title, Qt::ElideRight, int(l.text.width())));
    p.setPen(c.secondary);
    p.setFont(fnt(12));
    p.drawText(QRectF(l.text.left(), l.text.top() + 32, l.text.width(), 20), Qt::AlignVCenter,
               QFontMetrics(fnt(12)).elidedText(t ? t->artist : QStringLiteral("—"), Qt::ElideRight, int(l.text.width())));
    if (t) {
        const bool liked = core->isLiked(t->vid);
        drawIconButton(p, l.heart, QStringLiteral("heart"), hover == Heart, false, liked, liked);
    }

    // transport
    drawIconButton(p, l.shuffle, QStringLiteral("shuffle"), hover == Shuffle, false, core->shuffle);
    drawIconButton(p, l.prev, QStringLiteral("prev"), hover == Prev);
    const QString playIcon = (core->paused || core->idle) ? QStringLiteral("play") : QStringLiteral("pause");
    if (Theme::I().official())   // a bare white triangle, no disc
        Icons::paint(p, QRectF(l.play.center().x() - 18, l.play.center().y() - 18, 36, 36), playIcon, c.onSurface, true);
    else
        drawIconButton(p, l.play, playIcon, false, true);
    drawIconButton(p, l.next, QStringLiteral("next"), hover == Next);
    drawIconButton(p, l.repeat, QStringLiteral("repeat"), hover == Repeat, false, core->repeat > 0, false,
                   core->repeat == Core::RepeatOne ? QStringLiteral("1") : QString());

    // time + seek
    p.setFont(fnt(12));
    p.setPen(c.onSurfaceVariant);
    const QString tl = core->buffering ? QStringLiteral("Loading…") : (fmtTime(core->position) + QStringLiteral(" / ") + fmtTime(core->duration));
    p.drawText(l.timeL, Qt::AlignVCenter | Qt::AlignLeft, tl);
    auto slider = [&](const QRectF &r, double frac, const QColor &fill, bool active, qreal th) {
        const QRectF track(r.left(), r.center().y() - th / 2, r.width(), th);
        p.setPen(Qt::NoPen);
        QColor bgc = c.onSurface;
        bgc.setAlphaF(0.16);
        p.setBrush(bgc);
        p.drawRoundedRect(track, th / 2, th / 2);
        frac = qBound(0.0, frac, 1.0);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * frac, th), th / 2, th / 2);
        if (active) p.drawEllipse(QPointF(track.left() + track.width() * frac, track.center().y()), 6, 6);
    };
    const bool seekActive = hover == SeekBar || dragging == SeekBar;
    slider(QRectF(l.seek.left(), seekActive ? 3 : 2, l.seek.width(), 0), core->duration > 0 ? core->position / core->duration : 0,
           c.accent, seekActive, seekActive ? 5 : 3);

    // now-playing screen, queue
    drawIconButton(p, l.lyrics, QStringLiteral("lyrics"), hover == Lyrics);
    drawIconButton(p, l.queue, QStringLiteral("list"), hover == Queue);

    // volume
    drawIconButton(p, l.mute, (core->muted || core->volume == 0) ? QStringLiteral("mute") : QStringLiteral("volume"), hover == Mute);
    slider(l.volBar, core->muted ? 0 : core->volume / 100.0, c.tertiary, hover == VolBar || dragging == VolBar, 4);
    if (!l.volPct.isNull()) {
        p.setPen(c.onSurfaceVariant);
        p.setFont(fnt(11));
        p.drawText(l.volPct, Qt::AlignVCenter | Qt::AlignRight, QString::number(qRound(core->volume)) + QLatin1Char('%'));
    }
}
