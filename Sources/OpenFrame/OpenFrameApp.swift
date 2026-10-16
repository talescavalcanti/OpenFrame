import AppKit
import OpenFrameCore
import SwiftUI

@main
struct OpenFrameApp: App {
    @State private var model = EditorModel()

    private var inEditor: Bool { model.screen == .editor }

    init() {
        // Necessário quando o binário roda fora de um .app (swift run).
        NSApplication.shared.setActivationPolicy(.regular)
    }

    var body: some Scene {
        WindowGroup("OpenFrame") {
            ContentView()
                .environment(model)
                .frame(minWidth: 1180, minHeight: 740)
                .onAppear { NSApp.activate(ignoringOtherApps: true) }
                .onOpenURL { url in
                    if url.pathExtension == ProjectStore.fileExtension { model.open(url: url) }
                }
                // Abrir um .openframe reaproveita a janela existente.
                .handlesExternalEvents(preferring: ["*"], allowing: ["*"])
        }
        .defaultSize(width: 1440, height: 900)
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Novo projeto") { model.newProject() }
                    .keyboardShortcut("n")
                Button("Abrir projeto…") { model.openProject() }
                    .keyboardShortcut("o")
                Button("Projetos") { model.showHome() }
                    .keyboardShortcut("p", modifiers: [.command, .shift])
                    .disabled(!inEditor)
                Divider()
                Group {
                    Button("Importar mídia…") { model.presentImportPanel() }
                        .keyboardShortcut("i")
                    Button("Importar legendas (SRT)…") { model.importSubtitles() }
                }
                .disabled(!inEditor)
            }
            CommandGroup(replacing: .saveItem) {
                Group {
                    Button("Salvar") { model.saveProject() }
                        .keyboardShortcut("s")
                    Button("Exportar projeto…") { model.exportProjectFile() }
                        .keyboardShortcut("s", modifiers: [.command, .shift])
                    Divider()
                    Button("Exportar vídeo…") { model.showExport = true }
                        .keyboardShortcut("e")
                    Button("Exportar legendas (SRT)…") { model.exportSubtitles() }
                }
                .disabled(!inEditor)
            }
            CommandGroup(replacing: .undoRedo) {
                Button("Desfazer") { model.undo() }
                    .keyboardShortcut("z")
                    .disabled(!inEditor || !model.canUndo)
                Button("Refazer") { model.redo() }
                    .keyboardShortcut("z", modifiers: [.command, .shift])
                    .disabled(!inEditor || !model.canRedo)
            }
            CommandMenu("Clipe") {
                Group {
                    Button("Dividir no cursor") { model.splitAtPlayhead() }
                        .keyboardShortcut("b")
                    Button("Duplicar") { model.duplicateSelection() }
                        .keyboardShortcut("d")
                        .disabled(model.selection == nil)
                    Button("Apagar  ⌫") { model.deleteSelection() }
                        .disabled(model.selection == nil)
                    Button("Extrair áudio") { model.extractAudio() }
                        .disabled(!model.canExtractAudio)
                    Divider()
                    Button("Adicionar texto") { model.addText() }
                        .keyboardShortcut("t")
                    Button("Legendas automáticas…") { model.presentAutoCaptions() }
                }
                .disabled(!inEditor)
            }
            CommandMenu("Reprodução") {
                Group {
                    Button(model.isPlaying ? "Pausar  ␣" : "Reproduzir  ␣") { model.togglePlay() }
                    Button("Quadro anterior  ←") { model.step(frames: -1) }
                    Button("Próximo quadro  →") { model.step(frames: 1) }
                    Button("Corte anterior  ↑") { model.jumpToEdit(forward: false) }
                    Button("Próximo corte  ↓") { model.jumpToEdit(forward: true) }
                    Button("Ir para o início  ↖") { model.goToStart() }
                    Button("Ir para o fim  ↘") { model.goToEnd() }
                    Divider()
                    Button("Aproximar timeline") { model.zoomIn() }
                        .keyboardShortcut("=")
                    Button("Afastar timeline") { model.zoomOut() }
                        .keyboardShortcut("-")
                    Button("Ajustar timeline à janela  ⇧Z") { model.zoomToFit() }
                }
                .disabled(!inEditor)
            }
        }
    }
}
