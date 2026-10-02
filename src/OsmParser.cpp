#include "OsmParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <limits>

namespace {

enum class LandUse { Residential, Commercial, Industrial };

struct LandPoly {
    QPolygonF poly;
    QRectF box;
    LandUse use;
};

double polygonArea(const QPolygonF &p)
{
    double a = 0;
    for (int i = 0, n = p.size(); i < n; ++i) {
        const QPointF &u = p[i];
        const QPointF &v = p[(i + 1) % n];
        a += u.x() * v.y() - v.x() * u.y();
    }
    return std::abs(a) * 0.5;
}

QPointF centroidOf(const QPolygonF &p)
{
    QPointF c;
    if (p.isEmpty())
        return c;
    for (const QPointF &q : p)
        c += q;
    return c / p.size();
}

QPolygonF readGeometry(const QJsonArray &geom, const CityData &city)
{
    QPolygonF poly;
    poly.reserve(geom.size());
    for (const QJsonValue &v : geom) {
        const QJsonObject o = v.toObject();
        if (!o.contains("lat"))
            continue;
        poly << city.project(o.value("lat").toDouble(), o.value("lon").toDouble());
    }
    return poly;
}

QPolygonF reversed(const QPolygonF &p)
{
    QPolygonF r(p);
    std::reverse(r.begin(), r.end());
    return r;
}

// Assemble les membres "outer" d'une relation multipolygone en anneaux fermés.
QVector<QPolygonF> buildRings(QVector<QPolygonF> segs)
{
    QVector<QPolygonF> rings;
    while (!segs.isEmpty()) {
        QPolygonF cur = segs.takeFirst();
        bool progress = true;
        while (cur.size() > 1 && cur.first() != cur.last() && progress) {
            progress = false;
            for (int i = 0; i < segs.size(); ++i) {
                const QPolygonF &s = segs[i];
                if (s.isEmpty())
                    continue;
                if (s.first() == cur.last())
                    cur += s.mid(1);
                else if (s.last() == cur.last())
                    cur += reversed(s).mid(1);
                else if (s.last() == cur.first())
                    cur = s + cur.mid(1);
                else if (s.first() == cur.first())
                    cur = reversed(s) + cur.mid(1);
                else
                    continue;
                segs.removeAt(i);
                progress = true;
                break;
            }
        }
        if (cur.size() >= 3)
            rings << cur;
    }
    return rings;
}

RoadKind roadKind(const QString &hw)
{
    if (hw.startsWith("motorway") || hw.startsWith("trunk"))
        return RoadKind::Motorway;
    if (hw.startsWith("primary"))
        return RoadKind::Primary;
    if (hw.startsWith("secondary"))
        return RoadKind::Secondary;
    if (hw.startsWith("tertiary"))
        return RoadKind::Tertiary;
    if (hw == "service")
        return RoadKind::Service;
    return RoadKind::Residential;
}

double defaultLevels(BuildingUse use, double area)
{
    switch (use) {
    case BuildingUse::Residential: return area > 300 ? 5 : 2;
    case BuildingUse::Mixed: return area > 200 ? 5 : 3;
    case BuildingUse::Commercial: return 3;
    case BuildingUse::Industrial: return 1;
    case BuildingUse::Public: return 3;
    case BuildingUse::Other: return 1;
    }
    return 1;
}

} // namespace

ParseResult parseOverpassJson(const QByteArray &data, const QString &name, double lat0, double lon0,
                              double radiusM)
{
    ParseResult res;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (doc.isNull()) {
        res.error = QStringLiteral("JSON invalide : %1").arg(perr.errorString());
        return res;
    }
    const QJsonArray elements = doc.object().value("elements").toArray();
    if (elements.isEmpty()) {
        const QString remark = doc.object().value("remark").toString();
        res.error = remark.isEmpty() ? QStringLiteral("Aucune donnée OSM dans la réponse")
                                     : QStringLiteral("Overpass : %1").arg(remark);
        return res;
    }
    return parseOverpassElements(elements, name, lat0, lon0, radiusM);
}

