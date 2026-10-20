#pragma once
#include <QDir>
#include <QString>
#include <QtGlobal>

namespace Paths {
inline QString home() { return QDir::homePath(); }
inline QString stateDir() {
    const QByteArray x = qgetenv("XDG_STATE_HOME");
    return (x.isEmpty() ? home() + QStringLiteral("/.local/state") : QString::fromLocal8Bit(x)) + QStringLiteral("/ytmusic");
}
inline QString cacheDir() {
    const QByteArray x = qgetenv("XDG_CACHE_HOME");
    return (x.isEmpty() ? home() + QStringLiteral("/.cache") : QString::fromLocal8Bit(x)) + QStringLiteral("/ytmusic");
}
inline QString runtimeDir() {
    const QByteArray x = qgetenv("XDG_RUNTIME_DIR");
    return x.isEmpty() ? QStringLiteral("/tmp") : QString::fromLocal8Bit(x);
}
} // namespace Paths
