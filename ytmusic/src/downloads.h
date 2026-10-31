#pragma once
#include "types.h"
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QVector>

// Saves songs to a folder on this PC with yt-dlp (a few at a time, in the background).
// Files are named "Artist - Title [videoId].ext", so the folder is re-indexed by scanning
// for the "[videoId]" tag: anything already there is known as downloaded, and the player
// plays the local file instead of streaming it.
class Downloads : public QObject {
    Q_OBJECT
public:
    enum Format { Original = 0, Mp3 = 1 };

    explicit Downloads(QObject *parent = nullptr);
    ~Downloads() override;

    // settings (persisted by Core in state.json)
    QString dir;                 // where files go
    int format = Original;       // Original: M4A/Opus as YouTube serves it (no re-encode). Mp3: converted with ffmpeg.
    bool embedTags = true;       // embed title/artist + cover art (needs ffmpeg)
    static QString defaultDir();
    static bool haveFfmpeg();

    void setDir(const QString &d);
    void rescan();

    // state
    bool isDownloaded(const QString &vid) const;
    QString pathFor(const QString &vid) const;   // empty if not downloaded (or the file is gone)
    bool isActive(const QString &vid) const { return running.contains(vid) || queuedSet.contains(vid); }
    bool isRunning(const QString &vid) const { return running.contains(vid); }
    double progress(const QString &vid) const { return prog.value(vid, 0.0); }   // 0..1, running only
    int pendingCount() const { return int(running.size() + pending.size()); }
    int downloadedCount() const { return int(files.size()); }

    // actions
    int add(const QVector<Track> &tracks);      // returns how many were newly queued
    void cancel(const QString &vid);
    void cancelAll();
    bool remove(const QString &vid);            // deletes the file

signals:
    void changed();                              // something to repaint (progress, done, queued)
    void message(const QString &text);

private:
    void pump();
    void start(const Track &t);
    void finished(const QString &vid, int code, const QString &err);

    QHash<QString, QString> files;               // vid -> absolute path
    QVector<Track> pending;
    QSet<QString> queuedSet;
    QHash<QString, QProcess *> running;
    QHash<QString, Track> runningTrack;
    QHash<QString, double> prog;
    QHash<QString, QString> finalPath;           // printed by yt-dlp after moving the file into place
    int batchDone = 0, batchFailed = 0;
    static constexpr int kParallel = 2;
};
