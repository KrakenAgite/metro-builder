#pragma once

#include "CityData.h"
#include "TransitImport.h"
#include <QColor>
#include <QHash>
#include <QPolygonF>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace Rules {
constexpr double WalkFull = 300;          // m : desserte totale autour d'une station
constexpr double WalkMax = 800;           // m : au-delà, plus aucune desserte
constexpr double TrainSpeed = 600;        // m/min (~36 km/h en vitesse commerciale)
constexpr double AccelPenalty = 0.4;      // min perdues par inter-station (accélération/freinage)
constexpr double Dwell = 0.5;             // min d'arrêt en station
constexpr double Turnaround = 1.5;        // min de retournement au terminus
constexpr double TransferPenalty = 3;     // min de correspondance (marche dans la station)
constexpr double WagonCapacity = 140;     // voyageurs par voiture
constexpr int MinWagons = 3;
constexpr int MaxWagons = 5;
constexpr double TripRateResident = 0.12; // déplacements / habitant à l'heure de pointe
constexpr double TripRateJob = 0.05;      // déplacements / emploi à l'heure de pointe
constexpr double GravityLength = 3000;    // m : portée moyenne des déplacements
constexpr double MaxModeShare = 0.7; 
constexpr double CaptureTarget = 0.15;   // part de la demande captée considérée comme « bien desservie »
     // part modale max du métro
constexpr double GridCell = 100;          // m : taille des cellules de demande

constexpr double StartMoney = 600;        // M€
constexpr double StationCost = 25;        // M€
constexpr double TrackCostKm = 30;        // M€ / km de tunnel
constexpr double WagonCost = 2;           // M€ / voiture
constexpr double Refund = 0.5;            // part remboursée lors d'une démolition
constexpr double Fare = 1.9;              // € par voyage
constexpr double DailyFactor = 12;        // voyages/jour = voyages heure de pointe × facteur
constexpr double RevenueBoost = 4;        // recettes ×4 : compense le mois de 4 semaines (40 s au lieu de 10 s)
constexpr double WagonDailyCost = 2000;   // €/jour
constexpr double StationDailyCost = 1500; // €/jour
constexpr double DaysPerMonth = 30;
constexpr double PeakHours = 4;           // heures-équivalent de pointe par jour
constexpr double OffPeakHours = 8;        // heures-équivalent creuses (4 + 8 = 12 = DailyFactor)
constexpr double FareElasticity = 0.55;   // baisse de la fréquentation par euro au-dessus du tarif de base
constexpr double LoanRate = 0.04;         // taux annuel des emprunts
constexpr int LoanMonths = 120;           // durée de remboursement (10 ans)
constexpr double MaxDebt = 1000;          // M€ d'encours maximal
constexpr double SubsidyShare = 0.4;      // part des coûts d'exploitation que la ville peut subventionner
constexpr double RenewShare = 0.6;        // coût du renouvellement du matériel / prix neuf
constexpr double CityGrowthCap = 2.5;     // densité maximale atteinte par la croissance urbaine
constexpr double ViaductFactor = 0.55;    // coût d'un viaduc / tunnel
constexpr double BridgeFactor = 0.9;      // viaduc au-dessus de l'eau (pont)
constexpr double UnderwaterFactor = 1.6;  // tunnel sous un fleuve : plus profond, plus cher
constexpr double TunnelDepth = 12;        // m sous la surface
constexpr double UnderwaterDepth = 26;    // m sous la surface (sous le lit du fleuve)
constexpr double ViaductHeight = 9;       // m au-dessus du sol
constexpr double SecondsPerWeek = 10;     // secondes réelles par semaine de jeu à vitesse ×1
constexpr int WeeksPerMonth = 4;
constexpr double SecondsPerMonth = SecondsPerWeek * WeeksPerMonth;
} // namespace Rules

struct Station {
    int id = 0;
    QString name;
    QPointF pos;
    // calculé
    int lineCount = 0;
    double catchPop = 0, catchJobs = 0;
    double potential = 0;  // déplacements/h générés par la zone desservie
    double served = 0;     // déplacements/h réellement captés par le métro
    double boardings = 0;  // montées/h (départs + correspondances)
};

// Un tronçon du cycle d'un train (trajet entre 2 stations ou arrêt en station)
struct Leg {
    QPointF a, b;
    double t0 = 0, dur = 0;
    int seg = -1;  // index de l'inter-station, -1 pour un arrêt
    int dir = 0;   // 0 = aller, 1 = retour
    double angle = 0;
    int refSeg = 0, refDir = 0; // inter-station utilisé pour la charge affichée
};

