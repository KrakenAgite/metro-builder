// Partie de survie : tenir N années. Faillite (6 mois d'affilée dans le rouge) et, en option,
// colère de la ville (6 mois d'affilée sous une satisfaction minimale qui monte avec les années).
#include "Metro.h"

#include <QJsonObject>
#include <cmath>

void Metro::startSurvival(int years, bool anger)
{
    m_survival = Survival();
    m_survival.years = std::max(0, years);
    m_survival.anger = anger;
    m_survival.startMonth = month();
    emit missionChanged();
}

int Metro::survivalMonthsLeft() const
{
    if (m_survival.years <= 0)
        return 0;
    return std::max(0, m_survival.startMonth + m_survival.years * 12 - month());
}

double Metro::angerThreshold() const
{
    // aucune exigence la première année, puis 10 % de la demande captée, +4 points par an (40 % au plus)
    const int elapsed = month() - m_survival.startMonth;
    if (elapsed < AngerGrace)
        return 0;
    return std::min(0.40, 0.10 + 0.04 * ((elapsed - AngerGrace) / 12));
}

void Metro::checkSurvival()
{
    if (!m_survival.active() || m_sandbox || m_mission.active())
        return;
    // appelé après la clôture du mois : month() est déjà le mois suivant
    m_survival.redMonths = m_money < 0 ? m_survival.redMonths + 1 : 0;
    const double need = angerThreshold();
    m_survival.angryMonths = m_survival.anger && need > 0 && m_satisfaction < need ? m_survival.angryMonths + 1 : 0;
    if (m_survival.redMonths >= RedMonthsMax) {
        m_survival.status = 2;
        m_survival.reason = Survival::Bankruptcy;
    } else if (m_survival.angryMonths >= AngryMonthsMax) {
        m_survival.status = 2;
        m_survival.reason = Survival::Anger;
    } else if (survivalMonthsLeft() <= 0) {
        m_survival.status = 1;
        m_survival.stars = m_satisfaction >= 0.5 ? 3 : m_satisfaction >= 0.3 ? 2 : 1;
    } else {
        if (m_survival.redMonths > 0)
            emit message(tr("Trésorerie négative : faillite dans %n mois si rien ne change", nullptr,
                            RedMonthsMax - m_survival.redMonths));
        if (m_survival.angryMonths > 0)
            emit message(tr("La ville est mécontente (%1 % de la demande captée, %2 % exigés) : "
                            "la mairie reprend le réseau dans %n mois", nullptr, AngryMonthsMax - m_survival.angryMonths)
                             .arg(qRound(m_satisfaction * 100))
                             .arg(qRound(need * 100)));
        emit missionChanged();
        return;
    }
    emit missionChanged();
    emit survivalFinished(m_survival.status == 1, m_survival.status == 1 ? m_survival.stars : m_survival.reason);
}

QJsonObject Metro::survivalJson() const
{
    if (m_survival.years <= 0)
        return {};
    return QJsonObject{{"years", m_survival.years},     {"anger", m_survival.anger},
                       {"start", m_survival.startMonth}, {"red", m_survival.redMonths},
                       {"angry", m_survival.angryMonths}, {"status", m_survival.status},
                       {"reason", m_survival.reason},    {"stars", m_survival.stars}};
}

void Metro::loadSurvival(const QJsonObject &o)
{
    m_survival = Survival();
    if (o.isEmpty())
        return;
    m_survival.years = o.value("years").toInt();
    m_survival.anger = o.value("anger").toBool();
    m_survival.startMonth = o.value("start").toInt(1);
    m_survival.redMonths = o.value("red").toInt();
    m_survival.angryMonths = o.value("angry").toInt();
    m_survival.status = o.value("status").toInt();
    m_survival.reason = o.value("reason").toInt();
    m_survival.stars = o.value("stars").toInt();
}
