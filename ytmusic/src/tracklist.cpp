#include "tracklist.h"
#include "thumbs.h"
#include "ui_common.h"
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

using namespace UI;

TrackList::TrackList(Core *c, QWidget *parent) : QAbstractScrollArea(parent), core(c) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    verticalScrollBar()->setSingleStep(22);
    clock.start();
    spin.setInterval(50);
    connect(&spin, &QTimer::timeout, viewport(), qOverload<>(&QWidget::update));
    connect(&Thumbs::I(), &Thumbs::ready, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    auto restyle = [this] {
        verticalScrollBar()->setStyleSheet(QStringLiteral(
            "QScrollBar:vertical{background:transparent;width:6px;margin:0}"
            "QScrollBar::handle:vertical{background:%1;border-radius:3px;min-height:30px}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent}")
                                               .arg(P().outline.name()));
        viewport()->update();
    };
    connect(&Theme::I(), &Theme::changed, this, restyle);
    restyle();
}

void TrackList::setTab(Tab t) {
    tab = t;
    if (selecting) stopSelect();
    hover = {};
    verticalScrollBar()->setValue(0);
    refresh();
}

bool TrackList::loadingSomething() const {
    return (tab == Tab::Results && (core->detailOpen ? core->detail.loading : core->searching));
}

bool TrackList::canSelect() const { return tab == Tab::Queue || tab == Tab::Liked || tab == Tab::Saved; }

void TrackList::startSelect() {
    if (!canSelect() || count() == 0) return;
    selecting = true;
    selected.clear();
    emit selectionChanged();
    viewport()->update();
}

void TrackList::stopSelect() {
    selecting = false;
    selected.clear();
    emit selectionChanged();
    viewport()->update();
}

void TrackList::selectAll() {
    selected.clear();
    for (int i = 0; i < count(); ++i) selected.insert(i);
    emit selectionChanged();
    viewport()->update();
}

void TrackList::refresh() {
    if (selecting) {                       // keep only selections that still exist; leave select mode when the list is empty
        QSet<int> keep;
        for (int i : std::as_const(selected)) if (i < count()) keep.insert(i);
        const bool changed = keep != selected;
        selected = keep;
        if (count() == 0) { selecting = false; selected.clear(); emit selectionChanged(); }
        else if (changed) emit selectionChanged();
    }
    if (feedMode()) rebuildFeed();
    updateScroll();
    const bool needSpin = loadingSomething();
    if (needSpin && !spin.isActive()) spin.start();
    if (!needSpin && spin.isActive()) spin.stop();
    viewport()->update();
}

void TrackList::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScroll();
}

bool TrackList::cardMode() const {
    return (tab == Tab::Results && !core->detailOpen && core->searchMode == Core::SearchMode::Playlists) ||
           (tab == Tab::Saved && core->savedOpenId.isEmpty());
}

int TrackList::count() const {
    if (!cardMode()) return core->list(tab).size();
    return tab == Tab::Results ? core->playlistResults.size() : core->playlists.size();
}

bool TrackList::showChips() const {
    return tab == Tab::Results && !core->detailOpen && !core->hasResults() && !core->searching && core->status.isEmpty() &&
           !core->history.isEmpty();
}

int TrackList::footerH() const {
    if (tab != Tab::Results || core->detailOpen || !core->hasResults()) return 0;
    return (core->canLoadMore() || core->searching) ? 56 : 0;
}

int TrackList::contentH() const { return feedMode() ? feedHeight : count() * kRowH + footerH(); }

void TrackList::updateScroll() {
    auto *sb = verticalScrollBar();
    sb->setPageStep(viewport()->height());
    sb->setRange(0, qMax(0, contentH() - viewport()->height()));
}

