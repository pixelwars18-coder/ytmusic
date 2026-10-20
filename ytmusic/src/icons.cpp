#include "icons.h"
#include <QHash>
#include <QPainterPath>
#include <QtMath>

namespace {
const QHash<QString, QString> &paths() {
    static const QHash<QString, QString> h = {
        {"play", "M7 4 v16 l13 -8 z"},
        {"pause", "M6 5 h4 v14 h-4 z M14 5 h4 v14 h-4 z"},
        {"next", "M5 5 l10 7 l-10 7 z M17 5 h2 v14 h-2 z"},
        {"prev", "M19 5 l-10 7 l10 7 z M5 5 h2 v14 h-2 z"},
        {"search", "M17 10 a7 7 0 1 1 -14 0 a7 7 0 1 1 14 0 M15 15 l6 6"},
        {"plus", "M12 5 v14 M5 12 h14"},
        {"x", "M6 6 l12 12 M18 6 l-12 12"},
        {"volume", "M4 9 h4 l5 -4 v14 l-5 -4 h-4 z M16.5 8.5 a5 5 0 0 1 0 7 M19 6 a9 9 0 0 1 0 12"},
        {"mute", "M4 9 h4 l5 -4 v14 l-5 -4 h-4 z M17 9 l5 6 M22 9 l-5 6"},
        {"shuffle", "M3 7 h3 c5 0 5 10 10 10 h5 M18 14 l3 3 l-3 3 M3 17 h3 c2 0 3 -1.2 4 -2.5 M14 9.5 c1 -1.5 2 -2.5 4 -2.5 h3 M18 4 l3 3 l-3 3"},
        {"repeat", "M4 11 v-1 a3 3 0 0 1 3 -3 h13 M17 4 l3 3 l-3 3 M20 13 v1 a3 3 0 0 1 -3 3 h-13 M7 14 l-3 3 l3 3"},
        {"music", "M9 18 V5 l12 -2 v13 M9 18 a3 3 0 1 1 -6 0 a3 3 0 0 1 6 0 M21 16 a3 3 0 1 1 -6 0 a3 3 0 0 1 6 0"},
        {"trash", "M4 7 h16 M10 11 v6 M14 11 v6 M5 7 l1 12 a2 2 0 0 0 2 2 h8 a2 2 0 0 0 2 -2 l1 -12 M9 7 v-3 h6 v3"},
        {"queueNext", "M4 6 h10 M4 12 h10 M4 18 h6 M16 15 h6 M19 12 v6"},
        {"heart", "M12 20 l-7.5 -7.5 a5 5 0 0 1 7.5 -7 a5 5 0 0 1 7.5 7 z"},
        {"list", "M9 6 h11 M9 12 h11 M9 18 h11 M4 6 v.01 M4 12 v.01 M4 18 v.01"},
        {"bookmark", "M7 4 h10 v16 l-5 -4 l-5 4 z"},
        {"refresh", "M20 11 a8.1 8.1 0 0 0 -15.5 -2 m-.5 -4 v4 h4 M4 13 a8.1 8.1 0 0 0 15.5 2 m.5 4 v-4 h-4"},
        {"back", "M15 5 l-7 7 l7 7"},
        {"gear", "M15 12 a3 3 0 1 1 -6 0 a3 3 0 1 1 6 0 M12 3 v3 M12 18 v3 M3 12 h3 M18 12 h3 M5.6 5.6 l2.1 2.1 M16.3 16.3 l2.1 2.1 M5.6 18.4 l2.1 -2.1 M16.3 7.7 l2.1 -2.1"},
        {"check", "M5 12 l5 5 l9 -10"},
        {"clock", "M21 12 a9 9 0 1 1 -18 0 a9 9 0 1 1 18 0 M12 7 v5 l3 2"},
    };
    return h;
}
bool solid(const QString &n) { return n == QLatin1String("play") || n == QLatin1String("pause") || n == QLatin1String("next") || n == QLatin1String("prev"); }
} // namespace


