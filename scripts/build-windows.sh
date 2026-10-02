#!/usr/bin/env bash
# Compile Metro Builder pour Windows 64 bits depuis Linux (llvm-mingw + Qt 6.8.2 win64_llvm_mingw)
# et prépare dist/MetroBuilder-windows/ (exécutable + DLL + plugins) ainsi qu'une archive .zip.
# Prérequis : win-toolchain/ (compilateur et Qt Windows) et Qt 6.8.2 installé sous Linux (outils moc…).
set -euo pipefail
cd "$(dirname "$0")/.."
TC=win-toolchain
QT=$TC/qt
OBJDUMP=$TC/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64/bin/llvm-objdump
VERSION=$(sed -n 's/^project(MetroBuilder VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
OUT=dist/MetroBuilder-windows
ZIP=MetroBuilder-$VERSION-windows-x64.zip

cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=$TC/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-win -j"$(nproc)"

rm -rf "$OUT"
mkdir -p "$OUT/platforms" "$OUT/tls" "$OUT/styles"
cp build-win/metrobuilder.exe "$OUT/"
cp "$QT/plugins/platforms/qwindows.dll" "$OUT/platforms/"
cp "$QT/plugins/tls/qschannelbackend.dll" "$OUT/tls/"          # HTTPS (tuiles, géocodage) via Windows
cp "$QT/plugins/styles/qmodernwindowsstyle.dll" "$OUT/styles/" 2>/dev/null || true

# DLL non système nécessaires, en suivant les dépendances de proche en proche
declare -A seen
queue=("$OUT/metrobuilder.exe" "$OUT/platforms/qwindows.dll" "$OUT/tls/qschannelbackend.dll")
while ((${#queue[@]})); do
    f=${queue[0]}; queue=("${queue[@]:1}")
    for dll in $("$OBJDUMP" -p "$f" | awk '/DLL Name/ {print $3}'); do
        [[ -n ${seen[$dll]:-} ]] && continue
        seen[$dll]=1
        src=""
        for d in "$QT/bin" "$QT"; do [[ -f $d/$dll ]] && src=$d/$dll && break; done
        [[ -z $src ]] && continue # DLL système Windows
        cp "$src" "$OUT/"
        queue+=("$OUT/$dll")
    done
done

printf '[Paths]\nPlugins = .\n' > "$OUT/qt.conf"
cp README.md "$OUT/LISEZMOI.md"
(cd dist && rm -f "$ZIP" && zip -qr "$ZIP" MetroBuilder-windows)
echo "→ $OUT ($(du -sh "$OUT" | cut -f1)), archive dist/$ZIP ($(du -h "dist/$ZIP" | cut -f1))"
ls "$OUT"
