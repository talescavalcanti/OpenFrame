import QtQuick

// Cabeçalho da faixa: ícone do tipo, silenciar e ocultar. Arrastar a borda de baixo
// muda a altura da faixa; duplo clique nela volta à padrão.
Item {
    id: header
    required property string uid
    required property string kind
    required property bool isMain
    required property bool muted
    required property bool hidden
    required property string icon

    width: 58
    height: 50

    Column {
        anchors.centerIn: parent
        spacing: 7
        // Em faixas baixas, só os botões.
        Icon {
            visible: header.height >= 44
            anchors.horizontalCenter: parent.horizontalCenter
            name: header.icon
            size: 13
            color: header.isMain ? Theme.textSecondary : Theme.textTertiary
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 9
            GlyphButton {
                visible: header.kind !== "text"
                iconName: header.muted ? "volume-x" : "volume-2"
                iconSize: 12
                active: header.muted
                help: header.muted ? "Ativar som" : "Silenciar faixa"
                onClicked: Editor.toggleTrack(header.uid, "muted")
            }
            GlyphButton {
                visible: header.kind !== "audio"
                iconName: header.hidden ? "eye-off" : "eye"
                iconSize: 12
                active: header.hidden
                help: header.hidden ? "Mostrar faixa" : "Ocultar faixa"
                onClicked: Editor.toggleTrack(header.uid, "hidden")
            }
        }
    }

    // Borda de baixo, centrada no espaço entre as faixas.
    MouseArea {
        id: edge
        objectName: "rowEdge"
        y: header.height - 1
        width: parent.width
        height: 8
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeVerCursor
        readonly property bool hovered: containsMouse && !pressed // para a dica
        property real startHeight: 0
        property real startY: 0
        onPressed: (m) => {
            startHeight = header.height
            startY = mapToItem(null, m.x, m.y).y
        }
        onPositionChanged: (m) => {
            if (pressed)
                Editor.resizeRow(header.uid, startHeight + mapToItem(null, m.x, m.y).y - startY)
        }
        onReleased: Editor.endRowResize()
        onCanceled: Editor.endRowResize()
        onDoubleClicked: {
            Editor.resizeRow(header.uid, 0)
            Editor.endRowResize()
        }
        Rectangle {
            anchors.centerIn: parent
            width: 22
            height: 2
            radius: 1
            color: Theme.accent
            opacity: edge.containsMouse || edge.pressed ? 0.8 : 0
        }
        Tip { text: "Arraste para mudar a altura da faixa (duplo clique: padrão)" }
    }
}
