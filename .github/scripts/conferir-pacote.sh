#!/usr/bin/env bash
# Confere o pacote extraído: o whisper/ggml com as variantes de CPU, o plugin de
# TLS e as licenças novas no lugar, e o app do pacote baixando por HTTPS (HEAD no servidor do modelo)
# e transcrevendo uma fala de verdade com uma variante carregada de dentro do
# pacote (cenário "pacote"). Usado pela CI depois dos pacotes. O app roda sem o
# ambiente do Qt da CI e com um PATH mínimo, como no computador do usuário; no
# Windows, o HTTPS vai só pelo schannel (sem o OpenSSL que o Git traz).
#
# uso: conferir-pacote.sh <pasta dos pacotes> <pasta de saída>
# Precisa de OPENFRAME_MODELS_DIR com um modelo (a CI tem o base). No Linux,
# extrai o AppImage e usa o Xvfb; no Windows, usa a pasta do zip.
set -euo pipefail

pacotes=$(cd "$1" && pwd)
rm -rf "$2"
mkdir -p "$2"
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
work="$out/.trabalho"
mkdir -p "$work/midia" "$work/biblioteca"
cp "$root/qt/tests/fixtures/fala/frase.wav" "$work/midia/"
falhas=0
falta() {
    echo "::error::Falta no pacote: $1"
    falhas=1
}

if [[ "${RUNNER_OS:-$(uname -s)}" == Windows* || "$(uname -s)" == MINGW* ]]; then
    app="$pacotes/OpenFrame"
    for f in whisper.dll ggml.dll ggml-base.dll tls/qschannelbackend.dll licencas/whisper.cpp-LICENSE.txt \
        licencas/OFL-JetBrainsMono.txt licencas/OFL-PlayfairDisplay.txt licencas/OFL-BarlowCondensed.txt; do
        [[ -f "$app/$f" ]] || falta "$f"
    done
    variantes=(x64 sse42 sandybridge haswell skylakex cannonlake cascadelake icelake alderlake)
    for v in "${variantes[@]}"; do
        [[ -f "$app/ggml-cpu-$v.dll" ]] || falta "ggml-cpu-$v.dll"
    done
    executar=("$app/OpenFrame.exe")
    esperado="${pacotes##*/}/OpenFrame/ggml-cpu-"
    plataforma=(QT_QPA_PLATFORM=windows OPENFRAME_SOFTWARE=1 OPENFRAME_TLS_BACKEND=schannel)
    sistema=$(cygpath -u "$SYSTEMROOT")
    caminho="$sistema/System32:$sistema:$sistema/System32/Wbem"
