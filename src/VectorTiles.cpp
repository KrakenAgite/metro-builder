#include "VectorTiles.h"

#include <QHash>
#include <QSet>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace VectorTiles {

namespace {

// --- Lecteur protobuf minimal ------------------------------------------------

struct Pb {
    const uchar *p = nullptr;
    const uchar *end = nullptr;

    bool atEnd() const { return p >= end; }
    quint64 varint()
    {
        quint64 r = 0;
        int shift = 0;
        while (p < end) {
            const uchar b = *p++;
            r |= quint64(b & 0x7f) << shift;
            if (!(b & 0x80))
                break;
            shift += 7;
        }
        return r;
    }
    bool next(int &field, int &wire)
    {
        if (p >= end)
            return false;
        const quint64 key = varint();
        field = int(key >> 3);
        wire = int(key & 7);
        return true;
    }
    Pb sub()
    {
        const quint64 len = varint();
        Pb s{p, p + std::min<quint64>(len, quint64(end - p))};
        p = s.end;
        return s;
    }
    QByteArray bytes()
    {
        Pb s = sub();
        return QByteArray(reinterpret_cast<const char *>(s.p), int(s.end - s.p));
    }
    void skip(int wire)
    {
        switch (wire) {
        case 0: varint(); break;
        case 1: p += 8; break;
        case 2: sub(); break;
        case 5: p += 4; break;
        default: p = end; break;
        }
        p = std::min(p, end);
    }
};

int zigzag(quint64 v)
{
    return int((v >> 1) ^ (~(v & 1) + 1));
}

QVariant readValue(Pb v)
{
    int f, w;
    QVariant out;
    while (v.next(f, w)) {
        switch (f) {
        case 1: out = QString::fromUtf8(v.bytes()); break;
        case 2: {
            float x;
            memcpy(&x, v.p, 4);
            v.p += 4;
            out = double(x);
            break;
        }
        case 3: {
            double x;
            memcpy(&x, v.p, 8);
            v.p += 8;
            out = x;
            break;
        }
        case 4: out = double(qint64(v.varint())); break;
        case 5: out = double(v.varint()); break;
        case 6: out = double(zigzag(v.varint())); break;
        case 7: out = v.varint() != 0; break;
        default: v.skip(w); break;
        }
    }
    return out;
}

struct Feature {
    int type = 0; // 1 point, 2 ligne, 3 polygone
    QVector<quint32> tags;
    Pb geometry;
};

// Décode la géométrie en parties (anneaux ou lignes), en coordonnées de tuile
QVector<QPolygonF> decodeGeometry(Pb g, int type)
{
    QVector<QPolygonF> parts;
    QPolygonF cur;
    int x = 0, y = 0;
    while (!g.atEnd()) {
        const quint32 cmd = quint32(g.varint());
        const int id = cmd & 7;
        const int count = int(cmd >> 3);
        if (id == 1 || id == 2) {
            for (int i = 0; i < count && !g.atEnd(); ++i) {
                x += zigzag(g.varint());
                y += zigzag(g.varint());
                if (id == 1) {
                    if (!cur.isEmpty())
                        parts << cur;
                    cur = QPolygonF();
                }
                cur << QPointF(x, y);
            }
        } else if (id == 7) {
            if (type == 3 && !cur.isEmpty())
                cur << cur.first();
        } else {
            break;
        }
    }
    if (!cur.isEmpty())
        parts << cur;
    return parts;
}

double signedArea(const QPolygonF &p)
{
    double a = 0;
    for (int i = 0, n = p.size(); i + 1 < n; ++i)
        a += p[i].x() * p[i + 1].y() - p[i + 1].x() * p[i].y();
    return a / 2;
}

QPointF centroidOf(const QPolygonF &p)
{
    QPointF c;
    for (const QPointF &q : p)
        c += q;
    return p.isEmpty() ? c : c / p.size();
}

RoadKind roadKind(const QString &cls)
{
    if (cls == "motorway" || cls == "trunk")
        return RoadKind::Motorway;
    if (cls == "primary")
        return RoadKind::Primary;
    if (cls == "secondary")
        return RoadKind::Secondary;
    if (cls == "tertiary")
        return RoadKind::Tertiary;
    if (cls == "service")
        return RoadKind::Service;
    return RoadKind::Residential;
}

double polygonArea(const QPolygonF &p)
{
    return std::abs(signedArea(p));
}

} // namespace

// ---------------------------------------------------------------------------

QVector<TileId> tilesForArea(double lat, double lon, double radiusM, int zoom)
{
    return tilesForRect(lat, lon, QRectF(-radiusM, -radiusM, 2 * radiusM, 2 * radiusM), zoom);
}

