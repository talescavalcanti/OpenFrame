import QtQuick
import QtQuick.Templates as T

// Timeline: régua em pílula, faixas, clipes, playhead-agulha, transições na
// faixa principal e a barra de ações do clipe. A rolagem horizontal fica no
// Editor (o zoom ancora no playhead ou no cursor); a vertical, aqui.
Item {
    id: timeline

    // A geometria vertical (cada faixa com a sua altura) vem do Editor: rowTops, rowHeights, rowAt.
    readonly property real rulerHeight: 30
    readonly property real headerWidth: 58
    readonly property real clipInset: 4
    readonly property real actionBarHeight: 30
    readonly property real reserve: actionBarHeight + 6

    function rowTop(row) { return Editor.rowTops[row] ?? Editor.rowTop(row) }
    function rowHeight(row) { return Editor.rowHeights[row] ?? 50 }
    /// Linha sob a posição `y` do conteúdo; -1 sobre a régua.
    function rowAt(y) { return Editor.rowAt(y) }

    readonly property int mainRow: {
        for (let i = 0; i < Editor.tracks.count; ++i)
            if (Editor.tracks.get(i).isMain) return i
        return -1
    }

    TimelineToolbar {
        id: toolbar
        width: parent.width
    }
    HDivider { y: toolbar.height }

    Item {
        id: body
        y: toolbar.height + 1
        width: parent.width
        height: parent.height - y
        clip: true

        readonly property real contentHeight: 8 + Editor.rowsHeight + timeline.reserve + 8
        property real scrollY: 0
        readonly property real maxScrollY: Math.max(0, contentHeight - height)
        onMaxScrollYChanged: scrollY = Math.min(scrollY, maxScrollY)
        function scrollVertically(dy) { scrollY = Math.max(0, Math.min(maxScrollY, scrollY + dy)) }

        // Roda: Ctrl/Alt = zoom no cursor; Shift ou roda horizontal = rolar na
        // horizontal; senão, rola na vertical quando há faixas escondidas, ou na horizontal.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x / 2
                const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
                if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) {
                    const amount = event.pixelDelta.y !== 0 ? event.pixelDelta.y * 0.01 : event.angleDelta.y / 120 * 0.12
                    Editor.zoomAtViewportX(Math.exp(amount), Math.max(0, event.x - timeline.headerWidth))
                } else if (event.modifiers & Qt.ShiftModifier) {
                    Editor.scrollTimelineBy(-(dy !== 0 ? dy : dx))
                } else if (Math.abs(dx) > Math.abs(dy)) {
                    Editor.scrollTimelineBy(-dx)
                } else if (body.maxScrollY > 0) {
                    body.scrollVertically(-dy)
                } else {
                    Editor.scrollTimelineBy(-dy)
                }
                event.accepted = true
            }
        }

        // Cabeçalhos das faixas (rolam só na vertical).
        Item {
            width: timeline.headerWidth
            height: body.height
            clip: true
            Repeater {
                model: Editor.tracks
                TrackHeader {
                    required property int row
                    y: 8 - body.scrollY + timeline.rowTop(row)
                    height: timeline.rowHeight(row)
                }
            }
        }

        Item {
            id: viewport
            x: timeline.headerWidth
            width: body.width - timeline.headerWidth
            height: body.height
            clip: true

            Binding { target: Editor; property: "timelineViewportWidth"; value: viewport.width }

            Item {
                id: content
                x: -Editor.timelineScrollX
                y: 8 - body.scrollY
                width: Editor.timelineContentWidth
                height: body.contentHeight - 16
                readonly property real visibleLeft: Editor.timelineScrollX
                readonly property real visibleRight: Editor.timelineScrollX + viewport.width

                // Régua: só a parte visível é desenhada.
                TimelineRuler {
                    x: Editor.timelineScrollX
                    width: viewport.width
                    height: timeline.rulerHeight
                    zoom: Editor.zoom
                    offset: Editor.timelineScrollX
                    contentWidth: Editor.timelineContentWidth - 24
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        onPressed: (m) => Editor.scrub(Math.max(0, m.x + Editor.timelineScrollX) / Editor.zoom)
                        onPositionChanged: (m) => Editor.scrub(Math.max(0, m.x + Editor.timelineScrollX) / Editor.zoom)
                    }
                }

                // Fundo das faixas; clique tira a seleção e leva o cursor.
                Repeater {
                    model: Editor.tracks
                    Rectangle {
                        required property int row
                        required property bool isMain
                        required property bool hidden
                        readonly property bool highlighted: Editor.dragTargetRow === row || dropArea.previewRow === row
                        y: timeline.rowTop(row)
                        width: Editor.timelineContentWidth - 24
                        height: timeline.rowHeight(row)
                        radius: 12
                        color: Qt.rgba(1, 1, 1, highlighted ? 0.07 : isMain ? 0.035 : 0.018)
                        border.color: highlighted ? Theme.alpha(Theme.accent, 0.5) : "transparent"
                        opacity: hidden ? 0.4 : 1
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => {
                                Editor.selection = ""
                                Editor.seek(m.x / Editor.zoom)
                            }
                        }
                    }
                }
                Repeater {
                    model: Editor.clips
                    ClipItem {
                        visibleLeft: content.visibleLeft
                        visibleRight: content.visibleRight
                    }
                }

                // Junção entre clipes da faixa principal: "+" de transição (ou o
                // ícone da transição). Sem transição, só aparece com o mouse perto
                // ou com um dos dois clipes selecionado.
                HoverHandler {
                    id: rowHover
                    enabled: timeline.mainRow >= 0
                }
                Repeater {
                    model: Editor.junctions
                    T.AbstractButton {
                        id: junction
                        required property string uid
                        required property real time
                        required property bool hasTransition
                        required property string nextId
                        required property string help
                        readonly property real centerX: time * Editor.zoom
                        readonly property real mainHeight: timeline.rowHeight(timeline.mainRow)
                        readonly property bool near: rowHover.hovered
                            && Math.abs(rowHover.point.position.y - (timeline.rowTop(timeline.mainRow) + mainHeight / 2)) < mainHeight / 2
                            && Math.abs(rowHover.point.position.x - centerX) < 40
                        readonly property bool shown: hasTransition || near || Editor.selection === uid || Editor.selection === nextId
                        // Menor numa faixa baixa, para cobrir menos os nomes; abaixo do nome do clipe e,
                        // numa faixa alta, no meio das miniaturas (acima da forma de onda do som).
                        readonly property real size: mainHeight < 40 ? 16 : 20
                        readonly property real wave: mainHeight >= 80 ? Math.max(20, Math.round((mainHeight - 2 * timeline.clipInset) * 0.28)) : 0
                        x: centerX - size / 2
                        y: timeline.rowTop(timeline.mainRow)
                           + (mainHeight < 40 ? (mainHeight - size) / 2
                              : mainHeight < 64 ? mainHeight - timeline.clipInset - size - 4
                              : (mainHeight - wave - size) / 2)
                        z: 5
                        width: size
                        height: size
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        opacity: shown ? 1 : 0
                        enabled: shown
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        background: Rectangle {
                            radius: 6
                            color: junction.hasTransition ? Theme.accent : Theme.raisedHigh
                            border.color: junction.hasTransition ? "transparent" : Qt.rgba(1, 1, 1, 0.18)
                        }
                        contentItem: Item {
                            Icon {
                                anchors.centerIn: parent
                                name: junction.hasTransition ? "arrow-left-right" : "plus"
                                size: junction.size < 20 ? 9 : 11
                                color: junction.hasTransition ? Theme.textOnAccent : Theme.textPrimary
                            }
                        }
                        onClicked: {
                            Editor.selection = uid
                            Editor.inspectorTab = "transition"
                        }
                        Tip { text: junction.help }
                    }
                }

                // Onde o material arrastado da biblioteca vai cair.
                Rectangle {
                    visible: dropArea.containsDrag
                    x: dropArea.previewStart * Editor.zoom - 1
                    width: 2
                    height: content.height
                    color: Theme.accent
                    z: 6
                }
                DashedRect {
                    visible: dropArea.containsDrag && dropArea.previewRow >= 0 && dropArea.previewDuration > 0
                    x: dropArea.previewStart * Editor.zoom
                    y: timeline.rowTop(Math.max(0, dropArea.previewRow)) + timeline.clipInset
                    width: Math.max(4, dropArea.previewDuration * Editor.zoom)
                    height: timeline.rowHeight(Math.max(0, dropArea.previewRow)) - timeline.clipInset * 2
                    radius: 9
                    lineWidth: 1.5
                    dash: 5
                    gap: 4
                    color: Theme.accent
                    fill: Theme.alpha(Theme.accent, 0.12)
                    z: 6
                }

                ClipActionBar {
                    z: 9
                    viewportWidth: viewport.width
                }
            }

            DropArea {
                id: dropArea
                anchors.fill: parent
                keys: ["openframe/material"]
                property real previewStart: 0
                property real previewDuration: 0
                property int previewRow: -1
                function update(d) {
                    const time = Math.max(0, d.x + Editor.timelineScrollX) / Editor.zoom
                    const row = timeline.rowAt(d.y - content.y)
                    const p = Editor.dropPreview(d.source.materialId, time, row)
                    previewStart = p.start
                    previewDuration = p.duration
                    previewRow = p.row
                }
                onEntered: (d) => update(d)
                onPositionChanged: (d) => update(d)
                onExited: previewRow = -1
                onDropped: (d) => {
                    const time = Math.max(0, d.x + Editor.timelineScrollX) / Editor.zoom
                    Editor.dropMaterial(d.source.materialId, time, timeline.rowAt(d.y - content.y))
                    previewRow = -1
                }
            }

            // Barra de rolagem horizontal (aparece ao rolar ou com o mouse em cima).
            OfScrollBar {
                orientation: Qt.Horizontal
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                size: viewport.width / Math.max(Editor.timelineContentWidth, 1)
                position: Editor.timelineScrollX / Math.max(Editor.timelineContentWidth, 1)
                active: hovered || pressed
                onPositionChanged: if (pressed) Editor.timelineScrollX = position * Editor.timelineContentWidth
            }
        }
        // Playhead numa camada própria, que começa meia cabeça antes da área das
        // faixas: em 0 s a cabeça aparece inteira em vez de cortada.
        Item {
            x: timeline.headerWidth - 30
            width: viewport.width + 30
            height: body.height
            clip: true
            Playhead {
                originX: 30 - Editor.timelineScrollX
                y: content.y
                height: content.height + 8
            }
        }
        OfScrollBar {
            orientation: Qt.Vertical
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            size: body.height / Math.max(body.contentHeight, 1)
            position: body.scrollY / Math.max(body.contentHeight, 1)
            active: hovered || pressed
            onPositionChanged: if (pressed) body.scrollY = position * body.contentHeight
        }
    }
}
