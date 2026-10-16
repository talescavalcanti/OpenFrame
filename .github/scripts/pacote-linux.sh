#!/usr/bin/env bash
# AppImage do Linux, a partir do build (linuxdeploy + plugin do Qt).
# Roda na CI depois do install-qt-action (QT_ROOT_DIR) e com o FFmpeg em
# FFMPEG_ROOT (as bibliotecas dele entram no AppImage).
#
# uso: pacote-linux.sh <pasta do build> <pasta de saída>
set -euo pipefail

build=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(sed -n 's/^project(OpenFrame VERSION \([0-9.]*\).*/\1/p' "$root/qt/CMakeLists.txt")
appdir="$out/AppDir"
tools="$out/.ferramentas"
rm -rf "$appdir" "$tools"
mkdir -p "$tools"

# linuxdeploy fixo; o plugin do Qt só tem builds recentes (Qt 6.12) na "continuous".
curl -fsSL --retry 3 -o "$tools/linuxdeploy" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
curl -fsSL --retry 3 -o "$tools/linuxdeploy-plugin-qt" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$tools"/*
export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1 # sem FUSE na CI

# Executável, atalho, ícones, tipo .openframe e licenças.
cmake --install "$build" --prefix "$appdir/usr"
install -Dm644 "$root/qt/packaging/linux/openframe.desktop" "$appdir/usr/share/applications/openframe.desktop"
for s in 16 24 32 48 64 128 256 512; do
    install -Dm644 "$root/qt/packaging/icons/openframe-$s.png" "$appdir/usr/share/icons/hicolor/${s}x$s/apps/openframe.png"
done
install -Dm644 "$root/qt/packaging/linux/openframe-mime.xml" "$appdir/usr/share/mime/packages/openframe.xml"
doc="$appdir/usr/share/doc/openframe"
install -Dm644 "$root/LICENSE" "$doc/LICENSE.txt"
install -Dm644 "$root/qt/packaging/TERCEIROS.txt" "$doc/TERCEIROS.txt"
install -Dm644 "$root/qt/packaging/licenses/LGPL-3.0.txt" "$doc/licencas/LGPL-3.0.txt"
install -Dm644 "$root/qt/src/app/fonts/LICENSE-Lucide.txt" "$doc/licencas/LICENSE-Lucide.txt"
# A Inter está no app e no núcleo, com a mesma OFL-Inter.txt: vai uma cópia só,
# e o pacote falha se as duas um dia forem diferentes.
for f in "$root/qt/src/app/fonts/OFL-Inter.txt" "$root"/qt/src/core/fonts/OFL-*.txt; do
    dest="$doc/licencas/${f##*/}"
    if [[ -e $dest ]]; then
        cmp -s "$f" "$dest" || { echo "licenças diferentes com o mesmo nome: $f" >&2; exit 1; }
    else
        install -Dm644 "$f" "$dest"
    fi
done
install -Dm644 "${FETCHCONTENT_SOURCE_DIR_WHISPER:-$build/_deps/whisper-src}/LICENSE" \
    "$doc/licencas/whisper.cpp-LICENSE.txt"
if [[ -f "$FFMPEG_ROOT/LICENSE.txt" ]]; then
    install -Dm644 "$FFMPEG_ROOT/LICENSE.txt" "$doc/licencas/FFmpeg-LICENSE.txt"
fi
if command -v desktop-file-validate > /dev/null; then
    desktop-file-validate "$appdir/usr/share/applications/openframe.desktop"
fi

# Variantes de CPU do ggml (o núcleo as carrega com dlopen, então o linuxdeploy
# não as conhece) em usr/lib/openframe, e o plugin de TLS do Qt (download do
# modelo por HTTPS, com a OpenSSL do sistema). O libwhisper/libggml/libggml-base
# vêm como dependências do executável.
mkdir -p "$appdir/usr/lib/openframe" "$appdir/usr/plugins/tls"
cp "$build"/ggml/libggml-cpu-*.so "$appdir/usr/lib/openframe/"
cp "$QT_ROOT_DIR"/plugins/tls/libqopensslbackend.so "$QT_ROOT_DIR"/plugins/tls/libqcertonlybackend.so \
    "$appdir/usr/plugins/tls/"

# Qt e dependências (o FFmpeg vem do LD_LIBRARY_PATH). Primeiro só o AppDir,
# para tirar os plugins de mídia do Qt: o som usa só o QAudioSink, e o plugin
# FFmpeg do Qt traria uma segunda cópia do FFmpeg, que pede a libva-drm e cai
# num Linux sem ela. O som-appimage.sh prova que o pacote toca sem o plugin.
export QMAKE="$QT_ROOT_DIR/bin/qmake"
export QML_SOURCES_PATHS="$root/qt/src/app/qml"
export LD_LIBRARY_PATH="$FFMPEG_ROOT/lib:$QT_ROOT_DIR/lib:$build/ggml${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
linuxdeploy --appdir "$appdir" --plugin qt \
    --deploy-deps-only "$appdir/usr/lib/openframe" --deploy-deps-only "$appdir/usr/plugins/tls"
rm -rf "$appdir/usr/plugins/multimedia"
for lib in "$QT_ROOT_DIR"/lib/lib{avcodec,avformat,avutil,avfilter,swresample,swscale}.so.*; do
    [[ -e $lib ]] || continue
    name=$(basename "$lib")
    [[ -e "$FFMPEG_ROOT/lib/$name" ]] || rm -f "$appdir/usr/lib/$name"
done

# AppImage.
cd "$out"
LINUXDEPLOY_OUTPUT_VERSION="$version" linuxdeploy --appdir "$appdir" --output appimage
rm -rf "$tools" "$appdir"
ls -la "$out"
