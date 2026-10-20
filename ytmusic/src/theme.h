#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

struct Palette {
    QColor primary{"#fff59b"}, onPrimary{"#0e0e43"}, secondary{"#a9aefe"}, tertiary{"#9BFECE"};
    QColor surface{"#070722"}, surfaceVariant{"#11112d"}, onSurface{"#f3edf7"};
    QColor onSurfaceVariant{"#7c80b4"}, outline{"#21215F"}, error{"#fd4663"};
};

// Live Noctalia palette, rendered by a Noctalia user template to
// $XDG_STATE_HOME/ytmusic/theme.json. Falls back to Noctalia's stock colors.
class Theme : public QObject {
    Q_OBJECT
public:
    static Theme &I();
    const Palette &p() const { return pal; }
signals:
    void changed();
private:
    Theme();
    void reload();
    Palette pal;
    QFileSystemWatcher watcher;
    QTimer debounce;
    QString file;
};