QVector<TrackList::ChipItem> TrackList::chipLayout(qreal width, qreal &endY) const {
    QVector<ChipItem> out;
    const QFontMetrics fm(fnt(12, QFont::Medium));
    qreal x = 0, y = 30;
    for (const QString &h : core->history) {
        const qreal w = fm.horizontalAdvance(h) + 24;
        if (x > 0 && x + w > width) { x = 0; y += 38; }
        out.append({QRectF(x, y, w, 30), h});
        x += w + 8;
    }
    endY = y + 30;
    return out;
}

QString TrackList::act1Icon() const {
    if (cardMode()) return tab == Tab::Saved ? QStringLiteral("play") : QString();
    return tab == Tab::Queue ? QString() : QStringLiteral("queueNext");
}

QString TrackList::act2Icon() const {
    if (cardMode()) return tab == Tab::Saved ? QStringLiteral("trash") : QStringLiteral("bookmark");
    if (tab == Tab::Queue || tab == Tab::Saved) return QStringLiteral("x");
    return QStringLiteral("plus");
}

void TrackList::rowRects(int i, QRectF &row, QRectF &like, QRectF &addpl, QRectF &a1, QRectF &a2, QRectF &dur) const {
    rowRectsAt(i * kRowH - verticalScrollBar()->value(), row, like, addpl, a1, a2, dur);
}

void TrackList::rowRectsAt(qreal y, QRectF &row, QRectF &like, QRectF &addpl, QRectF &a1, QRectF &a2, QRectF &dur) const {
    row = QRectF(0, y, viewport()->width(), kRowInner);
    const qreal by = y + (kRowInner - 34) / 2;
    qreal x = row.right() - 8 - 34;
    a2 = QRectF(x, by, 34, 34);
    if (!act1Icon().isEmpty()) { x -= 34; a1 = QRectF(x, by, 34, 34); } else a1 = {};
    if (cardMode()) {
        like = addpl = dur = {};
        return;
    }
    x -= 34;
    addpl = QRectF(x, by, 34, 34);
    x -= 34;
    like = QRectF(x, by, 34, 34);
    dur = QRectF(x - 52, y, 48, kRowInner);
}

TrackList::Hit TrackList::hitTest(const QPoint &pt) const {
    if (feedMode()) return hitFeed(pt);
    const int n = count();
    if (n == 0) {
        if (showChips()) {
            qreal endY;
            const auto chips = chipLayout(viewport()->width(), endY);
            for (int i = 0; i < chips.size(); ++i)
                if (chips[i].r.contains(pt)) return {Chip, i};
            if (QRectF(viewport()->width() - 60, 0, 60, 22).contains(pt)) return {ClearHist, 0};
        }
        return {};
    }
    const int first = verticalScrollBar()->value() / kRowH;
    for (int i = first; i < n; ++i) {
        QRectF row, like, addpl, a1, a2, dur;
        rowRects(i, row, like, addpl, a1, a2, dur);
        if (row.top() > viewport()->height()) break;
        if (!row.contains(pt)) continue;
        if (a2.contains(pt)) return {Act2, i};
        if (!a1.isNull() && a1.contains(pt)) return {Act1, i};
        if (!addpl.isNull() && addpl.contains(pt)) return {AddPl, i};
        if (!like.isNull() && like.contains(pt)) return {Like, i};
        return {Row, i};
    }
    if (footerH() > 0 && core->canLoadMore()) {
        const qreal y = n * kRowH - verticalScrollBar()->value();
        const int w = pillWidth(QStringLiteral("Load more"), QStringLiteral("plus"));
        if (QRectF((viewport()->width() - w) / 2.0, y + 13, w, 30).contains(pt)) return {LoadMore, 0};
    }
    return {};
}

