#include "Achievements.h"
#include "Metro.h"

#include <QSettings>

Achievements::Achievements(QObject *parent)
    : QObject(parent)
{
}

const QVector<Achievements::Def> &Achievements::all()
{
    static const QVector<Def> defs = {
        {"first_station", tr("Premier coup de pioche"), tr("Construire une station")},
        {"first_line", tr("Inauguration"), tr("Mettre une ligne en service")},
        {"loop", tr("Tourner en rond"), tr("Ouvrir une ligne circulaire d'au moins 5 stations")},
        {"hub", tr("Plaque tournante"), tr("Desservir une station par 4 lignes")},
        {"lines10", tr("Réseau étoilé"), tr("Exploiter 10 lignes")},
        {"stations50", tr("Maillage serré"), tr("Construire 50 stations")},
        {"riders10k", tr("Heure de pointe"), tr("Transporter 10 000 voyageurs/h")},
        {"capture50", tr("Métro, boulot, dodo"), tr("Capter 50 % de la demande")},
        {"coverage90", tr("Personne n'est oublié"), tr("Desservir 90 % des habitants")},
        {"rich", tr("Coffre-fort"), tr("Atteindre 2 000 M€ de trésorerie")},
        {"profit12", tr("Bon gestionnaire"), tr("Enchaîner 12 mois bénéficiaires")},
        {"debtfree", tr("Libéré de la dette"), tr("Rembourser entièrement ses emprunts")},
        {"decade", tr("Vétéran"), tr("Jouer 10 ans sur la même partie")},
        {"growth", tr("Ville nouvelle"), tr("Voir la population grandir de 25 %")},
        {"renew", tr("Comme neuf"), tr("Renouveler le matériel d'une ligne")},
        {"route", tr("Plus vite qu'à pied"), tr("Trouver un itinéraire en métro deux fois plus rapide que la marche")},
        {"export", tr("Cartographe"), tr("Exporter le plan du réseau")},
        {"sandbox", tr("Bac à sable"), tr("Jouer une partie en bac à sable")},
        {"real", tr("Le vrai métro"), tr("Importer le métro réel d'une ville")},
        {"scenario", tr("Mission accomplie"), tr("Réussir un scénario")},
        {"scenario3", tr("Perfectionniste"), tr("Réussir un scénario avec 3 étoiles")},
        {"allcapitals", tr("Tour du monde"), tr("Réussir les 6 scénarios")},
    };
    return defs;
}

bool Achievements::has(const QString &id) const
{
    return QSettings().contains(QStringLiteral("achievements/%1").arg(id));
}

QDateTime Achievements::when(const QString &id) const
{
    return QSettings().value(QStringLiteral("achievements/%1").arg(id)).toDateTime();
}

int Achievements::count() const
{
    int n = 0;
    for (const Def &d : all())
        n += has(d.id);
    return n;
}

void Achievements::unlock(const QString &id)
{
    if (has(id))
        return;
    for (const Def &d : all())
        if (d.id == id) {
            QSettings().setValue(QStringLiteral("achievements/%1").arg(id), QDateTime::currentDateTime());
            emit unlocked(d);
            return;
        }
}

void Achievements::check(const Metro &m)
{
    if (!m.city())
        return;
    if (m.sandbox()) { // pas de succès de gestion en bac à sable
        unlock("sandbox");
        return;
    }
    int activeLines = 0, maxLines = 0;
    bool loop = false;
    for (const Line &l : m.lines())
        if (l.segmentCount() > 0) {
            ++activeLines;
            loop |= l.isLoop() && l.stops.size() >= 5;
        }
    for (const Station &s : m.stations())
        maxLines = std::max(maxLines, s.lineCount);
    const double residents = m.population();
    if (!m.stations().isEmpty())
        unlock("first_station");
    if (activeLines >= 1)
        unlock("first_line");
    if (loop)
        unlock("loop");
    if (maxLines >= 4)
        unlock("hub");
    if (activeLines >= 10)
        unlock("lines10");
    if (m.stations().size() >= 50)
        unlock("stations50");
    if (m.totalServed() >= 10000)
        unlock("riders10k");
    if (m.satisfaction() >= 0.5)
        unlock("capture50");
    if (residents > 0 && m.coveredResidents() / residents >= 0.9)
        unlock("coverage90");
    if (m.money() >= 2000)
        unlock("rich");
    int streak = 0;
    for (int i = m.history().size() - 1; i >= 0; --i) {
        const MonthRecord &r = m.history()[i];
        if (r.revenue <= r.operating || r.revenue <= 0)
            break;
        ++streak;
    }
    if (streak >= 12)
        unlock("profit12");
    if (m.month() > 120)
        unlock("decade");
    if (m.initialPopulation() > 0 && m.population() >= 1.25 * m.initialPopulation())
        unlock("growth");
}
