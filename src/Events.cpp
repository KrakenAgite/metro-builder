// Événements aléatoires du jeu : tirage mensuel, effets sur la simulation, choix du joueur.
#include "Metro.h"

#include <QJsonArray>
#include <QLocale>
#include <QRandomGenerator>
#include <QSet>
#include <cmath>

namespace {

constexpr int MaxActiveEvents = 2;
constexpr double MonthlyChance = 0.2; // probabilité qu'un événement survienne en fin de mois
constexpr int MinCalmMonths = 2;      // mois sans nouvel événement après chacun

QRandomGenerator &rng()
{
    return *QRandomGenerator::global();
}

template<typename T>
const T &pick(const QVector<T> &v)
{
    return v[rng().bounded(int(v.size()))];
}

double between(double lo, double hi)
{
    return lo + rng().generateDouble() * (hi - lo);
}

QString fmtMoney(double v)
{
    return QLocale().toString(v, 'f', v < 10 ? 1 : 0);
}

} // namespace

// ---------------------------------------------------------------------------
// Effets en cours
// ---------------------------------------------------------------------------

double Metro::lineCapacityFactor(int lineId) const
{
    double f = 1;
    for (const GameEvent &e : m_events)
        if (e.lineId == lineId
            && (e.kind == EventKind::Strike || e.kind == EventKind::Breakdown || e.kind == EventKind::TrainFailure))
            f *= e.value;
    return f;
}

bool Metro::stationClosed(int stationId) const
{
    for (const GameEvent &e : m_events)
        if (e.kind == EventKind::Flood && e.stationId == stationId)
            return true;
    return false;
}

double Metro::buildCostFactor() const
{
    double f = 1;
    for (const GameEvent &e : m_events)
        if (e.kind == EventKind::ConstructionDeal || e.kind == EventKind::MaterialShortage)
            f *= e.value;
    return f;
}

double Metro::opCostFactor() const
{
    double f = 1;
    for (const GameEvent &e : m_events)
        if (e.kind == EventKind::EnergyPrice)
            f *= e.value;
    return f;
}

double Metro::globalDemandFactor() const
{
    double f = 1;
    for (const GameEvent &e : m_events)
        if (e.kind == EventKind::Pollution || e.kind == EventKind::BadPress)
            f *= e.value;
    return f * fareDemandFactor();
}

double Metro::demandFactorAt(const QPointF &p) const
{
    double f = 1;
    for (const GameEvent &e : m_events)
        if (e.kind == EventKind::BigEvent && std::hypot(p.x() - e.pos.x(), p.y() - e.pos.y()) <= e.radius)
            f *= e.value;
    return f;
}

void Metro::applyDevelopments()
{
    // habitants / emplois des nouveaux quartiers, répartis uniformément dans leur rayon
    for (const Development &d : m_developments) {
        QVector<int> cells;
        for (int i = 0; i < m_grid.w * m_grid.h; ++i) {
            const QPointF c = m_grid.center(i);
            if (std::hypot(c.x() - d.pos.x(), c.y() - d.pos.y()) <= d.radius)
                cells << i;
        }
        for (int i : cells) {
            m_grid.pop[i] += d.pop / cells.size();
            m_grid.jobs[i] += d.jobs / cells.size();
        }
    }
}

void Metro::income(double amount)
{
    m_money += amount;
    m_current.revenue += amount;
    m_totalRevenue += amount;
}

void Metro::expense(double amount)
{
    m_money -= amount;
    m_current.operating += amount;
    m_totalOperating += amount;
}

// ---------------------------------------------------------------------------
// Déroulement
// ---------------------------------------------------------------------------

void Metro::tickEvents()
{
    bool changed = false;
    for (int i = m_events.size() - 1; i >= 0; --i) {
        GameEvent &e = m_events[i];
        if (e.pending) // décision attendue : l'effet dure tant que le joueur n'a pas choisi
            continue;
        if (--e.weeksLeft <= 0) {
            emit notice(tr("Terminé : %1").arg(e.title));
            m_events.removeAt(i);
            changed = true;
        }
    }
    if (changed) {
        recompute();
        emit eventsChanged();
    }
}