// Tracé échantillonné d'un inter-station (courbe passant par les points de passage)
struct SegPath {
    QPolygonF pts;
    QVector<double> cum;  // abscisse curviligne de chaque échantillon
    QVector<int> knots;   // index d'échantillon de chaque point de contrôle (début, points de passage, fin)
    double length() const { return cum.isEmpty() ? 0 : cum.last(); }
    QPointF pointAt(double s, double *angleDeg = nullptr) const;
};

// Résultat d'un test de clic sur un tracé ou un point de passage
struct TrackHit {
    int lineId = -1;
    int seg = -1;
    int index = -1; // point de passage touché, ou position d'insertion sur le tracé
    QPointF pos;
    bool valid() const { return lineId >= 0; }
};

struct Line {
    int id = 0;
    QString code; // affiché sur la pastille : chiffre (1, 2…) ou lettre (A, B…)
    QString name;
    QColor color;
    QVector<int> stops; // ids de stations, dans l'ordre de parcours
    int wagons = 4;
    int trains = 2;
    bool loop = false;
    double paidLength = 0; // longueur de tunnel équivalente déjà payée (m) ; viaducs et passages sous l'eau pondérés
    QSet<quint64> elevated; // inter-stations en viaduc (clé : ids triés), les autres sont en tunnel
    double offPeak = 1;   // part des rames en service aux heures creuses (1 ; 0,75 ; 0,5)
    double stockAge = 0;  // âge moyen du matériel roulant (mois)
    // points de passage par paire de stations (clé : ids triés), stockés du plus petit id vers le plus grand
    QHash<quint64, QVector<QPointF>> via;
    // calculé
    double length = 0;       // m
    double cycleMin = 0;     // durée d'un aller-retour (ou d'un tour)
    double headwayMin = 0;   // intervalle entre deux trains
    double capacity = 0;     // voyageurs/h/sens
    double ridership = 0;    // voyageurs/h utilisant la ligne
    double maxLoad = 0;      // charge max sur un inter-station (voyageurs/h/sens)
    QVector<double> load[2]; // charge par inter-station, par sens
    QVector<Leg> legs;
    QVector<SegPath> paths;  // un tracé par inter-station, dans le sens de la ligne

    bool isLoop() const { return loop && stops.size() >= 3; }
    int segmentCount() const { return stops.size() < 2 ? 0 : (isLoop() ? stops.size() : stops.size() - 1); }
    double loadRatio() const { return capacity > 0 ? maxLoad / capacity : (maxLoad > 0 ? 9 : 0); }
};

struct TrainVis {
    QVector<QPointF> wagonPos;   // centre de chaque voiture (les voitures suivent la courbe)
    QVector<double> wagonAngle;  // degrés
    int wagons = 4;
    QColor color;
    double loadRatio = 0;
    int lineId = -1, seg = -1; // inter-station courant
    double t = 0;              // position sur l'inter-station (0 = arrêt k, 1 = arrêt k+1)
};

// Événements aléatoires (tirés à chaque fin de mois)
enum class EventKind {
    Strike,           // grève : ligne à l'arrêt
    Breakdown,        // panne de signalisation : capacité réduite
    BigEvent,         // concert, match… : demande locale accrue
    Subsidy,          // subvention : recette exceptionnelle
    EnergyPrice,      // flambée de l'énergie : exploitation plus chère
    ConstructionDeal, // rabais des entreprises : construction moins chère
    MaterialShortage, // pénurie : construction plus chère
    Flood,            // inondation : station fermée
    Pollution,        // pic de pollution : demande accrue
    Development,      // nouveau quartier : habitants / emplois en plus (permanent)
    Sponsor,          // mécénat : argent contre le nom d'une station
    BadPress,         // usagers en colère : demande réduite
    TrainFailure,     // panne d'une rame vieillissante : capacité réduite
};

struct GameEvent {
    int id = 0;
    EventKind kind = EventKind::Subsidy;
    QString title, text;
    int tone = 0;          // 1 favorable, -1 défavorable
    int lineId = -1, stationId = -1;
    QPointF pos;           // lieu (marqueur sur la carte) ; nul si sans lieu
    double radius = 0;     // m
    double value = 1;      // multiplicateur, ou montant en M€
    double cost = 0;       // montant de l'option proposée (M€)
    QString extra;         // nom du mécène…
    int weeksLeft = 0;     // durée restante en semaines (0 = ponctuel)
    int startMonth = 0;
    QStringList choices;   // vide = simple information
    bool pending = false;  // en attente d'une décision du joueur
};

