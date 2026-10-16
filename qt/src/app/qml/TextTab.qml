import QtQuick
import QtQuick.Templates as T
import "Fmt.js" as Fmt

Column {
    id: tab
    readonly property var seg: Editor.selectedSegment
    readonly property var style: Fmt.at(seg, "text.style", ({}))
    readonly property var highlight: Fmt.at(seg, "text.highlight", null)
    readonly property var emphasis: Fmt.at(seg, "text.emphasis", null)
    spacing: 12

    /// Famílias: as sugeridas primeiro, depois as instaladas (e a atual, se faltar).
    function familyOptions(current) {
        const suggested = Editor.suggestedFontFamilies()
        const all = Editor.fontFamilies()
        const list = suggested.map(f => ({ value: f, title: f }))
        for (let i = 0; i < all.length; ++i)
            if (suggested.indexOf(all[i]) < 0) list.push({ value: all[i], title: all[i] })
        if (current !== "" && all.indexOf(current) < 0) list.unshift({ value: current, title: current + " (ausente)" })
        return list
    }

    // Texto (várias linhas).
    Rectangle {
        width: parent.width
        height: 72
        radius: Theme.fieldRadius
        color: Theme.raised
        border.color: editor.activeFocus ? Theme.alpha(Theme.accent, 0.6) : Theme.hairline
        Flickable {
            id: textScroll
            anchors.fill: parent
            anchors.margins: 6
            contentHeight: editor.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            T.TextArea {
                id: editor
                width: textScroll.width
                // O template não tem tamanho implícito: sem isto a altura fica 0 e a linha sai cortada.
                implicitHeight: contentHeight + topPadding + bottomPadding
                text: Fmt.at(tab.seg, "text.text", "")
                color: Theme.textPrimary
                selectionColor: Theme.alpha(Theme.accent, 0.45)
                font.family: Theme.family
                font.pixelSize: 13
                wrapMode: TextEdit.Wrap
                padding: 2
                onTextChanged: if (activeFocus && text !== Fmt.at(tab.seg, "text.text", "")) Editor.setSegmentValue("text.text", text)
            }
        }
    }

    SectionHeader { title: "Estilos prontos" }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: Editor.textPresets()
            PillButton {
                required property var modelData
                compact: true
                text: modelData.title
                onClicked: Editor.applyTextPreset(modelData.value)
            }
        }
    }

    // Fonte
    SectionHeader { title: "Fonte" }
    FieldRow {
        label: "Família"
        MenuField {
            options: tab.familyOptions(tab.style.fontName || "")
            value: tab.style.fontName
            onPicked: (v) => Editor.setFontFamily(v)
        }
    }
    FieldRow {
        label: "Estilo"
        MenuField {
            options: [{ value: "", title: "Padrão" }].concat(Editor.fontFaces(tab.style.fontName || "").map(f => ({ value: f, title: f })))
            value: tab.style.fontFace || ""
            onPicked: (v) => Editor.setFontFace(v)
        }
    }
    SegSlider { title: "Tamanho"; path: "text.style.fontSize"; from: 0.02; to: 0.25; defaultValue: 0.07; format: Fmt.fontSize }
    Row {
        width: parent.width
        spacing: 6
        PillPicker {
            width: parent.width - 3 * (28 + 6)
            options: [
                { value: "left", title: "Esquerda", icon: "align-left" },
                { value: "center", title: "Centro", icon: "align-center" },
                { value: "right", title: "Direita", icon: "align-right" }
            ]
            value: tab.style.alignment
            onPicked: (v) => Editor.setSegmentValue("text.style.alignment", v)
        }
        CircleButton { size: 28; iconName: "bold"; on: tab.style.bold === true; help: "Negrito"; onClicked: Editor.setSegmentValue("text.style.bold", !tab.style.bold) }
        CircleButton { size: 28; iconName: "italic"; on: tab.style.italic === true; help: "Itálico"; onClicked: Editor.setSegmentValue("text.style.italic", !tab.style.italic) }
        CircleButton { size: 28; iconName: "underline"; on: tab.style.underline === true; help: "Sublinhado"; onClicked: Editor.setSegmentValue("text.style.underline", !tab.style.underline) }
    }
    // Na largura toda: ao lado de um rótulo, "Normal" não cabia na pílula.
    PillPicker {
        width: parent.width
        options: [
            { value: "none", title: "Normal" }, { value: "upper", title: "AA" },
            { value: "lower", title: "aa" }, { value: "title", title: "Título" }
        ]
        value: tab.style.textCase
        onPicked: (v) => Editor.setSegmentValue("text.style.textCase", v)
    }

    SectionHeader { title: "Espaçamento"; hasAction: true; onAction: Editor.resetSegment("textSpacing") }
    SegSlider { title: "Entre letras"; path: "text.style.letterSpacing"; from: -0.5; to: 2; format: Fmt.signedEm }
    SegSlider { title: "Entre linhas"; path: "text.style.lineSpacing"; from: -0.5; to: 3; format: Fmt.signedEm }

    SectionHeader { title: "Cor" }
    SegColor { title: "Texto"; path: "text.style.color" }
    SegSlider { title: "Opacidade"; path: "text.style.opacity"; from: 0; to: 1; defaultValue: 1; format: Fmt.percent }

    SectionHeader { title: "Contorno" }
    SegSlider { title: "Espessura"; path: "text.style.strokeWidth"; from: 0; to: 20; format: Fmt.oneDecimal }
    SegColor { visible: (tab.style.strokeWidth || 0) > 0; title: "Cor"; path: "text.style.strokeColor" }

    // Destaque da palavra: só em textos com tempo por palavra (legendas automáticas).
    Column {
        objectName: "highlightSection"
        visible: (Fmt.at(tab.seg, "text.words", []) || []).length > 0
        width: parent.width
        spacing: 12
        SectionHeader { title: "Destaque da palavra" }
        SwitchRow {
            width: parent.width
            title: "Destacar a palavra falada"
            checked: tab.highlight !== null
            onToggled: (on) => Editor.setSegmentValue("text.highlight", on ? ({}) : undefined)
        }
        Column {
            visible: tab.highlight !== null
            width: parent.width
            spacing: 12
            OfText {
                visible: tab.seg.highlightActive === false
                width: parent.width
                text: "O texto foi editado e as palavras não batem mais com a fala: o destaque fica desligado até o número de palavras voltar a ser o mesmo."
                size: 11
                color: Theme.warning
                wrapMode: Text.Wrap
            }
            FieldRow {
                label: "Modo"
                PillPicker {
                    width: 200
                    options: [{ value: "current", title: "Só a atual" }, { value: "progressive", title: "Progressivo" }]
                    value: Fmt.at(tab.seg, "text.highlight.mode", "current")
                    onPicked: (v) => Editor.setSegmentValue("text.highlight.mode", v)
                }
            }
            FieldRow {
                label: "Revelação"
                MenuField {
                    options: Editor.revealOptions()
                    value: Fmt.at(tab.seg, "text.highlight.reveal", "none")
                    onPicked: (v) => Editor.setSegmentValue("text.highlight.reveal", v)
                }
            }
            SegSlider {
                visible: Fmt.at(tab.seg, "text.highlight.reveal", "none") === "dim"
                title: "Opacidade das próximas"
                path: "text.highlight.dimOpacity"
                from: 0; to: 1; defaultValue: 0.35
                format: Fmt.percent
            }
            SegColor { title: "Cor"; path: "text.highlight.color" }
            SegSlider { title: "Escala"; path: "text.highlight.scale"; from: 1; to: 1.6; defaultValue: 1.12; format: Fmt.percent }
            SegSwitch { title: "Pílula atrás da palavra"; path: "text.highlight.backgroundEnabled" }
            SegColor {
                visible: Fmt.at(tab.seg, "text.highlight.backgroundEnabled", false) === true
                title: "Cor da pílula"
                path: "text.highlight.backgroundColor"
            }
        }
    }

    // Palavra-chave: só nos textos com ênfase (visuais Editorial e Empilhado).
    Column {
        objectName: "emphasisSection"
        visible: tab.emphasis !== null
        width: parent.width
        spacing: 12
        SectionHeader { title: "Palavra-chave" }
        OfText {
            width: parent.width
            text: "Clique numa palavra para ligar ou desligar a ênfase."
            size: 11
            color: Theme.textTertiary
            wrapMode: Text.Wrap
        }
        Flow {
            objectName: "emphasisChips"
            width: parent.width
            spacing: 6
            Repeater {
                model: tab.seg.wordList || []
                T.AbstractButton {
                    id: chip
                    required property string modelData
                    required property int index
                    readonly property bool on: tab.emphasis !== null && (tab.emphasis.words || []).indexOf(index) >= 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    implicitWidth: chipText.implicitWidth + 20
                    implicitHeight: 26
                    onClicked: Editor.toggleEmphasisWord(index)
                    background: Rectangle {
                        radius: height / 2
                        color: chip.on ? Theme.accent : (chip.hovered ? Theme.raisedHigh : Theme.raised)
                        border.color: chip.on ? Theme.accent : Theme.hairline
                    }
                    contentItem: OfText {
                        id: chipText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: chip.modelData
                        size: 12
                        color: chip.on ? Theme.textOnAccent : Theme.textSecondary
                    }
                }
            }
        }
        ColorRow {
            width: parent.width
            title: "Cor"
            color: Fmt.color(Fmt.at(tab.seg, "text.emphasis.color", null) || tab.style.color, "white")
            onPicked: (c) => Editor.setSegmentValue("text.emphasis.color", c)
        }
        FieldRow {
            label: "Fonte"
            MenuField {
                options: tab.familyOptions(tab.emphasis ? tab.emphasis.fontName || "" : "")
                value: tab.emphasis ? tab.emphasis.fontName : ""
                onPicked: (v) => Editor.setEmphasisFont(v)
            }
        }
        FieldRow {
            label: "Estilo"
            MenuField {
                options: [{ value: "", title: "Padrão" }].concat(
                    Editor.fontFaces(tab.emphasis ? tab.emphasis.fontName || "" : "").map(f => ({ value: f, title: f })))
                value: tab.emphasis ? tab.emphasis.fontFace || "" : ""
                onPicked: (v) => Editor.setSegmentValue("text.emphasis.fontFace", v)
            }
        }
        SegSlider { title: "Tamanho"; path: "text.emphasis.scale"; from: 0.6; to: 2.5; defaultValue: 1.3; format: Fmt.percent }
    }

    SectionHeader { title: "Fundo" }
    SegSwitch { title: "Mostrar fundo"; path: "text.style.backgroundEnabled" }
    Column {
        visible: tab.style.backgroundEnabled === true
        width: parent.width
        spacing: 12
        SegColor { title: "Cor"; path: "text.style.backgroundColor" }
        SegSlider { title: "Opacidade"; path: "text.style.backgroundColor.a"; from: 0; to: 1; defaultValue: 0.6; format: Fmt.percent }
        SegSlider { title: "Arredondamento"; path: "text.style.backgroundRadius"; from: 0; to: 1; defaultValue: 0.18; format: Fmt.em }
        SegSlider { title: "Margem lateral"; path: "text.style.backgroundPaddingX"; from: 0; to: 1; defaultValue: 0.21; format: Fmt.em }
        SegSlider { title: "Margem vertical"; path: "text.style.backgroundPaddingY"; from: 0; to: 1; defaultValue: 0.21; format: Fmt.em }
    }

    SectionHeader { title: "Sombra" }
    SegSwitch { title: "Mostrar sombra"; path: "text.style.shadow" }
    Column {
        visible: tab.style.shadow === true
        width: parent.width
        spacing: 12
        SegColor { title: "Cor"; path: "text.style.shadowColor" }
        SegSlider { title: "Opacidade"; path: "text.style.shadowColor.a"; from: 0; to: 1; defaultValue: 0.65; format: Fmt.percent }
        SegSlider { title: "Distância"; path: "text.style.shadowDistance"; from: 0; to: 1; defaultValue: 0.04; format: Fmt.em }
        SegSlider { title: "Ângulo"; path: "text.style.shadowAngle"; from: -180; to: 180; defaultValue: -90; format: Fmt.degrees }
        SegSlider { title: "Desfoque"; path: "text.style.shadowBlur"; from: 0; to: 1; defaultValue: 0.15; format: Fmt.em }
    }

    SectionHeader { title: "Posição"; hasAction: true; onAction: Editor.resetSegment("textPosition") }
    SegSlider { title: "Posição X"; path: "text.style.x"; format: Fmt.signedPercent }
    SegSlider { title: "Posição Y"; path: "text.style.y"; defaultValue: -0.7; format: Fmt.signedPercent }
    SegSlider { title: "Escala"; path: "text.style.scale"; from: 0.2; to: 3; defaultValue: 1; format: Fmt.percent }
    SegSlider { title: "Rotação"; path: "text.style.rotation"; from: -180; to: 180; format: Fmt.degrees }
    BoxWidthControl {
        width: parent.width
        value: tab.style.maxWidth !== undefined ? tab.style.maxWidth : 0.9
        wholeTrack: tab.seg.isAutoCaption === true
        onMoved: (w) => Editor.setTextWidth(w)
    }
}
