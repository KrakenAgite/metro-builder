#pragma once

#include "CityData.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

class Metro;
class MapView;
class OsmLoader;
class Card;
class KpiTile;
class StatChip;
class LineBadge;
class ChartWidget;
class LineEconomicsWidget;
class StopDelegate;
class Toast;
class QLineEdit;
class QComboBox;
class QPushButton;
class QToolButton;
class QButtonGroup;
class QListWidget;
class QLabel;
class QSlider;
class QProgressBar;
class QHBoxLayout;
class QVBoxLayout;
struct GameEvent;
struct Objective;
class QWidget;
class QAction;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path); // partie .metro passée au lancement (double-clic sur une sauvegarde)

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *e) override;

private:
    Card *buildCityCard();
    Card *buildStatsCard();
    Card *buildDock();
    Card *buildLineCard();
    Card *buildStationCard();
    Card *buildFinanceCard();
    Card *buildGoalsCard();
    Card *buildRouteCard();
    void showRoute();
    void exportPlan();
    void exportMapImage();
    void enterSandbox();
    void refreshUndo();
    void refreshGoals();
    void onGoalCompleted(const Objective &goal);
    QString bestScoreKey() const;
    void buildEventCards();
    void onEventStarted(const GameEvent &e);
    void showNews(const GameEvent &e);
    void showDecision(const GameEvent &e);
    void refreshEvents();
    void refreshFinance();
    void buildShortcuts();
    void layoutOverlays();

    void loadCity();
    void setLoading(bool loading, const QString &message = {}, bool error = false);
    void onCityLoaded(QSharedPointer<CityData> city);
    void onCityExtended(QSharedPointer<CityData> city);
    void extendMap(int side);
    void tick();

    void refreshAll();
    void refreshStats();
    void refreshLines();
    void refreshLineEditor();
    void refreshStation();
    void applyStopOrder();
    void selectLine(int lineId);
    void selectStation(int stationId);
    void setTool(int tool);
    void newLine(bool letter = false);
    void showColorPicker();
    void showCodePicker();

    void saveGame();
    void saveGameAs();
    void loadGame();
    void openGame(const QString &path);
    QJsonObject gameState() const;
    bool writeGame(const QString &path, bool quiet);
    void applyPendingView();
    QString autosaveDir() const;
    QString autosavePath() const;
    QString latestAutosave() const;
    void setAutosaveMinutes(int minutes);
    void autosave();
    void refreshResume();
    void showHelp();

    Metro *m_metro;
    MapView *m_map;
    OsmLoader *m_loader;
    QWidget *m_root;
    Toast *m_toast;

    // carte « ville »
    Card *m_cityCard;
    QWidget *m_welcome;
    QLineEdit *m_cityEdit;
    QComboBox *m_radiusCombo;
    QPushButton *m_loadBtn;
    QProgressBar *m_busy;
    QLabel *m_cityStatus;

    // indicateurs
    Card *m_statsCard;
    StatChip *m_moneyChip, *m_dateChip, *m_ridersChip, *m_captureChip, *m_coverChip;

    // dock du bas
    Card *m_dock;
    QButtonGroup *m_toolGroup, *m_overlayGroup, *m_speedGroup;
    QHBoxLayout *m_badgeLayout;
    QToolButton *m_themeBtn;
    QToolButton *m_schemaBtn;
    QToolButton *m_financeBtn;
    QToolButton *m_soundBtn;
    QWidget *m_badgeBox;

    // éditeur de ligne
    Card *m_lineCard;
    QWidget *m_lineContent;
    QToolButton *m_lineColorBtn;
    LineBadge *m_lineCodeBtn;
    QLineEdit *m_lineName;
    QListWidget *m_stopList;
    StopDelegate *m_stopDelegate;
    QLabel *m_stopsEmpty;
    QButtonGroup *m_wagonGroup;
    QLabel *m_trainCount;
    QToolButton *m_loopBtn;
    QButtonGroup *m_offPeakGroup;
    QLabel *m_stockLabel;
    QPushButton *m_renewBtn;
    KpiTile *m_kLength, *m_kCycle, *m_kHeadway, *m_kCapacity, *m_kRiders, *m_kLoad;
    QLabel *m_lineAdvice;

    // fiche station
    Card *m_stationCard;
    QWidget *m_stationContent;
    QLineEdit *m_stationName;
    QHBoxLayout *m_stationLines;
    KpiTile *m_kPop, *m_kJobs, *m_kDemand, *m_kServed, *m_kBoard;
    QPushButton *m_addToLineBtn;

    // finances
    Card *m_financeCard = nullptr;
    QWidget *m_financeContent;
    KpiTile *m_fMoney, *m_fRevenue, *m_fOperating, *m_fResult, *m_fInvested, *m_fTurnover, *m_fSubsidy, *m_fDebt;
    ChartWidget *m_cMoney, *m_cFlows, *m_cResult, *m_cInvest, *m_cRiders, *m_cCapture, *m_cScore, *m_cPoints,
        *m_cPopulation, *m_cDebt;
    QWidget *m_policyBox;
    QSlider *m_fareSlider;
    QLabel *m_fareLabel, *m_fareHint, *m_maintHint, *m_debtLabel;
    QButtonGroup *m_maintGroup;
    QPushButton *m_repayBtn;
    LineEconomicsWidget *m_lineEco;
    int m_financeRange = 24; // mois affichés (0 = toute la partie)

    // itinéraire
    Card *m_routeCard;
    QLabel *m_routeSummary, *m_routeDetail;
    QVBoxLayout *m_routeSteps;
    QPointF m_routeA, m_routeB;
    bool m_routeActive = false;

    // annuler / refaire, mode de jeu
    QAction *m_undoAction, *m_redoAction, *m_sandboxAction;
    QToolButton *m_modeCareer, *m_modeSandbox;
    bool m_newSandbox = false; // mode des nouvelles parties

    // score et objectifs
    Card *m_goalsCard;
    QToolButton *m_goalsBtn;
    QLabel *m_scoreValue, *m_scoreSub, *m_goalsFooter;
    struct GoalRow {
        QWidget *box;
        QLabel *title, *reward, *progress;
        QProgressBar *bar;
    };
    QVector<GoalRow> m_goalRows;
    double m_bestScore = 0;

    // événements
    Card *m_eventsCard, *m_newsCard, *m_decisionCard;
    QVBoxLayout *m_eventsList;
    QLabel *m_newsIcon, *m_newsTitle, *m_newsText;
    QLabel *m_decisionIcon, *m_decisionTitle, *m_decisionText;
    QHBoxLayout *m_decisionButtons;
    QTimer m_newsTimer;
    int m_decisionId = -1;
    int m_speedBeforeDecision = -1;

    QTimer m_timer;
    QTimer m_layoutTimer;
    QElapsedTimer m_clock;
    int m_frame = 0;
    double m_speed = 1;
    int m_currentLine = -1;
    int m_selectedStation = -1;
    bool m_updating = false;
    QString m_cityQuery;
    double m_cityRadius = 2;
    QJsonObject m_pendingGame;
    QRectF m_pendingArea; // zone agrandie d'une partie en cours de chargement
    QJsonObject m_pendingView; // vue, calque, vitesse… d'une partie en cours de chargement
    QString m_savePath;        // fichier de la partie (Ctrl+S)
    QTimer m_autosaveTimer;
    int m_autosaveMinutes = 2;
    QWidget *m_resumeBox;
    QLabel *m_resumeLabel;
    QString m_resumePath;
    QAction *m_resumeAction;
};
