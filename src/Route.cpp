// Calculateur d'itinéraire : meilleur trajet entre deux points de la ville, à pied et en métro
// (marche jusqu'à une station, attente, trajet, correspondances, marche jusqu'à l'arrivée).
#include "Metro.h"

#include <cmath>
#include <limits>
#include <queue>

namespace {

constexpr double WalkSpeed = 75;  // m/min (4,5 km/h)
constexpr double Detour = 1.25;   // les rues ne vont pas en ligne droite
constexpr double MaxWalk = 1500;  // m : distance à pied maximale jusqu'à une station

double walkMeters(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y()) * Detour;
}

struct RouteEdge {
    int to;
    double cost;
    int line;     // index de ligne ; -1 = correspondance
    int seg, dir; // inter-station parcouru
};

} // namespace

Route Metro::route(const QPointF &from, const QPointF &to) const
{
    Route r;
    r.valid = true;
    r.from = from;
    r.to = to;
    r.walkMinutes = walkMeters(from, to) / WalkSpeed;

    // graphe (station, ligne) : lignes en service uniquement (pas de grève)
    const int nS = m_stations.size(), nL = m_lines.size();
    QVector<QVector<int>> nodeOf(nL), stationNodes(nS);
    QVector<int> nodeLine, nodeStation;
    for (int li = 0; li < nL; ++li) {
        const Line &l = m_lines[li];
        if (l.segmentCount() == 0 || lineCapacityFactor(l.id) <= 0)
            continue;
        for (int id : l.stops) {
            const int si = stationIndex(id);
            nodeOf[li] << nodeLine.size();
            stationNodes[si] << nodeLine.size();
            nodeLine << li;
            nodeStation << si;
        }
    }
    const int nN = nodeLine.size();
    QVector<QVector<RouteEdge>> adj(nN);
    for (int li = 0; li < nL; ++li) {
        const Line &l = m_lines[li];
        if (nodeOf[li].isEmpty())
            continue;
        const int n = l.stops.size();
        for (const Leg &g : l.legs) {
            if (g.seg < 0)
                continue;
            const int a = g.dir == 0 ? g.seg : (g.seg + 1) % n;
            const int b = g.dir == 0 ? (g.seg + 1) % n : g.seg;
            adj[nodeOf[li][a]] << RouteEdge{nodeOf[li][b], g.dur + Rules::Dwell, li, g.seg, g.dir};
        }
    }
    for (int si = 0; si < nS; ++si) {
        if (stationClosed(m_stations[si].id))
            continue;
        for (int a : stationNodes[si])
            for (int b : stationNodes[si])
                if (a != b)
                    adj[a] << RouteEdge{b, Rules::TransferPenalty + m_lines[nodeLine[b]].headwayMin / 2, -1, si, 0};
    }

    // plus court chemin depuis toutes les stations accessibles à pied
    const double inf = std::numeric_limits<double>::infinity();
    QVector<double> d(nN, inf);
    QVector<int> predNode(nN, -1);
    QVector<RouteEdge> predEdge(nN);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (int si = 0; si < nS; ++si) {
        const Station &s = m_stations[si];
        const double w = walkMeters(from, s.pos);
        if (w > MaxWalk || stationClosed(s.id))
            continue;
        for (int nd : stationNodes[si]) {
            const double c = w / WalkSpeed + m_lines[nodeLine[nd]].headwayMin / 2;
            if (c < d[nd]) {
                d[nd] = c;
                pq.push({c, nd});
            }
        }
    }
    while (!pq.empty()) {
        auto [du, u] = pq.top();
        pq.pop();
        if (du > d[u])
            continue;
        for (const RouteEdge &e : adj[u])
            if (du + e.cost < d[e.to]) {
                d[e.to] = du + e.cost;
                predNode[e.to] = u;
                predEdge[e.to] = e;
                pq.push({d[e.to], e.to});
            }
    }
    int best = -1;
    double bestCost = inf;
    for (int nd = 0; nd < nN; ++nd) {
        const Station &s = m_stations[nodeStation[nd]];
        const double w = walkMeters(s.pos, to);
        if (d[nd] == inf || w > MaxWalk || stationClosed(s.id))
            continue;
        if (d[nd] + w / WalkSpeed < bestCost) {
            bestCost = d[nd] + w / WalkSpeed;
            best = nd;
        }
    }

    const QColor walkColor; // invalide = tracé piéton
    if (best < 0 || bestCost >= r.walkMinutes) { // à pied, c'est plus rapide (ou pas de métro)
        r.minutes = r.walkMinutes;
        r.steps << RouteStep{RouteStep::Walk, -1, -1, -1, 0, {}, r.walkMinutes, walkMeters(from, to)};
        r.drawing << qMakePair(walkColor, QPolygonF{from, to});
        return r;
    }

    // chemin dans l'ordre : noeud de départ puis arêtes parcourues
    QVector<RouteEdge> edges;
    int start = best;
    for (int n = best; predNode[n] >= 0; n = predNode[n]) {
        edges.prepend(predEdge[n]);
        start = predNode[n];
    }
    r.byMetro = true;
    r.minutes = bestCost;
    const Station &first = m_stations[nodeStation[start]];
    const Station &last = m_stations[nodeStation[best]];
    r.steps << RouteStep{RouteStep::Walk, -1, -1, first.id, 0, {}, walkMeters(from, first.pos) / WalkSpeed,
                         walkMeters(from, first.pos)};
    r.drawing << qMakePair(walkColor, QPolygonF{from, first.pos});

    double wait = m_lines[nodeLine[start]].headwayMin / 2; // attente du premier train
    int node = start;
    for (const RouteEdge &e : edges) {
        if (e.line < 0) { // correspondance
            const Line &next = m_lines[nodeLine[e.to]];
            r.steps << RouteStep{RouteStep::Transfer, next.id, m_stations[e.seg].id, m_stations[e.seg].id, 0, {},
                                 e.cost, 0};
            wait = 0; // déjà comptée dans la correspondance
            node = e.to;
            continue;
        }
        const Line &l = m_lines[e.line];
        RouteStep *ride = (!r.steps.isEmpty() && r.steps.last().kind == RouteStep::Ride
                           && r.steps.last().lineId == l.id)
                              ? &r.steps.last()
                              : nullptr;
        if (!ride) {
            RouteStep s;
            s.kind = RouteStep::Ride;
            s.lineId = l.id;
            s.fromStation = m_stations[nodeStation[node]].id;
            s.minutes = wait;
            if (l.isLoop())
                s.towards = tr("boucle");
            else
                s.towards = station(e.dir == 0 ? l.stops.last() : l.stops.first())->name;
            r.steps << s;
            ride = &r.steps.last();
            r.drawing << qMakePair(l.color, QPolygonF());
            wait = 0;
        }
        ride->minutes += e.cost;
        ++ride->stops;
        ride->toStation = m_stations[nodeStation[e.to]].id;
        if (e.seg < l.paths.size()) {
            QPolygonF pts = l.paths[e.seg].pts;
            if (e.dir == 1)
                std::reverse(pts.begin(), pts.end());
            r.drawing.last().second << pts;
        }
        node = e.to;
    }
    r.steps << RouteStep{RouteStep::Walk, -1, last.id, -1, 0, {}, walkMeters(last.pos, to) / WalkSpeed,
                         walkMeters(last.pos, to)};
    r.drawing << qMakePair(walkColor, QPolygonF{last.pos, to});
    return r;
}
