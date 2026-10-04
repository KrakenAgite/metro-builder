// Scénarios sur de grandes capitales, et import du métro réel depuis OpenStreetMap.
#include "Metro.h"

#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

namespace {

MissionGoal metric(GoalKind kind, double target)
{
    MissionGoal g;
    g.type = MissionGoal::Metric;
    g.kind = kind;
    g.target = target;
    return g;
}

MissionGoal ridersShare(double share)
{
    MissionGoal g;
    g.type = MissionGoal::RidersShare;
    g.kind = GoalKind::Riders;
    g.target = share;
    return g;
}

MissionGoal noSaturation()
{
    MissionGoal g;
    g.type = MissionGoal::NoSaturation;
    return g;
}

QString num(double v)
{
    return QLocale().toString(qRound(v));
}

// presets les plus proches d'une couleur donnée (pour les lignes importées)
QColor nearestPreset(const QColor &c, const QVector<QColor> &used)
{
    const QVector<QColor> &palette = Metro::presetColors();
    QColor best = palette.first();
    double bestD = 1e18;
    for (const QColor &p : palette) {
        const double d = std::pow(p.red() - c.red(), 2) + std::pow(p.green() - c.green(), 2)
                         + std::pow(p.blue() - c.blue(), 2) + (used.contains(p) ? 40000 : 0);
        if (d < bestD) {
            bestD = d;
            best = p;
        }
    }
    return best;
}

} // namespace

const QVector<ScenarioDef> &Metro::scenarios()
{
    static const QVector<ScenarioDef> list = [] {
        QVector<ScenarioDef> v;
        ScenarioDef d;
        d = {"paris", "Paris", QObject::tr("Paris"), QObject::tr("Paris, toujours plus loin"),
             QObject::tr("Vous héritez du vrai métro parisien. Prolongez-le pour qu'il transporte bien plus de monde "
                         "et n'oublie aucun habitant, sans jamais perdre d'argent."),
             2, 300, true, 48, 2,
             {ridersShare(0.18), metric(GoalKind::Coverage, 95), metric(GoalKind::ProfitStreak, 12)}};
        v << d;
        d = {"london", "London", QObject::tr("Londres"), QObject::tr("Londres : désengorger la City"),
             QObject::tr("Partez de zéro au cœur de Londres : desservez les quartiers d'affaires et "
                         "multipliez les correspondances."),
             2.5, 900, false, 60, 2,
             {metric(GoalKind::Coverage, 50), metric(GoalKind::Transfers, 4), ridersShare(0.06)}};
        v << d;
        d = {"berlin", "Berlin", QObject::tr("Berlin"), QObject::tr("Berlin, d'est en ouest"),
             QObject::tr("Une ville étendue et un budget confortable : tissez un premier réseau complet "
                         "qui relie les deux rives de la Spree."),
             3, 700, false, 48, 1,
             {metric(GoalKind::Stations, 20), metric(GoalKind::Lines, 3), metric(GoalKind::Capture, 30)}};
        v << d;
        d = {"madrid", "Madrid", QObject::tr("Madrid"), QObject::tr("Madrid, métro rentable"),
             QObject::tr("Budget serré : la ville veut un métro qui gagne de l'argent chaque mois "
                         "pendant une année entière."),
             2, 400, false, 36, 2,
             {metric(GoalKind::ProfitStreak, 12), metric(GoalKind::Coverage, 40)}};
        v << d;
        d = {"newyork", "Midtown Manhattan, New York", QObject::tr("New York"), QObject::tr("Manhattan sous pression"),
             QObject::tr("Des tours de bureaux à perte de vue : transportez des foules immenses "
                         "sans qu'aucune ligne ne déborde."),
             2, 1000, false, 60, 3,
             {ridersShare(0.10), noSaturation(), metric(GoalKind::Capture, 40)}};
        v << d;
        d = {"tokyo", "Shinjuku, Tokyo", QObject::tr("Tokyo"), QObject::tr("Tokyo, l'heure de pointe"),
             QObject::tr("Shinjuku, la gare la plus fréquentée du monde : un réseau très maillé, "
                         "qui dessert presque tout le monde."),
             2.5, 1200, false, 72, 3,
             {ridersShare(0.12), metric(GoalKind::Coverage, 70), metric(GoalKind::Transfers, 6)}};
        v << d;
        return v;
    }();
    return list;
}

