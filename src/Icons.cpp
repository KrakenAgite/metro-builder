#include "Icons.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace Icons {

namespace {

QPainterPath poly(std::initializer_list<QPointF> pts, bool closed = false)
{
    QPainterPath path;
    auto it = pts.begin();
    path.moveTo(*it);
    for (++it; it != pts.end(); ++it)
        path.lineTo(*it);
    if (closed)
        path.closeSubpath();
    return path;
}

void draw(QPainter &p, Id id, const QColor &c)
{
    QPen pen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto fill = [&] {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
    };
    auto stroke = [&] {
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    };

    switch (id) {
    case Pointer:
        p.drawPath(poly({{5.5, 3.5}, {19, 11.5}, {12.5, 13.2}, {9.2, 19.5}}, true));
        break;
    case Station:
        p.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        fill();
        p.drawEllipse(QPointF(12, 12), 3, 3);
        break;
    case Route: {
        QPainterPath path;
        path.moveTo(8, 19);
        path.lineTo(15, 19);
        path.cubicTo(19.7, 19, 19.7, 12, 15, 12);
        path.lineTo(9, 12);
        path.cubicTo(4.3, 12, 4.3, 5, 9, 5);
        path.lineTo(16, 5);
        p.drawPath(path);
        p.drawEllipse(QPointF(5.5, 19), 2.2, 2.2);
        p.drawEllipse(QPointF(18.5, 5), 2.2, 2.2);
        break;
    }
    case Trash:
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawPath(poly({{9, 7}, {9, 4.5}, {15, 4.5}, {15, 7}}));
        p.drawPath(poly({{6, 7}, {7, 20}, {17, 20}, {18, 7}}));
        p.drawLine(QPointF(10, 11), QPointF(10, 16));
        p.drawLine(QPointF(14, 11), QPointF(14, 16));
        break;
    case Flame: {
        QPainterPath path;
        path.moveTo(12, 2.8);
        path.cubicTo(13.5, 6.5, 18.5, 9, 18.5, 14.5);
        path.cubicTo(18.5, 18.3, 15.6, 21, 12, 21);
        path.cubicTo(8.4, 21, 5.5, 18.3, 5.5, 14.8);
        path.cubicTo(5.5, 12, 7, 10.2, 8.5, 8.8);
        path.cubicTo(8.7, 11, 9.6, 12.4, 11, 12.8);
        path.cubicTo(10.4, 9.5, 10.8, 5.5, 12, 2.8);
        p.drawPath(path);
        break;
    }
    case Users: {
        p.drawEllipse(QPointF(9, 8), 3.5, 3.5);
        QPainterPath a;
        a.moveTo(2.5, 20);
        a.cubicTo(2.5, 15.8, 5.5, 13.5, 9, 13.5);
        a.cubicTo(12.5, 13.5, 15.5, 15.8, 15.5, 20);
        p.drawPath(a);
        QPainterPath b;
        b.moveTo(15.5, 4.8);
        b.cubicTo(18, 5.4, 18.8, 9.8, 15.8, 11.2);
        p.drawPath(b);
        QPainterPath s;
        s.moveTo(17.5, 14);
        s.cubicTo(19.8, 14.8, 21.5, 16.8, 21.5, 20);
        p.drawPath(s);
        break;
    }
    case Gauge: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(3, 5, 18, 18), 200);
        arc.arcTo(QRectF(3, 5, 18, 18), 200, -220);
        p.drawPath(arc);
        p.drawLine(QPointF(12, 14), QPointF(16, 9.5));
        fill();
        p.drawEllipse(QPointF(12, 14), 1.8, 1.8);
        break;
    }
    case EyeOff: {
        QPainterPath eye;
        eye.moveTo(2.5, 12);
        eye.cubicTo(5.5, 6.5, 18.5, 6.5, 21.5, 12);
        eye.cubicTo(18.5, 17.5, 5.5, 17.5, 2.5, 12);
        p.drawPath(eye);
        p.drawEllipse(QPointF(12, 12), 2.8, 2.8);
        p.drawLine(QPointF(4, 4), QPointF(20, 20));
        break;
    }
    case Pause:
        fill();
        p.drawRoundedRect(QRectF(6.5, 5, 4, 14), 1.2, 1.2);
        p.drawRoundedRect(QRectF(13.5, 5, 4, 14), 1.2, 1.2);
        break;
    case Play:
        p.setBrush(c);
        p.drawPath(poly({{8, 5.5}, {18.5, 12}, {8, 18.5}}, true));
        break;
    case Fast:
        p.setBrush(c);
        p.drawPath(poly({{3.5, 6.5}, {11.5, 12}, {3.5, 17.5}}, true));
        p.drawPath(poly({{12.5, 6.5}, {20.5, 12}, {12.5, 17.5}}, true));
        break;
    case Faster:
        p.setBrush(c);
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.drawPath(poly({{1.5, 7.5}, {8, 12}, {1.5, 16.5}}, true));
        p.drawPath(poly({{8.5, 7.5}, {15, 12}, {8.5, 16.5}}, true));
        p.drawPath(poly({{15.5, 7.5}, {22, 12}, {15.5, 16.5}}, true));
        break;
    case Recenter:
        p.drawPath(poly({{4, 9}, {4, 4}, {9, 4}}));
        p.drawPath(poly({{15, 4}, {20, 4}, {20, 9}}));
        p.drawPath(poly({{20, 15}, {20, 20}, {15, 20}}));
        p.drawPath(poly({{9, 20}, {4, 20}, {4, 15}}));
        p.drawEllipse(QPointF(12, 12), 2.8, 2.8);
        break;
    case Search:
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        p.drawLine(QPointF(15.5, 15.5), QPointF(20.5, 20.5));
        break;
    case Menu:
        for (double y : {6.0, 12.0, 18.0})
            p.drawLine(QPointF(4, y), QPointF(20, y));
        break;
    case Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Minus:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case ArrowUp:
        p.drawLine(QPointF(12, 19), QPointF(12, 5));
        p.drawPath(poly({{6, 11}, {12, 5}, {18, 11}}));
        break;
    case ArrowDown:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawPath(poly({{6, 13}, {12, 19}, {18, 13}}));
        break;
    case Close:
        p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
        p.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
        break;
    case Swap:
        p.drawLine(QPointF(4, 8), QPointF(19, 8));
        p.drawPath(poly({{15, 4}, {19, 8}, {15, 12}}));
        p.drawLine(QPointF(20, 16), QPointF(5, 16));
        p.drawPath(poly({{9, 12}, {5, 16}, {9, 20}}));
        break;
    case Loop: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4.5, 4.5, 15, 15), 60);
        arc.arcTo(QRectF(4.5, 4.5, 15, 15), 60, 300);
        p.drawPath(arc);
        const QPointF end = arc.currentPosition();
        p.drawPath(poly({end + QPointF(-4.5, -1.5), end, end + QPointF(-0.5, -4.8)}));
        break;
    }
    case Wallet:
        p.drawRoundedRect(QRectF(3, 6.5, 18, 13), 2.5, 2.5);
        p.drawPath(poly({{5, 6.5}, {15.5, 3.5}, {17, 6.5}}));
        p.drawRoundedRect(QRectF(14.5, 11, 6.5, 4.5), 1.5, 1.5);
        break;
    case Calendar:
        p.drawRoundedRect(QRectF(3.5, 5, 17, 15.5), 2.5, 2.5);
        p.drawLine(QPointF(3.5, 10), QPointF(20.5, 10));
        p.drawLine(QPointF(8, 3), QPointF(8, 7));
        p.drawLine(QPointF(16, 3), QPointF(16, 7));
        break;
    case Train:
        p.drawRoundedRect(QRectF(5.5, 3, 13, 14), 3, 3);
        p.drawLine(QPointF(5.5, 10), QPointF(18.5, 10));
        p.drawLine(QPointF(9, 17), QPointF(7, 21));
        p.drawLine(QPointF(15, 17), QPointF(17, 21));
        fill();
        p.drawEllipse(QPointF(9, 13.5), 1.2, 1.2);
        p.drawEllipse(QPointF(15, 13.5), 1.2, 1.2);
        break;
    case Target:
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawEllipse(QPointF(12, 12), 4.5, 4.5);
        fill();
        p.drawEllipse(QPointF(12, 12), 1.5, 1.5);
        break;
    case Home:
        p.drawPath(poly({{3, 11}, {12, 3.8}, {21, 11}}));
        p.drawPath(poly({{5.5, 9.3}, {5.5, 20}, {18.5, 20}, {18.5, 9.3}}));
        p.drawPath(poly({{10, 20}, {10, 14.5}, {14, 14.5}, {14, 20}}));
        break;
    case Briefcase:
        p.drawRoundedRect(QRectF(3, 7.5, 18, 12.5), 2.2, 2.2);
        p.drawPath(poly({{9, 7.5}, {9, 4.5}, {15, 4.5}, {15, 7.5}}));
        p.drawLine(QPointF(3, 13), QPointF(21, 13));
        break;
    case Save:
        p.drawPath(poly({{4, 4}, {16.5, 4}, {20, 7.5}, {20, 20}, {4, 20}}, true));
        p.drawPath(poly({{8, 4}, {8, 8.5}, {15, 8.5}, {15, 4}}));
        p.drawRect(QRectF(7.5, 13, 9, 7));
        break;
    case FolderOpen:
        p.drawPath(poly({{3, 19}, {3, 5.5}, {9, 5.5}, {11, 8}, {18.5, 8}, {18.5, 11}}));
        p.drawPath(poly({{3, 19}, {6, 11}, {21.5, 11}, {18.5, 19}}, true));
        break;
    case File:
        p.drawPath(poly({{6, 3}, {14, 3}, {19, 8}, {19, 21}, {6, 21}}, true));
        p.drawPath(poly({{14, 3}, {14, 8}, {19, 8}}));
        p.drawLine(QPointF(9, 13), QPointF(16, 13));
        p.drawLine(QPointF(9, 17), QPointF(14, 17));
        break;
    case Download:
        p.drawLine(QPointF(12, 4), QPointF(12, 15));
        p.drawPath(poly({{7, 10.5}, {12, 15.5}, {17, 10.5}}));
        p.drawLine(QPointF(5, 20), QPointF(19, 20));
        break;
    case Info:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        fill();
        p.drawEllipse(QPointF(12, 7.6), 1.3, 1.3);
        break;
    case Moon: {
        QPainterPath moon;
        moon.moveTo(20, 14.5);
        moon.cubicTo(18.8, 18.2, 15.3, 20.8, 11.4, 20.8);
        moon.cubicTo(6.4, 20.8, 3.2, 17, 3.2, 12.4);
        moon.cubicTo(3.2, 8.5, 5.8, 5.1, 9.5, 4);
        moon.cubicTo(8.3, 9.8, 13.8, 15.6, 20, 14.5);
        p.drawPath(moon);
        break;
    }
    case Sun:
        p.drawEllipse(QPointF(12, 12), 4, 4);
        for (int i = 0; i < 8; ++i) {
            const double a = i * M_PI / 4;
            p.drawLine(QPointF(12 + 7 * std::cos(a), 12 + 7 * std::sin(a)),
                       QPointF(12 + 9.5 * std::cos(a), 12 + 9.5 * std::sin(a)));
        }
        break;
    case Schema:
        // petit plan octilinéaire : deux lignes et une correspondance
        p.drawPath(poly({{3, 7}, {9, 7}, {16, 14}, {21, 14}}));
        p.drawPath(poly({{12.5, 3}, {12.5, 21}}));
        p.setBrush(QColor(0, 0, 0, 0));
        p.drawEllipse(QPointF(12.5, 10.5), 2.4, 2.4);
        fill();
        p.drawEllipse(QPointF(3, 7), 1.6, 1.6);
        p.drawEllipse(QPointF(21, 14), 1.6, 1.6);
        break;
    case Trophy:
        p.drawLine(QPointF(7, 4), QPointF(17, 4));
        {
            QPainterPath cup;
            cup.moveTo(7, 4);
            cup.lineTo(7, 9);
            cup.cubicTo(7, 13, 9.5, 15, 12, 15);
            cup.cubicTo(14.5, 15, 17, 13, 17, 9);
            cup.lineTo(17, 4);
            p.drawPath(cup);
            QPainterPath ears;
            ears.moveTo(7, 6);
            ears.cubicTo(3, 6, 3, 11, 7.6, 11.4);
            ears.moveTo(17, 6);
            ears.cubicTo(21, 6, 21, 11, 16.4, 11.4);
            p.drawPath(ears);
        }
        p.drawLine(QPointF(12, 15), QPointF(12, 18.5));
        p.drawLine(QPointF(8, 20), QPointF(16, 20));
        break;
    case Speaker:
    case SpeakerOff:
        p.drawPath(poly({{3.5, 9.5}, {7.5, 9.5}, {12, 5}, {12, 19}, {7.5, 14.5}, {3.5, 14.5}}, true));
        if (id == Speaker) {
            QPainterPath w1;
            w1.arcMoveTo(QRectF(10, 8, 6, 8), -50);
            w1.arcTo(QRectF(10, 8, 6, 8), -50, 100);
            QPainterPath w2;
            w2.arcMoveTo(QRectF(9, 4.5, 11, 15), -50);
            w2.arcTo(QRectF(9, 4.5, 11, 15), -50, 100);
            p.drawPath(w1);
            p.drawPath(w2);
        } else {
            p.drawLine(QPointF(15.5, 9.5), QPointF(20.5, 14.5));
            p.drawLine(QPointF(20.5, 9.5), QPointF(15.5, 14.5));
        }
        break;
    case Logo: {
        // carré arrondi sombre ; un « M » dessiné comme un plan de métro : trois lignes et leurs stations
        p.setPen(Qt::NoPen);
        QLinearGradient bg(0, 0, 24, 24);
        bg.setColorAt(0, QColor("#1E2533"));
        bg.setColorAt(1, QColor("#10141B"));
        p.setBrush(bg);
        p.setPen(QPen(QColor(255, 255, 255, 46), 0.6)); // liseré : se détache aussi sur fond sombre
        p.drawRoundedRect(QRectF(0.5, 0.5, 23, 23), 5.5, 5.5);
        auto seg = [&](const QColor &col, std::initializer_list<QPointF> pts) {
            p.setPen(QPen(col, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(poly(pts));
        };
        seg(QColor("#FFCD00"), {{6, 18.5}, {6, 6}});           // jambe gauche
        seg(QColor("#3987E5"), {{6, 6}, {12, 12}, {18, 6}});   // le V central
        seg(QColor("#E3051C"), {{18, 6}, {18, 18.5}});         // jambe droite
        auto station = [&](const QPointF &c, double r) {
            p.setPen(QPen(QColor("#10141B"), 1.1));
            p.setBrush(Qt::white);
            p.drawEllipse(c, r, r);
        };
        station({6, 6}, 2.1);    // correspondances
        station({18, 6}, 2.1);
        station({12, 12}, 1.6);
        station({6, 18.5}, 1.4); // terminus
        station({18, 18.5}, 1.4);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::NoBrush);
        break;
    }
    }
    stroke();
}

} // namespace