QVector<TileId> tilesForRect(double lat0, double lon0, const QRectF &area, int zoom)
{
    // repère de la ville : x vers l'est, y vers le sud (cf. CityData::project)
    const double ky = 110540.0, kx = 111320.0 * std::cos(lat0 * M_PI / 180.0);
    const double north = lat0 - area.top() / ky, south = lat0 - area.bottom() / ky;
    const double west = lon0 + area.left() / kx, east = lon0 + area.right() / kx;
    const double n = std::pow(2.0, zoom);
    auto tx = [n](double lo) { return int(std::floor((lo + 180.0) / 360.0 * n)); };
    auto ty = [n](double la) {
        const double r = la * M_PI / 180.0;
        return int(std::floor((1.0 - std::asinh(std::tan(r)) / M_PI) / 2.0 * n));
    };
    QVector<TileId> out;
    for (int y = ty(north); y <= ty(south); ++y)
        for (int x = tx(west); x <= tx(east); ++x)
            out << TileId{zoom, x, y};
    return out;
}

TileData decode(const QByteArray &pbf, const TileId &id, double lat0, double lon0)
{
    TileData out;
    CityData proj;
    proj.lat0 = lat0;
    proj.lon0 = lon0;
    const double n = std::pow(2.0, id.z);

    static const QSet<QString> commerce = {
        "shop", "grocery", "clothing_store", "restaurant", "cafe", "bar", "fast_food", "bank", "pharmacy",
        "hotel", "lodging", "alcohol_shop", "bakery", "butcher", "beer", "furniture", "ice_cream", "jewelry",
        "laundry", "optician", "books", "music", "cinema", "theatre", "office", "car", "hairdresser", "pub"};
    static const QSet<QString> publicPoi = {"school",  "college", "university", "hospital", "town_hall",
                                            "library", "police",  "post",       "kindergarten"};

    Pb tile{reinterpret_cast<const uchar *>(pbf.constData()), reinterpret_cast<const uchar *>(pbf.constData()) + pbf.size()};
    int f, w;
    while (tile.next(f, w)) {
        if (f != 3 || w != 2) {
            tile.skip(w);
            continue;
        }
        // --- couche
        Pb layer = tile.sub();
        QString name;
        QStringList keys;
        QVector<QVariant> values;
        QVector<Feature> features;
        double extent = 4096;
        int lf, lw;
        while (layer.next(lf, lw)) {
            switch (lf) {
            case 1: name = QString::fromUtf8(layer.bytes()); break;
            case 2: {
                Pb fp = layer.sub();
                Feature feat;
                int ff, fw;
                while (fp.next(ff, fw)) {
                    if (ff == 2 && fw == 2) {
                        Pb t = fp.sub();
                        while (!t.atEnd())
                            feat.tags << quint32(t.varint());
                    } else if (ff == 3) {
                        feat.type = int(fp.varint());
                    } else if (ff == 4 && fw == 2) {
                        feat.geometry = fp.sub();
                    } else {
                        fp.skip(fw);
                    }
                }
                features << feat;
                break;
            }
            case 3: keys << QString::fromUtf8(layer.bytes()); break;
            case 4: values << readValue(layer.sub()); break;
            case 5: extent = double(layer.varint()); break;
            default: layer.skip(lw); break;
            }
        }

        static const QSet<QString> wanted = {"building", "transportation", "transportation_name", "landuse",
                                             "landcover", "park", "water", "waterway", "poi"};
        if (!wanted.contains(name))
            continue;

        auto toWorld = [&](const QPointF &t) {
            const double lon = (id.x + t.x() / extent) / n * 360.0 - 180.0;
            const double lat = std::atan(std::sinh(M_PI * (1 - 2 * (id.y + t.y() / extent) / n))) * 180.0 / M_PI;
            return proj.project(lat, lon);
        };
        auto project = [&](const QPolygonF &p) {
            QPolygonF r;
            r.reserve(p.size());
            for (const QPointF &q : p)
                r << toWorld(q);
            return r;
        };
        const QRectF tileRect(0, 0, extent, extent);

        for (const Feature &feat : features) {
            QHash<QString, QVariant> tags;
            for (int i = 0; i + 1 < feat.tags.size(); i += 2)
                if (int(feat.tags[i]) < keys.size() && int(feat.tags[i + 1]) < values.size())
                    tags.insert(keys[feat.tags[i]], values[feat.tags[i + 1]]);
            const QString cls = tags.value("class").toString();
            const QVector<QPolygonF> parts = decodeGeometry(feat.geometry, feat.type);

            // anneaux extérieurs uniquement (aire positive dans le repère de tuile)
            auto outerRings = [&]() {
                QVector<QPolygonF> rings;
                for (const QPolygonF &r : parts)
                    if (r.size() >= 4 && signedArea(r) > 0)
                        rings << r;
                return rings;
            };

            if (name == "building" && feat.type == 3) {
                for (const QPolygonF &r : outerRings()) {
                    // un bâtiment coupé entre deux tuiles n'est compté que dans la tuile de son centre
                    if (!tileRect.contains(centroidOf(r)))
                        continue;
                    out.buildings << project(r);
                    out.buildingHeights << tags.value("render_height").toDouble();
                }
            } else if (name == "transportation" && feat.type == 2) {
                static const QSet<QString> roads = {"motorway", "trunk",    "primary", "secondary",
                                                    "tertiary", "minor",    "service"};
                if (!roads.contains(cls) || tags.value("brunnel").toString() == "tunnel")
                    continue;
                for (const QPolygonF &l : parts)
                    if (l.size() >= 2)
                        out.roads << Road{project(l), roadKind(cls), {}};
            } else if (name == "transportation_name" && feat.type == 2) {
                QString nm = tags.value("name").toString();
                if (nm.isEmpty())
                    nm = tags.value("name:latin").toString();
                if (nm.isEmpty())
                    continue;
                for (const QPolygonF &l : parts)
                    if (l.size() >= 2)
                        out.streetNames << Road{project(l), roadKind(cls), nm};
            } else if (name == "landuse" && feat.type == 3) {
                LandClass lc;
                if (cls == "residential")
                    lc = LandResidential;
                else if (cls == "commercial" || cls == "retail")
                    lc = LandCommercial;
                else if (cls == "industrial" || cls == "garages" || cls == "railway")
                    lc = LandIndustrial;
                else if (cls == "school" || cls == "university" || cls == "college" || cls == "hospital"
                         || cls == "kindergarten" || cls == "library")
                    lc = LandPublic;
                else if (cls == "cemetery" || cls == "pitch" || cls == "playground" || cls == "stadium") {
                    for (const QPolygonF &r : outerRings())
                        out.parks << project(r);
                    continue;
                } else
                    continue;
                for (const QPolygonF &r : outerRings())
                    out.landuse << qMakePair(project(r), lc);
            } else if (name == "landcover" && feat.type == 3) {
                if (cls == "wood" || cls == "forest") {
                    for (const QPolygonF &r : outerRings())
                        out.forests << project(r);
                } else if (cls == "grass" || cls == "farmland") {
                    for (const QPolygonF &r : outerRings())
                        out.parks << project(r);
                }
            } else if (name == "park" && feat.type == 3) {
                for (const QPolygonF &r : outerRings())
                    out.parks << project(r);
            } else if (name == "water" && feat.type == 3) {
                for (const QPolygonF &r : outerRings())
                    out.water << project(r);
            } else if (name == "waterway" && feat.type == 2) {
                if (cls == "river" || cls == "canal")
                    for (const QPolygonF &l : parts)
                        if (l.size() >= 2)
                            out.rivers << project(l);
            } else if (name == "poi" && feat.type == 1) {
                const QString sub = tags.value("subclass").toString();
                PoiKind kind;
                if (publicPoi.contains(cls) || publicPoi.contains(sub))
                    kind = PoiPublic;
                else if (commerce.contains(cls) || commerce.contains(sub))
                    kind = PoiCommerce;
                else
                    continue;
                for (const QPolygonF &pt : parts)
                    for (const QPointF &q : pt)
                        out.pois << qMakePair(toWorld(q), kind);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------

QSharedPointer<CityData> assemble(const QVector<TileData> &tiles, const QString &name, double lat0, double lon0,
                                  double radiusM)
{
    const QRectF area = radiusM > 0 ? QRectF(-radiusM, -radiusM, 2 * radiusM, 2 * radiusM) : QRectF();
    return assemble(tiles, name, lat0, lon0, area);
}

QSharedPointer<CityData> assemble(const QVector<TileData> &tiles, const QString &name, double lat0, double lon0,
                                  const QRectF &area)
{
    auto city = QSharedPointer<CityData>::create();
    city->name = name;
    city->lat0 = lat0;
    city->lon0 = lon0;

    QVector<QPair<QPolygonF, LandClass>> landuse;
    QVector<QPair<QPointF, PoiKind>> pois;
    QVector<QPolygonF> buildingPolys;
    QVector<double> heights;
    for (const TileData &t : tiles) {
        city->roads += t.roads;
        city->streetNames += t.streetNames;
        city->water += t.water;
        city->parks += t.parks;
        city->forests += t.forests;
        city->rivers += t.rivers;
        landuse += t.landuse;
        pois += t.pois;
        buildingPolys += t.buildings;
        heights += t.buildingHeights;
    }
    // les tuiles débordent de la zone demandée : on ne garde que la zone de jeu (+ marge pour le décor)
    if (area.isValid()) {
        const QRectF margin = area.adjusted(-400, -400, 400, 400);
        QVector<QPolygonF> keptPolys;
        QVector<double> keptHeights;
        for (int i = 0; i < buildingPolys.size(); ++i) {
            if (area.contains(centroidOf(buildingPolys[i]))) {
                keptPolys << buildingPolys[i];
                keptHeights << heights[i];
            }
        }
        buildingPolys = keptPolys;
        heights = keptHeights;
        auto trimRoads = [&](QVector<Road> &roads) {
            roads.erase(std::remove_if(roads.begin(), roads.end(),
                                       [&](const Road &r) { return !margin.intersects(r.pts.boundingRect()); }),
                        roads.end());
        };
        trimRoads(city->roads);
        trimRoads(city->streetNames);
    }
    QVector<QRectF> landBoxes;
    for (const auto &lu : landuse)
        landBoxes << lu.first.boundingRect();

    // Classement des bâtiments par occupation du sol
    city->buildings.reserve(buildingPolys.size());
    for (int i = 0; i < buildingPolys.size(); ++i) {
        Building b;
        b.poly = buildingPolys[i];
        b.centroid = centroidOf(b.poly);
        const double area = polygonArea(b.poly);
        b.use = area > 150 ? BuildingUse::Mixed : BuildingUse::Residential;
        for (int k = 0; k < landuse.size(); ++k) {
            if (!landBoxes[k].contains(b.centroid) || !landuse[k].first.containsPoint(b.centroid, Qt::OddEvenFill))
                continue;
            switch (landuse[k].second) {
            case LandResidential: b.use = area > 600 ? BuildingUse::Mixed : BuildingUse::Residential; break;
            case LandCommercial: b.use = BuildingUse::Commercial; break;
            case LandIndustrial: b.use = BuildingUse::Industrial; break;
            case LandPublic: b.use = BuildingUse::Public; break;
            }
            break;
        }
        if (area < 20)
            b.use = BuildingUse::Other;
        city->buildings << b;
    }

    // Commerces et équipements (points) : précisent l'usage du bâtiment qui les contient
    const double cell = 60;
    QHash<quint64, QVector<int>> index;
    auto cellKey = [cell](double x, double y) {
        return (quint64(quint32(qint32(std::floor(x / cell)))) << 32) | quint32(qint32(std::floor(y / cell)));
    };
    for (int i = 0; i < city->buildings.size(); ++i) {
        const QRectF r = city->buildings[i].poly.boundingRect();
        for (double x = std::floor(r.left() / cell) * cell; x <= r.right(); x += cell)
            for (double y = std::floor(r.top() / cell) * cell; y <= r.bottom(); y += cell)
                index[cellKey(x, y)] << i;
    }
    for (const auto &poi : pois) {
        for (int i : index.value(cellKey(poi.first.x(), poi.first.y()))) {
            Building &b = city->buildings[i];
            if (!b.poly.containsPoint(poi.first, Qt::OddEvenFill))
                continue;
            if (poi.second == PoiPublic)
                b.use = BuildingUse::Public;
            else if (b.use == BuildingUse::Residential)
                b.use = BuildingUse::Mixed;
            break;
        }
    }

    // Habitants et emplois
    for (int i = 0; i < city->buildings.size(); ++i) {
        Building &b = city->buildings[i];
        const double area = polygonArea(b.poly);
        double levels = heights[i] > 0 ? std::round(heights[i] / 3.2) : 0;
        if (levels <= 0 || levels > 60) {
            switch (b.use) {
            case BuildingUse::Residential: levels = area > 300 ? 5 : 2; break;
            case BuildingUse::Mixed: levels = area > 200 ? 5 : 3; break;
            case BuildingUse::Industrial: levels = 1; break;
            default: levels = 3; break;
            }
        }
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
    }

    QRectF bounds;
    for (const Building &b : city->buildings)
        bounds = bounds.united(b.poly.boundingRect());
    if (bounds.isEmpty())
        for (const Road &r : city->roads)
            bounds = bounds.united(r.pts.boundingRect());
    if (area.isValid())
        bounds = area; // toute la zone de jeu, même là où il n'y a pas de bâtiment (fleuve, parc…)
    city->bounds = bounds;
    city->area = area;
    return city;
}

} // namespace VectorTiles
