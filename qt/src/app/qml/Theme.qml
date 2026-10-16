pragma Singleton
import QtQuick

// Sistema visual: preto puro, cinzas neutros e um único destaque ciano
// (o "Playhead Cyan" da marca), usado só no que está ativo. Mesmos tokens do
// Theme.swift; a Inter substitui a SF Pro, que não pode ser distribuída.
QtObject {
    // Superfícies, do fundo para cima.
    readonly property color background: "#000000"
    readonly property color surface: Qt.rgba(0.043, 0.043, 0.043, 1)     // #0B0B0B, painéis
    readonly property color raised: Qt.rgba(0.082, 0.082, 0.082, 1)      // #151515, campos e clipes
    readonly property color raisedHigh: Qt.rgba(0.125, 0.125, 0.125, 1)  // #202020, hover e seleção
    readonly property color hairline: Qt.rgba(1, 1, 1, 0.07)

    // Texto.
    readonly property color textPrimary: Qt.rgba(1, 1, 1, 0.92)
    readonly property color textSecondary: Qt.rgba(1, 1, 1, 0.58)
    readonly property color textTertiary: Qt.rgba(1, 1, 1, 0.36)

    // Destaque.
    readonly property color accent: "#00C2CC"
    readonly property color accentSoft: Qt.rgba(0, 194 / 255, 204 / 255, 0.14)
    readonly property color textOnAccent: "#000000"
    readonly property color warning: Qt.rgba(1, 0.62, 0.35, 1)
    readonly property color danger: Qt.rgba(1, 0.42, 0.4, 1)
    /// Vermelho da gravação (botão, clipe que cresce).
    readonly property color recording: "#FF4545"

    // Clipes: tons de cinza; o ícone diz o tipo.
    readonly property color clipVideo: Qt.rgba(0.15, 0.15, 0.15, 1)
    readonly property color clipText: Qt.rgba(0.19, 0.19, 0.19, 1)
    readonly property color clipAudio: Qt.rgba(0.10, 0.10, 0.10, 1)
    readonly property color waveform: Qt.rgba(1, 1, 1, 0.32)

    // Raios.
    readonly property real panelRadius: 14
    readonly property real cardRadius: 10
    readonly property real fieldRadius: 8
    /// Altura do degradê no topo das rolagens sob título fixo.
    readonly property real scrollFade: 16

    // Inter, só entre os pesos 200 e 500 (os mesmos thin/light/regular/medium do app Swift).
    readonly property string family: "Inter"
    readonly property string iconFamily: "lucide"
    readonly property int thin: Font.ExtraLight
    readonly property int light: Font.Light
    readonly property int regular: Font.Normal
    readonly property int medium: Font.Medium

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    /// Cor com o canal alfa multiplicado (para os estados de hover e pressionado).
    function fade(c, f) { return Qt.rgba(c.r, c.g, c.b, c.a * f) }
}
