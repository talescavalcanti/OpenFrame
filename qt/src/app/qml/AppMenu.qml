import QtQuick

// Menu do app (a barra de menus do Mac): Arquivo, Editar, Clipe e Reprodução
// num só menu, com os atalhos ao lado.
OfMenu {
    id: menu
    width: 280
    onOpened: Editor.modalCount += 1
    onClosed: Editor.modalCount -= 1

    OfMenuItem { text: "Novo projeto"; shortcutText: "Ctrl+N"; onTriggered: Editor.newProject() }
    OfMenuItem { text: "Abrir projeto…"; shortcutText: "Ctrl+O"; onTriggered: Editor.requestDialog("openProject") }
    OfMenuItem { text: "Projetos"; shortcutText: "Ctrl+Shift+P"; onTriggered: Editor.showHome() }
    OfMenuSeparator {}
    OfMenuItem { text: "Importar mídia…"; shortcutText: "Ctrl+I"; onTriggered: Editor.requestDialog("importMedia") }
    OfMenuItem { text: "Importar legendas (SRT)…"; onTriggered: Editor.requestDialog("importSrt") }
    OfMenuSeparator {}
    OfMenuItem { text: "Salvar"; shortcutText: "Ctrl+S"; onTriggered: Editor.saveProject() }
    OfMenuItem { text: "Exportar projeto…"; shortcutText: "Ctrl+Shift+S"; onTriggered: Editor.requestDialog("exportProject") }
    OfMenuItem { text: "Exportar vídeo…"; shortcutText: "Ctrl+E"; enabled: Editor.duration > 0; onTriggered: Editor.showExport = true }
    OfMenuItem { text: "Exportar legendas (SRT)…"; enabled: Editor.cues.count > 0; onTriggered: Editor.requestDialog("exportSrt") }
    OfMenuSeparator {}
    OfMenuItem { text: "Dividir no cursor"; shortcutText: "Ctrl+B"; onTriggered: Editor.splitAtPlayhead() }
    OfMenuItem { text: "Duplicar"; shortcutText: "Ctrl+D"; enabled: Editor.selection !== ""; onTriggered: Editor.duplicateSelection() }
    OfMenuItem { text: "Apagar"; shortcutText: "Delete"; enabled: Editor.selection !== ""; onTriggered: Editor.deleteSelection() }
    OfMenuItem { text: "Extrair áudio"; enabled: Editor.selectedSegment.canExtractAudio === true; onTriggered: Editor.extractAudio() }
    OfMenuItem { text: "Adicionar texto"; shortcutText: "Ctrl+T"; onTriggered: Editor.addText() }
    OfMenuItem { text: "Legendas automáticas…"; enabled: Editor.duration > 0; onTriggered: Editor.presentAutoCaptions() }
    OfMenuSeparator {}
    OfMenuItem { text: Editor.isPlaying ? "Pausar" : "Reproduzir"; shortcutText: "Espaço"; onTriggered: Editor.togglePlay() }
    OfMenuItem { text: "Aproximar timeline"; shortcutText: "Ctrl+="; onTriggered: Editor.zoomIn() }
    OfMenuItem { text: "Afastar timeline"; shortcutText: "Ctrl+−"; onTriggered: Editor.zoomOut() }
    OfMenuItem { text: "Ajustar timeline à janela"; shortcutText: "Shift+Z"; onTriggered: Editor.zoomToFit() }
}
