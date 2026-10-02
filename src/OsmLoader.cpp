#include "OsmLoader.h"
#include "OsmParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
#include <utility>

namespace {

const char *kUserAgent = "MetroBuilder/1.0 (Qt metro building game)";
const double kTileSize = 2500;   // m : côté d'une tuile
const int kMaxParallel = 2;      // overpass-api.de n'accepte que 2 requêtes simultanées par IP
const int kMaxAttempts = 6;
const int kRequestTimeout = 90000;

// Requête allégée : seules les données utiles au jeu (pas de voies de service, occupation du sol filtrée).
QString tileQuery(const QString &bbox)
{
    return QStringLiteral(
               "[out:json][timeout:90];\n"
               "(\n"
               "  way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|"
               "living_street|pedestrian|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link)$\"](%1);\n"
               "  way[\"building\"](%1);\n"
               "  way[\"landuse\"~\"^(residential|commercial|retail|industrial|grass|forest|meadow|"
               "recreation_ground|cemetery|basin|reservoir)$\"](%1);\n"
               "  way[\"leisure\"~\"^(park|garden|pitch)$\"](%1);\n"
               "  way[\"natural\"~\"^(water|wood)$\"](%1);\n"
               "  way[\"waterway\"~\"^(river|canal|riverbank)$\"](%1);\n"
               "  relation[\"natural\"=\"water\"](%1);\n"
               "  relation[\"leisure\"=\"park\"](%1);\n"
               "  relation[\"building\"](%1);\n"
               ");\n"
               "out geom qt;\n")
        .arg(bbox);
}

// Décode la réponse d'une tuile ; une remarque « runtime error » signale une requête interrompue.
struct TileResult {
    QJsonArray elements;
    QString error;
};

TileResult decodeTile(const QByteArray &data)
{
    TileResult r;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (doc.isNull()) {
        r.error = QObject::tr("réponse invalide");
        return r;
    }
    const QString remark = doc.object().value("remark").toString();
    if (remark.contains("error", Qt::CaseInsensitive)) {
        r.error = remark.simplified().left(120);
        return r;
    }
    r.elements = doc.object().value("elements").toArray();
    return r;
}

} // namespace

OsmLoader::OsmLoader(QObject *parent)
    : QObject(parent)
    , m_servers{"https://overpass-api.de/api/interpreter",
                "https://overpass.private.coffee/api/interpreter",
                "https://overpass.kumi.systems/api/interpreter",
                "https://maps.mail.ru/osm/tools/overpass/api/interpreter"}
{
}

QString OsmLoader::cacheBase() const
{
    QString slug = m_city.toLower().simplified();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "_");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/osm";
    QDir().mkpath(dir);
    return QStringLiteral("%1/%2_%3m").arg(dir, slug).arg(int(m_radiusKm * 1000));
}

void OsmLoader::fail(const QString &message)
{
    ++m_generation;
    for (QNetworkReply *r : m_inFlight.keys())
        r->abort();
    m_inFlight.clear();
    m_queue.clear();
    m_elements = QJsonArray();
    m_seen.clear();
    m_busy = false;
    m_extending = false;
    emit failed(message);
}

