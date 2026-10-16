import QtQuick
import "Fmt.js" as Fmt

// Volume e fades; num clipe com o som extraído, só o aviso.
Column {
    id: controls
    objectName: "audioControls"
    width: parent ? parent.width : implicitWidth
    spacing: 12
    readonly property bool detached: Editor.selectedSegment.audioDetached === true
    SectionHeader { title: "Som" }
    Row {
        visible: controls.detached
        objectName: "audioDetachedNote"
        width: parent.width
        spacing: 7
        Icon { id: noteIcon; name: "volume-off"; size: 12; color: Theme.textTertiary; y: 1 }
        Note {
            width: parent.width - noteIcon.width - 7
            text: "O áudio deste clipe foi extraído para a faixa de áudio."
        }
    }
    SegSlider { visible: !controls.detached; title: "Volume"; path: "volume"; from: 0; to: 1; defaultValue: 1; format: Fmt.percent }
    SegSlider { visible: !controls.detached; title: "Fade de entrada"; path: "fadeIn"; scale: 1e-6; from: 0; to: 5; format: Fmt.seconds }
    SegSlider { visible: !controls.detached; title: "Fade de saída"; path: "fadeOut"; scale: 1e-6; from: 0; to: 5; format: Fmt.seconds }
}