namespace {
// Minimal SVG path parser (M L H V C A Z, absolute + relative): all our icons use.
QPainterPath parsePath(const QString &d) {
    QPainterPath path;
    int i = 0;
    const int n = d.size();
    auto skip = [&] { while (i < n && (d[i] == QLatin1Char(' ') || d[i] == QLatin1Char(','))) ++i; };
    auto num = [&]() -> double {
        skip();
        int j = i;
        if (j < n && (d[j] == QLatin1Char('-') || d[j] == QLatin1Char('+'))) ++j;
        while (j < n && (d[j].isDigit() || d[j] == QLatin1Char('.'))) ++j;
        const double v = d.mid(i, j - i).toDouble();
        i = j;
        return v;
    };
    auto flag = [&]() -> int { skip(); return d[i++] == QLatin1Char('1'); };
    auto moreNums = [&] { skip(); return i < n && (d[i].isDigit() || d[i] == QLatin1Char('-') || d[i] == QLatin1Char('.')); };

    QPointF cur, start;
    QChar cmd;
    while (true) {
        skip();
        if (i >= n) break;
        if (d[i].isLetter()) cmd = d[i++];
        const bool rel = cmd.isLower();
        const QChar c = cmd.toUpper();
        auto P = [&](double x, double y) { return rel ? QPointF(cur.x() + x, cur.y() + y) : QPointF(x, y); };
        if (c == QLatin1Char('Z')) {
            path.closeSubpath();
            cur = start;
            continue;
        }
        do {
            if (c == QLatin1Char('M')) {
                const double x = num(), y = num();
                cur = P(x, y);
                start = cur;
                path.moveTo(cur);
                cmd = rel ? QLatin1Char('l') : QLatin1Char('L'); // implicit lineto after moveto
                break;
            } else if (c == QLatin1Char('L')) {
                const double x = num(), y = num();
                cur = P(x, y);
                path.lineTo(cur);
            } else if (c == QLatin1Char('H')) {
                const double x = num();
                cur = QPointF(rel ? cur.x() + x : x, cur.y());
                path.lineTo(cur);
            } else if (c == QLatin1Char('V')) {
                const double y = num();
                cur = QPointF(cur.x(), rel ? cur.y() + y : y);
                path.lineTo(cur);
            } else if (c == QLatin1Char('C')) {
                const double a = num(), b = num(), c2 = num(), d2 = num(), x = num(), y = num();
                const QPointF p1 = P(a, b), p2 = P(c2, d2), p3 = P(x, y);
                path.cubicTo(p1, p2, p3);
                cur = p3;
            } else if (c == QLatin1Char('A')) {
                double rx = qAbs(num()), ry = qAbs(num());
                const double rot = qDegreesToRadians(num());
                const int fa = flag(), fs = flag();
                const double x = num(), y = num();
                const QPointF e = P(x, y);
                // endpoint -> center parameterisation (SVG implementation notes F.6.5)
                const double cp = qCos(rot), sp = qSin(rot);
                const double dx = (cur.x() - e.x()) / 2, dy = (cur.y() - e.y()) / 2;
                const double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
                const double lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
                if (lam > 1) { rx *= qSqrt(lam); ry *= qSqrt(lam); }
                const double sign = (fa == fs) ? -1 : 1;
                double num2 = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
                if (num2 < 0) num2 = 0;
                const double co = sign * qSqrt(num2 / (rx * rx * y1 * y1 + ry * ry * x1 * x1));
                const double cx1 = co * rx * y1 / ry, cy1 = -co * ry * x1 / rx;
                const double cx = cp * cx1 - sp * cy1 + (cur.x() + e.x()) / 2;
                const double cy = sp * cx1 + cp * cy1 + (cur.y() + e.y()) / 2;
                auto ang = [](double ux, double uy, double vx, double vy) {
                    return qAtan2(ux * vy - uy * vx, ux * vx + uy * vy);
                };
                const double th1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
                double dth = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
                if (!fs && dth > 0) dth -= 2 * M_PI;
                else if (fs && dth < 0) dth += 2 * M_PI;
                // Approximate the arc with cubic segments (<= 90 degrees each).
                const int segs = qMax(1, int(qCeil(qAbs(dth) / (M_PI / 2) - 1e-9)));
                const double step = dth / segs;
                const double t = 4.0 / 3.0 * qTan(step / 4);
                double a1 = th1;
                for (int k = 0; k < segs; ++k) {
                    const double a2 = a1 + step;
                    auto pt = [&](double a, double dxs, double dys) {
                        const double ex = rx * (qCos(a) - dxs * qSin(a)), ey = ry * (qSin(a) + dys * qCos(a));
                        return QPointF(cp * ex - sp * ey + cx, sp * ex + cp * ey + cy);
                    };
                    path.cubicTo(pt(a1, t, t), pt(a2, -t, -t), pt(a2, 0, 0));
                    a1 = a2;
                }
                cur = e;
            }
        } while (moreNums());
    }
    return path;
}
} // namespace

void Icons::paint(QPainter &p, const QRectF &box, const QString &name, const QColor &color, bool fill) {
    if (name.isEmpty()) return;
    static QHash<QString, QPainterPath> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.insert(name, parsePath(paths().value(name)));
    const qreal s = qMin(box.width(), box.height());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.center().x() - s / 2, box.center().y() - s / 2);
    p.scale(s / 24, s / 24);
    QPen pen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush((fill || solid(name)) ? QBrush(color) : Qt::NoBrush);
    p.drawPath(*it);
    p.restore();
}