// Quartier ajouté par un événement : habitants / emplois supplémentaires, permanents
struct Development {
    QPointF pos;
    double radius = 400;
    double pop = 0, jobs = 0;
    QString name;
};

// Objectifs : trois à la fois, par paliers de difficulté croissante ; chacun rapporte
// des points de score et une prime. Le score progresse aussi chaque mois avec la fréquentation.
enum class GoalKind {
    Stations,     // stations construites
    Lines,        // lignes en service (2 arrêts ou plus)
    Riders,       // voyageurs/h en pointe
    Capture,      // part de la demande captée (%)
    Coverage,     // habitants à distance de marche (%)
    Transfers,    // stations de correspondance
    Network,      // longueur cumulée des lignes (km)
    Money,        // trésorerie (M€)
    ProfitStreak, // mois bénéficiaires d'affilée
    Turnover,     // chiffre d'affaires cumulé (M€)
    Count
};

struct Objective {
    GoalKind kind = GoalKind::Stations;
    int level = 0;       // palier (0 = premier)
    double target = 0;
    double value = 0;    // valeur actuelle
    int points = 0;      // points de score à la clé
    double reward = 0;   // prime en M€
    QString title;
    double progress() const { return target > 0 ? std::clamp(value / target, 0.0, 1.0) : 0; }
};

// Itinéraire calculé : étapes à pied, en métro et correspondances
struct RouteStep {
    enum Kind { Walk, Ride, Transfer } kind = Walk;
    int lineId = -1;
    int fromStation = -1, toStation = -1; // ids (-1 = point de départ / d'arrivée)
    int stops = 0;          // arrêts parcourus (métro)
    QString towards;        // direction (terminus)
    double minutes = 0;
    double meters = 0;      // distance à pied
};

struct Route {
    bool valid = false;     // départ et arrivée renseignés
    bool byMetro = false;   // false : il vaut mieux marcher
    QPointF from, to;
    double minutes = 0;     // durée totale du meilleur trajet
    double walkMinutes = 0; // durée à pied directe
    QVector<RouteStep> steps;
    QVector<QPair<QColor, QPolygonF>> drawing; // tracé à surligner (couleur, polyligne monde) ; gris = marche
};

// Scénarios : une mission sur une grande ville, des conditions de victoire et une échéance
struct MissionGoal {
    enum Type { Metric, RidersShare, NoSaturation } type = Metric;
    GoalKind kind = GoalKind::Riders; // Metric
    double target = 0;   // Metric : valeur à atteindre ; RidersShare : part de la demande (fixée au départ)
    double value = 0;    // valeur actuelle
    QString title;
    bool done() const { return type == NoSaturation ? value > 0.5 : value + 1e-9 >= target; }
};

struct ScenarioDef {
    QString id, city, cityLabel, title, description;
    double radiusKm = 2;
    double money = 600;
    bool realNetwork = false; // démarre avec le métro réel de la ville (OpenStreetMap)
    int months = 48;
    int difficulty = 1;       // 1 à 3
    QVector<MissionGoal> goals;
};

struct Mission {
    QString id; // vide : partie libre
    QString title, cityLabel;
    int startMonth = 1, deadline = 0; // dernier mois inclus
    QVector<MissionGoal> goals;
    int status = 0; // 0 en cours, 1 réussie, 2 échouée
    int stars = 0;
    bool active() const { return !id.isEmpty() && status == 0; }
};

// Bilan d'un mois de jeu (montants en M€)
struct MonthRecord {
    int month = 0;
    double money = 0;      // trésorerie en fin de mois
    double revenue = 0;    // recettes voyageurs
    double operating = 0;  // exploitation (rames, stations)
    double investment = 0; // construction nette (stations, tunnels, matériel) moins reventes
    double riders = 0;     // voyageurs/h en pointe en fin de mois
    double capture = 0;    // part de la demande captée (0..1)
    double score = 0;      // score en fin de mois
    double monthPoints = 0; // points de fin de mois (fréquentation, demande captée, rentabilité)
    double goalPoints = 0;  // points des objectifs atteints dans le mois
    double subsidy = 0;     // subvention de la ville (M€)
    double loanPaid = 0;    // remboursement d'emprunts (M€)
    double debt = 0;        // encours restant en fin de mois (M€)
    double population = 0;  // habitants de la zone de jeu
    double result() const { return revenue - operating - investment; }
};

