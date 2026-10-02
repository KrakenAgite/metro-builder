# Metro Builder (C++ / Qt 6)

<img src="assets/logo.svg" width="96" alt="Logo Metro Builder">

Jeu de construction de réseau de métro sur une vraie ville issue d'OpenStreetMap.

## Compiler

Dépendances : Qt 6 (Widgets, Network, Concurrent), CMake ≥ 3.16, compilateur C++17, et facultativement
`libpulse-dev` pour le son (PulseAudio ou PipeWire ; sans elle, le jeu se compile sans son).

```bash
sudo apt install qt6-base-dev cmake g++ libpulse-dev   # Debian / Ubuntu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/metrobuilder
```

### Version Windows (compilation croisée depuis Linux)

`win-toolchain/` contient le compilateur [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20231128 et
Qt 6.8.2 pour Windows (`win64_llvm_mingw`, depuis download.qt.io) ; les outils Qt (moc) viennent du Qt 6.8.2
installé sous Linux. Puis :

```bash
scripts/build-windows.sh
```

Le script produit `dist/MetroBuilder-windows/` (exécutable, DLL Qt et C++, plugins `platforms` et `tls`) et
`dist/MetroBuilder-<version>-windows-x64.zip` : à décompresser sous Windows 10/11 64 bits, puis lancer `metrobuilder.exe`.
Le son passe par waveOut (winmm) sous Windows.

### Paquet Debian

```bash
scripts/build-deb.sh                                  # → dist/metrobuilder_<version>_amd64.deb
sudo apt install ./dist/metrobuilder_1.6.0_amd64.deb   # puis « Metro Builder » dans le menu des jeux
```

Le paquet associe aussi les sauvegardes `.metro` au jeu : elles ont leur propre icône (document au logo) et
s'ouvrent dans Metro Builder par double-clic (`metrobuilder partie.metro` en ligne de commande). Sous Windows,
l'association est enregistrée pour l'utilisateur au premier lancement de `metrobuilder.exe`.

Les dépendances (Qt 6, PulseAudio, OpenSSL) sont calculées par `dpkg-shlibdeps` ; paquet prévu pour
Debian 13 (trixie) ou une distribution aussi récente.

### Releases

Les paquets prêts à installer (`.deb` pour Debian, `.zip` pour Windows 64 bits) sont attachés aux
[releases GitHub](https://github.com/KrakenAgite/metro-builder/releases). Pour en publier une : changer
`VERSION` dans `CMakeLists.txt`, lancer les deux scripts, puis
`gh release create v<version> dist/*.deb dist/*.zip`.

## Jouer

L'interface est une carte plein écran avec des panneaux flottants :

| Zone | Contenu |
|---|---|
| Haut gauche | Recherche de ville, rayon, chargement, menu ☰ (fichier Overpass, sauvegarde, aide) |
| Haut droite | Indicateurs : budget et bilan mensuel, date, voyageurs/h, demande captée, habitants desservis |
| Bas (dock) | Outils · pastilles des lignes + « nouvelle ligne » · calques · vitesse · recadrer · carte claire/sombre |
| Bas gauche | Éditeur de la ligne sélectionnée (arrêts façon plan de ligne, voitures, rames, boucle, indicateurs) |
| Droite | Fiche de la station sélectionnée |

1. Tapez une ville (« Lyon », « Bordeaux », « Paris 11e »…), choisissez le rayon puis **Charger**.
   Le géocodage passe par Nominatim ; rues, bâtiments, eau et parcs viennent des tuiles vectorielles
   OpenStreetMap d'[OpenFreeMap](https://openfreemap.org) (format Mapbox Vector Tile, servi par un CDN :
   ~1 s pour 3 km de rayon). En secours, l'API Overpass est interrogée par zones en parallèle.
   Géocodage et tuiles sont mis en cache (`~/.cache/MetroBuilder/`) : une ville déjà vue se recharge sans réseau.
2. **Station (2)** : clic sur la carte → la station s'accroche à la rue la plus proche et prend son nom.
   Le cercle montre la zone desservie à pied (300 m = desserte complète, 800 m = limite).
3. **+** dans le dock crée une ligne numérotée (**N** : 1, 2, 3…) ou désignée par une lettre (**Maj+N** : A, B…)
   et passe en mode **Tracer (3)**. Dans l'éditeur de ligne, la pastille permet de changer de numéro/lettre et la
   pastille de couleur propose une palette de couleurs prédéfinies. En mode tracé, cliquez les stations
   dans l'ordre (un clic dans le vide crée une station). Ctrl+clic ajoute en tête, clic droit retire l'arrêt.
4. **Points de passage** : avec l'outil Sélection ou Tracer, glissez un tracé pour créer un point de
   passage et courber la ligne (contourner un fleuve, desservir un quartier…). Glissez une poignée blanche
   pour la déplacer, clic droit (ou outil Démolir) pour la supprimer. Les tracés sont des courbes lissées
   passant par les stations et les points de passage ; le tunnel est facturé sur la longueur réelle de la
   courbe, et les rames suivent la courbe voiture par voiture. Retirer une station conserve la forme du tunnel.
5. Dans l'éditeur de ligne : réordonner les arrêts (glisser-déposer, ↑/↓, inverser), longueur des
   trains (3 à 5 voitures), nombre de rames, ligne circulaire.
6. Calques (barre du bas) :
   - **Demande** : rouge = déplacements non desservis → vert = captés par le métro ; l'intensité suit le volume.
   - **Habitants** / **Emplois** : densités par hectare en paliers de couleurs vives (adaptés à chaque ville),
     courbes de niveau et bâtiments colorés selon la densité de leur quartier ; la valeur sous le curseur s'affiche.
   - **Charge des lignes** : épaisseur et couleur selon le rapport charge/capacité.
7. **Agrandir la carte** : les boutons « 1 km » au milieu de chaque bord de la zone de jeu (ou ☰ → Agrandir la
   carte) l'étendent d'1 km vers le nord, l'est, le sud ou l'ouest (jusqu'à 16 km de côté). Seules les tuiles
   manquantes sont téléchargées, le réseau est conservé et la demande recalculée ; la zone est enregistrée
   dans les sauvegardes.
8. **Finances** (bouton portefeuille du dock, touche **B** ou clic sur le budget) : trésorerie, recettes et
   coûts d'exploitation par mois, résultat d'exploitation, investissements, fréquentation et demande captée
   sur la période choisie (6 mois, 1 an, 2 ans, 5 ans ou toute la partie ; survol = valeurs du mois), et rentabilité de chaque ligne. L'historique est
   enregistré dans les sauvegardes.
   **Score et objectifs** (bouton trophée du dock ou **O**) : trois objectifs à la fois (stations, lignes,
   voyageurs, demande captée, habitants desservis, correspondances, kilomètres de lignes, trésorerie, mois
   bénéficiaires d'affilée, chiffre d'affaires). Chacun rapporte des points et une prime, puis cède la place au
   palier suivant d'une autre famille. Chaque fin de mois ajoute aussi des points (voyageurs/h ÷ 40, demande
   captée, bonus si l'exploitation est rentable). Score et objectifs sont sauvegardés ; le record de chaque ville
   est conservé.
9. **Plan schématique** (bouton plan du dock ou **M**) : le réseau redessiné comme un plan de métro — tronçons
   à 0°/45°/90°, inter-stations régulières sur une grille, correspondances marquées, noms placés sans chevaucher
   les tracés, légende des lignes et rames animées. On peut y sélectionner, tracer avec des stations existantes
   ou démolir ; la construction de nouvelles stations se fait sur la carte.
   **Exporter le plan** (☰ ou **Ctrl+E**) : PNG haute définition ou PDF A3 paysage, avec titre et légende ;
   ☰ → Capture de la carte enregistre la vue géographique.
10. **Itinéraire (5)** : cliquez un départ puis une arrivée : meilleur trajet (marche jusqu'à la station, attente,
    lignes, correspondances, marche finale) surligné sur la carte, durée comparée à la marche. Il se recalcule
    quand le réseau change ; clic droit pour l'effacer.
11. **Annuler / rétablir** (**Ctrl+Z** / **Ctrl+Y**, ou ☰) : toute modification du réseau (stations, lignes,
    arrêts, rames, tracés, noms, couleurs) ; la construction annulée est intégralement remboursée.
12. **Bac à sable** : à choisir sur l'écran d'accueil (« Carrière » / « Bac à sable ») ou ☰ pour la partie en
    cours (sans retour) : construction gratuite, ni événements ni score ; le mode est enregistré dans la sauvegarde.
13. **Gestion du réseau** (panneau Finances, **B**) :
    - **Prix du ticket** (1 à 4 €) : plus cher, chaque voyage rapporte plus mais la fréquentation baisse
      (environ −40 % par euro) ; la recette est maximale vers 1,80 €.
    - **Subvention de la ville** : jusqu'à 40 % des coûts d'exploitation, d'autant plus que le métro capte la demande.
    - **Emprunts** de 100, 250 ou 500 M€ à 4 %/an sur 10 ans (encours maximal 1 000 M€), remboursables d'un coup.
    - **Entretien** réduit / normal / renforcé : coût des rames (−15 % / +25 %) contre vitesse d'usure et pannes.
14. **Exploitation des lignes** (fiche de ligne) : **heures creuses** à 100, 75 ou 50 % des rames (moins de coûts,
    un peu moins de voyageurs hors pointe) ; **âge du matériel** : après 5 ans, des rames tombent en panne
    (capacité −40 % pendant 2 semaines) ; **Renouveler** remplace tout le matériel pour 60 % du prix neuf.
15. **Ville qui évolue** : chaque mois les quartiers desservis se densifient (jusqu'à +0,5 %/mois là où le métro
    capte bien la demande, plafonné à 2,5 fois la densité d'origine) et toute la ville croît lentement ;
    bilan démographique chaque année et courbe de population dans les Finances.

**Défis**
- **Scénarios** (écran d'accueil ou ☰ → Scénarios) : six missions sur de grandes villes — Paris (avec le vrai
  métro au départ), Londres, Berlin, Madrid, Manhattan et Shinjuku. Chacune fixe un budget, une échéance et des
  conditions de victoire (voyageurs transportés, habitants desservis, demande captée, correspondances, mois
  bénéficiaires, aucune ligne saturée…). Les objectifs de voyageurs sont proportionnels à la demande de la ville.
  Réussite en moins de la moitié du temps : 3 étoiles, des trois quarts : 2, sinon 1 ; le meilleur résultat
  est conservé. La mission s'affiche à la place des objectifs (bouton trophée).
- **Métro réel** (☰ → Importer le métro réel) : lignes `route=subway` d'OpenStreetMap via Overpass (mises en
  cache), arrêts dans la zone de jeu regroupés par nom en stations de correspondance, couleurs ramenées à la
  palette du jeu. Le réseau importé remplace le réseau actuel et n'est pas facturé.
- **Succès** (accueil ou ☰ → Succès) : 22 succès conservés d'une partie à l'autre (première ligne, ligne
  circulaire, station à 4 lignes, 10 000 voyageurs/h, 90 % des habitants desservis, 2 000 M€, dette remboursée,
  dix ans de jeu, scénarios en 3 étoiles, tour du monde des capitales…).

**Ambiance**
- **Jour et nuit** : l'horloge (affichée sous la date) avance d'une minute par seconde à ×1, la partie commence
  à 7 h. Au crépuscule la carte bleuit puis s'assombrit, les fenêtres s'allument et les stations rayonnent ;
  le métro ferme de 1 h à 5 h, roule à pleine fréquence aux heures de pointe (7 h – 9 h 30, 16 h 30 – 19 h 30)
  et selon le réglage « heures creuses » de chaque ligne le reste du temps. Désactivable dans ☰.
- **Vue en coupe** (fiche de ligne → « Vue en coupe ») : profil de la ligne avec le sol, les cours d'eau, les
  tunnels (12 m, 26 m sous un fleuve) et les viaducs sur piliers. Un clic sur un inter-station bascule tunnel
  ↔ viaduc : le viaduc coûte 45 % de moins (un pont 10 % de moins), un tunnel sous l'eau 60 % de plus ; mais
  un viaduc freine la densification des quartiers qu'il traverse. Les viaducs sont dessinés sur la carte
  (tablier et piliers).

**Confort**
- **Tutoriel** : proposé automatiquement à la première partie (et ☰ → Tutoriel) : 7 étapes guidées — construire
  des stations, tracer une ligne, lire la carte, régler les trains, le budget, les objectifs. Un anneau doré
  désigne le bouton à utiliser et l'étape suivante arrive d'elle-même quand l'action est faite.
- **Langue** : ☰ → Langue / Language : français ou anglais (par défaut, la langue du système). Le jeu redémarre
  pour changer de langue, la partie est sauvegardée automatiquement. Les textes sont dans
  `translations/metrobuilder_en.ts` (à recompiler avec `lrelease translations/metrobuilder_en.ts`), le fichier
  `.qm` est intégré à l'exécutable.

**Calendrier** : le temps avance par semaines (« Semaine 2 — mars, année 1 ») ; une semaine dure 10 s à
vitesse ×1 et un mois compte 4 semaines. Bilans et graphiques financiers restent mensuels ; la durée des
événements se décompte en semaines.

**Événements aléatoires** : environ deux par an (20 % de chance par fin de mois, au moins 2 mois de calme entre
deux, 2 événements actifs au plus ; ~7 sur 10 sont favorables et jamais deux coups durs d'affilée),
la ville réagit — grève ou panne sur une ligne, station inondée, flambée de l'énergie, pénurie de matériaux,
usagers en colère si une ligne est saturée ; mais aussi concerts et matchs qui font bondir la demande locale,
pics de pollution, subventions (selon la demande captée), rabais sur les travaux, nouveaux quartiers qui
ajoutent durablement habitants et emplois, et offres de mécénat contre le nom d'une station. Les événements
à choix mettent le jeu en pause ; les événements en cours sont listés à gauche (clic = centrer la carte) et
signalés sur la carte. Leur moteur est dans `src/Events.cpp`.

**Son** : musique d'ambiance calme générée en continu (nappe lente qui respire, accords tenus 8 s, basse douce, notes de piano feutré espacées dans un écho sombre) et bruitages synthétisés
(station, arrêt, carillon de nouvelle ligne, démolition, recettes, événements, erreurs), tous calculés en code
(`src/Audio.cpp`, aucun fichier son). Bouton haut-parleur du dock pour couper, ☰ → Son pour la musique, les
bruitages et leurs volumes.

**Sauvegardes** : une partie enregistre tout — ville et zone (agrandissements compris), stations, lignes,
points de passage, rames, budget, historique mensuel et cumuls (chiffre d'affaires, exploitation,
investissements), horloge des rames, vue, calque, vitesse et ligne sélectionnée. **Ctrl+S** réenregistre dans
le fichier courant, **Ctrl+Maj+S** « sauvegarder sous ». La **sauvegarde automatique** (☰ → Sauvegarde
automatique, toutes les 2 min par défaut, et toujours à la fermeture) garde une partie par ville dans
`~/.local/share/MetroBuilder/MetroBuilder/autosave/` ; l'écran d'accueil propose de **reprendre** la dernière.

Raccourcis : 1-4 outils · N nouvelle ligne numérotée · Maj+N ligne lettre · Espace pause · F recadrer · D carte sombre · M plan schématique · B finances · Échap fermer un panneau ·
Suppr démolir la station sélectionnée · Ctrl+S / Ctrl+O · F1 aide.

Les icônes sont dessinées en code (`src/Icons.cpp`) et le thème sombre est défini dans `src/Ui.cpp`.

## Modèle de simulation (src/Metro.cpp)

- Chaque bâtiment OSM reçoit des habitants et/ou des emplois selon son type, sa surface, son nombre
  d'étages (`building:levels`) et l'occupation du sol environnante. Ces valeurs sont agrégées sur une grille de 100 m.
- Chaque cellule génère des déplacements à l'heure de pointe et se rattache à la station active
  la plus accessible à pied.
- Graphe (station, ligne) + Dijkstra : temps de trajet = attente (½ intervalle) + roulage + arrêts
  + correspondances.
- Demande origine/destination calculée par un modèle gravitaire, puis part modale du métro selon le gain de
  temps face au bus ou à la voiture (les trajets de moins de 1 km se font à pied).
- Affectation sur chaque inter-station et dans chaque sens → charge comparée à la capacité
  (= trains/h × voitures × 140 voyageurs). Une ligne saturée perd des voyageurs.
- Budget : construction (stations, tunnel au km, voitures) et bilan mensuel recettes/exploitation. Les recettes
  par voyage sont multipliées par 4 (`Rules::RevenueBoost`) pour compenser le mois de 4 semaines (40 s à ×1).
  Toutes les constantes sont réglables dans `namespace Rules` (src/Metro.h).

Données © contributeurs OpenStreetMap, licence ODbL · tuiles © OpenMapTiles, servies par OpenFreeMap.
