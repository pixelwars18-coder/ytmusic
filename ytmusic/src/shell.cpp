#include "shell.h"
#include "ui_common.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

using namespace UI;

// ------------------------------------------------------------------- RailBar
RailBar::RailBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setMouseTracking(true);
    setFixedWidth(48);
    hv = new Hover(this);
    clock.start();
    kickTimer.setInterval(16);
    connect(&kickTimer, &QTimer::timeout, this, [this] {
        const qint64 now = clock.elapsed();
        for (auto it = kickAt.begin(); it != kickAt.end();) { if (now - it.value() > 450) it = kickAt.erase(it); else ++it; }
        if (kickAt.isEmpty()) kickTimer.stop();
        update();
    });
}

// VoiceStudio nudges a nav icon when it is hovered or clicked: ~0.4 s ease-out keyframes (lift, nudge, pulse, turn, tilt).
void RailBar::kick(int id) {
    kickAt[id] = clock.elapsed();
    if (!kickTimer.isActive()) kickTimer.start();
}

QVector<RailBar::Item> RailBar::items() const {
    QVector<Item> v;
    qreal y = 58;
    auto add = [&](int id, const QString &icon, const QString &tip, int badge = 0) {
        v.append({QRectF(8, y, 32, 32), id, icon, tip, badge});
        y += 38;
    };
    add(4, QStringLiteral("home"), QStringLiteral("For you"));
    add(0, QStringLiteral("search"), QStringLiteral("Search results"));
    add(1, QStringLiteral("list"), QStringLiteral("Queue"), int(core->queue.size()));
    add(2, QStringLiteral("heart"), QStringLiteral("Liked songs"));
    add(3, QStringLiteral("bookmark"), QStringLiteral("Playlists"));
    qreal by = height() - 8 - 32;
    for (const auto &b : {std::make_tuple(9, QStringLiteral("x"), QStringLiteral("Hide to tray")),
                          std::make_tuple(8, QStringLiteral("gear"), QStringLiteral("Settings")),
                          std::make_tuple(7, QStringLiteral("lyrics"), QStringLiteral("Lyrics (Y)"))}) {
        v.append({QRectF(8, by, 32, 32), std::get<0>(b), std::get<1>(b), std::get<2>(b), 0});
        by -= 38;
    }
    return v;
}

void RailBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    // logo: a soft blue tile with the music glyph
    const QRectF logo(8, 12, 32, 32);
    QLinearGradient g(logo.topLeft(), logo.bottomRight());
    g.setColorAt(0, withAlpha(c.primary, 70));
    g.setColorAt(1, withAlpha(c.tertiary, 50));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(logo, 9, 9);
    Icons::paint(p, QRectF(logo.center().x() - 9, logo.center().y() - 9, 18, 18), QStringLiteral("music"), c.onSurface);

    const auto its = items();
    for (int i = 0; i < its.size(); ++i) {
        const Item &it = its[i];
        const bool tabItem = it.id <= 4 && it.id != 9;
        const bool active = tabItem && ((it.id == 4 && tab == Tab::Home) || (it.id == 0 && tab == Tab::Results) || (it.id == 1 && tab == Tab::Queue) ||
                                        (it.id == 2 && tab == Tab::Liked) || (it.id == 3 && tab == Tab::Saved));
        const qreal hl = hv->get(it.id);
        if (active || hl > 0.001) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(Qt::white, active ? int(22 + 4 * hl) : int(12 * hl)));
            p.drawRoundedRect(it.r, 8, 8);
        }
        if (active) {
            p.setBrush(c.primary);
            p.drawRoundedRect(QRectF(0, it.r.center().y() - 8, 3, 16), 1.5, 1.5);
        }
        // icon keyframe (0 -> 1 at 45% -> 0)
        qreal k = 0, turn = 0;
        if (kickAt.contains(it.id)) {
            const qreal t = qBound<qreal>(0, (clock.elapsed() - kickAt[it.id]) / 400.0, 1);
            k = t < 0.45 ? studioEase(t / 0.45) : studioEase(1 - (t - 0.45) / 0.55);
            turn = studioEase(t);
        }
        p.save();
        p.translate(it.r.center());
        switch (it.id) {
        case 4: case 3: p.translate(0, -3 * k); break;                 // lift
        case 1: p.translate(3 * k, 0); break;                          // nudge
        case 8: p.rotate(360 * turn); break;                           // turn
        case 7: p.rotate(-14 * k); break;                              // tilt
        default: p.scale(1 + 0.16 * k, 1 + 0.16 * k); break;           // pulse
        }
        QColor ic = c.onSurfaceVariant;
        if (active) ic = c.onSurface;
        else if (hl > 0) ic = QColor::fromRgbF(ic.redF() + (c.onSurface.redF() - ic.redF()) * hl, ic.greenF() + (c.onSurface.greenF() - ic.greenF()) * hl, ic.blueF() + (c.onSurface.blueF() - ic.blueF()) * hl);
        Icons::paint(p, QRectF(-8, -8, 16, 16), it.icon, ic);
        p.restore();
        if (it.badge > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.primary);
            p.drawEllipse(QPointF(it.r.right() - 4, it.r.top() + 6), 3.5, 3.5);
        }
    }
}

