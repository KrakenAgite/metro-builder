#pragma once

#include "CityData.h"
#include "VectorTiles.h"
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>

class QNetworkReply;

// Récupère une ville : géocodage Nominatim puis rues/bâtiments depuis les tuiles vectorielles
// OpenFreeMap (rapide, servi par un CDN). En secours, interroge Overpass par zones en parallèle.
// Géocodage et tuiles sont mis en cache sur disque : une ville déjà vue se recharge sans réseau.
class OsmLoader : public QObject
{
    Q_OBJECT
public:
    explicit OsmLoader(QObject *parent = nullptr);

    void loadCity(const QString &city, double radiusKm, bool useCache = true);
    void loadFile(const QString &path);
    // Agrandit la zone de jeu d'une ville déjà chargée (tuiles vectorielles, cache réutilisé)
    void extendCity(const QSharedPointer<CityData> &city, const QRectF &area);
    bool busy() const { return m_busy; }

signals:
    void progress(const QString &message);
    void loaded(QSharedPointer<CityData> city);
    void extended(QSharedPointer<CityData> city);
    void failed(const QString &message);

private:
    struct Tile {
        QString bbox;
        int attempts = 0;
        bool done = false;
    };

    void geocode();
    // tuiles vectorielles (source principale)
    void startVectorTiles();
    void fetchTileJson();
    void pumpVector();
    void requestVectorTile(int index);
    void decodeVectorTile(int index, const QByteArray &data, bool writeCache);
    void finishVectorTiles();
    void vectorFailed(const QString &why);
    QString tilePath(const VectorTiles::TileId &id) const;
    QString geoPath() const;
    // Overpass (secours)
    void startTiles();
    void pump();
    void requestTile(int index);
    void retryTile(int index, const QString &why);
    void mergeTile(int index, const QByteArray &data);
    void finishTiles();
    void reportProgress();
    void parseAsync(const QByteArray &data, const QString &name, double lat, double lon, double radiusM);
    void fail(const QString &message);
    QString cacheBase() const;

    QNetworkAccessManager m_nam;
    QStringList m_servers;
    QString m_city;
    QString m_displayName;
    double m_radiusKm = 2;
    double m_lat = 0, m_lon = 0;
    bool m_busy = false;
    bool m_extending = false;
    QRectF m_area; // zone de jeu demandée, dans le repère de la ville
    int m_generation = 0; // invalide les réponses d'un chargement précédent

    QVector<Tile> m_tiles;
    QList<int> m_queue;
    int m_active = 0;
    int m_parsing = 0;
    int m_done = 0;
    qint64 m_bytesDone = 0;
    QHash<QNetworkReply *, qint64> m_inFlight;
    QJsonArray m_elements;
    QSet<quint64> m_seen;

    QString m_tileUrl;
    QVector<VectorTiles::TileId> m_vtiles;
    QVector<VectorTiles::TileData> m_vdata;
    QVector<int> m_vattempts;
    QList<int> m_vqueue;
    int m_vactive = 0;
    int m_vdone = 0;
};
