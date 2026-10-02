#include "DensityLayer.h"
#include "Metro.h"

#include <algorithm>
#include <cmath>

namespace {

// Palettes vives (du plus faible au plus fort)
const QVector<QColor> kPopulation = {QColor("#3B0F9E"), QColor("#7A1FD1"), QColor("#C21FB5"), QColor("#F2367A"),
                                     QColor("#FF7A3D"), QColor("#FFC02E"), QColor("#FFF35C")};
const QVector<QColor> kJobs = {QColor("#0A3A8C"), QColor("#0066D6"), QColor("#00A8F0"), QColor("#00DCC8"),
                               QColor("#3EF08A"), QColor("#B6F94A"), QColor("#FFF95B")};
const QVector<QColor> kDemand = {QColor("#FF1E56"), QColor("#FF6A1E"), QColor("#FFB800"),
                                 QColor("#E2F23A"), QColor("#7DF25B"), QColor("#00E6A0")};

// Flou gaussien séparable (sigma en cellules), normalisé : pas d'assombrissement au bord de la grille
QVector<float> blur(const QVector<float> &src, int w, int h, double sigma)
{
    const int r = int(std::ceil(sigma * 3));
    QVector<float> k(2 * r + 1);
    float sum = 0;
    for (int i = -r; i <= r; ++i)
        sum += k[i + r] = float(std::exp(-i * i / (2 * sigma * sigma)));
    for (float &v : k)
        v /= sum;
    QVector<float> tmp(w * h, 0), out(w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0, wsum = 0;
            for (int i = -r; i <= r; ++i)
                if (x + i >= 0 && x + i < w) {
                    acc += src[y * w + x + i] * k[i + r];
                    wsum += k[i + r];
                }
            tmp[y * w + x] = acc / wsum;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0, wsum = 0;
            for (int i = -r; i <= r; ++i)
                if (y + i >= 0 && y + i < h) {
                    acc += tmp[(y + i) * w + x] * k[i + r];
                    wsum += k[i + r];
                }
            out[y * w + x] = acc / wsum;
        }
    return out;
}

// Arrondi « lisible » : 1, 1,5, 2, 2,5, 3, 4, 5, 6, 8 × 10^n
double niceRound(double v)
{
    if (v <= 0)
        return 0;
    const double e = std::pow(10.0, std::floor(std::log10(v)));
    static const double steps[] = {1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10};
    double best = steps[0];
    for (double s : steps)
        if (std::abs(s * e - v) < std::abs(best * e - v))
            best = s;
    return best * e;
}

// Paliers adaptés à la ville : quantiles des cellules habitées, arrondis
QVector<double> adaptiveThresholds(const QVector<float> &field, double minimum)
{
    QVector<float> v;
    for (float x : field)
        if (x > 1)
            v << x;
    if (v.size() < 10)
        return {minimum, minimum * 2, minimum * 4, minimum * 8};
    std::sort(v.begin(), v.end());
    QVector<double> t;
    for (double q : {0.08, 0.22, 0.38, 0.54, 0.70, 0.84, 0.94}) {
        double x = niceRound(std::max(minimum, double(v[int(q * (v.size() - 1))])));
        if (!t.isEmpty() && x <= t.last())
            x = niceRound(t.last() * 1.25) > t.last() ? niceRound(t.last() * 1.25) : t.last() * 1.25;
        t << x;
    }
    return t;
}

} // namespace

void DensityLayer::clear()
{
    m_kind = None;
    m_field.clear();
    m_weight.clear();
    m_image = QImage();
    m_contours.clear();
    m_thresholds.clear();
    m_colors.clear();
}

