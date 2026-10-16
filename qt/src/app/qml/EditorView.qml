import QtCore
import QtQuick
import QtQuick.Layouts

// Editor em painéis flutuantes sobre o preto: barra de ferramentas → biblioteca
// (recolhível) → preview → inspetor; timeline embaixo, com altura ajustável.
Item {
    id: editor
    objectName: "editor"
    readonly property real barHeight: 52

    Settings {
        id: settings
        category: "editor"
        property real timelineHeight: 340
    }
    property real heightBeforeMaximize: -1
    readonly property real maxTimelineHeight: Math.max(190, height - barHeight - 360)
    readonly property real timelineHeight: Math.min(Math.max(settings.timelineHeight, 190), maxTimelineHeight)

    /// Maximiza a timeline ou volta à altura de antes.
    function toggleMaximized() {
        if (timelineHeight >= maxTimelineHeight - 1) {
            settings.timelineHeight = heightBeforeMaximize > 0 ? heightBeforeMaximize : 340
            heightBeforeMaximize = -1
        } else {
            heightBeforeMaximize = timelineHeight
            settings.timelineHeight = maxTimelineHeight
        }
    }

    TopBar {
        id: topBar
        width: parent.width
        height: editor.barHeight
    }

    RowLayout {
        id: panels
        x: 8
        y: editor.barHeight
        width: parent.width - 16
        height: parent.height - editor.barHeight - handle.height - editor.timelineHeight - 8
        spacing: 8

        LibraryRail {
            Layout.alignment: Qt.AlignTop
        }
        Panel {
            objectName: "library"
            visible: Editor.libraryVisible
            Layout.preferredWidth: 272
            Layout.fillHeight: true
            LibraryView { anchors.fill: parent }
        }
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 420
            PreviewPanel { anchors.fill: parent }
        }
        Panel {
            objectName: "inspector"
            Layout.preferredWidth: 308
            Layout.fillHeight: true
            InspectorView { anchors.fill: parent }
        }
    }

    // Faixa fina entre o preview e a timeline: arrastar muda a altura da
    // timeline e o duplo clique maximiza.
    Item {
        id: handle
        y: panels.y + panels.height
        width: parent.width
        height: 10
        Rectangle {
            anchors.centerIn: parent
            width: 36
            height: 3
            radius: 1.5
            color: Qt.rgba(1, 1, 1, handleArea.containsMouse || handleArea.pressed ? 0.28 : 0.12)
        }
        MouseArea {
            id: handleArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            property real startY
            property real startHeight
            onPressed: (m) => {
                startY = mapToItem(editor, m.x, m.y).y
                startHeight = editor.timelineHeight
            }
            onPositionChanged: (m) => {
                if (!pressed) return
                settings.timelineHeight = startHeight - (mapToItem(editor, m.x, m.y).y - startY)
                editor.heightBeforeMaximize = -1
            }
            onDoubleClicked: editor.toggleMaximized()
            readonly property bool hovered: containsMouse
            Tip { target: handleArea; text: "Arraste para redimensionar a timeline; clique duas vezes para maximizar" }
        }
    }

    Panel {
        objectName: "timeline"
        x: 8
        y: handle.y + handle.height
        width: parent.width - 16
        height: editor.timelineHeight
        TimelineView { anchors.fill: parent }
    }
}
