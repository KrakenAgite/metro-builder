#pragma once

#include "CityData.h"
#include <QColor>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>
#include <QVector>

// Une ligne de métro réelle : code, couleur et arrêts (dans l'ordre), en coordonnées de la ville
struct ImportedLine {
    QString ref;
    QString name;
    QColor color;
    QVector<QPair<QString, QPointF>> stops; // nom, position
};

// Récupère les lignes de métro (relations OSM route=subway) qui traversent la zone de jeu, via Overpass.
// La réponse est mise en cache ; seuls les arrêts situés dans la zone sont gardés.
class TransitImporter : public QObject
{
    Q_OBJECT
public:
    explicit TransitImporter(QObject *parent = nullptr);
    void fetch(const QSharedPointer<CityData> &city);
    bool busy() const { return m_busy; }

signals:
    void progress(const QString &message);
    void finished(const QVector<ImportedLine> &lines);
    void failed(const QString &message);

private:
    void request();
    void parse(const QByteArray &data);
    QString cachePath() const;

    QNetworkAccessManager m_nam;
    QStringList m_servers;
    QSharedPointer<CityData> m_city;
    QString m_query;
    int m_attempt = 0;
    bool m_busy = false;
};