struct DemandGrid {
    QPointF origin;
    double cell = Rules::GridCell;
    int w = 0, h = 0;
    QVector<double> pop, jobs;
    QVector<double> potential;  // déplacements/h générés
    QVector<double> servedFrac; // 0..1 part de la demande captée
    QVector<double> coverage;   // 0..1 accessibilité à pied d'une station active
    QPointF center(int i) const { return origin + QPointF((i % w + 0.5) * cell, (i / w + 0.5) * cell); }
};

class Metro : public QObject
{
    Q_OBJECT
public:
    explicit Metro(QObject *parent = nullptr);

    void setCity(QSharedPointer<CityData> city);
    void updateCity(QSharedPointer<CityData> city); // zone agrandie : garde le réseau
    QSharedPointer<CityData> city() const { return m_city; }
    void reset();

    // Stations
    int addStation(const QPointF &pos);
    void moveStation(int id, const QPointF &pos, bool commit);
    void removeStation(int id);
    void renameStation(int id, const QString &name);
    const Station *station(int id) const;
    const QVector<Station> &stations() const { return m_stations; }
    int stationAt(const QPointF &world, double radius) const;

    // Lignes
    int addLine(const QString &code = {}); // code vide = prochain numéro libre
    void removeLine(int id);
    const Line *line(int id) const;
    const QVector<Line> &lines() const { return m_lines; }
    bool addStop(int lineId, int stationId, bool atFront = false);
    void removeStop(int lineId, int stationId);
    void setStops(int lineId, const QVector<int> &stops);
    void moveStop(int lineId, int from, int to);
    void reverseLine(int lineId);
    void setWagons(int lineId, int wagons);
    void setTrains(int lineId, int trains);
    void setLoop(int lineId, bool loop);
    void setLineName(int lineId, const QString &name);
    void setLineColor(int lineId, const QColor &color);
    void setLineCode(int lineId, const QString &code);
    bool codeUsed(const QString &code, int exceptLine = -1) const;
    QString nextFreeCode(bool letter) const;
    static const QVector<QColor> &presetColors();

    // Points de passage (courbent le tracé entre deux stations)
    QVector<QPointF> waypoints(int lineId, int seg) const;
    bool insertWaypoint(int lineId, int seg, int index, const QPointF &pos); // à valider par moveWaypoint(commit)
    void moveWaypoint(int lineId, int seg, int index, const QPointF &pos, bool commit);
    void removeWaypoint(int lineId, int seg, int index);
    TrackHit waypointAt(const QPointF &world, double radius, int preferLine = -1) const;
    TrackHit trackAt(const QPointF &world, double radius, int preferLine = -1) const;

    // Simulation
    void recompute();
    void advance(double realSeconds, double speed);
    QVector<TrainVis> trains(double wagonSpacing) const; // espacement des voitures en mètres
    const DemandGrid &grid() const { return m_grid; }
    static double walkWeight(double d);
    QString nearestStreet(const QPointF &p, double maxDist) const;
    QPointF snapToRoad(const QPointF &p, double maxDist) const;

    // Statistiques
    double money() const { return m_money; }
    int month() const { return int(m_financeSeconds / Rules::SecondsPerMonth) + 1; }
    int week() const { return int(m_financeSeconds / Rules::SecondsPerWeek) % Rules::WeeksPerMonth + 1; } // 1..4
    double totalPotential() const { return m_totalPotential; }
    double totalServed() const { return m_totalServed; }
    double satisfaction() const { return m_satisfaction; }
    double coveredResidents() const { return m_coveredResidents; }
    double monthlyRevenue() const { return m_monthlyRevenue; } // €
    double monthlyCost() const { return m_monthlyCost; }       // €
    double simMinutes() const { return m_simMinutes; }
    // horloge de la journée (minutes depuis minuit) : la partie commence à 7 h, 1 s réelle = 1 min à ×1
    double clockMinutes() const { return std::fmod(m_simMinutes + 7 * 60, 1440.0); }
    // part des rames en circulation à cette heure : pointe, heures creuses, fermeture de nuit (1 h – 5 h)
    double serviceAt(double clockMinutes, const Line &l) const;
    // fermeture de nuit (1 h – 5 h) : seulement avec le cycle jour / nuit
    void setNightClosure(bool on) { m_nightClosure = on; }
    bool closedNow() const { return m_nightClosure && clockMinutes() >= 60 && clockMinutes() < 300; }
    const QVector<MonthRecord> &history() const { return m_history; } // mois clos
    MonthRecord currentMonth() const;                                 // mois en cours (partiel)
    double totalInvested() const { return m_totalInvested; }
    double totalRevenue() const { return m_totalRevenue; }     // chiffre d'affaires cumulé (M€)
    double totalOperating() const { return m_totalOperating; } // coûts d'exploitation cumulés (M€)
    double lineMonthlyRevenue(const Line &l) const; // €
    double lineMonthlyCost(const Line &l) const;    // €

