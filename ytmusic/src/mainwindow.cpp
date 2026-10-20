#include "mainwindow.h"
#include "playerbar.h"
#include "tracklist.h"
#include "paths.h"
#include "ui_common.h"
#include <QCloseEvent>
#include <QDir>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
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

// -------------------------------------------------------------------- TabsBar
TabsBar::TabsBar(Core *c, QWidget *parent) : QWidget(parent), core(c) {
    setFixedHeight(32);
    setMouseTracking(true);
}

void TabsBar::setTab(Tab t) {
    tab = t;
    update();
}

QVector<TabsBar::Item> TabsBar::items() const {
    QVector<Item> v;
    qreal x = 0;
    auto add = [&](int id, const QString &label, const QString &icon, bool accent) {
        const int w = pillWidth(label, icon);
        v.append({QRectF(x, 1, w, 30), id, label, icon, accent});
        x += w + 8;
    };
    add(4, QStringLiteral("For you"), {}, tab == Tab::Home);
    add(0, QStringLiteral("Results"), {}, tab == Tab::Results);
    add(1, core->queue.isEmpty() ? QStringLiteral("Queue") : QStringLiteral("Queue  %1").arg(core->queue.size()), {}, tab == Tab::Queue);
    add(2, core->liked.isEmpty() ? QStringLiteral("Liked") : QStringLiteral("Liked  %1").arg(core->liked.size()), {}, tab == Tab::Liked);
    add(3, core->playlists.isEmpty() ? QStringLiteral("Saved") : QStringLiteral("Saved  %1").arg(core->playlists.size()), {}, tab == Tab::Saved);

    const bool detail = tab == Tab::Results && core->detailOpen;
    const bool savedOpen = tab == Tab::Saved && !core->savedOpenId.isEmpty();
    const bool hasTracks = !core->list(tab).isEmpty();

    // right side, laid out from the right edge: hide button, then actions (last added = leftmost)
    QVector<Item> r;
    auto addR = [&](int id, const QString &label, const QString &icon) { r.append({QRectF(), id, label, icon, false}); };
    addR(9, QString(), QStringLiteral("x"));
    addR(8, QString(), QStringLiteral("gear"));
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
        addR(12, QStringLiteral("Queue all"), QStringLiteral("plus"));
        addR(11, QStringLiteral("Shuffle"), QStringLiteral("shuffle"));
        addR(10, QStringLiteral("Play all"), QStringLiteral("play"));
    }
    // not enough room beside the tabs: the action pills shrink to icon-only
    qreal need = 0;
    for (const Item &it : std::as_const(r)) need += (it.id == 9 || it.id == 8 ? 30 : pillWidth(it.label, it.icon)) + 8;
    const bool compact = x + need + 16 > width();
    qreal rx = width();
    for (Item &it : r) {
        if (compact && it.id != 9 && it.id != 8 && !it.icon.isEmpty()) it.label.clear();
        if (it.id == 9 || it.id == 8 || (compact && it.label.isEmpty() && !it.icon.isEmpty())) {
            it.r = QRectF(rx - 30, 1, 30, 30);
            rx -= 38;
        } else {
            const int w = pillWidth(it.label, it.icon);
            it.r = QRectF(rx - w, 1, w, 30);
            rx -= w + 8;
        }
        v.append(it);
    }

    // left-of-actions context: search-mode pills, or "back" + playlist title
    x += 4;
    if (selecting) {
        const QString text = QStringLiteral("%1 selected").arg(selCount);
        const qreal avail = rx - x - 8;
        if (avail > 40) v.append({QRectF(x, 1, avail, 30), 31, text, QString(), false});
    } else if (tab == Tab::Results && !detail) {
        const bool songs = core->searchMode == Core::SearchMode::Songs;
        add(20, QStringLiteral("Songs"), {}, songs);
        add(21, QStringLiteral("Playlists"), {}, !songs);
    } else if (detail || savedOpen) {
        v.append({QRectF(x, 1, 30, 30), 30, QString(), QStringLiteral("back"), false});
        x += 36;
        const QString title = detail ? core->detail.title : (core->savedOpen() ? core->savedOpen()->name : QString());
        const qreal avail = rx - x - 8;
        if (avail > 40) {
            const QFontMetrics fm(fnt(13, QFont::DemiBold));
            v.append({QRectF(x, 1, avail, 30), 31, fm.elidedText(title, Qt::ElideRight, int(avail)), QString(), false});
        }
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
            p.setFont(fnt(13, QFont::DemiBold));
            p.setPen(P().onSurface);
            p.drawText(it.r, Qt::AlignVCenter | Qt::AlignLeft, it.label);
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
        switch (it.id) {
        case 4: emit tabChanged(Tab::Home); break;
        case 0: emit tabChanged(Tab::Results); break;
        case 1: emit tabChanged(Tab::Queue); break;
        case 2: emit tabChanged(Tab::Liked); break;
        case 3: emit tabChanged(Tab::Saved); break;
        case 9: emit hideRequested(); break;
        case 8: emit actionTriggered(8); break;
        default: emit actionTriggered(it.id); break;
        }
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
    setMinimumSize(760, 520);
    resize(980, 640);

    search = new SearchBox(this);
    tabs = new TabsBar(core, this);
    tracks = new TrackList(core, this);
    bar = new PlayerBar(core, this);
    toast = new Toast(this);

    connect(search, &SearchBox::submitted, this, &MainWindow::doSearch);
    connect(tracks, &TrackList::searchRequested, this, [this](const QString &q) {
        search->edit->setText(q);
        doSearch(q);
    });
    connect(tabs, &TabsBar::tabChanged, this, &MainWindow::setTab);
    connect(tabs, &TabsBar::hideRequested, this, &QWidget::hide);
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

    auto repaintAll = [this] {
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
    tabs->setTab(Tab::Home);
    tracks->setTab(Tab::Home);
    core->refreshFeedIfStale();
    setFocus();
}

void MainWindow::setTab(Tab t) {
    tabs->setTab(t);
    tracks->setTab(t);
}

void MainWindow::doSearch(const QString &q) {
    if (q.trimmed().isEmpty()) return;
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
    const int m = 20, w = width() - 2 * m;
    search->setGeometry(m, m, w, 50);
    tabs->setGeometry(m, m + 50 + 14, w, 32);
    const int top = m + 50 + 14 + 32 + 14;
    const int barY = height() - m - 92;
    bar->setGeometry(m, barY, w, 92);
    tracks->setGeometry(m, top, w, barY - 16 - top);
}

void MainWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = P();
    QColor bg = c.surface;
    bg.setAlphaF(0.94);
    p.setBrush(bg);
    p.setPen(QPen(c.outline, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 24, 24);
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
        if (ctrl) { search->edit->setFocus(); search->edit->selectAll(); }
        else if (auto *t = core->nowPlaying()) core->toggleLike(*t);
        break;
    case Qt::Key_Q:
        if (ctrl) qApp->quit();
        break;
    case Qt::Key_Slash: search->edit->setFocus(); search->edit->selectAll(); break;
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
    case Qt::Key_Escape: hide(); break;
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
    else if (r == aCopy) { QGuiApplication::clipboard()->setText(QStringLiteral("https://www.youtube.com/watch?v=") + t.vid); showToast(QStringLiteral("Link copied")); }
    else if (aRemove && r == aRemove) {
        if (tab == Tab::Queue) core->removeFromQueue(index);
        else core->removeFromSaved(index);
    }
}

static qint64 dirBytes(const QString &path) {
    qint64 n = 0;
    const QFileInfoList fl = QDir(path).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : fl) n += fi.size();
    return n;
}

