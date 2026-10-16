import QtQuick

// Barra da timeline: dividir, duplicar, apagar, texto; intervalo do clipe
// selecionado no meio; zoom à direita.
Item {
    id: bar
    height: 46

    readonly property var seg: Editor.selectedSegment

    component HeightItem: OfMenuItem {
        property var preset
        text: preset.title
        marked: Editor.commonRowHeight === preset.value
        onTriggered: Editor.setAllRowHeights(preset.height)
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        CircleButton { size: 28; iconName: "scissors"; help: "Dividir no cursor (Ctrl+B)"; onClicked: Editor.splitAtPlayhead() }
        CircleButton { size: 28; iconName: "copy-plus"; enabled: Editor.selection !== ""; help: "Duplicar (Ctrl+D)"; onClicked: Editor.duplicateSelection() }
        CircleButton { size: 28; iconName: "trash-2"; enabled: Editor.selection !== ""; help: "Apagar (Delete)"; onClicked: Editor.deleteSelection() }
        Item {
            width: 13
            height: 28
            VDivider { anchors.centerIn: parent; height: 18 }
        }
        PillButton {
            anchors.verticalCenter: parent.verticalCenter
            kind: "quiet"
            compact: true
            text: "Texto"
            iconName: "type"
            help: "Adicionar texto (Ctrl+T)"
            onClicked: Editor.addText()
        }
        CircleButton {
            id: micButton
            objectName: "recordButton"
            size: 28
            iconName: "mic"
            on: Editor.recording.showPanel
            help: "Gravar áudio"
            onClicked: Editor.presentRecorder()
            RecorderPanel {
                x: 0
                y: micButton.height + 8
            }
        }
    }

    Row {
        visible: Editor.selection !== "" && bar.seg.startText !== undefined
        anchors.centerIn: parent
        spacing: 6
        OfText { text: bar.seg.startText + " – " + bar.seg.endText; size: 11; mono: true; color: Theme.textSecondary }
        OfText { text: bar.seg.durationText || ""; size: 11; mono: true; color: Theme.textTertiary }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        // Altura de todas as faixas de uma vez.
        GlyphButton {
            id: heightButton
            objectName: "rowHeightButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "unfold-vertical"
            iconSize: 15
            help: "Altura das faixas"
            rightPadding: 6
            onClicked: heightMenu.popup(heightButton, 0, heightButton.height + 4)
            OfMenu {
                id: heightMenu
                objectName: "rowHeightMenu"
                onOpened: Editor.modalCount += 1
                onClosed: Editor.modalCount -= 1
                HeightItem { preset: Editor.rowHeightPresets[0] }
                HeightItem { preset: Editor.rowHeightPresets[1] }
                HeightItem { preset: Editor.rowHeightPresets[2] }
                HeightItem { preset: Editor.rowHeightPresets[3] }
            }
        }
        GlyphButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "move-horizontal"
            iconSize: 15
            help: "Ajustar à janela (Shift+Z)"
            rightPadding: 6
            onClicked: Editor.zoomToFit()
        }
        GlyphButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "minus"
            help: "Afastar (Ctrl+−)"
            onClicked: Editor.zoomOut()
        }
        // Zoom em escala logarítmica, do projeto inteiro (mínimo) ao máximo.
        ThinSlider {
            anchors.verticalCenter: parent.verticalCenter
            width: 120
            readonly property real low: Math.log(Editor.minZoom)
            readonly property real high: Math.log(Editor.maxZoom)
            value: (Math.log(Editor.zoom) - low) / Math.max(high - low, 1e-6)
            onMoved: (v) => Editor.setZoom(Math.exp(low + v * (high - low)))
        }
        GlyphButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "plus"
            help: "Aproximar (Ctrl+=)"
            onClicked: Editor.zoomIn()
        }
    }
}