void OsmLoader::loadCity(const QString &city, double radiusKm, bool useCache)
{
    if (m_busy)
        return;
    m_busy = true;
    m_extending = false;
    m_city = city.trimmed();
    m_radiusKm = radiusKm;
    m_area = QRectF(-radiusKm * 1000, -radiusKm * 1000, radiusKm * 2000, radiusKm * 2000);

    const QString base = cacheBase();
    if (useCache && QFile::exists(base + ".json") && QFile::exists(base + ".meta")) {
        QFile meta(base + ".meta"), data(base + ".json");
        if (meta.open(QIODevice::ReadOnly) && data.open(QIODevice::ReadOnly)) {
            const QJsonObject m = QJsonDocument::fromJson(meta.readAll()).object();
            emit progress(tr("Chargement depuis le cache…"));
            parseAsync(data.readAll(), m.value("name").toString(m_city), m.value("lat").toDouble(),
                       m.value("lon").toDouble(), m_radiusKm * 1000);
            return;
        }
    }
    // géocodage déjà connu : on passe directement aux tuiles (éventuellement toutes en cache)
    QFile geo(geoPath());
    if (useCache && geo.open(QIODevice::ReadOnly)) {
        const QJsonObject g = QJsonDocument::fromJson(geo.readAll()).object();
        m_lat = g.value("lat").toDouble();
        m_lon = g.value("lon").toDouble();
        m_displayName = g.value("name").toString(m_city);
        startVectorTiles();
        return;
    }
    geocode();
}

void OsmLoader::extendCity(const QSharedPointer<CityData> &city, const QRectF &area)
{
    if (m_busy || !city)
        return;
    m_busy = true;
    m_extending = true;
    m_lat = city->lat0;
    m_lon = city->lon0;
    m_displayName = city->name;
    m_area = area;
    startVectorTiles();
}

void OsmLoader::loadFile(const QString &path)
{
    if (m_busy)
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(tr("Impossible d'ouvrir %1").arg(path));
        return;
    }
    m_busy = true;
    emit progress(tr("Lecture de %1…").arg(QFileInfo(path).fileName()));
    parseAsync(f.readAll(), QFileInfo(path).baseName(), NAN, NAN, 0);
}

void OsmLoader::geocode()
{
    emit progress(tr("Recherche de « %1 » (Nominatim)…").arg(m_city));
    QUrl url("https://nominatim.openstreetmap.org/search");
    QUrlQuery q;
    q.addQueryItem("q", m_city);
    q.addQueryItem("format", "json");
    q.addQueryItem("limit", "1");
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return fail(tr("Géocodage impossible : %1").arg(reply->errorString()));
        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        if (arr.isEmpty())
            return fail(tr("Ville « %1 » introuvable").arg(m_city));
        const QJsonObject o = arr.first().toObject();
        m_lat = o.value("lat").toString().toDouble();
        m_lon = o.value("lon").toString().toDouble();
        m_displayName = o.value("name").toString(m_city);
        QFile geo(geoPath());
        if (geo.open(QIODevice::WriteOnly))
            geo.write(QJsonDocument(QJsonObject{{"name", m_displayName}, {"lat", m_lat}, {"lon", m_lon}}).toJson());
        startVectorTiles();
    });
}

// ---------------------------------------------------------------------------
// Tuiles vectorielles OpenFreeMap
// ---------------------------------------------------------------------------

QString OsmLoader::geoPath() const
{
    QString slug = m_city.toLower().simplified();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "_");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/geo";
    QDir().mkpath(dir);
    return QStringLiteral("%1/%2.json").arg(dir, slug);
}

QString OsmLoader::tilePath(const VectorTiles::TileId &id) const
{
    return QStringLiteral("%1/tiles/%2/%3/%4.pbf")
        .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .arg(id.z)
        .arg(id.x)
        .arg(id.y);
}

void OsmLoader::startVectorTiles()
{
    ++m_generation;
    m_vtiles = VectorTiles::tilesForRect(m_lat, m_lon, m_area);
    m_vdata = QVector<VectorTiles::TileData>(m_vtiles.size());
    m_vattempts = QVector<int>(m_vtiles.size(), 0);
    m_vqueue.clear();
    m_vactive = m_vdone = 0;
    emit progress(tr("Téléchargement des tuiles : 0 / %1").arg(m_vtiles.size()));

    for (int i = 0; i < m_vtiles.size(); ++i) {
        QFile f(tilePath(m_vtiles[i]));
        if (f.open(QIODevice::ReadOnly))
            decodeVectorTile(i, f.readAll(), false);
        else
            m_vqueue << i;
    }
    if (m_vqueue.isEmpty())
        return;
    if (m_tileUrl.isEmpty())
        fetchTileJson();
    else
        pumpVector();
}

