#include "core.h"
#include "mainwindow.h"
#include "mpris.h"
#include "theme.h"
#include <QApplication>
#include <QKeyEvent>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QSocketNotifier>
#include <QStyleFactory>
#include <QTimer>
#include <csignal>
#include <malloc.h>
#include <unistd.h>

static int sigPipe[2];
static void onSignal(int) { const char c = 1; (void)!::write(sigPipe[1], &c, 1); }

static const QString kService = QStringLiteral("org.mpris.MediaPlayer2.ytmusic");

// Talk to an already-running instance (single-instance + CLI control).
static int remote(const QString &cmd) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto call = [&](const QString &iface, const QString &method) {
        QDBusMessage m = QDBusMessage::createMethodCall(kService, QStringLiteral("/org/mpris/MediaPlayer2"), iface, method);
        return bus.call(m, QDBus::Block, 2000).type() != QDBusMessage::ErrorMessage;
    };
    const QString player = QStringLiteral("org.mpris.MediaPlayer2.Player");
    const QString control = QStringLiteral("org.ytmusic.Control");
    bool ok = false;
    if (cmd.isEmpty() || cmd == QLatin1String("show") || cmd == QLatin1String("open")) ok = call(control, QStringLiteral("Show"));
    else if (cmd == QLatin1String("toggle")) ok = call(control, QStringLiteral("Toggle"));
    else if (cmd == QLatin1String("playpause")) ok = call(player, QStringLiteral("PlayPause"));
    else if (cmd == QLatin1String("next")) ok = call(player, QStringLiteral("Next"));
    else if (cmd == QLatin1String("prev") || cmd == QLatin1String("previous")) ok = call(player, QStringLiteral("Previous"));
    else if (cmd == QLatin1String("quit")) ok = call(QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Quit"));
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    // Keep the footprint small: one malloc arena, return freed memory promptly, and
    // don't load whichever Qt platform-theme / style plugins the desktop configures.
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ytmusic"));
    app.setDesktopFileName(QStringLiteral("ytmusic"));
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = app.arguments().mid(1);
    const QString cmd = args.value(0);
    QString shotPath;
    if (cmd == QLatin1String("--shot")) shotPath = args.value(1);

    if (shotPath.isEmpty()) {
        const bool running = QDBusConnection::sessionBus().interface()->isServiceRegistered(kService);
        if (running) return remote(cmd);
        if (!cmd.isEmpty() && cmd != QLatin1String("show") && cmd != QLatin1String("open")) return 1; // nothing to control
    }

    Theme::I();
    Core core;
    Mpris mpris(&core);
    if (shotPath.isEmpty() && !mpris.registerOnBus()) return remote(QString());

    MainWindow win(&core, &mpris);

    if (!shotPath.isEmpty()) { // dev helper: --shot out.png [tab 0-3] [query] [pl] [open]   renders the window and exits
        win.show();
        const int tabIdx = args.value(2).toInt();
        win.setTab(tabIdx == 1 ? Tab::Queue : tabIdx == 2 ? Tab::Liked : tabIdx == 3 ? Tab::Saved : tabIdx == 4 ? Tab::Home : Tab::Results);
        if (qEnvironmentVariableIsSet("YTMUSIC_SHOT_LYRICS")) { // dev: open the lyrics view
            QKeyEvent ke(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier);
            QCoreApplication::sendEvent(&win, &ke);
        }
        const bool pl = args.value(4) == QLatin1String("pl");
        if (pl) core.setSearchMode(Core::SearchMode::Playlists);
        if (args.size() > 3 && !args.value(3).isEmpty() && args.value(3) != QLatin1String("-")) core.search(args.value(3));
        if (args.value(5) == QLatin1String("open"))
            QTimer::singleShot(5000, &app, [&] { if (!core.playlistResults.isEmpty()) core.openPlaylist(core.playlistResults.first()); });
        if (args.value(5) == QLatin1String("savetest")) { // save the first playlist result, add a track to it, show the Saved tab
            QObject::connect(&core, &Core::message, &app, [](const QString &m) { fprintf(stderr, "MESSAGE: %s\n", qPrintable(m)); });
            QTimer::singleShot(5000, &app, [&] {
                if (core.playlistResults.isEmpty()) return;
                core.saveRemotePlaylist(core.playlistResults.first().url, core.playlistResults.first().name);
            });
            QTimer::singleShot(8000, &app, [&] {
                const QString id = core.createPlaylist(QStringLiteral("Scratch"));
                if (!core.playlists.isEmpty() && !core.playlists.at(1).tracks.isEmpty())
                    fprintf(stderr, "added %d\n", core.addToPlaylist(id, {core.playlists.at(1).tracks.first()}));
                fprintf(stderr, "again %d (expect 0)\n", core.addToPlaylist(id, {core.playlists.at(1).tracks.first()}));
                win.setTab(Tab::Saved);
            });
        }
        if (args.value(5) == QLatin1String("opensaved"))
            QTimer::singleShot(500, &app, [&] { if (!core.playlists.isEmpty()) core.openSaved(core.playlists.first().id); });
        QTimer::singleShot(qEnvironmentVariableIsSet("YTMUSIC_SHOT_LYRICS") ? 7000 : tabIdx == 4 ? 20000 : (args.size() > 3 && args.value(3) != QLatin1String("-") ? 11000 : 1500), &app, [&] {
            win.snapshot().save(shotPath);
            app.quit();
        });
        return app.exec();
    }

    if (!core.startHidden) win.show();
    // SIGTERM / SIGINT / SIGHUP quit cleanly (stops mpv, saves state)
    if (::pipe(sigPipe) == 0) {
        for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
            struct sigaction sa{};
            sa.sa_handler = onSignal;
            sigemptyset(&sa.sa_mask);
            sigaction(sig, &sa, nullptr);
        }
        auto *sn = new QSocketNotifier(sigPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &core, &Core::shutdown);
    return app.exec();
}
