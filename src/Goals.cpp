// Score et objectifs : trois objectifs actifs, remplacés à chaque réussite par le palier
// suivant d'une autre famille ; points de score mensuels selon la fréquentation.
#include "Metro.h"

#include <QJsonArray>
#include <QLocale>
#include <cmath>

namespace {

constexpr int ActiveGoals = 3;

// ordre dans lequel les familles d'objectifs se succèdent
const GoalKind Rotation[] = {
    GoalKind::Transfers, GoalKind::Coverage, GoalKind::ProfitStreak, GoalKind::Network, GoalKind::Capture,
    GoalKind::Turnover,  GoalKind::Stations, GoalKind::Riders,       GoalKind::Money,   GoalKind::Lines,
};
constexpr int RotationSize = int(sizeof(Rotation) / sizeof(Rotation[0]));

struct Ladder {
    QVector<double> steps; // paliers prédéfinis
    double growth;         // au-delà : multiplicateur (> 1) ou incrément (si add) ; 0 = pas au-delà
    bool add;
};

const Ladder &ladder(GoalKind k)
{
    static const Ladder L[] = {
        {{3, 8, 15, 25, 35, 50, 70, 100}, 1.3, false},                 // Stations
        {{1, 2, 3, 4, 6, 8, 10, 12}, 2, true},                          // Lines
        {{500, 1500, 3000, 5000, 8000, 12000, 18000, 25000}, 1.4, false}, // Riders
        {{10, 25, 40, 50, 60, 70, 80, 90, 95}, 0, true},              // Capture (%)
        {{15, 30, 45, 60, 70, 80, 90, 95}, 0, true},                  // Coverage (%)
        {{1, 2, 4, 6, 9, 12, 16, 20}, 1.3, false},                     // Transfers
        {{3, 8, 15, 25, 40, 60, 85, 120}, 1.4, false},                 // Network (km)
        {{700, 1000, 1500, 2500, 4000, 6000, 10000}, 1.5, false},      // Money (M€)
        {{1, 3, 6, 12, 24, 36, 48}, 12, true},                         // ProfitStreak
        {{25, 100, 250, 500, 1000, 2000, 4000}, 2, false},             // Turnover (M€)
    };
    return L[int(k)];
}

double targetFor(GoalKind k, int level)
{
    const Ladder &l = ladder(k);
    if (level < l.steps.size())
        return l.steps[level];
    double t = l.steps.last();
    for (int i = int(l.steps.size()) - 1; i < level; ++i)
        t = l.add ? t + l.growth : t * l.growth;
    if (!l.add) // valeurs rondes
        t = std::round(t / std::pow(10, std::floor(std::log10(t)) - 1)) * std::pow(10, std::floor(std::log10(t)) - 1);
    return t;
}

// plus aucun palier (pourcentages : 95 % est le dernier)
bool exhausted(GoalKind k, int level)
{
    return level >= ladder(k).steps.size() && ladder(k).growth <= 0;
}

QString num(double v, int decimals = 0)
{
    return QLocale().toString(v, 'f', decimals);
}

QString moneyText(double v)
{
    return v >= 1000 ? QStringLiteral("%1 Md€").arg(num(v / 1000, v >= 10000 ? 0 : 1)) : QStringLiteral("%1 M€").arg(num(v));
}

} // namespace

double Metro::goalValue(GoalKind kind) const
{
    switch (kind) {
    case GoalKind::Stations:
        return m_stations.size();
    case GoalKind::Lines: {
        int n = 0;
        for (const Line &l : m_lines)
            n += l.stops.size() >= 2;
        return n;
    }
    case GoalKind::Riders:
        return m_totalServed;
    case GoalKind::Capture:
        return m_satisfaction * 100;
    case GoalKind::Coverage: {
        const double res = m_city ? m_city->totalResidents : 0;
        return res > 0 ? m_coveredResidents / res * 100 : 0;
    }
    case GoalKind::Transfers: {
        int n = 0;
        for (const Station &s : m_stations)
            n += s.lineCount >= 2;
        return n;
    }
    case GoalKind::Network: {
        double m = 0;
        for (const Line &l : m_lines)
            if (l.stops.size() >= 2)
                m += l.length;
        return m / 1000;
    }
    case GoalKind::Money:
        return m_money;
    case GoalKind::ProfitStreak:
        return m_profitStreak;
    case GoalKind::Turnover:
        return m_totalRevenue;
    case GoalKind::Count:
        break;
    }
    return 0;
}

Objective Metro::makeGoal(GoalKind kind, int level) const
{
    Objective o;
    o.kind = kind;
    o.level = level;
    o.target = targetFor(kind, level);
    o.points = 100 * (level + 1);
    o.reward = std::min(15.0 + 15.0 * level, 150.0);
    const double t = o.target;
    switch (kind) {
    case GoalKind::Stations:
        o.title = tr("Construire %1 stations").arg(num(t));
        break;
    case GoalKind::Lines:
        o.title = t <= 1 ? tr("Ouvrir une première ligne") : tr("Exploiter %1 lignes").arg(num(t));
        break;
    case GoalKind::Riders:
        o.title = tr("Transporter %1 voyageurs/h").arg(num(t));
        break;
    case GoalKind::Capture:
        o.title = tr("Capter %1 % de la demande").arg(num(t));
        break;
    case GoalKind::Coverage:
        o.title = tr("Desservir %1 % des habitants").arg(num(t));
        break;
    case GoalKind::Transfers:
        o.title = t <= 1 ? tr("Créer une correspondance") : tr("Avoir %1 correspondances").arg(num(t));
        break;
    case GoalKind::Network:
        o.title = tr("Lignes totalisant %1 km").arg(num(t));
        break;
    case GoalKind::Money:
        o.title = tr("Trésorerie de %1").arg(moneyText(t));
        break;
    case GoalKind::ProfitStreak:
        o.title = t <= 1 ? tr("Un mois bénéficiaire") : tr("%1 mois bénéficiaires d'affilée").arg(num(t));
        break;
    case GoalKind::Turnover:
        o.title = tr("Chiffre d'affaires de %1").arg(moneyText(t));
        break;
    case GoalKind::Count:
        break;
    }
    o.value = goalValue(kind);
    return o;
}

