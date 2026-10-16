import QtQuick
import QtQuick.Templates as T

// Janela "Legendas automáticas": fonte, idioma, precisão (modelo do Whisper),
// estilo e o andamento da geração.
T.Popup {
    id: sheet
    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: 760
    height: Math.max(560, Math.min(820, (parent ? parent.height : 820) - 60))
    modal: true
    padding: 0
    closePolicy: gen.running ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape
    // Abre e fecha pela mudança, não por `visible: Editor.showAutoCaptions`: o
    // modalCount de onOpened/onClosed avisa pelo mesmo uiChanged e o vínculo
    // entraria em laço (por isso também o callLater).
    readonly property bool wanted: Editor.showAutoCaptions
    onWantedChanged: Qt.callLater(() => wanted ? open() : close())
    onOpened: Editor.modalCount += 1
    onClosed: {
        Editor.modalCount -= 1
        Editor.closeAutoCaptions()
    }

    readonly property var gen: Editor.captions

    T.Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }
    background: Rectangle {
        color: Theme.surface
        radius: Theme.panelRadius
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }

    contentItem: Item {
        Row {
            id: title
            x: 24
            y: 24
            spacing: 10
            OfText { id: heading; text: "Legendas automáticas"; size: 22; weight: Theme.light }
            OfText { anchors.baseline: heading.baseline; text: Editor.projectName; color: Theme.textTertiary }
        }

        // Opções
        Flickable {
            id: form
            objectName: "captionsForm"
            visible: gen.phase === "configuring"
            x: 24
            y: title.y + title.height + 14
            width: parent.width - 48
            height: parent.height - y - footer.height
            contentHeight: fields.height + 16
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            T.ScrollBar.vertical: OfScrollBar {}

            Column {
                id: fields
                width: form.width
                spacing: 10

                FieldRow {
                    label: "Fonte"
                    Row {
                        spacing: 10
                        PillPicker {
                            width: 280
                            enabled: Editor.clipWithAudio
                            opacity: enabled ? 1 : 0.5
                            options: [{ value: "timeline", title: "Timeline inteira" },
                                      { value: "clip", title: "Clipe selecionado" }]
                            value: gen.source
                            onPicked: (v) => gen.source = v
                        }
                        OfText {
                            visible: !Editor.clipWithAudio
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Selecione um clipe com áudio para usar só ele."
                            size: 11
                            color: Theme.textTertiary
                        }
                    }
                }
                FieldRow {
                    label: "Idioma"
                    MenuField {
                        width: 280
                        options: gen.languages
                        value: gen.language
                        onPicked: (v) => gen.language = v
                    }
                }

                SectionHeader { title: "Precisão" }
                Column {
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: gen.models
                        ModelRow {
                            required property var modelData
                            width: fields.width
                            info: modelData
                            selected: gen.model === modelData.id
                            onSelect: gen.model = modelData.id
                        }
                    }
                }

                SectionHeader { title: "Estilo" }
                Repeater {
                    model: gen.presetGroups
                    Column {
                        id: group
                        required property var modelData
                        required property int index
                        width: fields.width
                        spacing: 8
                        topPadding: index === 0 ? 0 : 6
                        OfText { text: group.modelData.name; size: 11; weight: Theme.medium; color: Theme.textTertiary }
                        // Uma fileira por grupo (o maior tem 4): os títulos ficam alinhados com as fileiras.
                        Row {
                            spacing: 10
                            Repeater {
                                model: sheet.gen.presetsIn(group.modelData.id)
                                StyleCard {
                                    required property var modelData
                                    width: (fields.width - 30) / 4
                                    preset: modelData.id
                                    name: modelData.name
                                    detail: modelData.detail
                                    layout: modelData.layout
                                    usesAccent: modelData.usesAccent
                                    selected: gen.preset === modelData.id
                                    onClicked: gen.preset = modelData.id
                                }
                            }
                        }
                    }
                }
                ColorRow {
                    visible: gen.usesAccent
                    width: parent.width
                    title: "Cor do destaque"
                    color: gen.highlightColor
                    onPicked: (c) => gen.highlightColor = c
                }
                // Sem legendas automáticas no projeto, não há o que substituir.
                SwitchRow {
                    visible: Editor.hasAutoCaptions
                    width: parent.width
                    title: gen.source === "clip" ? "Substituir as legendas automáticas deste clipe"
                                                 : "Substituir as legendas automáticas anteriores"
                    checked: gen.replacePrevious
                    onToggled: (v) => gen.replacePrevious = v
                }
            }
        }
        Item {
            id: footer
            visible: gen.phase === "configuring"
            anchors.bottom: parent.bottom
            width: parent.width
            height: 72
            HDivider { width: parent.width }
            readonly property string warning: Editor.captionSourceProblem !== "" ? Editor.captionSourceProblem
                                                                                  : gen.modelError
            Row {
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 48 - buttons.width - 12
                spacing: 8
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: footer.warning !== "" ? "triangle-alert" : (gen.modelPending ? "download" : "lock")
                    color: footer.warning !== "" ? Theme.warning : Theme.textTertiary
                }
                OfText {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 22
                    text: footer.warning !== "" ? footer.warning : gen.footerNote
                    color: footer.warning !== "" ? Theme.warning : Theme.textTertiary
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                }
            }
            Row {
                id: buttons
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                PillButton { kind: "quiet"; text: "Cancelar"; onClicked: Editor.closeAutoCaptions() }
                PillButton {
                    kind: "primary"
                    text: "Gerar legendas"
                    enabled: Editor.captionSourceProblem === ""
                    onClicked: Editor.generateCaptions()
                }
            }
        }

        // Andamento
        Item {
            visible: gen.running
            x: 24
            y: title.y + title.height + 22
            width: parent.width - 48
            height: parent.height - y - 24

            Row {
                id: steps
                spacing: 10
                readonly property var list: gen.includesDownload ? ["downloading", "mixing", "transcribing"]
                                                                 : ["mixing", "transcribing"]
                readonly property int current: list.indexOf(gen.phase)
                Repeater {
                    model: steps.list
                    Row {
                        required property string modelData
                        required property int index
                        readonly property bool done: index < steps.current
                        readonly property bool active: index === steps.current
                        spacing: 10
                        Rectangle {
                            visible: index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 1
                            color: index <= steps.current ? Theme.alpha(Theme.accent, 0.6) : Theme.hairline
                        }
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: done ? "circle-check" : (active ? "circle-dot" : "circle")
                            color: done || active ? Theme.accent : Theme.textTertiary
                        }
                        OfText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: ({ downloading: "Baixar modelo", mixing: "Misturar áudio",
                                     transcribing: "Transcrever" })[modelData]
                            weight: active ? Theme.medium : Theme.regular
                            color: active ? Theme.textPrimary : (done ? Theme.textSecondary : Theme.textTertiary)
                        }
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                width: parent.width
                spacing: 14
                OfText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Math.round(gen.progress * 100) + "%"
                    size: 44
                    mono: true
                    weight: Theme.thin
                }
                ProgressTrack { width: parent.width; height: 4; fraction: gen.progress }
                Item {
                    width: parent.width
                    height: 16
                    OfText { text: gen.detail; color: Theme.textSecondary }
                    OfText { anchors.right: parent.right; text: gen.remaining; color: Theme.textTertiary }
                }
            }
            Item {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 32
                OfText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Pode continuar editando depois: um Ctrl+Z desfaz a geração inteira."
                    color: Theme.textTertiary
                }
                PillButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Cancelar"
                    onClicked: Editor.cancelCaptionGeneration()
                }
            }
        }

        // Pronto
        Column {
            visible: gen.phase === "finished"
            anchors.centerIn: parent
            spacing: 14
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "circle-check"; size: 46; color: Theme.accent }
            OfText { anchors.horizontalCenter: parent.horizontalCenter; text: "Legendas prontas"; size: 18; weight: Theme.light }
            OfText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: gen.resultCount === 1 ? "1 legenda na timeline" : gen.resultCount + " legendas na timeline"
                color: Theme.textSecondary
            }
            OfText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Edite o texto no painel Legendas. Ctrl+Z desfaz a geração inteira."
                color: Theme.textTertiary
            }
        }
        PillButton {
            visible: gen.phase === "finished"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            kind: "primary"
            text: "Fechar"
            onClicked: Editor.closeAutoCaptions()
        }

        // Falhou
        Column {
            visible: gen.phase === "failed"
            anchors.centerIn: parent
            width: Math.min(480, parent.width - 96)
            spacing: 14
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "octagon-x"; size: 42; color: Theme.danger }
            OfText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Não deu para gerar as legendas"
                size: 18
                weight: Theme.light
            }
            OfText {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: gen.error
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }
        Item {
            visible: gen.phase === "failed"
            anchors.bottom: parent.bottom
            width: parent.width
            height: 78
            PillButton { x: 24; anchors.verticalCenter: parent.verticalCenter; text: "Voltar às opções"; onClicked: gen.backToOptions() }
            PillButton {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                text: "Fechar"
                onClicked: Editor.closeAutoCaptions()
            }
        }
    }

    // Uma linha da lista de precisão: escolher, baixar com progresso, cancelar e apagar.
    component ModelRow: T.AbstractButton {
        id: row
        property var info
        property bool selected: false
        signal select()
        height: 52
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: select()
        background: Rectangle {
            radius: Theme.cardRadius
            color: row.selected ? Theme.raisedHigh : (row.hovered ? Theme.raised : "transparent")
            border.color: row.selected ? Theme.alpha(Theme.accent, 0.5) : "transparent"
        }
        contentItem: Item {
            Icon {
                id: radio
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                name: row.selected ? "circle-dot" : "circle"
                size: 15
                color: row.selected ? Theme.accent : Theme.textTertiary
            }
            Column {
                x: radio.x + radio.width + 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Row {
                    spacing: 8
                    OfText { text: row.info.name; size: 13; weight: row.selected ? Theme.medium : Theme.regular }
                    Rectangle {
                        visible: row.info.recommended
                        anchors.verticalCenter: parent.verticalCenter
                        width: badge.implicitWidth + 14
                        height: 18
                        radius: 9
                        color: Theme.accentSoft
                        OfText { id: badge; anchors.centerIn: parent; text: "Recomendado"; size: 10; weight: Theme.medium; color: Theme.accent }
                    }
                }
                OfText { text: row.info.detail; size: 11; color: Theme.textTertiary }
            }
            OfText {
                anchors.right: status.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.info.sizeText
                size: 11
                mono: true
                color: Theme.textSecondary
            }
            Item {
                id: status
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 190
                height: 30
                // Baixando
                Row {
                    visible: row.info.downloading
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        ProgressTrack { width: 110; height: 3; fraction: row.info.fraction }
                        OfText { anchors.right: parent.right; text: row.info.progressText; size: 10; mono: true; color: Theme.textTertiary }
                    }
                    CircleButton {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 22
                        iconName: "x"
                        help: "Cancelar o download"
                        onClicked: sheet.gen.cancelDownload(row.info.id)
                    }
                }
                // Instalado
                Row {
                    visible: !row.info.downloading && row.info.installed
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 12; color: Theme.textSecondary }
                    OfText { anchors.verticalCenter: parent.verticalCenter; text: "Instalado"; size: 11; color: Theme.textSecondary }
                    GlyphButton {
                        id: more
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "ellipsis"
                        help: "Mais opções"
                        onClicked: modelMenu.popup(more, 0, more.height)
                    }
                    OfMenu {
                        id: modelMenu
                        onOpened: Editor.modalCount += 1
                        onClosed: Editor.modalCount -= 1
                        OfMenuItem {
                            text: "Apagar modelo (" + row.info.sizeText + ")"
                            destructive: true
                            onTriggered: sheet.gen.deleteModel(row.info.id)
                        }
                    }
                }
                PillButton {
                    visible: !row.info.downloading && !row.info.installed
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    text: "Baixar"
                    iconName: "download"
                    onClicked: sheet.gen.downloadModel(row.info.id)
                }
            }
        }
    }

    // Trilho fino de progresso, no ciano da marca.
    component ProgressTrack: Item {
        property real fraction: 0
        Rectangle { anchors.fill: parent; radius: height / 2; color: Qt.rgba(1, 1, 1, 0.1) }
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, parent.fraction))
            height: parent.height
            radius: height / 2
            color: Theme.accent
        }
    }

    // Cartão de uma predefinição: o quadro do vídeo na proporção do projeto com a
    // prévia animada (o mesmo render da exportação), que toca no hover ou selecionado.
    component StyleCard: T.AbstractButton {
        id: card
        property string preset
        property string name
        property string detail
        property string layout
        property bool usesAccent: false
        property bool selected: false
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        implicitHeight: box.height + 6 + label.height
        contentItem: Item {
            Rectangle {
                id: box
                width: parent.width
                height: Math.round(width * 9 / 16)
                radius: 8
                color: Qt.rgba(0, 0, 0, 0.35)
                clip: true
                Item {
                    id: frame
                    readonly property real aspect: Editor.captionPreviewAspect()
                    readonly property bool wide: aspect >= 16 / 9
                    anchors.centerIn: parent
                    width: wide ? box.width : box.height * aspect
                    height: wide ? box.width / aspect : box.height
                    clip: true
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(0.2, 0.2, 0.2, 1) }
                            GradientStop { position: 1; color: Qt.rgba(0.08, 0.08, 0.08, 1) }
                        }
                    }
                    Image {
                        anchors.fill: parent
                        source: Editor.captionBackdrop
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                    }
                    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25) }
                    CaptionPreviewStrip {
                        id: strip
                        anchors.fill: parent
                        preset: card.preset
                        accent: sheet.gen.highlightColor
                        usesAccent: card.usesAccent
                        playing: card.hovered || card.selected
                    }
                    CaptionSample {
                        visible: !strip.hasFrames
                        anchors.fill: parent
                        layout: card.layout
                        accent: card.usesAccent ? sheet.gen.highlightColor : "white"
                    }
                }
            }
            Rectangle {
                anchors.fill: box
                radius: 8
                color: "transparent"
                border.width: card.selected ? 1.5 : 1
                border.color: card.selected ? Theme.accent : (card.hovered ? Qt.rgba(1, 1, 1, 0.22) : Theme.hairline)
            }
            Column {
                id: label
                x: 2
                y: box.height + 6
                width: parent.width - 4
                spacing: 1
                OfText {
                    width: parent.width
                    text: card.name
                    size: 12
                    weight: Theme.medium
                    color: card.selected ? Theme.accent : Theme.textPrimary
                    elide: Text.ElideRight
                }
                OfText {
                    width: parent.width
                    height: 28
                    text: card.detail
                    size: 10.5
                    color: Theme.textTertiary
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    verticalAlignment: Text.AlignTop
                }
            }
        }
        Tip { target: card; text: card.detail }
    }

    // Amostra de cada visual enquanto os quadros da prévia são gerados.
    component CaptionSample: Item {
        id: sample
        property string layout
        property color accent: "white"
        readonly property real h: height
        Text {
            visible: sample.layout === "highlight"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: sample.h * 0.12
            textFormat: Text.StyledText
            text: "um vídeo <font color='" + sample.accent + "'>muito</font> bom"
            color: "white"
            font.family: Theme.family
            font.pixelSize: Math.max(6, sample.h * 0.11)
            font.weight: Font.Bold
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }
        Text {
            visible: sample.layout === "punch" || sample.layout === "stacked"
            anchors.centerIn: parent
            text: "MUITO BOM"
            color: "white"
            font.family: Theme.family
            font.pixelSize: Math.max(6, sample.h * 0.2)
            font.weight: Font.Black
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }
        Text {
            visible: sample.layout === "phrases"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: sample.h * 0.1
            horizontalAlignment: Text.AlignHCenter
            text: "Hoje eu vou mostrar\num vídeo muito bom"
            color: "white"
            font.family: Theme.family
            font.pixelSize: Math.max(6, sample.h * 0.085)
            font.weight: Font.DemiBold
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }
    }
}
