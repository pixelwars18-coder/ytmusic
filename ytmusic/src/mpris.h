#pragma once
#include "core.h"
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

class Mpris;

class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ none)
    Q_PROPERTY(QStringList SupportedMimeTypes READ none)
public:
    explicit MprisRoot(Mpris *m);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("YT Music"); }
    QString desktopEntry() const { return QStringLiteral("ytmusic"); }
    QStringList none() const { return {}; }
public slots:
    void Raise();
    void Quit();
private:
    Mpris *m;
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoNext)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPlay)
    Q_PROPERTY(bool CanSeek READ canPlay)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    explicit MprisPlayer(Mpris *m);
    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &s);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canGoNext() const;
    bool canPlay() const;
    bool canControl() const { return true; }
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &track, qlonglong posUs);
    void OpenUri(const QString &) {}
signals:
    void Seeked(qlonglong position);
private:
    Mpris *m;
};

class MprisControl : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ytmusic.Control")
public:
    explicit MprisControl(Mpris *m);
public slots:
    void Show();
    void Toggle();
private:
    Mpris *m;
};

// Owns org.mpris.MediaPlayer2.ytmusic and keeps clients (media widgets, playerctl,
// media keys) in sync with Core. Also exposes org.ytmusic.Control for the CLI.
class Mpris : public QObject {
    Q_OBJECT
public:
    explicit Mpris(Core *core);
    bool registerOnBus();
    Core *core;
signals:
    void raiseRequested();
    void toggleRequested();
    void quitRequested();
private:
    void sync();
    QVariantMap snapshot() const;
    QVariantMap last;
    friend class MprisPlayer;
    friend class MprisRoot;
    friend class MprisControl;
};
