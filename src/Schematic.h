#pragma once

#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

struct Station;
struct Line;

// Plan schématique du réseau (à la manière des plans de métro) :
// tronçons à 0°/45°/90°, inter-stations régulières, positions alignées sur une grille.
namespace Schematic {

// Positions schématiques des stations (mêmes unités que la carte, centrées sur le réseau réel)
QHash<int, QPointF> layout(const QVector<Station> &stations, const QVector<Line> &lines);

// Tracé octilinéaire entre deux points : droit, ou droit / diagonale / droit
QPolygonF octilinearPath(const QPointF &a, const QPointF &b);

} // namespace Schematic