else
    appimage=$(ls "$pacotes"/*.AppImage | head -n 1)
    (cd "$work" && "$appimage" --appimage-extract > /dev/null)
    app="$work/squashfs-root"
    for f in usr/lib/libwhisper.so.1 usr/lib/libggml.so.0 usr/lib/libggml-base.so.0 \
        usr/plugins/tls/libqopensslbackend.so usr/share/doc/openframe/licencas/whisper.cpp-LICENSE.txt \
        usr/share/doc/openframe/licencas/OFL-JetBrainsMono.txt usr/share/doc/openframe/licencas/OFL-PlayfairDisplay.txt \
        usr/share/doc/openframe/licencas/OFL-BarlowCondensed.txt; do
        [[ -f "$app/$f" ]] || falta "$f"
    done
    variantes=(x64 sse42 sandybridge ivybridge piledriver haswell skylakex cannonlake cascadelake icelake cooperlake
        zen4 alderlake sapphirerapids)
    for v in "${variantes[@]}"; do
        f="$app/usr/lib/openframe/libggml-cpu-$v.so"
        if [[ ! -f $f ]]; then
            falta "usr/lib/openframe/libggml-cpu-$v.so"
        else
            # O ldd que falha (um ELF estragado, por exemplo) não escreve "not found": vale o código de saída.
            saida=0
            deps=$(LD_LIBRARY_PATH="$app/usr/lib" ldd "$f" 2>&1) || saida=$?
            if [[ $saida -ne 0 ]] || grep -q 'not found' <<< "$deps"; then
                echo "::error::Dependências de libggml-cpu-$v.so (ldd saiu com $saida):"
                echo "$deps"
                falhas=1
            fi
        fi
    done
    # Diagnóstico da carga das variantes (o ggml falha calado no Release).
    echo "== ldd de libggml-cpu-haswell.so:"
    LD_LIBRARY_PATH="$app/usr/lib" ldd "$app/usr/lib/openframe/libggml-cpu-haswell.so" 2>&1 || echo "(ldd saiu com $?)"
    (cd "$app" && file usr/lib/openframe/libggml-cpu-haswell.so &&
        readelf -d usr/lib/openframe/libggml-cpu-haswell.so usr/lib/libggml-base.so.0 usr/bin/OpenFrame |
        grep -E 'NEEDED|RPATH|RUNPATH|SONAME|File:') || true
    sudo apt-get install -y --no-install-recommends xvfb xauth > /dev/null
    executar=(xvfb-run -a "$app/AppRun")
    esperado="squashfs-root/usr/lib/openframe/libggml-cpu-"
    plataforma=(QT_QPA_PLATFORM=xcb OPENFRAME_SOFTWARE=1)
    caminho=/usr/bin:/bin
fi
echo "Arquivos do whisper/ggml, do TLS e das licenças: $([[ $falhas -eq 0 ]] && echo completos || echo incompletos)."

# O app do pacote: HTTPS e transcrição. OPENFRAME_WHISPER_LOG mostra de onde a variante veio.
# Sem as variáveis do Qt da CI (install-qt-action) e das bibliotecas: o pacote se vira sozinho.
source "$root/.github/scripts/ambiente-limpo.sh"
echo "Ambiente do app: PATH=$caminho; sem ${limpo[*]:1}"
status=0
timeout 400 "${limpo[@]}" PATH="$caminho" "${plataforma[@]}" OPENFRAME_SNAPSHOT_DIR="$out/capturas" OPENFRAME_LIBRARY_DIR="$work/biblioteca" \
    OPENFRAME_SNAPSHOT_MEDIA="$work/midia" OPENFRAME_SNAPSHOT_SCENARIO=pacote OPENFRAME_WHISPER_LOG=1 \
    "${executar[@]}" > "$out/pacote.log" 2>&1 || status=$?
grep -E '^\[snapshot\]|^\[openframe\]|load_backend' "$out/pacote.log" || true
if ! grep -q '\[snapshot\] pacote: TLS sim' "$out/pacote.log"; then
    echo "::error::O app do pacote ficou sem TLS"
    falhas=1
fi
if [[ $status -ne 0 ]] || ! grep -q '\[snapshot\] fim' "$out/pacote.log"; then
    echo "::error::O app do pacote não passou no cenário 'pacote' (saída $status)"
    falhas=1
fi
# A variante tem de vir do pacote, não da pasta do build (que o executável também conhece).
carregada=$(grep -o 'loaded CPU backend from .*' "$out/pacote.log" | head -n 1 || true)
if [[ -z $carregada ]]; then
    echo "::error::Nenhuma variante de CPU do ggml foi carregada"
    falhas=1
elif [[ $(printf '%s' "$carregada" | tr '\\' '/') != *"$esperado"* ]]; then
    echo "::error::A variante veio de fora do pacote: $carregada"
    falhas=1
fi

# Tamanhos.
echo "Tamanhos:"
for f in "$pacotes"/*.zip "$pacotes"/*.exe "$pacotes"/*.AppImage; do
    if [[ -f $f ]]; then echo "  ${f##*/}: $(du -k "$f" | awk '{ printf "%.1f", $1 / 1024 }') MB"; fi
done
if [[ -d $app ]]; then
    ggml=$(find "$app" \( -iname '*ggml*' -o -iname '*whisper*' \) -type f -exec du -k {} + | awk '{ s += $1 } END { print s }')
    echo "  whisper/ggml no pacote: $((ggml / 1024)) MB"
fi
rm -rf "$work"
exit $falhas
