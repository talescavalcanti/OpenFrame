#!/usr/bin/env bash
# Prova do AppImage num Linux sem a libva: o pacote não leva o plugin de mídia do
# Qt (nem nada que dependa da libva), abre, toca e o som sai. Tira a libva do
# sistema, toca 3 s (cenário "som") num servidor PulseAudio com uma saída nula e
# mede o que chegou ao monitor dela. O app roda no ambiente limpo da conferência
# (sem o Qt da CI) e não pode carregar nenhum backend de mídia do Qt. Depois, toca
# de novo com o Qt da CI no QT_PLUGIN_PATH, como na máquina de quem desenvolve com
# Qt: o pacote tem de ignorá-lo. Usado pela CI do Linux depois dos pacotes.
#
# uso: som-appimage.sh <OpenFrame.AppImage> <pasta de saída>
# Precisa do ffmpeg no PATH (gera a mídia e mede o volume) e de sudo/apt.
set -euo pipefail

appimage=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
root=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$2"
mkdir -p "$2"
out=$(cd "$2" && pwd)
work="$out/.trabalho"
mkdir -p "$work/midia"

# 1. O pacote: sem plugins/multimedia e sem nada que peça a libva.
(cd "$work" && "$appimage" --appimage-extract > /dev/null)
app="$work/squashfs-root"
if [[ -d $app/usr/plugins/multimedia ]]; then
    echo "::error::O AppImage leva o plugin de mídia do Qt (usr/plugins/multimedia)"
    exit 1
fi
pedem=$(find "$app" -type f \( -name '*.so*' -o -perm -u+x \) -print0 |
    xargs -0 -r sh -c 'for f; do readelf -d "$f" 2>/dev/null | grep -q "NEEDED.*libva" && echo "$f"; done' sh || true)
levam=$(find "$app" -name 'libva*.so*' || true)
if [[ -n $pedem || -n $levam ]]; then
    echo "::error::Há bibliotecas no AppImage ligadas à libva:"
    echo "$pedem" "$levam"
    exit 1
fi
echo "Pacote: sem plugin de mídia do Qt e sem libva."

# 2. O sistema: sem libva, com PulseAudio e uma tela virtual (o app usa a xcb do pacote).
sudo apt-get install -y --no-install-recommends pulseaudio pulseaudio-utils xvfb xauth > /dev/null
# O apt-get remove levaria junto o pulseaudio (pulseaudio → libasound2-plugins →
# libavcodec → libva). O dpkg tira só a libva e deixa a dependência quebrada: a
# saída nula do PulseAudio não usa os plugins do ALSA. Nenhum passo depois deste usa o apt.
libva=$(dpkg-query -W -f='${Package} ${Status}\n' 'libva*' 2> /dev/null | awk '/ installed$/ && $1 ~ /^libva([0-9]|-)/ { print $1 }')
if [[ -n $libva ]]; then
    echo "Tirando só a libva: ${libva//$'\n'/ }"
    # shellcheck disable=SC2086
    sudo dpkg --remove --force-depends $libva
    sudo ldconfig
fi
if ldconfig -p | awk '/libva[.-]/ { achou = 1 } END { exit !achou }'; then
    echo "::error::A libva continua no sistema"
    exit 1
fi
echo "Sistema: sem libva."
for c in pulseaudio pactl parec; do
    if ! command -v "$c" > /dev/null; then
        echo "::error::O $c sumiu do sistema ao tirar a libva (pacotes tirados: ${libva//$'\n'/ })"
        exit 1
    fi
done
if ! pulseaudio --daemonize=yes --exit-idle-time=-1 --log-target=stderr || ! pactl info > /dev/null; then
    echo "::error::O PulseAudio não subiu sem a libva (dependência quebrada do libasound2-plugins)"
    exit 1
fi
pactl load-module module-null-sink sink_name=captura > /dev/null
pactl set-default-sink captura

# 3. Mídia: um vídeo com um tom de 440 Hz.
ffmpeg -v error -f lavfi -i testsrc2=size=640x360:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 \
    -t 6 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$work/midia/1-tom.mp4"