    // Score et objectifs
    double score() const { return m_score; }
    int monthPoints() const; // points gagnés en fin de mois au rythme actuel
    int lastMonthPoints() const { return m_lastMonthPoints; }
    int goalsCompleted() const { return m_goalsDone; }
    const QVector<Objective> &objectives() const { return m_goals; }
    static QString goalProgressText(const Objective &o); // « 2 / 3 stations »

    // Événements
    const QVector<GameEvent> &activeEvents() const { return m_events; }
    const QVector<Development> &developments() const { return m_developments; }
    void resolveEvent(int id, int choice);
    bool triggerEvent(EventKind kind); // déclenche un événement précis (si applicable)
    double lineCapacityFactor(int lineId) const;
    bool stationClosed(int stationId) const;
    double buildCostFactor() const;
    double opCostFactor() const;

    QJsonObject save() const;
    bool load(const QJsonObject &o);

    // Vue en coupe : tunnel ou viaduc par inter-station, passages sous les fleuves
    struct ProfilePoint {
        double s = 0;        // distance depuis le premier arrêt (m)
        double level = 0;    // altitude de la voie par rapport au sol (m, négatif = sous terre)
        bool water = false;  // au-dessus / au-dessous d'un cours d'eau
        bool elevated = false;
        int seg = 0;
    };
    bool segmentElevated(const Line &l, int seg) const;
    void setSegmentElevated(int lineId, int seg, bool on);
    bool isWater(const QPointF &p) const;
    QVector<ProfilePoint> lineProfile(int lineId) const;
    double trackUnits(const Line &l) const; // longueur équivalente tunnel (m) qui fixe le coût du tracé
    double segmentCost(const Line &l, int seg, bool elevated) const; // M€

    // Politique tarifaire, financement, entretien
    double fare() const { return m_fare; }
    void setFare(double euros);
    double fareDemandFactor() const; // effet du tarif sur la fréquentation
    int maintenance() const { return m_maintenance; } // 0 réduit, 1 normal, 2 renforcé
    void setMaintenance(int level);
    double debt() const;          // M€ restant dus
    double loanPayment() const;   // M€ remboursés par mois
    bool takeLoan(double amount); // M€
    bool repayLoans();
    double monthlySubsidy() const { return m_monthlySubsidy; } // €
    double population() const { return m_population; }
    // Exploitation des lignes
    void setOffPeak(int lineId, double ratio);
    double failureChance(const Line &l) const; // risque mensuel de panne d'une rame
    double renewCost(const Line &l) const;     // M€
    bool renewStock(int lineId);

    // Scénarios et import du réseau réel
    static const QVector<ScenarioDef> &scenarios();
    static const ScenarioDef *scenario(const QString &id);
    void startScenario(const ScenarioDef &def);
    const Mission &mission() const { return m_mission; }
    int monthsLeft() const { return m_mission.deadline - month() + 1; }
    void importNetwork(const QVector<ImportedLine> &lines); // remplace le réseau, sans frais
    double initialPopulation() const { return m_initialPopulation; }

    // Annuler / refaire : chaque modification du réseau est mémorisée ; annuler rembourse la construction
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool undo();
    bool redo();

    // Bac à sable : construction gratuite, ni événements ni score
    bool sandbox() const { return m_sandbox; }
    void setSandbox(bool on);

