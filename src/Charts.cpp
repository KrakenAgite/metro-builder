#include "Charts.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

// Jetons de texte et de surface (panneaux sombres)
const QColor kSurface(23, 26, 33);
const QColor kTextPrimary("#E8EAED");
const QColor kTextSecondary("#9AA0A6");
const QColor kGrid(255, 255, 255, 18);
const QColor kAxis(255, 255, 255, 60);

// Graduations « rondes » couvrant [lo, hi]
QVector<double> niceTicks(double lo, double hi, int count, double *outLo, double *outHi)
{
    if (hi - lo < 1e-9)
        hi = lo + 1;
    const double raw = (hi - lo) / count;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * mag >= raw) {
            step = m * mag;
            break;
        }
    const double first = std::floor(lo / step) * step;
    const double last = std::ceil(hi / step) * step;
    QVector<double> t;
    for (double v = first; v <= last + step * 0.5; v += step)
        t << (std::abs(v) < step * 1e-6 ? 0.0 : v);
    *outLo = first;
    *outHi = last;
    return t;
}

QRectF roundedEnd(const QRectF &r, bool up, QPainterPath *path, double radius = 4)
{
    // barre arrondie seulement côté valeur, ancrée carrée sur la ligne de base
    const double rad = std::min({radius, r.width() / 2, r.height()});
    QPainterPath p;
    if (up) {
        p.moveTo(r.bottomLeft());
        p.lineTo(r.left(), r.top() + rad);
        p.quadTo(r.topLeft(), QPointF(r.left() + rad, r.top()));
        p.lineTo(r.right() - rad, r.top());
        p.quadTo(r.topRight(), QPointF(r.right(), r.top() + rad));
        p.lineTo(r.bottomRight());
    } else {
        p.moveTo(r.topLeft());
        p.lineTo(r.left(), r.bottom() - rad);
        p.quadTo(r.bottomLeft(), QPointF(r.left() + rad, r.bottom()));
        p.lineTo(r.right() - rad, r.bottom());
        p.quadTo(r.bottomRight(), QPointF(r.right(), r.bottom() - rad));
        p.lineTo(r.topRight());
    }
    p.closeSubpath();
    *path = p;
    return r;
}

} // namespace

ChartWidget::ChartWidget(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

void ChartWidget::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    update();
}

void ChartWidget::setData(Kind kind, const QStringList &labels, const QStringList &tooltips,
                          const QVector<Series> &series, std::function<QString(double)> format, bool partialLast)
{
    m_kind = kind;
    m_labels = labels;
    m_tooltips = tooltips;
    m_series = series;
    m_format = format;
    m_partialLast = partialLast;
    double lo = 0, hi = 0; // l'axe part toujours de zéro
    bool any = false;
    for (const Series &s : series)
        for (double v : s.values) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
    if (!any || hi - lo < 1e-9)
        hi = lo + 1;
    m_ticks = niceTicks(lo, hi, 4, &m_min, &m_max);
    if (m_hover >= labels.size())
        m_hover = -1;
    update();
}

QRectF ChartWidget::plotRect() const
{
    const QFontMetrics fm(font());
    double labelW = 0;
    for (double t : m_ticks)
        labelW = std::max<double>(labelW, fm.horizontalAdvance(m_format ? m_format(t) : QString::number(t)));
    const double top = m_subtitle.isEmpty() ? 34 : 50;
    return QRectF(labelW + 14, top, width() - labelW - 24, height() - top - 26);
}

double ChartWidget::xOf(int i) const
{
    const QRectF r = plotRect();
    const int n = std::max<int>(1, m_labels.size());
    if (m_kind == BarChart)
        return r.left() + (i + 0.5) * r.width() / n;
    return n == 1 ? r.center().x() : r.left() + i * r.width() / (n - 1);
}

int ChartWidget::indexAt(double x) const
{
    const QRectF r = plotRect();
    const int n = m_labels.size();
    if (n == 0 || x < r.left() - 10 || x > r.right() + 10)
        return -1;
    if (m_kind == BarChart)
        return std::clamp(int((x - r.left()) / (r.width() / n)), 0, n - 1);
    return n == 1 ? 0 : std::clamp(int(std::round((x - r.left()) / (r.width() / (n - 1)))), 0, n - 1);
}

