import QtQuick
import "Fmt.js" as Fmt

// Aba Animação: grades de Entrada, Saída e Loop. Escolher toca o trecho no
// preview e volta ao playhead.
Column {
    id: tab
    readonly property var seg: Editor.selectedSegment
    readonly property var kinds: Editor.animationOptions(seg.isText === true)
    readonly property var loop: Fmt.at(seg, "loop", null)
    spacing: 12

    component Grid: Column {
        id: grid
        property string title
        property string key
        property string which
        readonly property var anim: Fmt.at(tab.seg, key, null)
        width: parent.width
        spacing: 12
        SectionHeader { title: grid.title }
        TileGrid {
            id: tiles
            AnimationTile {
                width: tiles.tileWidth
                text: "Nenhuma"
                iconName: "ban"
                segment: tab.seg.id || ""
                selected: grid.anim === null
                onClicked: Editor.setAnimation(grid.which, "")
            }
            Repeater {
                model: tab.kinds
                AnimationTile {
                    required property var modelData
                    width: tiles.tileWidth
                    text: modelData.short
                    help: modelData.title
                    iconName: modelData.icon
                    segment: tab.seg.id || ""
                    part: (grid.which === "in" ? "entry:" : "exit:") + modelData.value
                    selected: grid.anim !== null && grid.anim.kind === modelData.value
                    onClicked: Editor.setAnimation(grid.which, modelData.value)
                }
            }
        }
        SegSlider {
            visible: grid.anim !== null
            title: "Duração"
            path: grid.key + ".duration"
            scale: 1e-6
            from: 0.1; to: 3; defaultValue: 0.5
            format: Fmt.seconds
        }
    }

    Grid { title: "Entrada"; key: "animationIn"; which: "in" }
    Grid { title: "Saída"; key: "animationOut"; which: "out" }

    SectionHeader { title: "Loop" }
    TileGrid {
        id: loops
        AnimationTile {
            width: loops.tileWidth
            text: "Nenhum"
            iconName: "ban"
            segment: tab.seg.id || ""
            selected: tab.loop === null
            onClicked: Editor.setLoop("")
        }
        Repeater {
            model: Editor.loopOptions()
            AnimationTile {
                required property var modelData
                width: loops.tileWidth
                text: modelData.short
                help: modelData.title
                iconName: modelData.icon
                segment: tab.seg.id || ""
                part: "loop:" + modelData.value
                selected: tab.loop !== null && tab.loop.kind === modelData.value
                onClicked: Editor.setLoop(modelData.value)
            }
        }
    }
    ParamSlider {
        visible: tab.loop !== null
        title: "Ciclo"
        value: tab.loop ? tab.loop.periodSeconds : 2
        from: 0.5; to: 6; defaultValue: 2
        format: Fmt.seconds
        onMoved: (v) => Editor.setLoopValue("periodSeconds", v)
    }
    ParamSlider {
        visible: tab.loop !== null
        title: "Intensidade"
        value: tab.loop ? tab.loop.intensity : 0.5
        from: 0; to: 1; defaultValue: 0.5
        format: (v) => Math.round(v * 100) + "%"
        onMoved: (v) => Editor.setLoopValue("intensity", v)
    }
    Note { text: "O loop se repete durante todo o clipe, junto com a entrada e a saída." }
}
