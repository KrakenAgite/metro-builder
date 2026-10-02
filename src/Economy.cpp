// Économie du réseau : tarif, subventions, emprunts, entretien et vieillissement du matériel,
// heures creuses, et croissance de la ville autour des stations bien desservies.
#include "Metro.h"

#include <QJsonArray>
#include <QLocale>
#include <QRandomGenerator>
#include <cmath>

namespace {

qint64 cellKey(const QPointF &c)
{
    return (qint64(std::floor(c.x() / Rules::GridCell)) << 32) ^ qint64(quint32(qint32(std::floor(c.y() / Rules::GridCell))));
}

// entretien réduit / normal / renforcé
constexpr double MaintCost[3] = {0.85, 1.0, 1.25};   // coût d'exploitation des rames
constexpr double MaintAging[3] = {1.3, 1.0, 0.7};    // vitesse de vieillissement
constexpr double MaintFailure[3] = {1.8, 1.0, 0.4};  // risque de panne

QString years(double months)
{
    const double y = months / 12;
    return y < 1 ? QObject::tr("moins d'un an")
                 : y < 2 ? QObject::tr("1 an")
                         : QObject::tr("%1 ans").arg(int(y));
}

} // namespace

// ---------------------------------------------------------------------------
// Tarif
// ---------------------------------------------------------------------------

void Metro::setFare(double euros)
{
    euros = std::clamp(std::round(euros * 10) / 10, 1.0, 4.0);
    if (qFuzzyCompare(euros, m_fare))
        return;
    m_fare = euros;
    recompute();
}

double Metro::fareDemandFactor() const
{
    // fréquentation : +/− environ 40 % par euro autour du tarif de base ; la recette est maximale vers 1,8 €
    return std::exp(-Rules::FareElasticity * (m_fare - Rules::Fare));
}

// ---------------------------------------------------------------------------
// Entretien et matériel roulant
// ---------------------------------------------------------------------------

void Metro::setMaintenance(int level)
{
    level = std::clamp(level, 0, 2);
    if (level == m_maintenance)
        return;
    m_maintenance = level;
    recompute();
}

double Metro::maintenanceCostFactor() const
{
    return MaintCost[m_maintenance];
}

double Metro::failureChance(const Line &l) const
{
    // aucune panne les 5 premières années, puis un risque qui augmente avec l'âge
    const double base = std::clamp((l.stockAge - 60) / 720.0, 0.0, 0.3);
    return std::min(0.5, base * MaintFailure[m_maintenance]);
}

double Metro::renewCost(const Line &l) const
{
    return l.wagons * l.trains * Rules::WagonCost * Rules::RenewShare * buildCostFactor();
}

bool Metro::renewStock(int lineId)
{
    Line *l = lineMut(lineId);
    if (!l || l->stockAge < 1)
        return false;
    if (!spend(l->wagons * l->trains * Rules::WagonCost * Rules::RenewShare, tr("le renouvellement du matériel")))
        return false;
    l->stockAge = 0;
    emit notice(tr("%1 : matériel roulant renouvelé").arg(l->name));
    recompute();
    return true;
}

void Metro::setOffPeak(int lineId, double ratio)
{
    ratio = std::clamp(ratio, 0.5, 1.0);
    if (Line *l = lineMut(lineId); l && !qFuzzyCompare(l->offPeak, ratio)) {
        l->offPeak = ratio;
        recompute();
    }
}

// ---------------------------------------------------------------------------
// Emprunts
// ---------------------------------------------------------------------------

double Metro::debt() const
{
    double d = 0;
    for (const Loan &l : m_loans)
        d += l.remaining;
    return d;
}

double Metro::loanPayment() const
{
    double p = 0;
    for (const Loan &l : m_loans)
        p += l.payment;
    return p;
}

bool Metro::takeLoan(double amount)
{
    if (m_sandbox || amount <= 0)
        return false;
    if (debt() + amount > Rules::MaxDebt + 1e-6) {
        emit message(tr("La banque refuse : l'encours ne peut dépasser %1 M€").arg(Rules::MaxDebt, 0, 'f', 0));
        return false;
    }
    const double r = Rules::LoanRate / 12;
    const int n = Rules::LoanMonths;
    Loan loan;
    loan.remaining = amount;
    loan.payment = amount * r / (1 - std::pow(1 + r, -n)); // mensualité constante
    loan.monthsLeft = n;
    m_loans << loan;
    m_money += amount;
    emit notice(tr("Emprunt de %1 M€ accordé : %2 M€ par mois pendant 10 ans")
                    .arg(amount, 0, 'f', 0)
                    .arg(QLocale(QLocale::French).toString(loan.payment, 'f', 2)));
    emit networkChanged();
    return true;
}

