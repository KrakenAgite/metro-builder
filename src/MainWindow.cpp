#include "MainWindow.h"
#include "MapView.h"
#include "Metro.h"
#include "OsmLoader.h"
#include "Ui.h"
#include "Charts.h"
#include "Audio.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QCloseEvent>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QSaveFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <functional>
#include <QWidgetAction>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString money(double m)
{
    return QStringLiteral("%1 M€").arg(QLocale(QLocale::French).toString(m, 'f', 1));
}

QString count(double v)
{
    return QLocale(QLocale::French).toString(qRound(v));
}

QString compact(double v)
{
    if (v >= 10000)
        return QStringLiteral("%1 k").arg(QLocale(QLocale::French).toString(v / 1000, 'f', 1));
    return count(v);
}

QIcon colorDot(const QColor &c)
{
    QPixmap pm(QSize(22, 22) * qApp->devicePixelRatio());
    pm.setDevicePixelRatio(qApp->devicePixelRatio());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 70), 2));
    p.setBrush(c);
    p.drawEllipse(QRectF(2, 2, 18, 18));
    return QIcon(pm);
}

// Curseur de volume intégré à un menu
QWidgetAction *volumeSlider(const QString &label, float value, std::function<void(float)> onChange, QObject *parent)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(12, 4, 12, 4);
    auto *text = new QLabel(label);
    text->setStyleSheet("color: #9AA0A6;");
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(qRound(value * 100));
    slider->setMinimumWidth(130);
    QObject::connect(slider, &QSlider::valueChanged, slider, [onChange](int v) { onChange(v / 100.f); });
    l->addWidget(text);
    l->addWidget(slider);
    auto *act = new QWidgetAction(parent);
    act->setDefaultWidget(w);
    return act;
}

QLabel *section(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setProperty("role", "section");
    return l;
}

void clearLayout(QLayout *lay)
{
    while (QLayoutItem *it = lay->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr); // sort tout de suite de l'arbre des widgets
            w->deleteLater();
        }
        delete it;
    }
}

const int kMargin = 16 - Card::Shadow; // marge visuelle de 16 px autour des panneaux

