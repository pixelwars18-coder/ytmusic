#include "theme.h"
#include "paths.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

Theme &Theme::I() {
    static Theme t;
    return t;
}

static Palette officialPalette() {
    Palette p;   // the colours of music.youtube.com's dark theme
    p.accent = QColor("#ff0033");
    p.primary = QColor("#ffffff");
    p.onPrimary = QColor("#030303");
    p.secondary = QColor("#aaaaaa");
    p.tertiary = QColor("#ffffff");
    p.surface = QColor("#030303");
    p.surfaceVariant = QColor("#212121");
    p.onSurface = QColor("#ffffff");
    p.onSurfaceVariant = QColor("#aaaaaa");
    p.outline = QColor("#303030");
    p.error = QColor("#ff4e45");
    return p;
}

Theme::Theme() {
    officialLook = qgetenv("YTMUSIC_LOOK") != "noctalia";
    if (officialLook) pal = officialPalette();
    file = Paths::stateDir() + QStringLiteral("/theme.json");
    QDir().mkpath(Paths::stateDir());
    debounce.setSingleShot(true);
    debounce.setInterval(120);
    connect(&debounce, &QTimer::timeout, this, &Theme::reload);
    // Watch the directory too: templates replace the file atomically.
    watcher.addPath(Paths::stateDir());
    connect(&watcher, &QFileSystemWatcher::directoryChanged, &debounce, qOverload<>(&QTimer::start));
    connect(&watcher, &QFileSystemWatcher::fileChanged, &debounce, qOverload<>(&QTimer::start));
    reload();
}

void Theme::reload() {
    if (officialLook) { emit changed(); return; }   // fixed palette; theme.json only drives the Noctalia look
    if (!watcher.files().contains(file) && QFile::exists(file)) watcher.addPath(file);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return;
    Palette n = pal;
    auto get = [&](const char *key, QColor &c) {
        const QColor v(o.value(QLatin1String(key)).toString());
        if (v.isValid()) c = v;
    };
    get("mPrimary", n.primary);
    get("mOnPrimary", n.onPrimary);
    get("mSecondary", n.secondary);
    get("mTertiary", n.tertiary);
    get("mSurface", n.surface);
    get("mSurfaceVariant", n.surfaceVariant);
    get("mOnSurface", n.onSurface);
    get("mOnSurfaceVariant", n.onSurfaceVariant);
    get("mOutline", n.outline);
    get("mError", n.error);
    n.accent = n.primary;
    pal = n;
    emit changed();
}
