#include "mainwindow.h"
#include "playerbar.h"
#include "lyrics.h"
#include "tracklist.h"
#include "paths.h"
#include "ui_common.h"
#include "settings.h"
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QDir>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QFileInfo>
#include <QLabel>
#include <QFrame>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QCursor>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QWindow>
#include <malloc.h>
#include "thumbs.h"

using namespace UI;

// ---------------------------------------------------------------------- Toast
class Toast : public QWidget {
public:
    explicit Toast(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, this, [this] { hide(); });
    }
    void showText(const QString &t) {
        text = t;
        const QFontMetrics fm(fnt(13, QFont::Medium));
        resize(qMin(parentWidget()->width() - 40, fm.horizontalAdvance(t) + 40), 36);
        raise();
        show();
        update();
        timer.start(2800);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const Palette &c = P();
        p.setBrush(c.surfaceVariant);
        p.setPen(QPen(c.primary, 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
        p.setFont(fnt(13, QFont::Medium));
        p.setPen(c.onSurface);
        p.drawText(rect().adjusted(16, 0, -16, 0), Qt::AlignCenter, QFontMetrics(p.font()).elidedText(text, Qt::ElideRight, width() - 32));
    }
private:
    QString text;
    QTimer timer;
};

// ------------------------------------------------------------------ SearchBox
SearchBox::SearchBox(QWidget *parent) : QWidget(parent) {
    setFixedHeight(50);
    setMouseTracking(true);
    edit = new QLineEdit(this);
    edit->setFrame(false);
    edit->setPlaceholderText(QStringLiteral("Search YouTube or paste a link…   ( / )"));
    edit->setContextMenuPolicy(Qt::DefaultContextMenu);
    auto restyle = [this] {
        const Palette &c = P();
        edit->setFont(fnt(15));
        edit->setStyleSheet(QStringLiteral("QLineEdit{background:transparent;border:none;color:%1;"
                                           "selection-background-color:%2;selection-color:%3}")
                                .arg(c.onSurface.name(), c.primary.name(), c.onPrimary.name()));
        QPalette pal = edit->palette();
        pal.setColor(QPalette::PlaceholderText, c.onSurfaceVariant);
        edit->setPalette(pal);
        update();
    };
    connect(&Theme::I(), &Theme::changed, this, restyle);
    restyle();
    connect(edit, &QLineEdit::returnPressed, this, [this] { emit submitted(edit->text()); });
    connect(edit, &QLineEdit::textChanged, this, qOverload<>(&QWidget::update));
}

QRectF SearchBox::goRect() const { return QRectF(width() - 6 - 38, 6, 38, 38); }
QRectF SearchBox::clearRect() const { return QRectF(goRect().left() - 4 - 32, 9, 32, 32); }

void SearchBox::resizeEvent(QResizeEvent *) {
    const int right = edit->text().isEmpty() ? int(goRect().left()) - 8 : int(clearRect().left()) - 4;
    edit->setGeometry(18 + 20 + 10, 8, right - 48, 34);
}

void SearchBox::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    const bool focus = edit->hasFocus();
    p.setBrush(c.surfaceVariant);
    p.setPen(QPen(focus ? c.primary : c.outline, focus ? 2 : 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 25, 25);
    Icons::paint(p, QRectF(18, 15, 20, 20), QStringLiteral("search"), c.onSurfaceVariant);
    if (!edit->text().isEmpty()) drawIconButton(p, clearRect(), QStringLiteral("x"), hover == 1);
    drawIconButton(p, goRect(), QStringLiteral("search"), false, true);
    // geometry depends on whether the clear button is shown
    const int right = edit->text().isEmpty() ? int(goRect().left()) - 8 : int(clearRect().left()) - 4;
    if (edit->width() != right - 48) edit->setGeometry(48, 8, right - 48, 34);
}

void SearchBox::mouseMoveEvent(QMouseEvent *e) {
    const int h = (!edit->text().isEmpty() && clearRect().contains(e->position())) ? 1 : goRect().contains(e->position()) ? 2 : 0;
    if (h != hover) {
        hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}
void SearchBox::leaveEvent(QEvent *) { hover = 0; update(); }
void SearchBox::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    if (!edit->text().isEmpty() && clearRect().contains(e->position())) {
        edit->clear();
        edit->setFocus();
    } else if (goRect().contains(e->position())) {
        emit submitted(edit->text());
    } else {
        edit->setFocus();
    }
}

// -------------------------------------------------------------------- SideBar
namespace {
constexpr int kNavTop = 62, kNavH = 40, kNavGap = 4, kPlRow = 34;
}

SideBar::SideBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
}

void SideBar::setTab(Tab t) {
    tab = t;
    update();
}

void SideBar::setCompact(bool on) {
    if (compact == on) return;
    compact = on;
    plScroll = 0;
    hover = -1;
    update();
}

// The scrolling list of playlists: between the "Playlists" caption and the two bottom actions.
QRectF SideBar::playlistArea() const {
    const qreal top = kNavTop + 5 * (kNavH + kNavGap) + 14 + 28;
    const qreal bottom = height() - 8 - 2 * (kNavH + kNavGap) - 6;
    return QRectF(8, top, width() - 16, qMax<qreal>(0, bottom - top));
}

int SideBar::playlistContentH() const { return int(core->playlists.size()) * kPlRow; }

