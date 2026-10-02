#include "Metro.h"

#include <QJsonArray>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace {


double dist(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Projeté de p sur [a,b]
QPointF projectOnSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0)
        return a;
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return a + ab * t;
}

struct Traversal {
    int line;    // -1 = correspondance
    int seg;     // inter-station, ou index de station pour une correspondance
    int dir;
};

struct Edge {
    int to;
    double cost;
    Traversal tr;
};

struct OD {
    int from, to;
    double demand;
    double served = 0;
    QVector<Traversal> path;
};

} // namespace

Metro::Metro(QObject *parent)
    : QObject(parent)
{
}

void Metro::setCity(QSharedPointer<CityData> city)
{
    m_city = city;
    buildWaterIndex();
    buildGrid();
    reset();
}

void Metro::updateCity(QSharedPointer<CityData> city)
{
    // le repère (lat0/lon0) est inchangé : stations et tracés restent valides
    m_city = city;
    buildWaterIndex();
    const double before = m_population;
    buildGrid();
    m_popYearStart += m_population - before; // habitants de la zone ajoutée : pas de la croissance
    m_initialPopulation += m_population - before;
    recompute();
}

void Metro::reset()
{
    m_stations.clear();
    m_lines.clear();
    m_nextStationId = 1;
    m_nextLineId = 1;
    m_money = Rules::StartMoney;
    m_simMinutes = 0;
    m_financeSeconds = 0;
    m_history.clear();
    m_current = MonthRecord();
    m_totalInvested = m_totalRevenue = m_totalOperating = 0;
    m_events.clear();
    m_developments.clear();
    m_nextEventId = 1;
    m_monthsSinceEvent = 0;
    m_lastEventBad = false;
    m_score = 0;
    m_lastMonthPoints = m_goalsDone = m_profitStreak = m_goalRotation = 0;
    std::fill(std::begin(m_goalLevel), std::end(m_goalLevel), 0);
    m_goals.clear();
    m_fare = Rules::Fare;
    m_maintenance = 1;
    m_loans.clear();
    m_monthlySubsidy = 0;
    m_baseGrowth = 1;
    m_growth.clear();
    m_popYearStart = 0;
    m_initialPopulation = 0;
    m_mission = Mission();
    buildGrid();
    recompute();
    resetUndo();
    emit eventsChanged();
}

void Metro::buildGrid()
{
    m_grid = DemandGrid();
    if (!m_city || m_city->bounds.isEmpty())
        return;
    const QRectF b = m_city->bounds;
    m_grid.origin = b.topLeft();
    m_grid.w = std::max(1, int(std::ceil(b.width() / m_grid.cell)));
    m_grid.h = std::max(1, int(std::ceil(b.height() / m_grid.cell)));
    const int n = m_grid.w * m_grid.h;
    m_grid.pop.fill(0, n);
    m_grid.jobs.fill(0, n);
    m_grid.potential.fill(0, n);
    m_grid.servedFrac.fill(0, n);
    m_grid.coverage.fill(0, n);
    for (const Building &bd : m_city->buildings) {
        const int cx = int((bd.centroid.x() - m_grid.origin.x()) / m_grid.cell);
        const int cy = int((bd.centroid.y() - m_grid.origin.y()) / m_grid.cell);
        if (cx < 0 || cy < 0 || cx >= m_grid.w || cy >= m_grid.h)
            continue;
        const int i = cy * m_grid.w + cx;
        m_grid.pop[i] += bd.residents;
        m_grid.jobs[i] += bd.jobs;
    }
    applyDevelopments();
    applyGrowth();
    if (m_popYearStart <= 0)
        m_popYearStart = m_population;
    if (m_initialPopulation <= 0)
        m_initialPopulation = m_population;
    for (int i = 0; i < n; ++i)
        m_grid.potential[i] = m_grid.pop[i] * Rules::TripRateResident + m_grid.jobs[i] * Rules::TripRateJob;
}

double Metro::walkWeight(double d)
{
    if (d <= Rules::WalkFull)
        return 1;
    if (d >= Rules::WalkMax)
        return 0;
    return 1 - (d - Rules::WalkFull) / (Rules::WalkMax - Rules::WalkFull);
}

// ---------------------------------------------------------------------------
// Accès
// ---------------------------------------------------------------------------

Station *Metro::stationMut(int id)
{
    for (Station &s : m_stations)
        if (s.id == id)
            return &s;
    return nullptr;
}

const Station *Metro::station(int id) const
{
    return const_cast<Metro *>(this)->stationMut(id);
}

int Metro::stationIndex(int id) const
{
    for (int i = 0; i < m_stations.size(); ++i)
        if (m_stations[i].id == id)
            return i;
    return -1;
}

Line *Metro::lineMut(int id)
{
    for (Line &l : m_lines)
        if (l.id == id)
            return &l;
    return nullptr;
}

const Line *Metro::line(int id) const
{
    return const_cast<Metro *>(this)->lineMut(id);
}

int Metro::stationAt(const QPointF &world, double radius) const
{
    int best = -1;
    double bestD = radius;
    for (const Station &s : m_stations) {
        const double d = dist(s.pos, world);
        if (d <= bestD) {
            bestD = d;
            best = s.id;
        }
    }
    return best;
}

bool Metro::spend(double amount, const QString &what)
{
    if (amount <= 0 || m_sandbox)
        return true;
    amount *= buildCostFactor(); // rabais ou pénurie en cours
    if (m_money < amount) {
        emit message(tr("Budget insuffisant pour %1 (%2 M€ requis, %3 M€ disponibles)")
                         .arg(what)
                         .arg(amount, 0, 'f', 1)
                         .arg(m_money, 0, 'f', 1));
        return false;
    }
    charge(amount);
    return true;
}

QString Metro::nearestStreet(const QPointF &p, double maxDist) const
{
    if (!m_city)
        return {};
    QString best;
    double bestD = maxDist;
    for (const Road &r : m_city->roads + m_city->streetNames) {
        if (r.name.isEmpty())
            continue;
        for (int i = 0; i + 1 < r.pts.size(); ++i) {
            const double d = dist(p, projectOnSegment(p, r.pts[i], r.pts[i + 1]));
            if (d < bestD) {
                bestD = d;
                best = r.name;
            }
        }
    }
    return best;
}