    // Itinéraire entre deux points de la ville (marche + métro)
    Route route(const QPointF &from, const QPointF &to) const;

signals:
    void networkChanged();
    void message(const QString &text);
    void eventStarted(const GameEvent &event);
    void eventsChanged();
    void notice(const QString &text); // information neutre (fin d'un événement…)
    void goalCompleted(const Objective &goal);
    void goalsChanged();
    void undoChanged();
    void missionChanged();
    void missionFinished(bool won, int stars);

private:
    Station *stationMut(int id);
    Line *lineMut(int id);
    int stationIndex(int id) const;
    bool spend(double amount, const QString &what);
    void charge(double amount); // dépense (>0) ou remboursement (<0) d'investissement
    void income(double amount);  // recette exceptionnelle (M€)
    void expense(double amount); // dépense d'exploitation exceptionnelle (M€)
    void tickEvents();
    void rollEvents();
    bool startEvent(EventKind kind);
    double demandFactorAt(const QPointF &p) const;
    double globalDemandFactor() const;
    void applyDevelopments();
    QJsonArray eventsJson() const;
    void loadEvents(const QJsonObject &o);
    void closeMonth();
    void monthlyEconomy(); // fin de mois : emprunts, vieillissement, croissance urbaine
    void buildWaterIndex();
    double segmentUnits(const SegPath &path, bool elevated) const;
    void checkMission();
    QJsonObject missionJson() const;
    void loadMission(const QJsonObject &o);
    void growCity();
    void applyGrowth();
    double maintenanceCostFactor() const;
    QJsonObject economyJson() const;
    void loadEconomy(const QJsonObject &o);
    QJsonObject networkJson() const;
    void loadNetwork(const QJsonObject &o);
    void trackEdit();  // après une modification : mémorise l'état précédent pour « annuler »
    void resetUndo();
    void restoreState(const QJsonObject &net);
    void checkGoals();
    Objective makeGoal(GoalKind kind, int level) const;
    double goalValue(GoalKind kind) const;
    bool spawnGoal();
    QJsonObject goalsJson() const;
    void loadGoals(const QJsonObject &o);
    void settleTrack(Line &l);
    double lineLength(const Line &l) const;
    void buildGrid();
    void computeLineGeometry(Line &l);
    QVector<SegPath> buildPaths(const Line &l) const;
    QVector<QPointF> segmentVia(const Line &l, int seg) const;
    void setSegmentVia(Line &l, int seg, QVector<QPointF> pts);
    void detachStop(Line &l, int stationId);

    QSharedPointer<CityData> m_city;
    QVector<Station> m_stations;
    QVector<Line> m_lines;
    DemandGrid m_grid;
    int m_nextStationId = 1;
    int m_nextLineId = 1;

    double m_money = Rules::StartMoney;
    double m_simMinutes = 0;
    double m_financeSeconds = 0;
    double m_totalPotential = 0, m_totalServed = 0, m_satisfaction = 0, m_coveredResidents = 0;
    double m_monthlyRevenue = 0, m_monthlyCost = 0;
    QVector<MonthRecord> m_history;
    MonthRecord m_current;
    double m_totalInvested = 0, m_totalRevenue = 0, m_totalOperating = 0;
    QVector<GameEvent> m_events;
    QVector<Development> m_developments;
    int m_nextEventId = 1;
    int m_monthsSinceEvent = 0; // délai de calme entre deux événements
    bool m_lastEventBad = false;
    double m_score = 0;
    int m_lastMonthPoints = 0;
    int m_goalsDone = 0;
    int m_profitStreak = 0;    // mois bénéficiaires d'affilée
    int m_goalRotation = 0;    // prochaine famille d'objectifs proposée
    int m_goalLevel[int(GoalKind::Count)] = {};
    QVector<Objective> m_goals;
    bool m_checkingGoals = false;
    bool m_sandbox = false;
    double m_fare = Rules::Fare;
    int m_maintenance = 1;
    struct Loan {
        double remaining = 0, payment = 0;
        int monthsLeft = 0;
    };
    QVector<Loan> m_loans;
    double m_monthlySubsidy = 0;
    double m_population = 0, m_popYearStart = 0;
    double m_baseGrowth = 1;            // croissance naturelle de toute la ville
    QHash<qint64, float> m_growth;      // croissance autour des stations, par cellule (clé : coordonnées /100 m)
    double m_initialPopulation = 0;
    Mission m_mission;
    bool m_freeBuild = false;
    bool m_nightClosure = true;
    QHash<qint64, double> m_denomCache; // attractivité des destinations vue de chaque position de station           // import du réseau réel : rien n'est facturé
    QVector<QPair<QRectF, QPolygonF>> m_waterIndex; // surfaces d'eau (boîte englobante, polygone)
    QVector<QPair<QRectF, QPolygonF>> m_riverIndex; // cours d'eau linéaires
    struct UndoState {
        QJsonObject net;
        double invested = 0;
    };
    QVector<UndoState> m_undo, m_redo;
    UndoState m_lastState;
    bool m_restoring = false;
};
