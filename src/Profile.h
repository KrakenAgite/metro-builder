#pragma once

#include "Metro.h"
#include <QWidget>

// Vue en coupe d'une ligne : sol, cours d'eau, tunnels, viaducs et stations le long du parcours.
// Un clic sur un inter-station bascule tunnel / viaduc.
class ProfileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileWidget(Metro *metro, QWidget *parent = nullptr);
    void setLine(int lineId);
    void refresh();
    QSize sizeHint() const override { return QSize(900, 230); }

signals:
    void segmentClicked(int seg);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plot() const;
    double xOf(double s) const;
    double yOf(double level) const;
    int segmentAt(double x) const;

    Metro *m_metro;
    int m_lineId = -1;
    QVector<Metro::ProfilePoint> m_points;
    QVector<double> m_stationS; // abscisse de chaque arrêt
    double m_total = 0;
    int m_hover = -1;
};