QVector<SideBar::Item> SideBar::items() const {
    QVector<Item> v;
    const qreal x = 8, w = width() - 16;
    qreal y = kNavTop;
    const bool plOpen = tab == Tab::Saved && !core->savedOpenId.isEmpty();
    auto nav = [&](Tab t, const QString &label, const QString &icon, int count) {
        // an opened playlist is highlighted in the list below instead (unless that list is hidden)
        const bool active = tab == t && !nowPlayingOpen && !(t == Tab::Saved && plOpen && !compact);
        v.append({QRectF(x, y, w, kNavH), Nav, int(t), label, icon, QString(), count > 0 ? QString::number(count) : QString(), active});
        y += kNavH + kNavGap;
    };
    nav(Tab::Home, QStringLiteral("For you"), QStringLiteral("home"), 0);
    nav(Tab::Results, QStringLiteral("Search"), QStringLiteral("search"), 0);
    nav(Tab::Queue, QStringLiteral("Queue"), QStringLiteral("list"), int(core->queue.size()));
    nav(Tab::Liked, QStringLiteral("Liked songs"), QStringLiteral("heart"), int(core->liked.size()));
    nav(Tab::Saved, QStringLiteral("Playlists"), QStringLiteral("bookmark"), int(core->playlists.size()));

    if (!compact) {
        y += 14;
        v.append({QRectF(x + 12, y, w - 12 - 32, 22), Caption, 0, QStringLiteral("YOUR PLAYLISTS"), QString(), QString(), QString(), false});
        v.append({QRectF(x + w - 28, y - 3, 28, 28), NewPlaylist, 17, QStringLiteral("New playlist"), QStringLiteral("plus"), QString(), QString(), false});
        const QRectF area = playlistArea();
        qreal py = area.top() - plScroll;
        for (const SavedPlaylist &p : std::as_const(core->playlists)) {
            const QRectF r(area.left(), py, area.width(), kPlRow - 2);
            py += kPlRow;
            if (r.bottom() <= area.top() || r.top() >= area.bottom()) continue;
            v.append({r, Playlist, 0, p.name, QString(), p.id, QString::number(p.tracks.size()), plOpen && core->savedOpenId == p.id});
        }
    }

    qreal by = height() - 8 - 2 * kNavH - kNavGap;
    v.append({QRectF(x, by, w, kNavH), Action, 7, QStringLiteral("Now playing"), QStringLiteral("lyrics"), QString(), QString(), nowPlayingOpen});
    by += kNavH + kNavGap;
    v.append({QRectF(x, by, w, kNavH), Action, 8, QStringLiteral("Settings"), QStringLiteral("gear"), QString(), QString(), false});
    return v;
}

void SideBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    QColor panel = c.surfaceVariant;
    panel.setAlphaF(0.62);
    p.setPen(Qt::NoPen);
    p.setBrush(panel);
    p.drawRoundedRect(QRectF(rect()), 22, 22);

    // brand
    const QRectF logo(compact ? (width() - 34) / 2.0 : 16, 14, 34, 34);
    p.setBrush(c.primary);
    p.drawRoundedRect(logo, 11, 11);
    Icons::paint(p, logo.adjusted(8, 8, -8, -8), QStringLiteral("music"), c.onPrimary);
    if (!compact) {
        p.setPen(c.onSurface);
        p.setFont(fnt(16, QFont::Bold));
        p.drawText(QRectF(logo.right() + 10, logo.top(), width() - logo.right() - 18, logo.height()), Qt::AlignVCenter, QStringLiteral("YT Music"));
    }

    const auto its = items();
    const QRectF area = playlistArea();
    const QFont fLabel = fnt(13, QFont::Medium), fActive = fnt(13, QFont::DemiBold), fBadge = fnt(11, QFont::Medium);
    for (int i = 0; i < its.size(); ++i) {
        const Item &it = its[i];
        const bool hov = hover == i;
        if (it.kind == Caption) {
            QFont f = fnt(10, QFont::Bold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
            p.setFont(f);
            p.setPen(c.onSurfaceVariant);
            p.drawText(it.r, Qt::AlignVCenter | Qt::AlignLeft, it.label);
            continue;
        }
        if (it.kind == NewPlaylist) {
            drawIconButton(p, it.r, it.icon, hov);
            continue;
        }
        p.save();
        if (it.kind == Playlist) p.setClipRect(area);
        p.setPen(Qt::NoPen);
        if (it.active) {
            QColor a = c.primary;
            a.setAlphaF(0.15);
            p.setBrush(a);
            p.drawRoundedRect(it.r, 12, 12);
        } else if (hov) {
            p.setBrush(c.outline);
            p.drawRoundedRect(it.r, 12, 12);
        }
        const QColor fg = it.active ? c.primary : c.onSurface;
        const qreal cy = it.r.center().y();
        if (it.kind == Playlist) {
            const qreal bw = QFontMetrics(fBadge).horizontalAdvance(it.badge) + 6;
            const QRectF tr(it.r.left() + 12, it.r.top(), it.r.width() - 24 - bw, it.r.height());
            p.setFont(it.active ? fActive : fnt(13));
            p.setPen(it.active ? c.primary : (hov ? c.onSurface : c.onSurfaceVariant));
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(it.label, Qt::ElideRight, int(tr.width())));
            p.setFont(fBadge);
            p.setPen(c.onSurfaceVariant);
            p.drawText(QRectF(it.r.right() - 10 - bw, it.r.top(), bw, it.r.height()), Qt::AlignVCenter | Qt::AlignRight, it.badge);
        } else {
            const QRectF ir = compact ? QRectF(it.r.center().x() - 10, cy - 10, 20, 20) : QRectF(it.r.left() + 13, cy - 10, 20, 20);
            Icons::paint(p, ir, it.icon, fg, it.active && it.icon == QLatin1String("heart"));
            if (!compact) {
                p.setFont(it.active ? fActive : fLabel);
                p.setPen(fg);
                p.drawText(QRectF(it.r.left() + 45, it.r.top(), it.r.width() - 45 - 40, it.r.height()), Qt::AlignVCenter | Qt::AlignLeft, it.label);
                if (!it.badge.isEmpty()) {
                    p.setFont(fBadge);
                    p.setPen(it.active ? c.primary : c.onSurfaceVariant);
                    p.drawText(QRectF(it.r.right() - 46, it.r.top(), 34, it.r.height()), Qt::AlignVCenter | Qt::AlignRight, it.badge);
                }
            }
        }
        p.restore();
    }
}

void SideBar::mouseMoveEvent(QMouseEvent *e) {
    const auto its = items();
    const QRectF area = playlistArea();
    int h = -1;
    for (int i = 0; i < its.size(); ++i) {
        if (its[i].kind == Caption || !its[i].r.contains(e->position())) continue;
        if (its[i].kind == Playlist && !area.contains(e->position())) continue;
        h = i;
    }
    if (h != hover) {
        hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(h >= 0 && (compact || its[h].kind == NewPlaylist) ? its[h].label : QString());
        update();
    }
}

