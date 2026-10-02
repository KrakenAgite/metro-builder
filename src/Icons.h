#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

// Icônes vectorielles dessinées au QPainter sur une grille 24×24 (trait arrondi),
// pour ne dépendre d'aucun fichier ni du module Qt SVG.
namespace Icons {

enum Id {
    Pointer, Station, Route, Trash,
    Flame, Users, Gauge, EyeOff,
    Pause, Play, Fast, Faster,
    Recenter, Search, Menu, Plus, Minus,
    ArrowUp, ArrowDown, Close, Swap, Loop,
    Wallet, Calendar, Train, Target, Home, Briefcase,
    Save, FolderOpen, File, Download, Info, Logo, Moon, Sun, Schema, Speaker, SpeakerOff, Trophy,
    Undo, Redo, Directions, Image, Sandbox, ChevronLeft, ChevronRight,
};

QPixmap pixmap(Id id, int size, const QColor &color);

// Icône d'un bouton : gris au repos, blanc au survol ou une fois cochée.
QIcon icon(Id id, const QColor &normal = QColor("#C9CDD2"), const QColor &active = Qt::white);

// Petite rame stylisée de n voitures (sélecteur de longueur des trains)
QIcon wagons(int count);

// Logo de l'application en plusieurs tailles (icône de fenêtre)
QIcon appIcon();

} // namespace Icons
