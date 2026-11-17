#include "tracklist.h"
#include "thumbs.h"
#include "ui_common.h"
#include <QMouseEvent>
#include <QPainter>
#include <QHash>
#include <QScrollBar>
#include <QWheelEvent>

using namespace UI;

// Track::isExplicit() runs a regular expression; rows are repainted on every scroll step, so remember the answer.
static bool isExplicitCached(const Track &t) {
    static QHash<QString, bool> cache;
    const auto it = cache.constFind(t.vid);
    if (it != cache.constEnd()) return it.value();
    if (cache.size() > 5000) cache.clear();
    const bool e = t.isExplicit();
    cache.insert(t.vid, e);
    return e;
}

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
    connect(core->downloads, &Downloads::changed, viewport(), qOverload<>(&QWidget::update));
    dwell.setSingleShot(true);
    dwell.setInterval(120);
    connect(&dwell, &QTimer::timeout, this, [this] {
        if (const Track *t = trackAt(hover)) core->prefetch(t->vid);
    });
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

    scrollAnim.setEasingCurve(QEasingCurve::OutCubic);
    scrollAnim.setDuration(180);
    connect(&scrollAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        verticalScrollBar()->setValue(v.toInt());
    });
    fadeAnim.setEasingCurve(QEasingCurve::OutCubic);
    fadeAnim.setDuration(150);
    fadeAnim.setStartValue(0.0);
    fadeAnim.setEndValue(1.0);
    connect(&fadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        fade = v.toReal();
        viewport()->update();
    });
    connect(&fadeAnim, &QVariantAnimation::finished, this, [this] {
        fade = 1.0;
        viewport()->update();
    });
}

// A mouse wheel notch glides ~1.5 rows; touchpads already send fine-grained deltas and are followed directly.
void TrackList::wheelEvent(QWheelEvent *e) {
    QScrollBar *sb = verticalScrollBar();
    if (!e->pixelDelta().isNull()) {
        scrollAnim.stop();
        sb->setValue(sb->value() - e->pixelDelta().y());
        e->accept();
        return;
    }
    const int dy = e->angleDelta().y();
    if (dy == 0) {
        e->ignore();
        return;
    }
    const bool gliding = scrollAnim.state() == QAbstractAnimation::Running;
    const int from = sb->value();
    const int target = qBound(sb->minimum(), (gliding ? scrollTarget : from) - qRound(dy / 120.0 * kRowH * 1.5), sb->maximum());
    scrollAnim.stop();
    scrollTarget = target;
    if (target != from) {
        scrollAnim.setStartValue(from);
        scrollAnim.setEndValue(target);
        scrollAnim.start();
    }
    e->accept();
}