void OsmLoader::fetchTileJson()
{
    QNetworkRequest req(QUrl("https://tiles.openfreemap.org/planet"));
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam.get(req);
    const int gen = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        const QJsonArray tiles = QJsonDocument::fromJson(reply->readAll()).object().value("tiles").toArray();
        if (reply->error() != QNetworkReply::NoError || tiles.isEmpty())
            return vectorFailed(reply->errorString());
        m_tileUrl = tiles.first().toString();
        pumpVector();
    });
}

void OsmLoader::pumpVector()
{
    while (m_busy && m_vactive < 8 && !m_vqueue.isEmpty())
        requestVectorTile(m_vqueue.takeFirst());
}

void OsmLoader::requestVectorTile(int index)
{
    const VectorTiles::TileId id = m_vtiles[index];
    QString url = m_tileUrl;
    url.replace("{z}", QString::number(id.z)).replace("{x}", QString::number(id.x)).replace("{y}", QString::number(id.y));
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam.get(req);
    ++m_vactive;
    ++m_vattempts[index];
    const int gen = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, index, gen] {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        --m_vactive;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 204 || status == 404) {
            decodeVectorTile(index, {}, false); // tuile vide (mer, hors carte)
        } else if (reply->error() != QNetworkReply::NoError || data.isEmpty()) {
            if (m_vattempts[index] >= 3)
                return vectorFailed(reply->errorString());
            const int g = m_generation;
            QTimer::singleShot(500 * m_vattempts[index], this, [this, index, g] {
                if (g == m_generation) {
                    m_vqueue << index;
                    pumpVector();
                }
            });
        } else {
            decodeVectorTile(index, data, true);
        }
        pumpVector();
    });
}

void OsmLoader::decodeVectorTile(int index, const QByteArray &data, bool writeCache)
{
    const VectorTiles::TileId id = m_vtiles[index];
    const QString path = tilePath(id);
    const double lat = m_lat, lon = m_lon;
    const int gen = m_generation;
    auto *watcher = new QFutureWatcher<VectorTiles::TileData>(this);
    connect(watcher, &QFutureWatcher<VectorTiles::TileData>::finished, this, [this, watcher, index, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return;
        m_vdata[index] = watcher->result();
        ++m_vdone;
        emit progress(tr("Téléchargement des tuiles : %1 / %2").arg(m_vdone).arg(m_vtiles.size()));
        if (m_vdone == m_vtiles.size())
            finishVectorTiles();
    });
    watcher->setFuture(QtConcurrent::run([data, id, path, lat, lon, writeCache] {
        if (writeCache) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
        }
        return VectorTiles::decode(data, id, lat, lon);
    }));
}

void OsmLoader::finishVectorTiles()
{
    emit progress(tr("Construction de la ville…"));
    const QVector<VectorTiles::TileData> tiles = m_vdata;
    m_vdata.clear();
    const QString name = m_displayName;
    const double lat = m_lat, lon = m_lon;
    const QRectF area = m_area;
    const int gen = m_generation;
    auto *watcher = new QFutureWatcher<QSharedPointer<CityData>>(this);
    connect(watcher, &QFutureWatcher<QSharedPointer<CityData>>::finished, this, [this, watcher, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return;
        const QSharedPointer<CityData> city = watcher->result();
        if (!city || (city->buildings.isEmpty() && city->roads.isEmpty()))
            return vectorFailed(tr("tuiles vides"));
        m_busy = false;
        if (std::exchange(m_extending, false))
            emit extended(city);
        else
            emit loaded(city);
    });
    watcher->setFuture(QtConcurrent::run(
        [tiles, name, lat, lon, area] { return VectorTiles::assemble(tiles, name, lat, lon, area); }));
}

