#pragma once

#include "Icons.h"
#include <QFrame>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>

class QProgressBar;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace Theme {
inline const QColor Accent("#4C8DFF");
inline const QColor Text("#E8EAED");
inline const QColor TextDim("#9AA0A6");
inline const QColor Success("#34C77B");
inline const QColor Warning("#F5A524");
inline const QColor Danger("#F04D5E");

void apply();                       // style Fusion + palette sombre + feuille de style globale
QColor loadColor(double ratio);     // vert / orange / rouge selon charge/capacité
} // namespace Theme

// Panneau flottant arrondi et translucide, avec ombre douce peinte à la main.
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(QWidget *parent = nullptr, int radius = 16);
    static constexpr int Shadow = 10; // marge réservée à l'ombre

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_radius;
};

// Bouton icône carré (barre d'outils, actions de panneau)
QToolButton *iconButton(Icons::Id icon, const QString &tooltip, bool checkable = false, int size = 40);

// Fine séparation verticale pour la barre du bas
QWidget *vSeparator();

// Petite étiquette secondaire
QLabel *caption(const QString &text, QWidget *parent = nullptr);

// Tuile indicateur : légende + valeur (+ jauge optionnelle)
class KpiTile : public QWidget
{
    Q_OBJECT
public:
    KpiTile(const QString &caption, bool withMeter = false, QWidget *parent = nullptr);
    void setValue(const QString &value);
    void setMeter(double ratio, const QColor &color);

private:
    QLabel *m_value;
    QProgressBar *m_meter = nullptr;
};

// Indicateur du bandeau supérieur : icône + valeur + sous-titre
class StatChip : public QWidget
{
    Q_OBJECT
public:
    StatChip(Icons::Id icon, const QString &tooltip, QWidget *parent = nullptr);
    void setValue(const QString &value);
    void setSub(const QString &sub, const QColor &color = Theme::TextDim);

private:
    QLabel *m_value;
    QLabel *m_sub;
};

// Pastille ronde d'une ligne (numéro sur fond de couleur)
class LineBadge : public QToolButton
{
    Q_OBJECT
public:
    LineBadge(int lineId, const QString &label, const QColor &color, bool alert, QWidget *parent = nullptr);
    int lineId() const { return m_lineId; }
    void setLine(const QString &label, const QColor &color)
    {
        m_label = label;
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_lineId;
    QString m_label;
    QColor m_color;
    bool m_alert;
};

// Message éphémère au-dessus de la barre du bas
class Toast : public QLabel
{
    Q_OBJECT
public:
    explicit Toast(QWidget *parent);
    // un son d'annonce accompagne le message (bip d'erreur, ou carillon d'information) sauf si silent
    void show(const QString &text, bool error = false, bool silent = false);
    void setBottom(int y) { m_bottom = y; }

private:
    int m_bottom = 60;
    QTimer m_timer;
    QGraphicsOpacityEffect *m_effect;
    QPropertyAnimation *m_fade;
};

// Rendu de la liste des arrêts comme un plan de ligne (trait coloré + pastilles)
class StopDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Roles { StationIdRole = Qt::UserRole, SubtitleRole, FirstRole, LastRole };
    using QStyledItemDelegate::QStyledItemDelegate;
    void setLine(const QColor &color, bool loop)
    {
        m_color = color;
        m_loop = loop;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;

private:
    QColor m_color = Theme::Accent;
    bool m_loop = false;
};