// Place une zone défilante dans la carte et renvoie le widget qui recevra le contenu.
QWidget *scrollableContent(Card *card)
{
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(Card::Shadow + 4, Card::Shadow + 2, Card::Shadow + 4, Card::Shadow + 6);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    scroll->viewport()->setAutoFillBackground(false);
    auto *content = new QWidget;
    content->setAutoFillBackground(false);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(10);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return content;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_metro(new Metro(this))
    , m_loader(new OsmLoader(this))
{
    setWindowTitle(tr("Metro Builder"));
    setWindowIcon(Icons::appIcon());
    qApp->setWindowIcon(Icons::appIcon());
    // réglages audio mémorisés
    {
        QSettings s;
        Audio &a = Audio::instance();
        a.setMusicEnabled(s.value("audio/music", true).toBool());
        a.setSfxEnabled(s.value("audio/sfx", true).toBool());
        a.setMusicVolume(s.value("audio/musicVolume", 0.5).toFloat());
        a.setSfxVolume(s.value("audio/sfxVolume", 0.7).toFloat());
        a.setMuted(s.value("audio/muted", false).toBool());
    }
    setMinimumSize(900, 520);

    m_root = new QWidget;
    m_root->installEventFilter(this);
    setCentralWidget(m_root);

    m_map = new MapView(m_metro, m_root);
    m_cityCard = buildCityCard();
    m_statsCard = buildStatsCard();
    m_dock = buildDock();
    m_lineCard = buildLineCard();
    m_stationCard = buildStationCard();
    m_financeCard = buildFinanceCard();
    m_goalsCard = buildGoalsCard();
    m_routeCard = buildRouteCard();
    buildEventCards();
    for (Card *c : {m_cityCard, m_statsCard, m_dock, m_lineCard, m_stationCard, m_financeCard, m_goalsCard,
                    m_routeCard, m_eventsCard, m_newsCard, m_decisionCard})
        c->setParent(m_root);
    m_statsCard->hide();
    m_dock->hide();
    m_lineCard->hide();
    m_stationCard->hide();
    m_financeCard->hide();
    m_goalsCard->hide();
    m_routeCard->hide();
    m_eventsCard->hide();
    m_newsCard->hide();
    m_decisionCard->hide();
    m_toast = new Toast(m_root);
    for (Card *c : {m_cityCard, m_statsCard, m_dock, m_lineCard, m_stationCard, m_financeCard, m_goalsCard,
                    m_routeCard, m_eventsCard, m_newsCard, m_decisionCard})
        c->ensurePolished();
    // un clic sur le budget ouvre les finances
    m_moneyChip->installEventFilter(this);
    m_moneyChip->setCursor(Qt::PointingHandCursor);
    m_layoutTimer.setSingleShot(true);
    connect(&m_layoutTimer, &QTimer::timeout, this, &MainWindow::layoutOverlays);
    buildShortcuts();

    connect(m_loader, &OsmLoader::progress, this, [this](const QString &msg) { setLoading(true, msg); });
    connect(m_loader, &OsmLoader::failed, this, [this](const QString &msg) {
        m_map->setExtendBusy(false);
        setLoading(false, msg, true);
        m_toast->show(msg, true);
    });
    connect(m_loader, &OsmLoader::loaded, this, &MainWindow::onCityLoaded);
    connect(m_loader, &OsmLoader::extended, this, &MainWindow::onCityExtended);

    connect(m_metro, &Metro::networkChanged, this, &MainWindow::refreshAll);
    connect(m_metro, &Metro::message, this, [this](const QString &msg) {
        m_toast->show(msg, true); // le message joue lui-même le bip d'erreur
    });
    connect(m_metro, &Metro::eventStarted, this, &MainWindow::onEventStarted);
    connect(m_metro, &Metro::notice, this, [this](const QString &msg) { m_toast->show(msg); });
    connect(m_metro, &Metro::eventsChanged, this, &MainWindow::refreshEvents);
    connect(m_metro, &Metro::goalCompleted, this, &MainWindow::onGoalCompleted);
    connect(m_metro, &Metro::goalsChanged, this, &MainWindow::refreshGoals);
    connect(m_map, &MapView::statusMessage, this, [this](const QString &msg) { m_toast->show(msg); });
    connect(m_map, &MapView::stationSelected, this, &MainWindow::selectStation);
    connect(m_map, &MapView::lineClicked, this, &MainWindow::selectLine);
    connect(m_map, &MapView::extendRequested, this, &MainWindow::extendMap);
    connect(m_map, &MapView::routeRequested, this, [this](const QPointF &a, const QPointF &b) {
        m_routeA = a;
        m_routeB = b;
        m_routeActive = true;
        showRoute();
    });
    connect(m_map, &MapView::routeCleared, this, [this] {
        m_routeActive = false;
        m_routeCard->hide();
        layoutOverlays();
    });
    connect(m_metro, &Metro::undoChanged, this, &MainWindow::refreshUndo);

    connect(&m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    if (m_autosaveMinutes > 0)
        m_autosaveTimer.start(m_autosaveMinutes * 60000);
    refreshResume();

    m_clock.start();
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start();

    refreshAll();
    resize(1500, 950);
}

// ---------------------------------------------------------------------------
// Construction des panneaux
// ---------------------------------------------------------------------------

Card *MainWindow::buildCityCard()
{
    auto *card = new Card(nullptr, 18);
    auto *lay = new QVBoxLayout(card);
    lay->setSpacing(12);

    // En-tête d'accueil (masqué une fois la ville chargée)
    m_welcome = new QWidget;
    auto *wl = new QHBoxLayout(m_welcome);
    wl->setContentsMargins(14, 14, 14, 0);
    wl->setSpacing(14);
    auto *logo = new QLabel;
    logo->setPixmap(Icons::pixmap(Icons::Logo, 52, Qt::white));
    wl->addWidget(logo, 0, Qt::AlignTop);
    auto *wt = new QVBoxLayout;
    wt->setSpacing(2);
    auto *title = new QLabel(tr("Metro Builder"));
    title->setProperty("role", "title");
    auto *subtitle = new QLabel(tr("Dessinez le métro d'une vraie ville à partir d'OpenStreetMap : "
                                   "placez les stations, tracez les lignes et répondez à la demande."));
    subtitle->setProperty("role", "subtitle");
    subtitle->setWordWrap(true);
    wt->addWidget(title);
    wt->addWidget(subtitle);
    // mode des nouvelles parties
    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 8, 0, 0);
    modeRow->setSpacing(4);
    auto *modeGroup = new QButtonGroup(this);
    m_modeCareer = new QToolButton;
    m_modeCareer->setText(tr("Carrière"));
    m_modeCareer->setToolTip(tr("Budget, événements, score et objectifs"));
    m_modeSandbox = new QToolButton;
    m_modeSandbox->setText(tr("Bac à sable"));
    m_modeSandbox->setToolTip(tr("Construction gratuite, sans événements ni score : dessinez le réseau de vos rêves"));
    m_newSandbox = QSettings().value("game/sandbox", false).toBool();
    for (QToolButton *b : {m_modeCareer, m_modeSandbox}) {
        b->setCheckable(true);
        b->setProperty("variant", "segment");
        b->setCursor(Qt::PointingHandCursor);
        modeGroup->addButton(b);
        modeRow->addWidget(b);
    }
    (m_newSandbox ? m_modeSandbox : m_modeCareer)->setChecked(true);
    connect(m_modeSandbox, &QToolButton::toggled, this, [this](bool on) {
        m_newSandbox = on;
        QSettings().setValue("game/sandbox", on);
    });
    modeRow->addStretch();
    wt->addLayout(modeRow);
    wl->addLayout(wt, 1);
    lay->addWidget(m_welcome);

    // Barre de recherche
    auto *search = new QWidget;
    search->setObjectName("searchBox");
    search->setAttribute(Qt::WA_StyledBackground, true);
    search->setStyleSheet("#searchBox { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.08);"
                          " border-radius: 12px; }");
    auto *sl = new QHBoxLayout(search);
    sl->setContentsMargins(10, 4, 4, 4);
    sl->setSpacing(6);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(Icons::Search, 18, Theme::TextDim));
    sl->addWidget(icon);
    m_cityEdit = new QLineEdit("Lyon");
    m_cityEdit->setObjectName("search");
    m_cityEdit->setPlaceholderText(tr("Rechercher une ville…"));
    m_cityEdit->setMinimumWidth(150);
    sl->addWidget(m_cityEdit, 1);
    m_radiusCombo = new QComboBox;
    for (double r : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0})
        m_radiusCombo->addItem(QStringLiteral("%1 km").arg(QLocale(QLocale::French).toString(r)), r);
    m_radiusCombo->setCurrentIndex(2);
    m_radiusCombo->setToolTip(tr("Demi-côté de la zone de jeu (au-delà de 4 km la carte devient plus lourde à afficher)"));
    sl->addWidget(m_radiusCombo);
    m_loadBtn = new QPushButton(Icons::icon(Icons::Download, Qt::white, Qt::white), tr("Charger"));
    m_loadBtn->setProperty("variant", "primary");
    m_loadBtn->setCursor(Qt::PointingHandCursor);
    sl->addWidget(m_loadBtn);
    connect(m_loadBtn, &QPushButton::clicked, this, &MainWindow::loadCity);
    connect(m_cityEdit, &QLineEdit::returnPressed, this, &MainWindow::loadCity);

    auto *menuBtn = iconButton(Icons::Menu, tr("Menu"), false, 36);
    auto *menu = new QMenu(menuBtn);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->addAction(Icons::icon(Icons::File), tr("Ouvrir un fichier Overpass JSON…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Fichier Overpass JSON"), {}, "JSON (*.json)");
        if (!path.isEmpty()) {
            m_cityQuery.clear();
            m_loader->loadFile(path);
        }
    });
    menu->addSeparator();
    m_undoAction = menu->addAction(Icons::icon(Icons::Undo), tr("Annuler"), QKeySequence::Undo, this, [this] {
        if (m_metro->city() && m_metro->undo()) {
            Audio::instance().play(Audio::Click);
            m_toast->show(tr("Modification annulée"), false, true);
        }
    });
    m_redoAction = menu->addAction(Icons::icon(Icons::Redo), tr("Rétablir"), this, [this] {
        if (m_metro->city() && m_metro->redo()) {
            Audio::instance().play(Audio::Click);
            m_toast->show(tr("Modification rétablie"), false, true);
        }
    });
    m_redoAction->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
    menu->addSeparator();
    menu->addAction(Icons::icon(Icons::Save), tr("Sauvegarder"), QKeySequence::Save, this,
                    &MainWindow::saveGame);
    menu->addAction(Icons::icon(Icons::Save), tr("Sauvegarder sous…"), QKeySequence("Ctrl+Shift+S"), this,
                    &MainWindow::saveGameAs);
    menu->addAction(Icons::icon(Icons::FolderOpen), tr("Charger une partie…"), QKeySequence::Open, this,
                    &MainWindow::loadGame);
    m_resumeAction = menu->addAction(Icons::icon(Icons::Play), tr("Reprendre la dernière partie"), this, [this] {
        const QString path = latestAutosave();
        if (!path.isEmpty())
            openGame(path);
    });
    QMenu *autoMenu = menu->addMenu(Icons::icon(Icons::Calendar), tr("Sauvegarde automatique"));
    autoMenu->setWindowFlags(autoMenu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    autoMenu->setAttribute(Qt::WA_TranslucentBackground);
    auto *autoGroup = new QActionGroup(this);
    m_autosaveMinutes = QSettings().value("autosave/minutes", 2).toInt();
    for (int minutes : {0, 1, 2, 5, 10}) {
        QAction *a = autoMenu->addAction(minutes == 0 ? tr("Désactivée")
                                         : minutes == 1 ? tr("Toutes les minutes")
                                                        : tr("Toutes les %1 minutes").arg(minutes));
        a->setCheckable(true);
        a->setChecked(minutes == m_autosaveMinutes);
        autoGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, minutes] { setAutosaveMinutes(minutes); });
    }
    menu->addSeparator();
    menu->addAction(Icons::icon(Icons::Image), tr("Exporter le plan du réseau (PNG, PDF)…"), QKeySequence("Ctrl+E"),
                    this, &MainWindow::exportPlan);
    menu->addAction(Icons::icon(Icons::Image), tr("Capture de la carte (PNG)…"), this, &MainWindow::exportMapImage);
    m_sandboxAction = menu->addAction(Icons::icon(Icons::Sandbox), tr("Passer cette partie en bac à sable"), this,
                                      &MainWindow::enterSandbox);
    menu->addSeparator();
    menu->addAction(Icons::icon(Icons::Recenter), tr("Recadrer la carte"), QKeySequence("F"), m_map,
                    &MapView::fitCity);
    QMenu *extend = menu->addMenu(Icons::icon(Icons::Plus), tr("Agrandir la carte"));
    extend->setWindowFlags(extend->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    extend->setAttribute(Qt::WA_TranslucentBackground);
    const QString sides[4] = {tr("Vers le nord"), tr("Vers l'est"), tr("Vers le sud"), tr("Vers l'ouest")};
    for (int side = 0; side < 4; ++side)
        extend->addAction(tr("%1 (+1 km)").arg(sides[side]), this, [this, side] { extendMap(side); });
    QMenu *sound = menu->addMenu(Icons::icon(Icons::Speaker), tr("Son"));
    sound->setWindowFlags(sound->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    sound->setAttribute(Qt::WA_TranslucentBackground);
    QAction *musicAct = sound->addAction(tr("Musique d'ambiance"));
    musicAct->setCheckable(true);
    musicAct->setChecked(Audio::instance().musicEnabled());
    connect(musicAct, &QAction::toggled, this, [](bool on) {
        Audio::instance().setMusicEnabled(on);
        QSettings().setValue("audio/music", on);
    });
    sound->addAction(volumeSlider(tr("Volume de la musique"), Audio::instance().musicVolume(), [](float v) {
        Audio::instance().setMusicVolume(v);
        QSettings().setValue("audio/musicVolume", v);
    }, sound));
    sound->addSeparator();
    QAction *sfxAct = sound->addAction(tr("Bruitages"));
    sfxAct->setCheckable(true);
    sfxAct->setChecked(Audio::instance().sfxEnabled());
    connect(sfxAct, &QAction::toggled, this, [](bool on) {
        Audio::instance().setSfxEnabled(on);
        QSettings().setValue("audio/sfx", on);
    });
    sound->addAction(volumeSlider(tr("Volume des bruitages"), Audio::instance().sfxVolume(), [](float v) {
        Audio::instance().setSfxVolume(v);
        QSettings().setValue("audio/sfxVolume", v);
        Audio::instance().play(Audio::Click);
    }, sound));
    menu->addAction(Icons::icon(Icons::Info), tr("Aide et raccourcis"), QKeySequence::HelpContents, this,
                    &MainWindow::showHelp);
    addActions(menu->actions()); // raccourcis actifs même menu fermé
    menuBtn->setMenu(menu);
    menuBtn->setPopupMode(QToolButton::InstantPopup);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(search, 1);
    row->addWidget(menuBtn);
    lay->addLayout(row);

    // Reprise de la dernière partie (sauvegarde automatique)
    m_resumeBox = new QWidget;
    auto *rl = new QHBoxLayout(m_resumeBox);
    rl->setContentsMargins(6, 0, 4, 0);
    auto *rIcon = new QLabel;
    rIcon->setPixmap(Icons::pixmap(Icons::Save, 18, Theme::TextDim));
    rl->addWidget(rIcon);
    m_resumeLabel = new QLabel;
    m_resumeLabel->setProperty("role", "status");
    m_resumeLabel->setWordWrap(true);
    rl->addWidget(m_resumeLabel, 1);
    auto *resumeBtn = new QPushButton(Icons::icon(Icons::Play, Qt::white, Qt::white), tr("Reprendre"));
    resumeBtn->setProperty("variant", "primary");
    resumeBtn->setCursor(Qt::PointingHandCursor);
    connect(resumeBtn, &QPushButton::clicked, this, [this] { openGame(m_resumePath); });
    rl->addWidget(resumeBtn);
    m_resumeBox->hide();
    lay->addWidget(m_resumeBox);

    // Progression du chargement
    m_busy = new QProgressBar;
    m_busy->setRange(0, 0);
    m_busy->setTextVisible(false);
    m_busy->hide();
    m_cityStatus = new QLabel;
    m_cityStatus->setProperty("role", "status");
    m_cityStatus->setWordWrap(true);
    m_cityStatus->hide();
    lay->addWidget(m_busy);
    lay->addWidget(m_cityStatus);
    lay->setContentsMargins(6, 6, 6, 6);
    return card;
}

Card *MainWindow::buildStatsCard()
{
    auto *card = new Card;
    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(Card::Shadow + 8, Card::Shadow + 6, Card::Shadow + 8, Card::Shadow + 8);
    lay->setSpacing(8);
    m_moneyChip = new StatChip(Icons::Wallet, tr("Budget et bilan mensuel (recettes − exploitation)"));
    m_dateChip = new StatChip(Icons::Calendar, tr("Date de jeu"));
    m_ridersChip = new StatChip(Icons::Users, tr("Voyageurs transportés à l'heure de pointe"));
    m_captureChip = new StatChip(Icons::Target, tr("Part de la demande de déplacements captée par le métro"));
    m_coverChip = new StatChip(Icons::Home, tr("Habitants à distance de marche d'une station en service"));
    QWidget *chips[] = {m_moneyChip, m_dateChip, m_ridersChip, m_captureChip, m_coverChip};
    for (int i = 0; i < 5; ++i) {
        if (i)
            lay->addWidget(vSeparator());
        lay->addWidget(chips[i]);
    }
    return card;
}

Card *MainWindow::buildDock()
{
    auto *card = new Card(nullptr, 18);
    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(Card::Shadow + 8, Card::Shadow + 6, Card::Shadow + 8, Card::Shadow + 8);
    lay->setSpacing(4);

    // Outils
    m_toolGroup = new QButtonGroup(this);
    const struct {
        Icons::Id icon;
        QString tip;
        MapView::Tool tool;
    } tools[] = {
        {Icons::Pointer, tr("Sélection — 1"), MapView::Select},
        {Icons::Station, tr("Construire une station — 2"), MapView::AddStation},
        {Icons::Route, tr("Tracer la ligne sélectionnée — 3"), MapView::BuildLine},
        {Icons::Trash, tr("Démolir — 4"), MapView::Delete},
        {Icons::Directions, tr("Itinéraire : temps de trajet entre deux points — 5"), MapView::RouteTool},
    };
    for (const auto &t : tools) {
        auto *b = iconButton(t.icon, t.tip, true, 44);
        if (t.tool == MapView::Delete)
            b->setProperty("variant", "danger");
        m_toolGroup->addButton(b, t.tool);
        lay->addWidget(b);
    }
    m_toolGroup->button(MapView::Select)->setChecked(true);
    connect(m_toolGroup, &QButtonGroup::idClicked, this, &MainWindow::setTool);

    lay->addSpacing(6);
    lay->addWidget(vSeparator());
    lay->addSpacing(6);

    // Lignes
    m_badgeBox = new QWidget;
    m_badgeLayout = new QHBoxLayout(m_badgeBox);
    m_badgeLayout->setContentsMargins(0, 0, 0, 0);
    m_badgeLayout->setSpacing(2);
    lay->addWidget(m_badgeBox);
    auto *add = iconButton(Icons::Plus, tr("Nouvelle ligne — N (numéro) · Maj+N (lettre)"), false, 40);
    add->setStyleSheet("QToolButton { border: 1.5px dashed rgba(255,255,255,0.25); border-radius: 20px; }"
                       "QToolButton:hover { border-color: #4C8DFF; }");
    auto *addMenu = new QMenu(add);
    addMenu->setWindowFlags(addMenu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    addMenu->setAttribute(Qt::WA_TranslucentBackground);
    QAction *addNumber = addMenu->addAction(Icons::icon(Icons::Plus), {}, this, [this] { newLine(false); });
    QAction *addLetter = addMenu->addAction(Icons::icon(Icons::Plus), {}, this, [this] { newLine(true); });
    connect(addMenu, &QMenu::aboutToShow, this, [this, addNumber, addLetter] {
        const QString n = m_metro->nextFreeCode(false), l = m_metro->nextFreeCode(true);
        addNumber->setText(tr("Ligne %1 — numérotée").arg(n));
        addLetter->setText(l.isEmpty() ? tr("Plus de lettre disponible") : tr("Ligne %1 — lettre").arg(l));
        addLetter->setEnabled(!l.isEmpty());
    });
    add->setMenu(addMenu);
    add->setPopupMode(QToolButton::InstantPopup);
    lay->addWidget(add);

    lay->addSpacing(6);
    lay->addWidget(vSeparator());
    lay->addSpacing(6);

    // Calques
    m_overlayGroup = new QButtonGroup(this);
    const struct {
        Icons::Id icon;
        QString tip;
        MapView::Overlay ov;
    } overlays[] = {
        {Icons::Flame, tr("Calque : demande captée / non desservie"), MapView::Demand},
        {Icons::Home, tr("Calque : densité d'habitants"), MapView::Population},
        {Icons::Briefcase, tr("Calque : densité d'emplois"), MapView::Jobs},
        {Icons::Gauge, tr("Calque : charge des lignes"), MapView::Load},
        {Icons::EyeOff, tr("Aucun calque"), MapView::NoOverlay},
    };
    for (const auto &o : overlays) {
        auto *b = iconButton(o.icon, o.tip, true, 40);
        m_overlayGroup->addButton(b, o.ov);
        lay->addWidget(b);
    }
    m_overlayGroup->button(MapView::Demand)->setChecked(true);
    connect(m_overlayGroup, &QButtonGroup::idClicked, this,
            [this](int id) { m_map->setOverlay(MapView::Overlay(id)); });

    lay->addSpacing(6);
    lay->addWidget(vSeparator());
    lay->addSpacing(6);

    // Vitesse
    m_speedGroup = new QButtonGroup(this);
    const struct {
        Icons::Id icon;
        QString tip;
        int speed;
    } speeds[] = {
        {Icons::Pause, tr("Pause — Espace"), 0},
        {Icons::Play, tr("Vitesse normale"), 1},
        {Icons::Fast, tr("Vitesse ×3"), 3},
        {Icons::Faster, tr("Vitesse ×10"), 10},
    };
    for (const auto &s : speeds) {
        auto *b = iconButton(s.icon, s.tip, true, 40);
        m_speedGroup->addButton(b, s.speed);
        lay->addWidget(b);
    }
    m_speedGroup->button(1)->setChecked(true);
    connect(m_speedGroup, &QButtonGroup::idClicked, this, [this](int id) { m_speed = id; });

    lay->addSpacing(6);
    lay->addWidget(vSeparator());
    lay->addSpacing(6);

    // Carte claire / sombre (mémorisé entre les parties)
    m_themeBtn = iconButton(Icons::Moon, tr("Carte sombre — D"), true, 40);
    auto applyTheme = [this](bool dark) {
        m_map->setDarkMap(dark);
        m_themeBtn->setIcon(Icons::icon(dark ? Icons::Sun : Icons::Moon));
        m_themeBtn->setToolTip(dark ? tr("Carte claire — D") : tr("Carte sombre — D"));
        QSettings().setValue("map/dark", dark);
    };
    connect(m_themeBtn, &QToolButton::toggled, this, applyTheme);
    const bool dark = QSettings().value("map/dark", false).toBool();
    m_themeBtn->setChecked(dark);
    applyTheme(dark);
    lay->addWidget(m_themeBtn);

    // Plan schématique du réseau
    m_schemaBtn = iconButton(Icons::Schema, tr("Plan schématique — M"), true, 40);
    connect(m_schemaBtn, &QToolButton::toggled, this, [this](bool on) {
        m_map->setSchematic(on);
        m_schemaBtn->setToolTip(on ? tr("Revenir à la carte — M") : tr("Plan schématique — M"));
    });
    lay->addWidget(m_schemaBtn);

    // Score et objectifs
    m_goalsBtn = iconButton(Icons::Trophy, tr("Score et objectifs — O"), true, 40);
    m_goalsBtn->setChecked(QSettings().value("ui/goals", true).toBool());
    connect(m_goalsBtn, &QToolButton::toggled, this, [this](bool on) {
        QSettings().setValue("ui/goals", on);
        refreshGoals();
        layoutOverlays();
        m_layoutTimer.start(0);
    });
    lay->addWidget(m_goalsBtn);

    // Finances
    m_financeBtn = iconButton(Icons::Wallet, tr("Finances — B"), true, 40);
    connect(m_financeBtn, &QToolButton::toggled, this, [this](bool on) {
        m_financeCard->setVisible(on && m_metro->city());
        refreshFinance();
        layoutOverlays();
        m_layoutTimer.start(0);
    });
    lay->addWidget(m_financeBtn);

    // Son (coupure rapide ; réglages fins dans le menu ☰ → Son)
    m_soundBtn = iconButton(Icons::Speaker, tr("Couper le son"), false, 40);
    auto syncSound = [this] {
        const bool muted = Audio::instance().muted();
        m_soundBtn->setIcon(Icons::icon(muted ? Icons::SpeakerOff : Icons::Speaker));
        m_soundBtn->setToolTip(muted ? tr("Rétablir le son") : tr("Couper le son"));
    };
    connect(m_soundBtn, &QToolButton::clicked, this, [syncSound] {
        Audio &a = Audio::instance();
        a.setMuted(!a.muted());
        QSettings().setValue("audio/muted", a.muted());
        syncSound();
    });
    syncSound();
    lay->addWidget(m_soundBtn);
    return card;
}

Card *MainWindow::buildLineCard()
{
    auto *card = new Card;
    card->setFixedWidth(370);
    m_lineContent = scrollableContent(card);
    auto *lay = static_cast<QVBoxLayout *>(m_lineContent->layout());
    lay->setSpacing(10);

    // En-tête
    auto *head = new QHBoxLayout;
    head->setSpacing(4);
    m_lineCodeBtn = new LineBadge(-1, {}, Theme::Accent, false);
    m_lineCodeBtn->setCheckable(false);
    m_lineCodeBtn->setToolTip(tr("Changer le numéro ou la lettre de la ligne"));
    connect(m_lineCodeBtn, &QToolButton::clicked, this, &MainWindow::showCodePicker);
    head->addWidget(m_lineCodeBtn);
    m_lineColorBtn = new QToolButton;
    m_lineColorBtn->setFixedSize(34, 34);
    m_lineColorBtn->setIconSize(QSize(22, 22));
    m_lineColorBtn->setToolTip(tr("Changer la couleur"));
    m_lineColorBtn->setCursor(Qt::PointingHandCursor);
    connect(m_lineColorBtn, &QToolButton::clicked, this, &MainWindow::showColorPicker);
    m_lineName = new QLineEdit;
    m_lineName->setObjectName("flat");
    connect(m_lineName, &QLineEdit::editingFinished, this,
            [this] { m_metro->setLineName(m_currentLine, m_lineName->text()); });
    head->addWidget(m_lineName, 1);
    head->addWidget(m_lineColorBtn);
    auto *del = iconButton(Icons::Trash, tr("Supprimer la ligne"), false, 32);
    del->setProperty("variant", "danger");
    connect(del, &QToolButton::clicked, this, [this] {
        const Line *l = m_metro->line(m_currentLine);
        if (l && QMessageBox::question(this, tr("Supprimer la ligne"),
                                       tr("Supprimer %1 ? (%2 % du coût remboursé)")
                                           .arg(l->name)
                                           .arg(int(Rules::Refund * 100)))
                     == QMessageBox::Yes)
            m_metro->removeLine(m_currentLine);
    });
    head->addWidget(del);
    auto *close = iconButton(Icons::Close, tr("Fermer — Échap"), false, 32);
    connect(close, &QToolButton::clicked, this, [this] { selectLine(-1); });
    head->addWidget(close);
    lay->addLayout(head);

    // Arrêts
    auto *stopsHead = new QHBoxLayout;
    stopsHead->setSpacing(2);
    stopsHead->addWidget(section(tr("Arrêts")));
    stopsHead->addStretch();
    auto *up = iconButton(Icons::ArrowUp, tr("Monter l'arrêt"), false, 30);
    auto *down = iconButton(Icons::ArrowDown, tr("Descendre l'arrêt"), false, 30);
    auto *rm = iconButton(Icons::Minus, tr("Retirer l'arrêt de la ligne"), false, 30);
    auto *rev = iconButton(Icons::Swap, tr("Inverser le sens de la ligne"), false, 30);
    for (auto *b : {up, down, rm, rev})
        stopsHead->addWidget(b);
    lay->addLayout(stopsHead);

    m_stopList = new QListWidget;
    m_stopDelegate = new StopDelegate(m_stopList);
    m_stopList->setItemDelegate(m_stopDelegate);
    m_stopList->setDragDropMode(QAbstractItemView::InternalMove);
    m_stopList->setDefaultDropAction(Qt::MoveAction);
    m_stopList->setMouseTracking(true);
    m_stopList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_stopList->setFrameShape(QFrame::NoFrame);
    lay->addWidget(m_stopList);
    m_stopsEmpty = new QLabel(tr("Choisissez l'outil « Tracer » puis cliquez des stations sur la carte "
                                 "(ou un lieu vide pour en créer une)."));
    m_stopsEmpty->setProperty("role", "status");
    m_stopsEmpty->setWordWrap(true);
    lay->addWidget(m_stopsEmpty);

    auto deferApply = [this] { QTimer::singleShot(0, this, &MainWindow::applyStopOrder); };
    connect(m_stopList->model(), &QAbstractItemModel::rowsMoved, this, deferApply);
    connect(m_stopList->model(), &QAbstractItemModel::rowsInserted, this, deferApply);
    connect(m_stopList->model(), &QAbstractItemModel::rowsRemoved, this, deferApply);
    connect(m_stopList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        selectStation(it->data(StopDelegate::StationIdRole).toInt());
        m_map->setSelectedStation(m_selectedStation);
    });
    auto moveSel = [this](int delta) {
        const int row = m_stopList->currentRow();
        const Line *l = m_metro->line(m_currentLine);
        if (row < 0 || !l || row + delta < 0 || row + delta >= l->stops.size())
            return;
        m_metro->moveStop(m_currentLine, row, row + delta);
        m_stopList->setCurrentRow(row + delta);
    };
    connect(up, &QToolButton::clicked, this, [moveSel] { moveSel(-1); });
    connect(down, &QToolButton::clicked, this, [moveSel] { moveSel(+1); });
    connect(rm, &QToolButton::clicked, this, [this] {
        if (QListWidgetItem *it = m_stopList->currentItem())
            m_metro->removeStop(m_currentLine, it->data(StopDelegate::StationIdRole).toInt());
    });
    connect(rev, &QToolButton::clicked, this, [this] { m_metro->reverseLine(m_currentLine); });

    // Trains
    lay->addSpacing(2);
    lay->addWidget(section(tr("Trains")));
    auto *wagons = new QHBoxLayout;
    wagons->setSpacing(6);
    m_wagonGroup = new QButtonGroup(this);
    for (int n = Rules::MinWagons; n <= Rules::MaxWagons; ++n) {
        auto *b = new QToolButton;
        b->setProperty("variant", "segment");
        b->setCheckable(true);
        b->setIcon(Icons::wagons(n));
        b->setIconSize(QSize(44, 14));
        b->setText(tr("%1 voit.").arg(n));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setToolTip(tr("Rames de %1 voitures (%2 places)").arg(n).arg(n * int(Rules::WagonCapacity)));
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_wagonGroup->addButton(b, n);
        wagons->addWidget(b);
    }
    connect(m_wagonGroup, &QButtonGroup::idClicked, this, [this](int n) { m_metro->setWagons(m_currentLine, n); });
    lay->addLayout(wagons);

    auto *trains = new QHBoxLayout;
    trains->setSpacing(4);
    auto *trainIcon = new QLabel;
    trainIcon->setPixmap(Icons::pixmap(Icons::Train, 18, Theme::TextDim));
    trains->addWidget(trainIcon);
    auto *minus = iconButton(Icons::Minus, tr("Retirer une rame"), false, 30);
    auto *plus = iconButton(Icons::Plus, tr("Ajouter une rame"), false, 30);
    m_trainCount = new QLabel;
    m_trainCount->setProperty("role", "value");
    m_trainCount->setMinimumWidth(70);
    m_trainCount->setAlignment(Qt::AlignCenter);
    trains->addWidget(minus);
    trains->addWidget(m_trainCount);
    trains->addWidget(plus);
    trains->addStretch();
    m_loopBtn = new QToolButton;
    m_loopBtn->setProperty("variant", "segment");
    m_loopBtn->setCheckable(true);
    m_loopBtn->setIcon(Icons::icon(Icons::Loop));
    m_loopBtn->setText(tr("Boucle"));
    m_loopBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_loopBtn->setToolTip(tr("Ligne circulaire : le dernier arrêt est relié au premier"));
    m_loopBtn->setCursor(Qt::PointingHandCursor);
    trains->addWidget(m_loopBtn);
    lay->addLayout(trains);
    connect(minus, &QToolButton::clicked, this, [this] {
        if (const Line *l = m_metro->line(m_currentLine))
            m_metro->setTrains(m_currentLine, l->trains - 1);
    });
    connect(plus, &QToolButton::clicked, this, [this] {
        if (const Line *l = m_metro->line(m_currentLine))
            m_metro->setTrains(m_currentLine, l->trains + 1);
    });
    connect(m_loopBtn, &QToolButton::toggled, this, [this](bool on) {
        if (!m_updating)
            m_metro->setLoop(m_currentLine, on);
    });

    // Exploitation
    lay->addSpacing(2);
    lay->addWidget(section(tr("Exploitation")));
    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    m_kLength = new KpiTile(tr("Longueur"));
    m_kCycle = new KpiTile(tr("Aller-retour"));
    m_kHeadway = new KpiTile(tr("Intervalle"));
    m_kCapacity = new KpiTile(tr("Capacité /sens"));
    m_kRiders = new KpiTile(tr("Fréquentation"));
    m_kLoad = new KpiTile(tr("Charge max"), true);
    KpiTile *tiles[] = {m_kLength, m_kCycle, m_kHeadway, m_kCapacity, m_kRiders, m_kLoad};
    for (int i = 0; i < 6; ++i)
        grid->addWidget(tiles[i], i / 3, i % 3);
    lay->addLayout(grid);
    m_lineAdvice = new QLabel;
    m_lineAdvice->setProperty("role", "advice");
    m_lineAdvice->setWordWrap(true);
    lay->addWidget(m_lineAdvice);
    return card;
}

Card *MainWindow::buildStationCard()
{
    auto *card = new Card;
    card->setFixedWidth(330);
    m_stationContent = scrollableContent(card);
    auto *lay = static_cast<QVBoxLayout *>(m_stationContent->layout());
    lay->setSpacing(10);

    auto *head = new QHBoxLayout;
    head->setSpacing(4);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(Icons::Station, 22, Theme::Accent.lighter(130)));
    head->addWidget(icon);
    m_stationName = new QLineEdit;
    m_stationName->setObjectName("flat");
    connect(m_stationName, &QLineEdit::editingFinished, this,
            [this] { m_metro->renameStation(m_selectedStation, m_stationName->text()); });
    head->addWidget(m_stationName, 1);
    auto *close = iconButton(Icons::Close, tr("Fermer — Échap"), false, 32);
    connect(close, &QToolButton::clicked, this, [this] {
        selectStation(-1);
        m_map->setSelectedStation(-1);
    });
    head->addWidget(close);
    lay->addLayout(head);

    m_stationLines = new QHBoxLayout;
    m_stationLines->setSpacing(4);
    lay->addLayout(m_stationLines);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    m_kPop = new KpiTile(tr("Habitants desservis"));
    m_kJobs = new KpiTile(tr("Emplois desservis"));
    m_kDemand = new KpiTile(tr("Demande locale"));
    m_kServed = new KpiTile(tr("Captée par le métro"), true);
    m_kBoard = new KpiTile(tr("Montées /h"));
    grid->addWidget(m_kPop, 0, 0);
    grid->addWidget(m_kJobs, 0, 1);
    grid->addWidget(m_kDemand, 1, 0);
    grid->addWidget(m_kBoard, 1, 1);
    grid->addWidget(m_kServed, 2, 0, 1, 2);
    lay->addLayout(grid);

    auto *btns = new QHBoxLayout;
    m_addToLineBtn = new QPushButton(Icons::icon(Icons::Plus, Qt::white, Qt::white), {});
    m_addToLineBtn->setProperty("variant", "primary");
    m_addToLineBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addToLineBtn, &QPushButton::clicked, this,
            [this] { m_metro->addStop(m_currentLine, m_selectedStation); });
    auto *demolish = new QPushButton(Icons::icon(Icons::Trash, QColor("#FF8A96"), QColor("#FFB3BB")), tr("Démolir"));
    demolish->setProperty("variant", "danger");
    demolish->setCursor(Qt::PointingHandCursor);
    connect(demolish, &QPushButton::clicked, this, [this] {
        m_metro->removeStation(m_selectedStation);
        selectStation(-1);
        m_map->setSelectedStation(-1);
    });
    btns->addWidget(m_addToLineBtn, 1);
    btns->addWidget(demolish);
    lay->addLayout(btns);
    return card;
}

