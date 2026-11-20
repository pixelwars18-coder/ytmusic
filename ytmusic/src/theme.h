#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Default look: VoiceStudio's dark "Studio" theme (near-black canvas, #111 cards, hairline borders, blue accent).
struct Palette {
    QColor primary{"#4f7df3"}, onPrimary{"#ffffff"}, secondary{"#8fb4ff"}, tertiary{"#72cebd"};
    QColor surface{"#0a0a0a"}, surfaceVariant{"#111111"}, onSurface{"#f5f5f5"};
    QColor onSurfaceVariant{"#8d8d93"}, outline{"#1f1f22"}, error{"#f0506e"};
};
inline QColor withAlpha(QColor c, int a) { c.setAlpha(a); return c; }

// Optionally the live Noctalia palette, rendered by a Noctalia user template to
// $XDG_STATE_HOME/ytmusic/theme.json. Falls back to Noctalia's stock colors.
class Theme : public QObject {
    Q_OBJECT
public:
    static Theme &I();
    const Palette &p() const { return pal; }
    void setFollowNoctalia(bool on);   // off (default): the fixed Studio colours; on: the live Noctalia palette
    bool followNoctalia() const { return follow; }
signals:
    void changed();
private:
    Theme();
    void reload();
    Palette pal;
    bool follow = false;
    QFileSystemWatcher watcher;
    QTimer debounce;
    QString file;
};