void OsmLoader::vectorFailed(const QString &why)
{
    if (m_extending)
        return fail(tr("Agrandissement impossible : tuiles indisponibles (%1)").arg(why));
    emit progress(tr("Tuiles indisponibles (%1), passage par Overpass…").arg(why));
    startTiles(); // incrémente la génération : les tuiles en cours sont ignorées
}

// ---------------------------------------------------------------------------
// Téléchargement par tuiles
// ---------------------------------------------------------------------------

void OsmLoader::startTiles()
{
    ++m_generation;
    m_tiles.clear();
    m_queue.clear();
    m_inFlight.clear();
    m_elements = QJsonArray();
    m_seen.clear();
    m_active = m_parsing = m_done = 0;
    m_bytesDone = 0;

    const double r = m_radiusKm * 1000;
    const int n = std::max(1, int(std::ceil(2 * r / kTileSize)));
    const double step = 2 * r / n;
    const double kLat = 110540.0;
    const double kLon = 111320.0 * std::cos(m_lat * M_PI / 180.0);
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            const double s = m_lat + (-r + iy * step) / kLat;
            const double nn = m_lat + (-r + (iy + 1) * step) / kLat;
            const double w = m_lon + (-r + ix * step) / kLon;
            const double e = m_lon + (-r + (ix + 1) * step) / kLon;
            Tile t;
            t.bbox = QStringLiteral("%1,%2,%3,%4").arg(s, 0, 'f', 6).arg(w, 0, 'f', 6).arg(nn, 0, 'f', 6).arg(e, 0, 'f', 6);
            m_queue << m_tiles.size();
            m_tiles << t;
        }
    }
    // les tuiles centrales d'abord : ce sont les plus denses
    std::sort(m_queue.begin(), m_queue.end(), [n](int a, int b) {
        auto d = [n](int i) { return std::abs(i % n - (n - 1) / 2.0) + std::abs(i / n - (n - 1) / 2.0); };
        return d(a) > d(b);
    });
    reportProgress();
    pump();
}

void OsmLoader::pump()
{
    if (!m_busy)
        return;
    while (m_active < kMaxParallel && !m_queue.isEmpty())
        requestTile(m_queue.takeLast());
}

void OsmLoader::requestTile(int index)
{
    Tile &t = m_tiles[index];
    // 1er et 2e essai sur le serveur principal, puis on alterne avec les miroirs
    const QString server = t.attempts < 2 ? m_servers.first() : m_servers[t.attempts % m_servers.size()];
    ++t.attempts;
    ++m_active;

    QNetworkRequest req{QUrl(server)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setTransferTimeout(kRequestTimeout);
    QNetworkReply *reply = m_nam.post(req, "data=" + QUrl::toPercentEncoding(tileQuery(t.bbox)));
    m_inFlight.insert(reply, 0);
    const int gen = m_generation;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 got, qint64) {
        if (m_inFlight.contains(reply)) {
            m_inFlight[reply] = got;
            reportProgress();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, index, gen, server] {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        m_inFlight.remove(reply);
        --m_active;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || !data.trimmed().startsWith('{')) {
            const QString why = status ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
            retryTile(index, QStringLiteral("%1 : %2").arg(QUrl(server).host(), why));
        } else {
            m_bytesDone += data.size();
            mergeTile(index, data);
        }
        pump();
    });
}

void OsmLoader::retryTile(int index, const QString &why)
{
    Tile &t = m_tiles[index];
    if (t.attempts >= kMaxAttempts)
        return fail(tr("Téléchargement impossible (%1). Les serveurs Overpass sont surchargés, "
                       "réessayez dans quelques minutes ou réduisez le rayon.")
                        .arg(why));
    emit progress(tr("Zone %1 : %2, nouvel essai…").arg(index + 1).arg(why));
    // attente croissante avant de réessayer (laisse le serveur libérer un créneau)
    const int gen = m_generation;
    QTimer::singleShot(1500 * t.attempts, this, [this, index, gen] {
        if (gen != m_generation)
            return;
        m_queue.prepend(index); // pump() prend en fin de file : la reprise passe après les tuiles neuves
        pump();
    });
}