void MainWindow::buildShortcuts()
{
    auto shortcut = [this](const QKeySequence &key, auto fn) {
        auto *a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    };
    for (int t = MapView::Select; t <= MapView::RouteTool; ++t)
        shortcut(QKeySequence(QString::number(t + 1)), [this, t] {
            if (m_metro->city())
                setTool(t);
        });
    shortcut(QKeySequence("N"), [this] {
        if (m_metro->city())
            newLine(false);
    });
    shortcut(QKeySequence("Shift+N"), [this] {
        if (m_metro->city())
            newLine(true);
    });
    shortcut(QKeySequence("D"), [this] { m_themeBtn->toggle(); });
    shortcut(QKeySequence("B"), [this] {
        if (m_metro->city())
            m_financeBtn->toggle();
    });
    shortcut(QKeySequence("O"), [this] {
        if (m_metro->city())
            m_goalsBtn->toggle();
    });
    shortcut(QKeySequence("M"), [this] {
        if (m_metro->city())
            m_schemaBtn->toggle();
    });
    shortcut(QKeySequence(Qt::Key_Space), [this] {
        const int target = m_speed == 0 ? 1 : 0;
        m_speedGroup->button(target)->click();
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (m_selectedStation >= 0) {
            selectStation(-1);
            m_map->setSelectedStation(-1);
        } else {
            selectLine(-1);
        }
    });
}

