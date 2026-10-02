#include "MapView.h"
#include "Metro.h"
#include "Schematic.h"
#include "Audio.h"

#include <QGuiApplication>
#include <QHash>
#include <QTime>
#include <QSet>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

const QColor kPanel(23, 26, 33, 238);
const QColor kPanelBorder(255, 255, 255, 22);
const QColor kText("#E8EAED");
const QColor kTextDim("#9AA0A6");

void drawPanel(QPainter &p, const QRectF &r, double radius = 10)
{
    p.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawRoundedRect(r.adjusted(-i, -i + 2, i, i + 2), radius + i, radius + i);
    }
    p.setPen(QPen(kPanelBorder, 1));
    p.setBrush(kPanel);
    p.drawRoundedRect(r, radius, radius);
}

// largeur (m) et largeur mini (px) par type de voie
const double kRoadWidth[6] = {18, 14, 12, 10, 7, 4};
const double kRoadMinPx[6] = {2.5, 2, 1.6, 1.3, 0.8, 0.5};

} // namespace

// Palette de la carte : une version claire (type OSM) et une version sombre.
struct MapStyle {
    QColor background, water, park, forest, roadCasing;
    QColor roads[6];
    QColor buildings[6]; // Residential, Mixed, Commercial, Industrial, Public, Other
    QColor buildingOutline, bounds;
    QColor label, halo;
    QColor lineCasing, stationStroke, stationIdle, catchBuild, catchSelect;
    double overlayOpacity;
    QPainter::CompositionMode overlayMode;
};

namespace {

const MapStyle kLightStyle{
    QColor("#F2EFE9"), QColor("#AAD3DF"), QColor("#C8E6B0"), QColor("#ADD19E"), QColor("#C9C2B8"),
    {QColor("#F9B29C"), QColor("#FCD6A4"), QColor("#F7FABF"), QColor("#FFFFFF"), QColor("#FFFFFF"), QColor("#FFFFFF")},
    {QColor("#D9D0C9"), QColor("#D8C8BA"), QColor("#E8C8C8"), QColor("#DCD0E0"), QColor("#CFC6E6"), QColor("#DEDAD5")},
    QColor(0, 0, 0, 40), QColor(80, 80, 80, 120),
    QColor("#1A1A1A"), QColor(255, 255, 255, 235),
    QColor(255, 255, 255), QColor("#111111"), QColor("#666666"), QColor("#1565C0"), QColor("#37474F"),
    0.9, QPainter::CompositionMode_SourceOver,
};

const MapStyle kDarkStyle{
    QColor("#191C23"), QColor("#14293A"), QColor("#1C2E24"), QColor("#18291F"), QColor("#0F1217"),
    {QColor("#8C5A3F"), QColor("#7A6442"), QColor("#5D6049"), QColor("#3D434F"), QColor("#363B46"), QColor("#2F343E")},
    {QColor("#2A2F38"), QColor("#2E3039"), QColor("#3A2F35"), QColor("#322F3B"), QColor("#2E2F42"), QColor("#282B31")},
    QColor(255, 255, 255, 14), QColor(200, 205, 215, 90),
    QColor("#E8EAED"), QColor(14, 16, 21, 225),
    QColor("#0E1116"), QColor("#0E1116"), QColor("#9AA0A6"), QColor("#7AB0FF"), QColor("#CFD8DC"),
    0.85, QPainter::CompositionMode_Screen, // calque lumineux sur fond sombre
};

QColor loadColor(double ratio)
{
    if (ratio < 0.7)
        return QColor("#00D68F");
    if (ratio < 1.0)
        return QColor("#FFAA00");
    return QColor("#FF3B5C");
}

void drawHaloText(QPainter &p, const QPointF &pos, const QString &text, const QFont &font, const QColor &color,
                  const QColor &halo)
{
    // halo blanc par décalages : utilise le cache de glyphes (beaucoup plus rapide qu'un QPainterPath)
    p.setFont(font);
    p.setPen(halo);
    for (const QPointF d : {QPointF(-1.5, 0), QPointF(1.5, 0), QPointF(0, -1.5), QPointF(0, 1.5), QPointF(-1, -1),
                            QPointF(1, 1), QPointF(-1, 1), QPointF(1, -1)})
        p.drawText(pos + d, text);
    p.setPen(color);
    p.drawText(pos, text);
}

QString fmtCount(double v)
{
    if (v >= 10000)
        return QStringLiteral("%1 k").arg(v / 1000, 0, 'f', 1);
    return QString::number(qRound(v));
}

} // namespace

MapView::MapView(Metro *metro, QWidget *parent)
    : QWidget(parent)
    , m_metro(metro)
{
    m_style = &kLightStyle;
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    m_staticTimer.setSingleShot(true);
    m_staticTimer.setInterval(120);
    connect(&m_staticTimer, &QTimer::timeout, this, [this] {
        renderStatic();
        update();
    });
    connect(m_metro, &Metro::networkChanged, this, [this] {
        if (m_selected >= 0 && !m_metro->station(m_selected))
            m_selected = -1;
        if (m_overlay == Demand) // les densités ne dépendent pas du réseau
            rebuildOverlay();
        if (m_schematic)
            rebuildSchematic();
        update();
    });
}

void MapView::setCity(QSharedPointer<CityData> city)
{
    m_city = city;
    m_selected = m_hover = -1;
    buildPaths();
    rebuildOverlay(); // avant le rendu : les bâtiments sont colorés selon le calque
    fitCity();
}

void MapView::updateCity(QSharedPointer<CityData> city)
{
    m_city = city;
    buildPaths();
    rebuildOverlay();
    if (m_schematic)
        rebuildSchematic();
    renderStatic();
    update();
}

void MapView::setExtendBusy(bool busy)
{
    m_extendBusy = busy;
    update();
}

void MapView::setTool(Tool tool)
{
    m_tool = tool;
    update();
}

void MapView::setOverlay(Overlay overlay)
{
    m_overlay = overlay;
    rebuildOverlay();
    renderStatic(); // fond atténué et bâtiments colorés selon le calque
    update();
}

void MapView::setCurrentLine(int lineId)
{
    m_currentLine = lineId;
    update();
}

void MapView::setSelectedStation(int stationId)
{
    m_selected = stationId;
    update();
}

void MapView::setSchematic(bool on)
{
    if (on == m_schematic)
        return;
    m_schematic = on;
    m_hover = -1;
    m_hoverWaypoint = m_hoverTrack = TrackHit();
    if (on)
        rebuildSchematic();
    fitCity();
    update();
}

void MapView::rebuildSchematic()
{
    m_schem = Schematic::layout(m_metro->stations(), m_metro->lines());
}

void MapView::setDarkMap(bool dark)
{
    if (dark == darkMap())
        return;
    m_style = dark ? &kDarkStyle : &kLightStyle;
    renderStatic();
    update();
}

bool MapView::darkMap() const
{
    return m_style == &kDarkStyle;
}

void MapView::setView(const QPointF &center, double scale)
{
    if (scale <= 0)
        return;
    m_scale = std::clamp(scale, 0.01, 12.0);
    m_offset = QPointF(width() / 2.0, height() / 2.0) - center * m_scale;
    renderStatic();
    update();
}

void MapView::setInsets(int top, int bottom)
{
    if (m_insetTop == top && m_insetBottom == bottom)
        return;
    m_insetTop = top;
    m_insetBottom = bottom;
    update();
}