QPixmap pixmap(Id id, int size, const QColor &color)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    draw(p, id, color);
    return pm;
}

QIcon icon(Id id, const QColor &normal, const QColor &active)
{
    QIcon ic;
    for (int size : {16, 20, 24, 32, 48}) {
        ic.addPixmap(pixmap(id, size, normal), QIcon::Normal, QIcon::Off);
        ic.addPixmap(pixmap(id, size, active), QIcon::Active, QIcon::Off);
        ic.addPixmap(pixmap(id, size, active), QIcon::Normal, QIcon::On);
        ic.addPixmap(pixmap(id, size, active), QIcon::Active, QIcon::On);
        ic.addPixmap(pixmap(id, size, QColor("#5F6368")), QIcon::Disabled, QIcon::Off);
    }
    return ic;
}

QIcon wagons(int count)
{
    QIcon ic;
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    for (const auto &[color, mode, state] :
         {std::tuple{QColor("#C9CDD2"), QIcon::Normal, QIcon::Off}, std::tuple{QColor(Qt::white), QIcon::Normal, QIcon::On},
          std::tuple{QColor(Qt::white), QIcon::Active, QIcon::On}, std::tuple{QColor(Qt::white), QIcon::Active, QIcon::Off}}) {
        QPixmap pm(QSize(44, 14) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const double w = (44.0 - (count - 1) * 2) / count;
        for (int i = 0; i < count; ++i)
            p.drawRoundedRect(QRectF(i * (w + 2), 3, w, 8), 2, 2);
        p.end();
        ic.addPixmap(pm, mode, state);
    }
    return ic;
}

QIcon appIcon()
{
    QIcon ic;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        ic.addPixmap(pixmap(Logo, size, Qt::white));
    return ic;
}

} // namespace Icons
