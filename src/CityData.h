#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <cmath>

enum class RoadKind { Motorway, Primary, Secondary, Tertiary, Residential, Service };

struct Road {
    QPolygonF pts;
    RoadKind kind = RoadKind::Residential;
    QString name;
};

enum class BuildingUse { Residential, Mixed, Commercial, Industrial, Public, Other };

struct Building {
    QPolygonF poly;
    QPointF centroid;
    BuildingUse use = BuildingUse::Other;
    double residents = 0;
    double jobs = 0;
};

// Ville chargée depuis OpenStreetMap, projetée en mètres autour de (lat0, lon0).
// x vers l'est, y vers le sud (repère écran Qt).
struct CityData {
    QString name;
    double lat0 = 0, lon0 = 0;
    QRectF bounds;
    QRectF area; // zone de jeu demandée (peut être agrandie) ; vide pour un fichier importé

    QVector<Road> roads;
    QVector<Road> streetNames; // tracés nommés non dessinés (noms des stations)
    QVector<Building> buildings;
    QVector<QPolygonF> water, parks, forests;
    QVector<QPolygonF> rivers; // polylignes (cours d'eau sans surface)

    double totalResidents = 0;
    double totalJobs = 0;

    QPointF project(double lat, double lon) const
    {
        const double kx = 111320.0 * std::cos(lat0 * M_PI / 180.0);
        const double ky = 110540.0;
        return QPointF((lon - lon0) * kx, -(lat - lat0) * ky);
    }
};
