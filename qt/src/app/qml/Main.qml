import QtQuick
import QtQuick.Dialogs
import QtQuick.Window

// Janela única: tela inicial (projetos) ou editor, mais os diálogos de arquivo,
// a janela de exportação e os atalhos com Ctrl (o Cmd do Mac).
Window {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1180
    minimumHeight: 740
    visible: true
    color: Theme.background
    title: Editor.screen === "editor" ? Editor.projectName + " — OpenFrame" : "OpenFrame"

    readonly property bool inEditor: Editor.screen === "editor"
    readonly property bool idle: Editor.modalCount === 0 && !Editor.showExport && !Editor.showAutoCaptions

    Loader {
        anchors.fill: parent
        sourceComponent: window.inEditor ? editorComponent : homeComponent
    }
    Component { id: homeComponent; HomeView {} }
    Component { id: editorComponent; EditorView {} }

    ExportSheet {}
    AutoCaptionsSheet {}

    OfDialog {
        id: errorDialog
        title: "OpenFrame"
        message: Editor.errorMessage
        showCancel: false
        onClosed: Editor.errorMessage = ""
    }
    Connections {
        target: Editor
        function onUiChanged() {
            if (Editor.errorMessage !== "" && !errorDialog.opened)
                errorDialog.open()
        }
        function onDialogRequested(kind) {
            switch (kind) {
            case "openProject": openProjectDialog.open(); break
            case "importMedia": importMediaDialog.open(); break
            case "importSrt": importSrtDialog.open(); break
            case "exportSrt":
                exportSrtDialog.selectedFile = exportSrtDialog.currentFolder + "/" + Editor.suggestedFileName("srt")
                exportSrtDialog.open()
                break
            case "exportProject":
                exportProjectDialog.selectedFile = exportProjectDialog.currentFolder + "/" + Editor.suggestedFileName("openframe")
                exportProjectDialog.open()
                break
            case "exportVideo":
                exportVideoDialog.selectedFile = exportVideoDialog.currentFolder + "/" + Editor.suggestedFileName(Editor.exportSettings.extension)
                exportVideoDialog.open()
                break
            }
        }
    }

    // Diálogos de arquivo (nativos no Windows; no Linux, o do portal ou o do Qt).
    FileDialog {
        id: openProjectDialog
        title: "Abrir projeto"
        nameFilters: ["Projetos OpenFrame (*.openframe)"]
        onAccepted: Editor.openProjectFile(selectedFile)
    }
    FileDialog {
        id: importMediaDialog
        title: "Escolha vídeos, fotos ou áudios"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Vídeos, fotos e áudios (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.mts *.m2ts *.3gp *.jpg *.jpeg *.png *.heic *.heif *.webp *.bmp *.tif *.tiff *.gif *.mp3 *.m4a *.aac *.wav *.aif *.aiff *.flac *.ogg *.opus *.caf"
                      + " *.MP4 *.MOV *.M4V *.MKV *.AVI *.WEBM *.MTS *.JPG *.JPEG *.PNG *.HEIC *.MP3 *.M4A *.WAV)", "Todos os arquivos (*)"]
        onAccepted: Editor.importMedia(selectedFiles)
    }
    FileDialog {
        id: importSrtDialog
        title: "Importar legendas"
        nameFilters: ["Legendas SubRip (*.srt *.SRT)"]
        onAccepted: Editor.importSubtitles(selectedFile)
    }
    FileDialog {
        id: exportSrtDialog
        title: "Exportar legendas"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: ["Legendas SubRip (*.srt)"]
        onAccepted: Editor.exportSubtitles(selectedFile)
    }
    FileDialog {
        id: exportProjectDialog
        title: "Exportar projeto"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "openframe"
        nameFilters: ["Projetos OpenFrame (*.openframe)"]
        onAccepted: Editor.exportProjectFile(selectedFile)
    }
    FileDialog {
        id: exportVideoDialog
        title: "Exportar vídeo"
        fileMode: FileDialog.SaveFile
        defaultSuffix: Editor.exportSettings.extension
        nameFilters: [Editor.exportSettings.extension.toUpperCase() + " (*." + Editor.exportSettings.extension + ")"]
        onAccepted: Editor.startExport(selectedFile)
    }

    // Atalhos: os mesmos do Mac, com Ctrl no lugar de Cmd.
    Shortcut { sequence: "Ctrl+N"; enabled: window.idle; onActivated: window.inEditor ? Editor.newProject() : Editor.createProject() }
    Shortcut { sequence: "Ctrl+O"; enabled: window.idle; onActivated: Editor.requestDialog("openProject") }
    Shortcut { sequence: "Ctrl+Shift+P"; enabled: window.idle && window.inEditor; onActivated: Editor.showHome() }
    Shortcut { sequence: "Ctrl+I"; enabled: window.idle && window.inEditor; onActivated: Editor.requestDialog("importMedia") }
    Shortcut { sequence: "Ctrl+S"; enabled: window.idle && window.inEditor; onActivated: Editor.saveProject() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: window.idle && window.inEditor; onActivated: Editor.requestDialog("exportProject") }
    Shortcut { sequence: "Ctrl+E"; enabled: window.idle && window.inEditor && Editor.duration > 0; onActivated: Editor.showExport = true }
    Shortcut { sequences: [StandardKey.Undo]; enabled: window.idle && window.inEditor; onActivated: Editor.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: window.idle && window.inEditor; onActivated: Editor.redo() }
    Shortcut { sequence: "Ctrl+B"; enabled: window.idle && window.inEditor; onActivated: Editor.splitAtPlayhead() }
    Shortcut { sequence: "Ctrl+D"; enabled: window.idle && window.inEditor; onActivated: Editor.duplicateSelection() }
    Shortcut { sequence: "Ctrl+T"; enabled: window.idle && window.inEditor; onActivated: Editor.addText() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; enabled: window.idle && window.inEditor; onActivated: Editor.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; enabled: window.idle && window.inEditor; onActivated: Editor.zoomOut() }
}