void TrackList::mouseMoveEvent(QMouseEvent *e) {
    const Hit h = hitTest(e->position().toPoint());
    if (h.kind != hover.kind || h.idx != hover.idx) {
        hover = h;
        viewport()->setCursor(h.kind == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        viewport()->update();
    }
}

void TrackList::leaveEvent(QEvent *) {
    hover = {};
    viewport()->update();
}

void TrackList::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton && !selecting) {   // context menu for a song row
        const Hit h = hitTest(e->position().toPoint());
        if (h.kind == None || h.kind == LoadMore || h.kind == Chip || h.kind == ClearHist) return;
        if (feedMode()) {
            if (h.idx >= 0 && h.idx < feedItems.size() && feedItems[h.idx].kind == 2)
                emit trackMenuRequested(feedSecs[feedItems[h.idx].section].tracks[feedItems[h.idx].track], -1, e->globalPosition().toPoint());
        } else if (!cardMode()) {
            const QVector<Track> items = core->list(tab);
            if (h.idx >= 0 && h.idx < items.size()) emit trackMenuRequested(items[h.idx], h.idx, e->globalPosition().toPoint());
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const Hit h = hitTest(e->position().toPoint());
    if (h.kind == None) return;
    if (feedMode()) { pressFeed(h); return; }
    if (selecting && (h.kind == Row || h.kind == Like || h.kind == AddPl || h.kind == Act1 || h.kind == Act2)) {
        if (!selected.remove(h.idx)) selected.insert(h.idx);   // any click on a row ticks / unticks it
        emit selectionChanged();
        viewport()->update();
        return;
    }

    if (h.kind == LoadMore) { core->search(core->searchQuery, true); return; }
    if (h.kind == ClearHist) { core->clearHistory(); return; }
    if (h.kind == Chip) {
        qreal endY;
        const auto chips = chipLayout(viewport()->width(), endY);
        if (h.idx < chips.size()) emit searchRequested(chips[h.idx].text);
        return;
    }

    if (cardMode()) {
        if (remoteCards()) {
            if (h.idx >= core->playlistResults.size()) return;
            const PlaylistRef r = core->playlistResults[h.idx];
            if (h.kind == Row) { core->openPlaylist(r); verticalScrollBar()->setValue(0); }
            else if (h.kind == Act2) core->saveRemotePlaylist(r.url, r.name);
        } else {
            if (h.idx >= core->playlists.size()) return;
            const SavedPlaylist p = core->playlists[h.idx];
            if (h.kind == Row) { core->openSaved(p.id); verticalScrollBar()->setValue(0); }
            else if (h.kind == Act1) core->playTracks(p.tracks, 0, false);
            else if (h.kind == Act2) emit deletePlaylistRequested(p.id);
        }
        return;
    }

    const QVector<Track> items = core->list(tab);
    if (h.idx >= items.size()) return;
    switch (h.kind) {
    case Row:
        if (tab == Tab::Queue) core->playIndex(h.idx);
        else core->playFrom(tab, h.idx);
        break;
    case Like: core->toggleLike(items[h.idx]); break;
    case AddPl: emit addToPlaylistRequested(items[h.idx]); break;
    case Act1: core->playNext(tab, h.idx); break;
    case Act2:
        if (tab == Tab::Queue) core->removeFromQueue(h.idx);
        else if (tab == Tab::Saved) core->removeFromSaved(h.idx);
        else core->enqueue(tab, h.idx);
        break;
    default: break;
    }
}

QString TrackList::emptyMessage() const {
    switch (tab) {
    case Tab::Home: return {};
    case Tab::Results:
        if (core->detailOpen) return core->detail.loading ? QStringLiteral("Loading playlist…") : (core->detail.status.isEmpty() ? QStringLiteral("Empty playlist") : core->detail.status);
        if (core->status.isEmpty() && core->history.isEmpty()) return QStringLiteral("Search for songs or playlists");
        return core->status;
    case Tab::Queue: return QStringLiteral("Queue is empty");
    case Tab::Liked: return QStringLiteral("No liked songs yet. Tap the heart on a track.");
    case Tab::Saved:
        return core->savedOpenId.isEmpty() ? QStringLiteral("No playlists yet.\nCreate one, or save one from a playlist search.")
                                           : QStringLiteral("This playlist is empty");
    }
    return {};
}

void TrackList::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (feedMode()) { paintFeed(p); return; }
    const Palette &c = P();
    const int n = count();
    const int vw = viewport()->width(), vh = viewport()->height();

    if (n == 0) {
        if (showChips()) {
            Icons::paint(p, QRectF(0, 3, 14, 14), QStringLiteral("clock"), c.onSurfaceVariant);
            p.setFont(fnt(12));
            p.setPen(c.onSurfaceVariant);
            p.drawText(QRectF(20, 0, vw - 80, 20), Qt::AlignVCenter, QStringLiteral("Recent searches"));
            p.setPen(c.secondary);
            p.drawText(QRectF(vw - 60, 0, 60, 20), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("Clear"));
            qreal endY;
            const auto chips = chipLayout(vw, endY);
            for (int i = 0; i < chips.size(); ++i)
                drawPill(p, chips[i].r, chips[i].text, {}, false, hover.kind == Chip && hover.idx == i);
            return;
        }
        if (loadingSomething()) drawSpinner(p, QRectF(vw / 2.0 - 14, vh / 2.0 - 46, 28, 28), c.primary, clock.elapsed());
        p.setFont(fnt(15));
        p.setPen(c.onSurfaceVariant);
        p.drawText(QRectF(0, vh / 2.0 - 8, vw, 60), Qt::AlignHCenter | Qt::AlignTop, emptyMessage());
        return;
    }

    const bool cards = cardMode();
    const QVector<Track> *items = cards ? nullptr : &core->list(tab);
    const QString nowVid = core->nowVid();
    const int first = qMax(0, verticalScrollBar()->value() / kRowH);
    const QFont fTitle = fnt(14, QFont::Medium), fArtist = fnt(12), fDur = fnt(12);
    for (int i = first; i < n; ++i) {
        QRectF row, like, addpl, a1, a2, dur;
        rowRects(i, row, like, addpl, a1, a2, dur);
        if (row.top() > vh) break;

        QString title, sub;
        bool active = false;
        const Track *t = nullptr;
        if (cards && remoteCards()) {
            const PlaylistRef &r = core->playlistResults[i];
            title = r.name;
            sub = r.uploader;
        } else if (cards) {
            const SavedPlaylist &sp = core->playlists[i];
            title = sp.name;
            sub = (sp.tracks.size() == 1 ? QStringLiteral("1 song") : QStringLiteral("%1 songs").arg(sp.tracks.size())) + (sp.source.isEmpty() ? QString() : QStringLiteral("  ·  from YouTube"));
        } else {
            t = &(*items)[i];
            title = t->title;
            sub = t->artist;
            active = (tab == Tab::Queue) ? i == core->current : t->vid == nowVid;
        }
        const bool rowHover = hover.idx == i && hover.kind != None;

        p.setPen(Qt::NoPen);
        if (active) {
            p.setBrush(c.surfaceVariant);
            p.setPen(QPen(c.outline, 1));
            p.drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        } else if (rowHover) {
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawRoundedRect(row, 16, 16);
        }

        const QRectF th(row.left() + 8, row.top() + 8, 80, 48);
        if (t) drawThumb(p, th, t->vid, 12);
        else if (remoteCards()) drawThumbUrl(p, th, QStringLiteral("pl:") + core->playlistResults[i].url, core->playlistResults[i].thumbUrl, 12);
        else {
            const SavedPlaylist &sp = core->playlists[i];
            if (!sp.tracks.isEmpty()) drawThumb(p, th, sp.tracks.first().vid, 12);
            else drawThumbUrl(p, th, QString(), QString(), 12);
            // song-count badge
            const QString badge = QString::number(sp.tracks.size());
            const QFontMetrics bfm(fnt(11, QFont::Medium));
            const qreal bw = bfm.horizontalAdvance(badge) + 24;
            const QRectF br(th.right() - bw - 4, th.bottom() - 20, bw, 16);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(7, 7, 34, 200));
            p.drawRoundedRect(br, 8, 8);
            Icons::paint(p, QRectF(br.left() + 5, br.top() + 2, 12, 12), QStringLiteral("list"), c.onSurface);
            p.setFont(fnt(11, QFont::Medium));
            p.setPen(c.onSurface);
            p.drawText(br.adjusted(18, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight, badge);
        }
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(8, 8, 32, 165));
            p.drawRoundedRect(th, 12, 12);
            Icons::paint(p, QRectF(th.center().x() - 10, th.center().y() - 10, 20, 20),
                         core->paused ? QStringLiteral("play") : QStringLiteral("music"), c.primary);
        }

        const qreal rightEdge = cards ? (a1.isNull() ? a2.left() : a1.left()) : dur.left();
        const qreal tx = th.right() + 12, tw = rightEdge - tx - 4;
        p.setFont(fTitle);
        p.setPen(active ? c.primary : c.onSurface);
        p.drawText(QRectF(tx, row.top() + 10, tw, 22), Qt::AlignVCenter,
                   QFontMetrics(fTitle).elidedText(title, Qt::ElideRight, int(tw)));
        p.setFont(fArtist);
        p.setPen(c.onSurfaceVariant);
        p.drawText(QRectF(tx, row.top() + 32, tw, 20), Qt::AlignVCenter,
                   QFontMetrics(fArtist).elidedText(sub, Qt::ElideRight, int(tw)));
        auto hv = [&](Kind k) { return hover.kind == k && hover.idx == i; };
        if (selecting) {                                  // round checkbox instead of the buttons
            if (t) {
                p.setFont(fDur);
                p.setPen(c.onSurfaceVariant);
                p.drawText(QRectF(a2.left() - 60, dur.top(), 52, dur.height()), Qt::AlignVCenter | Qt::AlignRight, fmtTime(t->duration));
            }
            const QPointF cc = a2.center();
            if (selected.contains(i)) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.primary);
                p.drawEllipse(cc, 12, 12);
                Icons::paint(p, QRectF(cc.x() - 8, cc.y() - 8, 16, 16), QStringLiteral("check"), c.onPrimary);
            } else {
                p.setPen(QPen(c.onSurfaceVariant, 2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(cc, 11, 11);
            }
            continue;
        }
        if (t) {
            p.setFont(fDur);
            p.drawText(dur, Qt::AlignVCenter | Qt::AlignRight, fmtTime(t->duration));
            const bool liked = core->isLiked(t->vid);
            drawIconButton(p, like, QStringLiteral("heart"), hv(Like), false, liked, liked);
            drawIconButton(p, addpl, QStringLiteral("bookmark"), hv(AddPl));
        }
        if (!a1.isNull()) drawIconButton(p, a1, act1Icon(), hv(Act1));
        drawIconButton(p, a2, act2Icon(), hv(Act2));
    }

    if (footerH() > 0) {
        const qreal y = n * kRowH - verticalScrollBar()->value();
        if (core->canLoadMore()) {
            const int w = pillWidth(QStringLiteral("Load more"), QStringLiteral("plus"));
            drawPill(p, QRectF((vw - w) / 2.0, y + 13, w, 30), QStringLiteral("Load more"), QStringLiteral("plus"), false,
                     hover.kind == LoadMore);
        } else if (core->searching) {
            drawSpinner(p, QRectF(vw / 2.0 - 12, y + 16, 24, 24), c.primary, clock.elapsed());
        }
    }
}