void Metro::rollEvents()
{
    ++m_monthsSinceEvent;
    if (!m_city || m_sandbox || m_events.size() >= MaxActiveEvents || m_monthsSinceEvent <= MinCalmMonths
        || rng().generateDouble() > MonthlyChance)
        return;
    // tirage pondéré parmi les événements applicables
    // environ 7 événements sur 10 sont favorables
    const QVector<QPair<EventKind, int>> table = {
        {EventKind::BigEvent, 12},      {EventKind::Subsidy, 10},         {EventKind::Development, 10},
        {EventKind::Pollution, 8},      {EventKind::Sponsor, 8},          {EventKind::ConstructionDeal, 6},
        {EventKind::Breakdown, 6},      {EventKind::BadPress, 5},         {EventKind::Strike, 4},
        {EventKind::EnergyPrice, 4},    {EventKind::MaterialShortage, 3}, {EventKind::Flood, 3},
    };
    static const QSet<EventKind> unfavorable = {EventKind::Strike, EventKind::Breakdown, EventKind::EnergyPrice,
                                                EventKind::MaterialShortage, EventKind::Flood, EventKind::BadPress};
    QVector<QPair<EventKind, int>> candidates;
    for (const auto &entry : table) // jamais deux coups durs d'affilée
        if (!(m_lastEventBad && unfavorable.contains(entry.first)))
            candidates << entry;
    while (!candidates.isEmpty()) {
        int total = 0;
        for (const auto &c : candidates)
            total += c.second;
        int r = rng().bounded(total);
        int idx = 0;
        while (r >= candidates[idx].second) {
            r -= candidates[idx].second;
            ++idx;
        }
        if (startEvent(candidates[idx].first))
            return;
        candidates.removeAt(idx); // pas applicable maintenant : on en tire un autre
    }
}

bool Metro::triggerEvent(EventKind kind)
{
    return startEvent(kind);
}