void SideBar::leaveEvent(QEvent *) {
    hover = -1;
    update();
}

void SideBar::wheelEvent(QWheelEvent *e) {
    if (compact) return;
    const int maxScroll = qMax(0, playlistContentH() - int(playlistArea().height()));
    const int next = qBound(0, plScroll - e->angleDelta().y() / 2, maxScroll);
    if (next != plScroll) {
        plScroll = next;
        hover = -1;
        update();
    }
    e->accept();
}

void SideBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const QRectF area = playlistArea();
    for (const Item &it : items()) {
        if (it.kind == Caption || !it.r.contains(e->position())) continue;
        if (it.kind == Playlist && !area.contains(e->position())) continue;
        switch (it.kind) {
        case Nav: emit tabChanged(Tab(it.id)); break;
        case Playlist: emit playlistRequested(it.pid); break;
        default: emit actionTriggered(it.id); break;
        }
        return;
    }
    emit dragRequested();
}

// -------------------------------------------------------------------- TabsBar
TabsBar::TabsBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setFixedHeight(40);
    setMouseTracking(true);
}

void TabsBar::setTab(Tab t) {
    tab = t;
    update();
}

QVector<TabsBar::Item> TabsBar::items() const {
    QVector<Item> v;
    const qreal py = (height() - 30) / 2.0;
    const bool detail = tab == Tab::Results && core->detailOpen;
    const bool savedOpen = tab == Tab::Saved && !core->savedOpenId.isEmpty();
    const bool hasTracks = !core->list(tab).isEmpty();

    // right side, laid out from the right edge: hide button, then actions (last added = leftmost)
    QVector<Item> r;
    auto addR = [&](int id, const QString &label, const QString &icon) { r.append({QRectF(), id, label, icon, false}); };
    addR(9, QString(), QStringLiteral("x"));
    const bool canSel = tab == Tab::Queue || tab == Tab::Liked || tab == Tab::Saved;
    const bool savedList = tab == Tab::Saved && !savedOpen;
    const bool anything = savedList ? !core->playlists.isEmpty() : hasTracks;
    if (selecting) {                                    // select mode: Cancel / Select all / Delete
        addR(54, QStringLiteral("Delete"), QStringLiteral("trash"));
        addR(53, QStringLiteral("Select all"), QStringLiteral("check"));
        addR(52, QStringLiteral("Cancel"), QStringLiteral("x"));
    } else if (tab == Tab::Home) {
        addR(40, QStringLiteral("Refresh"), QStringLiteral("refresh"));
    } else if (tab == Tab::Queue) {
        if (!core->queue.isEmpty()) {
            addR(13, QStringLiteral("Clear"), QStringLiteral("trash"));
            addR(60, QStringLiteral("Download"), QStringLiteral("download"));
            addR(55, QStringLiteral("Remove duplicates"), QStringLiteral("x"));
            addR(50, QStringLiteral("Select"), QStringLiteral("check"));
            addR(18, QStringLiteral("Save as playlist"), QStringLiteral("bookmark"));
        }
    } else if (savedList) {
        addR(17, QStringLiteral("New playlist"), QStringLiteral("plus"));
        if (anything) { addR(51, QStringLiteral("Delete all"), QStringLiteral("trash")); addR(50, QStringLiteral("Select"), QStringLiteral("check")); }
    } else if (hasTracks) {
        if (savedOpen) { addR(16, QStringLiteral("Delete"), QStringLiteral("trash")); addR(15, QStringLiteral("Rename"), {}); }
        if (detail) addR(14, QStringLiteral("Save playlist"), QStringLiteral("bookmark"));
        if (tab == Tab::Liked) addR(19, QStringLiteral("Save as playlist"), QStringLiteral("bookmark"));
        if (canSel) { addR(51, QStringLiteral("Delete all"), QStringLiteral("trash")); addR(50, QStringLiteral("Select"), QStringLiteral("check")); }
        addR(60, QStringLiteral("Download"), QStringLiteral("download"));
        addR(12, QStringLiteral("Queue all"), QStringLiteral("plus"));
        addR(11, QStringLiteral("Shuffle"), QStringLiteral("shuffle"));
        addR(10, QStringLiteral("Play all"), QStringLiteral("play"));
    }

    // left side: page title, or "back" + the opened playlist's name; Search also gets its Songs / Playlists switch
    QString title;
    if (selecting) title = QStringLiteral("%1 selected").arg(selCount);
    else if (detail) title = core->detail.title;
    else if (savedOpen) title = core->savedOpen() ? core->savedOpen()->name : QString();
    else {
        switch (tab) {
        case Tab::Home: title = QStringLiteral("For you"); break;
        case Tab::Results: title = QStringLiteral("Search"); break;
        case Tab::Queue: title = QStringLiteral("Queue"); break;
        case Tab::Liked: title = QStringLiteral("Liked songs"); break;
        case Tab::Saved: title = QStringLiteral("Playlists"); break;
        }
    }
    const bool back = !selecting && (detail || savedOpen);
    const bool modes = !selecting && tab == Tab::Results && !detail;
    const qreal titleW = QFontMetrics(fnt(20, QFont::Bold)).horizontalAdvance(title) + 4;
    const int wSongs = pillWidth(QStringLiteral("Songs"), {}), wPl = pillWidth(QStringLiteral("Playlists"), {});
    const qreal modesW = modes ? wSongs + 8 + wPl + 14 : 0;
    const qreal leftNeed = (back ? 38 : 0) + qMin<qreal>(titleW, 200) + modesW;

    // not enough room beside the title: the action pills shrink to icon-only
    qreal need = 0;
    for (const Item &it : std::as_const(r)) need += (it.id == 9 ? 30 : pillWidth(it.label, it.icon)) + 8;
    const bool compact = leftNeed + need + 16 > width();
    qreal rx = width();
    for (Item &it : r) {
        if (compact && it.id != 9 && !it.icon.isEmpty()) it.label.clear();
        if (it.id == 9 || (compact && it.label.isEmpty() && !it.icon.isEmpty())) {
            it.r = QRectF(rx - 30, py, 30, 30);
            rx -= 38;
        } else {
            const int w = pillWidth(it.label, it.icon);
            it.r = QRectF(rx - w, py, w, 30);
            rx -= w + 8;
        }
        v.append(it);
    }

    qreal x = 0;
    if (back) {
        v.append({QRectF(x, py, 30, 30), 30, QString(), QStringLiteral("back"), false});
        x += 38;
    }
    const qreal avail = rx - x - 8 - modesW;
    if (avail > 30) {
        const qreal w = qMin(titleW, avail);
        v.append({QRectF(x, 0, w, height()), 31, title, QString(), false});
        x += w + 14;
    }
    if (modes) {
        const bool songs = core->searchMode == Core::SearchMode::Songs;
        v.append({QRectF(x, py, wSongs, 30), 20, QStringLiteral("Songs"), QString(), songs});
        x += wSongs + 8;
        v.append({QRectF(x, py, wPl, 30), 21, QStringLiteral("Playlists"), QString(), !songs});
    }
    return v;
}

void TabsBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto its = items();
    for (int i = 0; i < its.size(); ++i) {
        const Item &it = its[i];
        if (it.id == 9) drawIconButton(p, it.r, QStringLiteral("x"), hover == i);
        else if (it.id == 30) drawIconButton(p, it.r, QStringLiteral("back"), hover == i);
        else if (it.id == 31) {
            const QFont f = fnt(20, QFont::Bold);
            p.setFont(f);
            p.setPen(P().onSurface);
            p.drawText(it.r, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(f).elidedText(it.label, Qt::ElideRight, int(it.r.width())));
        } else drawPill(p, it.r, it.label, it.icon, it.accent, hover == i);
    }
}

void TabsBar::mouseMoveEvent(QMouseEvent *e) {
    const auto its = items();
    int h = -1;
    for (int i = 0; i < its.size(); ++i)
        if (its[i].id != 31 && its[i].r.contains(e->position())) h = i;
    if (h != hover) {
        hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}
void TabsBar::leaveEvent(QEvent *) { hover = -1; update(); }

void TabsBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    for (const Item &it : items()) {
        if (it.id == 31 || !it.r.contains(e->position())) continue;
        if (it.id == 9) emit hideRequested();
        else emit actionTriggered(it.id);
        return;
    }
    emit dragRequested();
}

// ----------------------------------------------------------------- MainWindow
MainWindow::MainWindow(Core *c, Mpris *mpris) : core(c) {
    setWindowTitle(QStringLiteral("YT Music"));
    setObjectName(QStringLiteral("ytmusic"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("ytmusic")));
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(780, 540);
    resize(1100, 700);

    search = new SearchBox(this);
    side = new SideBar(core, this);
    tabs = new TabsBar(core, this);
    tracks = new TrackList(core, this);
    lyricsView = new LyricsView(core, this);
    bar = new PlayerBar(core, this);
    toast = new Toast(this);

    connect(search, &SearchBox::submitted, this, &MainWindow::doSearch);
    connect(tracks, &TrackList::searchRequested, this, [this](const QString &q) {
        search->edit->setText(q);
        doSearch(q);
    });
    connect(tabs, &TabsBar::hideRequested, this, &QWidget::hide);
    connect(side, &SideBar::tabChanged, this, [this](Tab t) {
        if (lyricsView->isVisible()) lyricsView->closeView();
        if (t == Tab::Saved && !core->savedOpenId.isEmpty()) core->closeSaved();   // "Playlists" always shows the list
        setTab(t);
        if (t == Tab::Results && !core->hasResults() && !core->detailOpen) {
            search->edit->setFocus();
            search->edit->selectAll();
        }
    });
    connect(side, &SideBar::playlistRequested, this, [this](const QString &id) {
        if (lyricsView->isVisible()) lyricsView->closeView();
        setTab(Tab::Saved);
        core->openSaved(id);
    });
    connect(side, &SideBar::actionTriggered, this, &MainWindow::onAction);
    connect(lyricsView, &LyricsView::closed, this, [this] {
        side->nowPlayingOpen = false;
        side->update();
    });
    connect(bar, &PlayerBar::expandRequested, this, &MainWindow::toggleNowPlaying);
    connect(bar, &PlayerBar::queueRequested, this, [this] {
        if (lyricsView->isVisible()) lyricsView->closeView();
        setTab(Tab::Queue);
    });
    connect(tabs, &TabsBar::actionTriggered, this, &MainWindow::onAction);
    connect(tracks, &TrackList::addToPlaylistRequested, this, &MainWindow::addToPlaylistMenu);
    connect(tracks, &TrackList::trackMenuRequested, this, &MainWindow::showTrackMenu);
    connect(tracks, &TrackList::deletePlaylistRequested, this, [this](const QString &id) {
        for (const SavedPlaylist &p : std::as_const(core->playlists))
            if (p.id == id && QMessageBox::question(this, QStringLiteral("Delete playlist"), QStringLiteral("Delete “%1”?").arg(p.name)) == QMessageBox::Yes) {
                core->deletePlaylist(id);
                return;
            }
    });
    connect(core, &Core::message, this, &MainWindow::showToast);
    connect(tracks, &TrackList::selectionChanged, this, [this] {
        tabs->selecting = tracks->selecting;
        tabs->selCount = tracks->selected.size();
        tabs->update();
    });
    connect(&Theme::I(), &Theme::changed, this, &MainWindow::applyPalette);
    applyPalette();
    auto drag = [this] { if (windowHandle()) windowHandle()->startSystemMove(); };
    connect(tabs, &TabsBar::dragRequested, this, drag);
    connect(side, &SideBar::dragRequested, this, drag);

    auto repaintAll = [this] {
        side->update();
        tabs->update();
        tracks->refresh();
        bar->update();
        refreshTray();
    };
    connect(core, &Core::trackChanged, this, repaintAll);
    connect(core, &Core::playbackChanged, this, repaintAll);
    connect(core, &Core::listsChanged, this, repaintAll);
    connect(core, &Core::resultsChanged, this, repaintAll);
    connect(core, &Core::searchStateChanged, this, repaintAll);
    connect(core, &Core::feedChanged, this, repaintAll);
    connect(core, &Core::settingsChanged, this, repaintAll);
    connect(core, &Core::volumeChanged, bar, qOverload<>(&QWidget::update));
    connect(core, &Core::positionChanged, bar, qOverload<>(&QWidget::update));
    connect(&Theme::I(), &Theme::changed, this, [this] { update(); });

    connect(mpris, &Mpris::raiseRequested, this, &MainWindow::showWindow);
    connect(mpris, &Mpris::toggleRequested, this, &MainWindow::toggleVisible);
    connect(mpris, &Mpris::quitRequested, qApp, &QCoreApplication::quit);

    // Desktop notification on track change while the window is hidden.
    connect(core, &Core::trackStarted, this, [this](const Track &t) {
        if (isVisible()) return;
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                        QStringLiteral("/org/freedesktop/Notifications"),
                                                        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
        m << QStringLiteral("YT Music") << uint(0x59544d) << QStringLiteral("ytmusic") << t.title << t.artist << QStringList()
          << QVariantMap() << 4000;
        QDBusConnection::sessionBus().asyncCall(m);
    });

    qApp->installEventFilter(this);
    buildTray();
    setTab(Tab::Home);
    // Building the feed starts several yt-dlp processes: let the window appear first.
    QTimer::singleShot(700, core, &Core::refreshFeedIfStale);
    setFocus();
}

