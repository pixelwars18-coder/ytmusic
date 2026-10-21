#pragma once
#include "types.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QSocketNotifier>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVector>

// Playback engine + state: search (yt-dlp), playback (mpv over JSON IPC),
// queue / liked / history persistence, sleep timer, next-track preloading.
class Core : public QObject {
    Q_OBJECT
public:
    enum Repeat { RepeatOff, RepeatAll, RepeatOne };
    enum class SearchMode { Songs, Playlists };

    struct Detail { QString title, url, status; QVector<Track> tracks; bool loading = false; };

    explicit Core(QObject *parent = nullptr);
    ~Core() override;

    // ---- data (read-only for the UI) ----
    QVector<Track> results, queue, liked;
    QVector<PlaylistRef> playlistResults;      // playlist search results
    SearchMode searchMode = SearchMode::Songs;
    bool detailOpen = false;                   // a YouTube playlist opened from playlist search
    Detail detail;
    QVector<SavedPlaylist> playlists;          // your own playlists
    QVector<Track> recent;                     // recently played, newest first
    int plays = 0;                             // tracks started so far (drives feed refreshes)
    QVector<FeedSection> feed;                 // "For you" shelves built from plays and likes
    bool feedBuilding = false;
    qint64 feedBuiltAt = 0;
    int feedPlaysAtBuild = 0;
    QString savedOpenId;                       // opened saved playlist (Saved tab)
    QStringList history;
    int current = -1;
    bool searching = false, searchIsUrl = false;
    QString status, searchQuery;
    int searchLimit = 25, pageSize = 25;   // pageSize: results per search / "Load more"
    bool startHidden = false;              // start in the tray
    bool closeToTray = true;               // the window's close button hides it (false: quits)
    int audioQuality = 0;                  // 0 best, 1 balanced (<=128 kbps), 2 data saver (<=64 kbps)
    int memoryMode = 1;                    // when idle mpv is shut down: 0 soon, 1 normal, 2 never
    double position = 0, duration = 0, volume = 80, speed = 1.0;
    bool paused = true, idle = true, buffering = false, muted = false, shuffle = false;
    int repeat = RepeatOff;
    int sleepMinutes = 0;
    QDateTime sleepEnds;

