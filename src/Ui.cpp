#include "Ui.h"
#include "Audio.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace Theme {

QColor loadColor(double ratio)
{
    return ratio < 0.7 ? Success : ratio < 1.0 ? Warning : Danger;
}

void apply()
{
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#171A21"));
    pal.setColor(QPalette::WindowText, Text);
    pal.setColor(QPalette::Base, QColor("#1F232B"));
    pal.setColor(QPalette::AlternateBase, QColor("#242933"));
    pal.setColor(QPalette::Text, Text);
    pal.setColor(QPalette::Button, QColor("#242933"));
    pal.setColor(QPalette::ButtonText, Text);
    pal.setColor(QPalette::Highlight, Accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor("#1C2028"));
    pal.setColor(QPalette::ToolTipText, Text);
    pal.setColor(QPalette::PlaceholderText, QColor("#6B7280"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#5F6368"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#5F6368"));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#5F6368"));
    qApp->setPalette(pal);

    qApp->setStyleSheet(R"(
        * { outline: none; }
        Card QLabel { color: #E8EAED; background: transparent; }
        QLabel[role="caption"] { color: #9AA0A6; font-size: 8pt; }
        QLabel[role="section"] { color: #9AA0A6; font-size: 8pt; font-weight: 600; letter-spacing: 1px; }
        QLabel[role="value"] { color: #F1F3F4; font-size: 10.5pt; font-weight: 600; }
        QLabel[role="title"] { color: #FFFFFF; font-size: 22pt; font-weight: 700; }
        QLabel[role="subtitle"] { color: #9AA0A6; font-size: 10pt; }
        QLabel[role="status"] { color: #9AA0A6; font-size: 9pt; }
        QLabel[role="error"] { color: #FF8A96; font-size: 9pt; }
        QLabel[role="advice"] { color: #F5A524; font-size: 9pt; }

        Card QLineEdit, Card QComboBox {
            background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.09);
            border-radius: 9px; padding: 6px 10px; color: #E8EAED;
            selection-background-color: #4C8DFF;
        }
        Card QLineEdit:focus, Card QComboBox:focus { border-color: #4C8DFF; }
        Card QLineEdit#search { background: transparent; border: none; font-size: 11pt; padding: 4px 2px; }
        Card QLineEdit#flat {
            background: transparent; border: 1px solid transparent; font-size: 12.5pt;
            font-weight: 700; padding: 3px 6px;
        }
        Card QLineEdit#flat:hover { border-color: rgba(255,255,255,0.09); }
        Card QLineEdit#flat:focus { background: rgba(255,255,255,0.06); border-color: #4C8DFF; }
        Card QComboBox::drop-down { border: none; width: 18px; }
        Card QComboBox QAbstractItemView {
            background: #1C2028; color: #E8EAED; border: 1px solid rgba(255,255,255,0.1);
            selection-background-color: rgba(76,141,255,0.35); padding: 4px;
        }

        Card QToolButton {
            background: transparent; border: none; border-radius: 10px; color: #C9CDD2; padding: 4px;
        }
        Card QToolButton:hover { background: rgba(255,255,255,0.08); color: #FFFFFF; }
        Card QToolButton:pressed { background: rgba(255,255,255,0.14); }
        Card QToolButton:checked { background: #4C8DFF; color: #FFFFFF; }
        Card QToolButton[variant="danger"]:hover { background: rgba(240,77,94,0.22); }
        Card QToolButton[variant="segment"] {
            background: rgba(255,255,255,0.05); border-radius: 8px; padding: 6px 8px; font-weight: 600;
        }
        Card QToolButton[variant="segment"]:checked { background: #4C8DFF; }
        Card QToolButton::menu-indicator { image: none; width: 0; }

        QPushButton[variant="primary"] {
            background: #4C8DFF; color: white; border: none; border-radius: 9px;
            padding: 8px 16px; font-weight: 600;
        }
        QPushButton[variant="primary"]:hover { background: #6A9FFF; }
        QPushButton[variant="primary"]:pressed { background: #3B78E7; }
        QPushButton[variant="primary"]:disabled { background: #2B3446; color: #7D8597; }
        QPushButton[variant="danger"] {
            background: rgba(240,77,94,0.14); color: #FF8A96; border: none; border-radius: 9px;
            padding: 8px 14px; font-weight: 600;
        }
        QPushButton[variant="danger"]:hover { background: rgba(240,77,94,0.26); }

        Card QListWidget { background: transparent; border: none; color: #E8EAED; }

        Card QProgressBar {
            background: rgba(255,255,255,0.08); border: none; border-radius: 3px;
            max-height: 6px; min-height: 6px; text-align: center;
        }
        Card QProgressBar::chunk { background: #4C8DFF; border-radius: 3px; }

        QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
        QScrollBar::handle:vertical { background: rgba(255,255,255,0.18); border-radius: 3px; min-height: 24px; }
        QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.3); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

        QMenu {
            background: #1C2028; color: #E8EAED; border: 1px solid rgba(255,255,255,0.1);
            border-radius: 10px; padding: 6px;
        }
        QMenu::item { padding: 7px 28px 7px 10px; border-radius: 6px; }
        QMenu::item:selected { background: rgba(76,141,255,0.28); }
        QMenu::icon { padding-left: 8px; }
        QMenu::separator { height: 1px; background: rgba(255,255,255,0.08); margin: 5px 8px; }

        QToolTip {
            background: #1C2028; color: #E8EAED; border: 1px solid rgba(255,255,255,0.12);
            border-radius: 6px; padding: 5px 8px;
        }
        QLabel#toast {
            background: rgba(23,26,33,0.95); color: #E8EAED; border: 1px solid rgba(255,255,255,0.12);
            border-radius: 17px; padding: 8px 18px; font-weight: 500;
        }
        QLabel#toast[error="true"] { background: rgba(120,28,40,0.95); border-color: rgba(240,77,94,0.6); }
    )");
}

} // namespace Theme

// ---------------------------------------------------------------------------

Card::Card(QWidget *parent, int radius)
    : QFrame(parent)
    , m_radius(radius)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setContentsMargins(Shadow, Shadow - 2, Shadow, Shadow + 2);
}

void Card::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(Shadow, Shadow - 2, -Shadow, -Shadow - 2);
    p.setPen(Qt::NoPen);
    for (int i = Shadow; i >= 1; i -= 2) {
        p.setBrush(QColor(0, 0, 0, 10));
        p.drawRoundedRect(r.adjusted(-i, -i + 3, i, i + 3), m_radius + i, m_radius + i);
    }
    p.setBrush(QColor(23, 26, 33, 240));
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
}

QToolButton *iconButton(Icons::Id icon, const QString &tooltip, bool checkable, int size)
{
    auto *b = new QToolButton;
    b->setIcon(Icons::icon(icon));
    b->setIconSize(QSize(size * 0.5, size * 0.5));
    b->setFixedSize(size, size);
    b->setToolTip(tooltip);
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRaise(true);
    return b;
}

QWidget *vSeparator()
{
    auto *w = new QWidget;
    w->setFixedSize(1, 28);
    w->setStyleSheet("background: rgba(255,255,255,0.10);");
    return w;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("role", "caption");
    return l;
}

// ---------------------------------------------------------------------------

KpiTile::KpiTile(const QString &cap, bool withMeter, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("kpi");
    setStyleSheet("#kpi { background: rgba(255,255,255,0.04); border-radius: 9px; }");
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 7, 10, 8);
    lay->setSpacing(2);
    lay->addWidget(caption(cap));
    m_value = new QLabel("—");
    m_value->setProperty("role", "value");
    lay->addWidget(m_value);
    if (withMeter) {
        m_meter = new QProgressBar;
        m_meter->setRange(0, 1000);
        m_meter->setTextVisible(false);
        lay->addSpacing(2);
        lay->addWidget(m_meter);
    }
}

void KpiTile::setValue(const QString &value)
{
    m_value->setText(value);
}

void KpiTile::setMeter(double ratio, const QColor &color)
{
    if (!m_meter)
        return;
    m_meter->setValue(int(std::clamp(ratio, 0.0, 1.0) * 1000));
    m_meter->setStyleSheet(QStringLiteral("QProgressBar::chunk { background: %1; border-radius: 3px; }")
                               .arg(color.name()));
}

// ---------------------------------------------------------------------------

StatChip::StatChip(Icons::Id icon, const QString &tooltip, QWidget *parent)
    : QWidget(parent)
{
    setToolTip(tooltip);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 2, 10, 2);
    lay->setSpacing(10);
    auto *ic = new QLabel;
    ic->setPixmap(Icons::pixmap(icon, 20, Theme::Accent.lighter(130)));
    ic->setFixedSize(34, 34);
    ic->setAlignment(Qt::AlignCenter);
    ic->setStyleSheet("background: rgba(76,141,255,0.14); border-radius: 10px;");
    lay->addWidget(ic);
    auto *col = new QVBoxLayout;
    col->setSpacing(0);
    m_value = new QLabel("—");
    m_value->setProperty("role", "value");
    m_sub = caption({});
    col->addWidget(m_value);
    col->addWidget(m_sub);
    lay->addLayout(col);
}

void StatChip::setValue(const QString &value)
{
    if (m_value->text() != value)
        m_value->setText(value);
}

void StatChip::setSub(const QString &sub, const QColor &color)
{
    const QString style = QStringLiteral("color: %1;").arg(color.name());
    if (m_sub->text() != sub)
        m_sub->setText(sub);
    if (m_sub->styleSheet() != style)
        m_sub->setStyleSheet(style);
}

// ---------------------------------------------------------------------------

LineBadge::LineBadge(int lineId, const QString &label, const QColor &color, bool alert, QWidget *parent)
    : QToolButton(parent)
    , m_lineId(lineId)
    , m_label(label)
    , m_color(color)
    , m_alert(alert)
{
    setCheckable(true);
    setFixedSize(40, 40);
    setCursor(Qt::PointingHandCursor);
}

void LineBadge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(5, 5, -5, -5);
    if (isChecked()) {
        p.setPen(QPen(Qt::white, 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(-3, -3, 3, 3));
    } else if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawEllipse(r.adjusted(-3, -3, 3, 3));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(m_color);
    p.drawEllipse(r);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * (m_label.size() > 1 ? 0.9 : 1.1));
    p.setFont(f);
    p.setPen(m_color.lightnessF() > 0.62 ? QColor("#111") : Qt::white);
    p.drawText(r, Qt::AlignCenter, m_label);
    if (m_alert) {
        p.setPen(QPen(QColor(23, 26, 33), 2));
        p.setBrush(Theme::Danger);
        p.drawEllipse(QPointF(r.right() - 1, r.top() + 1), 5, 5);
    }
}

// ---------------------------------------------------------------------------

Toast::Toast(QWidget *parent)
    : QLabel(parent)
{
    setObjectName("toast");
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_effect);
    m_fade = new QPropertyAnimation(m_effect, "opacity", this);
    m_fade->setDuration(350);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_fade->setStartValue(1.0);
        m_fade->setEndValue(0.0);
        m_fade->start();
    });
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (m_effect->opacity() < 0.01)
            hide();
    });
    hide();
}

void Toast::show(const QString &text, bool error, bool silent)
{
    if (!silent)
        Audio::instance().play(error ? Audio::Error : Audio::Notify);
    setText(text);
    setProperty("error", error);
    style()->unpolish(this);
    style()->polish(this);
    adjustSize();
    if (parentWidget())
        move((parentWidget()->width() - width()) / 2, m_bottom - height());
    m_fade->stop();
    m_effect->setOpacity(1.0);
    QLabel::show();
    raise();
    m_timer.start(error ? 6000 : 3500);
}

// ---------------------------------------------------------------------------

QSize StopDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    return QSize(QStyledItemDelegate::sizeHint(opt, index).width(), 32);
}

void StopDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = opt.rect;
    if (opt.state & QStyle::State_Selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(76, 141, 255, 60));
        p->drawRoundedRect(r.adjusted(2, 1, -2, -1), 7, 7);
    } else if (opt.state & QStyle::State_MouseOver) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 14));
        p->drawRoundedRect(r.adjusted(2, 1, -2, -1), 7, 7);
    }

    const bool first = index.data(FirstRole).toBool();
    const bool last = index.data(LastRole).toBool();
    const double x = r.left() + 20;
    const double cy = r.center().y();
    p->setPen(QPen(m_color, 5, Qt::SolidLine, Qt::FlatCap));
    const double top = (first && !m_loop) ? cy : r.top();
    const double bottom = (last && !m_loop) ? cy : r.bottom() + 1;
    p->drawLine(QPointF(x, top), QPointF(x, bottom));

    const bool terminus = (first || last) && !m_loop;
    p->setPen(QPen(terminus ? QColor("#171A21") : m_color, terminus ? 2.5 : 3));
    p->setBrush(terminus ? m_color : QColor("#F5F5F5"));
    p->drawEllipse(QPointF(x, cy), terminus ? 7 : 5, terminus ? 7 : 5);
    if (terminus) {
        p->setPen(Qt::NoPen);
        p->setBrush(Qt::white);
        p->drawEllipse(QPointF(x, cy), 2.5, 2.5);
    }

    const QString sub = index.data(SubtitleRole).toString();
    QFont f = opt.font;
    const QFontMetrics fm(f);
    const int subW = fm.horizontalAdvance(sub) + 12;
    QRectF textRect(x + 18, r.top(), r.width() - (x - r.left()) - 18 - subW, r.height());
    f.setBold(terminus);
    p->setFont(f);
    p->setPen(Theme::Text);
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(f).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, int(textRect.width())));
    p->setFont(opt.font);
    p->setPen(Theme::TextDim);
    p->drawText(QRectF(r.right() - subW, r.top(), subW - 8, r.height()), Qt::AlignVCenter | Qt::AlignRight, sub);
    p->restore();
}