void MainWindow::setTab(Tab t) {
    side->setTab(t);
    tabs->setTab(t);
    tracks->setTab(t);
}

// The now-playing screen (artwork, what's next, lyrics) covers the page area.
void MainWindow::toggleNowPlaying() {
    lyricsView->toggle();
    side->nowPlayingOpen = lyricsView->isVisible();
    side->update();
}

void MainWindow::doSearch(const QString &q) {
    if (q.trimmed().isEmpty()) return;
    if (lyricsView->isVisible()) lyricsView->closeView();
    setTab(Tab::Results);
    core->search(q, false);
    setFocus();
}

void MainWindow::showWindow() {
    show();
    raise();
    activateWindow();
}
void MainWindow::toggleVisible() {
    if (isVisible()) hide();
    else showWindow();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (!core->closeToTray) { e->accept(); qApp->quit(); return; }
    e->ignore();
    hide(); // keep playing from the tray
}

void MainWindow::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    // Running from the tray: give back everything we can.
    Thumbs::I().clearMemory();
    malloc_trim(0);
    refreshTray();
}

QPixmap MainWindow::snapshot() {
    if (qEnvironmentVariableIsSet("YTMUSIC_SHOT_SCROLL")) tracks->verticalScrollBar()->setValue(qEnvironmentVariableIntValue("YTMUSIC_SHOT_SCROLL"));   // dev helper
    QPixmap pm(size());
    pm.fill(Qt::transparent);
    render(&pm);
    return pm;
}

void MainWindow::resizeEvent(QResizeEvent *) {
    // sidebar | search, page header, list        player bar across the bottom
    const int m = 16, gap = 14, barH = 92;
    side->setCompact(width() < 920);
    const int sw = side->isCompact() ? 60 : 214;
    const int barY = height() - m - barH;
    bar->setGeometry(m, barY, width() - 2 * m, barH);
    side->setGeometry(m, m, sw, barY - gap - m);
    const int cx = m + sw + gap, cw = width() - cx - m;
    search->setGeometry(cx, m, cw, 50);
    tabs->setGeometry(cx, m + 50 + 12, cw, 40);
    const int top = m + 50 + 12 + 40 + 10;
    tracks->setGeometry(cx, top, cw, barY - gap - top);
    lyricsView->setGeometry(cx, m, cw, barY - gap - m);
}

void MainWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    // near-black base (YT Music) with the Noctalia hue showing through, and a soft colour glow
    // pouring down from the top edge (Spotube / YT Music home)
    auto mix = [](const QColor &a, const QColor &b, qreal t) {
        return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                                a.blueF() * (1 - t) + b.blueF() * t, a.alphaF());
    };
    QColor bg = mix(c.surface, Qt::black, 0.42);
    bg.setAlphaF(0.96);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, 24, 24);
    QLinearGradient glow(0, 0, 0, qMin<qreal>(height() * 0.75, 460));
    QColor g0 = mix(c.secondary, c.primary, 0.25);
    g0.setAlphaF(0.34);
    QColor g1 = g0;
    g1.setAlphaF(0.0);
    glow.setColorAt(0, g0);
    glow.setColorAt(1, g1);
    p.setBrush(glow);
    p.drawRoundedRect(r, 24, 24);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c.outline, 1));
    p.drawRoundedRect(r, 24, 24);
}

// -------------------------------------------------- frameless move / resize
Qt::Edges MainWindow::edgesAt(const QPoint &pt) const {
    constexpr int b = 7;
    Qt::Edges e;
    if (pt.x() < b) e |= Qt::LeftEdge;
    if (pt.x() > width() - b) e |= Qt::RightEdge;
    if (pt.y() < b) e |= Qt::TopEdge;
    if (pt.y() > height() - b) e |= Qt::BottomEdge;
    return e;
}

void MainWindow::mouseMoveEvent(QMouseEvent *e) {
    const Qt::Edges ed = edgesAt(e->position().toPoint());
    if (ed == (Qt::LeftEdge | Qt::TopEdge) || ed == (Qt::RightEdge | Qt::BottomEdge)) setCursor(Qt::SizeFDiagCursor);
    else if (ed == (Qt::RightEdge | Qt::TopEdge) || ed == (Qt::LeftEdge | Qt::BottomEdge)) setCursor(Qt::SizeBDiagCursor);
    else if (ed & (Qt::LeftEdge | Qt::RightEdge)) setCursor(Qt::SizeHorCursor);
    else if (ed & (Qt::TopEdge | Qt::BottomEdge)) setCursor(Qt::SizeVerCursor);
    else setCursor(Qt::ArrowCursor);
}

void MainWindow::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !windowHandle()) return;
    const Qt::Edges ed = edgesAt(e->position().toPoint());
    if (ed) windowHandle()->startSystemResize(ed);
    else windowHandle()->startSystemMove();
}

