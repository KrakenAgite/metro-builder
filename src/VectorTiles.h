#pragma once

#include "CityData.h"
#include <QByteArray>
#include <QSharedPointer>
#include <QVector>

// Lecture des tuiles vectorielles OpenMapTiles (format Mapbox Vector Tile, protobuf),
// servies par exemple par OpenFreeMap. Beaucoup plus rapide qu'Overpass : les tuiles
// sont précalculées et distribuées par un CDN.
namespace VectorTiles {

constexpr int Zoom = 14; // niveau le plus détaillé (bâtiments complets)

struct TileId {
    int z = Zoom, x = 0, y = 0;
};

// Tuiles couvrant le carré de demi-côté radiusM autour de (lat, lon)
QVector<TileId> tilesForArea(double lat, double lon, double radiusM, int zoom = Zoom);
// Tuiles couvrant un rectangle exprimé dans le repère de la ville (mètres autour de lat0/lon0)
QVector<TileId> tilesForRect(double lat0, double lon0, const QRectF &area, int zoom = Zoom);

enum LandClass { LandResidential, LandCommercial, LandIndustrial, LandPublic };
enum PoiKind { PoiCommerce, PoiPublic };

// Contenu utile d'une tuile, déjà projeté dans le repère de la ville
struct TileData {
    QVector<Road> roads;
    QVector<Road> streetNames;
    QVector<QPolygonF> buildings;
    QVector<double> buildingHeights; // m (0 si inconnu)
    QVector<QPair<QPolygonF, LandClass>> landuse;
    QVector<QPolygonF> water, parks, forests, rivers;
    QVector<QPair<QPointF, PoiKind>> pois;
    QString error;
};

TileData decode(const QByteArray &pbf, const TileId &id, double lat0, double lon0);

// Assemble les tuiles en une ville (classement des bâtiments, habitants, emplois)
QSharedPointer<CityData> assemble(const QVector<TileData> &tiles, const QString &name, double lat0, double lon0,
                                  double radiusM);
QSharedPointer<CityData> assemble(const QVector<TileData> &tiles, const QString &name, double lat0, double lon0,
                                  const QRectF &area);

} // namespace VectorTiles
