#pragma once
#include "core.h"
#include "mpris.h"
#include <QLineEdit>
#include <QSystemTrayIcon>
#include <QWidget>

class TrackList;
class Toast;
class PlayerBar;
class LyricsView;
class QMenu;
class QAction;

// Rounded "pill" search box wrapping a transparent QLineEdit.
class SearchBox : public QWidget {
    Q_OBJECT
public:
    explicit SearchBox(QWidget *parent = nullptr);
    QLineEdit *edit;
signals:
    void submitted(const QString &text);
    void dragRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    QRectF clearRect() const, goRect() const;
    int hover = 0; // 1 clear, 2 go
};

// Left rail: brand, the five pages, your playlists, and Now playing / Settings at the bottom.
// Shrinks to an icon-only strip when the window is narrow.
class SideBar : public QWidget {
    Q_OBJECT
public:
    SideBar(Core *core, QWidget *parent = nullptr);
    Tab tab = Tab::Home;
    bool nowPlayingOpen = false;
    void setTab(Tab t);
    void setCompact(bool on);
    bool isCompact() const { return compact; }
signals:
    void tabChanged(Tab t);
    void playlistRequested(const QString &id);
    void actionTriggered(int id);   // same ids as the header actions: 7 now playing, 8 settings, 17 new playlist
    void dragRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    enum Kind { Nav, Playlist, Action, Caption, NewPlaylist };
    struct Item { QRectF r; Kind kind; int id; QString label, icon, pid, badge; bool active; };
    QVector<Item> items() const;
    QRectF playlistArea() const;
    int playlistContentH() const;
    Core *core;
    bool compact = false;
    int hover = -1;
    int plScroll = 0;
};

// Page title (or "back" + playlist name) on the left, context actions + hide button on the right.
class TabsBar : public QWidget {
    Q_OBJECT
public:
    TabsBar(Core *core, QWidget *parent = nullptr);
    Tab tab = Tab::Results;
    bool selecting = false;   // select mode of the list below: the actions turn into Cancel / Select all / Delete
    int selCount = 0;
    void setTab(Tab t);
signals:
    void tabChanged(Tab t);
    void dragRequested();
    void hideRequested();
    void actionTriggered(int id);
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    struct Item { QRectF r; int id; QString label, icon; bool accent; };
    QVector<Item> items() const;
    Core *core;
    int hover = -1;
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(Core *core, Mpris *mpris);
    void toggleVisible();
    void showWindow();
    void setTab(Tab t);
    void toggleNowPlaying();
    QPixmap snapshot();
    void showToast(const QString &text);
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;
private:
    void buildTray();
    void refreshTray();
    void doSearch(const QString &q);
    void onAction(int id);
    void openSettings();
    void showTrackMenu(const Track &t, int index, const QPoint &pos);
    void deleteSelected();
    void deleteAllItems();
    bool confirmTwice(const QString &first, const QString &second);
    void addToPlaylistMenu(const Track &t);
    void applyPalette();
    QString askName(const QString &title, const QString &initial, bool *ok);
    Qt::Edges edgesAt(const QPoint &pt) const;

    Core *core;
    SearchBox *search;
    SideBar *side;
    TabsBar *tabs;
    TrackList *tracks;
    LyricsView *lyricsView;
    PlayerBar *bar;
    Toast *toast = nullptr;
    QSystemTrayIcon *tray = nullptr;
    QMenu *trayMenu = nullptr;
    QAction *actTitle = nullptr, *actPlay = nullptr, *actToggle = nullptr, *actSleepOff = nullptr;
    QMenu *sleepMenu = nullptr, *speedMenu = nullptr;
};