// ------------------------------------------------------------------ keyboard
bool MainWindow::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::Polish) {
        // rounded menus: let the stylesheet's corners show through
        if (auto *m = qobject_cast<QMenu *>(obj)) m->setAttribute(Qt::WA_TranslucentBackground);
    }
    if (ev->type() == QEvent::MouseButtonPress && search->edit->hasFocus()) {
        // Clicking anywhere outside the search box gives focus back to the window.
        auto *w = qobject_cast<QWidget *>(obj);
        if (w && w->window() == this && w != search->edit && w != search) setFocus();
    }
    if (obj == search->edit && ev->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(ev);
        if (k->key() == Qt::Key_Escape) { setFocus(); return true; }
        if (k->key() == Qt::Key_Space && search->edit->text().isEmpty() && k->modifiers() == Qt::NoModifier) {
            core->togglePlay();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    switch (e->key()) {
    case Qt::Key_L:
        if (ctrl) { if (lyricsView->isVisible()) lyricsView->closeView(); search->edit->setFocus(); search->edit->selectAll(); }
        else if (auto *t = core->nowPlaying()) core->toggleLike(*t);
        break;
    case Qt::Key_Q:
        if (ctrl) qApp->quit();
        break;
    case Qt::Key_Slash:
        if (lyricsView->isVisible()) lyricsView->closeView();
        search->edit->setFocus();
        search->edit->selectAll();
        break;
    case Qt::Key_Space: core->togglePlay(); break;
    case Qt::Key_N: core->next(false); break;
    case Qt::Key_P: core->prev(); break;
    case Qt::Key_S: core->toggleShuffle(); break;
    case Qt::Key_R: core->cycleRepeat(); break;
    case Qt::Key_M: core->toggleMute(); break;
    case Qt::Key_1: setTab(Tab::Home); break;
    case Qt::Key_2: setTab(Tab::Results); break;
    case Qt::Key_3: setTab(Tab::Queue); break;
    case Qt::Key_4: setTab(Tab::Liked); break;
    case Qt::Key_5: setTab(Tab::Saved); break;
    case Qt::Key_BracketLeft: core->setSpeed(core->speed - 0.25); break;
    case Qt::Key_BracketRight: core->setSpeed(core->speed + 0.25); break;
    case Qt::Key_Backslash: core->setSpeed(1.0); break;
    case Qt::Key_Left: core->seekBy(-5); break;
    case Qt::Key_Right: core->seekBy(5); break;
    case Qt::Key_Up: core->setVolume(core->volume + 5); break;
    case Qt::Key_Down: core->setVolume(core->volume - 5); break;
    case Qt::Key_Y: toggleNowPlaying(); break;
    case Qt::Key_Escape: if (lyricsView->isVisible()) lyricsView->closeView(); else hide(); break;
    default: QWidget::keyPressEvent(e);
    }
}

// ----------------------------------------------------------------------- tray
void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    QIcon icon = QIcon::fromTheme(QStringLiteral("ytmusic"));
    if (icon.isNull()) icon = QIcon(QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor/scalable/apps/ytmusic.svg"));
    tray = new QSystemTrayIcon(icon, this);
    trayMenu = new QMenu;
    actTitle = trayMenu->addAction(QStringLiteral("Nothing playing"));
    actTitle->setEnabled(false);
    trayMenu->addSeparator();
    actPlay = trayMenu->addAction(QStringLiteral("Play"), core, &Core::togglePlay);
    trayMenu->addAction(QStringLiteral("Next"), core, [this] { core->next(false); });
    trayMenu->addAction(QStringLiteral("Previous"), core, &Core::prev);
    sleepMenu = trayMenu->addMenu(QStringLiteral("Sleep timer"));
    sleepMenu->addAction(QStringLiteral("15 minutes"), core, [this] { core->setSleep(15); });
    sleepMenu->addAction(QStringLiteral("30 minutes"), core, [this] { core->setSleep(30); });
    sleepMenu->addAction(QStringLiteral("1 hour"), core, [this] { core->setSleep(60); });
    actSleepOff = sleepMenu->addAction(QStringLiteral("Off"), core, [this] { core->setSleep(0); });
    speedMenu = trayMenu->addMenu(QStringLiteral("Playback speed"));
    for (double sp : {0.75, 1.0, 1.25, 1.5, 2.0})
        speedMenu->addAction(sp == 1.0 ? QStringLiteral("Normal") : QStringLiteral("%1×").arg(sp), core, [this, sp] { core->setSpeed(sp); });
    trayMenu->addAction(QStringLiteral("Settings…"), this, &MainWindow::openSettings);
    trayMenu->addSeparator();
    actToggle = trayMenu->addAction(QStringLiteral("Hide window"), this, &MainWindow::toggleVisible);
    trayMenu->addAction(QStringLiteral("Quit"), qApp, &QCoreApplication::quit);
    connect(trayMenu, &QMenu::aboutToShow, this, &MainWindow::refreshTray);
    tray->setContextMenu(trayMenu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) toggleVisible();
    });
    tray->show();
    refreshTray();
}

void MainWindow::refreshTray() {
    if (!tray) return;
    const Track *t = core->nowPlaying();
    const QString sl = core->sleepLabel();
    tray->setToolTip((t ? t->title : QStringLiteral("YT Music")) + (sl.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(sl)));
    actTitle->setText(t ? t->title : QStringLiteral("Nothing playing"));
    actPlay->setText((core->paused || core->idle) ? QStringLiteral("Play") : QStringLiteral("Pause"));
    actToggle->setText(isVisible() ? QStringLiteral("Hide window") : QStringLiteral("Show window"));
    actSleepOff->setEnabled(core->sleepMinutes > 0);
    sleepMenu->setTitle(sl.isEmpty() ? QStringLiteral("Sleep timer") : QStringLiteral("Sleep timer (%1)").arg(sl));
}