void MapView::fitCity()
{
    if (!m_city || m_city->bounds.isEmpty())
        return;
    QRectF b = m_city->bounds;
    if (m_schematic && !m_schem.isEmpty()) {
        QPolygonF pts;
        for (const QPointF &q : m_schem)
            pts << q;
        b = pts.boundingRect();
        const double pad = std::max({b.width(), b.height(), 1500.0}) * 0.12;
        b.adjust(-pad * 1.6, -pad, pad * 1.6, pad * 1.4); // de la place pour les noms de stations
    }
    const double availH = std::max(100, height() - m_insetTop - m_insetBottom);
    m_scale = std::min(width() / b.width(), availH / b.height()) * 0.95;
    m_offset = QPointF(width() / 2.0, m_insetTop + availH / 2.0) - b.center() * m_scale;
    renderStatic();
    update();
}

void MapView::buildPaths()
{
    for (auto &v : m_roads)
        v.clear();
    for (auto &v : m_buildings)
        v.clear();
    m_water.clear();
    m_parks.clear();
    m_forests.clear();
    m_rivers.clear();
    if (!m_city)
        return;
    auto shape = [](const QPolygonF &p) { return Shape{p, p.boundingRect()}; };
    for (const Road &r : m_city->roads)
        m_roads[int(r.kind)] << shape(r.pts);
    for (const Building &b : m_city->buildings)
        m_buildings[int(b.use)] << shape(b.poly);
    for (const QPolygonF &p : m_city->water)
        m_water << shape(p);
    for (const QPolygonF &p : m_city->parks)
        m_parks << shape(p);
    for (const QPolygonF &p : m_city->forests)
        m_forests << shape(p);
    for (const QPolygonF &p : m_city->rivers)
        m_rivers << shape(p);
}

void MapView::scheduleStatic()
{
    m_staticTimer.start();
}

// Rend la carte OSM (eau, parcs, rues, bâtiments) dans une image mise en cache.
void MapView::renderStatic()
{
    if (!m_city || width() <= 0 || height() <= 0)
        return;
    const qreal dpr = devicePixelRatioF();
    m_static = QPixmap(size() * dpr);
    m_static.setDevicePixelRatio(dpr);
    m_static.fill(m_style->background);
    m_staticScale = m_scale;
    m_staticOffset = m_offset;

    QPainter p(&m_static);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    const double px = 1.0 / m_scale; // 1 pixel écran en mètres

    // zone visible en coordonnées monde, pour ignorer ce qui est hors écran
    const QRectF view = QRectF(toWorld(QPointF(0, 0)), toWorld(QPointF(width(), height()))).normalized();
    auto fill = [&](const QVector<Shape> &shapes, double minPx) {
        for (const Shape &s : shapes)
            if (s.box.intersects(view) && std::max(s.box.width(), s.box.height()) * m_scale >= minPx)
                p.drawPolygon(s.poly);
    };
    auto stroke = [&](const QVector<Shape> &shapes, double margin) {
        const QRectF v = view.adjusted(-margin, -margin, margin, margin);
        for (const Shape &s : shapes)
            if (s.box.intersects(v))
                p.drawPolyline(s.poly);
    };

    p.setPen(Qt::NoPen);
    p.setBrush(m_style->forest);
    fill(m_forests, 1);
    p.setBrush(m_style->park);
    fill(m_parks, 1);
    p.setBrush(m_style->water);
    fill(m_water, 1);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_style->water, std::max(25.0, 3 * px), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    stroke(m_rivers, 50);

    // rues : bordure puis remplissage, des plus petites aux plus grandes
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 5; k >= 0; --k) {
            if (k == int(RoadKind::Service) && m_scale < 0.25)
                continue;
            const double w = std::max(kRoadWidth[k], kRoadMinPx[k] * px);
            if (pass == 0)
                p.setPen(QPen(m_style->roadCasing, w + 1.5 * px, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            else
                p.setPen(QPen(m_style->roads[k], w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            stroke(m_roads[k], w);
        }
    }

    // Avec un calque actif, le fond est atténué pour que les couleurs ressortent
    const bool dark = darkMap();
    auto veil = [&](int alpha) {
        p.fillRect(view.adjusted(-10 * px, -10 * px, 10 * px, 10 * px),
                   dark ? QColor(8, 10, 14, alpha) : QColor(255, 255, 255, alpha));
    };
    if (m_layer.isDensity())
        veil(dark ? 150 : 150);

    p.setPen(m_scale > 0.6 ? QPen(m_style->buildingOutline, 0) : Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, m_scale > 0.3);
    if (m_layer.isDensity()) {
        // chaque bâtiment prend la couleur du palier de densité de son quartier
        const QColor muted = dark ? QColor("#2A2E36") : QColor("#E4E1DC");
        for (const auto &group : m_buildings)
            for (const Shape &s : group) {
                if (!s.box.intersects(view) || std::max(s.box.width(), s.box.height()) * m_scale < 0.8)
                    continue;
                const int band = m_layer.bandAt(s.box.center());
                p.setBrush(band > 0 ? m_layer.bandColor(band) : muted);
                p.drawPolygon(s.poly);
            }
    } else {
        for (int k = 0; k < 6; ++k) {
            p.setBrush(m_style->buildings[k]);
            fill(m_buildings[k], 0.8);
        }
        if (m_overlay == Demand)
            veil(dark ? 90 : 110);
        else if (m_overlay == Load)
            veil(dark ? 70 : 90);
    }
    p.setRenderHint(QPainter::Antialiasing, true);

    // emprise de jeu
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_style->bounds, 2 * px, Qt::DashLine));
    p.drawRect(m_city->bounds);
}

// Image de la demande : une cellule de grille = un pixel, lissée à l'affichage.
void MapView::rebuildOverlay()
{
    DensityLayer::Kind kind = DensityLayer::None;
    switch (m_overlay) {
    case Demand: kind = DensityLayer::Demand; break;
    case Population: kind = DensityLayer::Population; break;
    case Jobs: kind = DensityLayer::Jobs; break;
    default: break;
    }
    m_layer.build(kind, m_metro->grid());
}

