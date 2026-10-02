#pragma once

#include <QWidget>

class QMenu;
class QAction;
class QButtonGroup;
class QStackedWidget;
class QVBoxLayout;
class QLabel;

// Menu du jeu en plein écran (bouton ☰) : fond de l'accueil et bloc central, rubriques à gauche,
// réglages à droite. Le contenu est lu dans un QMenu (rubriques = sous-menus) dont les actions gardent
// leurs raccourcis clavier ; boutons, interrupteurs et choix segmentés restent synchronisés avec elles.
class GameMenu : public QWidget
{
    Q_OBJECT
public:
    GameMenu(QMenu *model, QWidget *parent);
    void open(const QString &subtitle);
    void close();
    bool isOpen() const { return isVisible(); }

signals:
    void opened();
    void closed();

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QWidget *buildPage(QMenu *menu);
    void addItems(QMenu *menu, QVBoxLayout *lay);
    QWidget *actionRow(QAction *a);
    QWidget *toggleRow(QAction *a);
    QWidget *choiceRow(const QList<QAction *> &group);

    QWidget *m_panel;
    QLabel *m_subtitle;
    QButtonGroup *m_nav;
    QStackedWidget *m_pages;
};