// ---------------------------------------------------------------------------
// Événements aléatoires
// ---------------------------------------------------------------------------

namespace {

// Pastille d'un événement : verte (favorable) ou rouge (défavorable), avec un symbole
QPixmap eventBadge(int tone, int size)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(tone > 0 ? Theme::Success : tone < 0 ? Theme::Danger : Theme::Warning);
    p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    QFont f = qApp->font();
    f.setBold(true);
    f.setPixelSize(int(size * 0.62));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, tone > 0 ? QStringLiteral("★") : QStringLiteral("!"));
    return pm;
}

} // namespace

void MainWindow::buildEventCards()
{
    // liste des événements en cours
    m_eventsCard = new Card;
    auto *el = new QVBoxLayout(m_eventsCard);
    el->setContentsMargins(Card::Shadow + 10, Card::Shadow + 6, Card::Shadow + 10, Card::Shadow + 8);
    el->setSpacing(4);
    el->addWidget(section(tr("Événements en cours")));
    m_eventsList = new QVBoxLayout;
    m_eventsList->setSpacing(2);
    el->addLayout(m_eventsList);

    // actualité (événement sans décision)
    m_newsCard = new Card;
    auto *nl = new QHBoxLayout(m_newsCard);
    nl->setContentsMargins(Card::Shadow + 14, Card::Shadow + 10, Card::Shadow + 10, Card::Shadow + 12);
    nl->setSpacing(12);
    m_newsIcon = new QLabel;
    nl->addWidget(m_newsIcon, 0, Qt::AlignTop);
    auto *ncol = new QVBoxLayout;
    m_newsTitle = new QLabel;
    m_newsTitle->setStyleSheet("font-weight: 700; font-size: 11pt;");
    m_newsTitle->setWordWrap(true);
    m_newsText = new QLabel;
    m_newsText->setWordWrap(true);
    m_newsText->setProperty("role", "subtitle");
    ncol->addWidget(m_newsTitle);
    ncol->addWidget(m_newsText);
    nl->addLayout(ncol, 1);
    auto *nclose = iconButton(Icons::Close, tr("Fermer"), false, 28);
    connect(nclose, &QToolButton::clicked, this, [this] { m_newsCard->hide(); });
    nl->addWidget(nclose, 0, Qt::AlignTop);
    m_newsTimer.setSingleShot(true);
    connect(&m_newsTimer, &QTimer::timeout, this, [this] { m_newsCard->hide(); });

    // décision (le jeu est en pause tant que le joueur n'a pas choisi)
    m_decisionCard = new Card(nullptr, 18);
    auto *dl = new QVBoxLayout(m_decisionCard);
    dl->setContentsMargins(Card::Shadow + 20, Card::Shadow + 16, Card::Shadow + 20, Card::Shadow + 18);
    dl->setSpacing(10);
    auto *dh = new QHBoxLayout;
    dh->setSpacing(12);
    m_decisionIcon = new QLabel;
    dh->addWidget(m_decisionIcon, 0, Qt::AlignTop);
    auto *dcol = new QVBoxLayout;
    auto *kicker = new QLabel(tr("DÉCISION REQUISE · JEU EN PAUSE"));
    kicker->setProperty("role", "section");
    m_decisionTitle = new QLabel;
    m_decisionTitle->setStyleSheet("font-weight: 700; font-size: 13pt;");
    m_decisionTitle->setWordWrap(true);
    dcol->addWidget(kicker);
    dcol->addWidget(m_decisionTitle);
    dh->addLayout(dcol, 1);
    dl->addLayout(dh);
    m_decisionText = new QLabel;
    m_decisionText->setWordWrap(true);
    m_decisionText->setProperty("role", "subtitle");
    dl->addWidget(m_decisionText);
    m_decisionButtons = new QHBoxLayout;
    m_decisionButtons->setSpacing(8);
    dl->addLayout(m_decisionButtons);
}

void MainWindow::onEventStarted(const GameEvent &e)
{
    // annonce (alerte si une décision est attendue), puis le motif favorable / défavorable
    Audio::instance().play(e.choices.isEmpty() ? Audio::Notify : Audio::Alert);
    const Audio::Sfx mood = e.kind == EventKind::Subsidy ? Audio::Coins : e.tone > 0 ? Audio::Good : Audio::Bad;
    QTimer::singleShot(550, this, [mood] { Audio::instance().play(mood); });
    if (!e.choices.isEmpty())
        showDecision(e);
    else
        showNews(e);
}

void MainWindow::showNews(const GameEvent &e)
{
    m_newsIcon->setPixmap(eventBadge(e.tone, 30));
    m_newsTitle->setText(e.title);
    m_newsText->setText(e.text);
    m_newsCard->show();
    m_newsTimer.start(9000);
    layoutOverlays();
    m_layoutTimer.start(0);
}

void MainWindow::showDecision(const GameEvent &e)
{
    if (m_decisionCard->isVisible() && m_decisionId != e.id)
        return; // une décision à la fois ; les suivantes restent dans la liste des événements
    m_decisionId = e.id;
    m_decisionIcon->setPixmap(eventBadge(e.tone, 38));
    m_decisionTitle->setText(e.title);
    m_decisionText->setText(e.text);
    clearLayout(m_decisionButtons);
    m_decisionButtons->addStretch();
    for (int i = e.choices.size() - 1; i >= 0; --i) {
        auto *b = new QPushButton(e.choices[i]);
        b->setProperty("variant", i == 0 ? "primary" : "danger");
        if (i > 0)
            b->setStyleSheet("QPushButton { background: rgba(255,255,255,0.08); color: #E8EAED; }"
                             "QPushButton:hover { background: rgba(255,255,255,0.16); }");
        b->setCursor(Qt::PointingHandCursor);
        const int id = e.id;
        connect(b, &QPushButton::clicked, this, [this, id, i] {
            m_decisionCard->hide();
            m_decisionId = -1;
            Audio::instance().play(i == 0 ? Audio::Coins : Audio::Click);
            m_metro->resolveEvent(id, i);
            if (m_speedBeforeDecision > 0) // reprise de la partie à sa vitesse
                m_speedGroup->button(m_speedBeforeDecision)->click();
            m_speedBeforeDecision = -1;
            // une autre décision attend peut-être
            for (const GameEvent &o : m_metro->activeEvents())
                if (o.pending) {
                    showDecision(o);
                    break;
                }
        });
        m_decisionButtons->addWidget(b);
    }
    if (m_speedBeforeDecision < 0)
        m_speedBeforeDecision = qRound(m_speed);
    m_speedGroup->button(0)->click(); // pause
    m_decisionCard->show();
    layoutOverlays();
    m_layoutTimer.start(0);
}

void MainWindow::refreshEvents()
{
    clearLayout(m_eventsList);
    const auto &events = m_metro->activeEvents();
    for (const GameEvent &e : events) {
        QString when = e.pending ? tr("décision en attente")
                                 : e.weeksLeft > 1 ? tr("encore %1 semaines").arg(e.weeksLeft)
                                                   : tr("dernière semaine");
        auto *b = new QPushButton(QIcon(eventBadge(e.tone, 18)), QStringLiteral("%1\n%2").arg(e.title, when));
        b->setIconSize(QSize(18, 18));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 8px; color: #E8EAED;"
                         " text-align: left; padding: 5px 6px; }"
                         "QPushButton:hover { background: rgba(255,255,255,0.07); }");
        b->setToolTip(e.text);
        const GameEvent copy = e;
        connect(b, &QPushButton::clicked, this, [this, copy] {
            if (copy.pending)
                showDecision(copy);
            else if (!copy.pos.isNull())
                m_map->setView(copy.pos, std::max(m_map->viewScale(), 0.35));
        });
        m_eventsList->addWidget(b);
    }
    m_eventsCard->setVisible(!events.isEmpty() && m_metro->city());
    // partie chargée avec une décision en suspens
    if (!m_decisionCard->isVisible())
        for (const GameEvent &e : events)
            if (e.pending) {
                showDecision(e);
                break;
            }
    layoutOverlays();
    m_layoutTimer.start(0);
}

// ---------------------------------------------------------------------------
// Score et objectifs
// ---------------------------------------------------------------------------

Card *MainWindow::buildGoalsCard()
{
    auto *card = new Card;
    card->setFixedWidth(330);
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(Card::Shadow + 12, Card::Shadow + 8, Card::Shadow + 10, Card::Shadow + 10);
    lay->setSpacing(8);

    auto *head = new QHBoxLayout;
    head->setSpacing(10);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(Icons::Trophy, 20, QColor("#F5C542")));
    icon->setFixedSize(34, 34);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("background: rgba(245,197,66,0.14); border-radius: 10px;");
    head->addWidget(icon);
    auto *col = new QVBoxLayout;
    col->setSpacing(0);
    m_scoreValue = new QLabel("0");
    m_scoreValue->setProperty("role", "value");
    m_scoreValue->setToolTip(tr("Score de la partie"));
    m_scoreSub = caption({});
    col->addWidget(m_scoreValue);
    col->addWidget(m_scoreSub);
    head->addLayout(col, 1);
    auto *close = iconButton(Icons::Close, tr("Masquer — O"), false, 28);
    connect(close, &QToolButton::clicked, this, [this] { m_goalsBtn->setChecked(false); });
    head->addWidget(close, 0, Qt::AlignTop);
    lay->addLayout(head);

    lay->addWidget(section(tr("Objectifs")));
    for (int i = 0; i < 3; ++i) {
        GoalRow r;
        r.box = new QWidget;
        auto *rl = new QVBoxLayout(r.box);
        rl->setContentsMargins(0, 0, 0, 2);
        rl->setSpacing(4);
        r.title = new QLabel;
        r.title->setStyleSheet("font-weight: 600;");
        rl->addWidget(r.title);
        r.bar = new QProgressBar;
        r.bar->setRange(0, 1000);
        r.bar->setTextVisible(false);
        rl->addWidget(r.bar);
        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(8);
        r.progress = caption({});
        r.reward = caption({});
        r.reward->setStyleSheet("color: #F5C542;");
        bottom->addWidget(r.progress, 1);
        bottom->addWidget(r.reward);
        rl->addLayout(bottom);
        lay->addWidget(r.box);
        m_goalRows << r;
    }
    m_goalsFooter = caption({});
    m_goalsFooter->setWordWrap(true);
    lay->addWidget(m_goalsFooter);
    return card;
}

QString MainWindow::bestScoreKey() const
{
    return QStringLiteral("scores/%1").arg(QFileInfo(autosavePath()).completeBaseName());
}

void MainWindow::refreshGoals()
{
    if (!m_metro->city())
        return;
    const double score = m_metro->score();
    if (!m_metro->sandbox() && score > m_bestScore) {
        m_bestScore = score;
        QSettings().setValue(bestScoreKey(), score);
    }
    if (!m_goalsCard->isVisible() && !m_goalsBtn->isChecked())
        return;
    m_scoreValue->setText(tr("%1 points").arg(count(score)));
    m_scoreSub->setText(tr("+%1 à la fin du mois · record %2").arg(count(m_metro->monthPoints())).arg(count(m_bestScore)));
    const auto &goals = m_metro->objectives();
    for (int i = 0; i < m_goalRows.size(); ++i) {
        GoalRow &r = m_goalRows[i];
        r.box->setVisible(i < goals.size());
        if (i >= goals.size())
            continue;
        const Objective &g = goals[i];
        r.title->setText(g.title);
        r.reward->setText(tr("+%1 pts · +%2").arg(g.points).arg(money(g.reward)));
        r.bar->setValue(int(g.progress() * 1000));
        r.bar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background: %1; border-radius: 3px; }")
                                 .arg((g.progress() >= 0.75 ? Theme::Success : Theme::Accent).name()));
        r.progress->setText(Metro::goalProgressText(g));
    }
    m_goalsFooter->setText(goals.isEmpty() ? tr("Tous les objectifs sont atteints !")
                           : m_metro->goalsCompleted() == 0
                               ? tr("Chaque objectif atteint rapporte des points et une prime.")
                           : m_metro->goalsCompleted() == 1 ? tr("1 objectif atteint")
                                                            : tr("%1 objectifs atteints").arg(m_metro->goalsCompleted()));
}

void MainWindow::onGoalCompleted(const Objective &goal)
{
    m_toast->show(tr("Objectif atteint : %1 · +%2 points · +%3").arg(goal.title).arg(goal.points).arg(money(goal.reward)),
                  false, true);
    Audio::instance().play(Audio::Coins);
    QTimer::singleShot(350, this, [] { Audio::instance().play(Audio::Good); });
    refreshGoals();
    m_layoutTimer.start(0);
}

// ---------------------------------------------------------------------------
// Itinéraire
// ---------------------------------------------------------------------------