    const Track *nowPlaying() const { return current >= 0 && current < queue.size() ? &queue[current] : nullptr; }
    QString nowVid() const { return nowPlaying() ? nowPlaying()->vid : QString(); }
    bool isLiked(const QString &vid) const { return likedSet.contains(vid); }
    bool hasResults() const { return searchMode == SearchMode::Songs ? !results.isEmpty() : !playlistResults.isEmpty(); }
    bool canLoadMore() const {
        const int n = searchMode == SearchMode::Songs ? results.size() : playlistResults.size();
        return !searchIsUrl && !searching && n > 0 && n >= searchLimit;
    }
    /** Home page shelves: "Jump back in" (your recent plays) followed by the recommendations. */
    QVector<FeedSection> homeSections() const {
        QVector<FeedSection> out;
        if (!recent.isEmpty()) out.append({QStringLiteral("recent"), QStringLiteral("Jump back in"), QStringLiteral("Pick up where you left off"), recent.mid(0, 8)});
        out += feed;
        return out;
    }
    const SavedPlaylist *savedOpen() const {
        for (const SavedPlaylist &p : playlists) if (p.id == savedOpenId) return &p;
        return nullptr;
    }
    const QVector<Track> &list(Tab t) const {
        switch (t) {
        case Tab::Queue: return queue;
        case Tab::Liked: return liked;
        case Tab::Saved: { const SavedPlaylist *p = savedOpen(); return p ? p->tracks : kEmpty; }
        case Tab::Home: return kEmpty;
        default: return detailOpen ? detail.tracks : results;
        }
    }
    QString sleepLabel() const;
    bool mpvConnected() const { return fd >= 0; }

public slots:
    void search(const QString &q, bool more = false);
    void playIndex(int i);
    void playFrom(Tab t, int i, bool doShuffle = false);
    void playTracks(const QVector<Track> &tracks, int start = 0, bool doShuffle = false);
    void enqueue(Tab t, int i);
    void playNext(Tab t, int i);
    void enqueueAll(Tab t);
    void clearQueue();
    void removeFromQueue(int i);
    void next(bool automatic = false);
    void prev();
    void togglePlay();
    void play();
    void pause();
    void stop();
    void toggleShuffle();
    void setShuffle(bool on);
    void cycleRepeat();
    void setRepeat(int r);
    void setVolume(double v);
    void setSpeed(double s);               // playback speed (not saved: every session starts at 1x)
    void startRadio(const Track &t);       // plays the track, then YouTube's Mix for it
    void toggleMute();
    void seekTo(double sec);
    void seekBy(double sec);
    void toggleLike(const Track &t);
    void clearHistory();
    // "For you"
    void refreshFeed();
    void refreshFeedIfStale();
    void enqueueTrack(const Track &t);
    void playNextTrack(const Track &t);
    // playlists
    void setSearchMode(SearchMode m);
    void openPlaylist(const PlaylistRef &r);
    void closeDetail();
    void saveRemotePlaylist(const QString &url, const QString &name);
    QString createPlaylist(const QString &name, const QVector<Track> &tracks = {}, const QString &source = {});
    int addToPlaylist(const QString &id, const QVector<Track> &tracks);
    void removeFromSaved(int index);
    // bulk deletes (select mode / "Delete all")
    void unlike(const QSet<QString> &vids);
    void clearLikes();
    void removeFromQueueMany(QVector<int> indices);
    void removeFromSavedMany(QVector<int> indices);
    void deletePlaylists(const QSet<QString> &ids);
    void clearAllPlaylists();
    void clearSaved(const QString &id);
    void renamePlaylist(const QString &id, const QString &name);
    void deletePlaylist(const QString &id);
    void openSaved(const QString &id);
    void closeSaved();
    void setSleep(int minutes);
    int removeQueueDuplicates();           // keeps the first copy (and the playing one); returns how many went
    QString exportBackup(const QString &path) const;   // error text, empty on success
    QString importBackup(const QString &path, bool *ok); // merges; returns a summary or the error
    void clearRecentPlays();               // "Jump back in" + the For you shelves
    void setPageSize(int n);
    void setStartHidden(bool on);
    void setCloseToTray(bool on);
    void setAudioQuality(int q);
    void setMemoryMode(int m);
    void shutdown();

signals:
    void trackChanged();     // now-playing track changed
    void trackStarted(const Track &t);
    void playbackChanged();  // paused / idle / buffering / duration
    void positionChanged();
    void listsChanged();     // queue or liked changed
    void resultsChanged();
    void searchStateChanged();
    void volumeChanged();
    void settingsChanged();  // shuffle / repeat / history / sleep
    void seeked(double sec);
    void message(const QString &text); // short status / toast text
    void feedChanged();

private:
    void load();
    void save();
    void scheduleSave() { saveTimer.start(); }
    void startMpv();
    void ensureMpv();
    void stopMpv();
    void checkMpvIdle();
    void mpv(const QJsonArray &cmd);
    void mpvLine(const QByteArray &line);
    void onEndFile(const QString &reason);
    void rebuildLikedSet();
    int pickNext(bool automatic);
    void preloadNext();
    void resolveUrl(const QString &vid);
    void loadTrack(const Track &t);
    void loadResolved(const QString &vid);
    void trackFailed();
    void checkSleep();

    QProcess *mpvProc = nullptr;
    int fd = -1; // mpv JSON IPC (plain AF_UNIX socket: avoids linking QtNetwork)
    QSocketNotifier *notifier = nullptr;
    void tryConnect();
    void closeSocket();
    QByteArray sockBuf;
    QTimer connectTimer, saveTimer, sleepTimer, mpvIdleTimer;
    QElapsedTimer inactiveSince;
    QJsonArray pendingLoad; // loadfile queued while mpv is (re)starting
    QString resumeVid;      // after an idle shutdown of mpv: where to pick up again
    double resumeAt = 0;
    QString sockPath;
    QProcess *ytProc = nullptr;
    QProcess *detailProc = nullptr;
    inline static const QVector<Track> kEmpty{};
    QElapsedTimer posThrottle;
    int errorStreak = 0;
    QSet<QString> likedSet;

    // preloading
    struct Resolved { QString url; QDateTime when; };
    QHash<QString, Resolved> urlCache;
    QHash<QString, QProcess *> resolving;
    int nextPick = -1;       // pre-chosen next index (needed for shuffle)
    bool usedDirect = false; // current track was loaded from a resolved URL
    bool retried = false;
    QString waitingVid;      // track whose URL we are resolving right now
    bool loaded = false;
};
