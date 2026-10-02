#!/usr/bin/env bash
# Construit le paquet Debian dist/metrobuilder_<version>_<arch>.deb (dépendances calculées par dpkg-shlibdeps).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(MetroBuilder VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCH=$(dpkg --print-architecture)
STAGE=build-deb/metrobuilder_${VERSION}_${ARCH}

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build build-release -j"$(nproc)"

rm -rf build-deb
install -Dm755 build-release/metrobuilder "$STAGE/usr/bin/metrobuilder"
strip --strip-unneeded "$STAGE/usr/bin/metrobuilder"
install -Dm644 assets/metrobuilder.desktop "$STAGE/usr/share/applications/metrobuilder.desktop"
install -Dm644 assets/metrobuilder.png "$STAGE/usr/share/icons/hicolor/256x256/apps/metrobuilder.png"
install -Dm644 assets/logo.svg "$STAGE/usr/share/icons/hicolor/scalable/apps/metrobuilder.svg"
# sauvegardes .metro : type MIME, icône de document, ouverture par le jeu (caches mis à jour par les triggers dpkg)
install -Dm644 assets/metrobuilder-mime.xml "$STAGE/usr/share/mime/packages/metrobuilder.xml"
install -Dm644 assets/metrobuilder-save.png "$STAGE/usr/share/icons/hicolor/256x256/mimetypes/application-x-metrobuilder-save.png"
install -Dm644 README.md "$STAGE/usr/share/doc/metrobuilder/README.md"

# dépendances des bibliothèques liées (Qt 6, PulseAudio…)
mkdir -p build-deb/debian
printf 'Source: metrobuilder\n\nPackage: metrobuilder\nArchitecture: any\n' > build-deb/debian/control
DEPENDS=$(cd build-deb && dpkg-shlibdeps -O "../$STAGE/usr/bin/metrobuilder" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
# le plugin TLS de Qt (dans libqt6network6) charge OpenSSL à l'exécution pour les téléchargements HTTPS
DEPENDS="$DEPENDS, libssl3t64 | libssl3"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: metrobuilder
Version: $VERSION
Section: games
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Recommends: qt6-wayland
Installed-Size: $(du -sk "$STAGE/usr" | cut -f1)
Maintainer: Gabriel Arthus <120360026+KrakenAgite@users.noreply.github.com>
Homepage: https://github.com/KrakenAgite/metro-builder
Description: jeu de construction de métro sur une vraie ville
 Metro Builder télécharge une ville depuis OpenStreetMap (rues, bâtiments,
 densités) et vous laisse y construire stations et lignes de métro : tracés
 courbes, longueur des trains, demande de déplacements, plan schématique,
 finances, événements aléatoires, score et objectifs.
CONTROL

mkdir -p dist
fakeroot dpkg-deb --build -Zxz "$STAGE" dist/ >/dev/null
echo "→ dist/metrobuilder_${VERSION}_${ARCH}.deb ($(du -h dist/metrobuilder_${VERSION}_${ARCH}.deb | cut -f1))"
echo "Depends: $DEPENDS"