// -------------------------------------------------------- playlists & dialogs
void MainWindow::applyPalette() {
    const Palette &c = P();
    QPalette pal;
    pal.setColor(QPalette::Window, c.surfaceVariant);
    pal.setColor(QPalette::WindowText, c.onSurface);
    pal.setColor(QPalette::Base, c.surface);
    pal.setColor(QPalette::AlternateBase, c.surfaceVariant);
    pal.setColor(QPalette::Text, c.onSurface);
    pal.setColor(QPalette::Button, c.surfaceVariant);
    pal.setColor(QPalette::ButtonText, c.onSurface);
    pal.setColor(QPalette::Highlight, c.primary);
    pal.setColor(QPalette::HighlightedText, c.onPrimary);
    pal.setColor(QPalette::ToolTipBase, c.surfaceVariant);
    pal.setColor(QPalette::ToolTipText, c.onSurface);
    pal.setColor(QPalette::PlaceholderText, c.onSurfaceVariant);
    pal.setColor(QPalette::Disabled, QPalette::Text, c.onSurfaceVariant);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.onSurfaceVariant);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, c.onSurfaceVariant);
    qApp->setPalette(pal);
    qApp->setStyleSheet(UI::appStyleSheet());
}

void MainWindow::showToast(const QString &text) {
    toast->showText(text);
    toast->move((width() - toast->width()) / 2, bar->y() - 52);
}

QString MainWindow::askName(const QString &title, const QString &initial, bool *ok) {
    return QInputDialog::getText(this, title, QStringLiteral("Name:"), QLineEdit::Normal, initial, ok);
}

void MainWindow::showTrackMenu(const Track &t, int index, const QPoint &pos) {
    const Tab tab = tabs->tab;
    QMenu m(this);
    QAction *aNext = m.addAction(QStringLiteral("Play next"));
    QAction *aQueue = m.addAction(QStringLiteral("Add to queue"));
    QAction *aRadio = m.addAction(QStringLiteral("Start radio"));
    QAction *aPl = m.addAction(QStringLiteral("Add to playlist…"));
    QAction *aLike = m.addAction(core->isLiked(t.vid) ? QStringLiteral("Remove from liked") : QStringLiteral("Add to liked"));
    m.addSeparator();
    QAction *aArtist = t.artist.isEmpty() ? nullptr : m.addAction(QStringLiteral("More from %1").arg(t.artist));
    QAction *aCopy = m.addAction(QStringLiteral("Copy link"));
    m.addSeparator();
    Downloads *dl = core->downloads;
    QAction *aDl = nullptr, *aDlCancel = nullptr, *aDlShow = nullptr, *aDlDelete = nullptr;
    if (dl->isActive(t.vid)) aDlCancel = m.addAction(dl->isRunning(t.vid) ? QStringLiteral("Cancel download (%1%)").arg(qRound(dl->progress(t.vid) * 100))
                                                                          : QStringLiteral("Cancel download"));
    else if (!dl->pathFor(t.vid).isEmpty()) {
        aDlShow = m.addAction(QStringLiteral("Show in folder"));
        aDlDelete = m.addAction(QStringLiteral("Delete download"));
    } else aDl = m.addAction(QStringLiteral("Download to this PC"));
    QAction *aRemove = nullptr;
    if (index >= 0 && tab == Tab::Queue) aRemove = m.addAction(QStringLiteral("Remove from queue"));
    else if (index >= 0 && tab == Tab::Saved && !core->savedOpenId.isEmpty()) aRemove = m.addAction(QStringLiteral("Remove from this playlist"));

    QAction *r = m.exec(pos);
    if (!r) return;
    if (r == aNext) { core->playNextTrack(t); showToast(QStringLiteral("Playing next")); }
    else if (r == aQueue) { core->enqueueTrack(t); showToast(QStringLiteral("Added to the queue")); }
    else if (r == aRadio) core->startRadio(t);
    else if (r == aPl) addToPlaylistMenu(t);
    else if (r == aLike) core->toggleLike(t);
    else if (aArtist && r == aArtist) { search->edit->setText(t.artist); doSearch(t.artist); }
    else if (aDl && r == aDl) { dl->add({t}); showToast(QStringLiteral("Downloading “%1”…").arg(t.title)); }
    else if (aDlCancel && r == aDlCancel) { dl->cancel(t.vid); showToast(QStringLiteral("Download cancelled")); }
    else if (aDlShow && r == aDlShow) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dl->pathFor(t.vid)).absolutePath()));
    else if (aDlDelete && r == aDlDelete) showToast(dl->remove(t.vid) ? QStringLiteral("Download deleted") : QStringLiteral("Couldn't delete the file"));
    else if (r == aCopy) { QGuiApplication::clipboard()->setText(QStringLiteral("https://www.youtube.com/watch?v=") + t.vid); showToast(QStringLiteral("Link copied")); }
    else if (aRemove && r == aRemove) {
        if (tab == Tab::Queue) core->removeFromQueue(index);
        else core->removeFromSaved(index);
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(core, this);
    connect(&dlg, &SettingsDialog::toast, this, &MainWindow::showToast);
    dlg.exec();
}

// Two separate confirmations; the second spells out that it can't be undone.
bool MainWindow::confirmTwice(const QString &first, const QString &second) {
    if (QMessageBox::question(this, QStringLiteral("Delete all"), first) != QMessageBox::Yes) return false;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Are you sure?"), second, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(QStringLiteral("Delete everything"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == yes;
}

void MainWindow::deleteSelected() {
    const Tab t = tabs->tab;
    if (tracks->selected.isEmpty()) { showToast(QStringLiteral("Nothing selected")); return; }
    const int n = tracks->selected.size();
    const bool playlists = t == Tab::Saved && core->savedOpenId.isEmpty();
    const QString what = playlists ? (n == 1 ? QStringLiteral("playlist") : QStringLiteral("playlists"))
                                   : (n == 1 ? QStringLiteral("song") : QStringLiteral("songs"));
    const QString verb = playlists ? QStringLiteral("Delete") : QStringLiteral("Remove");
    if (QMessageBox::question(this, verb, QStringLiteral("%1 %2 %3?").arg(verb).arg(n).arg(what)) != QMessageBox::Yes) return;

    QVector<int> idx(tracks->selected.begin(), tracks->selected.end());
    switch (t) {
    case Tab::Queue: core->removeFromQueueMany(idx); break;
    case Tab::Liked: {
        QSet<QString> vids;
        for (int i : std::as_const(idx)) if (i >= 0 && i < core->liked.size()) vids.insert(core->liked[i].vid);
        core->unlike(vids);
        break;
    }
    case Tab::Saved:
        if (playlists) {
            QSet<QString> ids;
            for (int i : std::as_const(idx)) if (i >= 0 && i < core->playlists.size()) ids.insert(core->playlists[i].id);
            core->deletePlaylists(ids);
        } else {
            core->removeFromSavedMany(idx);
        }
        break;
    default: break;
    }
    tracks->stopSelect();
    showToast(QStringLiteral("Done"));
}