bool Metro::repayLoans()
{
    const double d = debt();
    if (d <= 0)
        return false;
    if (m_money < d) {
        emit message(tr("Trésorerie insuffisante pour rembourser %1 M€").arg(d, 0, 'f', 1));
        return false;
    }
    m_money -= d;
    m_current.loanPaid += d;
    m_loans.clear();
    emit notice(tr("Emprunts remboursés par anticipation (%1 M€)").arg(d, 0, 'f', 1));
    emit networkChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Fin de mois
// ---------------------------------------------------------------------------

void Metro::monthlyEconomy()
{
    // remboursement des emprunts (intérêts compris)
    const double r = Rules::LoanRate / 12;
    for (Loan &l : m_loans) {
        const double pay = std::min(l.payment, l.remaining * (1 + r));
        l.remaining = l.remaining * (1 + r) - pay;
        --l.monthsLeft;
        m_money -= pay;
        m_current.loanPaid += pay;
    }
    m_loans.erase(std::remove_if(m_loans.begin(), m_loans.end(),
                                 [](const Loan &l) { return l.monthsLeft <= 0 || l.remaining < 0.01; }),
                  m_loans.end());

    // vieillissement du matériel, pannes des rames âgées
    QVector<int> failing;
    for (Line &l : m_lines) {
        if (l.segmentCount() == 0)
            continue;
        l.stockAge += MaintAging[m_maintenance];
        bool already = false;
        for (const GameEvent &e : m_events)
            already |= e.kind == EventKind::TrainFailure && e.lineId == l.id;
        if (!m_sandbox && !already && QRandomGenerator::global()->generateDouble() < failureChance(l))
            failing << l.id;
    }
    for (int id : failing) {
        const Line *l = line(id);
        GameEvent e;
        e.id = m_nextEventId++;
        e.kind = EventKind::TrainFailure;
        e.startMonth = month();
        e.lineId = id;
        e.tone = -1;
        e.value = 0.6;
        e.weeksLeft = 2;
        e.cost = std::round(1 + l->trains * 0.5);
        e.title = tr("Rame en panne sur la %1").arg(l->name);
        e.text = tr("Une rame de %1 (matériel de %2) est immobilisée : capacité de la ligne réduite de 40 % "
                    "pendant 2 semaines. Un entretien renforcé ou un renouvellement du matériel limite ces pannes.")
                     .arg(l->name, years(l->stockAge));
        e.choices = {tr("Réparer tout de suite (−%1 M€)").arg(e.cost, 0, 'f', 0), tr("Attendre")};
        e.pending = true;
        m_events << e;
        emit eventStarted(e);
    }
    if (!failing.isEmpty())
        emit eventsChanged();

    growCity();
}

// ---------------------------------------------------------------------------
// Croissance urbaine
// ---------------------------------------------------------------------------

// Les quartiers bien desservis se densifient : jusqu'à +0,5 %/mois là où le métro capte bien la demande,
// plafonné à 2,5 fois la densité d'origine ; toute la ville croît aussi lentement (0,05 %/mois).
void Metro::growCity()
{
    if (!m_city || m_grid.w == 0)
        return;
    m_baseGrowth = std::min(1.5, m_baseGrowth * 1.0005);
    const int n = m_grid.w * m_grid.h;
    bool changed = false;
    for (int c = 0; c < n; ++c) {
        if (m_grid.pop[c] + m_grid.jobs[c] <= 0)
            continue;
        const double g = 0.004 * m_grid.servedFrac[c] + 0.001 * m_grid.coverage[c];
        if (g < 1e-5)
            continue;
        float &f = m_growth[cellKey(m_grid.center(c))];
        if (f <= 0)
            f = 1;
        f = float(std::min(Rules::CityGrowthCap, f * (1 + g)));
        changed = true;
    }
    Q_UNUSED(changed);
    buildGrid(); // la croissance de fond touche toutes les cellules
    recompute();
    // bilan annuel (appelé avant l'archivage du mois qui se termine)
    const int closed = m_history.size() + 1;
    if (closed % 12 == 0) {
        const double gained = m_population - m_popYearStart;
        if (m_popYearStart > 0 && gained > 0)
            emit notice(tr("Bilan de l'année %1 : la ville compte %2 habitants (+%3), surtout autour de vos stations")
                            .arg(closed / 12)
                            .arg(QLocale(QLocale::French).toString(qRound(m_population)))
                            .arg(QLocale(QLocale::French).toString(qRound(gained))));
        m_popYearStart = m_population;
    }
}

void Metro::applyGrowth()
{
    const int n = m_grid.w * m_grid.h;
    m_population = 0;
    for (int c = 0; c < n; ++c) {
        double f = m_baseGrowth;
        if (!m_growth.isEmpty())
            if (auto it = m_growth.constFind(cellKey(m_grid.center(c))); it != m_growth.constEnd())
                f *= it.value();
        m_grid.pop[c] *= f;
        m_grid.jobs[c] *= f;
        m_population += m_grid.pop[c];
    }
}

// ---------------------------------------------------------------------------
// Sauvegarde
// ---------------------------------------------------------------------------

QJsonObject Metro::economyJson() const
{
    QJsonArray loans, growth;
    for (const Loan &l : m_loans)
        loans << QJsonArray{l.remaining, l.payment, l.monthsLeft};
    for (auto it = m_growth.cbegin(); it != m_growth.cend(); ++it) {
        if (it.value() < 1.001f)
            continue;
        growth << double(qint32(it.key() >> 32)) << double(qint32(quint32(it.key() & 0xffffffff)))
               << std::round(it.value() * 1000) / 1000;
    }
    return QJsonObject{{"fare", m_fare},         {"maintenance", m_maintenance}, {"loans", loans},
                       {"baseGrowth", m_baseGrowth}, {"growth", growth},         {"popYearStart", m_popYearStart}};
}

void Metro::loadEconomy(const QJsonObject &o)
{
    m_fare = std::clamp(o.value("fare").toDouble(Rules::Fare), 1.0, 4.0);
    m_maintenance = std::clamp(o.value("maintenance").toInt(1), 0, 2);
    m_loans.clear();
    for (const QJsonValue &v : o.value("loans").toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() == 3)
            m_loans << Loan{a[0].toDouble(), a[1].toDouble(), a[2].toInt()};
    }
    m_baseGrowth = o.value("baseGrowth").toDouble(1);
    m_growth.clear();
    const QJsonArray g = o.value("growth").toArray();
    for (int i = 0; i + 2 < g.size(); i += 3) {
        const qint64 key = (qint64(qint32(g[i].toDouble())) << 32) ^ qint64(quint32(qint32(g[i + 1].toDouble())));
        m_growth[key] = float(g[i + 2].toDouble());
    }
    m_popYearStart = o.value("popYearStart").toDouble(0);
}
