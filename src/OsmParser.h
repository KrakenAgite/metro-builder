#pragma once

#include "CityData.h"
#include <QByteArray>
#include <QJsonArray>
#include <QSharedPointer>

struct ParseResult {
    QSharedPointer<CityData> city;
    QString error;
};

// Transforme une réponse Overpass (JSON, "out geom") en CityData.
// Si lat0/lon0 sont NaN, le centre est déduit des données.
// radiusM > 0 fixe l'emprise de jeu à un carré de ce demi-côté.
ParseResult parseOverpassJson(const QByteArray &data, const QString &name,
                              double lat0 = NAN, double lon0 = NAN, double radiusM = 0);

// Même chose à partir d'une liste d'éléments déjà décodés (fusion de plusieurs zones).
ParseResult parseOverpassElements(const QJsonArray &elements, const QString &name, double lat0, double lon0,
                                  double radiusM);