bool Metro::startEvent(EventKind kind)
{
    // cibles possibles
    QVector<const Line *> activeLines;
    for (const Line &l : m_lines)
        if (l.segmentCount() > 0 && lineCapacityFactor(l.id) >= 1)
            activeLines << &l;
    QVector<const Station *> activeStations;
    for (const Station &s : m_stations)
        if (s.lineCount > 0 && !stationClosed(s.id))
            activeStations << &s;
    for (const GameEvent &e : m_events)
        if (e.kind == kind && kind != EventKind::BigEvent)
            return false; // pas deux fois le même effet global en même temps

    GameEvent e;
    e.id = m_nextEventId++;
    e.kind = kind;
    e.startMonth = month();

    switch (kind) {
    case EventKind::Strike: {
        if (activeLines.isEmpty())
            return false;
        const Line *l = pick(activeLines);
        e.lineId = l->id;
        e.tone = -1;
        e.value = 0;
        e.weeksLeft = 2 * Rules::WeeksPerMonth;
        e.cost = std::round(3 + l->trains);
        e.title = tr("Grève sur la %1").arg(l->name);
        e.text = tr("Les conducteurs de la %1 cessent le travail : la ligne est à l'arrêt pendant 2 mois. "
                    "Négocier coûte %2 M€ et met fin à la grève tout de suite.")
                     .arg(l->name, fmtMoney(e.cost));
        e.choices = {tr("Négocier (−%1 M€)").arg(fmtMoney(e.cost)), tr("Laisser durer")};
        break;
    }
    case EventKind::Breakdown: {
        if (activeLines.isEmpty())
            return false;
        const Line *l = pick(activeLines);
        e.lineId = l->id;
        e.tone = -1;
        e.value = 0.5;
        e.weeksLeft = 1 * Rules::WeeksPerMonth;
        e.cost = 2;
        e.title = tr("Panne de signalisation sur la %1").arg(l->name);
        e.text = tr("Un incident de signalisation divise par deux la capacité de la %1 pendant un mois.").arg(l->name);
        e.choices = {tr("Réparation express (−%1 M€)").arg(fmtMoney(e.cost)), tr("Attendre")};
        break;
    }
    case EventKind::BigEvent: {
        if (activeStations.isEmpty())
            return false;
        const Station *s = pick(activeStations);
        static const QStringList kinds = {tr("Concert géant"), tr("Match de championnat"), tr("Salon international"),
                                          tr("Festival de rue"), tr("Marathon")};
        e.stationId = s->id;
        e.pos = s->pos;
        e.radius = 900;
        e.tone = 1;
        e.value = 2.0;
        e.weeksLeft = 1 * Rules::WeeksPerMonth;
        e.title = tr("%1 près de %2").arg(pick(kinds), s->name);
        e.text = tr("Pendant un mois, la demande de déplacements double autour de la station %1.").arg(s->name);
        break;
    }
    case EventKind::Subsidy: {
        if (month() < 3)
            return false;
        e.tone = 1;
        e.value = std::round(15 + 120 * m_satisfaction);
        e.title = tr("Subvention de la région");
        e.text = tr("Satisfaite de la part de la demande captée par le métro (%1 %), la région verse %2 M€.")
                     .arg(qRound(m_satisfaction * 100))
                     .arg(fmtMoney(e.value));
        income(e.value);
        break;
    }
    case EventKind::EnergyPrice:
        e.tone = -1;
        e.value = 1.2;
        e.weeksLeft = 2 * Rules::WeeksPerMonth;
        e.title = tr("Flambée des prix de l'énergie");
        e.text = tr("Les coûts d'exploitation augmentent de 20 % pendant 2 mois.");
        break;
    case EventKind::ConstructionDeal:
        e.tone = 1;
        e.value = 0.8;
        e.weeksLeft = 2 * Rules::WeeksPerMonth;
        e.title = tr("Rabais des entreprises de travaux");
        e.text = tr("Les chantiers coûtent 20 % de moins pendant 2 mois : c'est le moment de construire !");
        break;
    case EventKind::MaterialShortage:
        e.tone = -1;
        e.value = 1.15;
        e.weeksLeft = 2 * Rules::WeeksPerMonth;
        e.title = tr("Pénurie de matériaux");
        e.text = tr("Les coûts de construction augmentent de 15 % pendant 2 mois.");
        break;
    case EventKind::Flood: {
        if (activeStations.isEmpty())
            return false;
        const Station *s = pick(activeStations);
        e.stationId = s->id;
        e.pos = s->pos;
        e.radius = 250;
        e.tone = -1;
        e.weeksLeft = 1 * Rules::WeeksPerMonth;
        e.cost = 2;
        e.title = tr("Inondation à %1").arg(s->name);
        e.text = tr("Après de fortes pluies, la station %1 est fermée pendant un mois.").arg(s->name);
        e.choices = {tr("Pompage d'urgence (−%1 M€)").arg(fmtMoney(e.cost)), tr("Attendre la décrue")};
        break;
    }
    case EventKind::Pollution:
        e.tone = 1;
        e.value = 1.2;
        e.weeksLeft = 1 * Rules::WeeksPerMonth;
        e.title = tr("Pic de pollution");
        e.text = tr("La circulation automobile est restreinte : la demande de transport en commun augmente de 20 % "
                    "pendant un mois.");
        break;
    case EventKind::Development: {
        if (!m_city || m_city->bounds.isEmpty())
            return false;
        static const QStringList housing = {tr("Nouveau quartier"), tr("Écoquartier"), tr("Programme de logements")};
        static const QStringList offices = {tr("Pôle de bureaux"), tr("Campus universitaire"), tr("Centre commercial"),
                                            tr("Hôpital")};
        const QRectF b = m_city->bounds.adjusted(400, 400, -400, -400);
        if (b.isEmpty())
            return false;
        Development d;
        d.pos = QPointF(between(b.left(), b.right()), between(b.top(), b.bottom()));
        d.radius = 400;
        const bool jobs = rng().bounded(2) == 0;
        d.name = jobs ? pick(offices) : pick(housing);
        d.pop = jobs ? between(500, 1500) : between(4000, 9000);
        d.jobs = jobs ? between(4000, 10000) : between(300, 1000);
        m_developments << d;
        const QString street = nearestStreet(d.pos, 600);
        e.pos = d.pos;
        e.radius = d.radius;
        e.tone = 1;
        e.weeksLeft = 3 * Rules::WeeksPerMonth; // marqueur visible 3 mois ; les habitants restent
        e.title = tr("%1 inauguré").arg(d.name);
        e.text = tr("%1 ouvre %2 : +%3 habitants et +%4 emplois. Pensez à le desservir !")
                     .arg(d.name)
                     .arg(street.isEmpty() ? tr("dans la ville") : tr("près de %1").arg(street))
                     .arg(QLocale().toString(qRound(d.pop)))
                     .arg(QLocale().toString(qRound(d.jobs)));
        buildGrid();
        break;
    }
    case EventKind::Sponsor: {
        QVector<const Station *> free;
        for (const Station *s : activeStations)
            if (!s->name.contains(QStringLiteral(" – ")))
                free << s;
        if (free.isEmpty())
            return false;
        const Station *s = pick(free);
        // entreprises fictives
        static const QStringList sponsors = {tr("Cafés Martin"), tr("Assurances Horizon"), tr("Boulangeries Dorées"),
                                             tr("Électro Plus"), tr("Mutuelle Avenir"), tr("Brasseries du Parc")};
        e.stationId = s->id;
        e.pos = s->pos;
        e.radius = 200;
        e.tone = 1;
        e.extra = pick(sponsors);
        e.cost = std::round(between(8, 25));
        e.title = tr("Proposition de mécénat");
        e.text = tr("%1 propose %2 M€ pour associer son nom à la station %3 (« %3 – %1 »).")
                     .arg(e.extra, fmtMoney(e.cost), s->name);
        e.choices = {tr("Accepter (+%1 M€)").arg(fmtMoney(e.cost)), tr("Refuser")};
        break;
    }
    case EventKind::BadPress: {
        const Line *worst = nullptr;
        for (const Line &l : m_lines)
            if (l.loadRatio() > 1 && (!worst || l.loadRatio() > worst->loadRatio()))
                worst = &l;
        if (!worst)
            return false;
        e.lineId = -1; // effet global, la ligne n'est citée qu'en exemple
        e.tone = -1;
        e.value = 0.9;
        e.weeksLeft = 1 * Rules::WeeksPerMonth;
        e.title = tr("Les usagers de la %1 en colère").arg(worst->name);
        e.text = tr("La saturation de la %1 fait la une : la fréquentation du réseau baisse de 10 % pendant "
                    "un mois. Ajoutez des rames ou des voitures !")
                     .arg(worst->name);
        break;
    }
    }

    e.pending = !e.choices.isEmpty();
    m_monthsSinceEvent = 0;
    m_lastEventBad = e.tone < 0;
    if (e.weeksLeft > 0 || e.pending)
        m_events << e;
    recompute();
    emit eventStarted(e);
    emit eventsChanged();
    return true;
}