int MapView::stationUnder(const QPointF &screen) const
{
    if (m_schematic) {
        int best = -1;
        double bestD = 12;
        for (auto it = m_schem.begin(); it != m_schem.end(); ++it) {
            const QPointF d = toScreen(it.value()) - screen;
            const double dist = std::hypot(d.x(), d.y());
            if (dist <= bestD) {
                bestD = dist;
                best = it.key();
            }
        }
        return best;
    }
    return m_metro->stationAt(toWorld(screen), 12.0 / m_scale);
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------

void MapView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_style->background);
    if (!m_city) {
        // écran d'accueil : fond sombre et plan de métro stylisé
        QLinearGradient bg(0, 0, width(), height());
        bg.setColorAt(0, QColor("#0E1116"));
        bg.setColorAt(1, QColor("#1A2130"));
        p.fillRect(rect(), bg);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 10), 1));
        for (int x = 0; x < width(); x += 40)
            p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 40)
            p.drawLine(0, y, width(), y);
        const double w = width(), h = height();
        const struct {
            QColor c;
            QVector<QPointF> pts;
        } deco[] = {
            {QColor("#FFCD00"), {{0, h * 0.30}, {w * 0.35, h * 0.30}, {w * 0.55, h * 0.50}, {w, h * 0.50}}},
            {QColor("#4C8DFF"), {{w * 0.25, 0}, {w * 0.25, h * 0.55}, {w * 0.45, h * 0.75}, {w * 0.45, h}}},
            {QColor("#CF009E"), {{w * 0.70, 0}, {w * 0.70, h * 0.35}, {w * 0.85, h * 0.50}, {w * 0.85, h}}},
            {QColor("#6ECA97"), {{0, h * 0.80}, {w * 0.60, h * 0.80}, {w * 0.75, h * 0.65}, {w, h * 0.65}}},
        };
        for (const auto &d : deco) {
            QColor c = d.c;
            c.setAlpha(55);
            p.setPen(QPen(c, 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(d.pts.data(), d.pts.size());
            p.setPen(QPen(QColor(255, 255, 255, 60), 3));
            p.setBrush(QColor("#0E1116"));
            for (int i = 1; i + 1 < d.pts.size(); ++i)
                p.drawEllipse(d.pts[i], 8, 8);
        }
        return;
    }
    if (m_schematic) {
        paintSchematic(p);
        return;
    }

    if (!m_static.isNull() && qFuzzyCompare(m_scale, m_staticScale)) {
        // simple translation : copie directe, bien plus rapide qu'un dessin transformé
        p.drawPixmap((m_offset - m_staticOffset).toPoint(), m_static);
    } else if (!m_static.isNull()) {
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.translate(m_offset);
        p.scale(m_scale / m_staticScale, m_scale / m_staticScale);
        p.translate(-m_staticOffset);
        p.drawPixmap(0, 0, m_static);
        p.restore();
    }

    if (!m_layer.image().isNull()) {
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(m_offset);
        p.scale(m_scale, m_scale);
        p.setClipRect(m_layer.worldRect()); // le calque s'arrête à la zone de jeu
        if (m_layer.kind() == DensityLayer::Demand) {
            p.setOpacity(m_style->overlayOpacity);
            p.setCompositionMode(m_style->overlayMode);
        }
        p.drawImage(m_layer.worldRect(), m_layer.image());
        p.setOpacity(1);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // courbes de niveau : nettes quel que soit le zoom
        const auto &contours = m_layer.contours();
        for (int i = 0; i < contours.size(); ++i) {
            QColor c = m_layer.bandColor(i + 1);
            c = darkMap() ? c.lighter(115) : c.darker(125);
            QPen pen(c, i == contours.size() - 1 ? 2.2 : 1.4);
            pen.setCosmetic(true);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(contours[i]);
        }
        p.restore();
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    // zone de desserte à pied (survol, sélection, ou aperçu de construction)
    auto catchment = [&](const QPointF &world, const QColor &c) {
        const QPointF s = toScreen(world);
        p.setPen(QPen(c, 1.5, Qt::DashLine));
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 25));
        p.drawEllipse(s, Rules::WalkMax * m_scale, Rules::WalkMax * m_scale);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 30));
        p.setPen(Qt::NoPen);
        p.drawEllipse(s, Rules::WalkFull * m_scale, Rules::WalkFull * m_scale);
    };
    if (m_tool == AddStation && m_mouseIn && m_hover < 0)
        catchment(m_metro->snapToRoad(toWorld(m_mouse), std::max(25.0, 12 / m_scale)), m_style->catchBuild);
    for (int id : {m_selected, m_hover})
        if (const Station *s = m_metro->station(id))
            catchment(s->pos, m_style->catchSelect);

    drawLines(p);
    drawWaypoints(p);
    drawStations(p);
    drawEvents(p);
    drawTrains(p); // au-dessus des stations : une rame à quai reste visible
    drawProbe(p);
    drawExtendButtons(p);
    drawHud(p);
    drawLegend(p);
}

namespace {

// Décale une polyligne écran latéralement (normale moyenne à chaque sommet)
QPolygonF offsetPolyline(const QPolygonF &pts, double d)
{
    if (std::abs(d) < 1e-6 || pts.size() < 2)
        return pts;
    QPolygonF out(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF a = pts[std::max(0, i - 1)], b = pts[std::min(int(pts.size()) - 1, i + 1)];
        QPointF t = b - a;
        const double len = std::hypot(t.x(), t.y());
        if (len < 1e-9) {
            out[i] = pts[i];
            continue;
        }
        out[i] = pts[i] + QPointF(-t.y() / len, t.x() / len) * d;
    }
    return out;
}

} // namespace

bool MapView::waypointToolActive() const
{
    return !m_schematic && (m_tool == Select || m_tool == BuildLine || m_tool == Delete);
}