QString Metro::goalProgressText(const Objective &o)
{
    const double v = std::min(o.value, o.target);
    switch (o.kind) {
    case GoalKind::Capture:
    case GoalKind::Coverage:
        return QStringLiteral("%1 / %2 %").arg(num(v, 1), num(o.target));
    case GoalKind::Network:
        return QStringLiteral("%1 / %2 km").arg(num(v, 1), num(o.target));
    case GoalKind::Money:
    case GoalKind::Turnover:
        return QStringLiteral("%1 / %2").arg(moneyText(std::max(0.0, v)), moneyText(o.target));
    case GoalKind::Riders:
        return QStringLiteral("%1 / %2 voy./h").arg(num(v), num(o.target));
    case GoalKind::ProfitStreak:
        return tr("%1 / %2 mois").arg(num(v), num(o.target));
    default:
        return QStringLiteral("%1 / %2").arg(num(v), num(o.target));
    }
}

// Ajoute l'objectif suivant de la rotation (famille absente des objectifs actifs).
// Les paliers déjà dépassés sont sautés, sans récompense.
bool Metro::spawnGoal()
{
    for (int tries = 0; tries < RotationSize; ++tries) {
        const GoalKind kind = Rotation[m_goalRotation];
        m_goalRotation = (m_goalRotation + 1) % RotationSize;
        bool active = false;
        for (const Objective &g : m_goals)
            active |= g.kind == kind;
        if (active)
            continue;
        int &level = m_goalLevel[int(kind)];
        const double value = goalValue(kind);
        while (!exhausted(kind, level) && targetFor(kind, level) <= value)
            ++level;
        if (exhausted(kind, level))
            continue;
        m_goals << makeGoal(kind, level);
        ++level;
        return true;
    }
    return false;
}

void Metro::checkGoals()
{
    if (m_checkingGoals || m_sandbox)
        return;
    if (!m_mission.id.isEmpty() && m_mission.status == 0) { // scénario : seules comptent les conditions de victoire
        checkMission();
        return;
    }
    m_checkingGoals = true;
    bool changed = false;
    if (m_goals.isEmpty() && m_goalsDone == 0 && m_goalLevel[0] == 0) {
        // début de partie : on commence par les bases
        for (GoalKind k : {GoalKind::Stations, GoalKind::Lines, GoalKind::Riders}) {
            int &level = m_goalLevel[int(k)];
            while (!exhausted(k, level) && targetFor(k, level) <= goalValue(k))
                ++level;
            m_goals << makeGoal(k, level);
            ++level;
        }
        changed = true;
    }
    for (int i = 0; i < m_goals.size();) {
        Objective &g = m_goals[i];
        g.value = goalValue(g.kind);
        if (g.value + 1e-9 < g.target) {
            ++i;
            continue;
        }
        const Objective done = g;
        m_goals.removeAt(i);
        m_score += done.points;
        m_current.goalPoints += done.points;
        ++m_goalsDone;
        income(done.reward);
        emit goalCompleted(done);
        changed = true;
        i = 0; // la prime a pu faire progresser d'autres objectifs
    }
    while (m_goals.size() < ActiveGoals && spawnGoal())
        changed = true;
    m_checkingGoals = false;
    if (changed)
        emit goalsChanged();
}

int Metro::monthPoints() const
{
    // fréquentation + qualité de desserte + bonus si l'exploitation est rentable
    const bool profitable = m_monthlyRevenue > m_monthlyCost && m_monthlyRevenue > 0;
    return int(std::round(m_totalServed / 40 + m_satisfaction * 60)) + (profitable ? 10 : 0);
}

QJsonObject Metro::goalsJson() const
{
    QJsonArray levels, active;
    for (int l : m_goalLevel)
        levels << l;
    for (const Objective &g : m_goals)
        active << QJsonArray{int(g.kind), g.level};
    return QJsonObject{{"score", m_score},         {"lastMonth", m_lastMonthPoints}, {"done", m_goalsDone},
                       {"streak", m_profitStreak}, {"rotation", m_goalRotation},     {"levels", levels},
                       {"active", active}};
}

void Metro::loadGoals(const QJsonObject &o)
{
    m_score = o.value("score").toDouble();
    m_lastMonthPoints = o.value("lastMonth").toInt();
    m_goalsDone = o.value("done").toInt();
    m_profitStreak = o.value("streak").toInt();
    m_goalRotation = std::clamp(o.value("rotation").toInt(), 0, RotationSize - 1);
    const QJsonArray levels = o.value("levels").toArray();
    for (int k = 0; k < int(GoalKind::Count); ++k)
        m_goalLevel[k] = k < levels.size() ? levels[k].toInt() : 0;
    m_goals.clear();
    for (const QJsonValue &v : o.value("active").toArray()) {
        const QJsonArray a = v.toArray();
        const int kind = a[0].toInt(-1);
        if (kind >= 0 && kind < int(GoalKind::Count))
            m_goals << makeGoal(GoalKind(kind), a[1].toInt());
    }
}
