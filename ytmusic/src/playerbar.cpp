#include "playerbar.h"
#include "ui_common.h"
#include <QMouseEvent>
#include <QPainter>

using namespace UI;

PlayerBar::PlayerBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setMouseTracking(true);
    setFixedHeight(92);
    setFocusPolicy(Qt::NoFocus);
}

PlayerBar::Layout PlayerBar::layout() const {
    Layout l;
    const qreal W = width(), pad = 14;
    l.thumb = QRectF(pad, pad, 64, 64);
    l.heart = QRectF(pad + 270 - 34, (height() - 34) / 2.0, 34, 34);
    l.text = QRectF(l.thumb.right() + 12, pad, l.heart.left() - l.thumb.right() - 20, 64);

    const qreal volW = 200;
    l.volArea = QRectF(W - pad - volW, 0, volW, height());
    l.mute = QRectF(l.volArea.left(), (height() - 34) / 2.0, 34, 34);
    l.volPct = QRectF(l.volArea.right() - 30, 0, 30, height());
    l.volBar = QRectF(l.mute.right() + 8, (height() - 18) / 2.0, l.volPct.left() - l.mute.right() - 16, 18);

    const qreal cx0 = pad + 270 + 20, cx1 = l.volArea.left() - 20;
    const qreal cx = (cx0 + cx1) / 2;
    const qreal total = 34 + 40 + 48 + 40 + 34 + 4 * 8;
    qreal x = cx - total / 2;
    const qreal ty = 8;
    auto place = [&](qreal w) { QRectF r(x, ty + (48 - w) / 2, w, w); x += w + 8; return r; };
    l.shuffle = place(34);
    l.prev = place(40);
    l.play = place(48);
    l.next = place(40);
    l.repeat = place(34);

    const qreal sy = 62;
    l.timeL = QRectF(cx0, sy, 36, 18);
    l.timeR = QRectF(cx1 - 46, sy, 46, 18);
    l.seek = QRectF(l.timeL.right() + 10, sy, l.timeR.left() - l.timeL.right() - 20, 18);
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
    if (l.mute.contains(pt)) return Mute;
    if (l.seek.adjusted(0, -4, 0, 4).contains(pt)) return SeekBar;
    if (l.volBar.adjusted(0, -4, 0, 4).contains(pt)) return VolBar;
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

    p.setBrush(c.surfaceVariant);
    p.setPen(QPen(c.outline, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 24, 24);

    // now playing
    const Track *t = core->nowPlaying();
    drawThumb(p, l.thumb, t ? t->vid : QString(), 14);
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
    drawIconButton(p, l.play, (core->paused || core->idle) ? QStringLiteral("play") : QStringLiteral("pause"), false, true);
    drawIconButton(p, l.next, QStringLiteral("next"), hover == Next);
    drawIconButton(p, l.repeat, QStringLiteral("repeat"), hover == Repeat, false, core->repeat > 0, false,
                   core->repeat == Core::RepeatOne ? QStringLiteral("1") : QString());

    // seek
    p.setFont(fnt(11));
    p.setPen(c.onSurfaceVariant);
    p.drawText(l.timeL, Qt::AlignVCenter | Qt::AlignRight, fmtTime(core->position));
    p.setPen(core->buffering ? c.tertiary : c.onSurfaceVariant);
    p.drawText(l.timeR, Qt::AlignVCenter | Qt::AlignLeft, core->buffering ? QStringLiteral("Loading…") : fmtTime(core->duration));
    auto slider = [&](const QRectF &r, double frac, const QColor &fill, bool active) {
        const QRectF track(r.left(), r.center().y() - 3, r.width(), 6);
        p.setPen(Qt::NoPen);
        p.setBrush(c.outline);
        p.drawRoundedRect(track, 3, 3);
        frac = qBound(0.0, frac, 1.0);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * frac, 6), 3, 3);
        if (active) p.drawEllipse(QPointF(track.left() + track.width() * frac, track.center().y()), 7, 7);
    };
    slider(l.seek, core->duration > 0 ? core->position / core->duration : 0, c.primary, hover == SeekBar || dragging == SeekBar);

    // volume
    drawIconButton(p, l.mute, (core->muted || core->volume == 0) ? QStringLiteral("mute") : QStringLiteral("volume"), hover == Mute);
    slider(l.volBar, core->muted ? 0 : core->volume / 100.0, c.tertiary, hover == VolBar || dragging == VolBar);
    p.setPen(c.onSurfaceVariant);
    p.setFont(fnt(11));
    p.drawText(l.volPct, Qt::AlignVCenter | Qt::AlignRight, QString::number(qRound(core->volume)) + QLatin1Char('%'));
}
