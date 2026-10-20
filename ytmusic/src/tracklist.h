#pragma once
#include "core.h"
#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>

// Virtualised, custom-painted list: tracks (results / queue / liked / a playlist) or playlist
// cards (playlist search results, your saved playlists), plus recent-search chips, a spinner
// and a "Load more" footer.
class TrackList : public QAbstractScrollArea {
    Q_OBJECT
public:
    TrackList(Core *core, QWidget *parent = nullptr);
    void setTab(Tab t);
    void refresh();
    // select mode (Queue, Liked, Saved): tick rows, then delete them
    bool selecting = false;
    QSet<int> selected;                  // indices into the current list (tracks, or playlists on the Saved list)
    bool canSelect() const;
    void startSelect();
    void stopSelect();
    void selectAll();
signals:
    void selectionChanged();
    void searchRequested(const QString &q);
    void addToPlaylistRequested(const Track &t);
    void trackMenuRequested(const Track &t, int index, const QPoint &globalPos);   // right-click on a song
    void deletePlaylistRequested(const QString &id);
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    enum Kind { None, Row, Like, AddPl, Act1, Act2, LoadMore, Chip, ClearHist, HeaderPlay, HeaderShuffle, HeroRefresh };
    struct Hit { Kind kind = None; int idx = -1; };
    static constexpr int kRowH = 68, kRowInner = 64;

    struct ChipItem { QRectF r; QString text; };
    QVector<ChipItem> chipLayout(qreal width, qreal &endY) const;
    bool cardMode() const;
    bool remoteCards() const { return tab == Tab::Results; }
    int count() const;
    bool showChips() const;
    bool loadingSomething() const;
    int footerH() const;
    int contentH() const;
    Hit hitTest(const QPoint &vp) const;
    void rowRects(int i, QRectF &row, QRectF &like, QRectF &addpl, QRectF &a1, QRectF &a2, QRectF &dur) const;
    void rowRectsAt(qreal y, QRectF &row, QRectF &like, QRectF &addpl, QRectF &a1, QRectF &a2, QRectF &dur) const;

    // "For you" page: hero + shelves (header + up to 8 rows each), laid out in a flat list
    struct FeedItem { int kind; int section; int track; int y; int h; };   // kind: 0 hero, 1 header, 2 row
    bool feedMode() const { return tab == Tab::Home; }
    void rebuildFeed();
    Hit hitFeed(const QPoint &vp) const;
    void paintFeed(QPainter &p);
    void pressFeed(const Hit &h);
    QVector<FeedItem> feedItems;
    QVector<FeedSection> feedSecs;
    int feedHeight = 0;
    void updateScroll();
    QString act1Icon() const;
    QString act2Icon() const;
    QString emptyMessage() const;

    Core *core;
    Tab tab = Tab::Results;
    Hit hover;
    QTimer spin;
    QElapsedTimer clock;
};
