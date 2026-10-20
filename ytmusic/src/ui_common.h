#pragma once
#include "icons.h"
#include "theme.h"
#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QString>

namespace UI {
inline const Palette &P() { return Theme::I().p(); }
inline QFont fnt(int px, QFont::Weight w = QFont::Normal) {
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(w);
    return f;
}
QString fmtTime(double s);
// Circular icon button. `fill` fills the glyph (e.g. liked heart).
void drawIconButton(QPainter &p, const QRectF &r, const QString &icon, bool hover, bool filled = false,
                    bool toggled = false, bool fill = false, const QString &badge = {});
int pillWidth(const QString &label, const QString &icon);
void drawPill(QPainter &p, const QRectF &r, const QString &label, const QString &icon, bool accent, bool hover);
void drawSpinner(QPainter &p, const QRectF &r, const QColor &c, qint64 ms);
void drawThumb(QPainter &p, const QRectF &r, const QString &vid, qreal radius);
void drawThumbUrl(QPainter &p, const QRectF &r, const QString &key, const QString &url, qreal radius);
} // namespace UI
