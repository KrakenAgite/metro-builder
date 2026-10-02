#include "Schematic.h"
#include "Metro.h"

#include <QSet>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Schematic {

namespace {

double len(const QPointF &v)
{
    return std::hypot(v.x(), v.y());
}

} // namespace

QHash<int, QPointF> layout(const QVector<Station> &stations, const QVector<Line> &lines)
{
    const int n = stations.size();
    QHash<int, int> index;
    QVector<QPointF> geo(n), pos(n);
    for (int i = 0; i < n; ++i) {
        index[stations[i].id] = i;
        geo[i] = pos[i] = stations[i].pos;
    }

    // tronçons uniques du réseau
    QVector<QPair<int, int>> edges;
    QSet<quint64> seen;
    for (const Line &l : lines) {
        const int segs = l.segmentCount();
        for (int k = 0; k < segs; ++k) {
            const int a = index.value(l.stops[k], -1), b = index.value(l.stops[(k + 1) % l.stops.size()], -1);
            if (a < 0 || b < 0 || a == b)
                continue;
            const quint64 key = (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b));
            if (!seen.contains(key)) {
                seen.insert(key);
                edges << qMakePair(a, b);
            }
        }
    }
    QHash<int, QPointF> out;
    if (edges.isEmpty()) {
        for (int i = 0; i < n; ++i)
            out[stations[i].id] = geo[i];
        return out;
    }

    // longueur cible commune : médiane des inter-stations réelles
    QVector<double> lengths;
    for (const auto &e : edges)
        lengths << len(geo[e.second] - geo[e.first]);
    std::sort(lengths.begin(), lengths.end());
    const double L = std::max(250.0, lengths[lengths.size() / 2]);

    // Relaxation : chaque tronçon tend vers la direction octilinéaire la plus proche et la longueur L,
    // avec un léger rappel vers la géographie et une répulsion entre stations trop proches.
    const double step = M_PI / 4;
    for (int iter = 0; iter < 500; ++iter) {
        QVector<QPointF> disp(n);
        QVector<double> weight(n, 0);
        for (const auto &e : edges) {
            const QPointF v = pos[e.second] - pos[e.first];
            const double ang = std::round(std::atan2(v.y(), v.x()) / step) * step;
            // diagonales de (±L, ±L) : elles restent sur la grille comme les tronçons droits
            const double cx = std::round(std::cos(ang)), cy = std::round(std::sin(ang));
            const QPointF target(cx * L, cy * L);
            const QPointF corr = (target - v) * 0.5;
            disp[e.second] += corr;
            disp[e.first] -= corr;
            weight[e.second] += 1;
            weight[e.first] += 1;
        }
        const double anchor = iter < 250 ? 0.04 : 0.01;
        for (int i = 0; i < n; ++i) {
            disp[i] += (geo[i] - pos[i]) * anchor * std::max(1.0, weight[i]);
            for (int j = i + 1; j < n; ++j) {
                const QPointF d = pos[j] - pos[i];
                const double dl = len(d);
                if (dl < L * 0.7) {
                    const QPointF push = (dl < 1e-6 ? QPointF(1, 0) : d / dl) * (L * 0.7 - dl) * 0.5;
                    disp[j] += push;
                    disp[i] -= push;
                    weight[i] += 1;
                    weight[j] += 1;
                }
            }
        }
        for (int i = 0; i < n; ++i)
            pos[i] += disp[i] / std::max(1.0, weight[i]) * 0.6;
    }

    // Alignement sur une grille (sans faire se superposer deux stations)
    const double g = L / 2;
    QSet<quint64> taken;
    auto cellKey = [](qint64 x, qint64 y) { return (quint64(quint32(qint32(x))) << 32) | quint32(qint32(y)); };
    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // les correspondances d'abord : ce sont elles qui structurent le plan
    std::sort(order.begin(), order.end(), [&](int a, int b) { return stations[a].lineCount > stations[b].lineCount; });
    for (int i : order) {
        qint64 gx = std::llround(pos[i].x() / g), gy = std::llround(pos[i].y() / g);
        for (int r = 0; taken.contains(cellKey(gx, gy)) && r < 8; ++r) {
            const int dx[] = {1, -1, 0, 0, 1, -1, 1, -1};
            const int dy[] = {0, 0, 1, -1, 1, -1, -1, 1};
            const qint64 nx = std::llround(pos[i].x() / g) + dx[r], ny = std::llround(pos[i].y() / g) + dy[r];
            gx = nx;
            gy = ny;
        }
        taken.insert(cellKey(gx, gy));
        pos[i] = QPointF(gx * g, gy * g);
    }

    // recentrage sur le centre du réseau réel
    QPointF cGeo, cPos;
    for (int i = 0; i < n; ++i) {
        cGeo += geo[i];
        cPos += pos[i];
    }
    const QPointF shift = (cGeo - cPos) / n;
    for (int i = 0; i < n; ++i)
        out[stations[i].id] = pos[i] + QPointF(std::round(shift.x() / g) * g, std::round(shift.y() / g) * g);
    return out;
}

QPolygonF octilinearPath(const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double ax = std::abs(d.x()), ay = std::abs(d.y());
    const double eps = 1e-3 * std::max(1.0, ax + ay);
    if (ax < eps || ay < eps || std::abs(ax - ay) < eps)
        return QPolygonF{a, b};
    // droit (moitié du reste) + diagonale + droit (autre moitié)
    const double diag = std::min(ax, ay);
    const double sx = d.x() > 0 ? 1 : -1, sy = d.y() > 0 ? 1 : -1;
    const QPointF straight = ax > ay ? QPointF(sx * (ax - diag) / 2, 0) : QPointF(0, sy * (ay - diag) / 2);
    const QPointF p1 = a + straight;
    const QPointF p2 = p1 + QPointF(sx * diag, sy * diag);
    return QPolygonF{a, p1, p2, b};
}

} // namespace Schematic
