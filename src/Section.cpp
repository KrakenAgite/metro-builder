// Vue en coupe : tunnels et viaducs par inter-station, passages sous les cours d'eau, profil des lignes.
#include "Metro.h"

#include <cmath>

namespace {

quint64 segKey(int a, int b)
{
    return (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b));
}

double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0;
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

constexpr double SampleStep = 20; // m entre deux échantillons le long d'un tracé

} // namespace

void Metro::buildWaterIndex()
{
    m_waterIndex.clear();
    m_riverIndex.clear();
    if (!m_city)
        return;
    for (const QPolygonF &w : m_city->water)
        if (w.size() >= 3)
            m_waterIndex << qMakePair(w.boundingRect(), w);
    for (const QPolygonF &r : m_city->rivers)
        if (r.size() >= 2)
            m_riverIndex << qMakePair(r.boundingRect().adjusted(-30, -30, 30, 30), r);
}

bool Metro::isWater(const QPointF &p) const
{
    for (const auto &w : m_waterIndex)
        if (w.first.contains(p) && w.second.containsPoint(p, Qt::OddEvenFill))
            return true;
    for (const auto &r : m_riverIndex) // cours d'eau sans surface : 25 m de part et d'autre
        if (r.first.contains(p))
            for (int i = 0; i + 1 < r.second.size(); ++i)
                if (distToSegment(p, r.second[i], r.second[i + 1]) < 25)
                    return true;
    return false;
}

bool Metro::segmentElevated(const Line &l, int seg) const
{
    if (seg < 0 || seg >= l.segmentCount())
        return false;
    return l.elevated.contains(segKey(l.stops[seg], l.stops[(seg + 1) % l.stops.size()]));
}

double Metro::segmentUnits(const SegPath &path, bool elevated) const
{
    // longueur pondérée : viaduc moins cher, pont ou tunnel sous le fleuve plus cher
    const double len = path.length();
    if (len <= 0)
        return 0;
    const int n = std::max(1, int(std::ceil(len / SampleStep)));
    double units = 0;
    for (int i = 0; i < n; ++i) {
        const bool water = isWater(path.pointAt((i + 0.5) * len / n));
        const double f = elevated ? (water ? Rules::BridgeFactor : Rules::ViaductFactor)
                                  : (water ? Rules::UnderwaterFactor : 1.0);
        units += f * len / n;
    }
    return units;
}

double Metro::trackUnits(const Line &l) const
{
    const QVector<SegPath> paths = buildPaths(l);
    double units = 0;
    for (int k = 0; k < paths.size(); ++k)
        units += segmentUnits(paths[k], segmentElevated(l, k));
    return units;
}

double Metro::segmentCost(const Line &l, int seg, bool elevated) const
{
    if (seg < 0 || seg >= l.paths.size())
        return 0;
    return segmentUnits(l.paths[seg], elevated) / 1000.0 * Rules::TrackCostKm;
}

void Metro::setSegmentElevated(int lineId, int seg, bool on)
{
    Line *l = lineMut(lineId);
    if (!l || seg < 0 || seg >= l->segmentCount() || segmentElevated(*l, seg) == on)
        return;
    const quint64 key = segKey(l->stops[seg], l->stops[(seg + 1) % l->stops.size()]);
    // passer en tunnel coûte la différence : vérifier le budget avant
    const double before = trackUnits(*l);
    if (on)
        l->elevated.insert(key);
    else
        l->elevated.remove(key);
    const double extra = (trackUnits(*l) - before) / 1000.0 * Rules::TrackCostKm * buildCostFactor();
    if (!m_sandbox && extra > 0 && m_money < extra) {
        if (on)
            l->elevated.remove(key);
        else
            l->elevated.insert(key);
        emit message(tr("Budget insuffisant pour creuser ce tunnel (%1 M€ requis)").arg(extra, 0, 'f', 1));
        emit networkChanged();
        return;
    }
    settleTrack(*l);
    recompute();
}

QVector<Metro::ProfilePoint> Metro::lineProfile(int lineId) const
{
    QVector<ProfilePoint> out;
    const Line *l = line(lineId);
    if (!l || l->paths.isEmpty())
        return out;
    double s0 = 0;
    for (int k = 0; k < l->paths.size(); ++k) {
        const SegPath &sp = l->paths[k];
        const bool up = segmentElevated(*l, k);
        const double len = sp.length();
        const int n = std::max(2, int(std::ceil(len / SampleStep)));
        for (int i = 0; i <= n; ++i) {
            if (k > 0 && i == 0)
                continue; // la station est déjà le dernier point de l'inter-station précédente
            ProfilePoint p;
            p.s = s0 + len * i / n;
            p.seg = k;
            p.elevated = up;
            p.water = isWater(sp.pointAt(len * i / n));
            p.level = up ? Rules::ViaductHeight : -(p.water ? Rules::UnderwaterDepth : Rules::TunnelDepth);
            out << p;
        }
        s0 += len;
    }
    // rampes : lissage de la voie (~250 m), les stations restent à leur niveau
    QVector<double> smooth(out.size());
    const int half = std::max(1, int(125 / SampleStep));
    for (int i = 0; i < out.size(); ++i) {
        double sum = 0;
        int count = 0;
        for (int j = std::max(0, i - half); j <= std::min<int>(out.size() - 1, i + half); ++j) {
            sum += out[j].level;
            ++count;
        }
        smooth[i] = sum / count;
    }
    for (int i = 0; i < out.size(); ++i)
        out[i].level = smooth[i];
    return out;
}