Card *MainWindow::buildRouteCard()
{
    auto *card = new Card;
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(Card::Shadow + 12, Card::Shadow + 8, Card::Shadow + 10, Card::Shadow + 10);
    lay->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(Icons::Directions, 20, Theme::Accent.lighter(130)));
    head->addWidget(icon);
    auto *title = new QLabel(tr("Itinéraire"));
    title->setStyleSheet("font-weight: 700; font-size: 11pt;");
    head->addWidget(title, 1);
    auto *close = iconButton(Icons::Close, tr("Effacer l'itinéraire"), false, 28);
    connect(close, &QToolButton::clicked, m_map, &MapView::clearRoute);
    head->addWidget(close);
    lay->addLayout(head);
    m_routeSummary = new QLabel;
    m_routeSummary->setStyleSheet("font-size: 15pt; font-weight: 700;");
    lay->addWidget(m_routeSummary);
    m_routeDetail = new QLabel;
    m_routeDetail->setWordWrap(true);
    m_routeDetail->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_routeDetail->setProperty("role", "subtitle");
    lay->addWidget(m_routeDetail);
    lay->addSpacing(4);
    m_routeSteps = new QVBoxLayout;
    m_routeSteps->setSpacing(4);
    lay->addLayout(m_routeSteps);
    return card;
}

void MainWindow::showRoute()
{
    const Route r = m_metro->route(m_routeA, m_routeB);
    m_map->setRoute(r);
    auto mins = [](double m) {
        const int n = std::max(1, int(std::round(m)));
        return n >= 60 ? QObject::tr("%1 h %2").arg(n / 60).arg(n % 60, 2, 10, QLatin1Char('0'))
                       : QObject::tr("%1 min").arg(n);
    };
    if (r.byMetro) {
        const double gain = r.walkMinutes - r.minutes;
        m_routeSummary->setText(tr("%1 en métro").arg(mins(r.minutes)));
        m_routeDetail->setText(tr("%1 à pied · %2 gagnées").arg(mins(r.walkMinutes), mins(gain)));
        m_routeDetail->setWordWrap(false); // une ligne : pas d'espace réservé pour un retour à la ligne
    } else {
        m_routeSummary->setText(tr("%1 à pied").arg(mins(r.walkMinutes)));
        m_routeDetail->setWordWrap(true);
        m_routeDetail->setText(tr("Aucun trajet en métro plus rapide : il manque une station à moins de 1,5 km "
                                  "du départ ou de l'arrivée, ou le détour est trop grand."));
    }
    clearLayout(m_routeSteps);
    auto stationName = [this](int id) {
        const Station *s = m_metro->station(id);
        return s ? s->name : QString();
    };
    for (const RouteStep &st : r.steps) {
        auto *rowWidget = new QWidget; // un widget par étape : effacé proprement par clearLayout
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        QString text;
        if (st.kind == RouteStep::Ride) {
            const Line *l = m_metro->line(st.lineId);
            auto *badge = new LineBadge(st.lineId, l ? l->code : QString(), l ? l->color : Qt::gray, false);
            badge->setFixedSize(26, 26);
            badge->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(badge, 0, Qt::AlignTop);
            text = tr("<b>%1</b> direction %2<br><span style='color:#9AA0A6'>%3 → %4 · %5 · %6, attente comprise</span>")
                       .arg(l ? l->name : QString(), st.towards, stationName(st.fromStation),
                            stationName(st.toStation),
                            st.stops == 1 ? tr("1 arrêt") : tr("%1 arrêts").arg(st.stops), mins(st.minutes));
        } else {
            auto *ic = new QLabel;
            ic->setFixedSize(26, 26);
            ic->setAlignment(Qt::AlignCenter);
            ic->setPixmap(Icons::pixmap(st.kind == RouteStep::Walk ? Icons::Users : Icons::Swap, 16, Theme::TextDim));
            row->addWidget(ic, 0, Qt::AlignTop);
            if (st.kind == RouteStep::Transfer)
                text = tr("Correspondance à <b>%1</b><br><span style='color:#9AA0A6'>%2 avec l'attente</span>")
                           .arg(stationName(st.fromStation), mins(st.minutes));
            else if (st.toStation >= 0)
                text = tr("Marcher jusqu'à <b>%1</b><br><span style='color:#9AA0A6'>%2 · %3 m</span>")
                           .arg(stationName(st.toStation), mins(st.minutes)).arg(qRound(st.meters / 10) * 10);
            else
                text = tr("Marcher jusqu'à l'arrivée<br><span style='color:#9AA0A6'>%1 · %2 m</span>")
                           .arg(mins(st.minutes)).arg(qRound(st.meters / 10) * 10);
        }
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(label, 1);
        m_routeSteps->addWidget(rowWidget);
    }
    m_routeCard->show();
    layoutOverlays();
    m_layoutTimer.start(0);
}

// ---------------------------------------------------------------------------
// Export, bac à sable, annuler
// ---------------------------------------------------------------------------

