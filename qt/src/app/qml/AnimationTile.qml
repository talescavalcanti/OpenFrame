import QtQuick
import QtQuick.Templates as T

// Cartão da aba Animação: a miniatura animada do próprio clipe (toca no hover e
// no selecionado) ou, sem os quadros, o ícone. O nome curto fica no cartão e o
// completo na dica.
T.AbstractButton {
    id: tile
    property bool selected: false
    property string iconName
    property string segment
    /// "entry:<tipo>", "exit:<tipo>", "loop:<tipo>" ou "" (só o repouso).
    property string part
    property string help

    implicitWidth: 80
    implicitHeight: 50 + 6 + 15
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        Rectangle {
            id: box
            width: parent.width
            height: 50
            radius: 8
            color: Theme.raised
            clip: true
            AnimatedThumb {
                id: thumb
                anchors.fill: parent
                anchors.margins: 1
                visible: hasFrames
                segment: tile.segment
                part: tile.part
                playing: tile.part !== "" && (tile.hovered || tile.selected)
            }
            Icon {
                visible: !thumb.hasFrames
                anchors.centerIn: parent
                name: tile.iconName
                size: 18
                color: tile.selected ? Theme.accent : Theme.textSecondary
            }
        }
        Rectangle {
            anchors.fill: box
            radius: 8
            color: "transparent"
            border.width: tile.selected ? 1.5 : 1
            border.color: tile.selected ? Theme.accent : (tile.hovered ? Qt.rgba(1, 1, 1, 0.22) : Theme.hairline)
        }
        OfText {
            y: 56
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: tile.text
            size: 11
            elide: Text.ElideRight
            weight: tile.selected ? Theme.medium : Theme.regular
            color: tile.selected ? Theme.accent : (tile.hovered ? Theme.textPrimary : Theme.textSecondary)
        }
    }
    Tip { target: tile; text: tile.help }
}
