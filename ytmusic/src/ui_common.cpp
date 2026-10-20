#include "ui_common.h"
#include "thumbs.h"
#include <QPainterPath>
#include <QWidget>

namespace UI {

QString fmtTime(double s) {
    if (s < 0 || s != s) s = 0;
    const int t = int(s);
    const int h = t / 3600, m = (t % 3600) / 60, sec = t % 60;
    if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
}

void drawIconButton(QPainter &p, const QRectF &r, const QString &icon, bool hover, bool filled, bool toggled, bool fill,
                    const QString &badge) {
    const Palette &c = P();
    p.setPen(Qt::NoPen);
    if (filled || hover) {
        p.setBrush(filled ? c.primary : c.outline);
        p.drawEllipse(r);
    }
    const QColor ic = filled ? c.onPrimary : (toggled ? c.primary : c.onSurface);
    const qreal s = r.width() * 0.45;
    Icons::paint(p, QRectF(r.center().x() - s / 2, r.center().y() - s / 2, s, s), icon, ic, fill);
    if (!badge.isEmpty()) {
        p.setPen(c.primary);
        p.setFont(fnt(10, QFont::Bold));
        p.drawText(r.adjusted(0, 0, -6, -4), Qt::AlignRight | Qt::AlignBottom, badge);
    }
}

int pillWidth(const QString &label, const QString &icon) {
    const QFontMetrics fm(fnt(12, QFont::Medium));
    return fm.horizontalAdvance(label) + 24 + (icon.isEmpty() ? 0 : 20);
}

void drawPill(QPainter &p, const QRectF &r, const QString &label, const QString &icon, bool accent, bool hover) {
    const Palette &c = P();
    p.setRenderHint(QPainter::Antialiasing);
    if (accent) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.secondary);
    } else {
        p.setPen(QPen(c.outline, 1));
        p.setBrush(hover ? QBrush(c.outline) : QBrush(Qt::NoBrush));
    }
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), r.height() / 2, r.height() / 2);
    const QColor fg = accent ? c.onPrimary : c.onSurface;
    p.setFont(fnt(12, QFont::Medium));
    QRectF tr = r;
    if (!icon.isEmpty()) {
        Icons::paint(p, QRectF(r.left() + 12, r.center().y() - 7, 14, 14), icon, fg);
        tr.setLeft(r.left() + 20);
    }
    p.setPen(fg);
    p.drawText(tr, Qt::AlignCenter, label);
}

void drawSpinner(QPainter &p, const QRectF &r, const QColor &c, qint64 ms) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const int start = -int((ms % 900) * 360 / 900) * 16;
    p.drawArc(r.adjusted(2, 2, -2, -2), start, -270 * 16);
    p.restore();
}

void drawThumb(QPainter &p, const QRectF &r, const QString &vid, qreal radius) {
    const Palette &c = P();
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(path, c.surfaceVariant);
    if (!vid.isEmpty()) {
        const QPixmap pm = Thumbs::I().get(vid, r.size().toSize(), p.device()->devicePixelRatioF());
        if (!pm.isNull()) {
            p.setClipPath(path);
            p.drawPixmap(r.topLeft(), pm);
        }
    } else {
        Icons::paint(p, QRectF(r.center().x() - 14, r.center().y() - 14, 28, 28), QStringLiteral("music"), c.outline);
    }
    p.restore();
}

void drawThumbUrl(QPainter &p, const QRectF &r, const QString &key, const QString &url, qreal radius) {
    const Palette &c = P();
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(path, c.surfaceVariant);
    const QPixmap pm = url.isEmpty() ? QPixmap() : Thumbs::I().getUrl(key, url, r.size().toSize(), p.device()->devicePixelRatioF());
    if (!pm.isNull()) {
        p.setClipPath(path);
        p.drawPixmap(r.topLeft(), pm);
    } else {
        Icons::paint(p, QRectF(r.center().x() - 14, r.center().y() - 14, 28, 28), QStringLiteral("list"), c.outline);
    }
    p.restore();
}

} // namespace UI