void MapView::drawLines(QPainter &p)
{
    const auto &lines = m_metro->lines();

    // Lignes empruntant exactement le même tunnel : décalage latéral pour les voir côte à côte
    auto groupKey = [&](const Line &l, int k) {
        const int a = l.stops[k], b = l.stops[(k + 1) % l.stops.size()];
        QString key = QStringLiteral("%1-%2").arg(std::min(a, b)).arg(std::max(a, b));
        QVector<QPointF> via = m_metro->waypoints(l.id, k);
        if (a > b)
            std::reverse(via.begin(), via.end());
        for (const QPointF &v : via)
            key += QStringLiteral(";%1,%2").arg(qRound(v.x())).arg(qRound(v.y()));
        return key;
    };
    QHash<QString, QVector<int>> shared;
    for (const Line &l : lines)
        for (int k = 0; k < l.segmentCount(); ++k)
            shared[groupKey(l, k)] << l.id;

    const double spacing = 7;
    const QRectF view = rect().adjusted(-40, -40, 40, 40);
    for (int pass = 0; pass < 2; ++pass) {
        for (const Line &l : lines) {
            const int n = l.stops.size();
            for (int k = 0; k < l.segmentCount() && k < l.paths.size(); ++k) {
                QPolygonF pts;
                pts.reserve(l.paths[k].pts.size());
                for (const QPointF &w : l.paths[k].pts)
                    pts << toScreen(w);
                if (!pts.boundingRect().intersects(view))
                    continue;
                const QVector<int> &group = shared[groupKey(l, k)];
                if (group.size() > 1) {
                    // sens de décalage indépendant du sens de parcours de chaque ligne
                    const double side = l.stops[k] > l.stops[(k + 1) % n] ? -1 : 1;
                    pts = offsetPolyline(pts, side * (group.indexOf(l.id) - (group.size() - 1) / 2.0) * spacing);
                }
                const bool current = l.id == m_currentLine && m_tool == BuildLine;
                if (m_overlay == Load) {
                    const double ratio = l.capacity > 0
                                             ? std::max(l.load[0].value(k), l.load[1].value(k)) / l.capacity
                                             : 0;
                    const double w = 4 + 10 * std::min(1.5, ratio);
                    if (pass == 0)
                        p.setPen(QPen(loadColor(ratio), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    else
                        p.setPen(QPen(l.color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                } else {
                    // ligne perturbée (grève, panne) : tracé en pointillés, atténué si à l'arrêt
                    const double service = m_metro->lineCapacityFactor(l.id);
                    if (pass == 0) {
                        p.setPen(QPen(current ? m_style->label : m_style->lineCasing, current ? 11 : 9,
                                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    } else {
                        QColor col = l.color;
                        if (service <= 0)
                            col.setAlphaF(0.45);
                        QPen pen(col, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                        if (service < 1)
                            pen.setDashPattern({1.2, 1.1});
                        p.setPen(pen);
                    }
                }
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(pts);
            }
        }
    }

    // Aperçu du prochain tronçon en mode tracé
    if (m_tool == BuildLine && m_mouseIn) {
        if (const Line *l = m_metro->line(m_currentLine); l && !l->stops.isEmpty()) {
            const bool front = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
            const Station *end = m_metro->station(front ? l->stops.first() : l->stops.last());
            QPointF target = m_mouse;
            if (const Station *h = m_metro->station(m_hover))
                target = toScreen(h->pos);
            p.setPen(QPen(l->color, 4, Qt::DashLine, Qt::RoundCap));
            p.drawLine(toScreen(end->pos), target);
            const double km = std::hypot(target.x() - toScreen(end->pos).x(), target.y() - toScreen(end->pos).y())
                              / m_scale / 1000.0;
            const QString txt = tr("%1 km · %2 M€").arg(km, 0, 'f', 2).arg(km * Rules::TrackCostKm, 0, 'f', 1);
            drawHaloText(p, target + QPointF(12, -10), txt, font(), m_style->label, m_style->halo);
        }
    }
}

// Poignées des points de passage (ligne courante, ligne survolée ou en cours d'édition)
void MapView::drawWaypoints(QPainter &p)
{
    if (!waypointToolActive())
        return;
    QSet<int> shown;
    if (m_metro->line(m_currentLine))
        shown << m_currentLine;
    for (const TrackHit &h : {m_hoverWaypoint, m_hoverTrack, m_dragWaypoint})
        if (h.valid())
            shown << h.lineId;
    for (int lineId : shown) {
        const Line *l = m_metro->line(lineId);
        if (!l)
            continue;
        for (int k = 0; k < l->segmentCount(); ++k) {
            const QVector<QPointF> via = m_metro->waypoints(lineId, k);
            for (int i = 0; i < via.size(); ++i) {
                const bool hot = (m_hoverWaypoint.valid() && m_hoverWaypoint.lineId == lineId
                                  && m_hoverWaypoint.seg == k && m_hoverWaypoint.index == i)
                                 || (m_dragWaypoint.valid() && m_dragWaypoint.lineId == lineId
                                     && m_dragWaypoint.seg == k && m_dragWaypoint.index == i);
                const double r = hot ? 7 : 5;
                p.setPen(QPen(l->color, 2.5));
                p.setBrush(m_tool == Delete && hot ? QColor("#F04D5E") : QColor("#FFFFFF"));
                p.drawEllipse(toScreen(via[i]), r, r);
            }
        }
    }
    // fantôme « + » là où un glisser créera un point de passage
    if (m_hoverTrack.valid() && !m_hoverWaypoint.valid() && !m_dragging && m_tool != Delete) {
        if (const Line *l = m_metro->line(m_hoverTrack.lineId)) {
            const QPointF c = toScreen(m_hoverTrack.pos);
            p.setPen(QPen(l->color, 2));
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawEllipse(c, 7, 7);
            p.setPen(QPen(QColor("#222"), 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPointF(-3.5, 0), c + QPointF(3.5, 0));
            p.drawLine(c + QPointF(0, -3.5), c + QPointF(0, 3.5));
        }
    }
}

void MapView::drawStations(QPainter &p)
{
    QFont labelFont = font();
    labelFont.setPointSizeF(labelFont.pointSizeF() * 0.95);
    labelFont.setBold(true);
    const bool labels = m_scale > 0.12;
    const QFontMetrics lfm(labelFont);
    QVector<QRectF> placed;

    for (const Station &s : m_metro->stations()) {
        const QPointF c = toScreen(s.pos);
        if (!rect().adjusted(-50, -50, 50, 50).contains(c.toPoint()))
            continue;
        const bool hl = s.id == m_hover || s.id == m_selected;
        double r = s.lineCount > 1 ? 8 : 6;
        if (hl)
            r += 2;
        if (s.id == m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(21, 101, 192, 90));
            p.drawEllipse(c, r + 6, r + 6);
        }
        if (s.lineCount == 0) {
            p.setPen(QPen(m_style->stationIdle, 2, Qt::DashLine));
            p.setBrush(QColor(255, 255, 255, 200));
        } else {
            p.setPen(QPen(m_style->stationStroke, s.lineCount > 1 ? 3 : 2));
            p.setBrush(Qt::white);
        }
        p.drawEllipse(c, r, r);
        if (m_metro->stationClosed(s.id)) { // station fermée (inondation) : barrée
            p.setPen(QPen(QColor("#FF3B5C"), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPointF(-r - 3, -r - 3), c + QPointF(r + 3, r + 3));
            p.drawLine(c + QPointF(r + 3, -r - 3), c + QPointF(-r - 3, r + 3));
        }
        if (labels || hl) {
            // on évite les étiquettes qui se chevauchent (sauf station survolée ou sélectionnée)
            const QPointF at = c + QPointF(r + 4, -r);
            const QRectF box = QRectF(at.x() - 2, at.y() - lfm.ascent() - 2, lfm.horizontalAdvance(s.name) + 4,
                                      lfm.height() + 4);
            bool clash = false;
            for (const QRectF &o : placed)
                clash |= o.intersects(box);
            if (!clash || hl) {
                placed << box;
                drawHaloText(p, at, s.name, labelFont, m_style->label, m_style->halo);
            }
        }
    }
}

// Chaque rame est représentée par un simple point de la couleur de sa ligne
void MapView::drawTrains(QPainter &p)
{
    const QRectF view = rect().adjusted(-20, -20, 20, 20);
    for (const TrainVis &t : m_metro->trains(0)) {
        if (t.wagonPos.isEmpty())
            continue;
        const QPointF c = toScreen(t.wagonPos[t.wagonPos.size() / 2]);
        if (!view.contains(c))
            continue;
        // anneau blanc cerné de sombre : visible sur sa propre ligne comme sur n'importe quel fond
        p.setPen(QPen(QColor(0, 0, 0, 150), 1.2));
        p.setBrush(Qt::white);
        p.drawEllipse(c, 7.5, 7.5);
        p.setPen(Qt::NoPen);
        p.setBrush(t.color);
        p.drawEllipse(c, 4.8, 4.8);
    }
}

void MapView::drawHud(QPainter &p)
{
    p.setFont(font());
    QString hint;
    switch (m_tool) {
    case Select:
        hint = tr("Cliquez une station ou une ligne · glissez un tracé pour le courber · "
                  "clic droit sur un point de passage pour le supprimer");
        break;
    case AddStation:
        hint = tr("Cliquez sur la carte pour construire une station (%1 M€)").arg(Rules::StationCost);
        break;
    case BuildLine:
        if (const Line *l = m_metro->line(m_currentLine))
            hint = tr("%1 : cliquez des stations pour prolonger · Ctrl+clic en tête · "
                      "glissez le tracé pour le courber · clic droit pour retirer")
                       .arg(l->name);
        else
            hint = tr("Choisissez ou créez une ligne dans la barre du bas");
        break;
    case Delete:
        hint = tr("Cliquez une station ou un point de passage pour le supprimer (%1 % remboursés)")
                   .arg(int(Rules::Refund * 100));
        break;
    }
    if (m_schematic)
        hint = tr("Plan schématique · M pour revenir à la carte · cliquez une station pour l'inspecter");
    const QFontMetrics fm = p.fontMetrics();
    const double hintW = std::min<double>(width() - 40, fm.horizontalAdvance(hint) + 32);
    const QRectF box((width() - hintW) / 2, height() - m_insetBottom - 44, hintW, 30);
    drawPanel(p, box, 15);
    p.setPen(kText);
    p.drawText(box.adjusted(16, 0, -16, 0), Qt::AlignCenter | Qt::TextSingleLine,
               fm.elidedText(hint, Qt::ElideRight, int(box.width() - 32)));

    // Infobulle de la station survolée
    const Station *s = m_metro->station(m_hover);
    if (!s)
        return;
    QVector<const Line *> lines;
    for (const Line &l : m_metro->lines())
        if (l.stops.contains(s->id))
            lines << &l;
    const QList<QPair<QString, QString>> rows = {
        {tr("Habitants"), fmtCount(s->catchPop)},
        {tr("Emplois"), fmtCount(s->catchJobs)},
        {tr("Demande"), tr("%1 /h").arg(fmtCount(s->potential))},
        {tr("Captée"), tr("%1 /h").arg(fmtCount(s->served))},
        {tr("Montées"), tr("%1 /h").arg(fmtCount(s->boardings))},
    };
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics bfm(bold);
    const int lh = fm.height() + 3;
    double w = bfm.horizontalAdvance(s->name) + 24;
    for (const auto &r : rows)
        w = std::max<double>(w, fm.horizontalAdvance(r.first) + bfm.horizontalAdvance(r.second) + 48);
    const double h = 14 + bfm.height() + 6 + 18 + rows.size() * lh + 8;
    QRectF tip(m_mouse + QPointF(18, 18), QSizeF(std::max(w, 190.0), h));
    if (tip.right() > width() - 8)
        tip.moveRight(m_mouse.x() - 18);
    if (tip.bottom() > height() - m_insetBottom)
        tip.moveBottom(m_mouse.y() - 18);
    drawPanel(p, tip, 10);
    double y = tip.top() + 12;
    p.setFont(bold);
    p.setPen(kText);
    p.drawText(QPointF(tip.left() + 12, y + bfm.ascent()), s->name);
    y += bfm.height() + 6;
    // pastilles des lignes
    p.setFont(font());
    double x = tip.left() + 12;
    if (lines.isEmpty()) {
        p.setPen(kTextDim);
        p.drawText(QPointF(x, y + fm.ascent()), tr("Non desservie"));
    }
    for (const Line *l : lines) {
        const QString t = l->name;
        const double tw = fm.horizontalAdvance(t) + 16;
        p.setPen(Qt::NoPen);
        p.setBrush(l->color);
        p.drawRoundedRect(QRectF(x, y, tw, 16), 8, 8);
        p.setPen(l->color.lightnessF() > 0.6 ? QColor("#111") : Qt::white);
        p.drawText(QRectF(x, y, tw, 16), Qt::AlignCenter, t);
        x += tw + 4;
    }
    y += 18 + 4;
    for (const auto &r : rows) {
        p.setPen(kTextDim);
        p.setFont(font());
        p.drawText(QPointF(tip.left() + 12, y + fm.ascent()), r.first);
        p.setPen(kText);
        p.setFont(bold);
        p.drawText(QRectF(tip.left(), y, tip.width() - 12, lh), Qt::AlignRight | Qt::AlignTop, r.second);
        y += lh;
    }
    p.setFont(font());
}

void MapView::drawLegend(QPainter &p)
{
    if (m_overlay == NoOverlay)
        return;
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    const QFontMetrics sfm(small);

    QString title, subtitle;
    QVector<QColor> swatches;
    QStringList labels; // étiquettes aux frontières entre cases
    if (m_overlay == Load) {
        title = tr("Charge des lignes");
        subtitle = tr("voyageurs / capacité");
        swatches = {loadColor(0), loadColor(0.8), loadColor(1.2)};
        labels = {"70 %", "100 %"};
    } else {
        title = m_layer.title();
        subtitle = m_overlay == Demand ? tr("part captée · intensité = volume de déplacements") : m_layer.unit();
        const int first = m_overlay == Demand ? 0 : 1;
        for (int b = first; b < m_layer.bandCount(); ++b)
            swatches << m_layer.bandColor(b);
        for (int i = 0; i < m_layer.thresholds().size(); ++i)
            labels << m_layer.formatThreshold(i);
        if (m_overlay != Demand)
            labels.removeFirst(); // première case = à partir du premier seuil
    }
    if (swatches.isEmpty())
        return;

    const double sw = 30, sh = 12;
    const double boxW = std::max<double>({swatches.size() * sw + 24, double(QFontMetrics(bold).horizontalAdvance(title) + 24),
                                          double(sfm.horizontalAdvance(subtitle) + 24)});
    const QRectF box(width() - boxW - 16, height() - m_insetBottom - 16 - 84, boxW, 84);
    drawPanel(p, box, 10);
    p.setPen(kText);
    p.setFont(bold);
    p.drawText(box.adjusted(12, 8, -12, 0), Qt::AlignLeft | Qt::AlignTop, title);
    p.setFont(small);
    p.setPen(kTextDim);
    p.drawText(box.adjusted(12, 26, -12, 0), Qt::AlignLeft | Qt::AlignTop, subtitle);

    const double x0 = box.left() + 12, y0 = box.top() + 46;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < swatches.size(); ++i) {
        p.setBrush(swatches[i]);
        const QRectF r(x0 + i * sw, y0, sw - 2, sh);
        p.drawRoundedRect(r, 3, 3);
    }
    p.setPen(kTextDim);
    for (int i = 0; i < labels.size() && i + 1 < swatches.size() + 1; ++i) {
        const double x = x0 + (i + 1) * sw - 1;
        p.drawText(QRectF(x - 20, y0 + sh + 2, 40, 16), Qt::AlignHCenter | Qt::AlignTop, labels[i]);
    }
    if (m_overlay == Population || m_overlay == Jobs)
        p.drawText(QRectF(x0 - 20, y0 + sh + 2, 40, 16), Qt::AlignHCenter | Qt::AlignTop,
                   m_layer.formatThreshold(0));
    p.setFont(font());
}

// Valeur du calque sous le curseur
void MapView::drawProbe(QPainter &p)
{
    if (!m_mouseIn || m_hover >= 0 || m_dragging || m_layer.kind() == DensityLayer::None)
        return;
    const QPointF w = toWorld(m_mouse);
    if (!m_layer.worldRect().contains(w))
        return;
    QString text;
    const double v = m_layer.valueAt(w);
    if (m_layer.kind() == DensityLayer::Demand) {
        const double d = m_layer.demandAt(w);
        if (d < 0.5)
            return;
        text = tr("%1 dépl./h · %2 % captée").arg(qRound(d)).arg(qRound(v * 100));
    } else {
        text = QStringLiteral("%1 %2").arg(qRound(v)).arg(m_layer.unit());
    }
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics fm(bold);
    const QRectF box(m_mouse + QPointF(14, -30), QSizeF(fm.horizontalAdvance(text) + 30, 22));
    drawPanel(p, box, 11);
    p.setPen(Qt::NoPen);
    p.setBrush(m_layer.bandColor(m_layer.bandOf(v)));
    p.drawEllipse(QPointF(box.left() + 12, box.center().y()), 4.5, 4.5);
    p.setFont(bold);
    p.setPen(kText);
    p.drawText(box.adjusted(22, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.setFont(font());
}

// Marqueurs des événements en cours (zone concernée + pastille pulsante + titre)
void MapView::drawEvents(QPainter &p)
{
    const double pulse = (QTime::currentTime().msecsSinceStartOfDay() % 1600) / 1600.0;
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics fm(bold);
    for (const GameEvent &e : m_metro->activeEvents()) {
        if (e.pos.isNull())
            continue;
        const QColor col = e.tone > 0 ? QColor("#00C48C") : QColor("#FF3B5C");
        const QPointF c = toScreen(e.pos);
        const double rr = e.radius * m_scale;
        if (rr > 14) {
            QColor fill = col;
            fill.setAlpha(28);
            p.setBrush(fill);
            p.setPen(QPen(col, 2, Qt::DashLine));
            p.drawEllipse(c, rr, rr);
        }
        // onde qui s'élargit
        QColor ring = col;
        ring.setAlphaF(0.6 * (1 - pulse));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ring, 3));
        p.drawEllipse(c, 12 + 18 * pulse, 12 + 18 * pulse);
        // pastille
        const QPointF pin = c + QPointF(0, -22);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(col);
        p.drawEllipse(pin, 11, 11);
        p.setFont(bold);
        p.setPen(Qt::white);
        p.drawText(QRectF(pin.x() - 11, pin.y() - 11, 22, 22), Qt::AlignCenter,
                   e.tone > 0 ? QStringLiteral("★") : QStringLiteral("!"));
        // titre
        const QRectF tag(pin.x() + 16, pin.y() - 11, fm.horizontalAdvance(e.title) + 18, 22);
        drawPanel(p, tag, 11);
        p.setPen(kText);
        p.drawText(tag, Qt::AlignCenter, e.title);
        p.setFont(font());
    }
}

// Boutons « + 1 km » au milieu de chaque bord de la zone de jeu
void MapView::drawExtendButtons(QPainter &p)
{
    for (QRectF &r : m_extendRects)
        r = QRectF();
    if (!m_city || m_schematic || m_city->bounds.isEmpty())
        return;
    const QRectF b(toScreen(m_city->bounds.topLeft()), toScreen(m_city->bounds.bottomRight()));
    const QRectF safe = QRectF(rect()).adjusted(12, m_insetTop + 8, -12, -m_insetBottom - 60);
    if (safe.width() < 100 || safe.height() < 100)
        return;
    const QString arrows[4] = {QStringLiteral("↑"), QStringLiteral("→"), QStringLiteral("↓"), QStringLiteral("←")};
    QFont f = font();
    f.setBold(true);
    const QFontMetrics fm(f);
    for (int side = 0; side < 4; ++side) {
        const QString text = m_extendBusy ? tr("…") : tr("%1  1 km").arg(arrows[side]);
        const QSizeF size(fm.horizontalAdvance(text) + 22, 26);
        QPointF c;
        switch (side) {
        case 0: c = QPointF(b.center().x(), b.top() - size.height() / 2 - 6); break;
        case 1: c = QPointF(b.right() + size.width() / 2 + 6, b.center().y()); break;
        case 2: c = QPointF(b.center().x(), b.bottom() + size.height() / 2 + 6); break;
        case 3: c = QPointF(b.left() - size.width() / 2 - 6, b.center().y()); break;
        }
        // si le bord est hors de l'écran, le bouton reste collé au bord visible
        c.setX(std::clamp(c.x(), safe.left() + size.width() / 2, safe.right() - size.width() / 2));
        c.setY(std::clamp(c.y(), safe.top() + size.height() / 2, safe.bottom() - size.height() / 2));
        const QRectF r(c - QPointF(size.width() / 2, size.height() / 2), size);
        m_extendRects[side] = r;
        drawPanel(p, r, 13);
        if (side == m_hoverExtend && !m_extendBusy) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(76, 141, 255, 200));
            p.drawRoundedRect(r, 13, 13);
        }
        p.setFont(f);
        p.setPen(kText);
        p.drawText(r, Qt::AlignCenter, text);
    }
    p.setFont(font());
}

int MapView::extendButtonAt(const QPointF &screen) const
{
    if (m_extendBusy)
        return -1;
    for (int side = 0; side < 4; ++side)
        if (m_extendRects[side].contains(screen))
            return side;
    return -1;
}

// ---------------------------------------------------------------------------
// Plan schématique
// ---------------------------------------------------------------------------

void MapView::paintSchematic(QPainter &p)
{
    const bool dark = darkMap();
    const QColor paper = dark ? QColor("#12151B") : QColor("#F7F5F0");
    const QColor ink = dark ? QColor("#E8EAED") : QColor("#1B1D22");
    p.fillRect(rect(), paper);
    p.setRenderHint(QPainter::Antialiasing, true);

    // trame de points discrète
    p.setPen(Qt::NoPen);
    p.setBrush(dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 16));
    for (int x = 20; x < width(); x += 32)
        for (int y = 20; y < height(); y += 32)
            p.drawEllipse(QPointF(x, y), 1.1, 1.1);

    const auto &lines = m_metro->lines();
    auto pos = [&](int id) { return toScreen(m_schem.value(id)); };
    auto key = [](int a, int b) { return (quint64(std::min(a, b)) << 32) | quint32(std::max(a, b)); };
    QHash<quint64, QVector<int>> shared;
    for (const Line &l : lines)
        for (int k = 0; k < l.segmentCount(); ++k)
            shared[key(l.stops[k], l.stops[(k + 1) % l.stops.size()])] << l.id;

    // tracé écran d'un tronçon (octilinéaire, décalé si plusieurs lignes le partagent)
    auto segmentPath = [&](const Line &l, int k) {
        const int a = l.stops[k], b = l.stops[(k + 1) % l.stops.size()];
        QPolygonF pts = Schematic::octilinearPath(pos(a), pos(b));
        const QVector<int> &group = shared[key(a, b)];
        if (group.size() > 1) {
            const double side = a > b ? -1 : 1;
            pts = offsetPolyline(pts, side * (group.indexOf(l.id) - (group.size() - 1) / 2.0) * 8);
        }
        return pts;
    };

    for (int pass = 0; pass < 2; ++pass)
        for (const Line &l : lines)
            for (int k = 0; k < l.segmentCount(); ++k) {
                if (pass == 0)
                    p.setPen(QPen(paper, 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                else
                    p.setPen(QPen(l.color, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(segmentPath(l, k));
            }

    // tronçons écran : obstacles pour le placement des noms
    QVector<QLineF> obstacles;
    for (const Line &l : lines)
        for (int k = 0; k < l.segmentCount(); ++k) {
            const QPolygonF pts = segmentPath(l, k);
            for (int i = 0; i + 1 < pts.size(); ++i)
                obstacles << QLineF(pts[i], pts[i + 1]);
        }
    auto hitsLine = [&](const QRectF &r) {
        const QLineF edges[] = {QLineF(r.topLeft(), r.topRight()), QLineF(r.topRight(), r.bottomRight()),
                                QLineF(r.bottomRight(), r.bottomLeft()), QLineF(r.bottomLeft(), r.topLeft())};
        for (const QLineF &o : obstacles) {
            if (r.contains(o.p1()) || r.contains(o.p2()))
                return true;
            for (const QLineF &e : edges)
                if (o.intersects(e) == QLineF::BoundedIntersection)
                    return true;
        }
        return false;
    };

    // stations
    QFont labelFont = font();
    labelFont.setBold(true);
    const QFontMetrics lfm(labelFont);
    QVector<QRectF> placed;
    for (const Station &s : m_metro->stations()) {
        if (!m_schem.contains(s.id))
            continue;
        const QPointF c = pos(s.id);
        const bool hl = s.id == m_hover || s.id == m_selected;
        const Line *only = nullptr;
        double hx = 0, hy = 0; // orientation des tronçons qui arrivent à la station
        for (const Line &l : lines) {
            const int i = l.stops.indexOf(s.id);
            if (i < 0 || l.segmentCount() == 0)
                continue;
            only = &l;
            for (int j : {i - 1, i + 1}) {
                const int jj = l.isLoop() ? (j + l.stops.size()) % l.stops.size() : j;
                if (jj < 0 || jj >= l.stops.size())
                    continue;
                const QPointF d = pos(l.stops[jj]) - c;
                hx += std::abs(d.x());
                hy += std::abs(d.y());
            }
        }
        if (s.id == m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(76, 141, 255, 90));
            p.drawEllipse(c, 16, 16);
        }
        if (s.lineCount > 1) {
            p.setPen(QPen(ink, 3));
            p.setBrush(Qt::white);
            p.drawEllipse(c, hl ? 10 : 8.5, hl ? 10 : 8.5);
        } else if (s.lineCount == 1 && only) {
            p.setPen(QPen(only->color, 3));
            p.setBrush(Qt::white);
            p.drawEllipse(c, hl ? 7 : 5.5, hl ? 7 : 5.5);
        } else {
            p.setPen(QPen(m_style->stationIdle, 2, Qt::DashLine));
            p.setBrush(paper);
            p.drawEllipse(c, 5, 5);
        }

        // nom : à droite d'un tronçon vertical, au-dessus d'un tronçon horizontal
        const double tw = lfm.horizontalAdvance(s.name);
        QVector<QPointF> candidates;
        if (hy >= hx)
            candidates = {c + QPointF(13, lfm.ascent() / 2.0 - 1), c + QPointF(-13 - tw, lfm.ascent() / 2.0 - 1)};
        else
            candidates = {c + QPointF(-tw / 2, -13), c + QPointF(-tw / 2, 13 + lfm.ascent())};
        // autres positions possibles autour de la station (diagonales comprises)
        const double asc = lfm.ascent();
        candidates << c + QPointF(11, -9) << c + QPointF(11, 9 + asc) << c + QPointF(-11 - tw, -9)
                   << c + QPointF(-11 - tw, 9 + asc) << c + QPointF(-tw / 2, -13) << c + QPointF(-tw / 2, 13 + asc)
                   << c + QPointF(13, asc / 2 - 1) << c + QPointF(-13 - tw, asc / 2 - 1);
        // 1er choix : ne touche ni un nom ni un tracé ; sinon ne touche aucun nom
        QRectF chosen;
        QPointF chosenAt;
        for (int pass = 0; pass < 2 && chosen.isNull(); ++pass) {
            for (const QPointF &at : candidates) {
                const QRectF box(at.x() - 3, at.y() - asc - 2, tw + 6, lfm.height() + 4);
                bool clash = false;
                for (const QRectF &o : placed)
                    clash |= o.intersects(box);
                if (!clash && (pass == 1 || !hitsLine(box))) {
                    chosen = box;
                    chosenAt = at;
                    break;
                }
            }
        }
        if (chosen.isNull() && hl) {
            chosenAt = candidates.first();
            chosen = QRectF(chosenAt.x() - 3, chosenAt.y() - asc - 2, tw + 6, lfm.height() + 4);
        }
        if (!chosen.isNull()) {
            placed << chosen;
            drawHaloText(p, chosenAt, s.name, labelFont, ink, QColor(paper.red(), paper.green(), paper.blue(), 235));
        }
    }

    // rames : un point qui glisse sur le tronçon schématique
    for (const TrainVis &t : m_metro->trains(0)) {
        const Line *l = m_metro->line(t.lineId);
        if (!l || t.seg < 0 || t.seg >= l->segmentCount())
            continue;
        const QPolygonF path = segmentPath(*l, t.seg);
        double total = 0;
        for (int i = 0; i + 1 < path.size(); ++i)
            total += QLineF(path[i], path[i + 1]).length();
        double s = t.t * total;
        QPointF at = path.first();
        for (int i = 0; i + 1 < path.size(); ++i) {
            const double segLen = QLineF(path[i], path[i + 1]).length();
            if (s <= segLen || i + 2 == path.size()) {
                at = path[i] + (path[i + 1] - path[i]) * (segLen > 0 ? std::min(1.0, s / segLen) : 0);
                break;
            }
            s -= segLen;
        }
        p.setPen(QPen(QColor(0, 0, 0, 150), 1.2));
        p.setBrush(Qt::white);
        p.drawEllipse(at, 6.5, 6.5);
        p.setPen(Qt::NoPen);
        p.setBrush(t.color);
        p.drawEllipse(at, 4, 4);
    }

    drawHud(p);
    drawSchematicLegend(p);
}

void MapView::drawSchematicLegend(QPainter &p)
{
    const auto &lines = m_metro->lines();
    if (lines.isEmpty())
        return;
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics fm(font()), bfm(bold);
    const int rows = std::min<int>(lines.size(), 12);
    double w = bfm.horizontalAdvance(tr("Plan du réseau")) + 24;
    for (int i = 0; i < rows; ++i)
        w = std::max<double>(w, fm.horizontalAdvance(lines[i].name) + 64);
    const double rowH = 24;
    const QRectF box(width() - w - 16, height() - m_insetBottom - 16 - (38 + rows * rowH), w, 38 + rows * rowH);
    drawPanel(p, box, 10);
    p.setFont(bold);
    p.setPen(kText);
    p.drawText(box.adjusted(12, 9, -12, 0), Qt::AlignLeft | Qt::AlignTop, tr("Plan du réseau"));
    for (int i = 0; i < rows; ++i) {
        const Line &l = lines[i];
        const double y = box.top() + 34 + i * rowH;
        const QRectF badge(box.left() + 12, y, 20, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(l.color);
        p.drawEllipse(badge);
        QFont bf = bold;
        bf.setPointSizeF(bf.pointSizeF() * (l.code.size() > 1 ? 0.75 : 0.9));
        p.setFont(bf);
        p.setPen(l.color.lightnessF() > 0.62 ? QColor("#111") : Qt::white);
        p.drawText(badge, Qt::AlignCenter, l.code);
        p.setFont(font());
        p.setPen(kText);
        p.drawText(QRectF(badge.right() + 10, y, w, 20), Qt::AlignVCenter | Qt::AlignLeft, l.name);
    }
    p.setFont(font());
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void MapView::resizeEvent(QResizeEvent *)
{
    if (m_staticScale == 0)
        fitCity();
    else
        scheduleStatic();
}

void MapView::mousePressEvent(QMouseEvent *e)
{
    m_pressPos = m_mouse = e->position();
    m_pressButton = e->button();
    m_dragging = false;
    m_dragStation = (e->button() == Qt::LeftButton && m_tool == Select && !m_schematic) ? stationUnder(e->position())
                                                                                        : -1;
    m_dragWaypoint = m_pendingTrack = TrackHit();
    if (e->button() == Qt::LeftButton && (m_tool == Select || m_tool == BuildLine) && !m_schematic
        && stationUnder(e->position()) < 0) {
        m_dragWaypoint = waypointUnder(e->position());
        if (!m_dragWaypoint.valid())
            m_pendingTrack = trackUnder(e->position());
    }
}

TrackHit MapView::waypointUnder(const QPointF &screen) const
{
    return m_metro->waypointAt(toWorld(screen), 10.0 / m_scale, m_currentLine);
}

TrackHit MapView::trackUnder(const QPointF &screen) const
{
    return m_metro->trackAt(toWorld(screen), 8.0 / m_scale, m_currentLine);
}

void MapView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    const QPointF delta = pos - m_mouse;
    m_mouse = pos;
    m_mouseIn = true;
    if (e->buttons() != Qt::NoButton) {
        if (!m_dragging && (pos - m_pressPos).manhattanLength() > 4)
            m_dragging = true;
        if (m_dragging) {
            if (m_dragStation >= 0) {
                m_metro->moveStation(m_dragStation, toWorld(pos), false);
            } else if (m_pendingTrack.valid()) {
                // glisser sur un tracé : on crée un point de passage puis on le déplace
                const TrackHit h = m_pendingTrack;
                m_pendingTrack = TrackHit();
                if (m_metro->insertWaypoint(h.lineId, h.seg, h.index, toWorld(pos)))
                    m_dragWaypoint = {h.lineId, h.seg, h.index, toWorld(pos)};
            } else if (m_dragWaypoint.valid()) {
                m_metro->moveWaypoint(m_dragWaypoint.lineId, m_dragWaypoint.seg, m_dragWaypoint.index,
                                      toWorld(pos), false);
            } else {
                m_offset += delta;
                scheduleStatic();
            }
        }
    }
    m_hover = stationUnder(pos);
    if (m_dragStation >= 0 && m_dragging)
        m_hover = -1;
    m_hoverWaypoint = m_hoverTrack = TrackHit();
    if (m_hover < 0 && !m_dragging && waypointToolActive()) {
        m_hoverWaypoint = waypointUnder(pos);
        if (!m_hoverWaypoint.valid())
            m_hoverTrack = trackUnder(pos);
    }
    m_hoverExtend = m_dragging ? -1 : extendButtonAt(pos);
    Qt::CursorShape cursor = Qt::CrossCursor;
    if (m_hoverExtend >= 0)
        cursor = Qt::PointingHandCursor;
    else if (m_dragging)
        cursor = Qt::ClosedHandCursor;
    else if (m_hover >= 0)
        cursor = Qt::PointingHandCursor;
    else if (m_hoverWaypoint.valid())
        cursor = m_tool == Delete ? Qt::PointingHandCursor : Qt::SizeAllCursor;
    else if (m_hoverTrack.valid() && m_tool != Delete)
        cursor = Qt::OpenHandCursor;
    setCursor(cursor);
    update();
}

void MapView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && m_dragStation >= 0) {
        const QPointF w = m_metro->snapToRoad(toWorld(e->position()), std::max(25.0, 12 / m_scale));
        m_metro->moveStation(m_dragStation, w, true);
    } else if (m_dragging && m_dragWaypoint.valid()) {
        m_metro->moveWaypoint(m_dragWaypoint.lineId, m_dragWaypoint.seg, m_dragWaypoint.index,
                              toWorld(e->position()), true);
    } else if (!m_dragging && e->button() == m_pressButton) {
        const int side = e->button() == Qt::LeftButton ? extendButtonAt(e->position()) : -1;
        if (side >= 0)
            emit extendRequested(side);
        else
            handleClick(e->button(), e->position(), e->modifiers());
    }
    m_dragging = false;
    m_dragStation = -1;
    m_dragWaypoint = m_pendingTrack = TrackHit();
    m_pressButton = Qt::NoButton;
    setCursor(Qt::CrossCursor);
    update();
}

void MapView::handleClick(Qt::MouseButton button, const QPointF &pos, Qt::KeyboardModifiers mods)
{
    if (!m_city)
        return;
    const int sid = stationUnder(pos);
    const QPointF world = m_metro->snapToRoad(toWorld(pos), std::max(25.0, 12 / m_scale));

    if (m_schematic) {
        // sur le plan, on agit sur les stations existantes ; la construction se fait sur la carte
        const QString needMap = tr("Repassez en vue carte (M) pour construire une station");
        if (button == Qt::RightButton) {
            if (m_tool == BuildLine && sid >= 0)
                m_metro->removeStop(m_currentLine, sid);
            return;
        }
        if (button != Qt::LeftButton)
            return;
        switch (m_tool) {
        case Select:
        case AddStation:
            if (sid < 0 && m_tool == AddStation) {
                emit statusMessage(needMap);
                return;
            }
            m_selected = sid;
            emit stationSelected(sid);
            break;
        case BuildLine:
            if (!m_metro->line(m_currentLine))
                emit statusMessage(tr("Sélectionnez ou créez une ligne avant de tracer."));
            else if (sid < 0)
                emit statusMessage(needMap);
            else
                m_metro->addStop(m_currentLine, sid, mods & Qt::ControlModifier);
            break;
        case Delete:
            if (sid >= 0) {
                m_metro->removeStation(sid);
                emit stationSelected(-1);
            }
            break;
        }
        return;
    }
    const TrackHit wp = sid < 0 && waypointToolActive() ? waypointUnder(pos) : TrackHit();
    if (button == Qt::RightButton) {
        if (wp.valid()) {
            m_metro->removeWaypoint(wp.lineId, wp.seg, wp.index);
            Audio::instance().play(Audio::Click);
        } else if (m_tool == BuildLine && sid >= 0) {
            m_metro->removeStop(m_currentLine, sid);
            Audio::instance().play(Audio::Demolish);
        }
        return;
    }
    if (button != Qt::LeftButton)
        return;

    switch (m_tool) {
    case Select:
        m_selected = sid;
        emit stationSelected(sid);
        if (sid < 0) {
            // clic sur un tracé : sélection de la ligne
            const TrackHit h = wp.valid() ? wp : trackUnder(pos);
            if (h.valid())
                emit lineClicked(h.lineId);
        }
        break;
    case AddStation:
        if (sid >= 0) {
            m_selected = sid;
            emit stationSelected(sid);
        } else if (int id = m_metro->addStation(world); id >= 0) {
            Audio::instance().play(Audio::Station);
            m_selected = id;
            emit stationSelected(id);
        }
        break;
    case BuildLine: {
        if (!m_metro->line(m_currentLine)) {
            emit statusMessage(tr("Sélectionnez ou créez une ligne avant de tracer."));
            return;
        }
        int target = sid;
        if (target < 0) {
            target = m_metro->addStation(world);
            if (target >= 0)
                Audio::instance().play(Audio::Station);
        }
        if (target >= 0) {
            if (m_metro->addStop(m_currentLine, target, mods & Qt::ControlModifier))
                Audio::instance().play(Audio::Connect);
            m_selected = target;
            emit stationSelected(target);
        }
        break;
    }
    case Delete:
        if (sid >= 0) {
            m_metro->removeStation(sid);
            Audio::instance().play(Audio::Demolish);
            emit stationSelected(-1);
        } else if (wp.valid()) {
            m_metro->removeWaypoint(wp.lineId, wp.seg, wp.index);
        }
        break;
    }
}

void MapView::wheelEvent(QWheelEvent *e)
{
    const double factor = std::pow(1.0015, e->angleDelta().y());
    const double ns = std::clamp(m_scale * factor, 0.01, 12.0);
    const QPointF w = toWorld(e->position());
    m_scale = ns;
    m_offset = e->position() - w * ns;
    scheduleStatic();
    update();
}

void MapView::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_selected >= 0) {
        m_metro->removeStation(m_selected);
        emit stationSelected(-1);
    } else if (e->key() == Qt::Key_Escape) {
        m_selected = -1;
        emit stationSelected(-1);
        update();
    } else {
        QWidget::keyPressEvent(e);
    }
}

void MapView::leaveEvent(QEvent *)
{
    m_mouseIn = false;
    m_hover = -1;
    m_hoverWaypoint = m_hoverTrack = TrackHit();
    update();
}