const ScenarioDef *Metro::scenario(const QString &id)
{
    for (const ScenarioDef &d : scenarios())
        if (d.id == id)
            return &d;
    return nullptr;
}

void Metro::startScenario(const ScenarioDef &def)
{
    setSandbox(false);
    m_money = def.money;
    m_mission = Mission();
    m_survival = Survival(); // la mission remplace le but de survie
    m_mission.id = def.id;
    m_mission.title = def.title;
    m_mission.cityLabel = def.cityLabel;
    m_mission.startMonth = month();
    m_mission.deadline = month() + def.months - 1;
    for (MissionGoal g : def.goals) {
        switch (g.type) {
        case MissionGoal::RidersShare:
            // objectif proportionnel à la demande de la ville, arrondi à la centaine
            g.target = std::max(500.0, std::round(g.target * m_totalPotential / 100) * 100);
            g.title = tr("Transporter %1 voyageurs/h").arg(num(g.target));
            break;
        case MissionGoal::NoSaturation:
            g.title = tr("Aucune ligne saturée");
            break;
        case MissionGoal::Metric:
            g.title = makeGoal(g.kind, 0).title; // libellé par défaut
            switch (g.kind) {
            case GoalKind::Stations: g.title = tr("Construire %1 stations").arg(num(g.target)); break;
            case GoalKind::Lines: g.title = tr("Exploiter %1 lignes").arg(num(g.target)); break;
            case GoalKind::Capture: g.title = tr("Capter %1 % de la demande").arg(num(g.target)); break;
            case GoalKind::Coverage: g.title = tr("Desservir %1 % des habitants").arg(num(g.target)); break;
            case GoalKind::Transfers: g.title = tr("Avoir %1 correspondances").arg(num(g.target)); break;
            case GoalKind::ProfitStreak: g.title = tr("%1 mois bénéficiaires d'affilée").arg(num(g.target)); break;
            case GoalKind::Riders: g.title = tr("Transporter %1 voyageurs/h").arg(num(g.target)); break;
            default: break;
            }
            break;
        }
        m_mission.goals << g;
    }
    m_goals.clear(); // les objectifs libres laissent la place à la mission
    checkMission();
    emit missionChanged();
    emit goalsChanged();
    emit networkChanged();
}

void Metro::checkMission()
{
    if (!m_mission.active())
        return;
    bool all = true;
    int activeLines = 0, saturated = 0;
    for (const Line &l : m_lines)
        if (l.segmentCount() > 0) {
            ++activeLines;
            saturated += l.loadRatio() > 1;
        }
    for (MissionGoal &g : m_mission.goals) {
        switch (g.type) {
        case MissionGoal::Metric:
            g.value = goalValue(g.kind);
            break;
        case MissionGoal::RidersShare:
            g.value = m_totalServed;
            break;
        case MissionGoal::NoSaturation:
            g.value = activeLines > 0 && saturated == 0 ? 1 : 0;
            break;
        }
        all &= g.done();
    }
    if (all) {
        const double used = double(month() - m_mission.startMonth + 1) / (m_mission.deadline - m_mission.startMonth + 1);
        m_mission.status = 1;
        m_mission.stars = used <= 0.5 ? 3 : used <= 0.75 ? 2 : 1;
        emit missionChanged();
        emit missionFinished(true, m_mission.stars);
    } else if (month() > m_mission.deadline) {
        m_mission.status = 2;
        emit missionChanged();
        emit missionFinished(false, 0);
    }
}