void Metro::resolveEvent(int id, int choice)
{
    for (int i = 0; i < m_events.size(); ++i) {
        GameEvent &e = m_events[i];
        if (e.id != id || !e.pending)
            continue;
        e.pending = false;
        bool remove = false;
        switch (e.kind) {
        case EventKind::Strike:
        case EventKind::Breakdown:
        case EventKind::Flood:
        case EventKind::TrainFailure:
            if (choice == 0) {
                if (m_money < e.cost) {
                    emit message(tr("Budget insuffisant (%1 M€ requis)").arg(fmtMoney(e.cost)));
                } else {
                    expense(e.cost);
                    emit notice(tr("%1 : problème réglé").arg(e.title));
                    remove = true;
                }
            }
            break;
        case EventKind::Sponsor:
            if (choice == 0) {
                if (Station *s = stationMut(e.stationId)) {
                    s->name = QStringLiteral("%1 – %2").arg(s->name, e.extra);
                    income(e.cost);
                    emit notice(tr("Station renommée : %1").arg(s->name));
                }
            }
            remove = true; // décision ponctuelle
            break;
        default:
            break;
        }
        if (remove || e.weeksLeft <= 0)
            m_events.removeAt(i);
        recompute();
        emit eventsChanged();
        return;
    }
}

// ---------------------------------------------------------------------------
// Sauvegarde
// ---------------------------------------------------------------------------

