#pragma once

#include <QColor>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

// Graphique minimaliste dessiné au QPainter (thème sombre des panneaux) :
// courbe / aire / barres groupées, grille discrète, légende si plusieurs séries,
// réticule et infobulle au survol.
class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    enum Kind { LineChart, AreaChart, BarChart };
    struct Series {
        QString name;
        QColor color;
        QVector<double> values;
    };

    explicit ChartWidget(const QString &title, QWidget *parent = nullptr);

    // labels : abscisses (une par point) ; tooltips : libellé long de chaque point
    // partialLast : le dernier point est le mois en cours (dessiné atténué)
    void setData(Kind kind, const QStringList &labels, const QStringList &tooltips, const QVector<Series> &series,
                 std::function<QString(double)> format, bool partialLast = true);
    void setSubtitle(const QString &subtitle);

    QSize sizeHint() const override { return QSize(420, 230); }
    QSize minimumSizeHint() const override { return QSize(260, 190); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plotRect() const;
    int indexAt(double x) const;
    double xOf(int i) const;

    QString m_title, m_subtitle;
    Kind m_kind = LineChart;
    QStringList m_labels, m_tooltips;
    QVector<Series> m_series;
    std::function<QString(double)> m_format;
    bool m_partialLast = true;
    double m_min = 0, m_max = 1;
    QVector<double> m_ticks;
    int m_hover = -1;
};

// Rentabilité par ligne : une rangée par ligne, barre divergente autour de zéro
class LineEconomicsWidget : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        QString code, name;
        QColor color;
        double riders = 0;   // voyageurs/h
        double revenue = 0;  // M€/mois
        double cost = 0;     // M€/mois
    };
    explicit LineEconomicsWidget(QWidget *parent = nullptr);
    void setRows(const QVector<Row> &rows);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<Row> m_rows;
};
