#include "Profile.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace {

constexpr double TopLevel = 22;    // m : haut du dessin (au-dessus du sol)
constexpr double BottomLevel = -34; // m : bas du dessin

} // namespace

ProfileWidget::ProfileWidget(Metro *metro, QWidget *parent)
    : QWidget(parent)
    , m_metro(metro)
{
    setMouseTracking(true);
    setMinimumHeight(200);
}

void ProfileWidget::setLine(int lineId)
{
    m_lineId = lineId;
    refresh();
}

void ProfileWidget::refresh()
{
    m_points = m_metro->lineProfile(m_lineId);
    m_stationS.clear();
    m_total = 0;
    if (const Line *l = m_metro->line(m_lineId)) {
        m_stationS << 0;
        for (const SegPath &sp : l->paths) {
            m_total += sp.length();
            m_stationS << m_total;
        }
    }
    update();
}

QRectF ProfileWidget::plot() const
{
    return QRectF(24, 54, width() - 48, height() - 54 - 10);
}

double ProfileWidget::xOf(double s) const
{
    const QRectF r = plot();
    return r.left() + (m_total > 0 ? s / m_total : 0) * r.width();
}

double ProfileWidget::yOf(double level) const
{
    const QRectF r = plot();
    return r.top() + (TopLevel - level) / (TopLevel - BottomLevel) * r.height();
}

int ProfileWidget::segmentAt(double x) const
{
    for (int k = 0; k + 1 < m_stationS.size(); ++k)
        if (x >= xOf(m_stationS[k]) && x <= xOf(m_stationS[k + 1]))
            return k;
    return -1;
}

void ProfileWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Line *l = m_metro->line(m_lineId);
    if (!l || m_points.size() < 2) {
        p.setPen(QColor("#9AA0A6"));
        p.drawText(rect(), Qt::AlignCenter, tr("Ajoutez au moins deux arrêts pour voir la coupe de la ligne"));
        return;
    }
    const QRectF r = plot();
    const double ground = yOf(0);

    // ciel et sous-sol (strates)
    QLinearGradient sky(0, r.top(), 0, ground);
    sky.setColorAt(0, QColor("#1B2333"));
    sky.setColorAt(1, QColor("#2A3346"));
    p.fillRect(QRectF(r.left(), r.top(), r.width(), ground - r.top()), sky);
    QLinearGradient soil(0, ground, 0, r.bottom());
    soil.setColorAt(0, QColor("#5A4A36"));
    soil.setColorAt(0.35, QColor("#4A3D2E"));
    soil.setColorAt(1, QColor("#2E2820"));
    p.fillRect(QRectF(r.left(), ground, r.width(), r.bottom() - ground), soil);
    p.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    for (double lvl : {-10.0, -20.0, -30.0})
        p.drawLine(QPointF(r.left(), yOf(lvl)), QPointF(r.right(), yOf(lvl)));

    // cours d'eau : lame d'eau sous la surface
    for (int i = 0; i + 1 < m_points.size(); ++i) {
        if (!m_points[i].water)
            continue;
        const double x0 = xOf(m_points[i].s), x1 = xOf(m_points[i + 1].s);
        p.fillRect(QRectF(x0, ground, x1 - x0 + 0.6, yOf(-7) - ground), QColor("#2F6FB0"));
    }
    // sol (ligne de terrain)
    p.setPen(QPen(QColor("#7FA36B"), 2.5));
    p.drawLine(QPointF(r.left(), ground), QPointF(r.right(), ground));

    // segment survolé
    if (m_hover >= 0 && m_hover + 1 < m_stationS.size()) {
        const double x0 = xOf(m_stationS[m_hover]), x1 = xOf(m_stationS[m_hover + 1]);
        p.fillRect(QRectF(x0, r.top(), x1 - x0, r.height()), QColor(255, 255, 255, 16));
    }

    // voie : tube de tunnel (sous terre), piliers (viaduc), puis la voie à la couleur de la ligne
    QPolygonF track;
    for (const auto &pt : m_points)
        track << QPointF(xOf(pt.s), yOf(pt.level));
    QPainterPath tube;
    for (int i = 0; i + 1 < m_points.size(); ++i)
        if (m_points[i].level < -2 || m_points[i + 1].level < -2) {
            tube.moveTo(track[i]);
            tube.lineTo(track[i + 1]);
        }
    p.setPen(QPen(QColor("#15181E"), 13, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(tube);
    p.setPen(QPen(QColor("#9AA0A6"), 1.2));
    for (int i = 0; i < m_points.size(); ++i) {
        if (!m_points[i].elevated || m_points[i].level < 2)
            continue;
        const double x = track[i].x();
        if (i % 3 == 0) { // un pilier tous les ~60 m
            p.setPen(QPen(QColor("#8C939E"), std::max(2.0, r.width() / std::max(1.0, m_total) * 4)));
            p.drawLine(QPointF(x, track[i].y() + 3), QPointF(x, ground));
        }
    }
    p.setPen(QPen(QColor("#C9CED6"), 7, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    QPainterPath deck;
    for (int i = 0; i + 1 < m_points.size(); ++i)
        if (m_points[i].elevated && m_points[i + 1].elevated && m_points[i].level > 1) {
            deck.moveTo(track[i] + QPointF(0, 3));
            deck.lineTo(track[i + 1] + QPointF(0, 3));
        }
    p.drawPath(deck);
    p.setPen(QPen(l->color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(track);

    // stations : quai souterrain + puits d'accès, ou quai aérien + escalier
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const QFontMetrics fm(small);
    double lastLabelRight = -1e9;
    for (int k = 0; k < m_stationS.size() && k < l->stops.size() + (l->isLoop() ? 1 : 0); ++k) {
        const double x = xOf(m_stationS[k]);
        // niveau de la voie au droit de la station
        double level = 0;
        for (const auto &pt : m_points)
            if (std::abs(pt.s - m_stationS[k]) < 1e-6 || pt.s >= m_stationS[k]) {
                level = pt.level;
                break;
            }
        const double y = yOf(level);
        const bool up = level > 0;
        p.setPen(QPen(QColor("#E8EAED"), 1.6));
        p.drawLine(QPointF(x, y + (up ? 6 : -6)), QPointF(x, ground)); // puits ou escalier
        p.setBrush(QColor("#E8EAED"));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(x - 5, ground - 4, 10, 4)); // édicule
        p.setBrush(QColor("#20242C"));
        p.setPen(QPen(l->color, 2));
        p.drawRoundedRect(QRectF(x - 13, y - 6, 26, 12), 3, 3); // quai
        // nom (sauté s'il chevaucherait le précédent)
        const Station *s = m_metro->station(l->stops[k % l->stops.size()]);
        if (!s)
            continue;
        const QString name = fm.elidedText(s->name, Qt::ElideRight, 150);
        const double w = fm.horizontalAdvance(name);
        const double lx = std::clamp(x - w / 2, 2.0, width() - w - 2);
        if (lx > lastLabelRight + 6) {
            p.setPen(QColor("#C9CDD2"));
            p.drawText(QPointF(lx, r.top() - 8 - (k % 2) * 14), name);
            lastLabelRight = lx + w;
        }
    }
    // légende d'altitude
    p.setPen(QColor("#7D8597"));
    p.drawText(QPointF(r.left() + 4, ground - 6), tr("sol"));
    p.drawText(QPointF(r.left() + 4, yOf(-30) - 4), tr("−30 m"));
}

void ProfileWidget::mouseMoveEvent(QMouseEvent *e)
{
    const int seg = segmentAt(e->position().x());
    if (seg != m_hover) {
        m_hover = seg;
        update();
    }
    const Line *l = m_metro->line(m_lineId);
    if (!l || seg < 0 || seg >= l->segmentCount()) {
        QToolTip::hideText();
        return;
    }
    const bool up = m_metro->segmentElevated(*l, seg);
    const Station *a = m_metro->station(l->stops[seg]);
    const Station *b = m_metro->station(l->stops[(seg + 1) % l->stops.size()]);
    const double now = m_metro->segmentCost(*l, seg, up), other = m_metro->segmentCost(*l, seg, !up);
    const auto loc = QLocale(QLocale::French);
    QString water;
    for (const auto &pt : m_points)
        if (pt.seg == seg && pt.water) {
            water = up ? tr("<br>Franchit un cours d'eau sur un pont") : tr("<br>Passe sous un cours d'eau (tunnel profond)");
            break;
        }
    QToolTip::showText(e->globalPosition().toPoint(),
                       tr("<b>%1 → %2</b><br>%3 · %4 M€%5<br><i>Clic : passer en %6 (%7 %8 M€)</i>")
                           .arg(a ? a->name : QString(), b ? b->name : QString())
                           .arg(up ? tr("Viaduc") : tr("Tunnel"))
                           .arg(loc.toString(now, 'f', 1))
                           .arg(water)
                           .arg(up ? tr("tunnel") : tr("viaduc"))
                           .arg(other > now ? tr("coût") : tr("remboursement"))
                           .arg(loc.toString(other > now ? other - now : (now - other) * Rules::Refund, 'f', 1)),
                       this);
}

void ProfileWidget::mousePressEvent(QMouseEvent *e)
{
    const int seg = segmentAt(e->position().x());
    if (seg >= 0 && e->button() == Qt::LeftButton)
        emit segmentClicked(seg);
}

void ProfileWidget::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