QJsonArray Metro::eventsJson() const
{
    QJsonArray events;
    for (const GameEvent &e : m_events)
        events << QJsonObject{{"id", e.id},           {"kind", int(e.kind)},       {"title", e.title},
                              {"text", e.text},       {"tone", e.tone},            {"line", e.lineId},
                              {"station", e.stationId}, {"x", e.pos.x()},          {"y", e.pos.y()},
                              {"radius", e.radius},   {"value", e.value},          {"cost", e.cost},
                              {"extra", e.extra},     {"weeks", e.weeksLeft},    {"start", e.startMonth},
                              {"choices", QJsonArray::fromStringList(e.choices)}, {"pending", e.pending}};
    QJsonArray devs;
    for (const Development &d : m_developments)
        devs << QJsonObject{{"x", d.pos.x()}, {"y", d.pos.y()}, {"radius", d.radius},
                            {"pop", d.pop},   {"jobs", d.jobs}, {"name", d.name}};
    return QJsonArray{events, devs, m_nextEventId, m_monthsSinceEvent, m_lastEventBad};
}

void Metro::loadEvents(const QJsonObject &o)
{
    m_events.clear();
    m_developments.clear();
    m_nextEventId = 1;
    const QJsonArray all = o.value("events").toArray();
    m_monthsSinceEvent = all.size() >= 5 ? all[3].toInt() : 0;
    m_lastEventBad = all.size() >= 5 && all[4].toBool();
    if (all.size() >= 3) {
        for (const QJsonValue &v : all[0].toArray()) {
            const QJsonObject j = v.toObject();
            GameEvent e;
            e.id = j.value("id").toInt();
            e.kind = EventKind(j.value("kind").toInt());
            e.title = j.value("title").toString();
            e.text = j.value("text").toString();
            e.tone = j.value("tone").toInt();
            e.lineId = j.value("line").toInt(-1);
            e.stationId = j.value("station").toInt(-1);
            e.pos = QPointF(j.value("x").toDouble(), j.value("y").toDouble());
            e.radius = j.value("radius").toDouble();
            e.value = j.value("value").toDouble(1);
            e.cost = j.value("cost").toDouble();
            e.extra = j.value("extra").toString();
            // anciennes sauvegardes : durée en mois
            e.weeksLeft = j.contains("weeks") ? j.value("weeks").toInt()
                                              : j.value("months").toInt() * Rules::WeeksPerMonth;
            e.startMonth = j.value("start").toInt();
            for (const QJsonValue &c : j.value("choices").toArray())
                e.choices << c.toString();
            e.pending = j.value("pending").toBool();
            m_events << e;
        }
        for (const QJsonValue &v : all[1].toArray()) {
            const QJsonObject j = v.toObject();
            Development d;
            d.pos = QPointF(j.value("x").toDouble(), j.value("y").toDouble());
            d.radius = j.value("radius").toDouble(400);
            d.pop = j.value("pop").toDouble();
            d.jobs = j.value("jobs").toDouble();
            d.name = j.value("name").toString();
            m_developments << d;
        }
        m_nextEventId = all[2].toInt(1);
    }
    buildGrid(); // les nouveaux quartiers s'ajoutent à la grille de demande
    emit eventsChanged();
}