// ------------------------------------------------------------------ "For you"
void TrackList::rebuildFeed() {
    feedSecs = core->homeSections();
    feedItems.clear();
    int y = 0;
    auto add = [&](int kind, int sec, int trk, int h) { feedItems.append({kind, sec, trk, y, h}); y += h; };
    add(0, -1, -1, 88);
    for (int s = 0; s < feedSecs.size(); ++s) {
        add(1, s, -1, 64);
        const int n = qMin(8, int(feedSecs[s].tracks.size()));
        for (int t = 0; t < n; ++t) add(2, s, t, kRowH);
        y += 8;
    }
    feedHeight = y + 8;
}

TrackList::Hit TrackList::hitFeed(const QPoint &pt) const {
    const int scroll = verticalScrollBar()->value();
    const int vw = viewport()->width();
    for (int i = 0; i < feedItems.size(); ++i) {
        const FeedItem &it = feedItems[i];
        const qreal y = it.y - scroll;
        if (y > viewport()->height()) break;
        if (y + it.h < 0 || pt.y() < y || pt.y() >= y + it.h) continue;
        if (it.kind == 0) {
            return QRectF(vw - 8 - 44, y + 18, 44, 44).contains(pt) ? Hit{HeroRefresh, i} : Hit{};
        }
        if (it.kind == 1) {
            if (feedSecs[it.section].tracks.isEmpty()) return {};
            if (QRectF(vw - 8 - 40, y + 12, 40, 40).contains(pt)) return {HeaderShuffle, i};
            if (QRectF(vw - 8 - 88, y + 12, 40, 40).contains(pt)) return {HeaderPlay, i};
            return {};
        }
        QRectF row, like, addpl, a1, a2, dur;
        rowRectsAt(y, row, like, addpl, a1, a2, dur);
        if (a2.contains(pt)) return {Act2, i};
        if (!a1.isNull() && a1.contains(pt)) return {Act1, i};
        if (addpl.contains(pt)) return {AddPl, i};
        if (like.contains(pt)) return {Like, i};
        return {Row, i};
    }
    return {};
}