void DensityLayer::build(Kind kind, const DemandGrid &g)
{
    clear();
    if (kind == None || g.w == 0)
        return;
    m_kind = kind;
    m_w = g.w;
    m_h = g.h;
    m_cell = g.cell;
    m_origin = g.origin;
    m_rect = QRectF(g.origin, QSizeF(g.w * g.cell, g.h * g.cell));
    const int n = m_w * m_h;

    QVector<float> raw(n);
    switch (kind) {
    case Population:
        for (int i = 0; i < n; ++i)
            raw[i] = float(g.pop[i]);
        m_field = blur(raw, m_w, m_h, 1.0);
        m_thresholds = adaptiveThresholds(m_field, 5);
        m_colors = kPopulation;
        break;
    case Jobs:
        for (int i = 0; i < n; ++i)
            raw[i] = float(g.jobs[i]);
        m_field = blur(raw, m_w, m_h, 1.0);
        m_thresholds = adaptiveThresholds(m_field, 5);
        m_colors = kJobs;
        break;
    case Demand: {
        // part captée lissée, pondérée par le volume de déplacements
        QVector<float> pot(n), served(n);
        for (int i = 0; i < n; ++i) {
            pot[i] = float(g.potential[i]);
            served[i] = float(g.potential[i] * g.servedFrac[i]);
        }
        m_weight = blur(pot, m_w, m_h, 1.0);
        const QVector<float> s = blur(served, m_w, m_h, 1.0);
        m_field.resize(n);
        for (int i = 0; i < n; ++i)
            m_field[i] = m_weight[i] > 1e-3f ? s[i] / m_weight[i] : 0;
        m_thresholds = {0.15, 0.3, 0.45, 0.6, 0.8};
        m_colors = kDemand;
        break;
    }
    case None:
        break;
    }

    // Image des aplats : 4 pixels par cellule, valeurs interpolées puis découpées en paliers
    double ref = 1;
    if (kind == Demand) {
        QVector<float> sorted;
        for (float v : m_weight)
            if (v > 0.5f)
                sorted << v;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.isEmpty())
            ref = std::max(1.0, double(sorted[int(sorted.size() * 0.95)]));
    }
    const int U = 4;
    m_image = QImage(m_w * U, m_h * U, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);
    for (int iy = 0; iy < m_image.height(); ++iy) {
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(iy));
        for (int ix = 0; ix < m_image.width(); ++ix) {
            const QPointF world = m_origin + QPointF((ix + 0.5) / U * m_cell, (iy + 0.5) / U * m_cell);
            const int band = bandOf(sample(m_field, world));
            double alpha;
            if (kind == Demand) {
                const double w = sample(m_weight, world);
                if (w < 0.5)
                    continue;
                alpha = 0.3 + 0.65 * std::min(1.0, std::sqrt(w / ref));
            } else {
                if (band == 0)
                    continue;
                alpha = 0.28 + 0.3 * band / double(m_thresholds.size());
            }
            const QColor c = bandColor(band);
            line[ix] = qPremultiply(qRgba(c.red(), c.green(), c.blue(), int(alpha * 255)));
        }
    }

    // Courbes de niveau (marching squares) pour les densités
    if (!isDensity())
        return;
    auto f = [&](int x, int y) -> float {
        return (x < 0 || y < 0 || x >= m_w || y >= m_h) ? 0.f : m_field[y * m_w + x];
    };
    auto center = [&](double x, double y) { return m_origin + QPointF((x + 0.5) * m_cell, (y + 0.5) * m_cell); };
    for (double t : m_thresholds) {
        QPainterPath path;
        for (int y = -1; y < m_h; ++y) {
            for (int x = -1; x < m_w; ++x) {
                const float v0 = f(x, y), v1 = f(x + 1, y), v2 = f(x + 1, y + 1), v3 = f(x, y + 1);
                const int idx = (v0 >= t) << 3 | (v1 >= t) << 2 | (v2 >= t) << 1 | (v3 >= t);
                if (idx == 0 || idx == 15)
                    continue;
                auto lerp = [&](double ax, double ay, float va, double bx, double by, float vb) {
                    const double k = std::abs(vb - va) < 1e-6 ? 0.5 : (t - va) / (vb - va);
                    return center(ax + (bx - ax) * k, ay + (by - ay) * k);
                };
                const QPointF T = lerp(x, y, v0, x + 1, y, v1);
                const QPointF R = lerp(x + 1, y, v1, x + 1, y + 1, v2);
                const QPointF B = lerp(x, y + 1, v3, x + 1, y + 1, v2);
                const QPointF L = lerp(x, y, v0, x, y + 1, v3);
                auto seg = [&](const QPointF &a, const QPointF &b) {
                    path.moveTo(a);
                    path.lineTo(b);
                };
                switch (idx) {
                case 1: case 14: seg(L, B); break;
                case 2: case 13: seg(B, R); break;
                case 3: case 12: seg(L, R); break;
                case 4: case 11: seg(T, R); break;
                case 5: seg(L, T); seg(B, R); break;
                case 6: case 9: seg(T, B); break;
                case 7: case 8: seg(L, T); break;
                case 10: seg(T, R); seg(L, B); break;
                }
            }
        }
        m_contours << path;
    }
}

double DensityLayer::sample(const QVector<float> &f, const QPointF &world) const
{
    if (f.isEmpty())
        return 0;
    const double gx = (world.x() - m_origin.x()) / m_cell - 0.5;
    const double gy = (world.y() - m_origin.y()) / m_cell - 0.5;
    const int x0 = int(std::floor(gx)), y0 = int(std::floor(gy));
    const double fx = gx - x0, fy = gy - y0;
    auto at = [&](int x, int y) -> double {
        return (x < 0 || y < 0 || x >= m_w || y >= m_h) ? 0.0 : f[y * m_w + x];
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy)
           + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

double DensityLayer::valueAt(const QPointF &world) const
{
    return sample(m_field, world);
}

double DensityLayer::demandAt(const QPointF &world) const
{
    return sample(m_weight, world);
}

int DensityLayer::bandOf(double value) const
{
    int b = 0;
    while (b < m_thresholds.size() && value >= m_thresholds[b])
        ++b;
    return b;
}

QColor DensityLayer::bandColor(int band) const
{
    if (m_colors.isEmpty())
        return Qt::transparent;
    if (m_kind == Demand)
        return m_colors[std::clamp(band, 0, int(m_colors.size()) - 1)];
    return band <= 0 ? QColor(Qt::transparent) : m_colors[std::clamp(band - 1, 0, int(m_colors.size()) - 1)];
}

QString DensityLayer::title() const
{
    switch (m_kind) {
    case Population: return QObject::tr("Densité d'habitants");
    case Jobs: return QObject::tr("Densité d'emplois");
    case Demand: return QObject::tr("Demande captée par le métro");
    case None: break;
    }
    return {};
}

QString DensityLayer::unit() const
{
    switch (m_kind) {
    case Population: return QObject::tr("hab/ha");
    case Jobs: return QObject::tr("emplois/ha");
    case Demand: return QStringLiteral("%");
    case None: break;
    }
    return {};
}

QString DensityLayer::formatThreshold(int i) const
{
    if (i < 0 || i >= m_thresholds.size())
        return {};
    if (m_kind == Demand)
        return QString::number(qRound(m_thresholds[i] * 100));
    return QString::number(qRound(m_thresholds[i]));
}
