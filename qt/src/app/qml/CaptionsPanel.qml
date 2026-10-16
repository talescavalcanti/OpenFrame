import QtQuick
import QtQuick.Templates as T

// Legendas: gerar automáticas, importar/exportar SRT e a lista dos textos da
// timeline, com edição em linha (duplo clique ou lápis; Enter grava, Esc cancela)
// e lixeira.
Item {
    id: panel
    objectName: "captionsPanel"
    /// Legenda em edição (vazio = nenhuma).
    property string editingId: ""

    Column {
        id: top
        x: 14
        y: 14
        width: parent.width - 28
        spacing: 12
        PanelHeader {
            title: "Legendas"
            detail: Editor.cues.count > 0 ? Editor.cues.count : ""
        }
        PillButton {
            width: parent.width
            fill: true
            kind: "primary"
            text: "Legendas automáticas"
            iconName: "audio-waveform"
            onClicked: Editor.presentAutoCaptions()
        }
        Row {
            width: parent.width
            spacing: 8
            PillButton {
                width: (parent.width - 8) / 2
                fill: true
                compact: true
                text: "Importar SRT"
                iconName: "download"
                onClicked: Editor.requestDialog("importSrt")
            }
            PillButton {
                width: (parent.width - 8) / 2
                fill: true
                compact: true
                text: "Exportar SRT"
                iconName: "upload"
                enabled: Editor.cues.count > 0
                onClicked: Editor.requestDialog("exportSrt")
            }
        }
        PillButton {
            visible: Editor.selectionIsAutoCaption
            width: parent.width
            fill: true
            compact: true
            text: "Aplicar este estilo a todas"
            iconName: "paintbrush"
            help: "Copia o estilo, o destaque e a palavra-chave desta legenda para todas as legendas automáticas."
            onClicked: Editor.applyStyleToAutoCaptions()
        }
        OfText {
            visible: Editor.cues.count === 0
            width: parent.width
            topPadding: 4
            text: "Nenhum texto na timeline. Importe um arquivo SRT ou adicione textos pelo painel Texto."
            size: 11
            color: Theme.textTertiary
            wrapMode: Text.Wrap
        }
    }

    ListView {
        id: list
        visible: Editor.cues.count > 0
        x: 14
        y: top.y + top.height + 12 - Theme.scrollFade / 2
        width: parent.width - 28
        height: parent.height - y
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        header: Item { height: Theme.scrollFade }
        model: Editor.cues
        T.ScrollBar.vertical: OfScrollBar {}
        delegate: T.AbstractButton {
            id: row
            required property string uid
            required property real start
            required property string startText
            required property string caption
            readonly property bool selected: Editor.selection === uid
            width: ListView.view.width
            implicitHeight: Math.max(textLabel.implicitHeight, 14) + 16
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            background: Rectangle {
                radius: 8
                color: row.selected || row.hovered ? Theme.raised : "transparent"
            }
            readonly property bool editing: panel.editingId === uid
            function commit(text) {
                Editor.setCaptionText(uid, text)
                panel.editingId = ""
            }
            contentItem: Item {
                OfText {
                    id: timeLabel
                    x: 10
                    y: 8
                    text: row.startText
                    size: 10
                    mono: true
                    color: row.selected ? Theme.accent : Theme.textTertiary
                    height: 15
                }
                OfText {
                    id: textLabel
                    visible: !row.editing
                    x: timeLabel.x + timeLabel.implicitWidth + 10
                    y: 8
                    width: parent.width - x - 10 - (row.hovered ? actions.width + 4 : 0)
                    text: row.caption
                    size: 12
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    verticalAlignment: Text.AlignTop
                    color: row.selected ? Theme.textPrimary : Theme.textSecondary
                }
                InputField {
                    id: field
                    visible: row.editing
                    x: textLabel.x - 4
                    y: 3
                    width: parent.width - x - 6
                    implicitHeight: 26
                    leftPadding: 6
                    rightPadding: 6
                    onVisibleChanged: if (visible) {
                        text = row.caption
                        forceActiveFocus()
                        selectAll()
                    }
                    Keys.onReturnPressed: row.commit(text)
                    Keys.onEnterPressed: row.commit(text)
                    Keys.onEscapePressed: panel.editingId = ""
                    // Trocar de linha grava a anterior.
                    onActiveFocusChanged: if (!activeFocus && row.editing) row.commit(text)
                }
                Row {
                    id: actions
                    visible: row.hovered && !row.editing
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    y: 4
                    spacing: 2
                    GlyphButton { iconName: "pencil"; help: "Editar o texto"; onClicked: panel.editingId = row.uid }
                    GlyphButton { iconName: "trash-2"; help: "Apagar a legenda"; onClicked: Editor.deleteCaption(row.uid) }
                }
            }
            onDoubleClicked: panel.editingId = uid
            onClicked: {
                Editor.selection = uid
                Editor.inspectorTab = "text"
                Editor.seek(start)
            }
        }
    }
    ScrollFade {
        visible: list.visible
        x: list.x
        y: list.y
        width: list.width
    }
}