QJsonObject Metro::missionJson() const
{
    if (m_mission.id.isEmpty())
        return {};
    QJsonArray goals;
    for (const MissionGoal &g : m_mission.goals)
        goals << QJsonObject{{"type", int(g.type)}, {"kind", int(g.kind)}, {"target", g.target}, {"title", g.title}};
    return QJsonObject{{"id", m_mission.id},         {"title", m_mission.title}, {"city", m_mission.cityLabel},
                       {"start", m_mission.startMonth}, {"deadline", m_mission.deadline},
                       {"status", m_mission.status}, {"stars", m_mission.stars}, {"goals", goals}};
}

void Metro::loadMission(const QJsonObject &o)
{
    m_mission = Mission();
    if (o.isEmpty())
        return;
    m_mission.id = o.value("id").toString();
    m_mission.title = o.value("title").toString();
    m_mission.cityLabel = o.value("city").toString();
    m_mission.startMonth = o.value("start").toInt(1);
    m_mission.deadline = o.value("deadline").toInt();
    m_mission.status = o.value("status").toInt();
    m_mission.stars = o.value("stars").toInt();
    for (const QJsonValue &v : o.value("goals").toArray()) {
        const QJsonObject go = v.toObject();
        MissionGoal g;
        g.type = MissionGoal::Type(go.value("type").toInt());
        g.kind = GoalKind(std::clamp(go.value("kind").toInt(), 0, int(GoalKind::Count) - 1));
        g.target = go.value("target").toDouble();
        g.title = go.value("title").toString();
        m_mission.goals << g;
    }
}

// ---------------------------------------------------------------------------
// Import du métro réel
// ---------------------------------------------------------------------------

void Metro::importNetwork(const QVector<ImportedLine> &imported)
{
    m_freeBuild = true;
    m_stations.clear();
    m_lines.clear();
    m_nextStationId = m_nextLineId = 1;
    auto stationFor = [&](const QString &name, const QPointF &pos) {
        // même nom à moins de 600 m (correspondance), ou station anonyme très proche
        for (const Station &s : m_stations) {
            const double d = std::hypot(s.pos.x() - pos.x(), s.pos.y() - pos.y());
            if ((!name.isEmpty() && s.name == name && d < 600) || d < 90)
                return s.id;
        }
        Station s;
        s.id = m_nextStationId++;
        s.pos = pos;
        s.name = name.isEmpty() ? nearestStreet(pos, 200) : name;
        if (s.name.isEmpty())
            s.name = tr("Station %1").arg(s.id);
        m_stations << s;
        return s.id;
    };
    QVector<QColor> used;
    for (const ImportedLine &il : imported) {
        Line l;
        l.id = m_nextLineId++;
        QString code = il.ref;
        code.remove(QRegularExpression("^(M|U|Line|Ligne)\\s*", QRegularExpression::CaseInsensitiveOption));
        code = code.left(3);
        l.code = code.isEmpty() || codeUsed(code) ? nextFreeCode(false) : code;
        l.name = tr("Ligne %1").arg(l.code);
        l.color = il.color.isValid() ? nearestPreset(il.color, used) : presetColors()[(l.id - 1) % presetColors().size()];
        used << l.color;
        l.wagons = Rules::MaxWagons;
        for (const auto &stop : il.stops) {
            const int id = stationFor(stop.first, stop.second);
            if (!l.stops.contains(id))
                l.stops << id;
        }
        if (l.stops.size() < 2)
            continue;
        m_lines << l;
        Line &added = m_lines.last();
        computeLineGeometry(added);
        added.trains = std::clamp(int(std::ceil(added.length / 1500.0)), 2, 12);
        added.paidLength = trackUnits(added);
    }
    // stations isolées (lignes écartées) : retirées
    m_stations.erase(std::remove_if(m_stations.begin(), m_stations.end(),
                                    [&](const Station &s) {
                                        return std::none_of(m_lines.begin(), m_lines.end(),
                                                            [&](const Line &l) { return l.stops.contains(s.id); });
                                    }),
                     m_stations.end());
    recompute();
    m_freeBuild = false;
    resetUndo();
}
