#include "GameMenu.h"
#include "Ui.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

const char *kStyle = R"(
    #menuPanel QToolButton#nav {
        text-align: left; padding: 10px 14px; border: none; border-radius: 10px;
        color: #C9CDD2; background: transparent; font-weight: 600;
    }
    #menuPanel QToolButton#nav:hover { background: rgba(255,255,255,0.06); }
    #menuPanel QToolButton#nav:checked { background: rgba(76,141,255,0.18); color: white; }
    #menuPanel QPushButton#item {
        text-align: left; padding: 0; border: none; border-radius: 10px; background: rgba(255,255,255,0.05);
        min-height: 40px;
    }
    #menuPanel QPushButton#item[twoLines="true"] { min-height: 54px; }
    #menuPanel QPushButton#item:hover { background: rgba(255,255,255,0.10); }
    #menuPanel QPushButton#item:disabled { background: rgba(255,255,255,0.02); }
    #menuPanel QPushButton#item:checked { background: rgba(76,141,255,0.16); }
    #menuPanel QLabel#itemText { color: #E8EAED; }
    #menuPanel QLabel#itemHint { color: #7D8597; }
    #menuPanel QLabel#group { color: #9AA0A6; font-weight: 700; letter-spacing: 1px; }
)";

// texte d'une action sans les esperluettes de raccourci
QString clean(const QString &t)
{
    QString s = t;
    s.remove('&');
    return s;
}

QLabel *groupTitle(const QString &text)
{
    auto *l = new QLabel(clean(text).toUpper());
    l->setObjectName("group");
    l->setContentsMargins(2, 10, 0, 2);
    return l;
}

} // namespace

GameMenu::GameMenu(QMenu *model, QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    hide();

    auto *card = new Card(this, 20);
    m_panel = card;
    card->setObjectName("menuPanel");
    card->setStyleSheet(kStyle);
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(Card::Shadow + 22, Card::Shadow + 18, Card::Shadow + 22, Card::Shadow + 20);
    lay->setSpacing(14);

    // en-tête : logo, titre, partie en cours, fermer
    auto *head = new QHBoxLayout;
    head->setSpacing(14);
    auto *logo = new QLabel;
    logo->setPixmap(Icons::pixmap(Icons::Logo, 44, Qt::white));
    head->addWidget(logo);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto *title = new QLabel(tr("Metro Builder"));
    title->setProperty("role", "title");
    m_subtitle = new QLabel;
    m_subtitle->setProperty("role", "subtitle");
    titles->addWidget(title);
    titles->addWidget(m_subtitle);
    head->addLayout(titles, 1);
    auto *closeBtn = iconButton(Icons::Close, tr("Fermer le menu — Échap"), false, 36);
    connect(closeBtn, &QToolButton::clicked, this, &GameMenu::close);
    head->addWidget(closeBtn, 0, Qt::AlignTop);
    lay->addLayout(head);

    // corps : rubriques à gauche, contenu à droite
    auto *body = new QHBoxLayout;
    body->setSpacing(18);
    auto *navCol = new QVBoxLayout;
    navCol->setSpacing(4);
    m_nav = new QButtonGroup(this);
    m_pages = new QStackedWidget;
    int index = 0;
    for (QAction *a : model->actions()) {
        QMenu *sub = a->menu();
        if (!sub)
            continue;
        auto *b = new QToolButton;
        b->setObjectName("nav");
        b->setText(clean(sub->title()));
        b->setIcon(sub->icon());
        b->setIconSize(QSize(20, 20));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumWidth(170);
        m_nav->addButton(b, index++);
        navCol->addWidget(b);
        auto *scroll = new QScrollArea;
        scroll->setWidget(buildPage(sub));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
        m_pages->addWidget(scroll);
    }
    connect(m_nav, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
    if (auto *first = m_nav->button(0))
        first->setChecked(true);
    navCol->addStretch();
    auto *resume = new QPushButton(Icons::icon(Icons::Play, Qt::white, Qt::white), tr("Reprendre"));
    resume->setProperty("variant", "primary");
    resume->setCursor(Qt::PointingHandCursor);
    connect(resume, &QPushButton::clicked, this, &GameMenu::close);
    navCol->addWidget(resume);
    body->addLayout(navCol);
    auto *sep = new QFrame;
    sep->setFixedWidth(1);
    sep->setStyleSheet("background: rgba(255,255,255,0.08);");
    body->addWidget(sep);
    body->addWidget(m_pages, 1);
    lay->addLayout(body, 1);
}

QWidget *GameMenu::buildPage(QMenu *menu)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 8, 0);
    lay->setSpacing(6);
    addItems(menu, lay);
    lay->addStretch();
    return page;
}

