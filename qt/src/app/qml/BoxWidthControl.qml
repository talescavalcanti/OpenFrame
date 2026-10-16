import QtQuick

// "Largura da caixa": slider e campo em % da largura do quadro, pela mesma função
// das alças do preview. Passa de 100% para caber numa linha só; numa legenda
// automática vale para todas as legendas da faixa.
Column {
    id: root
    property real value: 0.9
    property bool wholeTrack: false
    readonly property real defaultValue: 0.9
    readonly property real minimum: 0.1
    readonly property real maximum: 3
    signal moved(real value)

    width: parent ? parent.width : implicitWidth
    spacing: 5

    function commit() {
        const typed = parseInt(field.text)
        if (isNaN(typed)) {
            field.text = Math.round(root.value * 100)
            return
        }
        const w = Math.max(root.minimum, Math.min(root.maximum, typed / 100))
        if (Math.round(w * 100) !== Math.round(root.value * 100))
            root.moved(w)
        field.text = Math.round(w * 100)
    }

    Item {
        width: parent.width
        height: 22
        OfText {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - field.width - percent.width - 10
            text: root.wholeTrack ? "Largura da caixa · todas as legendas" : "Largura da caixa"
            size: 12
            elide: Text.ElideRight
            color: Theme.textSecondary
            MouseArea {
                id: labelArea
                anchors.fill: parent
                hoverEnabled: true
                readonly property bool hovered: containsMouse
                onDoubleClicked: root.moved(root.defaultValue)
                Tip {
                    target: labelArea
                    text: root.wholeTrack ? "Vale para todas as legendas automáticas da faixa. Clique duas vezes para redefinir."
                                          : "Clique duas vezes para redefinir"
                }
            }
        }
        InputField {
            id: field
            objectName: "boxWidthField"
            anchors.right: percent.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            implicitHeight: 22
            leftPadding: 6
            rightPadding: 6
            horizontalAlignment: TextInput.AlignRight
            monoDigits: true
            validator: IntValidator { bottom: 0; top: 999 }
            text: Math.round(root.value * 100)
            onEditingFinished: root.commit()
        }
        OfText {
            id: percent
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "%"
            size: 12
            color: Theme.textTertiary
        }
    }
    ThinSlider {
        width: parent.width
        value: root.value
        from: root.minimum
        to: root.maximum
        onMoved: (v) => root.moved(v)
    }
    Connections {
        target: root
        function onValueChanged() { if (!field.activeFocus) field.text = Math.round(root.value * 100) }
    }
}