# 4. Toca gravando o monitor da saída nula.
parec -d captura.monitor --format=s16le --rate=48000 --channels=2 > "$work/som.raw" &
gravador=$!
# O qt.multimedia.plugin mostra se algum backend de mídia do Qt carrega.
source "$root/.github/scripts/ambiente-limpo.sh"
tocar() { # tocar <log> <capturas> [VAR=valor...]
    local log=$1 capturas=$2 status=0
    shift 2
    mkdir -p "$work/biblioteca-${capturas##*/}"
    timeout 180 "${limpo[@]}" PATH=/usr/bin:/bin "$@" OPENFRAME_SNAPSHOT_DIR="$capturas" \
        OPENFRAME_LIBRARY_DIR="$work/biblioteca-${capturas##*/}" OPENFRAME_SNAPSHOT_MEDIA="$work/midia" OPENFRAME_SNAPSHOT_SCENARIO=som \
        OPENFRAME_SOFTWARE=1 QT_QPA_PLATFORM=xcb QT_LOGGING_RULES="qt.multimedia.plugin=true" \
        xvfb-run -a "$app/AppRun" > "$log" 2>&1 || status=$?
    cat "$log"
    if [[ $status -ne 0 ]] || ! grep -q '\[snapshot\] fim' "$log"; then
        echo "::error::O AppImage não chegou ao fim do cenário de som (saída $status, $log)"
        return 1
    fi
    # Sem nenhum plugin, o Qt escreve 'Loading media backend ""' (nome vazio) e segue só com o QAudioSink.
    if grep -q 'Loading media backend "[^"]' "$log"; then
        echo "::error::O AppImage carregou um backend de mídia do Qt ($log): $(grep -m 1 'Loading media backend "[^"]' "$log")"
        return 1
    fi
}
echo "Ambiente do app: PATH=/usr/bin:/bin; sem ${limpo[*]:1}"
status=0
tocar "$out/som.log" "$out/capturas" || status=1
kill "$gravador" 2> /dev/null || true
wait "$gravador" 2> /dev/null || true
[[ $status -eq 0 ]] || exit 1

# 5. O som: o tom tem pico perto de -18 dBFS; silêncio fica em -91 dB.
if [[ ! -s $work/som.raw ]]; then
    echo "::error::O gravador do PulseAudio não gravou nada"
    exit 1
fi
ffmpeg -v error -f s16le -ar 48000 -ac 2 -i "$work/som.raw" "$out/som.wav"
medida=$(ffmpeg -hide_banner -i "$out/som.wav" -af volumedetect -f null - 2>&1 | grep -E 'mean_volume|max_volume')
echo "$medida"
pico=$(echo "$medida" | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')
if [[ -z $pico ]] || ! awk -v p="$pico" 'BEGIN { exit !(p > -40) }'; then
    echo "::error::Não saiu som do AppImage (pico ${pico:-?} dB)"
    exit 1
fi
echo "Som do AppImage: pico $pico dB."

# 6. Com o Qt da CI no ambiente (plugins e QML de outro Qt, com o plugin FFmpeg de
# mídia que pede a libva): o pacote usa só os dele e não cai.
if [[ -n ${QT_ROOT_DIR:-} && -d $QT_ROOT_DIR/plugins/multimedia ]]; then
    echo "De novo, com QT_PLUGIN_PATH=$QT_ROOT_DIR/plugins e QML2_IMPORT_PATH=$QT_ROOT_DIR/qml:"
    tocar "$out/som-qt-de-fora.log" "$out/capturas-qt-de-fora" QT_PLUGIN_PATH="$QT_ROOT_DIR/plugins" \
        QML2_IMPORT_PATH="$QT_ROOT_DIR/qml" QML_IMPORT_PATH="$QT_ROOT_DIR/qml"
    echo "Com o Qt da CI no ambiente: o pacote não carregou nenhum backend de mídia."
else
    echo "::warning::Sem o Qt da CI (QT_ROOT_DIR) com plugins de mídia: o teste com o Qt de fora não rodou"
fi
rm -rf "$work"