void TrackList::pressFeed(const Hit &h) {
    if (h.idx < 0 || h.idx >= feedItems.size()) return;
    const FeedItem &it = feedItems[h.idx];
    switch (h.kind) {
    case HeroRefresh: core->refreshFeed(); return;
    case HeaderPlay: core->playTracks(feedSecs[it.section].tracks, 0, false); return;
    case HeaderShuffle: core->playTracks(feedSecs[it.section].tracks, 0, true); return;
    default: break;
    }
    if (it.kind != 2) return;
    const Track &t = feedSecs[it.section].tracks[it.track];
    switch (h.kind) {
    case Row: core->playTracks(feedSecs[it.section].tracks, it.track, false); break;
    case Like: core->toggleLike(t); break;
    case AddPl: emit addToPlaylistRequested(t); break;
    case Act1: core->playNextTrack(t); break;
    case Act2: core->enqueueTrack(t); break;
    default: break;
    }
}

void TrackList::paintFeed(QPainter &p) {
    const Palette &c = P();
    const int vw = viewport()->width(), vh = viewport()->height();
    const int scroll = verticalScrollBar()->value();
    const QString nowVid = core->nowVid();
    const QFont fTitle = fnt(14, QFont::Medium), fArtist = fnt(12), fDur = fnt(12);
    for (int i = 0; i < feedItems.size(); ++i) {
        const FeedItem &it = feedItems[i];
        const qreal y = it.y - scroll;
        if (y > vh) break;
        if (y + it.h < 0) continue;
        auto hv = [&](Kind k) { return hover.kind == k && hover.idx == i; };

        if (it.kind == 0) { // hero
            p.setFont(fnt(30, QFont::Bold));
            p.setPen(c.onSurface);
            p.drawText(QRectF(4, y + 6, vw - 80, 44), Qt::AlignVCenter, QStringLiteral("For you"));
            p.setFont(fnt(13));
            p.setPen(core->feedBuilding ? c.tertiary : c.onSurfaceVariant);
            const QString sub = core->feedBuilding ? QStringLiteral("Updating your picks…")
                                : (core->feed.isEmpty() ? QStringLiteral("Couldn't load picks. Check your connection and refresh.")
                                                        : QStringLiteral("Picks from what you play and like"));
            p.drawText(QRectF(4, y + 50, vw - 80, 22), Qt::AlignVCenter, sub);
            const QRectF b(vw - 8 - 44, y + 18, 44, 44);
            p.setPen(Qt::NoPen);
            p.setBrush(hv(HeroRefresh) ? c.surfaceVariant : c.outline);
            p.drawEllipse(b);
            Icons::paint(p, QRectF(b.center().x() - 10, b.center().y() - 10, 20, 20), QStringLiteral("refresh"), c.onSurface);
            continue;
        }
        if (it.kind == 1) { // shelf header
            const FeedSection &fs = feedSecs[it.section];
            p.setFont(fnt(18, QFont::Bold));
            p.setPen(c.onSurface);
            const int tw = vw - 120;
            p.drawText(QRectF(4, y + 6, tw, 28), Qt::AlignVCenter, QFontMetrics(fnt(18, QFont::Bold)).elidedText(fs.title, Qt::ElideRight, tw));
            p.setFont(fnt(12));
            p.setPen(c.onSurfaceVariant);
            p.drawText(QRectF(4, y + 34, tw, 20), Qt::AlignVCenter, QFontMetrics(fnt(12)).elidedText(fs.subtitle, Qt::ElideRight, tw));
            const QRectF bs(vw - 8 - 40, y + 12, 40, 40), bp(vw - 8 - 88, y + 12, 40, 40);
            for (const auto &pr : {std::make_pair(bp, QStringLiteral("play")), std::make_pair(bs, QStringLiteral("shuffle"))}) {
                const bool h2 = hv(pr.second == QLatin1String("play") ? HeaderPlay : HeaderShuffle);
                p.setPen(Qt::NoPen);
                p.setBrush(h2 ? c.outline : c.surfaceVariant);
                p.drawEllipse(pr.first);
                Icons::paint(p, QRectF(pr.first.center().x() - 9, pr.first.center().y() - 9, 18, 18), pr.second, c.onSurface);
            }
            continue;
        }
        // track row
        const Track &t = feedSecs[it.section].tracks[it.track];
        QRectF row, like, addpl, a1, a2, dur;
        rowRectsAt(y, row, like, addpl, a1, a2, dur);
        const bool active = t.vid == nowVid;
        const bool rowHover = hover.idx == i && hover.kind != None;
        p.setPen(Qt::NoPen);
        if (active) {
            p.setBrush(c.surfaceVariant);
            p.setPen(QPen(c.outline, 1));
            p.drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        } else if (rowHover) {
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawRoundedRect(row, 16, 16);
        }
        const QRectF th(row.left() + 8, row.top() + 8, 80, 48);
        drawThumb(p, th, t.vid, 12);
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(8, 8, 32, 165));
            p.drawRoundedRect(th, 12, 12);
            Icons::paint(p, QRectF(th.center().x() - 10, th.center().y() - 10, 20, 20), core->paused ? QStringLiteral("play") : QStringLiteral("music"), c.primary);
        }
        const qreal tx = th.right() + 12, tw = dur.left() - tx - 4;
        p.setFont(fTitle);
        p.setPen(active ? c.primary : c.onSurface);
        p.drawText(QRectF(tx, row.top() + 10, tw, 22), Qt::AlignVCenter, QFontMetrics(fTitle).elidedText(t.title, Qt::ElideRight, int(tw)));
        p.setFont(fArtist);
        p.setPen(c.onSurfaceVariant);
        p.drawText(QRectF(tx, row.top() + 32, tw, 20), Qt::AlignVCenter, QFontMetrics(fArtist).elidedText(t.artist, Qt::ElideRight, int(tw)));
        p.setFont(fDur);
        p.drawText(dur, Qt::AlignVCenter | Qt::AlignRight, fmtTime(t.duration));
        const bool liked = core->isLiked(t.vid);
        drawIconButton(p, like, QStringLiteral("heart"), hv(Like), false, liked, liked);
        drawIconButton(p, addpl, QStringLiteral("bookmark"), hv(AddPl));
        drawIconButton(p, a1, QStringLiteral("queueNext"), hv(Act1));
        drawIconButton(p, a2, QStringLiteral("plus"), hv(Act2));
    }
}
