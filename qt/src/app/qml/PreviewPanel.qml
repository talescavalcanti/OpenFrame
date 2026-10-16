import QtQuick

// Preview com cantos arredondados, grade dos terços opcional e a cápsula de transporte.
Item {
    id: panel

    Item {
        id: area
        x: 18
        y: 18
        width: parent.width - 36
        height: parent.height - 18 - transport.height - 24

        // Canvas encaixado na área, na proporção do projeto.
        Item {
            id: canvas
            readonly property real aspect: Editor.aspectValue > 0 ? Editor.aspectValue : 16 / 9
            readonly property bool wide: aspect >= area.width / Math.max(area.height, 1)
            width: wide ? area.width : area.height * aspect
            height: wide ? area.width / aspect : area.height
            anchors.centerIn: parent

            PreviewFrame {
                id: frame
                anchors.fill: parent
                radius: 10
            }
            // Seleção, alças e guias sobre o quadro (desligado durante a reprodução).
            CanvasOverlay {
                objectName: "canvasOverlay"
                anchors.fill: parent
                visible: Editor.duration > 0
            }
            // Grade dos terços, no ciano da marca.
            Item {
                visible: Editor.showGrid
                anchors.fill: parent
                Repeater {
                    model: 2
                    Rectangle {
                        required property int index
                        x: Math.round(canvas.width * (index + 1) / 3)
                        width: 1
                        height: canvas.height
                        color: Theme.alpha(Theme.accent, 0.55)
                    }
                }
                Repeater {
                    model: 2
                    Rectangle {
                        required property int index
                        y: Math.round(canvas.height * (index + 1) / 3)
                        width: canvas.width
                        height: 1
                        color: Theme.alpha(Theme.accent, 0.55)
                    }
                }
            }
            // Vazio: convite para importar.
            Rectangle {
                visible: Editor.duration === 0
                anchors.fill: parent
                radius: 10
                color: Theme.alpha(Theme.raised, 0.6)
                Column {
                    anchors.centerIn: parent
                    spacing: 14
                    LogoMark {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 54
                        height: 54
                        frameColor: Theme.textTertiary
                        litColor: Theme.alpha(Theme.accent, 0.6)
                    }
                    OfText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Importe mídia e adicione à timeline"
                        color: Theme.textSecondary
                    }
                    PillButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        compact: true
                        text: "Importar mídia"
                        onClicked: Editor.requestDialog("importMedia")
                    }
                }
            }
        }
    }

    TransportBar {
        id: transport
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
    }
}