// Une rubrique : actions simples (boutons), interrupteurs, choix exclusifs (segments), curseurs,
// et sous-menus rendus comme des groupes titrés
void GameMenu::addItems(QMenu *menu, QVBoxLayout *lay)
{
    const QList<QAction *> actions = menu->actions();
    for (int i = 0; i < actions.size(); ++i) {
        QAction *a = actions[i];
        if (!a->isVisible())
            continue;
        if (a->isSeparator()) {
            if (!a->text().isEmpty())
                lay->addWidget(groupTitle(a->text()));
            else
                lay->addSpacing(8);
            continue;
        }
        if (QMenu *sub = a->menu()) {
            lay->addWidget(groupTitle(sub->title()));
            addItems(sub, lay);
            continue;
        }
        if (auto *wa = qobject_cast<QWidgetAction *>(a)) {
            // curseur de volume : un nouveau curseur, relié au même réglage que celui du menu
            const QString label = wa->property("label").toString();
            auto *row = new QWidget;
            auto *rl = new QHBoxLayout(row);
            rl->setContentsMargins(12, 2, 12, 2);
            auto *text = new QLabel(label);
            text->setObjectName("itemHint");
            text->setMinimumWidth(170);
            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 100);
            slider->setValue(wa->property("value").toInt());
            connect(slider, &QSlider::valueChanged, wa, [wa](int v) {
                wa->setProperty("value", v);
                if (auto *original = wa->defaultWidget()->findChild<QSlider *>())
                    original->setValue(v);
            });
            rl->addWidget(text);
            rl->addWidget(slider, 1);
            lay->addWidget(row);
            continue;
        }
        // choix exclusifs consécutifs (même groupe) : une rangée de segments
        if (a->isCheckable() && a->actionGroup() && a->actionGroup()->isExclusive()) {
            QList<QAction *> group{a};
            while (i + 1 < actions.size() && actions[i + 1]->actionGroup() == a->actionGroup())
                group << actions[++i];
            lay->addWidget(choiceRow(group));
            continue;
        }
        lay->addWidget(a->isCheckable() ? toggleRow(a) : actionRow(a));
    }
}

