#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QVector>

struct DemandGrid;

// Calque thématique de la carte : champ lissé à partir de la grille de 100 m (1 ha),
// découpé en paliers de couleurs vives, avec courbes de niveau vectorielles.
class DensityLayer
{
public:
    enum Kind { None, Demand, Population, Jobs };

    void build(Kind kind, const DemandGrid &grid);
    void clear();

    Kind kind() const { return m_kind; }
    bool isDensity() const { return m_kind == Population || m_kind == Jobs; }
    const QImage &image() const { return m_image; }
    QRectF worldRect() const { return m_rect; }
    const QVector<QPainterPath> &contours() const { return m_contours; }

    // Paliers : bande 0 = sous le premier seuil ; bande i = au-dessus du seuil i-1
    const QVector<double> &thresholds() const { return m_thresholds; }
    int bandCount() const { return m_thresholds.size() + 1; }
    QColor bandColor(int band) const;
    int bandOf(double value) const;

    double valueAt(const QPointF &world) const;  // valeur lissée (hab/ha, emplois/ha ou part captée)
    double demandAt(const QPointF &world) const; // déplacements/h/ha (calque demande)
    int bandAt(const QPointF &world) const { return bandOf(valueAt(world)); }

    QString title() const;
    QString unit() const;
    QString formatThreshold(int i) const;

private:
    double sample(const QVector<float> &f, const QPointF &world) const;

    Kind m_kind = None;
    int m_w = 0, m_h = 0;
    double m_cell = 100;
    QPointF m_origin;
    QRectF m_rect;
    QVector<float> m_field;  // valeur lissée par cellule
    QVector<float> m_weight; // intensité de la demande (calque demande)
    QVector<double> m_thresholds;
    QVector<QColor> m_colors;
    QImage m_image;
    QVector<QPainterPath> m_contours;
};
