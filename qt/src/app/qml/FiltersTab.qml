import QtQuick
import "Fmt.js" as Fmt

Column {
    id: tab
    readonly property var seg: Editor.selectedSegment
    readonly property string current: Fmt.at(seg, "filter.preset", "none")
    spacing: 12

    TileGrid {
        id: grid
        topPadding: 8
        Repeater {
            model: Editor.filterOptions()
            ChoiceTile {
                required property var modelData
                width: grid.tileWidth
                text: modelData.title
                selected: tab.current === modelData.value
                iconName: "blend"
                preview: Editor.thumbnailRevision >= 0 ? Editor.filterPreview(tab.seg.materialId || "", modelData.value) : ""
                onClicked: Editor.setFilter(modelData.value)
            }
        }
    }
    SegSlider {
        visible: tab.current !== "none"
        title: "Intensidade"
        path: "filter.intensity"
        from: 0; to: 1; defaultValue: 1
        format: Fmt.percent
    }
}
