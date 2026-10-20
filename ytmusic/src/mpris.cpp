#include "mpris.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QRegularExpression>

static const char *kPath = "/org/mpris/MediaPlayer2";
static const char *kPlayer = "org.mpris.MediaPlayer2.Player";

MprisRoot::MprisRoot(Mpris *mm) : QDBusAbstractAdaptor(mm), m(mm) {}
void MprisRoot::Raise() { emit m->raiseRequested(); }
void MprisRoot::Quit() { emit m->quitRequested(); }

MprisControl::MprisControl(Mpris *mm) : QDBusAbstractAdaptor(mm), m(mm) {}
void MprisControl::Show() { emit m->raiseRequested(); }
void MprisControl::Toggle() { emit m->toggleRequested(); }

MprisPlayer::MprisPlayer(Mpris *mm) : QDBusAbstractAdaptor(mm), m(mm) {}
QString MprisPlayer::playbackStatus() const {
    const Core *c = m->core;
    if (!c->nowPlaying() || c->idle) return QStringLiteral("Stopped");
    return c->paused ? QStringLiteral("Paused") : QStringLiteral("Playing");
}
QString MprisPlayer::loopStatus() const {
    switch (m->core->repeat) {
    case Core::RepeatAll: return QStringLiteral("Playlist");
    case Core::RepeatOne: return QStringLiteral("Track");
    default: return QStringLiteral("None");
    }
}
void MprisPlayer::setLoopStatus(const QString &s) {
    m->core->setRepeat(s == QLatin1String("Track") ? Core::RepeatOne : s == QLatin1String("Playlist") ? Core::RepeatAll : Core::RepeatOff);
}
bool MprisPlayer::shuffle() const { return m->core->shuffle; }
void MprisPlayer::setShuffle(bool on) { m->core->setShuffle(on); }
double MprisPlayer::volume() const { return m->core->muted ? 0.0 : m->core->volume / 100.0; }
void MprisPlayer::setVolume(double v) { m->core->setVolume(v * 100.0); }
qlonglong MprisPlayer::position() const { return qlonglong(m->core->position * 1e6); }
bool MprisPlayer::canGoNext() const { return !m->core->queue.isEmpty(); }
bool MprisPlayer::canPlay() const { return m->core->nowPlaying() != nullptr; }

QVariantMap MprisPlayer::metadata() const {
    QVariantMap md;
    const Track *t = m->core->nowPlaying();
    if (!t) {
        md["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")));
        return md;
    }
    QString id = t->vid;
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    md["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/ytmusic/track/") + id));
    md["mpris:length"] = qlonglong(m->core->duration * 1e6);
    md["mpris:artUrl"] = QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(t->vid);
    md["xesam:title"] = t->title;
    md["xesam:artist"] = t->artist.isEmpty() ? QStringList() : QStringList{t->artist};
    md["xesam:url"] = QStringLiteral("https://www.youtube.com/watch?v=") + t->vid;
    return md;
}

void MprisPlayer::Next() { m->core->next(false); }
void MprisPlayer::Previous() { m->core->prev(); }
void MprisPlayer::Pause() { m->core->pause(); }
void MprisPlayer::PlayPause() { m->core->togglePlay(); }
void MprisPlayer::Stop() { m->core->stop(); }
void MprisPlayer::Play() { m->core->play(); }
void MprisPlayer::Seek(qlonglong us) { m->core->seekBy(us / 1e6); }
void MprisPlayer::SetPosition(const QDBusObjectPath &, qlonglong us) { m->core->seekTo(us / 1e6); }

// ---------------------------------------------------------------------------
Mpris::Mpris(Core *c) : QObject(c), core(c) {
    new MprisRoot(this);
    auto *player = new MprisPlayer(this);
    new MprisControl(this);
    connect(core, &Core::trackChanged, this, &Mpris::sync);
    connect(core, &Core::playbackChanged, this, &Mpris::sync);
    connect(core, &Core::volumeChanged, this, &Mpris::sync);
    connect(core, &Core::settingsChanged, this, &Mpris::sync);
    connect(core, &Core::listsChanged, this, &Mpris::sync);
    connect(core, &Core::seeked, player, [player](double s) { emit player->Seeked(qlonglong(s * 1e6)); });
}

bool Mpris::registerOnBus() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.ytmusic"))) return false;
    if (!bus.registerObject(QString::fromLatin1(kPath), this, QDBusConnection::ExportAdaptors)) return false;
    last = snapshot();
    return true;
}

QVariantMap Mpris::snapshot() const {
    QVariantMap s;
    if (auto *p = findChild<MprisPlayer *>()) {
        s["PlaybackStatus"] = p->playbackStatus();
        s["LoopStatus"] = p->loopStatus();
        s["Shuffle"] = p->shuffle();
        s["Metadata"] = p->metadata();
        s["Volume"] = p->volume();
        s["CanGoNext"] = p->canGoNext();
        s["CanGoPrevious"] = p->canGoNext();
        s["CanPlay"] = p->canPlay();
        s["CanPause"] = p->canPlay();
        s["CanSeek"] = p->canPlay();
    }
    return s;
}

void Mpris::sync() {
    const QVariantMap now = snapshot();
    QVariantMap changed;
    for (auto it = now.cbegin(); it != now.cend(); ++it) {
        if (last.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
    }
    // Duration arrives after the track starts; QVariantMap compare covers Metadata.
    last = now;
    if (changed.isEmpty()) return;
    QDBusMessage sig = QDBusMessage::createSignal(QString::fromLatin1(kPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    sig << QString::fromLatin1(kPlayer) << changed << QStringList();
    QDBusConnection::sessionBus().send(sig);
}
