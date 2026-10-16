import QtQuick

// Clipe na timeline: fundo cinza pelo tipo, miniaturas ou forma de onda,
// nome e indicadores dentro do clipe; selecionado ganha borda, brilho e alças
// ciano. Arrastar move (também para outra faixa); as alças aparam. O conteúdo
// acompanha a altura da faixa: numa alta, o vídeo mostra o som embaixo das
// miniaturas e o texto aparece inteiro; numa baixa, só o essencial.
Item {
    id: root
    required property string uid
    required property int row
    required property real start
    required property real duration
    required property string kind
    required property string icon
    required property string title
    required property string badges
    required property string materialId
    required property real sourceStart
    required property real sourceDuration
    required property real materialDuration
    required property real aspect
    required property real volume
    required property bool isMain
    required property bool dragged
    required property int rowShift
    required property bool hidden
    required property bool hasAudio
    required property string fullText
    required property string durationText
    required property bool autoCaption
    required property bool audioDetached
    required property bool canExtractAudio

    /// Janela visível da timeline, em coordenadas do conteúdo.
    property real visibleLeft: 0
    property real visibleRight: 0

    readonly property bool selected: Editor.selection === uid
    readonly property real radius: 9
    readonly property real rowHeight: Editor.rowHeights[row] ?? 50
    /// Faixa baixa: só o essencial (ícone e nome, sem indicadores).
    readonly property bool compact: rowHeight < 40
    /// Faixa alta: a duração no nome e o texto em várias linhas.
    readonly property bool tall: rowHeight >= 64
    /// O som do vídeo numa faixa embaixo das miniaturas.
    readonly property bool videoWave: kind === "video" && hasAudio && !audioDetached && rowHeight >= 80 && videoPeaks.hasPeaks
    readonly property real videoWaveHeight: Math.max(20, Math.round(height * 0.28))

    x: start * Editor.zoom
    y: (Editor.rowTops[row + rowShift] ?? 0) + 4
    width: Math.max(4, duration * Editor.zoom)
    height: rowHeight - 8
    z: dragged ? 3 : selected ? 2 : 1
    opacity: hidden ? 0.4 : 1

    Behavior on x {
        enabled: Editor.slideAnimation && !root.dragged
        NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
    }
    Behavior on y {
        enabled: Editor.slideAnimation && !root.dragged
        NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
    }

    // Parte visível do clipe (os itens desenhados só pintam essa janela).
    readonly property real visL: Math.max(0, visibleLeft - x)
    readonly property real visR: Math.min(width, visibleRight - x)

    // Brilho da seleção.
    Rectangle {
        visible: root.selected
        anchors.fill: parent
        anchors.margins: -4
        radius: root.radius + 4
        color: "transparent"
        border.width: 4
        border.color: Theme.alpha(Theme.accent, 0.14)
    }
    Rectangle {
        id: body
        anchors.fill: parent
        radius: root.radius
        clip: true
        color: root.kind === "text" ? Theme.clipText
             : root.kind === "audio" ? Theme.clipAudio
             : root.kind === "missing" ? Theme.raised : Theme.clipVideo

        // Os itens pintados só aparecem com ao menos 1 px: menos que isso (a ponta de um clipe saindo da
        // janela visível ao rolar) dá uma imagem vazia e o aviso do QPainter.
        ThumbnailStrip {
            visible: (root.kind === "video" || root.kind === "photo") && width >= 1
            x: Math.max(2, root.visL)
            y: 2
            width: Math.max(0, Math.min(root.width - 2, root.visR) - x)
            height: root.height - 4 - (root.videoWave ? root.videoWaveHeight - 2 : 0)
            offset: x - 2
            fullWidth: root.width - 4
            radius: root.radius - 2
            materialId: root.materialId
            video: root.kind === "video"
            aspect: root.aspect
            sourceStart: root.sourceStart
            sourceDuration: root.sourceDuration
            materialDuration: root.materialDuration
        }
        Rectangle {
            visible: root.videoWave
            y: root.height - height
            width: root.width
            height: root.videoWaveHeight
            color: Theme.clipAudio
            WaveformView {
                id: videoPeaks
                visible: width >= 1
                x: Math.max(6, root.visL)
                y: 3
                width: Math.max(0, Math.min(root.width - 6, root.visR) - x)
                height: parent.height - 6
                offset: x - 6
                fullWidth: root.width - 12
                materialId: root.kind === "video" && root.hasAudio ? root.materialId : ""
                from: root.sourceStart
                to: root.sourceStart + root.sourceDuration
                bar: 1.5
                gap: 1
                color: root.selected ? Theme.alpha(Theme.accent, 0.7) : Theme.waveform
                opacity: 0.4 + 0.6 * Math.min(root.volume, 1)
            }
        }
        // Áudio: a metade de baixo na altura padrão, o clipe inteiro numa faixa baixa
        // e, numa alta, tudo abaixo do nome.
        WaveformView {
            visible: root.kind === "audio" && width >= 1
            x: Math.max(6, root.visL)
            width: Math.max(0, Math.min(root.width - 6, root.visR) - x)
            height: root.compact ? root.height - 4 : root.tall ? root.height - 28 - 5 : root.height * 0.5
            y: root.height - height - (root.compact ? 2 : 5)
            bar: root.tall ? 1.5 : 2
            gap: root.tall ? 1 : 1.5
            offset: x - 6
            fullWidth: root.width - 12
            materialId: root.materialId
            from: root.sourceStart
            to: root.sourceStart + root.sourceDuration
            color: root.selected ? Theme.alpha(Theme.accent, 0.7) : Theme.waveform
            opacity: 0.4 + 0.6 * Math.min(root.volume, 1)
        }

        // Faixa alta: o texto legível, com as linhas que couberem.
        OfText {
            visible: root.kind === "text" && root.tall
            x: Math.max(9, Math.min(root.visL + 9, root.width - width - 9))
            y: 28
            width: Math.max(0, Math.min(root.width, root.visR) - Math.max(0, root.visL) - 18)
            text: root.fullText
            size: root.rowHeight >= 90 ? 12 : 11
            color: Theme.alpha(Theme.textPrimary, 0.85)
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: Math.max(1, Math.floor((root.height - 34) / 15))
        }

        // Ícone e nome dentro do clipe, mais os indicadores de ajustes (e a duração, numa faixa alta).
        Rectangle {
            readonly property real pad: root.compact ? 5 : 7
            visible: root.width > 46
            x: Math.max(root.compact ? 3 : 6, Math.min(root.visL + 6, root.width - width - 6))
            y: root.compact ? 3 : 6
            height: root.compact ? 14 : 18
            radius: height / 2
            width: Math.min(chip.implicitWidth + 2 * pad, root.width - 12)
            color: root.kind === "text" ? "transparent" : Qt.rgba(0, 0, 0, 0.55)
            clip: true
            Row {
                id: chip
                x: parent.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                Icon { anchors.verticalCenter: parent.verticalCenter; name: root.icon; size: root.compact ? 9 : 10; color: Theme.textPrimary }
                OfText {
                    anchors.verticalCenter: parent.verticalCenter
                    // Numa faixa alta, o texto vai inteiro no corpo do clipe e o nome diz o tipo.
                    text: root.kind === "text" && root.tall ? (root.autoCaption ? "Legenda" : "Texto") : root.title
                    size: root.compact ? 9 : 10
                    weight: Theme.medium
                    width: Math.min(implicitWidth, Math.max(0, root.width - 12 - 14 - 15
                                                             - (badge.visible ? badge.implicitWidth + 5 : 0)
                                                             - (root.audioDetached ? 15 : 0)
                                                             - (durationLabel.visible ? durationLabel.implicitWidth + 5 : 0)))
                }
                // O som foi extraído para a faixa de áudio.
                Icon {
                    visible: root.audioDetached
                    anchors.verticalCenter: parent.verticalCenter
                    name: "volume-off"
                    size: root.compact ? 9 : 10
                    color: Theme.textSecondary
                }
                OfText {
                    id: badge
                    visible: root.badges !== "" && !root.compact
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.badges
                    size: 9
                    weight: Theme.medium
                    color: Theme.textSecondary
                }
                OfText {
                    id: durationLabel
                    visible: root.tall
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.durationText
                    size: 9
                    mono: true
                    color: Theme.textTertiary
                }
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: root.selected ? 2 : 1
        border.color: root.selected ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
    }

    MouseArea {
        id: area
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        property point pressPoint
        property bool moving: false
        onPressed: (m) => {
            pressPoint = mapToItem(null, m.x, m.y)
            moving = false
        }
        onPositionChanged: (m) => {
            if (!(m.buttons & Qt.LeftButton)) return
            const p = mapToItem(null, m.x, m.y)
            const dx = p.x - pressPoint.x, dy = p.y - pressPoint.y
            if (!moving && Math.hypot(dx, dy) < 4) return
            moving = true
            // A linha sob o cursor (as faixas podem ter alturas diferentes).
            Editor.dragMove(root.uid, dx, Editor.rowAt(mapToItem(root.parent, m.x, m.y).y) - root.row)
        }
        onReleased: (m) => {
            if (moving) {
                moving = false
                Editor.endDragMove(root.uid)
            } else if (m.button === Qt.RightButton) {
                Editor.selection = root.uid
                menu.popup()
            } else {
                Editor.selection = root.uid
            }
        }
        onCanceled: {
            if (moving) Editor.endDragMove(root.uid)
            moving = false
        }
    }

    component Handle: Item {
        id: handle
        property bool leading
        width: 9
        height: root.height
        clip: true
        Rectangle {
            x: handle.leading ? 0 : -root.radius
            width: handle.width + root.radius
            height: parent.height
            radius: root.radius
            color: Theme.accent
        }
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: Math.min(14, root.height - 10)
            radius: 1
            color: Theme.alpha(Theme.textOnAccent, 0.75)
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -3
            cursorShape: Qt.SizeHorCursor
            preventStealing: true
            property real pressX
            onPressed: (m) => pressX = mapToItem(null, m.x, m.y).x
            onPositionChanged: (m) => {
                const dx = mapToItem(null, m.x, m.y).x - pressX
                if (Math.abs(dx) >= 1) Editor.dragTrim(root.uid, handle.leading, dx)
            }
            onReleased: Editor.endDragTrim()
            onCanceled: Editor.endDragTrim()
        }
    }
    Handle { visible: root.selected; leading: true }
    Handle { visible: root.selected; leading: false; x: root.width - width }

    OfMenu {
        id: menu
        onOpened: Editor.modalCount += 1
        onClosed: Editor.modalCount -= 1
        OfMenuItem { text: "Dividir no cursor"; onTriggered: { Editor.selection = root.uid; Editor.splitAtPlayhead() } }
        OfMenuItem { text: "Duplicar"; onTriggered: { Editor.selection = root.uid; Editor.duplicateSelection() } }
        OfMenuItem {
            text: "Extrair áudio"
            visible: root.canExtractAudio
            height: visible ? implicitHeight : 0
            onTriggered: { Editor.selection = root.uid; Editor.extractAudio() }
        }
        OfMenuSeparator {}
        OfMenuItem { text: "Apagar"; destructive: true; onTriggered: { Editor.selection = root.uid; Editor.deleteSelection() } }
    }
}