void ChartWidget::mouseMoveEvent(QMouseEvent *e)
{
    const int i = plotRect().adjusted(-10, -10, 10, 10).contains(e->position()) ? indexAt(e->position().x()) : -1;
    if (i != m_hover) {
        m_hover = i;
        update();
    }
}

void ChartWidget::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void ChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.85);
    const QFontMetrics sfm(small);

    // titre (+ sous-titre) ; légende à droite s'il y a plusieurs séries
    p.setFont(bold);
    p.setPen(kTextPrimary);
    p.drawText(QRectF(0, 4, width(), 20), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    if (!m_subtitle.isEmpty()) {
        p.setFont(small);
        p.setPen(kTextSecondary);
        p.drawText(QRectF(0, 22, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, m_subtitle);
    }
    if (m_series.size() > 1) {
        p.setFont(small);
        double x = width();
        for (int s = m_series.size() - 1; s >= 0; --s) {
            const double w = sfm.horizontalAdvance(m_series[s].name);
            x -= w;
            p.setPen(kTextSecondary);
            p.drawText(QRectF(x, 4, w, 20), Qt::AlignVCenter, m_series[s].name);
            x -= 14;
            p.setPen(Qt::NoPen);
            p.setBrush(m_series[s].color);
            p.drawRoundedRect(QRectF(x, 9, 9, 9), 2, 2);
            x -= 14;
        }
    }

    const QRectF r = plotRect();
    const int n = m_labels.size();
    auto yOf = [&](double v) { return r.bottom() - (v - m_min) / (m_max - m_min) * r.height(); };

    // grille et graduations (discrètes)
    p.setFont(small);
    for (double t : m_ticks) {
        const double y = yOf(t);
        p.setPen(QPen(t == 0 ? kAxis : kGrid, 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(kTextSecondary);
        p.drawText(QRectF(0, y - 9, r.left() - 8, 18), Qt::AlignRight | Qt::AlignVCenter,
                   m_format ? m_format(t) : QString::number(t));
    }
    if (n == 0) {
        p.setPen(kTextSecondary);
        p.drawText(r, Qt::AlignCenter, tr("Pas encore de données"));
        return;
    }
    // étiquettes d'abscisse : on en saute pour qu'elles ne se chevauchent pas
    double labelW = 0;
    for (const QString &l : m_labels)
        labelW = std::max<double>(labelW, sfm.horizontalAdvance(l));
    labelW += 12;
    const int every = std::max(1, int(std::ceil(labelW / std::max(1.0, r.width() / std::max(1, n)))));
    p.setPen(kTextSecondary);
    for (int i = 0; i < n; ++i) {
        if ((n - 1 - i) % every != 0)
            continue;
        // centrée sous le point, mais jamais coupée par les bords du graphique
        const double tw = sfm.horizontalAdvance(m_labels[i]);
        const double lx = std::clamp(xOf(i) - tw / 2, 0.0, width() - tw - 1.0);
        p.drawText(QRectF(lx, r.bottom() + 4, tw + 1, 18), Qt::AlignLeft | Qt::AlignTop, m_labels[i]);
    }

    if (m_kind == BarChart) {
        const int ns = m_series.size();
        const double slot = r.width() / n;
        const double groupW = std::min(slot * 0.72, 18.0 * ns + 2.0 * (ns - 1));
        const double barW = (groupW - 2.0 * (ns - 1)) / ns; // 2 px entre barres voisines
        for (int i = 0; i < n; ++i) {
            const bool partial = m_partialLast && i == n - 1;
            for (int s = 0; s < ns; ++s) {
                const double v = m_series[s].values.value(i);
                if (std::abs(v) < 1e-9)
                    continue;
                const double x0 = xOf(i) - groupW / 2 + s * (barW + 2);
                const double y0 = yOf(0), y1 = yOf(v);
                QPainterPath path;
                roundedEnd(QRectF(x0, std::min(y0, y1), barW, std::abs(y1 - y0)), v > 0, &path);
                QColor c = m_series[s].color;
                if (partial)
                    c.setAlphaF(0.45);
                if (m_hover >= 0 && m_hover != i)
                    c.setAlphaF(c.alphaF() * 0.55);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPath(path);
            }
        }
    } else {
        for (const Series &s : m_series) {
            QPolygonF pts;
            for (int i = 0; i < n; ++i)
                pts << QPointF(xOf(i), yOf(s.values.value(i)));
            if (m_kind == AreaChart && pts.size() > 1) {
                QPainterPath area;
                area.moveTo(pts.first().x(), yOf(std::max(0.0, m_min)));
                for (const QPointF &q : pts)
                    area.lineTo(q);
                area.lineTo(pts.last().x(), yOf(std::max(0.0, m_min)));
                area.closeSubpath();
                QLinearGradient g(0, r.top(), 0, r.bottom());
                QColor top = s.color, bottom = s.color;
                top.setAlphaF(0.32);
                bottom.setAlphaF(0.02);
                g.setColorAt(0, top);
                g.setColorAt(1, bottom);
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawPath(area);
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (pts.size() > 1)
                p.drawPolyline(pts);
            // dernier point (mois en cours) : marqueur cerclé de la couleur de fond
            const QPointF last = pts.last();
            p.setPen(QPen(kSurface, 2));
            p.setBrush(s.color);
            p.drawEllipse(last, 4.5, 4.5);
        }
    }

    // survol : réticule + infobulle
    if (m_hover < 0 || m_hover >= n)
        return;
    const double hx = xOf(m_hover);
    if (m_kind != BarChart) {
        p.setPen(QPen(kAxis, 1, Qt::DashLine));
        p.drawLine(QPointF(hx, r.top()), QPointF(hx, r.bottom()));
        for (const Series &s : m_series) {
            p.setPen(QPen(kSurface, 2));
            p.setBrush(s.color);
            p.drawEllipse(QPointF(hx, yOf(s.values.value(m_hover))), 5, 5);
        }
    }
    QStringList rows;
    const QString head = m_tooltips.value(m_hover, m_labels.value(m_hover));
    double w = QFontMetrics(bold).horizontalAdvance(head);
    for (const Series &s : m_series) {
        const QString line = QStringLiteral("%1  %2").arg(s.name, m_format(s.values.value(m_hover)));
        rows << line;
        w = std::max<double>(w, sfm.horizontalAdvance(line) + 18);
    }
    const double h = 26 + rows.size() * 18;
    QRectF tip(hx + 12, r.top() + 4, w + 20, h);
    if (tip.right() > width() - 2)
        tip.moveRight(hx - 12);
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.setBrush(QColor(12, 14, 18, 245));
    p.drawRoundedRect(tip, 7, 7);
    p.setFont(bold);
    p.setPen(kTextPrimary);
    p.drawText(QRectF(tip.left() + 10, tip.top() + 5, tip.width() - 20, 18), Qt::AlignLeft | Qt::AlignVCenter, head);
    p.setFont(small);
    for (int s = 0; s < rows.size(); ++s) {
        const double y = tip.top() + 25 + s * 18;
        p.setPen(Qt::NoPen);
        p.setBrush(m_series[s].color);
        p.drawRoundedRect(QRectF(tip.left() + 10, y + 4, 9, 9), 2, 2);
        p.setPen(kTextPrimary);
        p.drawText(QRectF(tip.left() + 26, y, tip.width() - 34, 18), Qt::AlignLeft | Qt::AlignVCenter, rows[s]);
    }
}

// ---------------------------------------------------------------------------

LineEconomicsWidget::LineEconomicsWidget(QWidget *parent)
    : QWidget(parent)
{
}

void LineEconomicsWidget::setRows(const QVector<Row> &rows)
{
    m_rows = rows;
    updateGeometry();
    update();
}

QSize LineEconomicsWidget::sizeHint() const
{
    return QSize(600, 30 + std::max<int>(1, m_rows.size()) * 30);
}

void LineEconomicsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.85);

    const double w = width();
    // colonnes : pastille + nom | voyageurs | recettes | coûts | barre de résultat
    const double cName = 0, cRiders = w * 0.30, cRev = w * 0.42, cCost = w * 0.54, cBar = w * 0.66;
    const double barW = w - cBar - 70;
    p.setFont(small);
    p.setPen(kTextSecondary);
    const double hy = 4;
    p.drawText(QRectF(cName, hy, cRiders, 18), Qt::AlignLeft | Qt::AlignVCenter, tr("Ligne"));
    p.drawText(QRectF(cRiders, hy, cRev - cRiders - 8, 18), Qt::AlignRight | Qt::AlignVCenter, tr("Voy./h"));
    p.drawText(QRectF(cRev, hy, cCost - cRev - 8, 18), Qt::AlignRight | Qt::AlignVCenter, tr("Recettes"));
    p.drawText(QRectF(cCost, hy, cBar - cCost - 8, 18), Qt::AlignRight | Qt::AlignVCenter, tr("Coûts"));
    p.drawText(QRectF(cBar, hy, barW + 70, 18), Qt::AlignHCenter | Qt::AlignVCenter, tr("Résultat / mois (M€)"));

    if (m_rows.isEmpty()) {
        p.drawText(QRectF(0, 30, w, 24), Qt::AlignCenter, tr("Aucune ligne en service"));
        return;
    }
    double maxAbs = 0.1;
    for (const Row &r : m_rows)
        maxAbs = std::max(maxAbs, std::abs(r.revenue - r.cost));
    const double zeroX = cBar + barW / 2;
    p.setPen(QPen(kAxis, 1));
    p.drawLine(QPointF(zeroX, 26), QPointF(zeroX, height() - 2));

    const auto loc = QLocale(QLocale::French);
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        const double y = 28 + i * 30;
        const QRectF badge(cName, y + 4, 20, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(r.color);
        p.drawEllipse(badge);
        QFont bf = bold;
        bf.setPointSizeF(bf.pointSizeF() * (r.code.size() > 1 ? 0.72 : 0.85));
        p.setFont(bf);
        p.setPen(r.color.lightnessF() > 0.62 ? QColor("#111") : Qt::white);
        p.drawText(badge, Qt::AlignCenter, r.code);
        p.setFont(font());
        p.setPen(kTextPrimary);
        p.drawText(QRectF(cName + 28, y, cRiders - cName - 32, 28), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(font()).elidedText(r.name, Qt::ElideRight, int(cRiders - cName - 32)));
        p.setPen(kTextSecondary);
        p.drawText(QRectF(cRiders, y, cRev - cRiders - 8, 28), Qt::AlignRight | Qt::AlignVCenter,
                   loc.toString(qRound(r.riders)));
        p.drawText(QRectF(cRev, y, cCost - cRev - 8, 28), Qt::AlignRight | Qt::AlignVCenter,
                   loc.toString(r.revenue, 'f', 2));
        p.drawText(QRectF(cCost, y, cBar - cCost - 8, 28), Qt::AlignRight | Qt::AlignVCenter,
                   loc.toString(r.cost, 'f', 2));

        // barre divergente : à droite si bénéficiaire, à gauche si déficitaire
        const double res = r.revenue - r.cost;
        const double len = std::abs(res) / maxAbs * (barW / 2);
        const QRectF bar = res >= 0 ? QRectF(zeroX + 1, y + 8, len, 12) : QRectF(zeroX - 1 - len, y + 8, len, 12);
        QPainterPath path;
        path.addRoundedRect(bar, 4, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(r.color);
        p.drawPath(path);
        p.setPen(kTextPrimary);
        const QString txt = QStringLiteral("%1%2").arg(res >= 0 ? "+" : "−").arg(loc.toString(std::abs(res), 'f', 2));
        if (res >= 0)
            p.drawText(QRectF(bar.right() + 6, y, 64, 28), Qt::AlignLeft | Qt::AlignVCenter, txt);
        else
            p.drawText(QRectF(bar.left() - 70, y, 64, 28), Qt::AlignRight | Qt::AlignVCenter, txt);
    }
}
