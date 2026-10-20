#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

struct Track {
    QString vid, title, artist;
    double duration = 0;

    bool operator==(const Track &o) const { return vid == o.vid; }
    QJsonObject toJson() const {
        return {{"vid", vid}, {"title", title}, {"artist", artist}, {"duration", duration}};
    }
    static Track fromJson(const QJsonObject &o) {
        return {o.value("vid").toString(), o.value("title").toString(),
                o.value("artist").toString(), o.value("duration").toDouble()};
    }
};

enum class Tab { Results, Queue, Liked, Saved, Home };

// One shelf of the "For you" page.
struct FeedSection {
    QString id, title, subtitle;
    QVector<Track> tracks;
};

// A YouTube playlist as found by playlist search.
struct PlaylistRef {
    QString url, name, uploader, thumbUrl;
};

// A playlist kept in the library.
struct SavedPlaylist {
    QString id, name, source; // source: YouTube URL it was saved from, or empty
    QVector<Track> tracks;
};
