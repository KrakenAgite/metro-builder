#pragma once

#include "CityData.h"
#include "DensityLayer.h"
#include "Metro.h"
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QSharedPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QWidget>

class Metro;
struct MapStyle;

class MapView : public QWidget
{
    Q_OBJECT
public:
    enum Tool { Select, AddStation, BuildLine, Delete, RouteTool };
    enum Overlay { NoOverlay, Demand, Population, Jobs, Load };

    explicit MapView(Metro *metro, QWidget *parent = nullptr);

    void setCity(QSharedPointer<CityData> city);
    void updateCity(QSharedPointer<CityData> city); // zone agrandie : garde la vue
    void setExtendBusy(bool busy);
    void setTool(Tool tool);
    void setOverlay(Overlay overlay);
    void setCurrentLine(int lineId);
    void setSelectedStation(int stationId);
    void fitCity();
    // espace occupé par les panneaux flottants en haut et en bas
    void setInsets(int top, int bottom);
    void setDarkMap(bool dark);
    void setDayNight(bool on);
    // qualité graphique : 0 économie, 1 équilibrée, 2 maximale
    void setQuality(int quality);
    int quality() const { return m_quality; }
    void setShowPerf(bool on);
    bool dayNight() const { return m_dayNight; }
    double darkness() const; // 0 en plein jour, 1 en pleine nuit
    void setSchematic(bool on);
    bool schematic() const { return m_schematic; }
    Overlay overlay() const { return m_overlay; }
    // vue courante (centre de l'écran en coordonnées monde + zoom), pour les sauvegardes
    QPointF viewCenter() const { return toWorld(QPointF(width() / 2.0, height() / 2.0)); }
    double viewScale() const { return m_scale; }
    void setView(const QPointF &center, double scale);
    bool darkMap() const;
    // itinéraire affiché (outil Itinéraire)
    void setRoute(const Route &route);
    void clearRoute();
    // exporte le plan schématique du réseau en PNG ou PDF
    bool exportPlan(const QString &path, const QString &title);

signals:
    void stationSelected(int stationId);
    void lineClicked(int lineId);
    void extendRequested(int side); // 0 nord, 1 est, 2 sud, 3 ouest
    void statusMessage(const QString &text);
    void routeRequested(const QPointF &from, const QPointF &to); // monde
    void routeCleared();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QPointF toWorld(const QPointF &screen) const { return (screen - m_offset) / m_scale; }
    QPointF toScreen(const QPointF &world) const { return world * m_scale + m_offset; }
    void buildPaths();
    void renderStatic();
    void scheduleStatic();
    void rebuildOverlay();
    void handleClick(Qt::MouseButton button, const QPointF &pos, Qt::KeyboardModifiers mods);
    int stationUnder(const QPointF &screen) const;
    TrackHit waypointUnder(const QPointF &screen) const;
    TrackHit trackUnder(const QPointF &screen) const;
    bool waypointToolActive() const;
    void drawWaypoints(QPainter &p);

    void drawLines(QPainter &p);
    void drawStations(QPainter &p);
    void drawTrains(QPainter &p);
    void drawHud(QPainter &p);
    void drawLegend(QPainter &p);
    void drawNight(QPainter &p, double dark);
    void drawBase(QPainter &p);          // carte statique + calque de densité
    void drawLights(QPainter &p);        // fenêtres et halos des stations (pleine intensité)
    void drawBuildPreview(QPainter &p);  // tronçon en cours de tracé (suit la souris)
    // couches mises en cache : redessinées seulement quand leur clé change (vue, réseau, survol…)
    bool cacheValid(QPixmap &cache, QString &key, const QString &want);
    QString viewKey() const;
    void drawProbe(QPainter &p);
    void drawExtendButtons(QPainter &p);
    void drawEvents(QPainter &p);
    int extendButtonAt(const QPointF &screen) const;
    void rebuildSchematic();
    void paintSchematic(QPainter &p);
    void drawSchematicLegend(QPainter &p);
    void drawRoute(QPainter &p);
    double vw() const { return m_exporting ? m_exportSize.width() : width(); }
    double vh() const { return m_exporting ? m_exportSize.height() : height(); }

    Metro *m_metro;
    QSharedPointer<CityData> m_city;

    // Couches statiques (coordonnées monde)
    struct Shape {
        QPolygonF poly;
        QRectF box;
    };
    QVector<Shape> m_roads[6];
    QVector<Shape> m_buildings[6];
    QVector<Shape> m_water, m_parks, m_forests, m_rivers;
    QVector<QPointF> m_lights; // fenêtres éclairées la nuit (échantillon de bâtiments)
    bool m_dayNight = true;

    QPixmap m_static;
    double m_staticScale = 0;
    QPointF m_staticOffset;
    QTimer m_staticTimer;
    DensityLayer m_layer;

    const MapStyle *m_style;
    int m_insetTop = 0, m_insetBottom = 0;
    bool m_schematic = false;
    QRectF m_extendRects[4];
    int m_hoverExtend = -1;
    bool m_extendBusy = false;
    QHash<int, QPointF> m_schem; // positions des stations sur le plan schématique
    double m_scale = 0.2; // pixels par mètre
    QPointF m_offset;

    Tool m_tool = Select;
    Overlay m_overlay = Demand;
    int m_currentLine = -1;
    int m_selected = -1;
    int m_hover = -1;

    QPointF m_mouse;
    bool m_mouseIn = false;
    QPointF m_pressPos;
    Qt::MouseButton m_pressButton = Qt::NoButton;
    bool m_dragging = false;
    int m_dragStation = -1;
    TrackHit m_dragWaypoint, m_pendingTrack, m_hoverWaypoint, m_hoverTrack;

    Route m_route;
    QPointF m_routeFrom;
    bool m_hasRouteFrom = false;

    QPixmap m_baseCache, m_netCache, m_lightsCache, m_schemCache;
    QHash<QRgb, QPixmap> m_trainSprites; // pastille d'une rame, par couleur de ligne
    QString m_baseKey, m_netKey, m_lightsKey, m_schemKey;
    quint64 m_netRev = 0;   // incrémenté à chaque changement du réseau ou des événements
    quint64 m_layerRev = 0; // incrémenté à chaque reconstruction du calque
    int m_schemLayer = 0;   // 0 : tout, 1 : partie fixe du plan, 2 : rames et bulles
    int m_quality = 2;
    bool m_showPerf = false;
    QElapsedTimer m_fpsClock;
    int m_fpsFrames = 0;
    double m_fps = 0, m_paintMs = 0;
    void drawPerf(QPainter &p);

    bool m_exporting = false;
    QSizeF m_exportSize;
    QString m_exportTitle;
};