void MainWindow::deleteAllItems() {
    const Tab t = tabs->tab;
    const bool playlists = t == Tab::Saved && core->savedOpenId.isEmpty();
    if (t == Tab::Liked) {
        if (confirmTwice(QStringLiteral("Remove all liked songs?"), QStringLiteral("Your whole Liked list will be emptied. It can't be undone."))) {
            core->clearLikes();
            showToast(QStringLiteral("Liked songs cleared"));
        }
    } else if (playlists) {
        if (confirmTwice(QStringLiteral("Delete all %1 playlists?").arg(core->playlists.size()), QStringLiteral("Every playlist you saved or created will be deleted. It can't be undone."))) {
            core->clearAllPlaylists();
            showToast(QStringLiteral("All playlists deleted"));
        }
    } else if (t == Tab::Saved) {
        if (confirmTwice(QStringLiteral("Remove all songs from this playlist?"),
                         QStringLiteral("The playlist itself stays, but it will be empty. It can't be undone."))) {
            core->clearSaved(core->savedOpenId);
            showToast(QStringLiteral("Playlist emptied"));
        }
    }
}

void MainWindow::onAction(int id) {
    const Tab t = tabs->tab;
    bool ok = false;
    switch (id) {
    case 8: openSettings(); break;
    case 7: toggleNowPlaying(); break;
    case 10: core->playFrom(t, 0, false); break;
    case 11: core->playFrom(t, 0, true); break;
    case 12: core->enqueueAll(t); showToast(QStringLiteral("Added %1 songs to the queue").arg(core->list(t).size())); break;
    case 13:
        if (QMessageBox::question(this, QStringLiteral("Clear queue"), QStringLiteral("Clear the whole queue (%1 songs)?").arg(core->queue.size())) == QMessageBox::Yes)
            core->clearQueue();
        break;
    case 50: tracks->startSelect(); break;
    case 51: deleteAllItems(); break;
    case 55: { const int n = core->removeQueueDuplicates(); showToast(n ? QStringLiteral("Removed %1 duplicate%2").arg(n).arg(n == 1 ? "" : "s") : QStringLiteral("No duplicates in the queue")); break; }
    case 52: tracks->stopSelect(); break;
    case 53: tracks->selectAll(); break;
    case 54: deleteSelected(); break;
    case 14: core->saveRemotePlaylist(core->detail.url, core->detail.title); break;
    case 15:
        if (const SavedPlaylist *p = core->savedOpen()) {
            const QString n = askName(QStringLiteral("Rename playlist"), p->name, &ok);
            if (ok) core->renamePlaylist(p->id, n);
        }
        break;
    case 16:
        if (const SavedPlaylist *p = core->savedOpen()) {
            if (QMessageBox::question(this, QStringLiteral("Delete playlist"), QStringLiteral("Delete “%1”?").arg(p->name)) == QMessageBox::Yes)
                core->deletePlaylist(p->id);
        }
        break;
    case 17: {
        const QString n = askName(QStringLiteral("New playlist"), {}, &ok);
        if (ok) core->createPlaylist(n);
        break;
    }
    case 18:
    case 19: {
        const QString n = askName(id == 18 ? QStringLiteral("Save queue as playlist") : QStringLiteral("Save liked songs as playlist"),
                                  id == 18 ? QStringLiteral("My queue") : QStringLiteral("Liked songs"), &ok);
        if (ok) {
            core->createPlaylist(n, id == 18 ? core->queue : core->liked);
            showToast(QStringLiteral("Playlist saved"));
        }
        break;
    }
    case 40: core->refreshFeed(); break;
    case 60: {
        const int n = core->downloads->add(core->list(t));
        showToast(n ? QStringLiteral("Downloading %1 song%2 to %3").arg(n).arg(n == 1 ? "" : "s").arg(QDir(core->downloads->dir).dirName())
                    : QStringLiteral("Already downloaded"));
        break;
    }
    case 20: core->setSearchMode(Core::SearchMode::Songs); break;
    case 21: core->setSearchMode(Core::SearchMode::Playlists); break;
    case 30:
        if (t == Tab::Results) core->closeDetail();
        else core->closeSaved();
        tracks->verticalScrollBar()->setValue(0);
        break;
    default: break;
    }
}

void MainWindow::addToPlaylistMenu(const Track &t) {
    QMenu m(this);
    QAction *fresh = m.addAction(QStringLiteral("New playlist…"));
    if (!core->playlists.isEmpty()) m.addSeparator();
    QHash<QAction *, QString> ids;
    for (const SavedPlaylist &p : std::as_const(core->playlists)) ids.insert(m.addAction(p.name), p.id);
    QAction *a = m.exec(QCursor::pos());
    if (!a) return;
    if (a == fresh) {
        bool ok = false;
        const QString n = askName(QStringLiteral("New playlist"), {}, &ok);
        if (!ok) return;
        core->createPlaylist(n, {t});
        showToast(QStringLiteral("Added to “%1”").arg(n.trimmed().isEmpty() ? QStringLiteral("New playlist") : n.trimmed()));
        return;
    }
    const QString id = ids.value(a);
    QString name;
    for (const SavedPlaylist &p : std::as_const(core->playlists)) if (p.id == id) name = p.name;
    const int n = core->addToPlaylist(id, {t});
    showToast(n > 0 ? QStringLiteral("Added to “%1”").arg(name) : QStringLiteral("Already in “%1”").arg(name));
}
