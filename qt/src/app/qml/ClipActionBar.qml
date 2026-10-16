import QtQuick

// Ações do clipe selecionado, flutuando junto dele sem cobrir outros clipes.
// Fica dentro da área visível.
Rectangle {
    id: bar
    property real viewportWidth: 0

    readonly property var seg: Editor.selectedSegment
    readonly property int row: Editor.selectedRow
    readonly property real start: (seg.start || 0) * Editor.zoom
    readonly property real end: (seg.end || 0) * Editor.zoom
    readonly property bool canSplit: Editor.playhead > (seg.start || 0) && Editor.playhead < (seg.end || 0)
    readonly property bool canExtract: seg.canExtractAudio === true
    readonly property int actions: 2 + (canSplit ? 1 : 0) + (canExtract ? 1 : 0)
    readonly property real barWidth: actions * 30 + (actions - 1) + 8

    // Ao lado da linha do playhead (ou no início do clipe), dentro da área
    // visível, sem se afastar do clipe e sem passar das bordas do conteúdo.
    readonly property real anchorX: canSplit ? Editor.playhead * Editor.zoom + 8 : start + 8
    readonly property real visibleLeft: Editor.timelineScrollX
    readonly property real visibleRight: visibleLeft + viewportWidth
    readonly property real inView: viewportWidth > 0 ? Math.max(Math.min(anchorX, visibleRight - barWidth - 8), visibleLeft + 8) : anchorX
    readonly property real nearClip: Math.min(Math.max(inView, start - barWidth + 24), end - 24)
    readonly property real baseX: Math.max(0, Math.min(nearClip, Editor.timelineContentWidth - 24 - barWidth))

    // Sem cobrir outros clipes: acima do clipe se a faixa de cima estiver livre
    // nesse trecho (a primeira faixa tem a régua em cima), senão abaixo, senão
    // dentro do próprio clipe.
    readonly property string place: {
        void Editor.clipCount // reavaliar quando os clipes mudarem
        const from = baseX / Editor.zoom, to = (baseX + barWidth) / Editor.zoom
        if (row > 0 && Editor.isRowFree(row - 1, from, to))
            return "above"
        if (Editor.isRowFree(row + 1, from, to))
            return "below"
        return "inside"
    }

    visible: !Editor.gestureActive && Editor.selection !== "" && row >= 0
    // Dentro do clipe, sem passar das bordas dele (se couber).
    x: place === "inside" && end - start >= barWidth + 8 ? Math.max(start + 4, Math.min(baseX, end - barWidth - 4)) : baseX
    readonly property real rowTop: Editor.rowTops[row] ?? 0
    readonly property real rowHeight: Editor.rowHeights[row] ?? 50
    y: rowTop + (place === "above" ? -30 + 2 : place === "below" ? rowHeight - 2 : (rowHeight - 30) / 2)
    width: barWidth
    height: 30
    radius: 15
    color: Theme.raisedHigh
    border.color: Qt.rgba(1, 1, 1, 0.12)

    // Sombra.
    Rectangle {
        z: -1
        x: 0
        y: 3
        width: parent.width
        height: parent.height
        radius: parent.radius
        color: Qt.rgba(0, 0, 0, 0.45)
    }

    Row {
        x: 4
        height: parent.height
        GlyphButton {
            visible: bar.canSplit
            width: 30
            height: 30
            iconName: "scissors"
            iconSize: 13
            help: "Dividir (Ctrl+B)"
            onClicked: Editor.splitAtPlayhead()
        }
        Rectangle { visible: bar.canSplit; width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.rgba(1, 1, 1, 0.1) }
        GlyphButton {
            width: 30
            height: 30
            iconName: "copy-plus"
            iconSize: 13
            help: "Duplicar (Ctrl+D)"
            onClicked: Editor.duplicateSelection()
        }
        Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.rgba(1, 1, 1, 0.1) }
        GlyphButton {
            visible: bar.canExtract
            width: 30
            height: 30
            iconName: "audio-lines"
            iconSize: 13
            help: "Extrair áudio"
            onClicked: Editor.extractAudio()
        }
        Rectangle { visible: bar.canExtract; width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.rgba(1, 1, 1, 0.1) }
        GlyphButton {
            width: 30
            height: 30
            iconName: "trash-2"
            iconSize: 13
            help: "Apagar (Delete)"
            onClicked: Editor.deleteSelection()
        }
    }
}