void RailBar::mouseMoveEvent(QMouseEvent *e) {
    const auto its = items();
    int h = -1;
    for (int i = 0; i < its.size(); ++i) if (its[i].r.contains(e->position())) h = i;
    if (h != hover) {
        if (hover >= 0 && hover < its.size()) hv->set(its[hover].id, false);
        hover = h;
        if (h >= 0) { hv->set(its[h].id, true); kick(its[h].id); }
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(h >= 0 ? its[h].tip : QString());
        update();
    }
}
void RailBar::leaveEvent(QEvent *) {
    for (const Item &it : items()) hv->set(it.id, false);
    hover = -1;
    update();
}

void RailBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    for (const Item &it : items()) {
        if (!it.r.contains(e->position())) continue;
        kick(it.id);
        switch (it.id) {
        case 4: emit tabChanged(Tab::Home); break;
        case 0: emit tabChanged(Tab::Results); break;
        case 1: emit tabChanged(Tab::Queue); break;
        case 2: emit tabChanged(Tab::Liked); break;
        case 3: emit tabChanged(Tab::Saved); break;
        case 9: emit hideRequested(); break;
        default: emit actionTriggered(it.id); break;
        }
        return;
    }
    emit dragRequested();
}

// ----------------------------------------------------------------- SidePanel
SidePanel::SidePanel(Core *c, QWidget *parent) : QWidget(parent), core(c) { setMouseTracking(true); hv = new Hover(this); }

int SidePanel::rowCount() const { return showRecent ? int(qMin<qsizetype>(core->recent.size(), 60)) : int(core->playlists.size()) + 1; }
QRectF SidePanel::rowRect(int i) const { return QRectF(8, listArea().top() + 4 + i * 46 - scroll, width() - 16, 42); }
QRectF SidePanel::tabRect(int i) const {
    const qreal w = (width() - 24 - 4) / 2.0;
    return QRectF(12 + i * (w + 4), 50, w, 28);
}

void SidePanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Palette &c = P();

    p.setPen(c.onSurface);
    p.setFont(fnt(14, QFont::DemiBold));
    p.drawText(QRectF(16, 8, width() - 70, 32), Qt::AlignVCenter, QStringLiteral("Library"));
    const QRectF cb(width() - 42, 9, 30, 30);
    if (hv->get(-100) > 0.001) { p.setPen(Qt::NoPen); p.setBrush(withAlpha(Qt::white, int(18 * hv->get(-100)))); p.drawRoundedRect(cb, 8, 8); }
    Icons::paint(p, QRectF(cb.center().x() - 8, cb.center().y() - 8, 16, 16), QStringLiteral("panel"), c.onSurfaceVariant);

    // segmented control
    p.setPen(QPen(withAlpha(Qt::white, 20), 1));
    p.setBrush(withAlpha(Qt::white, 8));
    p.drawRoundedRect(QRectF(12, 50, width() - 24, 28).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    for (int i = 0; i < 2; ++i) {
        const bool on = (i == 1) == showRecent;
        const QRectF r = tabRect(i).adjusted(2, 2, -2, -2);
        if (on) { p.setPen(Qt::NoPen); p.setBrush(withAlpha(Qt::white, 24)); p.drawRoundedRect(r, 6, 6); }
        p.setFont(fnt(12, QFont::Medium));
        p.setPen(on ? c.onSurface : c.onSurfaceVariant);
        p.drawText(r, Qt::AlignCenter, i == 0 ? QStringLiteral("Playlists") : QStringLiteral("Recent"));
    }

    p.save();
    p.setClipRect(listArea());
    const int n = rowCount();
    for (int i = 0; i < n; ++i) {
        const QRectF r = rowRect(i);
        if (r.bottom() < listArea().top() || r.top() > height()) continue;
        if (hv->get(i) > 0.001) { p.setPen(Qt::NoPen); p.setBrush(withAlpha(Qt::white, int(12 * hv->get(i)))); p.drawRoundedRect(r, 8, 8); }
        QString title, sub, icon;
        QColor tile = c.primary;
        QString vid;
        if (showRecent) {
            const Track &t = core->recent[i];
            title = t.title; sub = t.artist; vid = t.vid;
        } else if (i == 0) {
            title = QStringLiteral("Liked songs");
            sub = QStringLiteral("%1 songs").arg(core->liked.size());
            icon = QStringLiteral("heart");
            tile = QColor("#e5658a");
        } else {
            const SavedPlaylist &sp = core->playlists[i - 1];
            title = sp.name;
            sub = sp.tracks.size() == 1 ? QStringLiteral("1 song") : QStringLiteral("%1 songs").arg(sp.tracks.size());
            icon = QStringLiteral("list");
            tile = (i % 3 == 1) ? c.tertiary : (i % 3 == 2) ? QColor("#b8a1ee") : QColor("#79b9ee");
        }
        const QRectF th(r.left() + 6, r.top() + 5, 32, 32);
        if (showRecent) drawThumb(p, th, vid, 7);
        else {
            p.setPen(QPen(withAlpha(tile, 70), 1));
            p.setBrush(withAlpha(tile, 34));
            p.drawRoundedRect(th.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
            Icons::paint(p, QRectF(th.center().x() - 8, th.center().y() - 8, 16, 16), icon, tile);
        }
        const qreal tx = th.right() + 10, tw = r.right() - tx - 6;
        p.setFont(fnt(13, QFont::Medium));
        p.setPen(c.onSurface);
        p.drawText(QRectF(tx, r.top() + 3, tw, 20), Qt::AlignVCenter, QFontMetrics(fnt(13, QFont::Medium)).elidedText(title, Qt::ElideRight, int(tw)));
        p.setFont(fnt(11));
        p.setPen(c.onSurfaceVariant);
        p.drawText(QRectF(tx, r.top() + 21, tw, 16), Qt::AlignVCenter, QFontMetrics(fnt(11)).elidedText(sub, Qt::ElideRight, int(tw)));
    }
    if (showRecent && n == 0) {
        p.setFont(fnt(12));
        p.setPen(c.onSurfaceVariant);
        p.drawText(listArea().adjusted(16, 16, -16, 0), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, QStringLiteral("Songs you play show up here"));
    }
    p.restore();
}

void SidePanel::wheelEvent(QWheelEvent *e) {
    const qreal maxScroll = qMax<qreal>(0, rowCount() * 46 + 8 - listArea().height());
    scroll = qBound<qreal>(0, scroll - e->angleDelta().y() * 0.5, maxScroll);
    update();
}

void SidePanel::mouseMoveEvent(QMouseEvent *e) {
    const QPointF pt = e->position();
    int r = -1, t = -1;
    if (listArea().contains(pt)) for (int i = 0; i < rowCount(); ++i) if (rowRect(i).contains(pt)) r = i;
    for (int i = 0; i < 2; ++i) if (tabRect(i).contains(pt)) t = i;
    const bool col = QRectF(width() - 42, 9, 30, 30).contains(pt);
    if (r != hoverRow || t != hoverTab || col != hoverCollapse) {
        if (hoverRow >= 0) hv->set(hoverRow, false);
        if (r >= 0) hv->set(r, true);
        hv->set(-100, col);
        hoverRow = r; hoverTab = t; hoverCollapse = col;
        setCursor((r >= 0 || t >= 0 || col) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}
void SidePanel::leaveEvent(QEvent *) {
    if (hoverRow >= 0) hv->set(hoverRow, false);
    hv->set(-100, false);
    hoverRow = hoverTab = -1;
    hoverCollapse = false;
    update();
}

void SidePanel::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF pt = e->position();
    if (QRectF(width() - 42, 9, 30, 30).contains(pt)) { emit collapseRequested(); return; }
    for (int i = 0; i < 2; ++i) if (tabRect(i).contains(pt)) { showRecent = i == 1; scroll = 0; update(); return; }
    if (!listArea().contains(pt)) return;
    for (int i = 0; i < rowCount(); ++i) {
        if (!rowRect(i).contains(pt)) continue;
        if (showRecent) core->playTracks(core->recent.mid(i), 0, false);
        else if (i == 0) emit likedRequested();
        else emit playlistRequested(core->playlists[i - 1].id);
        return;
    }
}

// -------------------------------------------------------------------- TopBar
TopBar::TopBar(QWidget *parent) : QWidget(parent) {
    hv = new Hover(this);
    setMouseTracking(true);
    setFixedHeight(44);
}

void TopBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    if (hv->get(0) > 0.001) { p.setPen(Qt::NoPen); p.setBrush(withAlpha(Qt::white, int(18 * hv->get(0)))); p.drawRoundedRect(toggleRect(), 8, 8); }
    Icons::paint(p, QRectF(toggleRect().center().x() - 8, toggleRect().center().y() - 8, 16, 16), QStringLiteral("panel"), c.onSurfaceVariant);
    p.setFont(fnt(13, QFont::Medium));
    p.setPen(c.onSurface);
    p.drawText(QRectF(54, 0, width() - 54, height()), Qt::AlignVCenter, titleFn ? titleFn() : QString());
}

void TopBar::mouseMoveEvent(QMouseEvent *e) {
    const bool h = toggleRect().contains(e->position());
    if (h != hoverToggle) { hoverToggle = h; hv->set(0, h); setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void TopBar::leaveEvent(QEvent *) { hoverToggle = false; hv->set(0, false); update(); }
void TopBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    if (toggleRect().contains(e->position())) emit toggleRequested();
    else emit dragRequested();
}