void MainWindow::exportPlan()
{
    if (!m_metro->city())
        return;
    if (m_metro->lines().isEmpty()) {
        m_toast->show(tr("Créez au moins une ligne avant d'exporter le plan"), true);
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString name = m_metro->city()->name;
    name.replace(QRegularExpression("[^\\w-]+"), "_");
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter le plan du réseau"),
                                                QStringLiteral("%1/plan-metro-%2.png").arg(dir, name),
                                                tr("Image PNG (*.png);;Document PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive) && !path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".png";
    if (m_map->exportPlan(path, tr("Métro de %1").arg(m_metro->city()->name)))
        m_toast->show(tr("Plan exporté : %1").arg(QFileInfo(path).fileName()));
    else
        m_toast->show(tr("Impossible d'écrire %1").arg(QFileInfo(path).fileName()), true);
}

void MainWindow::exportMapImage()
{
    if (!m_metro->city())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString path = QFileDialog::getSaveFileName(this, tr("Capture de la carte"),
                                                QStringLiteral("%1/metro-%2.png").arg(dir, m_metro->city()->name),
                                                tr("Image PNG (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    if (m_map->grab().save(path))
        m_toast->show(tr("Carte enregistrée : %1").arg(QFileInfo(path).fileName()));
    else
        m_toast->show(tr("Impossible d'écrire %1").arg(QFileInfo(path).fileName()), true);
}

void MainWindow::enterSandbox()
{
    if (!m_metro->city() || m_metro->sandbox())
        return;
    if (QMessageBox::question(this, tr("Bac à sable"),
                              tr("Passer cette partie en bac à sable ? La construction devient gratuite, "
                                 "les événements et les objectifs s'arrêtent. On ne peut pas revenir en carrière."))
        != QMessageBox::Yes)
        return;
    m_metro->setSandbox(true);
    refreshAll();
    refreshUndo();
    m_toast->show(tr("Partie en bac à sable : construisez librement"));
}

void MainWindow::refreshUndo()
{
    const bool city = m_metro->city() != nullptr;
    m_undoAction->setEnabled(city && m_metro->canUndo());
    m_redoAction->setEnabled(city && m_metro->canRedo());
    m_sandboxAction->setEnabled(city && !m_metro->sandbox());
}

// ---------------------------------------------------------------------------
// Finances
// ---------------------------------------------------------------------------

Card *MainWindow::buildFinanceCard()
{
    auto *card = new Card;
    m_financeContent = scrollableContent(card);
    auto *lay = static_cast<QVBoxLayout *>(m_financeContent->layout());
    lay->setSpacing(12);

    auto *head = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(Icons::Wallet, 22, Theme::Accent.lighter(130)));
    head->addWidget(icon);
    auto *title = new QLabel(tr("Finances du réseau"));
    title->setStyleSheet("font-size: 13pt; font-weight: 700;");
    head->addWidget(title);
    head->addStretch();
    // période affichée par les graphiques (mémorisée)
    auto *rangeGroup = new QButtonGroup(this);
    const struct {
        QString text;
        int months;
    } ranges[] = {{tr("6 mois"), 6}, {tr("1 an"), 12}, {tr("2 ans"), 24}, {tr("5 ans"), 60}, {tr("Tout"), 0}};
    m_financeRange = QSettings().value("finance/range", 24).toInt();
    for (const auto &r : ranges) {
        auto *b = new QToolButton;
        b->setText(r.text);
        b->setCheckable(true);
        b->setProperty("variant", "segment");
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(r.months ? tr("Afficher les %1 derniers mois").arg(r.months) : tr("Afficher toute la partie"));
        b->setChecked(r.months == m_financeRange);
        rangeGroup->addButton(b, r.months);
        head->addWidget(b);
    }
    connect(rangeGroup, &QButtonGroup::idClicked, this, [this](int months) {
        m_financeRange = months;
        QSettings().setValue("finance/range", months);
        refreshFinance();
    });
    head->addSpacing(8);
    auto *close = iconButton(Icons::Close, tr("Fermer — B"), false, 32);
    connect(close, &QToolButton::clicked, this, [this] { m_financeBtn->setChecked(false); });
    head->addWidget(close);
    lay->addLayout(head);

    auto *kpis = new QHBoxLayout;
    kpis->setSpacing(8);
    m_fMoney = new KpiTile(tr("Trésorerie"));
    m_fRevenue = new KpiTile(tr("Recettes / mois"));
    m_fOperating = new KpiTile(tr("Exploitation / mois"));
    m_fResult = new KpiTile(tr("Résultat d'exploitation / mois"));
    m_fInvested = new KpiTile(tr("Investi depuis le début"));
    m_fTurnover = new KpiTile(tr("Chiffre d'affaires cumulé"));
    for (KpiTile *t : {m_fMoney, m_fRevenue, m_fOperating, m_fResult, m_fTurnover, m_fInvested})
        kpis->addWidget(t);
    lay->addLayout(kpis);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(16);
    // échelles très différentes (centaines de M€ d'investissement contre quelques M€ de recettes) :
    // un graphique par grandeur plutôt qu'un seul où les petites valeurs disparaîtraient
    m_cMoney = new ChartWidget(tr("Trésorerie"));
    m_cMoney->setSubtitle(tr("M€, en fin de mois"));
    m_cFlows = new ChartWidget(tr("Recettes et exploitation"));
    m_cFlows->setSubtitle(tr("M€ par mois · dernier mois en cours"));
    m_cResult = new ChartWidget(tr("Résultat d'exploitation"));
    m_cResult->setSubtitle(tr("recettes − exploitation, M€ par mois"));
    m_cInvest = new ChartWidget(tr("Investissements"));
    m_cInvest->setSubtitle(tr("construction nette, M€ par mois"));
    m_cRiders = new ChartWidget(tr("Fréquentation"));
    m_cRiders->setSubtitle(tr("voyageurs par heure de pointe"));
    m_cCapture = new ChartWidget(tr("Demande captée"));
    m_cCapture->setSubtitle(tr("part des déplacements faits en métro"));
    const ChartWidget *charts[] = {m_cMoney, m_cFlows, m_cResult, m_cInvest, m_cRiders, m_cCapture};
    for (int i = 0; i < 6; ++i) {
        auto *ch = const_cast<ChartWidget *>(charts[i]);
        ch->setMinimumHeight(190);
        grid->addWidget(ch, i / 3, i % 3);
    }
    // score : cumul sur deux colonnes, puis les points gagnés chaque mois selon leur origine
    m_cScore = new ChartWidget(tr("Score"));
    m_cScore->setSubtitle(tr("points cumulés, en fin de mois · mois en cours estimé"));
    m_cPoints = new ChartWidget(tr("Points gagnés"));
    m_cPoints->setSubtitle(tr("par mois, selon leur origine"));
    for (ChartWidget *ch : {m_cScore, m_cPoints})
        ch->setMinimumHeight(190);
    grid->addWidget(m_cScore, 2, 0, 1, 2);
    grid->addWidget(m_cPoints, 2, 2);
    lay->addLayout(grid);

    lay->addWidget(section(tr("Rentabilité par ligne")));
    m_lineEco = new LineEconomicsWidget;
    lay->addWidget(m_lineEco);
    auto *note = caption(tr("Recettes réparties entre les lignes au prorata de leur fréquentation · "
                            "coûts = exploitation des rames (hors stations et construction)."));
    note->setWordWrap(true);
    lay->addWidget(note);
    return card;
}

void MainWindow::refreshFinance()
{
    if (!m_financeCard || !m_financeCard->isVisible())
        return;
    const auto loc = QLocale(QLocale::French);
    auto meur = [loc](double v) {
        // assez de décimales pour distinguer les graduations (0,25 ; 2,5…) sans surcharger les grands montants
        const double a = std::abs(v);
        const int decimals = a >= 100 ? 0 : a >= 10 ? 1 : a == 0 ? 0 : a >= 1 ? 1 : 2;
        return QStringLiteral("%1 M€").arg(loc.toString(v, 'f', decimals));
    };

    // KPI
    const double rev = m_metro->monthlyRevenue() / 1e6, op = m_metro->monthlyCost() / 1e6;
    m_fMoney->setValue(meur(m_metro->money()));
    m_fRevenue->setValue(meur(rev));
    m_fOperating->setValue(meur(op));
    m_fResult->setValue(QStringLiteral("%1%2").arg(rev - op >= 0 ? "+" : "−").arg(meur(std::abs(rev - op))));
    m_fInvested->setValue(meur(m_metro->totalInvested()));
    m_fTurnover->setValue(meur(m_metro->totalRevenue()));

    // 24 derniers mois, le dernier étant le mois en cours
    QVector<MonthRecord> months = m_metro->history();
    months << m_metro->currentMonth();
    if (m_financeRange > 0 && months.size() > m_financeRange)
        months = months.mid(months.size() - m_financeRange);
    const bool withYear = m_financeRange == 0 ? months.size() > 24 : m_financeRange > 24;
    static const char *shortNames[] = {"janv.", "févr.", "mars", "avr.", "mai", "juin",
                                       "juil.", "août", "sept.", "oct.", "nov.", "déc."};
    static const char *longNames[] = {"Janvier", "Février", "Mars", "Avril", "Mai", "Juin",
                                      "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre"};
    QStringList labels, tips;
    QVector<double> money, revenue, operating, invest, riders, capture, score, monthPts, goalPts;
    for (int i = 0; i < months.size(); ++i) {
        const MonthRecord &r = months[i];
        const int m = r.month - 1;
        // sur plusieurs années, l'axe porte aussi l'année (« mars 3 » = mars de l'année 3)
        labels << (withYear ? QStringLiteral("%1 %2").arg(tr(shortNames[m % 12])).arg(m / 12 + 1)
                            : tr(shortNames[m % 12]));
        tips << tr("%1, année %2%3").arg(tr(longNames[m % 12])).arg(m / 12 + 1)
                    .arg(i == months.size() - 1 ? tr(" (en cours)") : QString());
        money << r.money;
        revenue << r.revenue;
        operating << r.operating;
        invest << r.investment;
        riders << r.riders;
        capture << r.capture * 100;
        score << r.score;
        monthPts << r.monthPoints;
        goalPts << r.goalPoints;
    }
    // palette catégorielle validée sur le fond des panneaux (ordre fixe)
    const QColor s1("#3987e5"), s2("#d95926");
    m_cMoney->setData(ChartWidget::AreaChart, labels, tips, {{tr("Trésorerie"), s1, money}}, meur);
    m_cFlows->setData(ChartWidget::BarChart, labels, tips,
                      {{tr("Recettes"), s1, revenue}, {tr("Exploitation"), s2, operating}}, meur);
    QVector<double> result;
    for (int i = 0; i < revenue.size(); ++i)
        result << revenue[i] - operating[i];
    m_cResult->setData(ChartWidget::BarChart, labels, tips, {{tr("Résultat"), s1, result}}, meur);
    m_cInvest->setData(ChartWidget::BarChart, labels, tips, {{tr("Investissements"), s1, invest}}, meur);
    m_cRiders->setData(ChartWidget::LineChart, labels, tips, {{tr("Voyageurs/h"), s1, riders}},
                       [loc](double v) { return loc.toString(qRound(v)); });
    m_cCapture->setData(ChartWidget::LineChart, labels, tips, {{tr("Demande captée"), s1, capture}},
                        [loc](double v) { return QStringLiteral("%1 %").arg(loc.toString(v, 'f', v < 10 ? 1 : 0)); });

    auto pts = [loc](double v) { return tr("%1 pts").arg(loc.toString(qRound(v))); };
    m_cScore->setData(ChartWidget::AreaChart, labels, tips, {{tr("Score"), s1, score}}, pts);
    m_cPoints->setData(ChartWidget::BarChart, labels, tips,
                       {{tr("Fin de mois"), s1, monthPts}, {tr("Objectifs"), s2, goalPts}}, pts);

    QVector<LineEconomicsWidget::Row> rows;
    for (const Line &l : m_metro->lines()) {
        if (l.segmentCount() == 0)
            continue;
        rows << LineEconomicsWidget::Row{l.code, l.name, l.color, l.ridership,
                                         m_metro->lineMonthlyRevenue(l) / 1e6, m_metro->lineMonthlyCost(l) / 1e6};
    }
    m_lineEco->setRows(rows);
}

// ---------------------------------------------------------------------------
// Placement des panneaux flottants
// ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_root && event->type() == QEvent::Resize)
        layoutOverlays();
    if (watched == m_moneyChip && event->type() == QEvent::MouseButtonPress)
        m_financeBtn->toggle();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::layoutOverlays()
{
    const int W = m_root->width(), H = m_root->height();
    m_map->setGeometry(0, 0, W, H);
    const bool hasCity = m_metro->city() != nullptr;

    // Ville : centrée sur l'écran d'accueil, compacte en haut à gauche ensuite
    if (!hasCity) {
        const int w = std::min(580, W - 40);
        m_cityCard->setFixedWidth(w);
        m_cityCard->adjustSize();
        m_cityCard->move((W - w) / 2, (H - m_cityCard->height()) / 2 - 40);
    } else {
        m_cityCard->setFixedWidth(std::min(440, W / 3 + 40));
        m_cityCard->adjustSize();
        m_cityCard->move(kMargin, kMargin);
    }

    m_statsCard->adjustSize();
    m_statsCard->move(W - m_statsCard->width() - kMargin, kMargin);

    m_dock->adjustSize();
    const int dockTop = H - m_dock->height() - kMargin;
    m_dock->move((W - m_dock->width()) / 2, dockTop);

    // Panneaux latéraux : jamais plus hauts que l'espace libre ; au-delà, leur contenu défile.
    const bool compact = H < 820;
    // événements en cours : sous la recherche, à gauche
    if (m_eventsCard->isVisible()) {
        m_eventsCard->setFixedWidth(std::min(360, m_cityCard->width()));
        m_eventsCard->adjustSize();
        m_eventsCard->move(kMargin, m_cityCard->geometry().bottom() - Card::Shadow);
    }
    // itinéraire : sous les événements (ou la recherche), à gauche
    if (m_routeCard->isVisible()) {
        const int top = (m_eventsCard->isVisible() ? m_eventsCard : m_cityCard)->geometry().bottom() - Card::Shadow;
        const int w = std::min(380, std::max(320, m_cityCard->width()));
        m_routeCard->setFixedWidth(w);
        // hauteur exacte pour cette largeur (les textes sur plusieurs lignes faussent adjustSize)
        m_routeCard->layout()->activate();
        m_routeCard->setFixedHeight(m_routeCard->layout()->totalHeightForWidth(w));
        m_routeCard->move(kMargin, top);
    }
    for (QWidget *content : {m_lineContent, m_stationContent})
        content->layout()->setSpacing(compact ? 6 : 10);
    auto wanted = [](Card *card, QWidget *content) {
        const QMargins m = card->layout()->contentsMargins() + card->contentsMargins();
        return content->sizeHint().height() + m.top() + m.bottom();
    };
    const int bottomLimit = dockTop + Card::Shadow; // haut visuel du dock
    if (m_lineCard->isVisible()) {
        const int topEdge = m_routeCard->isVisible()    ? m_routeCard->geometry().bottom()
                            : m_eventsCard->isVisible() ? m_eventsCard->geometry().bottom()
                                                        : m_cityCard->geometry().bottom();
        const int avail = bottomLimit - (topEdge - Card::Shadow);
        // la liste des arrêts rétrécit d'abord (3 arrêts visibles au minimum), puis le panneau défile
        const int rows = m_stopList->count();
        const int full = rows * 32 + 4;
        m_stopList->setFixedHeight(full);
        const int excess = wanted(m_lineCard, m_lineContent) - avail;
        if (excess > 0)
            m_stopList->setFixedHeight(std::max(std::min(rows, 3) * 32 + 4, full - excess));
        const int h = std::min(wanted(m_lineCard, m_lineContent), avail);
        m_lineCard->resize(m_lineCard->width(), h);
        m_lineCard->move(kMargin, bottomLimit - h);
    }
    // score et objectifs : sous les indicateurs, à droite (masqués sur petit écran quand la fiche station est ouverte)
    m_goalsCard->setVisible(hasCity && !m_metro->sandbox() && m_goalsBtn->isChecked()
                            && !(compact && m_stationCard->isVisible()));
    m_goalsBtn->setEnabled(!m_metro->sandbox());
    if (m_goalsCard->isVisible()) {
        m_goalsCard->adjustSize();
        m_goalsCard->move(W - m_goalsCard->width() - kMargin, m_statsCard->geometry().bottom() - Card::Shadow);
    }
    if (m_stationCard->isVisible()) {
        const int top = (m_goalsCard->isVisible() ? m_goalsCard : m_statsCard)->geometry().bottom() - Card::Shadow;
        const int h = std::min(wanted(m_stationCard, m_stationContent), bottomLimit - top);
        m_stationCard->resize(m_stationCard->width(), h);
        m_stationCard->move(W - m_stationCard->width() - kMargin, top);
    }
    if (m_financeCard->isVisible()) {
        // grand panneau centré entre le bandeau du haut et la barre du bas
        const int w = std::min(1040, W - 2 * kMargin);
        m_financeCard->setFixedWidth(w);
        const int top = std::max(m_statsCard->geometry().bottom(), m_cityCard->geometry().bottom()) - Card::Shadow;
        const int h = std::min(wanted(m_financeCard, m_financeContent), bottomLimit - top);
        m_financeCard->resize(w, h);
        m_financeCard->move((W - w) / 2, top);
    }

    if (m_newsCard->isVisible()) {
        const int w = std::min(500, W - 2 * kMargin);
        m_newsCard->setFixedWidth(w);
        m_newsCard->adjustSize();
        m_newsCard->move((W - w) / 2, m_statsCard->geometry().bottom() - Card::Shadow);
    }
    if (m_decisionCard->isVisible()) {
        const int w = std::min(520, W - 2 * kMargin);
        m_decisionCard->setFixedWidth(w);
        m_decisionCard->adjustSize();
        m_decisionCard->move((W - w) / 2, (H - m_decisionCard->height()) / 2 - 30);
    }

    m_map->setInsets(hasCity ? m_cityCard->geometry().bottom() - Card::Shadow : 0,
                     hasCity ? H - dockTop - Card::Shadow : 0);
    // messages au-dessus de la bulle d'aide de la carte
    m_toast->setBottom(hasCity ? dockTop + Card::Shadow - 56 : H - 40);
    for (QWidget *w : {static_cast<QWidget *>(m_cityCard), static_cast<QWidget *>(m_statsCard),
                       static_cast<QWidget *>(m_dock), static_cast<QWidget *>(m_lineCard),
                       static_cast<QWidget *>(m_stationCard), static_cast<QWidget *>(m_goalsCard),
                       static_cast<QWidget *>(m_routeCard),
                       static_cast<QWidget *>(m_financeCard),
                       static_cast<QWidget *>(m_eventsCard), static_cast<QWidget *>(m_newsCard),
                       static_cast<QWidget *>(m_decisionCard), static_cast<QWidget *>(m_toast)})
        w->raise();
}

// ---------------------------------------------------------------------------
// Chargement de la ville
// ---------------------------------------------------------------------------

void MainWindow::loadCity()
{
    if (m_loader->busy() || m_cityEdit->text().trimmed().isEmpty())
        return;
    if (!m_metro->stations().isEmpty()
        && QMessageBox::question(this, tr("Nouvelle ville"),
                                 tr("Charger une nouvelle ville efface le réseau actuel. Continuer ?"))
               != QMessageBox::Yes)
        return;
    m_cityQuery = m_cityEdit->text().trimmed();
    m_cityRadius = m_radiusCombo->currentData().toDouble();
    m_pendingArea = QRectF();
    m_pendingView = {};
    m_savePath.clear();
    m_loader->loadCity(m_cityQuery, m_cityRadius);
}

void MainWindow::setLoading(bool loading, const QString &message, bool error)
{
    m_loadBtn->setEnabled(!loading);
    m_busy->setVisible(loading);
    m_cityStatus->setVisible(!message.isEmpty());
    m_cityStatus->setText(message);
    m_cityStatus->setProperty("role", error ? "error" : "status");
    m_cityStatus->style()->unpolish(m_cityStatus);
    m_cityStatus->style()->polish(m_cityStatus);
    layoutOverlays();
}

void MainWindow::onCityLoaded(QSharedPointer<CityData> city)
{
    setLoading(false);
    m_currentLine = m_selectedStation = -1;
    m_metro->setCity(city);
    m_map->setCity(city);
    if (!m_pendingGame.isEmpty()) {
        m_metro->load(m_pendingGame);
        m_pendingGame = {};
    } else {
        m_metro->setSandbox(m_newSandbox); // nouvelle partie
    }
    m_routeActive = false;
    m_routeCard->hide();
    // partie sauvegardée sur une zone agrandie : on la reconstitue
    if (m_pendingArea.isValid() && m_pendingArea != city->area)
        QTimer::singleShot(0, this, [this, city] {
            m_loader->extendCity(city, m_pendingArea);
            m_map->setExtendBusy(true);
            m_pendingArea = QRectF();
        });
    m_welcome->hide();
    m_resumeBox->hide();
    m_loadBtn->setText({});
    m_loadBtn->setToolTip(tr("Charger la ville"));
    m_statsCard->show();
    m_dock->show();
    m_bestScore = QSettings().value(bestScoreKey(), 0).toDouble();
    refreshGoals();
    m_lineCard->hide();
    m_stationCard->hide();
    refreshAll();
    layoutOverlays();
    m_map->fitCity();
    setWindowTitle(tr("Metro Builder — %1").arg(city->name));
    m_toast->show(tr("%1 : %2 bâtiments · ~%3 habitants · ~%4 emplois")
                      .arg(city->name)
                      .arg(count(city->buildings.size()))
                      .arg(count(city->totalResidents))
                      .arg(count(city->totalJobs)),
                  false, true); // le carillon « nouvelle ligne » accompagne déjà le chargement
    applyPendingView();
    Audio::instance().play(Audio::NewLine);
}

void MainWindow::extendMap(int side)
{
    const QSharedPointer<CityData> city = m_metro->city();
    if (!city || m_loader->busy())
        return;
    QRectF area = city->area.isValid() ? city->area : city->bounds;
    const double step = 1000;
    switch (side) {
    case 0: area.setTop(area.top() - step); break;
    case 1: area.setRight(area.right() + step); break;
    case 2: area.setBottom(area.bottom() + step); break;
    case 3: area.setLeft(area.left() - step); break;
    default: return;
    }
    if (area.width() > 16000 || area.height() > 16000) {
        m_toast->show(tr("Taille maximale atteinte (16 km de côté)"), true);
        return;
    }
    static const char *names[] = {"le nord", "l'est", "le sud", "l'ouest"};
    m_toast->show(tr("Agrandissement vers %1…").arg(tr(names[side])));
    m_map->setExtendBusy(true);
    m_loader->extendCity(city, area);
}

void MainWindow::onCityExtended(QSharedPointer<CityData> city)
{
    setLoading(false);
    m_metro->updateCity(city);
    m_map->updateCity(city);
    m_map->setExtendBusy(false);
    Audio::instance().play(Audio::Good);
    m_toast->show(tr("Carte agrandie : %1 × %2 km · %3 bâtiments · ~%4 habitants")
                      .arg(QLocale(QLocale::French).toString(city->area.width() / 1000, 'f', 0))
                      .arg(QLocale(QLocale::French).toString(city->area.height() / 1000, 'f', 0))
                      .arg(count(city->buildings.size()))
                      .arg(count(city->totalResidents)),
                  false, true); // son « favorable » déjà joué
    refreshAll();
}

void MainWindow::tick()
{
    const double dt = m_clock.restart() / 1000.0;
    m_metro->advance(std::min(dt, 0.2), m_speed);
    if (m_metro->city()) {
        m_map->update();
        if (++m_frame % 8 == 0) {
            refreshStats();
            refreshGoals();
        }
        if (m_frame % 15 == 0)
            refreshFinance();
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void MainWindow::setTool(int tool)
{
    Audio::instance().play(Audio::Click);
    if (auto *b = m_toolGroup->button(tool))
        b->setChecked(true);
    m_map->setTool(MapView::Tool(tool));
    if (tool == MapView::BuildLine && !m_metro->line(m_currentLine))
        m_toast->show(tr("Sélectionnez une ligne en bas, ou créez-en une avec « + »"));
}

void MainWindow::newLine(bool letter)
{
    const QString code = m_metro->nextFreeCode(letter);
    if (code.isEmpty()) {
        m_toast->show(tr("Plus de lettre disponible"), true);
        return;
    }
    const int id = m_metro->addLine(code);
    if (id >= 0)
        Audio::instance().play(Audio::NewLine);
    if (id < 0)
        return;
    selectLine(id);
    setTool(MapView::BuildLine);
}

void MainWindow::selectLine(int lineId)
{
    m_currentLine = m_metro->line(lineId) ? lineId : -1;
    m_map->setCurrentLine(m_currentLine);
    m_lineCard->setVisible(m_currentLine >= 0);
    refreshLines();
    refreshLineEditor();
    refreshStation();
    layoutOverlays();
    m_layoutTimer.start(0);
}

void MainWindow::selectStation(int stationId)
{
    m_selectedStation = m_metro->station(stationId) ? stationId : -1;
    m_stationCard->setVisible(m_selectedStation >= 0);
    refreshStation();
    layoutOverlays();
    m_layoutTimer.start(0);
}

// ---------------------------------------------------------------------------
// Rafraîchissement
// ---------------------------------------------------------------------------

void MainWindow::refreshAll()
{
    if (!m_metro->line(m_currentLine))
        m_currentLine = -1;
    if (!m_metro->station(m_selectedStation))
        m_selectedStation = -1;
    m_lineCard->setVisible(m_currentLine >= 0 && m_metro->city());
    m_stationCard->setVisible(m_selectedStation >= 0 && m_metro->city());
    refreshStats();
    refreshLines();
    refreshLineEditor();
    refreshStation();
    refreshFinance();
    if (m_routeActive)
        showRoute(); // le réseau a changé : itinéraire recalculé
    refreshUndo();
    layoutOverlays();
    m_layoutTimer.start(0); // second passage une fois les nouveaux widgets stylés
}

void MainWindow::refreshStats()
{
    const double net = (m_metro->monthlyRevenue() - m_metro->monthlyCost()) / 1e6;
    m_moneyChip->setValue(m_metro->sandbox() ? tr("Bac à sable") : money(m_metro->money()));
    m_moneyChip->setSub(tr("%1%2 /mois").arg(net >= 0 ? "+" : "").arg(money(net)),
                        net >= 0 ? Theme::Success : Theme::Danger);
    const int month = m_metro->month() - 1;
    static const char *months[] = {"Janvier", "Février", "Mars",      "Avril",   "Mai",      "Juin",
                                   "Juillet", "Août",    "Septembre", "Octobre", "Novembre", "Décembre"};
    // « Semaine 1 » / « janvier, année 1 »
    m_dateChip->setValue(tr("Semaine %1").arg(m_metro->week()));
    m_dateChip->setSub(tr("%1, année %2").arg(tr(months[month % 12]).toLower()).arg(month / 12 + 1));
    m_ridersChip->setValue(tr("%1 /h").arg(compact(m_metro->totalServed())));
    m_ridersChip->setSub(tr("sur %1 dépl./h").arg(compact(m_metro->totalPotential())));
    m_captureChip->setValue(QStringLiteral("%1 %").arg(QLocale(QLocale::French).toString(m_metro->satisfaction() * 100, 'f', 1)));
    m_captureChip->setSub(tr("demande captée"));
    const double res = m_metro->city() ? m_metro->city()->totalResidents : 0;
    m_coverChip->setValue(res > 0 ? QStringLiteral("%1 %").arg(QLocale(QLocale::French).toString(
                                        m_metro->coveredResidents() / res * 100, 'f', 1))
                                  : QStringLiteral("—"));
    m_coverChip->setSub(tr("habitants desservis"));
}

void MainWindow::refreshLines()
{
    clearLayout(m_badgeLayout);
    for (const Line &l : m_metro->lines()) {
        // pas d'alerte « saturée » pour une ligne à l'arrêt (grève) : elle n'a simplement plus de capacité
        auto *b = new LineBadge(l.id, l.code, l.color, l.loadRatio() > 1 && m_metro->lineCapacityFactor(l.id) > 0);
        b->setChecked(l.id == m_currentLine);
        b->setToolTip(tr("<b>%1</b><br>%2 arrêts · %3 voyageurs/h%4")
                          .arg(l.name.toHtmlEscaped())
                          .arg(l.stops.size())
                          .arg(count(l.ridership))
                          .arg(l.loadRatio() > 1 ? tr("<br><span style='color:#FF8A96'>Saturée</span>") : QString()));
        const int id = l.id;
        connect(b, &LineBadge::clicked, this, [this, id] { selectLine(id == m_currentLine ? -1 : id); });
        m_badgeLayout->addWidget(b);
    }
    m_badgeBox->setVisible(!m_metro->lines().isEmpty());
}

void MainWindow::refreshLineEditor()
{
    const Line *l = m_metro->line(m_currentLine);
    if (!l)
        return;
    m_updating = true;
    if (!m_lineName->hasFocus())
        m_lineName->setText(l->name);
    m_lineColorBtn->setIcon(colorDot(l->color));
    m_lineCodeBtn->setLine(l->code, l->color);
    m_stopDelegate->setLine(l->color, l->isLoop());

    const int row = m_stopList->currentRow();
    m_stopList->clear();
    for (int k = 0; k < l->stops.size(); ++k) {
        const Station *s = m_metro->station(l->stops[k]);
        auto *it = new QListWidgetItem(s ? s->name : "?");
        it->setData(StopDelegate::StationIdRole, l->stops[k]);
        it->setData(StopDelegate::FirstRole, k == 0);
        it->setData(StopDelegate::LastRole, k == l->stops.size() - 1);
        if (s && l->segmentCount() > 0)
            it->setData(StopDelegate::SubtitleRole, tr("%1 /h").arg(compact(s->boardings)));
        m_stopList->addItem(it);
    }
    if (row >= 0 && row < m_stopList->count())
        m_stopList->setCurrentRow(row);
    m_stopList->setVisible(!l->stops.isEmpty());
    m_stopsEmpty->setVisible(l->stops.size() < 2);
    m_stopsEmpty->setText(l->stops.isEmpty()
                              ? tr("Choisissez l'outil « Tracer » puis cliquez des stations sur la carte "
                                   "(ou un lieu vide pour en créer une).")
                              : tr("Ajoutez au moins une autre station pour mettre la ligne en service."));

    if (auto *b = m_wagonGroup->button(l->wagons))
        b->setChecked(true);
    m_trainCount->setText(tr("%1 rame%2").arg(l->trains).arg(l->trains > 1 ? "s" : ""));
    m_loopBtn->setChecked(l->loop);
    m_loopBtn->setEnabled(l->stops.size() >= 3);
    m_updating = false;

    const bool active = l->segmentCount() > 0;
    const auto loc = QLocale(QLocale::French);
    m_kLength->setValue(active ? tr("%1 km").arg(loc.toString(l->length / 1000, 'f', 2)) : "—");
    m_kCycle->setValue(active ? tr("%1 min").arg(loc.toString(l->cycleMin, 'f', 0)) : "—");
    m_kHeadway->setValue(active ? tr("%1 min").arg(loc.toString(l->headwayMin, 'f', 1)) : "—");
    m_kCapacity->setValue(active ? compact(l->capacity) : "—");
    m_kRiders->setValue(active ? tr("%1 /h").arg(compact(l->ridership)) : "—");
    const double ratio = l->loadRatio();
    m_kLoad->setValue(active ? QStringLiteral("%1 %").arg(int(ratio * 100)) : "—");
    m_kLoad->setMeter(ratio, Theme::loadColor(ratio));

    QString advice;
    if (active && ratio > 1)
        advice = tr("⚠ Ligne saturée : ajoutez des voitures ou des rames.");
    else if (active && ratio < 0.25 && l->trains > 1)
        advice = tr("Ligne peu chargée : vous pouvez retirer des rames pour économiser.");
    m_lineAdvice->setText(advice);
    m_lineAdvice->setVisible(!advice.isEmpty());
}

void MainWindow::applyStopOrder()
{
    const Line *l = m_metro->line(m_currentLine);
    if (m_updating || !l || m_stopList->count() != l->stops.size())
        return;
    QVector<int> order;
    for (int i = 0; i < m_stopList->count(); ++i)
        order << m_stopList->item(i)->data(StopDelegate::StationIdRole).toInt();
    QVector<int> a = order, b = l->stops;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a == b && order != l->stops)
        m_metro->setStops(m_currentLine, order);
}

void MainWindow::refreshStation()
{
    const Station *s = m_metro->station(m_selectedStation);
    if (!s)
        return;
    if (!m_stationName->hasFocus())
        m_stationName->setText(s->name);

    clearLayout(m_stationLines);
    bool onCurrent = false, onAny = false;
    for (const Line &l : m_metro->lines()) {
        if (!l.stops.contains(s->id))
            continue;
        onCurrent |= l.id == m_currentLine;
        onAny = true;
        auto *chip = new QLabel(l.name);
        chip->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: 9px; padding: 2px 9px;"
                                           " font-weight: 600;")
                                .arg(l.color.name(), l.color.lightnessF() > 0.62 ? "#111" : "#fff"));
        m_stationLines->addWidget(chip);
    }
    if (!onAny)
        m_stationLines->addWidget(caption(tr("Non desservie — ajoutez-la à une ligne")));
    m_stationLines->addStretch();

    m_kPop->setValue(compact(s->catchPop));
    m_kJobs->setValue(compact(s->catchJobs));
    m_kDemand->setValue(tr("%1 /h").arg(compact(s->potential)));
    m_kBoard->setValue(tr("%1 /h").arg(compact(s->boardings)));
    const double share = s->potential > 0 ? s->served / s->potential : 0;
    m_kServed->setValue(tr("%1 /h  ·  %2 %").arg(compact(s->served)).arg(int(share * 100)));
    m_kServed->setMeter(share / Rules::CaptureTarget, Theme::Accent);

    const Line *cur = m_metro->line(m_currentLine);
    m_addToLineBtn->setEnabled(cur && !onCurrent);
    m_addToLineBtn->setText(cur ? tr("Ajouter à %1").arg(cur->name) : tr("Choisissez une ligne"));
}

// ---------------------------------------------------------------------------
// Sélecteurs (couleur prédéfinie, numéro ou lettre)
// ---------------------------------------------------------------------------

namespace {

QMenu *popupWith(QWidget *parent, QWidget *content)
{
    auto *menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    content->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.06); border: 2px solid transparent; border-radius: 8px;"
        " color: #E8EAED; font-weight: 600; }"
        "QToolButton:hover { background: rgba(76,141,255,0.35); }"
        "QToolButton:checked { background: #4C8DFF; color: white; }"
        "QToolButton:disabled { color: #4A4F57; background: transparent; }"
        "QLabel { color: #9AA0A6; font-size: 8pt; font-weight: 600; }");
    auto *wa = new QWidgetAction(menu);
    wa->setDefaultWidget(content);
    menu->addAction(wa);
    return menu;
}

} // namespace

