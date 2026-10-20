#pragma once
#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QString>

namespace Icons {
// Tabler-style 24x24 stroke icons, drawn as vector paths (parsed once).
void paint(QPainter &p, const QRectF &box, const QString &name, const QColor &color, bool fill = false);
} // namespace Icons