void MainWindow::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Settings"));
    auto *lay = new QFormLayout(&dlg);

    auto *page = new QComboBox;
    for (int n : {25, 50, 100}) page->addItem(QStringLiteral("%1 songs").arg(n), n);
    page->setCurrentIndex(qMax(0, page->findData(core->pageSize)));
    lay->addRow(QStringLiteral("Results per search"), page);

    auto *quality = new QComboBox;
    quality->addItem(QStringLiteral("Best available"), 0);
    quality->addItem(QStringLiteral("Balanced (up to 128 kbps)"), 1);
    quality->addItem(QStringLiteral("Data saver (up to 64 kbps)"), 2);
    quality->setCurrentIndex(core->audioQuality);
    lay->addRow(QStringLiteral("Audio quality"), quality);

    auto *memory = new QComboBox;
    memory->addItem(QStringLiteral("Free quickly (30 s idle)"), 0);
    memory->addItem(QStringLiteral("Normal (2 min idle)"), 1);
    memory->addItem(QStringLiteral("Never (instant resume)"), 2);
    memory->setCurrentIndex(core->memoryMode);
    lay->addRow(QStringLiteral("Release player memory"), memory);

    auto *hidden = new QCheckBox(QStringLiteral("Start hidden in the tray"));
    hidden->setChecked(core->startHidden);
    lay->addRow(hidden);
    auto *tray = new QCheckBox(QStringLiteral("Close button hides to the tray (off: quits)"));
    tray->setChecked(core->closeToTray);
    lay->addRow(tray);

    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    lay->addRow(sep);

    auto *btnSearch = new QPushButton(QStringLiteral("Clear recent searches"));
    auto *btnPlays = new QPushButton(QStringLiteral("Clear play history && For you"));
    auto *btnThumbs = new QPushButton;
    const QString thumbDir = Paths::cacheDir() + QStringLiteral("/thumbs");
    auto thumbLabel = [thumbDir] { return QStringLiteral("Clear picture cache (%1 MB)").arg(dirBytes(thumbDir) / 1048576.0, 0, 'f', 1); };
    btnThumbs->setText(thumbLabel());
    lay->addRow(btnSearch);
    lay->addRow(btnPlays);
    lay->addRow(btnThumbs);
    connect(btnSearch, &QPushButton::clicked, &dlg, [this, btnSearch] { core->clearHistory(); btnSearch->setEnabled(false); });
    connect(btnPlays, &QPushButton::clicked, &dlg, [this, btnPlays] {
        if (QMessageBox::question(this, QStringLiteral("Clear play history"), QStringLiteral("Clear your recently played list and the For you shelves?")) != QMessageBox::Yes) return;
        core->clearRecentPlays();
        btnPlays->setEnabled(false);
    });
    connect(btnThumbs, &QPushButton::clicked, &dlg, [thumbDir, btnThumbs, thumbLabel] {
        const QFileInfoList fl = QDir(thumbDir).entryInfoList(QDir::Files);
        for (const QFileInfo &fi : fl) QFile::remove(fi.filePath());
        btnThumbs->setText(thumbLabel());
    });

    auto *about = new QLabel(QStringLiteral("Likes, playlists and history are stored in\n%1").arg(Paths::stateDir()));
    about->setEnabled(false);
    lay->addRow(about);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    lay->addRow(bb);

    dlg.exec();
    core->setPageSize(page->currentData().toInt());
    core->setStartHidden(hidden->isChecked());
    core->setCloseToTray(tray->isChecked());
    core->setAudioQuality(quality->currentData().toInt());
    core->setMemoryMode(memory->currentData().toInt());
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
    case 10: core->playFrom(t, 0, false); break;
    case 11: core->playFrom(t, 0, true); break;
    case 12: core->enqueueAll(t); showToast(QStringLiteral("Added %1 songs to the queue").arg(core->list(t).size())); break;
    case 13:
        if (QMessageBox::question(this, QStringLiteral("Clear queue"), QStringLiteral("Clear the whole queue (%1 songs)?").arg(core->queue.size())) == QMessageBox::Yes)
            core->clearQueue();
        break;
    case 50: tracks->startSelect(); break;
    case 51: deleteAllItems(); break;
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