void MainWindow::showColorPicker()
{
    const Line *line = m_metro->line(m_currentLine);
    if (!line)
        return;
    auto *content = new QWidget;
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(8, 6, 8, 8);
    lay->addWidget(new QLabel(tr("COULEUR DE LA LIGNE")));
    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    QMenu *menu = popupWith(this, content);
    const QVector<QColor> &colors = Metro::presetColors();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor col = colors[i];
        QStringList usedBy;
        for (const Line &o : m_metro->lines())
            if (o.id != line->id && o.color == col)
                usedBy << o.name;
        auto *b = new QToolButton;
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
        const bool current = col == line->color;
        b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border-radius: 15px; border: 3px solid %2; }"
                                        "QToolButton:hover { border-color: rgba(255,255,255,0.6); }")
                             .arg(col.name(), current ? "#FFFFFF" : "transparent"));
        b->setToolTip(usedBy.isEmpty() ? col.name() : tr("Déjà utilisée par %1").arg(usedBy.join(", ")));
        if (!usedBy.isEmpty()) // petite marque sur les couleurs déjà prises
            b->setText(QStringLiteral("•"));
        connect(b, &QToolButton::clicked, this, [this, col, menu] {
            m_metro->setLineColor(m_currentLine, col);
            menu->close();
        });
        grid->addWidget(b, i / 8, i % 8);
    }
    lay->addLayout(grid);
    menu->popup(m_lineColorBtn->mapToGlobal(QPoint(0, m_lineColorBtn->height() + 4)));
}