ParseResult parseOverpassElements(const QJsonArray &elements, const QString &name, double lat0, double lon0,
                                  double radiusM)
{
    ParseResult res;
    if (elements.isEmpty()) {
        res.error = QStringLiteral("Aucune donnée OSM dans la réponse");
        return res;
    }
    auto city = QSharedPointer<CityData>::create();
    city->name = name;

    if (std::isnan(lat0) || std::isnan(lon0)) {
        double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
        for (const QJsonValue &ev : elements) {
            const QJsonObject b = ev.toObject().value("bounds").toObject();
            if (b.isEmpty())
                continue;
            minLat = std::min(minLat, b.value("minlat").toDouble());
            maxLat = std::max(maxLat, b.value("maxlat").toDouble());
            minLon = std::min(minLon, b.value("minlon").toDouble());
            maxLon = std::max(maxLon, b.value("maxlon").toDouble());
        }
        lat0 = (minLat + maxLat) / 2;
        lon0 = (minLon + maxLon) / 2;
    }
    city->lat0 = lat0;
    city->lon0 = lon0;

    struct PendingBuilding {
        QPolygonF poly;
        QJsonObject tags;
    };
    QVector<PendingBuilding> pending;
    QVector<LandPoly> landuse;

    auto addArea = [&](const QJsonObject &tags, const QPolygonF &poly) {
        const QString lu = tags.value("landuse").toString();
        const QString leisure = tags.value("leisure").toString();
        const QString natural = tags.value("natural").toString();
        const QString waterway = tags.value("waterway").toString();
        if (natural == "water" || waterway == "riverbank" || lu == "basin" || lu == "reservoir")
            city->water << poly;
        else if (leisure == "park" || leisure == "garden" || leisure == "pitch" || lu == "grass"
                 || lu == "recreation_ground" || lu == "meadow" || lu == "cemetery")
            city->parks << poly;
        else if (natural == "wood" || lu == "forest")
            city->forests << poly;
        else if (lu == "residential")
            landuse << LandPoly{poly, poly.boundingRect(), LandUse::Residential};
        else if (lu == "commercial" || lu == "retail")
            landuse << LandPoly{poly, poly.boundingRect(), LandUse::Commercial};
        else if (lu == "industrial")
            landuse << LandPoly{poly, poly.boundingRect(), LandUse::Industrial};
    };

    for (const QJsonValue &ev : elements) {
        const QJsonObject e = ev.toObject();
        const QString type = e.value("type").toString();
        const QJsonObject tags = e.value("tags").toObject();

        if (type == "way") {
            QPolygonF poly = readGeometry(e.value("geometry").toArray(), *city);
            if (poly.size() < 2)
                continue;
            if (tags.contains("highway")) {
                Road r;
                r.pts = poly;
                r.kind = roadKind(tags.value("highway").toString());
                r.name = tags.value("name").toString();
                city->roads << r;
            } else if (tags.contains("building")) {
                if (poly.size() >= 3)
                    pending << PendingBuilding{poly, tags};
            } else {
                const QString ww = tags.value("waterway").toString();
                if (ww == "river" || ww == "canal")
                    city->rivers << poly;
                else if (poly.size() >= 3)
                    addArea(tags, poly);
            }
        } else if (type == "relation") {
            QVector<QPolygonF> outers;
            for (const QJsonValue &mv : e.value("members").toArray()) {
                const QJsonObject m = mv.toObject();
                if (m.value("type").toString() != "way")
                    continue;
                const QString role = m.value("role").toString();
                if (role != "outer" && !role.isEmpty())
                    continue;
                QPolygonF p = readGeometry(m.value("geometry").toArray(), *city);
                if (p.size() >= 2)
                    outers << p;
            }
            const QVector<QPolygonF> rings = buildRings(outers);
            for (const QPolygonF &ring : rings) {
                if (tags.contains("building"))
                    pending << PendingBuilding{ring, tags};
                else
                    addArea(tags, ring);
            }
        }
    }

    // Classification des bâtiments et estimation habitants / emplois.
    city->buildings.reserve(pending.size());
    for (const PendingBuilding &pb : pending) {
        Building b;
        b.poly = pb.poly;
        b.centroid = centroidOf(pb.poly);
        const QString bt = pb.tags.value("building").toString();
        const double area = polygonArea(pb.poly);

        static const QStringList resTypes = {"house", "detached", "apartments", "residential", "terrace",
                                             "semidetached_house", "dormitory", "bungalow", "semi"};
        static const QStringList comTypes = {"commercial", "office", "retail", "supermarket", "hotel",
                                             "kiosk", "mall"};
        static const QStringList indTypes = {"industrial", "warehouse", "factory", "manufacture"};
        static const QStringList pubTypes = {"school", "university", "college", "hospital", "public",
                                             "civic", "government", "train_station", "transportation",
                                             "church", "cathedral", "museum", "sports_hall", "stadium",
                                             "kindergarten", "townhall"};
        static const QStringList otherTypes = {"garage", "garages", "shed", "roof", "carport", "hut",
                                               "greenhouse", "ruins", "construction", "parking", "service"};

        if (resTypes.contains(bt))
            b.use = BuildingUse::Residential;
        else if (comTypes.contains(bt))
            b.use = BuildingUse::Commercial;
        else if (indTypes.contains(bt))
            b.use = BuildingUse::Industrial;
        else if (pubTypes.contains(bt))
            b.use = BuildingUse::Public;
        else if (otherTypes.contains(bt))
            b.use = BuildingUse::Other;
        else {
            // building=yes : on s'appuie sur l'occupation du sol
            b.use = area > 150 ? BuildingUse::Mixed : BuildingUse::Residential;
            for (const LandPoly &lp : landuse) {
                if (!lp.box.contains(b.centroid) || !lp.poly.containsPoint(b.centroid, Qt::OddEvenFill))
                    continue;
                b.use = lp.use == LandUse::Residential ? (area > 600 ? BuildingUse::Mixed : BuildingUse::Residential)
                      : lp.use == LandUse::Commercial  ? BuildingUse::Commercial
                                                       : BuildingUse::Industrial;
                break;
            }
        }
        if (b.use == BuildingUse::Residential
            && (pb.tags.contains("shop") || pb.tags.contains("amenity") || pb.tags.contains("office")))
            b.use = BuildingUse::Mixed;

        double levels = pb.tags.value("building:levels").toString().toDouble();
        if (levels <= 0 || levels > 60)
            levels = defaultLevels(b.use, area);
        const double floor = area < 25 ? 0 : area * levels;

        switch (b.use) {
        case BuildingUse::Residential: b.residents = floor / 45.0; break;
        case BuildingUse::Mixed:
            b.residents = floor * 0.7 / 45.0;
            b.jobs = floor * 0.3 / 30.0;
            break;
        case BuildingUse::Commercial: b.jobs = floor / 30.0; break;
        case BuildingUse::Industrial: b.jobs = floor / 90.0; break;
        case BuildingUse::Public: b.jobs = floor / 50.0; break;
        case BuildingUse::Other: break;
        }
        city->totalResidents += b.residents;
        city->totalJobs += b.jobs;
        city->buildings << b;
    }

    QRectF bounds;
    for (const Building &b : city->buildings)
        bounds = bounds.united(b.poly.boundingRect());
    if (bounds.isEmpty())
        for (const Road &r : city->roads)
            bounds = bounds.united(r.pts.boundingRect());
    if (radiusM > 0)
        bounds = bounds.intersected(QRectF(-radiusM, -radiusM, 2 * radiusM, 2 * radiusM));
    city->bounds = bounds;

    if (city->buildings.isEmpty() && city->roads.isEmpty()) {
        res.error = QStringLiteral("Aucune rue ni bâtiment trouvé");
        return res;
    }
    res.city = city;
    return res;
}