QPointF Metro::snapToRoad(const QPointF &p, double maxDist) const
{
    if (!m_city)
        return p;
    QPointF best = p;
    double bestD = maxDist;
    for (const Road &r : m_city->roads) {
        if (r.kind == RoadKind::Service)
            continue;
        for (int i = 0; i + 1 < r.pts.size(); ++i) {
            const QPointF q = projectOnSegment(p, r.pts[i], r.pts[i + 1]);
            const double d = dist(p, q);
            if (d < bestD) {
                bestD = d;
                best = q;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Stations
// ---------------------------------------------------------------------------

int Metro::addStation(const QPointF &pos)
{
    if (m_city && !m_city->bounds.adjusted(-200, -200, 200, 200).contains(pos)) {
        emit message(tr("Impossible de construire hors de la zone de jeu"));
        return -1;
    }
    if (!spend(Rules::StationCost, tr("une station")))
        return -1;

    Station s;
    s.id = m_nextStationId++;
    s.pos = pos;
    QString base = nearestStreet(pos, 150);
    if (base.isEmpty())
        base = tr("Station %1").arg(s.id);
    QString name = base;
    for (int k = 2; std::any_of(m_stations.begin(), m_stations.end(),
                                [&](const Station &o) { return o.name == name; });
         ++k)
        name = QStringLiteral("%1 %2").arg(base).arg(k);
    s.name = name;
    m_stations << s;
    recompute();
    return s.id;
}

void Metro::moveStation(int id, const QPointF &pos, bool commit)
{
    Station *s = stationMut(id);
    if (!s)
        return;
    s->pos = pos;
    if (!commit) {
        for (Line &l : m_lines) // aperçu du tracé pendant le glisser
            if (l.stops.contains(id))
                l.paths = buildPaths(l);
        return;
    }
    for (Line &l : m_lines)
        if (l.stops.contains(id))
            settleTrack(l);
    recompute();
}

void Metro::removeStation(int id)
{
    const int idx = stationIndex(id);
    if (idx < 0)
        return;
    for (Line &l : m_lines) {
        if (l.stops.contains(id)) {
            detachStop(l, id);
            settleTrack(l);
        }
    }
    charge(-Rules::StationCost * Rules::Refund);
    m_stations.removeAt(idx);
    recompute();
}

void Metro::renameStation(int id, const QString &name)
{
    if (Station *s = stationMut(id); s && !name.trimmed().isEmpty() && s->name != name.trimmed()) {
        s->name = name.trimmed();
        trackEdit();
        emit networkChanged();
    }
}

// ---------------------------------------------------------------------------
// Lignes
// ---------------------------------------------------------------------------

double Metro::lineLength(const Line &l) const
{
    double len = 0;
    for (const SegPath &sp : buildPaths(l))
        len += sp.length();
    return len;
}

// ---------------------------------------------------------------------------
// Tracés courbes et points de passage
// ---------------------------------------------------------------------------

namespace {

quint64 pairKey(int a, int b)
{
    return (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b));
}

// Catmull-Rom centripète entre p1 et p2 (p0 et p3 donnent les tangentes), t dans [0,1]
QPointF catmullRom(const QPointF &p0, const QPointF &p1, const QPointF &p2, const QPointF &p3, double u)
{
    auto knot = [](double t, const QPointF &a, const QPointF &b) {
        return t + std::max(1e-3, std::sqrt(std::hypot(b.x() - a.x(), b.y() - a.y())));
    };
    const double t0 = 0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
    const double t = t1 + (t2 - t1) * u;
    const QPointF a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
    const QPointF a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
    const QPointF a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
    const QPointF b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
    const QPointF b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
    return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b, QPointF *proj)
{
    *proj = projectOnSegment(p, a, b);
    return dist(p, *proj);
}

} // namespace

QPointF SegPath::pointAt(double s, double *angleDeg) const
{
    if (pts.size() < 2)
        return pts.isEmpty() ? QPointF() : pts.first();
    int i;
    if (s <= 0)
        i = 0;
    else if (s >= length())
        i = pts.size() - 2;
    else
        i = int(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin()) - 1;
    i = std::clamp(i, 0, int(pts.size()) - 2);
    const QPointF a = pts[i], b = pts[i + 1];
    const double segLen = std::max(1e-6, cum[i + 1] - cum[i]);
    if (angleDeg)
        *angleDeg = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / M_PI;
    // au-delà des extrémités on prolonge en ligne droite (rame à quai)
    return a + (b - a) * ((s - cum[i]) / segLen);
}

QVector<QPointF> Metro::segmentVia(const Line &l, int seg) const
{
    const int n = l.stops.size();
    const int a = l.stops[seg], b = l.stops[(seg + 1) % n];
    QVector<QPointF> pts = l.via.value(pairKey(a, b));
    if (a > b)
        std::reverse(pts.begin(), pts.end());
    return pts;
}

void Metro::setSegmentVia(Line &l, int seg, QVector<QPointF> pts)
{
    const int n = l.stops.size();
    const int a = l.stops[seg], b = l.stops[(seg + 1) % n];
    if (a > b)
        std::reverse(pts.begin(), pts.end());
    if (pts.isEmpty())
        l.via.remove(pairKey(a, b));
    else
        l.via[pairKey(a, b)] = pts;
}

// Retire un arrêt en conservant la forme du tunnel : les points de passage des deux
// inter-stations voisins et l'emplacement de la station deviennent ceux du nouveau tronçon.
void Metro::detachStop(Line &l, int stationId)
{
    const int i = l.stops.indexOf(stationId);
    if (i < 0)
        return;
    const int n = l.stops.size();
    const bool hasPrev = i > 0 || l.isLoop();
    const bool hasNext = i < n - 1 || l.isLoop();
    if (hasPrev && hasNext && n >= 3 && (n > 3 || !l.isLoop())) {
        const int prevSeg = (i - 1 + n) % n;
        const int a = l.stops[prevSeg], b = l.stops[(i + 1) % n];
        if (a != b && !l.via.contains(pairKey(a, b))) {
            QVector<QPointF> merged = segmentVia(l, prevSeg);
            merged << station(stationId)->pos;
            merged += segmentVia(l, i);
            if (a > b)
                std::reverse(merged.begin(), merged.end());
            l.via[pairKey(a, b)] = merged;
        }
    }
    l.stops.removeAll(stationId);
}

// Courbe lissée passant par toutes les stations et tous les points de passage de la ligne,
// découpée en un tracé par inter-station.
QVector<SegPath> Metro::buildPaths(const Line &l) const
{
    const int segs = l.segmentCount();
    QVector<SegPath> out;
    if (segs == 0)
        return out;
    const int n = l.stops.size();
    const bool loop = l.isLoop();

    QVector<QPointF> ctrl;
    QVector<int> stationIdx;
    for (int k = 0; k < n; ++k) {
        stationIdx << ctrl.size();
        ctrl << station(l.stops[k])->pos;
        if (k < segs)
            ctrl += segmentVia(l, k);
    }
    const int m = ctrl.size();
    auto P = [&](int i) -> QPointF {
        if (loop)
            return ctrl[((i % m) + m) % m];
        if (i < 0)
            return ctrl[0] * 2 - ctrl[1];
        if (i >= m)
            return ctrl[m - 1] * 2 - ctrl[m - 2];
        return ctrl[i];
    };

    out.resize(segs);
    for (int k = 0; k < segs; ++k) {
        SegPath &sp = out[k];
        const int i0 = stationIdx[k];
        const int i1 = (loop && k == n - 1) ? m : stationIdx[k + 1];
        sp.pts << P(i0);
        sp.knots << 0;
        for (int i = i0; i < i1; ++i) {
            const QPointF p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
            const int samples = std::clamp(int(dist(p1, p2) / 20.0), 4, 40);
            for (int j = 1; j <= samples; ++j)
                sp.pts << (j == samples ? p2 : catmullRom(p0, p1, p2, p3, double(j) / samples));
            sp.knots << sp.pts.size() - 1;
        }
        sp.cum.resize(sp.pts.size());
        sp.cum[0] = 0;
        for (int j = 1; j < sp.pts.size(); ++j)
            sp.cum[j] = sp.cum[j - 1] + dist(sp.pts[j - 1], sp.pts[j]);
    }
    return out;
}

QVector<QPointF> Metro::waypoints(int lineId, int seg) const
{
    const Line *l = line(lineId);
    if (!l || seg < 0 || seg >= l->segmentCount())
        return {};
    return segmentVia(*l, seg);
}

bool Metro::insertWaypoint(int lineId, int seg, int index, const QPointF &pos)
{
    Line *l = lineMut(lineId);
    if (!l || seg < 0 || seg >= l->segmentCount())
        return false;
    if (m_money <= 0) {
        emit message(tr("Budget insuffisant pour modifier le tracé"));
        return false;
    }
    QVector<QPointF> pts = segmentVia(*l, seg);
    pts.insert(std::clamp(index, 0, int(pts.size())), pos);
    setSegmentVia(*l, seg, pts);
    l->paths = buildPaths(*l);
    return true;
}

void Metro::moveWaypoint(int lineId, int seg, int index, const QPointF &pos, bool commit)
{
    Line *l = lineMut(lineId);
    if (!l || seg < 0 || seg >= l->segmentCount())
        return;
    QVector<QPointF> pts = segmentVia(*l, seg);
    if (index < 0 || index >= pts.size())
        return;
    pts[index] = pos;
    setSegmentVia(*l, seg, pts);
    if (!commit) {
        l->paths = buildPaths(*l);
        return;
    }
    settleTrack(*l);
    recompute();
}

void Metro::removeWaypoint(int lineId, int seg, int index)
{
    Line *l = lineMut(lineId);
    if (!l || seg < 0 || seg >= l->segmentCount())
        return;
    QVector<QPointF> pts = segmentVia(*l, seg);
    if (index < 0 || index >= pts.size())
        return;
    pts.removeAt(index);
    setSegmentVia(*l, seg, pts);
    settleTrack(*l);
    recompute();
}

TrackHit Metro::waypointAt(const QPointF &world, double radius, int preferLine) const
{
    TrackHit best;
    double bestD = radius;
    for (const Line &l : m_lines) {
        for (int k = 0; k < l.segmentCount(); ++k) {
            const QVector<QPointF> pts = segmentVia(l, k);
            for (int i = 0; i < pts.size(); ++i) {
                const double d = dist(world, pts[i]) - (l.id == preferLine ? radius * 0.3 : 0);
                if (d <= bestD) {
                    bestD = d;
                    best = {l.id, k, i, pts[i]};
                }
            }
        }
    }
    return best;
}

TrackHit Metro::trackAt(const QPointF &world, double radius, int preferLine) const
{
    TrackHit best;
    double bestD = radius;
    for (const Line &l : m_lines) {
        for (int k = 0; k < l.paths.size(); ++k) {
            const SegPath &sp = l.paths[k];
            if (!sp.pts.boundingRect().adjusted(-radius, -radius, radius, radius).contains(world))
                continue;
            for (int j = 0; j + 1 < sp.pts.size(); ++j) {
                QPointF proj;
                const double d = distToSegment(world, sp.pts[j], sp.pts[j + 1], &proj)
                                 - (l.id == preferLine ? radius * 0.3 : 0);
                if (d > bestD)
                    continue;
                bestD = d;
                // position d'insertion : nombre de points de contrôle déjà dépassés
                int span = 0;
                while (span + 1 < sp.knots.size() && sp.knots[span + 1] <= j)
                    ++span;
                best = {l.id, k, span, proj};
            }
        }
    }
    return best;
}

// Facture (ou rembourse) la variation du tracé d'une ligne (tunnel, viaduc, passages sous l'eau).
void Metro::settleTrack(Line &l)
{
    const double len = trackUnits(l);
    const double diffKm = (len - l.paidLength) / 1000.0;
    if (diffKm > 0)
        charge(diffKm * Rules::TrackCostKm * buildCostFactor());
    else
        charge(diffKm * Rules::TrackCostKm * Rules::Refund); // diffKm < 0 : remboursement
    l.paidLength = len;
}

const QVector<QColor> &Metro::presetColors()
{
    // palette inspirée des grands réseaux de métro, couleurs franches et bien distinctes
    static const QVector<QColor> colors = {
        QColor("#FFCD00"), QColor("#0055C8"), QColor("#E3051C"), QColor("#00A88F"),
        QColor("#CF009E"), QColor("#FF7E2E"), QColor("#6ECA97"), QColor("#8D5E2A"),
        QColor("#6EC4E8"), QColor("#62259D"), QColor("#B6BD00"), QColor("#FA9ABA"),
        QColor("#007852"), QColor("#F28E00"), QColor("#00B2E3"), QColor("#9B9B9B"),
    };
    return colors;
}

bool Metro::codeUsed(const QString &code, int exceptLine) const
{
    for (const Line &l : m_lines)
        if (l.id != exceptLine && l.code.compare(code, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString Metro::nextFreeCode(bool letter) const
{
    if (letter) {
        for (char ch = 'A'; ch <= 'Z'; ++ch)
            if (!codeUsed(QString(QChar(ch))))
                return QString(QChar(ch));
        return {};
    }
    for (int n = 1; n < 100; ++n)
        if (!codeUsed(QString::number(n)))
            return QString::number(n);
    return {};
}

int Metro::addLine(const QString &wantedCode)
{
    Line l;
    l.id = m_nextLineId++;
    l.code = wantedCode.isEmpty() || codeUsed(wantedCode) ? nextFreeCode(false) : wantedCode;
    l.name = tr("Ligne %1").arg(l.code);
    // première couleur de la palette qui n'est pas déjà prise
    const QVector<QColor> &palette = presetColors();
    l.color = palette[(l.id - 1) % palette.size()];
    for (const QColor &c : palette) {
        if (std::none_of(m_lines.begin(), m_lines.end(), [&](const Line &o) { return o.color == c; })) {
            l.color = c;
            break;
        }
    }
    if (!spend(l.wagons * l.trains * Rules::WagonCost, tr("le matériel roulant d'une nouvelle ligne"))) {
        --m_nextLineId;
        return -1;
    }
    m_lines << l;
    recompute();
    return l.id;
}

void Metro::removeLine(int id)
{
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].id != id)
            continue;
        const Line &l = m_lines[i];
        charge(-(l.paidLength / 1000.0 * Rules::TrackCostKm + l.wagons * l.trains * Rules::WagonCost)
               * Rules::Refund);
        m_lines.removeAt(i);
        recompute();
        return;
    }
}

bool Metro::addStop(int lineId, int stationId, bool atFront)
{
    Line *l = lineMut(lineId);
    const Station *s = station(stationId);
    if (!l || !s)
        return false;
    if (l->stops.contains(stationId)) {
        emit message(tr("%1 est déjà desservie par %2").arg(s->name, l->name));
        return false;
    }
    if (!l->stops.isEmpty()) {
        const Station *nb = station(atFront ? l->stops.first() : l->stops.last());
        const double cost = dist(nb->pos, s->pos) / 1000.0 * Rules::TrackCostKm * buildCostFactor();
        if (!m_sandbox && m_money < cost) {
            spend(cost, tr("ce tunnel"));
            return false;
        }
    }
    if (atFront)
        l->stops.prepend(stationId);
    else
        l->stops.append(stationId);
    settleTrack(*l);
    recompute();
    return true;
}

void Metro::removeStop(int lineId, int stationId)
{
    if (Line *l = lineMut(lineId); l && l->stops.contains(stationId)) {
        detachStop(*l, stationId);
        settleTrack(*l);
        recompute();
    }
}

void Metro::setStops(int lineId, const QVector<int> &stops)
{
    Line *l = lineMut(lineId);
    if (!l || l->stops == stops)
        return;
    l->stops = stops;
    settleTrack(*l);
    recompute();
}

void Metro::moveStop(int lineId, int from, int to)
{
    Line *l = lineMut(lineId);
    if (!l || from < 0 || to < 0 || from >= l->stops.size() || to >= l->stops.size() || from == to)
        return;
    l->stops.move(from, to);
    settleTrack(*l);
    recompute();
}

void Metro::reverseLine(int lineId)
{
    if (Line *l = lineMut(lineId)) {
        std::reverse(l->stops.begin(), l->stops.end());
        recompute();
    }
}

void Metro::setWagons(int lineId, int wagons)
{
    Line *l = lineMut(lineId);
    if (!l)
        return;
    wagons = std::clamp(wagons, Rules::MinWagons, Rules::MaxWagons);
    const int delta = (wagons - l->wagons) * l->trains;
    if (delta > 0 && !spend(delta * Rules::WagonCost, tr("%1 voitures").arg(delta))) {
        emit networkChanged(); // pour resynchroniser l'interface
        return;
    }
    if (delta < 0)
        charge(delta * Rules::WagonCost * Rules::Refund); // delta < 0 : revente
    if (delta > 0) // voitures neuves : l'âge moyen rajeunit
        l->stockAge *= double(l->wagons) / wagons;
    l->wagons = wagons;
    recompute();
}

void Metro::setTrains(int lineId, int trains)
{
    Line *l = lineMut(lineId);
    if (!l)
        return;
    trains = std::clamp(trains, 1, 40);
    const int delta = (trains - l->trains) * l->wagons;
    if (delta > 0 && !spend(delta * Rules::WagonCost, tr("%1 rame(s)").arg(trains - l->trains))) {
        emit networkChanged();
        return;
    }
    if (delta < 0)
        charge(delta * Rules::WagonCost * Rules::Refund);
    if (delta > 0) // rames neuves
        l->stockAge *= double(l->trains) / trains;
    l->trains = trains;
    recompute();
}

void Metro::setLoop(int lineId, bool loop)
{
    if (Line *l = lineMut(lineId); l && l->loop != loop) {
        l->loop = loop;
        settleTrack(*l);
        recompute();
    }
}

void Metro::setLineName(int lineId, const QString &name)
{
    if (Line *l = lineMut(lineId); l && !name.trimmed().isEmpty() && l->name != name.trimmed()) {
        l->name = name.trimmed();
        trackEdit();
        emit networkChanged();
    }
}

void Metro::setLineCode(int lineId, const QString &code)
{
    Line *l = lineMut(lineId);
    if (!l || code.isEmpty() || codeUsed(code, lineId))
        return;
    if (l->name == tr("Ligne %1").arg(l->code)) // nom par défaut : on le suit
        l->name = tr("Ligne %1").arg(code);
    l->code = code;
    trackEdit();
    emit networkChanged();
}

void Metro::setLineColor(int lineId, const QColor &color)
{
    if (Line *l = lineMut(lineId); l && color.isValid()) {
        l->color = color;
        trackEdit();
        emit networkChanged();
    }
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void Metro::computeLineGeometry(Line &l)
{
    l.legs.clear();
    l.length = l.cycleMin = l.headwayMin = l.capacity = 0;
    l.ridership = l.maxLoad = 0;
    const int segs = l.segmentCount();
    l.load[0].fill(0, segs);
    l.load[1].fill(0, segs);
    l.paths = buildPaths(l);
    if (segs == 0)
        return;

    const int n = l.stops.size();
    QVector<QPointF> p(n);
    for (int k = 0; k < n; ++k)
        p[k] = station(l.stops[k])->pos;

    double t = 0;
    auto dwell = [&](int k, double dur) {
        Leg g;
        g.a = g.b = p[k];
        g.t0 = t;
        g.dur = dur;
        l.legs << g;
        t += dur;
    };
    auto travel = [&](int seg, int dir, int from, int to) {
        Leg g;
        g.a = p[from];
        g.b = p[to];
        g.t0 = t;
        const double len = l.paths[seg].length();
        g.dur = len / Rules::TrainSpeed + Rules::AccelPenalty;
        g.seg = g.refSeg = seg;
        g.dir = g.refDir = dir;
        g.angle = std::atan2(g.b.y() - g.a.y(), g.b.x() - g.a.x()) * 180.0 / M_PI;
        l.legs << g;
        t += g.dur;
        if (dir == 0)
            l.length += len;
    };

    if (l.isLoop()) {
        for (int k = 0; k < n; ++k) {
            dwell(k, Rules::Dwell);
            travel(k, 0, k, (k + 1) % n);
        }
    } else {
        for (int k = 0; k + 1 < n; ++k) {
            dwell(k, k == 0 ? Rules::Turnaround : Rules::Dwell);
            travel(k, 0, k, k + 1);
        }
        dwell(n - 1, Rules::Turnaround);
        for (int k = n - 2; k >= 0; --k) {
            travel(k, 1, k + 1, k);
            if (k > 0)
                dwell(k, Rules::Dwell);
        }
    }
    // Un train à l'arrêt garde l'orientation et la charge du trajet suivant
    for (int i = 0; i < l.legs.size(); ++i) {
        Leg &g = l.legs[i];
        if (g.seg >= 0)
            continue;
        const Leg &next = l.legs[(i + 1) % l.legs.size()];
        g.angle = next.angle;
        g.refSeg = next.refSeg;
        g.refDir = next.refDir;
    }

    l.cycleMin = t;
    l.headwayMin = l.cycleMin / l.trains;
    l.capacity = 60.0 / l.headwayMin * l.wagons * Rules::WagonCapacity * lineCapacityFactor(l.id);
}

void Metro::recompute()
{
    for (Station &s : m_stations) {
        s.lineCount = 0;
        s.catchPop = s.catchJobs = s.potential = s.served = s.boardings = 0;
    }
    for (Line &l : m_lines) {
        computeLineGeometry(l);
        if (l.segmentCount() == 0)
            continue;
        for (int id : l.stops)
            if (Station *s = stationMut(id))
                ++s->lineCount;
    }

    const int nCells = m_grid.w * m_grid.h;
    const int nS = m_stations.size();
    QVector<int> cellStation(nCells, -1);
    QVector<double> cellW(nCells, 0);
    m_grid.servedFrac.fill(0, nCells);
    m_grid.coverage.fill(0, nCells);

    // 1. Zone de chalandise : chaque cellule se rattache à la station active la plus accessible.
    for (int si = 0; si < nS; ++si) {
        const Station &s = m_stations[si];
        if (s.lineCount == 0 || stationClosed(s.id))
            continue;
        const int x0 = std::max(0, int((s.pos.x() - Rules::WalkMax - m_grid.origin.x()) / m_grid.cell));
        const int x1 = std::min(m_grid.w - 1, int((s.pos.x() + Rules::WalkMax - m_grid.origin.x()) / m_grid.cell));
        const int y0 = std::max(0, int((s.pos.y() - Rules::WalkMax - m_grid.origin.y()) / m_grid.cell));
        const int y1 = std::min(m_grid.h - 1, int((s.pos.y() + Rules::WalkMax - m_grid.origin.y()) / m_grid.cell));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int c = y * m_grid.w + x;
                const double w = walkWeight(dist(m_grid.center(c), s.pos));
                if (w > cellW[c]) {
                    cellW[c] = w;
                    cellStation[c] = si;
                }
            }
    }
    m_coveredResidents = 0;
    for (int c = 0; c < nCells; ++c) {
        m_grid.coverage[c] = cellW[c];
        if (cellStation[c] < 0)
            continue;
        Station &s = m_stations[cellStation[c]];
        s.catchPop += m_grid.pop[c] * cellW[c];
        s.catchJobs += m_grid.jobs[c] * cellW[c];
        s.potential += m_grid.potential[c] * cellW[c] * demandFactorAt(m_grid.center(c)) * globalDemandFactor();
        m_coveredResidents += m_grid.pop[c] * cellW[c];
    }

    // 2. Graphe (station, ligne) pour calculer les temps de parcours avec correspondances.
    const int nL = m_lines.size();
    QVector<QVector<int>> nodeOf(nL);      // nodeOf[l][k] = noeud de l'arrêt k
    QVector<QVector<int>> stationNodes(nS);
    QVector<int> nodeLine;
    for (int li = 0; li < nL; ++li) {
        const Line &l = m_lines[li];
        if (l.segmentCount() == 0)
            continue;
        for (int id : l.stops) {
            const int si = stationIndex(id);
            nodeOf[li] << nodeLine.size();
            stationNodes[si] << nodeLine.size();
            nodeLine << li;
        }
    }
    const int nN = nodeLine.size();
    QVector<QVector<Edge>> adj(nN);
    for (int li = 0; li < nL; ++li) {
        const Line &l = m_lines[li];
        for (const Leg &g : l.legs) {
            if (g.seg < 0)
                continue;
            const int n = l.stops.size();
            const int a = g.dir == 0 ? g.seg : (g.seg + 1) % n;
            const int b = g.dir == 0 ? (g.seg + 1) % n : g.seg;
            adj[nodeOf[li][a]] << Edge{nodeOf[li][b], g.dur + Rules::Dwell, {li, g.seg, g.dir}};
        }
    }
    for (int si = 0; si < nS; ++si)
        for (int a : stationNodes[si])
            for (int b : stationNodes[si])
                if (a != b)
                    adj[a] << Edge{b, Rules::TransferPenalty + m_lines[nodeLine[b]].headwayMin / 2, {-1, si, 0}};

    // 3. Attractivité des destinations (modèle gravitaire)
    QVector<double> denom(nS, 0);
    for (int si = 0; si < nS; ++si) {
        if (m_stations[si].lineCount == 0)
            continue;
        for (int c = 0; c < nCells; ++c) {
            const double a = m_grid.jobs[c] + 0.25 * m_grid.pop[c];
            if (a > 0)
                denom[si] += a * std::exp(-dist(m_grid.center(c), m_stations[si].pos) / Rules::GravityLength);
        }
    }

    // 4. Plus courts chemins + demande origine/destination
    QVector<OD> ods;
    const double inf = std::numeric_limits<double>::infinity();
    for (int si = 0; si < nS; ++si) {
        const Station &from = m_stations[si];
        if (from.lineCount == 0 || from.potential <= 0 || denom[si] <= 0)
            continue;
        QVector<double> d(nN, inf);
        QVector<int> predNode(nN, -1);
        QVector<Traversal> predTr(nN);
        using Item = std::pair<double, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (int nd : stationNodes[si]) {
            d[nd] = m_lines[nodeLine[nd]].headwayMin / 2;
            pq.push({d[nd], nd});
        }
        while (!pq.empty()) {
            auto [du, u] = pq.top();
            pq.pop();
            if (du > d[u])
                continue;
            for (const Edge &e : adj[u]) {
                if (du + e.cost < d[e.to]) {
                    d[e.to] = du + e.cost;
                    predNode[e.to] = u;
                    predTr[e.to] = e.tr;
                    pq.push({d[e.to], e.to});
                }
            }
        }
        for (int sj = 0; sj < nS; ++sj) {
            const Station &to = m_stations[sj];
            if (sj == si || to.lineCount == 0 || stationClosed(to.id))
                continue;
            int best = -1;
            for (int nd : stationNodes[sj])
                if (d[nd] < inf && (best < 0 || d[nd] < d[best]))
                    best = nd;
            if (best < 0)
                continue;
            const double dij = dist(from.pos, to.pos);
            const double attract = to.catchJobs + 0.25 * to.catchPop;
            const double metroTime = d[best] + 6;     // + marche d'accès aux deux bouts
            const double altTime = dij / 200.0 + 10;  // bus / voiture en ville (~12 km/h porte à porte)
            double share = Rules::MaxModeShare / (1 + std::exp((metroTime - altTime) / 5.0));
            if (dij < 1000)
                share *= dij / 1000.0; // trajets courts : on marche
            const double demand = from.potential * attract * std::exp(-dij / Rules::GravityLength) / denom[si] * share;
            if (demand < 0.01)
                continue;
            OD od{si, sj, demand, 0, {}};
            for (int nd = best; predNode[nd] >= 0; nd = predNode[nd])
                od.path.prepend(predTr[nd]);
            ods << od;
        }
    }

    // 5. Affectation : charge demandée, puis limitation par la capacité
    for (const OD &od : ods)
        for (const Traversal &t : od.path)
            if (t.line >= 0)
                m_lines[t.line].load[t.dir][t.seg] += od.demand;
    QVector<double> crowd(nL, 1);
    for (int li = 0; li < nL; ++li) {
        Line &l = m_lines[li];
        for (int dir = 0; dir < 2; ++dir)
            for (double v : l.load[dir])
                l.maxLoad = std::max(l.maxLoad, v);
        crowd[li] = l.maxLoad <= 0 ? 1 : std::min(1.0, l.capacity / l.maxLoad);
    }
    m_totalServed = 0;
    for (OD &od : ods) {
        double f = 1;
        for (const Traversal &t : od.path)
            if (t.line >= 0)
                f = std::min(f, crowd[t.line]);
        od.served = od.demand * f;
        m_totalServed += od.served;
        m_stations[od.from].served += od.served;
        m_stations[od.from].boardings += od.served;
        int lastLine = -1;
        for (const Traversal &t : od.path) {
            if (t.line < 0) {
                m_stations[t.seg].boardings += od.served;
            } else if (t.line != lastLine) {
                m_lines[t.line].ridership += od.served;
                lastLine = t.line;
            }
        }
    }

    // 6. Demande captée par cellule (pour l'overlay)
    double totalPot = 0, servedPot = 0;
    for (int c = 0; c < nCells; ++c) {
        totalPot += m_grid.potential[c];
        if (cellStation[c] < 0)
            continue;
        const Station &s = m_stations[cellStation[c]];
        const double capture = s.potential > 0 ? std::min(1.0, s.served / s.potential / Rules::CaptureTarget) : 0;
        m_grid.servedFrac[c] = cellW[c] * capture;
        servedPot += m_grid.potential[c] * m_grid.servedFrac[c];
    }
    m_totalPotential = totalPot;
    m_satisfaction = totalPot > 0 ? servedPot / totalPot : 0;

    // 7. Finances (par mois)
    // voyages/jour = pointe × (heures de pointe + heures creuses × maintien de la fréquentation) ;
    // en heures creuses, moins de rames = attente plus longue = un peu moins de voyageurs
    double weighted = 0, ridersSum = 0, stockCost = 0;
    for (const Line &l : m_lines) {
        weighted += l.ridership * (Rules::PeakHours + Rules::OffPeakHours * std::sqrt(l.offPeak));
        ridersSum += l.ridership;
        stockCost += lineMonthlyCost(l);
    }
    const double dailyFactor = ridersSum > 0 ? weighted / ridersSum : Rules::DailyFactor;
    m_monthlyRevenue = m_totalServed * dailyFactor * m_fare * Rules::DaysPerMonth * Rules::RevenueBoost;
    m_monthlyCost = (stockCost + nS * Rules::StationDailyCost * Rules::DaysPerMonth) * opCostFactor();
    // subvention de la ville : jusqu'à 40 % de l'exploitation si le métro capte bien la demande
    m_monthlySubsidy = m_monthlyCost * Rules::SubsidyShare * std::clamp((m_satisfaction - 0.1) / 0.3, 0.0, 1.0);

    trackEdit();
    checkGoals();
    emit networkChanged();
}

void Metro::advance(double realSeconds, double speed)
{
    if (speed <= 0)
        return;
    double dt = realSeconds * speed;
    m_simMinutes += dt; // 1 s réelle = 1 min de circulation à ×1
    // découpage aux changements de semaine : les événements se décomptent chaque semaine,
    // et chaque mois (4 semaines) a son bilan exact
    while (dt > 0) {
        const int weeksDone = int(std::floor(m_financeSeconds / Rules::SecondsPerWeek + 1e-9));
        const double toWeekEnd = (weeksDone + 1) * Rules::SecondsPerWeek - m_financeSeconds;
        const double step = std::min(dt, std::max(1e-9, toWeekEnd));
        const double frac = step / Rules::SecondsPerMonth;
        m_current.revenue += m_monthlyRevenue / 1e6 * frac;
        m_current.operating += m_monthlyCost / 1e6 * frac;
        m_totalRevenue += m_monthlyRevenue / 1e6 * frac;
        m_totalOperating += m_monthlyCost / 1e6 * frac;
        m_current.subsidy += m_monthlySubsidy / 1e6 * frac;
        m_money += (m_monthlyRevenue + m_monthlySubsidy - m_monthlyCost) / 1e6 * frac;
        m_financeSeconds += step;
        dt -= step;
        if (m_financeSeconds >= (weeksDone + 1) * Rules::SecondsPerWeek - 1e-9) {
            tickEvents(); // fin de semaine
            checkGoals();
            if (m_financeSeconds >= (m_history.size() + 1) * Rules::SecondsPerMonth - 1e-9)
                closeMonth();
        }
    }
}

void Metro::charge(double amount)
{
    if (m_sandbox || m_freeBuild) // bac à sable ou réseau réel importé : rien n'est facturé
        return;
    m_money -= amount;
    m_current.investment += amount;
    m_totalInvested += amount;
}

void Metro::closeMonth()
{
    m_current.month = m_history.size() + 1;
    m_current.money = m_money;
    m_current.riders = m_totalServed;
    m_current.capture = m_satisfaction;
    // score du mois et série de mois bénéficiaires
    m_lastMonthPoints = m_sandbox ? 0 : monthPoints();
    m_score += m_lastMonthPoints;
    m_profitStreak = m_current.revenue > m_current.operating && m_current.revenue > 0 ? m_profitStreak + 1 : 0;
    m_current.score = m_score;
    m_current.monthPoints = m_lastMonthPoints;
    monthlyEconomy(); // emprunts, vieillissement du matériel, croissance urbaine
    m_current.debt = debt();
    m_current.money = m_money;
    m_current.population = m_population;
    m_history << m_current;
    m_current = MonthRecord();
    checkGoals();
    rollEvents(); // les nouveaux événements se tirent en fin de mois
}

MonthRecord Metro::currentMonth() const
{
    MonthRecord r = m_current;
    r.month = m_history.size() + 1;
    r.money = m_money;
    r.riders = m_totalServed;
    r.capture = m_satisfaction;
    r.monthPoints = monthPoints(); // estimation au rythme actuel
    r.score = m_score + r.monthPoints;
    r.debt = debt();
    r.population = m_population;
    return r;
}

double Metro::lineMonthlyRevenue(const Line &l) const
{
    // une recette par voyage, répartie entre les lignes empruntées au prorata de leur fréquentation
    double total = 0;
    for (const Line &o : m_lines)
        total += o.ridership;
    return total > 0 ? m_monthlyRevenue * l.ridership / total : 0;
}

double Metro::lineMonthlyCost(const Line &l) const
{
    // les rames retirées aux heures creuses coûtent moins (énergie, conduite), l'entretien reste
    return l.wagons * l.trains * Rules::WagonDailyCost * Rules::DaysPerMonth * (0.35 + 0.65 * l.offPeak)
           * maintenanceCostFactor();
}

double Metro::serviceAt(double minutes, const Line &l)
{
    const double h = minutes / 60;
    if (h >= 1 && h < 5)
        return 0; // métro fermé la nuit
    const bool peak = (h >= 7 && h < 9.5) || (h >= 16.5 && h < 19.5);
    return peak ? 1.0 : std::max(1.0 / std::max(1, l.trains), l.offPeak);
}

QVector<TrainVis> Metro::trains(double wagonSpacing) const
{
    QVector<TrainVis> out;
    for (const Line &l : m_lines) {
        if (l.legs.isEmpty() || l.cycleMin <= 0 || lineCapacityFactor(l.id) <= 0) // ligne en grève : rames au dépôt
            continue;
        const int running = int(std::round(l.trains * serviceAt(clockMinutes(), l)));
        for (int i = 0; i < running; ++i) {
            const double phase = std::fmod(m_simMinutes + i * l.cycleMin / running, l.cycleMin);
            auto it = std::upper_bound(l.legs.begin(), l.legs.end(), phase,
                                       [](double t, const Leg &g) { return t < g.t0; });
            const Leg &g = *(it == l.legs.begin() ? it : it - 1);
            const double f = g.dur > 0 ? std::clamp((phase - g.t0) / g.dur, 0.0, 1.0) : 0;
            // vitesse non linéaire : accélération / freinage
            const double eased = f * f * (3 - 2 * f);
            TrainVis tv;
            // à quai : on se place au début du trajet suivant ; en marche : le long du tracé
            const int seg = g.seg >= 0 ? g.seg : g.refSeg;
            const int dir = g.seg >= 0 ? g.dir : g.refDir;
            if (seg < 0 || seg >= l.paths.size())
                continue;
            const SegPath &sp = l.paths[seg];
            const double s = g.seg >= 0 ? eased * sp.length() : 0;
            const double center = dir == 0 ? s : sp.length() - s;
            for (int w = 0; w < l.wagons; ++w) {
                double ang = 0;
                tv.wagonPos << sp.pointAt(center + (w - (l.wagons - 1) / 2.0) * wagonSpacing, &ang);
                tv.wagonAngle << ang;
            }
            tv.wagons = l.wagons;
            tv.lineId = l.id;
            tv.seg = seg;
            tv.t = sp.length() > 0 ? std::clamp(center / sp.length(), 0.0, 1.0) : 0;
            tv.color = l.color;
            if (g.refSeg >= 0 && g.refSeg < l.load[g.refDir].size() && l.capacity > 0)
                tv.loadRatio = l.load[g.refDir][g.refSeg] / l.capacity;
            out << tv;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sauvegarde
// ---------------------------------------------------------------------------

static QJsonArray viaJson(const Line &l)
{
    QJsonArray arr;
    for (auto it = l.via.begin(); it != l.via.end(); ++it) {
        QJsonArray xy;
        for (const QPointF &p : it.value())
            xy << p.x() << p.y();
        arr << QJsonObject{{"a", int(it.key() >> 32)}, {"b", int(it.key() & 0xffffffff)}, {"pts", xy}};
    }
    return arr;
}

QJsonObject Metro::networkJson() const
{
    QJsonArray st, ln;
    for (const Station &s : m_stations)
        st << QJsonObject{{"id", s.id}, {"name", s.name}, {"x", s.pos.x()}, {"y", s.pos.y()}};
    for (const Line &l : m_lines) {
        QJsonArray stops;
        for (int id : l.stops)
            stops << id;
        ln << QJsonObject{{"id", l.id},         {"code", l.code},     {"name", l.name},     {"color", l.color.name()},
                          {"wagons", l.wagons}, {"trains", l.trains}, {"loop", l.loop},
                          {"paid", l.paidLength}, {"stops", stops}, {"via", viaJson(l)},
                          {"offpeak", l.offPeak}, {"age", l.stockAge}, {"elevated", [&] {
                              QJsonArray a;
                              for (quint64 k : l.elevated)
                                  a << QJsonArray{int(k >> 32), int(k & 0xffffffff)};
                              return a;
                          }()}};
    }
    return QJsonObject{{"stations", st}, {"lines", ln}, {"nextStation", m_nextStationId}, {"nextLine", m_nextLineId}};
}

QJsonObject Metro::save() const
{
    QJsonObject o = networkJson();
    QJsonArray hist;
    for (const MonthRecord &r : m_history)
        hist << QJsonArray{r.money, r.revenue, r.operating, r.investment, r.riders, r.capture,
                           r.score, r.monthPoints, r.goalPoints, r.subsidy, r.loanPaid, r.debt, r.population};
    const QJsonObject rest{{"money", m_money},
                       {"finance", m_financeSeconds},
                       {"history", hist},
                       {"current", QJsonArray{m_current.revenue, m_current.operating, m_current.investment, m_current.goalPoints,
                                              m_current.subsidy, m_current.loanPaid}},
                       {"invested", m_totalInvested},
                       {"totalRevenue", m_totalRevenue},
                       {"totalOperating", m_totalOperating},
                       {"simMinutes", m_simMinutes},
                       {"version", 2},
                       {"events", eventsJson()},
                       {"goals", goalsJson()},
                       {"sandbox", m_sandbox},
                       {"economy", economyJson()},
                       {"mission", missionJson()}};
    for (auto it = rest.begin(); it != rest.end(); ++it)
        o.insert(it.key(), it.value());
    return o;
}

void Metro::loadNetwork(const QJsonObject &o)
{
    m_stations.clear();
    m_lines.clear();
    for (const QJsonValue &v : o.value("stations").toArray()) {
        const QJsonObject so = v.toObject();
        Station s;
        s.id = so.value("id").toInt();
        s.name = so.value("name").toString();
        s.pos = QPointF(so.value("x").toDouble(), so.value("y").toDouble());
        m_stations << s;
    }
    for (const QJsonValue &v : o.value("lines").toArray()) {
        const QJsonObject lo = v.toObject();
        Line l;
        l.id = lo.value("id").toInt();
        l.name = lo.value("name").toString();
        l.code = lo.value("code").toString(QString::number(l.id));
        l.color = QColor(lo.value("color").toString());
        l.wagons = std::clamp(lo.value("wagons").toInt(4), Rules::MinWagons, Rules::MaxWagons);
        l.trains = std::max(1, lo.value("trains").toInt(2));
        l.loop = lo.value("loop").toBool();
        l.paidLength = lo.value("paid").toDouble();
        l.offPeak = std::clamp(lo.value("offpeak").toDouble(1), 0.5, 1.0);
        l.stockAge = lo.value("age").toDouble(0);
        for (const QJsonValue &ev : lo.value("elevated").toArray()) {
            const QJsonArray pair = ev.toArray();
            if (pair.size() == 2)
                l.elevated.insert(pairKey(pair[0].toInt(), pair[1].toInt()));
        }
        for (const QJsonValue &vv : lo.value("via").toArray()) {
            const QJsonObject vo = vv.toObject();
            const QJsonArray xy = vo.value("pts").toArray();
            QVector<QPointF> pts;
            for (int i = 0; i + 1 < xy.size(); i += 2)
                pts << QPointF(xy[i].toDouble(), xy[i + 1].toDouble());
            if (!pts.isEmpty())
                l.via[pairKey(vo.value("a").toInt(), vo.value("b").toInt())] = pts;
        }
        for (const QJsonValue &sv : lo.value("stops").toArray())
            if (stationIndex(sv.toInt()) >= 0)
                l.stops << sv.toInt();
        // ancienne sauvegarde (avant les viaducs) : le tracé est considéré comme payé au nouveau barème
        if (!lo.contains("elevated"))
            l.paidLength = trackUnits(l);
        m_lines << l;
    }
    m_nextStationId = o.value("nextStation").toInt(m_stations.size() + 1);
    m_nextLineId = o.value("nextLine").toInt(m_lines.size() + 1);
}

bool Metro::load(const QJsonObject &o)
{
    loadNetwork(o);
    m_sandbox = o.value("sandbox").toBool();
    m_money = o.value("money").toDouble(Rules::StartMoney);
    m_financeSeconds = o.value("finance").toDouble();
    m_history.clear();
    for (const QJsonValue &v : o.value("history").toArray()) {
        const QJsonArray a = v.toArray();
        MonthRecord r;
        r.month = m_history.size() + 1;
        r.money = a[0].toDouble();
        r.revenue = a[1].toDouble();
        r.operating = a[2].toDouble();
        r.investment = a[3].toDouble();
        r.riders = a[4].toDouble();
        r.capture = a[5].toDouble();
        r.score = a.size() > 6 ? a[6].toDouble() : 0; // anciennes sauvegardes : pas d'historique du score
        r.monthPoints = a.size() > 7 ? a[7].toDouble() : 0;
        r.goalPoints = a.size() > 8 ? a[8].toDouble() : 0;
        r.subsidy = a.size() > 12 ? a[9].toDouble() : 0;
        r.loanPaid = a.size() > 12 ? a[10].toDouble() : 0;
        r.debt = a.size() > 12 ? a[11].toDouble() : 0;
        r.population = a.size() > 12 ? a[12].toDouble() : 0;
        m_history << r;
    }
    const QJsonArray cur = o.value("current").toArray();
    m_current = MonthRecord();
    if (cur.size() >= 3) {
        m_current.revenue = cur[0].toDouble();
        m_current.operating = cur[1].toDouble();
        m_current.investment = cur[2].toDouble();
    }
    if (cur.size() > 3)
        m_current.goalPoints = cur[3].toDouble();
    if (cur.size() > 5) {
        m_current.subsidy = cur[4].toDouble();
        m_current.loanPaid = cur[5].toDouble();
    }
    m_totalInvested = o.value("invested").toDouble();
    // anciennes sauvegardes : cumuls reconstitués à partir de l'historique
    double histRevenue = m_current.revenue, histOperating = m_current.operating;
    for (const MonthRecord &r : m_history) {
        histRevenue += r.revenue;
        histOperating += r.operating;
    }
    m_totalRevenue = o.value("totalRevenue").toDouble(histRevenue);
    m_totalOperating = o.value("totalOperating").toDouble(histOperating);
    m_simMinutes = o.value("simMinutes").toDouble(0);
    loadEvents(o);
    loadGoals(o.value("goals").toObject()); // absent (ancienne sauvegarde) : objectifs repris du début
    loadEconomy(o.value("economy").toObject());
    loadMission(o.value("mission").toObject());
    buildGrid(); // croissance urbaine sauvegardée
    // anciennes sauvegardes sans historique : on repart du mois en cours
    m_financeSeconds = std::max(m_financeSeconds, m_history.size() * Rules::SecondsPerMonth);
    if (m_financeSeconds >= (m_history.size() + 1) * Rules::SecondsPerMonth)
        m_financeSeconds = m_history.size() * Rules::SecondsPerMonth;
    recompute();
    resetUndo();
    return true;
}

// ---------------------------------------------------------------------------
// Annuler / refaire
// ---------------------------------------------------------------------------

void Metro::trackEdit()
{
    if (m_restoring || !m_city)
        return;
    const QJsonObject net = networkJson();
    if (net == m_lastState.net) {
        m_lastState.invested = m_totalInvested;
        return;
    }
    m_undo << m_lastState;
    if (m_undo.size() > 100)
        m_undo.removeFirst();
    m_redo.clear();
    m_lastState = {net, m_totalInvested};
    emit undoChanged();
}

void Metro::resetUndo()
{
    m_undo.clear();
    m_redo.clear();
    m_lastState = {networkJson(), m_totalInvested};
    emit undoChanged();
}

// Remet le réseau dans l'état donné ; les investissements faits depuis sont remboursés (ou refacturés)
void Metro::restoreState(const QJsonObject &net)
{
    m_restoring = true;
    loadNetwork(net);
    recompute();
    m_restoring = false;
    m_lastState = {networkJson(), m_totalInvested};
    emit undoChanged();
}

bool Metro::undo()
{
    if (m_undo.isEmpty())
        return false;
    const UndoState target = m_undo.last();
    const double cost = target.invested - m_totalInvested; // < 0 : remboursement intégral
    if (!m_sandbox && cost > 0 && m_money < cost) { // annuler une démolition : on rachète au prix remboursé
        emit message(tr("Budget insuffisant pour annuler (%1 M€ requis)").arg(cost, 0, 'f', 1));
        return false;
    }
    m_undo.removeLast();
    m_redo << UndoState{m_lastState.net, m_totalInvested};
    charge(cost);
    restoreState(target.net);
    return true;
}

bool Metro::redo()
{
    if (m_redo.isEmpty())
        return false;
    const UndoState target = m_redo.last();
    const double cost = target.invested - m_totalInvested;
    if (!m_sandbox && cost > 0 && m_money < cost) {
        emit message(tr("Budget insuffisant pour rétablir (%1 M€ requis)").arg(cost, 0, 'f', 1));
        return false;
    }
    m_redo.removeLast();
    m_undo << UndoState{m_lastState.net, m_totalInvested};
    charge(cost);
    restoreState(target.net);
    return true;
}

// ---------------------------------------------------------------------------
// Bac à sable
// ---------------------------------------------------------------------------

void Metro::setSandbox(bool on)
{
    if (on == m_sandbox)
        return;
    m_sandbox = on;
    if (on) { // ni événements ni objectifs
        m_events.clear();
        emit eventsChanged();
    }
    recompute();
    emit goalsChanged();
}
