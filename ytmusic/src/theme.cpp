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

static Palette applePalette() {
    Palette p;   // Apple Music, dark appearance
    p.accent = QColor("#fa233b");
    p.primary = QColor("#fa233b");
    p.onPrimary = QColor("#ffffff");
    p.secondary = QColor("#98989f");
    p.tertiary = QColor("#ffffff");
    p.surface = QColor("#1e1e1e");
    p.surfaceVariant = QColor("#2b2b2d");
    p.onSurface = QColor("#ffffff");
    p.onSurfaceVariant = QColor("#98989f");
    p.outline = QColor("#3a3a3c");
    p.error = QColor("#ff453a");
    return p;
}

Theme::Theme() {
    appleLook = qgetenv("YTMUSIC_LOOK") != "noctalia";
    if (appleLook) pal = applePalette();
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
    if (appleLook) { emit changed(); return; }
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
