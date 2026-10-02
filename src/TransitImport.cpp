#include "TransitImport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <cmath>

namespace {

const char *kUserAgent = "MetroBuilder/1.0 (Qt metro building game)";

bool isStopRole(const QString &role)
{
    return role == QLatin1String("stop") || role.startsWith(QLatin1String("stop_"));
}

} // namespace

TransitImporter::TransitImporter(QObject *parent)
    : QObject(parent)
    , m_servers{"https://overpass-api.de/api/interpreter", "https://overpass.private.coffee/api/interpreter",
                "https://overpass.kumi.systems/api/interpreter"}
{
}

QString TransitImporter::cachePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/transit";
    QDir().mkpath(dir);
    const QByteArray key = QCryptographicHash::hash(m_query.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return dir + "/" + key + ".json";
}

void TransitImporter::fetch(const QSharedPointer<CityData> &city)
{
    if (m_busy || !city)
        return;
    m_city = city;
    // zone de jeu en latitude / longitude
    const QRectF a = city->area.isValid() ? city->area : city->bounds;
    const double kx = 111320.0 * std::cos(city->lat0 * M_PI / 180.0), ky = 110540.0;
    const double south = city->lat0 - a.bottom() / ky, north = city->lat0 - a.top() / ky;
    const double west = city->lon0 + a.left() / kx, east = city->lon0 + a.right() / kx;
    m_query = QStringLiteral("[out:json][timeout:60];relation[\"route\"=\"subway\"](%1,%2,%3,%4);out body;node(r);out;")
                  .arg(south, 0, 'f', 5)
                  .arg(west, 0, 'f', 5)
                  .arg(north, 0, 'f', 5)
                  .arg(east, 0, 'f', 5);
    m_busy = true;
    m_attempt = 0;
    QFile cached(cachePath());
    if (cached.open(QIODevice::ReadOnly)) {
        const QByteArray data = cached.readAll();
        QTimer::singleShot(0, this, [this, data] { parse(data); });
        return;
    }
    request();
}

void TransitImporter::request()
{
    emit progress(tr("Recherche des lignes de métro existantes…"));
    QNetworkRequest req{QUrl(m_servers[m_attempt % m_servers.size()])};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setTransferTimeout(70000);
    QNetworkReply *reply = m_nam.post(req, "data=" + QUrl::toPercentEncoding(m_query));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || status != 200 || !data.trimmed().startsWith('{')) {
            if (++m_attempt < 4) {
                QTimer::singleShot(1500, this, &TransitImporter::request);
                return;
            }
            m_busy = false;
            emit failed(tr("Impossible de récupérer le métro réel (serveurs Overpass indisponibles)"));
            return;
        }
        QFile f(cachePath());
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        parse(data);
    });
}

void TransitImporter::parse(const QByteArray &data)
{
    m_busy = false;
    const QJsonArray elements = QJsonDocument::fromJson(data).object().value("elements").toArray();
    const QRectF area = m_city->area.isValid() ? m_city->area : m_city->bounds;
    QHash<qint64, QPair<QString, QPointF>> nodes;
    for (const QJsonValue &v : elements) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != QLatin1String("node"))
            continue;
        const QJsonObject tags = o.value("tags").toObject();
        nodes.insert(qint64(o.value("id").toDouble()),
                     {tags.value("name").toString(), m_city->project(o.value("lat").toDouble(), o.value("lon").toDouble())});
    }
    // une relation par sens de circulation : on garde, pour chaque ligne, celle qui a le plus d'arrêts dans la zone
    QHash<QString, ImportedLine> best;
    for (const QJsonValue &v : elements) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != QLatin1String("relation"))
            continue;
        const QJsonObject tags = o.value("tags").toObject();
        ImportedLine line;
        line.ref = tags.value("ref").toString().trimmed();
        if (line.ref.isEmpty())
            line.ref = tags.value("name").toString().section(' ', -1);
        line.name = tags.value("name").toString();
        line.color = QColor(tags.value("colour").toString());
        for (const QJsonValue &m : o.value("members").toArray()) {
            const QJsonObject mo = m.toObject();
            if (mo.value("type").toString() != QLatin1String("node") || !isStopRole(mo.value("role").toString()))
                continue;
            const auto it = nodes.constFind(qint64(mo.value("ref").toDouble()));
            if (it == nodes.constEnd() || !area.contains(it->second))
                continue;
            line.stops << *it;
        }
        if (line.stops.size() < 2 || line.ref.isEmpty())
            continue;
        if (!best.contains(line.ref) || best[line.ref].stops.size() < line.stops.size())
            best[line.ref] = line;
    }
    QVector<ImportedLine> lines = best.values().toVector();
    std::sort(lines.begin(), lines.end(), [](const ImportedLine &a, const ImportedLine &b) {
        bool okA, okB;
        const int na = a.ref.toInt(&okA), nb = b.ref.toInt(&okB);
        if (okA && okB)
            return na < nb;
        if (okA != okB)
            return okA;
        return a.ref < b.ref;
    });
    if (lines.isEmpty()) {
        emit failed(tr("Aucune ligne de métro trouvée dans OpenStreetMap pour cette zone"));
        return;
    }
    emit finished(lines);
}
