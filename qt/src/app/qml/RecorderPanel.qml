import QtQuick
import QtQuick.Templates as T

// Painel "Gravar áudio" (o popover do Mac), flutuando sob o botão de microfone:
// o microfone, o nível ao vivo e o botão vermelho; depois, a contagem, o tempo
// gravado com a forma de onda, e os erros de permissão ou de microfone. Não conta
// como diálogo: espaço e Esc continuam com o gravador.
T.Popup {
    id: panel
    objectName: "recorderPanel"
    readonly property var rec: Editor.recording
    readonly property string phase: rec.phase

    readonly property bool wanted: rec.showPanel
    onWantedChanged: Qt.callLater(() => wanted ? open() : close())
    onClosed: if (rec.showPanel && !rec.active) Editor.closeRecorder()

    width: 300
    padding: 16
    closePolicy: rec.active ? T.Popup.NoAutoClose : T.Popup.CloseOnPressOutsideParent
    implicitHeight: content.implicitHeight + topPadding + bottomPadding

    background: Rectangle {
        color: "#121212"
        radius: Theme.panelRadius
        border.color: Qt.rgba(1, 1, 1, 0.12)
        Rectangle {
            z: -1
            y: 6
            width: parent.width
            height: parent.height
            radius: parent.radius
            color: Qt.rgba(0, 0, 0, 0.45)
        }
    }

    contentItem: Column {
        id: content
        spacing: 14

        Item {
            width: parent.width
            height: 18
            OfText { text: "Gravar áudio"; size: 13; weight: Theme.medium; anchors.verticalCenter: parent.verticalCenter }
            Row {
                visible: panel.phase === "recording" || panel.phase === "finishing"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.recording; anchors.verticalCenter: parent.verticalCenter }
                OfText { text: panel.rec.durationText; size: 11; mono: true; color: Theme.textSecondary }
            }
        }

        MenuField {
            objectName: "recorderInput"
            width: parent.width
            enabled: !panel.rec.active
            help: "Microfone"
            value: panel.rec.input
            options: {
                const list = [{ value: "", title: panel.rec.input === "" ? panel.rec.inputName : "Padrão do sistema" }]
                for (const input of panel.rec.inputs)
                    list.push({ value: input.id, title: input.name })
                return list
            }
            onPicked: (v) => Editor.selectRecordingInput(v)
        }

        // Medidor em segmentos, em dB (−60…0), como os medidores de áudio.
        Row {
            id: meter
            readonly property int count: 24
            readonly property real db: 20 * Math.log10(Math.max(panel.rec.level, 0.000001))
            readonly property int lit: Math.round(Math.max(0, Math.min(1, (db + 60) / 60)) * count)
            width: parent.width
            spacing: 2
            Repeater {
                model: meter.count
                Rectangle {
                    required property int index
                    width: (meter.width - meter.spacing * (meter.count - 1)) / meter.count
                    height: 8
                    radius: 1.5
                    color: index >= meter.lit ? Qt.rgba(1, 1, 1, 0.08)
                         : index >= meter.count - 3 ? Theme.recording
                         : index >= meter.count - 7 ? "#F5D33A" : Theme.accent
                }
            }
        }

        // Pronto.
        RecordButton {
            visible: panel.phase === "ready"
            recording: false
            onClicked: Editor.startRecording()
        }
        Note {
            visible: panel.phase === "ready"
            text: "A timeline toca sem som enquanto você grava. Espaço para; Esc cancela."
        }

        // Contagem.
        Row {
            visible: panel.phase === "countdown"
            spacing: 12
            OfText {
                width: 40
                text: panel.rec.countdown
                size: 34
                weight: Theme.light
                color: Theme.recording
                horizontalAlignment: Text.AlignHCenter
            }
            OfText {
                anchors.verticalCenter: parent.verticalCenter
                text: "Gravando em " + panel.rec.countdown + "…\nEsc cancela."
                size: 12
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }

        // Gravando.
        WaveformView {
            visible: panel.phase === "recording" || panel.phase === "finishing"
            width: parent.width
            height: 34
            materialId: "gravacao-ao-vivo"
            from: Math.max(0, panel.rec.duration - 6)
            to: Math.max(panel.rec.duration, 0.02)
            bar: 2
            gap: 1
            color: Theme.alpha(Theme.recording, 0.85)
        }
        RecordButton {
            visible: panel.phase === "recording" || panel.phase === "finishing"
            enabled: panel.phase === "recording"
            recording: true
            onClicked: Editor.stopRecording()
        }

        // Erro.
        Row {
            visible: panel.phase === "failed"
            width: parent.width
            spacing: 8
            Icon { id: alert; name: "triangle-alert"; size: 13; color: Theme.warning; y: 1 }
            OfText {
                width: parent.width - alert.width - 8
                text: panel.rec.error
                size: 12
                color: Theme.warning
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
        PillButton {
            visible: panel.phase === "failed"
            compact: true
            text: "Tentar de novo"
            onClicked: Editor.presentRecorder()
        }
    }

    OfDialog {
        readonly property bool wanted: panel.rec.confirmDiscard
        onWantedChanged: Qt.callLater(() => wanted ? open() : close())
        title: "Descartar a gravação?"
        message: "O áudio gravado até agora será apagado."
        acceptTitle: "Descartar"
        cancelTitle: "Continuar gravando"
        destructive: true
        onAccepted: Editor.cancelRecording(true)
        onClosed: if (panel.rec.confirmDiscard) Editor.keepRecording()
    }

    component RecordButton: T.AbstractButton {
        id: button
        property bool recording
        width: parent ? parent.width : 268
        height: 34
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        opacity: enabled ? (pressed ? 0.75 : 1) : 0.5
        background: Rectangle {
            radius: height / 2
            color: button.hovered ? Qt.lighter(Theme.recording, 1.08) : Theme.recording
        }
        contentItem: Item {
            Row {
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: button.recording ? 10 : 12
                    height: width
                    radius: button.recording ? 2 : 6
                    color: "white"
                }
                OfText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: button.recording ? "Parar" : "Gravar"
                    size: 13
                    weight: Theme.medium
                    color: "white"
                }
            }
        }
        Tip { text: button.recording ? "Parar (Espaço)" : "Gravar com contagem de 3 s" }
    }
}