void MainWindow::showCodePicker()
{
    const Line *line = m_metro->line(m_currentLine);
    if (!line)
        return;
    auto *content = new QWidget;
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(8, 6, 8, 8);
    lay->setSpacing(6);
    QMenu *menu = popupWith(this, content);
    auto addGrid = [&](const QString &title, const QStringList &codes) {
        lay->addWidget(new QLabel(title));
        auto *grid = new QGridLayout;
        grid->setSpacing(4);
        for (int i = 0; i < codes.size(); ++i) {
            const QString code = codes[i];
            auto *b = new QToolButton;
            b->setText(code);
            b->setFixedSize(32, 30);
            b->setCheckable(true);
            b->setChecked(code == line->code);
            b->setEnabled(code == line->code || !m_metro->codeUsed(code, line->id));
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QToolButton::clicked, this, [this, code, menu] {
                m_metro->setLineCode(m_currentLine, code);
                menu->close();
            });
            grid->addWidget(b, i / 9, i % 9);
        }
        lay->addLayout(grid);
    };
    QStringList numbers, letters;
    for (int n = 1; n <= 18; ++n)
        numbers << QString::number(n);
    for (char ch = 'A'; ch <= 'Z'; ++ch)
        letters << QString(QChar(ch));
    addGrid(tr("NUMÉROS"), numbers);
    addGrid(tr("LETTRES"), letters);
    menu->popup(m_lineCodeBtn->mapToGlobal(QPoint(0, m_lineCodeBtn->height() + 4)));
}

// ---------------------------------------------------------------------------
// Sauvegarde / aide
// ---------------------------------------------------------------------------

QJsonObject MainWindow::gameState() const
{
    const QSharedPointer<CityData> city = m_metro->city();
    const QRectF area = city->area.isValid() ? city->area : city->bounds;
    const QPointF center = m_map->viewCenter();
    const QJsonObject view{{"cx", center.x()},
                           {"cy", center.y()},
                           {"scale", m_map->viewScale()},
                           {"overlay", int(m_map->overlay())},
                           {"schematic", m_map->schematic()},
                           {"speed", m_speed},
                           {"currentLine", m_currentLine},
                           {"financeRange", m_financeRange}};
    return QJsonObject{{"version", 2},
                       {"savedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
                       {"city", m_cityQuery},
                       {"cityName", city->name},
                       {"radius", m_cityRadius},
                       {"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                       {"month", m_metro->month()},
                       {"week", m_metro->week()},
                       {"money", m_metro->money()},
                       {"network", m_metro->save()},
                       {"view", view}};
}

bool MainWindow::writeGame(const QString &path, bool quiet)
{
    if (!m_metro->city() || m_cityQuery.isEmpty()) {
        if (!quiet)
            m_toast->show(tr("Chargez d'abord une ville par son nom"), true);
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); // écriture atomique : un plantage ne corrompt jamais l'ancienne sauvegarde
    if (!f.open(QIODevice::WriteOnly)) {
        m_toast->show(f.errorString(), true);
        return false;
    }
    f.write(QJsonDocument(gameState()).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        m_toast->show(tr("Échec de la sauvegarde : %1").arg(f.errorString()), true);
        return false;
    }
    return true;
}

void MainWindow::saveGame()
{
    if (m_savePath.isEmpty())
        return saveGameAs();
    if (writeGame(m_savePath, false))
        m_toast->show(tr("Partie sauvegardée dans %1").arg(QFileInfo(m_savePath).fileName()));
}

void MainWindow::saveGameAs()
{
    if (!m_metro->city() || m_cityQuery.isEmpty()) {
        m_toast->show(tr("Chargez d'abord une ville par son nom"), true);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Sauvegarder la partie"), m_savePath.isEmpty() ? m_cityQuery + ".metro" : m_savePath,
        tr("Partie Metro Builder (*.metro)"));
    if (path.isEmpty())
        return;
    m_savePath = path.endsWith(".metro") ? path : path + ".metro";
    if (writeGame(m_savePath, false))
        m_toast->show(tr("Partie sauvegardée dans %1").arg(QFileInfo(m_savePath).fileName()));
}

void MainWindow::loadGame()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Charger une partie"), {},
                                                      tr("Partie Metro Builder (*.metro)"));
    if (!path.isEmpty())
        openGame(path);
}

void MainWindow::openGame(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_toast->show(tr("Impossible d'ouvrir %1").arg(QFileInfo(path).fileName()), true);
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QString city = o.value("city").toString();
    const double radius = o.value("radius").toDouble(2);
    if (city.isEmpty()) {
        m_toast->show(tr("Fichier de partie invalide"), true);
        return;
    }
    // une sauvegarde automatique n'est pas le fichier « courant » de Ctrl+S
    m_savePath = path.startsWith(autosaveDir()) ? QString() : path;
    const QJsonArray a = o.value("area").toArray();
    m_pendingArea = a.size() == 4 ? QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()) : QRectF();
    m_pendingView = o.value("view").toObject();
    if (m_metro->city() && city == m_cityQuery && qFuzzyCompare(radius, m_cityRadius)) {
        m_metro->load(o.value("network").toObject());
        if (m_pendingArea.isValid() && m_pendingArea != m_metro->city()->area) {
            m_loader->extendCity(m_metro->city(), m_pendingArea);
            m_map->setExtendBusy(true);
        }
        m_pendingArea = QRectF();
        applyPendingView();
        m_toast->show(tr("Partie chargée"));
        return;
    }
    m_pendingGame = o.value("network").toObject();
    m_cityQuery = city;
    m_cityRadius = radius;
    m_cityEdit->setText(city);
    const int idx = m_radiusCombo->findData(radius);
    if (idx >= 0)
        m_radiusCombo->setCurrentIndex(idx);
    m_loader->loadCity(city, radius);
}

void MainWindow::openFile(const QString &path)
{
    if (!QFileInfo(path).isFile()) {
        m_toast->show(tr("Fichier introuvable : %1").arg(path), true);
        return;
    }
    openGame(QFileInfo(path).absoluteFilePath());
}

// Restaure vue, calque, vitesse et ligne sélectionnée d'une partie chargée
void MainWindow::applyPendingView()
{
    const QJsonObject v = m_pendingView;
    m_pendingView = {};
    if (v.isEmpty())
        return;
    if (auto *b = m_overlayGroup->button(v.value("overlay").toInt(MapView::Demand)))
        b->click();
    m_schemaBtn->setChecked(v.value("schematic").toBool());
    if (auto *b = m_speedGroup->button(qRound(v.value("speed").toDouble(1))))
        b->click();
    m_financeRange = v.value("financeRange").toInt(m_financeRange);
    if (!m_schemaBtn->isChecked())
        m_map->setView(QPointF(v.value("cx").toDouble(), v.value("cy").toDouble()), v.value("scale").toDouble());
    const int line = v.value("currentLine").toInt(-1);
    if (m_metro->line(line))
        selectLine(line);
}

// ---------------------------------------------------------------------------
// Sauvegarde automatique
// ---------------------------------------------------------------------------

QString MainWindow::autosaveDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave";
}

QString MainWindow::autosavePath() const
{
    QString slug = m_cityQuery.toLower().simplified();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "_");
    return QStringLiteral("%1/%2.metro").arg(autosaveDir(), slug.isEmpty() ? QStringLiteral("partie") : slug);
}

QString MainWindow::latestAutosave() const
{
    const QFileInfoList files =
        QDir(autosaveDir()).entryInfoList({"*.metro"}, QDir::Files, QDir::Time); // plus récente d'abord
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

void MainWindow::setAutosaveMinutes(int minutes)
{
    m_autosaveMinutes = minutes;
    QSettings().setValue("autosave/minutes", minutes);
    if (minutes > 0)
        m_autosaveTimer.start(minutes * 60000);
    else
        m_autosaveTimer.stop();
    m_toast->show(minutes > 0 ? tr("Sauvegarde automatique toutes les %1 min").arg(minutes)
                              : tr("Sauvegarde automatique désactivée"));
}

void MainWindow::autosave()
{
    if (!m_metro->city() || m_cityQuery.isEmpty() || m_loader->busy())
        return;
    if (writeGame(autosavePath(), true))
        m_toast->show(tr("Sauvegarde automatique ✓"), false, true); // discrète : pas de son
}

void MainWindow::refreshResume()
{
    m_resumePath = latestAutosave();
    m_resumeAction->setEnabled(!m_resumePath.isEmpty());
    if (m_resumePath.isEmpty() || m_metro->city()) {
        m_resumeBox->hide();
        return;
    }
    QFile f(m_resumePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_resumeBox->hide();
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    static const char *months[] = {"janvier", "février", "mars", "avril", "mai", "juin",
                                   "juillet", "août", "septembre", "octobre", "novembre", "décembre"};
    const int m = std::max(0, o.value("month").toInt(1) - 1);
    const QDateTime when = QDateTime::fromString(o.value("savedAt").toString(), Qt::ISODate);
    m_resumeLabel->setText(tr("<b>%1</b> — semaine %6, %2, année %3 · %4 M€<br>sauvegardée le %5")
                               .arg(o.value("cityName").toString(o.value("city").toString()).toHtmlEscaped())
                               .arg(tr(months[m % 12]))
                               .arg(m / 12 + 1)
                               .arg(QLocale(QLocale::French).toString(o.value("money").toDouble(), 'f', 1))
                               .arg(QLocale(QLocale::French).toString(when, "d MMMM 'à' HH:mm"))
                               .arg(o.value("week").toInt(1)));
    m_resumeBox->show();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_autosaveMinutes > 0)
        autosave(); // on ne perd jamais la partie en quittant
    QMainWindow::closeEvent(e);
}

void MainWindow::showHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Aide"));
    box.setIconPixmap(Icons::pixmap(Icons::Logo, 48, Qt::white));
    box.setText(tr("<h3>Metro Builder</h3>"
                   "<p><b>1</b> Sélection · <b>2</b> Station · <b>3</b> Tracer · <b>4</b> Démolir · <b>5</b> Itinéraire<br>"
                   "<b>Ctrl+Z / Ctrl+Y</b> annuler / rétablir (la construction annulée est remboursée) · <b>Ctrl+E</b> exporter le plan<br>"
                   "<b>N</b> nouvelle ligne numérotée · <b>Maj+N</b> ligne lettre · <b>Espace</b> pause · <b>F</b> recadrer · <b>M</b> plan schématique · <b>B</b> finances · <b>O</b> objectifs · <b>Échap</b> fermer<br>"
                   "<b>Suppr</b> démolir la station sélectionnée · <b>Ctrl+S / Ctrl+O</b> partie</p>"
                   "<p>En mode tracé : clic = ajouter en bout de ligne, Ctrl+clic = en tête, "
                   "clic droit = retirer l'arrêt. Réordonnez les arrêts par glisser-déposer dans le panneau de la ligne.</p>"
                   "<p><b>Points de passage</b> : glissez un tracé (outil Sélection ou Tracer) pour le courber, "
                   "glissez une poignée blanche pour la déplacer, clic droit dessus pour la supprimer. "
                   "Le tunnel supplémentaire est facturé au kilomètre.</p>"
                   "<p><b>Événements</b> : grèves, pannes, inondations, subventions, nouveaux quartiers… "
                   "Ceux qui demandent une décision mettent le jeu en pause.</p>"
                   "<p><b>Score et objectifs</b> : chaque mois rapporte des points (voyageurs, demande captée, "
                   "rentabilité) ; chaque objectif atteint donne des points et une prime, puis laisse place à un plus difficile. "
                   "Le meilleur score de chaque ville est conservé.</p>"
                   "<p><b>Itinéraire</b> (5) : cliquez un départ puis une arrivée pour voir le meilleur trajet "
                   "(marche, attente, métro, correspondances) et le comparer à la marche.</p>"
                   "<p><b>Bac à sable</b> : choisissez-le sur l'écran d'accueil (ou ☰ pour la partie en cours) : "
                   "construction gratuite, sans événements ni score.</p>"
                   "<p><b>Agrandir la carte</b> : boutons « 1 km » sur les bords de la zone de jeu, ou menu ☰.</p>"
                   "<p>Calque <b>demande</b> : rouge = déplacements non desservis, vert = captés par le métro.</p>"
                   "<p style='color:#9AA0A6'>Données © contributeurs OpenStreetMap (ODbL) · "
                   "tuiles © OpenMapTiles, servies par OpenFreeMap.</p>"));
    box.exec();
}