void OsmLoader::mergeTile(int index, const QByteArray &data)
{
    ++m_parsing;
    const int gen = m_generation;
    auto *watcher = new QFutureWatcher<TileResult>(this);
    connect(watcher, &QFutureWatcher<TileResult>::finished, this, [this, watcher, index, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return;
        --m_parsing;
        const TileResult r = watcher->result();
        if (!r.error.isEmpty()) {
            retryTile(index, r.error);
            return;
        }
        // une rue ou un bâtiment à cheval sur deux tuiles n'est gardé qu'une fois
        for (const QJsonValue &v : r.elements) {
            const QJsonObject o = v.toObject();
            const quint64 key = (quint64(o.value("type").toString() == "relation") << 63)
                                | quint64(o.value("id").toInteger());
            if (!m_seen.contains(key)) {
                m_seen.insert(key);
                m_elements.append(v);
            }
        }
        m_tiles[index].done = true;
        ++m_done;
        reportProgress();
        if (m_done == m_tiles.size())
            finishTiles();
    });
    watcher->setFuture(QtConcurrent::run([data] { return decodeTile(data); }));
}

void OsmLoader::reportProgress()
{
    qint64 bytes = m_bytesDone;
    for (qint64 b : m_inFlight)
        bytes += b;
    emit progress(tr("Téléchargement : %1 / %2 zones · %3 Mo reçus")
                      .arg(m_done)
                      .arg(m_tiles.size())
                      .arg(bytes / 1048576.0, 0, 'f', 1));
}

void OsmLoader::finishTiles()
{
    emit progress(tr("Analyse des données OSM…"));
    const QJsonArray elements = m_elements;
    m_elements = QJsonArray();
    m_seen.clear();
    const QString name = m_displayName, base = cacheBase();
    const double lat = m_lat, lon = m_lon, radius = m_radiusKm * 1000;
    const int gen = m_generation;

    auto *watcher = new QFutureWatcher<ParseResult>(this);
    connect(watcher, &QFutureWatcher<ParseResult>::finished, this, [this, watcher, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return;
        const ParseResult res = watcher->result();
        if (!res.city)
            return fail(res.error);
        m_busy = false;
        emit loaded(res.city);
    });
    watcher->setFuture(QtConcurrent::run([elements, name, base, lat, lon, radius] {
        ParseResult res = parseOverpassElements(elements, name, lat, lon, radius);
        if (res.city) {
            // mise en cache du résultat fusionné (format Overpass, relu par parseOverpassJson)
            QFile f(base + ".json");
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(QJsonObject{{"elements", elements}}).toJson(QJsonDocument::Compact));
            QFile m(base + ".meta");
            if (m.open(QIODevice::WriteOnly))
                m.write(QJsonDocument(QJsonObject{{"name", name}, {"lat", lat}, {"lon", lon}}).toJson());
        }
        return res;
    }));
}

void OsmLoader::parseAsync(const QByteArray &data, const QString &name, double lat, double lon, double radiusM)
{
    emit progress(tr("Analyse des données OSM…"));
    const int gen = ++m_generation;
    auto *watcher = new QFutureWatcher<ParseResult>(this);
    connect(watcher, &QFutureWatcher<ParseResult>::finished, this, [this, watcher, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return;
        const ParseResult res = watcher->result();
        if (!res.city)
            return fail(res.error);
        m_busy = false;
        emit loaded(res.city);
    });
    watcher->setFuture(QtConcurrent::run([data, name, lat, lon, radiusM] {
        return parseOverpassJson(data, name, lat, lon, radiusM);
    }));
}