void TrackList::setTab(Tab t) {
    const bool changed = tab != t;
    tab = t;
    if (selecting) stopSelect();
    hover = {};
    scrollAnim.stop();
    verticalScrollBar()->setValue(0);
    refresh();
    if (changed && isVisible()) {
        fadeAnim.stop();
        fade = 0.0;
        fadeAnim.start();
    }
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
    if (feedMode()) rebuildFeed();   // the card grid depends on the width
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
    if (h.kind != hover.kind || h.idx != hover.idx || h.sub != hover.sub) {
        if (h.idx != hover.idx) { if (h.kind == None) dwell.stop(); else dwell.start(); }
        hover = h;
        viewport()->setCursor(h.kind == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        viewport()->update();
    }
}

const Track *TrackList::trackAt(const Hit &h) const {
    if (h.kind == None || h.idx < 0 || selecting) return nullptr;
    if (feedMode()) {
        if (h.idx >= feedItems.size() || feedItems[h.idx].kind != 2) return nullptr;
        const FeedItem &it = feedItems[h.idx];
        if (it.section >= feedSecs.size() || it.track >= feedSecs[it.section].tracks.size()) return nullptr;
        return &feedSecs[it.section].tracks[it.track];
    }
    if (cardMode() || (h.kind != Row && h.kind != Like && h.kind != AddPl && h.kind != Act1 && h.kind != Act2)) return nullptr;
    const QVector<Track> &items = core->list(tab);
    return h.idx < items.size() ? &items[h.idx] : nullptr;
}

// Small round badge in the thumbnail's corner: progress ring while downloading, a check once it's on this PC.
void TrackList::drawDlBadge(QPainter &p, const QRectF &th, const QString &vid) const {
    const Downloads *d = core->downloads;
    const bool done = d->isDownloaded(vid), active = d->isActive(vid);
    if (!done && !active) return;
    const Palette &c = P();
    const QRectF b(th.right() - 21, th.bottom() - 21, 18, 18);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(done ? c.primary : QColor(7, 7, 34, 215));
    p.drawEllipse(b);
    if (done) {
        Icons::paint(p, b.adjusted(4, 4, -4, -4), QStringLiteral("check"), c.onPrimary);
    } else {
        p.setPen(QPen(c.outline, 2));
        p.drawEllipse(b.adjusted(2.5, 2.5, -2.5, -2.5));
        if (d->isRunning(vid)) {
            p.setPen(QPen(c.primary, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(b.adjusted(2.5, 2.5, -2.5, -2.5), 90 * 16, -int(qMax(0.04, d->progress(vid)) * 360 * 16));
        }
        Icons::paint(p, b.adjusted(5, 5, -5, -5), QStringLiteral("download"), c.onSurface);
    }
    p.restore();
}

void TrackList::leaveEvent(QEvent *) {
    dwell.stop();
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

// Draws a song's artist line; explicit songs get a small rounded "E" tag in front.
static void drawArtistLine(QPainter &p, const Palette &c, const QFont &f, const QRectF &r, const QString &text, bool isExplicit) {
    p.setFont(f);
    p.setPen(c.onSurfaceVariant);
    qreal ax = r.left();
    if (isExplicit) {
        const QRectF tag(r.left(), r.top() + 4, 14, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(c.onSurfaceVariant);
        p.drawRoundedRect(tag, 3, 3);
        QFont tf = f;
        tf.setPixelSize(10);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(c.surface);
        p.drawText(tag, Qt::AlignCenter, QStringLiteral("E"));
        p.setFont(f);
        p.setPen(c.onSurfaceVariant);
        ax += 20;
    }
    const int w = int(r.right() - ax);
    p.drawText(QRectF(ax, r.top(), r.right() - ax, r.height()), Qt::AlignVCenter, QFontMetrics(f).elidedText(text, Qt::ElideRight, w));
}

void TrackList::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (fade < 1.0) {
        p.setOpacity(fade);
        p.translate(0, (1.0 - fade) * 10);
    }
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
        if (t) { drawThumb(p, th, t->vid, 12); if (!active) drawDlBadge(p, th, t->vid); }
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
        drawArtistLine(p, c, fArtist, QRectF(tx, row.top() + 32, tw, 20), sub, t && isExplicitCached(*t));
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
// Shelves are grids of artwork cards: as many columns as fit, two rows per shelf (one for "Jump back in").
void TrackList::rebuildFeed() {
    feedSecs = core->homeSections();
    feedItems.clear();
    constexpr int gap = 14;
    const int vw = qMax(240, viewport()->width() - 8);   // keep clear of the scroll bar
    const int cols = qMax(2, (vw + gap) / (172 + gap));
    const int cardW = (vw - (cols - 1) * gap) / cols;
    feedThumbH = cardW;   // square album art, like YT Music
    const int cardH = feedThumbH + 56;
    int y = 0;
    const int chipH = Theme::I().apple() ? 0 : 52;   // Apple Music has no mood chips
    feedItems.append({3, -1, -1, y, chipH, 0, vw});
    y += chipH;
    if (!Theme::I().apple() && !feedSecs.isEmpty() && !feedSecs[0].tracks.isEmpty()) {   // big "listen again" banner
        const int hh = qBound(190, vw * 11 / 40, 250);
        feedItems.append({4, 0, 0, y, hh, 0, vw});
        y += hh + 14;
    }
    const bool status = core->feedBuilding || core->feed.isEmpty();   // otherwise no status line
    feedItems.append({0, -1, -1, y, status ? 26 : 0, 0, vw});
    y += status ? 30 : 4;
    for (int s = 0; s < feedSecs.size(); ++s) {
        feedItems.append({1, s, -1, y, 66, 0, vw});
        y += 66;
        const int rows = feedSecs[s].id == QLatin1String("recent") ? 1 : 2;
        const int n = qMin(cols * rows, int(feedSecs[s].tracks.size()));
        for (int t = 0; t < n; ++t)
            feedItems.append({2, s, t, y + (t / cols) * (cardH + gap), cardH, (t % cols) * (cardW + gap), cardW});
        y += ((n + cols - 1) / cols) * (cardH + gap) + 6;
    }
    feedHeight = y + 8;
}

const QStringList &TrackList::moods() {
    static const QStringList m{QStringLiteral("Energize"), QStringLiteral("Relax"), QStringLiteral("Workout"), QStringLiteral("Focus"),
                               QStringLiteral("Commute"), QStringLiteral("Party"), QStringLiteral("Feel good"), QStringLiteral("Sad"),
                               QStringLiteral("Romance")};
    return m;
}

QVector<QRectF> TrackList::moodRects(qreal width) const {
    QVector<QRectF> v;
    const QFontMetrics fm(fnt(13, QFont::Medium));
    qreal x = 0;
    for (const QString &m : moods()) {
        const qreal w = fm.horizontalAdvance(m) + 30;
        if (x + w > width) break;   // whatever fits on one row
        v.append(QRectF(x, 0, w, 34));
        x += w + 10;
    }
    return v;
}

TrackList::Hit TrackList::hitFeed(const QPoint &pt) const {
    const int scroll = verticalScrollBar()->value();
    for (int i = 0; i < feedItems.size(); ++i) {
        const FeedItem &it = feedItems[i];
        const qreal y = it.y - scroll;
        if (y > viewport()->height()) break;
        if (y + it.h < 0 || pt.y() < y || pt.y() >= y + it.h) continue;
        if (it.kind == 0) return {};
        if (it.kind == 4) return {HeaderPlay, i};
        if (it.kind == 3) {
            const QVector<QRectF> rs = moodRects(it.w);
            for (int k = 0; k < rs.size(); ++k)
                if (rs[k].translated(0, y + 6).contains(pt)) return {Chip, i, k};
            return {};
        }
        if (it.kind == 1) {
            if (feedSecs[it.section].tracks.isEmpty()) return {};
            const qreal right = it.x + it.w;
            if (QRectF(right - 40, y + 12, 40, 40).contains(pt)) return {HeaderShuffle, i};
            if (QRectF(right - 88, y + 12, 40, 40).contains(pt)) return {HeaderPlay, i};
            return {};
        }
        if (pt.x() < it.x || pt.x() >= it.x + it.w) continue;
        // hover buttons in the artwork's top-right corner
        if (QRectF(it.x + it.w - 8 - 30, y + 8, 30, 30).contains(pt)) return {Act2, i};
        if (QRectF(it.x + it.w - 8 - 30 - 36, y + 8, 30, 30).contains(pt)) return {Like, i};
        return {Row, i};
    }
    return {};
}

void TrackList::pressFeed(const Hit &h) {
    if (h.idx < 0 || h.idx >= feedItems.size()) return;
    const FeedItem &it = feedItems[h.idx];
    if (h.kind == Chip && it.kind == 3) {
        if (h.sub >= 0 && h.sub < moods().size()) emit searchRequested(moods()[h.sub] + QStringLiteral(" music mix"));
        return;
    }
    if (it.section < 0 || it.section >= feedSecs.size()) return;
    switch (h.kind) {
    case HeaderPlay: core->playTracks(feedSecs[it.section].tracks, 0, false); return;
    case HeaderShuffle: core->playTracks(feedSecs[it.section].tracks, 0, true); return;
    default: break;
    }
    if (it.kind != 2 || it.track >= feedSecs[it.section].tracks.size()) return;
    const Track t = feedSecs[it.section].tracks[it.track];
    switch (h.kind) {
    case Row: core->playTracks(feedSecs[it.section].tracks, it.track, false); break;
    case Like: core->toggleLike(t); break;
    case Act2: core->enqueueTrack(t); emit core->message(QStringLiteral("Added to the queue")); break;
    default: break;
    }
}

void TrackList::paintFeed(QPainter &p) {
    const Palette &c = P();
    const int vh = viewport()->height();
    const int scroll = verticalScrollBar()->value();
    const QString nowVid = core->nowVid();
    const QFont fTitle = fnt(13, QFont::Medium), fArtist = fnt(12);
    const QFontMetrics fmTitle(fTitle);
    for (int i = 0; i < feedItems.size(); ++i) {
        const FeedItem &it = feedItems[i];
        const qreal y = it.y - scroll;
        if (y > vh) break;
        if (y + it.h < 0) continue;
        auto hv = [&](Kind k) { return hover.kind == k && hover.idx == i; };

        if (it.kind == 3 && it.h == 0) continue;
        if (it.kind == 3) { // mood chips (YT Music)
            const QVector<QRectF> rs = moodRects(it.w);
            p.setFont(fnt(13, QFont::Medium));
            for (int k = 0; k < rs.size(); ++k) {
                const QRectF r = rs[k].translated(0, y + 6);
                const bool h = hover.kind == Chip && hover.idx == i && hover.sub == k;
                QColor fill = c.onSurface;
                fill.setAlphaF(h ? 0.20 : 0.10);
                p.setPen(Qt::NoPen);
                p.setBrush(fill);
                p.drawRoundedRect(r, 11, 11);
                p.setPen(c.onSurface);
                p.drawText(r, Qt::AlignCenter, moods()[k]);
            }
            continue;
        }
        if (it.kind == 4) { // hero banner: the first pick, large, with a play button
            const Track &t = feedSecs[0].tracks[0];
            const QRectF hr(it.x, y, it.w, it.h);
            const int rad = 22;
            const QPixmap hq = Thumbs::I().getUrl(t.vid + QStringLiteral(":hq"), QStringLiteral("https://i.ytimg.com/vi/%1/hq720.jpg").arg(t.vid),
                                                  hr.size().toSize(), p.device()->devicePixelRatioF(), rad);
            if (!hq.isNull()) p.drawPixmap(hr.topLeft(), hq);
            else drawThumb(p, hr, t.vid, rad);
            QLinearGradient shade(hr.left(), 0, hr.right(), 0);
            shade.setColorAt(0, QColor(0, 0, 0, 215));
            shade.setColorAt(0.55, QColor(0, 0, 0, 120));
            shade.setColorAt(1, QColor(0, 0, 0, 20));
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawRoundedRect(hr, rad, rad);
            const qreal tw = qMin<qreal>(it.w * 0.62, 520);
            QFont fe = fnt(11, QFont::Bold);
            fe.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
            p.setFont(fe);
            p.setPen(c.primary);
            p.drawText(QRectF(it.x + 28, y + it.h * 0.17, tw, 18), Qt::AlignVCenter, feedSecs[0].title.toUpper());
            const QFont fb = fnt(30, QFont::Bold);
            p.setFont(fb);
            p.setPen(Qt::white);
            p.drawText(QRectF(it.x + 28, y + it.h * 0.17 + 24, tw, 76), Qt::AlignTop | Qt::TextWordWrap,
                       QFontMetrics(fb).elidedText(t.title, Qt::ElideRight, int(tw * 2 - 20)));
            p.setFont(fnt(14));
            p.setPen(QColor(255, 255, 255, 190));
            p.drawText(QRectF(it.x + 28, y + it.h - 86, tw, 20), Qt::AlignVCenter, t.artist);
            const QRectF pb(it.x + 28, y + it.h - 54, 112, 38);
            const bool ph = hv(HeaderPlay);
            p.setPen(Qt::NoPen);
            p.setBrush(ph ? c.primary.lighter(112) : c.primary);
            p.drawRoundedRect(pb, 19, 19);
            Icons::paint(p, QRectF(pb.left() + 16, pb.center().y() - 8, 16, 16), QStringLiteral("play"), c.onPrimary);
            p.setFont(fnt(14, QFont::Bold));
            p.setPen(c.onPrimary);
            p.drawText(pb.adjusted(36, 0, 0, 0), Qt::AlignVCenter, QStringLiteral("Play"));
            continue;
        }
        if (it.kind == 0 && it.h == 0) continue;
        if (it.kind == 0) { // status line under the page title
            p.setFont(fnt(13));
            p.setPen(core->feedBuilding ? c.tertiary : c.onSurfaceVariant);
            const QString sub = core->feedBuilding ? QStringLiteral("Updating your picks…")
                                : (core->feed.isEmpty() ? QStringLiteral("Couldn't load picks. Check your connection and refresh.")
                                                        : QStringLiteral("Picks from what you play and like"));
            p.drawText(QRectF(2, y, it.w - 4, 22), Qt::AlignVCenter, sub);
            continue;
        }
        if (it.kind == 1) { // shelf header
            const FeedSection &fs = feedSecs[it.section];
            const int tw = it.w - 104;
            QFont fEyebrow = fnt(11, QFont::DemiBold);
            fEyebrow.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            p.setFont(fEyebrow);
            p.setPen(c.onSurfaceVariant);
            p.drawText(QRectF(2, y + 8, tw, 16), Qt::AlignVCenter, QFontMetrics(fEyebrow).elidedText(fs.subtitle.toUpper(), Qt::ElideRight, tw));
            const QFont fBig = fnt(24, QFont::Bold);
            p.setFont(fBig);
            p.setPen(c.onSurface);
            p.drawText(QRectF(2, y + 24, tw, 34), Qt::AlignVCenter, QFontMetrics(fBig).elidedText(fs.title, Qt::ElideRight, tw));
            const qreal right = it.x + it.w;
            const QRectF bs(right - 40, y + 12, 40, 40), bp(right - 88, y + 12, 40, 40);
            p.setPen(Qt::NoPen);
            p.setBrush(hv(HeaderPlay) ? c.primary.lighter(112) : c.primary);
            p.drawEllipse(bp);
            Icons::paint(p, QRectF(bp.center().x() - 9, bp.center().y() - 9, 18, 18), QStringLiteral("play"), c.onPrimary);
            p.setPen(Qt::NoPen);
            p.setBrush(hv(HeaderShuffle) ? c.outline : c.surfaceVariant);
            p.drawEllipse(bs);
            Icons::paint(p, QRectF(bs.center().x() - 9, bs.center().y() - 9, 18, 18), QStringLiteral("shuffle"), c.onSurface);
            continue;
        }

        // artwork card
        if (it.section >= feedSecs.size() || it.track >= feedSecs[it.section].tracks.size()) continue;
        const Track &t = feedSecs[it.section].tracks[it.track];
        const bool active = t.vid == nowVid;
        const bool cardHover = hover.idx == i && hover.kind != None;
        const QRectF th(it.x, y, it.w, feedThumbH);
        drawThumb(p, th, t.vid, Theme::I().apple() ? 8 : 12);
        if (!active && !cardHover) {
            drawDlBadge(p, th, t.vid);
        }
        if (active || cardHover) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, cardHover ? (Theme::I().apple() ? 55 : 90) : 150));
            p.drawRoundedRect(th, Theme::I().apple() ? 8 : 12, Theme::I().apple() ? 8 : 12);
        }
        if (cardHover) {
            const bool apl = Theme::I().apple();   // Apple Music: small translucent play disc at the artwork's bottom-left
            const QRectF pc = apl ? QRectF(th.left() + 10, th.bottom() - 10 - 34, 34, 34) : QRectF(th.right() - 14 - 44, th.bottom() - 14 - 44, 44, 44);
            p.setPen(Qt::NoPen);
            p.setBrush(apl ? QColor(255, 255, 255, 215) : c.primary);
            p.drawEllipse(pc);
            Icons::paint(p, QRectF(pc.center().x() - 8, pc.center().y() - 8, 16, 16), QStringLiteral("play"), apl ? c.primary : c.onPrimary);
            const bool liked = core->isLiked(t.vid);
            const QRectF bq(it.x + it.w - 8 - 30, y + 8, 30, 30), bl(bq.left() - 36, y + 8, 30, 30);
            p.setPen(Qt::NoPen);
            p.setBrush(hv(Act2) ? c.outline : QColor(7, 7, 34, 210));
            p.drawEllipse(bq);
            Icons::paint(p, bq.adjusted(8, 8, -8, -8), QStringLiteral("plus"), c.onSurface);
            p.setPen(Qt::NoPen);
            p.setBrush(hv(Like) ? c.outline : QColor(7, 7, 34, 210));
            p.drawEllipse(bl);
            Icons::paint(p, bl.adjusted(8, 8, -8, -8), QStringLiteral("heart"), liked ? c.primary : c.onSurface, liked);
        } else if (active) {
            Icons::paint(p, QRectF(th.center().x() - 13, th.center().y() - 13, 26, 26),
                         core->paused ? QStringLiteral("play") : QStringLiteral("music"), c.primary);
        }
        const qreal tx = it.x + 2, tw = it.w - 4;
        p.setFont(fTitle);
        p.setPen(active ? c.primary : c.onSurface);
        p.drawText(QRectF(tx, th.bottom() + 8, tw, 20), Qt::AlignVCenter, fmTitle.elidedText(t.title, Qt::ElideRight, int(tw)));
        drawArtistLine(p, c, fArtist, QRectF(tx, th.bottom() + 28, tw, 20), t.artist, isExplicitCached(t));
    }
}