// Bouton d'action : icône, libellé, raccourci à droite ; ferme le menu puis déclenche l'action
QWidget *GameMenu::actionRow(QAction *a)
{
    auto *b = new QPushButton;
    b->setObjectName("item");
    b->setCursor(Qt::PointingHandCursor);
    auto *rl = new QHBoxLayout(b);
    rl->setContentsMargins(12, 8, 14, 8);
    rl->setSpacing(10);
    auto *icon = new QLabel;
    icon->setPixmap(a->icon().pixmap(18, 18));
    icon->setFixedWidth(20);
    auto *text = new QLabel(clean(a->text()));
    text->setObjectName("itemText");
    // description (infobulle de l'action) sous le libellé, en entier ; raccourci clavier à droite
    auto *desc = new QLabel;
    desc->setObjectName("itemHint");
    auto *hint = new QLabel(a->shortcut().toString(QKeySequence::NativeText));
    hint->setObjectName("itemHint");
    auto *col = new QVBoxLayout;
    col->setSpacing(1);
    col->addWidget(text);
    col->addWidget(desc);
    for (QLabel *l : {icon, text, desc, hint})
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
    rl->addWidget(icon);
    rl->addLayout(col, 1);
    rl->addWidget(hint);
    auto update = [a, b, icon, text, desc] {
        b->setEnabled(a->isEnabled());
        icon->setPixmap(a->icon().pixmap(18, 18));
        text->setText(clean(a->text()));
        const bool hasDesc = !a->toolTip().isEmpty() && a->toolTip() != clean(a->text());
        desc->setText(hasDesc ? a->toolTip() : QString());
        desc->setVisible(hasDesc);
        b->setProperty("twoLines", hasDesc); // deux lignes : libellé et description (feuille de style)
    };
    update();
    connect(a, &QAction::changed, b, update);
    connect(b, &QPushButton::clicked, this, [this, a] {
        close();
        a->trigger();
    });
    return b;
}

// Interrupteur : reste ouvert, l'état suit celui de l'action
QWidget *GameMenu::toggleRow(QAction *a)
{
    auto *b = new QPushButton;
    b->setObjectName("item");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    auto *rl = new QHBoxLayout(b);
    rl->setContentsMargins(12, 8, 14, 8);
    rl->setSpacing(10);
    auto *icon = new QLabel;
    icon->setPixmap(a->icon().pixmap(18, 18));
    icon->setFixedWidth(20);
    auto *text = new QLabel(clean(a->text()));
    text->setObjectName("itemText");
    auto *state = new QLabel;
    for (QLabel *l : {icon, text, state}) {
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
        rl->addWidget(l);
    }
    rl->insertStretch(2, 1);
    auto sync = [a, b, state] {
        b->setChecked(a->isChecked());
        state->setText(a->isChecked() ? tr("Activé") : tr("Désactivé"));
        state->setStyleSheet(a->isChecked() ? "color: #34C77B; font-weight: 700;" : "color: #7D8597;");
    };
    sync();
    connect(a, &QAction::toggled, b, sync);
    connect(b, &QPushButton::clicked, a, [a, sync] {
        a->trigger();
        sync();
    });
    return b;
}

// Choix exclusif : segments côte à côte (ou en colonne s'ils sont longs)
QWidget *GameMenu::choiceRow(const QList<QAction *> &group)
{
    auto *row = new QWidget;
    int textLen = 0;
    for (QAction *a : group)
        textLen += clean(a->text()).size();
    QBoxLayout *rl = textLen > 48 ? static_cast<QBoxLayout *>(new QVBoxLayout(row))
                                  : static_cast<QBoxLayout *>(new QHBoxLayout(row));
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(4);
    for (QAction *a : group) {
        auto *b = new QToolButton;
        b->setText(clean(a->text()));
        b->setCheckable(true);
        b->setChecked(a->isChecked());
        b->setProperty("variant", "segment");
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(34);
        connect(a, &QAction::toggled, b, &QToolButton::setChecked);
        connect(b, &QToolButton::clicked, a, [a, b] {
            if (!a->isChecked())
                a->trigger();
            b->setChecked(a->isChecked());
        });
        rl->addWidget(b);
    }
    return row;
}

void GameMenu::open(const QString &subtitle)
{
    m_subtitle->setText(subtitle);
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
    emit opened();
}

void GameMenu::close()
{
    if (!isVisible())
        return;
    hide();
    emit closed();
}

void GameMenu::resizeEvent(QResizeEvent *)
{
    // bloc central : largeur et hauteur bornées, centré
    const int w = std::min(860, width() - 40), h = std::min(600, height() - 40);
    m_panel->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
}

void GameMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    Theme::paintBackdrop(p, rect());
}

void GameMenu::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
        close();
    else
        QWidget::keyPressEvent(e);
}
