#!/usr/bin/env bash
# Pacotes do Windows: zip portátil e instalador (Inno Setup), a partir do build.
# Roda no Git Bash da CI, depois do ilammy/msvc-dev-cmd e do install-qt-action.
#
# uso: pacote-windows.sh <pasta do build> <pasta de saída>
# Precisa de: windeployqt no PATH, FFMPEG_ROOT, VCToolsRedistDir (vcvars), 7z.
set -euo pipefail

build=$1
# Caminho absoluto: o ISCC resolve caminhos relativos a partir da pasta do .iss.
mkdir -p "$2"
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(sed -n 's/^project(OpenFrame VERSION \([0-9.]*\).*/\1/p' "$root/qt/CMakeLists.txt")
app="$out/OpenFrame"
ffmpeg=$(cygpath -u "$FFMPEG_ROOT")

rm -rf "$out"
mkdir -p "$app/licencas"
# O qt_standard_project_setup põe os executáveis na raiz do build no Windows.
cp "$build/OpenFrame.exe" "$app/"

# Qt (sem os plugins de mídia: o som usa só o QAudioSink, e o plugin FFmpeg do
# Qt traria uma segunda cópia do FFmpeg).
windeployqt --release --no-translations --skip-plugin-types multimedia \
    --qmldir "$root/qt/src/app/qml" "$app/OpenFrame.exe"

# Plugin de TLS (download do modelo por HTTPS): o windeployqt o leva com o Qt
# Network; se faltar, vai à mão.
if [[ ! -f "$app/tls/qschannelbackend.dll" ]]; then
    mkdir -p "$app/tls"
    cp "$(cygpath -u "$QT_ROOT_DIR")/plugins/tls/qschannelbackend.dll" "$app/tls/"
fi

# whisper/ggml e as 9 variantes de CPU ao lado do executável (o windeployqt não
# os conhece; o núcleo carrega as variantes com LoadLibrary).
cp "$build"/ggml/whisper.dll "$build"/ggml/ggml.dll "$build"/ggml/ggml-base.dll "$build"/ggml/ggml-cpu-*.dll "$app/"

# FFmpeg e o runtime do Visual C++ ao lado do executável.
cp "$ffmpeg"/bin/*.dll "$app/"
crt=$(ls -d "$(cygpath -u "$VCToolsRedistDir")"/x64/Microsoft.VC14*.CRT | head -n 1)
cp "$crt"/*.dll "$app/"

# Licenças.
cp "$root/LICENSE" "$app/LICENSE.txt"
cp "$root/qt/packaging/TERCEIROS.txt" "$app/"
cp "$root/qt/packaging/licenses/LGPL-3.0.txt" "$root/qt/src/app/fonts/LICENSE-Lucide.txt" "$app/licencas/"
# A Inter está no app e no núcleo, com a mesma OFL-Inter.txt: vai uma cópia só,
# e o pacote falha se as duas um dia forem diferentes.
for f in "$root/qt/src/app/fonts/OFL-Inter.txt" "$root"/qt/src/core/fonts/OFL-*.txt; do
    dest="$app/licencas/${f##*/}"
    if [[ -e $dest ]]; then
        cmp -s "$f" "$dest" || { echo "licenças diferentes com o mesmo nome: $f" >&2; exit 1; }
    else
        cp "$f" "$dest"
    fi
done
whisper_src=${FETCHCONTENT_SOURCE_DIR_WHISPER:-$build/_deps/whisper-src}
cp "$whisper_src/LICENSE" "$app/licencas/whisper.cpp-LICENSE.txt"
[[ -f "$ffmpeg/LICENSE.txt" ]] && cp "$ffmpeg/LICENSE.txt" "$app/licencas/FFmpeg-LICENSE.txt"

# Zip portátil.
(cd "$out" && 7z a -tzip -bd "OpenFrame-$version-windows-x64-portatil.zip" OpenFrame > /dev/null)

# Instalador. Os /D… não podem virar caminhos do MSYS.
iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
if [[ ! -x $iscc ]]; then
    choco install innosetup -y --no-progress > /dev/null
fi
MSYS2_ARG_CONV_EXCL='*' "$iscc" /Q "/DAppVersion=$version" "/DPayloadDir=$(cygpath -w "$app")" \
    "/DOutputDir=$(cygpath -w "$out")" "$(cygpath -w "$root/qt/packaging/windows/OpenFrame.iss")"

ls -la "$out"
