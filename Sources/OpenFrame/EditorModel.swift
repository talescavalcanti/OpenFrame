import AppKit
import AVFoundation
import Observation
import OpenFrameCore
import SwiftUI
import UniformTypeIdentifiers

enum Screen {
    case home, editor
}

enum InspectorTab: String, CaseIterable, Identifiable {
    case basic, adjust, filters, effects, animation, transition, text, audio

    var id: String { rawValue }

    var title: String {
        switch self {
        case .basic: "Básico"
        case .adjust: "Ajustar"
        case .filters: "Filtros"
        case .effects: "Efeitos"
        case .animation: "Animação"
        case .transition: "Transição"
        case .text: "Texto"
        case .audio: "Áudio"
        }
    }

    var icon: String {
        switch self {
        case .basic: "arrow.up.and.down.and.arrow.left.and.right"
        case .adjust: "slider.horizontal.3"
        case .filters: "camera.filters"
        case .effects: "sparkles"
        case .animation: "wand.and.stars"
        case .transition: "arrow.left.arrow.right"
        case .text: "textformat"
        case .audio: "waveform"
        }
    }
}

/// Painel da biblioteca, escolhido na barra de ferramentas da esquerda.
enum LibraryPanel: String, CaseIterable, Identifiable {
    case media, text, captions, shortcuts

    var id: String { rawValue }

    var title: String {
        switch self {
        case .media: "Mídia"
        case .text: "Texto"
        case .captions: "Legendas"
        case .shortcuts: "Atalhos"
        }
    }

    var icon: String {
        switch self {
        case .media: "photo.on.rectangle"
        case .text: "textformat"
        case .captions: "captions.bubble"
        case .shortcuts: "keyboard"
        }
    }
}

/// Estado do editor. Toda alteração no projeto passa por `mutate` (com
/// desfazer) ou pelos métodos de gesto, que atualizam o preview.
@MainActor
@Observable
final class EditorModel {
    private(set) var project = Project()
    var selection: UUID? {
        didSet { if selection != oldValue { ensureValidTab() } }
    }
    var inspectorTab = InspectorTab.basic
    /// Pedido para pôr o foco no campo do texto (duplo clique num texto no preview).
    var focusTextEditor = false
    /// Manipulação no preview: guias de encaixe, graus durante o giro e largura pedida pelas alças laterais.
    var canvasGuides: [SnapGuide] = []
    var canvasRotationLabel: Double?
    var canvasWidthDrag: WidthDragState?
    @ObservationIgnored var placementDrag: PlacementDrag?
    /// Quadros das miniaturas animadas do inspetor.
    @ObservationIgnored let animationStrips = FrameStripStore()
    var libraryPanel = LibraryPanel.media
    var libraryVisible = true
    /// Grade dos terços sobre o preview.
    var showGrid = false
    private(set) var playhead: Micros = 0
    private(set) var isPlaying = false
    /// Pontos por segundo na timeline.
    var zoom: Double = 80
    /// Largura visível e deslocamento horizontal da área rolável da timeline.
    var timelineViewportWidth: CGFloat = 0
    private(set) var timelineScrollX: CGFloat = 0
    /// Destino do clipe sendo arrastado na vertical (outra faixa).
    private(set) var dragTarget: DragTarget?
    /// Onde o clipe arrastado (ou aparado pelo início) é desenhado durante o gesto.
    private(set) var dragPreview: DragPreview?
    /// Material da biblioteca sendo arrastado, para prever onde ele cai.
    var draggingMaterial: UUID?
    var exportSettings = ExportSettings()
    var showExport = false
    var showAutoCaptions = false
    /// Legenda com o texto em edição no painel Legendas.
    var editingCaption: UUID?
    @ObservationIgnored let captionGeneration = CaptionGeneration()
    @ObservationIgnored let recording = AudioRecording()
    var errorMessage: String?
    /// Tela inicial (projetos) ou editor.
    private(set) var screen = Screen.home
    /// Projetos da biblioteca, para a tela inicial.
    private(set) var entries: [ProjectEntry] = []
    /// Projeto da biblioteca aberto no editor (salvo automaticamente).
    private(set) var libraryID: UUID?
    /// Arquivo `.openframe` aberto de fora da biblioteca (`-open`), salvo à mão.
    private(set) var documentURL: URL?
    private(set) var isDirty = false
    private(set) var thumbnails: [UUID: [CGImage]] = [:]
    /// Picos de áudio por material (`Waveform.peaksPerSecond` por segundo).
    private(set) var waveforms: [UUID: [Float]] = [:]

    @ObservationIgnored let player = AVPlayer()
    @ObservationIgnored let library: ProjectLibrary
    @ObservationIgnored private var autosaveTask: Task<Void, Never>?
    @ObservationIgnored private var thumbnailTask: Task<Void, Never>?
    @ObservationIgnored private var lastThumbnail = Date.distantPast
    @ObservationIgnored private var missingThumbnailsTask: Task<Void, Never>?
    /// Projetos cuja miniatura já foi tentada nesta sessão (mídia ausente não repete).
    @ObservationIgnored private var thumbnailAttempts = Set<UUID>()
    @ObservationIgnored private var undoStack: [Project] = []
    @ObservationIgnored private var redoStack: [Project] = []
    @ObservationIgnored private var lastCoalesceKey: String?
    @ObservationIgnored private var lastCoalesceTime = Date.distantPast
    @ObservationIgnored private var gestureBase: Project?
    @ObservationIgnored private var structureKey: CompositionBuilder.StructureKey?
    @ObservationIgnored private var planBox: PlanBox?
    @ObservationIgnored private var previewRenderSize: CGSize = .zero
    /// Lado menor do preview em pixels: segue o player na tela (ver `PreviewResolution`).
    @ObservationIgnored private(set) var previewShortSide = 720
    /// Último tamanho do player informado pela view: pontos e escala da tela.
    @ObservationIgnored private var previewViewport: (points: CGSize, scale: CGFloat)?
    @ObservationIgnored private var previewResolutionTask: Task<Void, Never>?
    @ObservationIgnored private var rebuildTask: Task<Void, Never>?
    @ObservationIgnored private var isSeeking = false
    @ObservationIgnored private var pendingSeek: CMTime?
    /// Trecho tocado uma vez ao escolher um efeito: onde parar e para onde voltar.
    @ObservationIgnored private var excerpt: (end: Micros, resume: Micros)?
    @ObservationIgnored private var timeObserver: Any?
    @ObservationIgnored private var keyMonitor: Any?
    @ObservationIgnored private(set) weak var timelineScrollView: NSScrollView?
    @ObservationIgnored private var timelineScrollObserver: Any?
    @ObservationIgnored private var pendingTimelineScrollX: CGFloat?
    @ObservationIgnored private var scrollMonitor: Any?
    /// Última versão de cada segmento mostrado no inspetor (ver `binding`).
    @ObservationIgnored private var inspectedSegments: [UUID: Segment] = [:]

    var canUndo: Bool { !undoStack.isEmpty }
    var canRedo: Bool { !redoStack.isEmpty }
    var duration: Micros { project.duration }
    var selectedSegment: Segment? { selection.flatMap { project.segment($0) } }

    init() {
        // `OPENFRAME_LIBRARY_DIR` troca a pasta da biblioteca (capturas e testes manuais).
        let root = ProcessInfo.processInfo.environment["OPENFRAME_LIBRARY_DIR"].map { URL(fileURLWithPath: $0, isDirectory: true) }
        library = ProjectLibrary(root: root ?? ProjectLibrary.defaultRoot)
        player.actionAtItemEnd = .pause
        timeObserver = player.addPeriodicTimeObserver(forInterval: CMTime(value: 1, timescale: 30), queue: .main) { [weak self] time in
            MainActor.assumeIsolated { self?.playerTimeChanged(time) }
        }
        installKeyMonitor()
        installScrollMonitor()
        // `OpenFrame -open caminho.openframe` (argumentos com "-" viram UserDefaults;
        // caminhos soltos seriam tratados pelo AppKit como documentos a abrir).
        if let path = UserDefaults.standard.string(forKey: "open") {
            openExternal(url: URL(fileURLWithPath: path))
        } else {
            refreshLibrary()
        }
        NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil,
                                               queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.saveNow() }
        }
        DebugSnapshot.runIfRequested(self)
    }

    // MARK: - Alterações e desfazer

    /// Aplica uma alteração com desfazer. Alterações com a mesma `coalesce`
    /// em sequência (ex.: arrastar um slider) viram um único passo.
    func mutate(coalesce: String? = nil, _ change: (inout Project) -> Void) {
        var copy = project
        change(&copy)
        guard copy != project else { return }
        let now = Date()
        let merges = coalesce != nil && coalesce == lastCoalesceKey && now.timeIntervalSince(lastCoalesceTime) < 1
        if !merges { pushUndo(project) }
        lastCoalesceKey = coalesce
        lastCoalesceTime = now
        setProject(copy)
    }

    private func pushUndo(_ snapshot: Project) {
        undoStack.append(snapshot)
        if undoStack.count > 300 { undoStack.removeFirst() }
        redoStack.removeAll()
    }

    func undo() {
        guard let previous = undoStack.popLast() else { return }
        redoStack.append(project)
        lastCoalesceKey = nil
        setProject(previous)
    }

    func redo() {
        guard let next = redoStack.popLast() else { return }
        undoStack.append(project)
        lastCoalesceKey = nil
        setProject(next)
    }

    private func setProject(_ p: Project) {
        project = p
        isDirty = true
        scheduleAutosave()
        if let id = selection, p.segment(id) == nil { selection = nil }
        if playhead > p.duration { playhead = p.duration }
        projectDidChange()
    }

    // Gestos (arrastar clipes): cada atualização parte do estado do início
    // do gesto; o desfazer é registrado uma vez, ao soltar.

    func beginGesture() {
        if gestureBase == nil { gestureBase = project }
    }

    func updateGesture(_ change: (inout Project) -> Void) {
        guard var copy = gestureBase else { return }
        change(&copy)
        guard copy != project else { return }
        project = copy
        isDirty = true
        scheduleAutosave()
        projectDidChange()
    }

    func endGesture() {
        if let base = gestureBase, base != project {
            pushUndo(base)
            lastCoalesceKey = nil
        }
        gestureBase = nil
    }

    var gestureSnapshot: Project? { gestureBase }

    // MARK: - Bindings para o inspetor

    /// Ao apagar o clipe selecionado (ou desfazer a criação dele), o SwiftUI
    /// ainda reavalia a aba do inspetor uma vez antes de removê-la, quando o
    /// segmento já não está no projeto. Os bindings usam então a última versão
    /// conhecida dele.
    func binding<T: Equatable>(_ id: UUID, _ keyPath: WritableKeyPath<Segment, T>) -> Binding<T> {
        if let seg = project.segment(id) { inspectedSegments[id] = seg }
        let fallback = inspectedSegments[id]
        return Binding(
            get: { [weak self] in
                // O segmento sempre existe quando o inspetor o mostra pela primeira vez.
                (self?.project.segment(id) ?? fallback)![keyPath: keyPath]
            },
            set: { [weak self] value in
                self?.mutate(coalesce: "\(id)\(keyPath)") { p in
                    TimelineEditor.update(&p, segment: id) { $0[keyPath: keyPath] = value }
                }
            })
    }

    func projectBinding<T: Equatable>(_ keyPath: WritableKeyPath<Project, T>) -> Binding<T> {
        Binding(
            get: { [weak self] in self?.project[keyPath: keyPath] ?? Project()[keyPath: keyPath] },
            set: { [weak self] value in
                self?.mutate(coalesce: "\(keyPath)") { $0[keyPath: keyPath] = value }
            })
    }

    func trackBinding(_ trackId: UUID, _ keyPath: WritableKeyPath<Track, Bool>) -> Binding<Bool> {
        Binding(
            get: { [weak self] in self?.project.tracks.first { $0.id == trackId }?[keyPath: keyPath] ?? false },
            set: { [weak self] value in
                self?.mutate { p in
                    if let i = p.tracks.firstIndex(where: { $0.id == trackId }) { p.tracks[i][keyPath: keyPath] = value }
                }
            })
    }

    private func ensureValidTab() {
        guard let seg = selectedSegment else { return }
        let tabs = availableTabs(for: seg)
        if !tabs.contains(inspectorTab) { inspectorTab = tabs.first ?? .basic }
    }

    func availableTabs(for seg: Segment) -> [InspectorTab] {
        if seg.text != nil { return [.text, .animation] }
        guard let m = project.material(seg.materialId) else { return [] }
        if m.kind == .audio { return [.audio] }
        var tabs: [InspectorTab] = [.basic, .adjust, .filters, .effects, .animation]
        if isOnMainTrack(seg.id), !isLastOnMainTrack(seg.id) { tabs.append(.transition) }
        return tabs
    }

    /// Seleciona o texto e abre a aba Texto com o foco no campo.
    func editText(_ id: UUID) {
        guard project.segment(id)?.text != nil else { return }
        selection = id
        inspectorTab = .text
        focusTextEditor = true
    }

    func isOnMainTrack(_ id: UUID) -> Bool {
        guard let loc = project.locate(id) else { return false }
        return project.tracks[loc.track].isMain
    }

    func isLastOnMainTrack(_ id: UUID) -> Bool {
        guard let main = project.mainTrackIndex else { return true }
        return project.tracks[main].segments.last?.id == id
    }

    // MARK: - Preview

    private var previewSize: CGSize { project.canvasSize(shortSide: previewShortSide) }

    /// A view do player mudou de tamanho ou de tela. O primeiro tamanho vale na hora;
    /// os seguintes, ~300 ms depois que o tamanho parar (redimensionar a janela ou a timeline).
    func previewViewportChanged(points: CGSize, scale: CGFloat) {
        guard points.width > 1, points.height > 1 else { return }
        let first = previewViewport == nil
        previewViewport = (points, scale)
        previewResolutionTask?.cancel()
        if first {
            updatePreviewResolution()
            return
        }
        previewResolutionTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(300))
            guard !Task.isCancelled else { return }
            self?.updatePreviewResolution()
        }
    }

    /// Recalcula o degrau; só refaz o preview se ele mudou.
    private func updatePreviewResolution() {
        guard let viewport = previewViewport else { return }
        setPreviewShortSide(PreviewResolution.shortSide(points: viewport.points, scale: viewport.scale))
    }

    /// Troca o lado menor do preview (o DebugSnapshot também usa, para medir cada degrau).
    func setPreviewShortSide(_ side: Int) {
        guard side != previewShortSide else { return }
        previewShortSide = side
        projectDidChange()
    }

    private func projectDidChange() {
        let key = CompositionBuilder.structureKey(project)
        if key == structureKey, let box = planBox, previewRenderSize == previewSize {
            box.update(project: project)
            refreshFrame()
        } else {
            rebuildPreview()
        }
    }

    private func rebuildPreview() {
        rebuildTask?.cancel()
        let snapshot = project
        let size = previewSize
        let key = CompositionBuilder.structureKey(snapshot)
        rebuildTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(40))
            guard !Task.isCancelled else { return }
            do {
                let built = try await CompositionBuilder.build(project: snapshot, renderSize: size,
                                                               frameDuration: CMTime(value: 1, timescale: 30))
                guard !Task.isCancelled, let self else { return }
                built.box.update(project: self.project)
                let item = AVPlayerItem(asset: built.composition)
                item.videoComposition = built.videoComposition
                item.audioMix = built.audioMix
                item.audioTimePitchAlgorithm = .spectral
                let resume = self.isPlaying
                self.player.replaceCurrentItem(with: item)
                self.planBox = built.box
                self.structureKey = key
                self.previewRenderSize = size
                self.seekPlayer(to: self.playhead.cmTime)
                if resume { self.player.play() }
            } catch {
                self?.errorMessage = "Falha ao montar o preview: \(error.localizedDescription)"
            }
        }
    }

    /// Força o redesenho do quadro atual após mudar só efeitos.
    private func refreshFrame() {
        guard !isPlaying, let item = player.currentItem,
              let composition = item.videoComposition?.mutableCopy() as? AVVideoComposition
        else { return }
        item.videoComposition = composition
        seekPlayer(to: playhead.cmTime)
    }

    private func playerTimeChanged(_ time: CMTime) {
        guard isPlaying else { return }
        playhead = min(time.micros, duration)
        if let excerpt, playhead >= excerpt.end || player.rate == 0 {
            player.pause()
            isPlaying = false
            self.excerpt = nil
            seek(to: excerpt.resume)
            return
        }
        revealPlayhead()
        if player.rate == 0 || playhead >= duration {
            isPlaying = false
        }
    }

    /// Toca o trecho uma vez e volta ao ponto em que o playhead estava
    /// (prévia de um efeito recém-escolhido).
    func playExcerpt(_ range: Range<Micros>) {
        let resume = excerpt?.resume ?? playhead
        if isPlaying { player.pause() }
        let start = min(max(0, range.lowerBound), duration)
        let end = min(range.upperBound, duration)
        guard end > start else { return }
        excerpt = (end, resume)
        seek(to: start)
        isPlaying = true
        player.play()
    }

    func togglePlay() {
        excerpt = nil
        if isPlaying {
            player.pause()
            isPlaying = false
            playhead = min(player.currentTime().micros, duration)
        } else {
            guard duration > 0 else { return }
            if playhead >= duration - 30_000 { seek(to: 0) }
            isPlaying = true
            player.play()
        }
    }

    func seek(to t: Micros) {
        playhead = min(max(0, t), duration)
        seekPlayer(to: playhead.cmTime)
    }

    func step(frames: Int) {
        if isPlaying { togglePlay() }
        seek(to: playhead + Micros(Double(frames) / project.fps * 1_000_000))
        revealPlayhead()
    }

    /// Scrub pela régua ou pela agulha do playhead.
    func scrub(to t: Micros) {
        excerpt = nil
        if isPlaying { togglePlay() }
        seek(to: t)
    }

    func goToStart() {
        if isPlaying { togglePlay() }
        seek(to: 0)
        revealPlayhead()
    }

    func goToEnd() {
        if isPlaying { togglePlay() }
        seek(to: duration)
        revealPlayhead()
    }

    /// Vai ao corte anterior ou seguinte: início ou fim de qualquer clipe.
    func jumpToEdit(forward: Bool) {
        if isPlaying { togglePlay() }
        var edges: Set<Micros> = [0, duration]
        for seg in project.tracks.flatMap(\.segments) {
            edges.insert(seg.targetStart)
            edges.insert(seg.targetEnd)
        }
        let target = forward ? edges.filter { $0 > playhead }.min() : edges.filter { $0 < playhead }.max()
        guard let target else { return }
        seek(to: target)
        revealPlayhead()
    }

    /// Busca "perseguida": enquanto uma busca está em andamento, guarda só a
    /// mais recente, para o scrub não acumular atraso.
    private func seekPlayer(to time: CMTime) {
        pendingSeek = time
        guard !isSeeking else { return }
        performPendingSeek()
    }

    private func performPendingSeek() {
        guard let target = pendingSeek else { return }
        pendingSeek = nil
        isSeeking = true
        player.seek(to: target, toleranceBefore: .zero, toleranceAfter: .zero) { [weak self] _ in
            Task { @MainActor in
                self?.isSeeking = false
                self?.performPendingSeek()
            }
        }
    }

    // MARK: - Mídia

    func presentImportPanel() {
        let panel = NSOpenPanel()
        // Sem `allowedContentTypes`: com o filtro de tipos, o painel do sistema
        // escondia os .mov. Quem decide o que fica habilitado é o delegate, com a
        // mesma regra do `MediaProbe`.
        let filter = MediaPanelFilter()
        panel.delegate = filter
        panel.allowsMultipleSelection = true
        panel.message = "Escolha vídeos, fotos ou áudios"
        let response = panel.runModal()
        withExtendedLifetime(filter) {}
        if response == .OK { importMedia(panel.urls) }
    }

    func importMedia(_ urls: [URL]) {
        Task {
            var failures: [String] = []
            for url in urls {
                do {
                    let m = try await MediaProbe.probe(url)
                    if !project.materials.contains(where: { $0.path == m.path }) {
                        mutate { $0.materials.append(m) }
                        loadThumbnails(m)
                    }
                } catch {
                    failures.append(error.localizedDescription)
                }
            }
            if !failures.isEmpty { errorMessage = failures.joined(separator: "\n") }
        }
    }

    func addToTimeline(_ m: Material) {
        var id: UUID?
        mutate { id = TimelineEditor.addMedia(m, to: &$0, at: playhead) }
        selection = id
    }

    func addAsOverlay(_ m: Material) {
        var id: UUID?
        mutate { id = TimelineEditor.addOverlay(m, to: &$0, at: playhead) }
        selection = id
    }

    /// Material da biblioteca solto na timeline, no tempo e na linha (ordem de
    /// exibição) indicados.
    func dropMaterial(_ id: UUID, at time: Micros, row: Int?) {
        guard let m = project.material(id) else { return }
        let track = trackIndex(forRow: row)
        var newId: UUID?
        mutate { newId = TimelineEditor.insertMedia(m, into: &$0, at: time, track: track) }
        selection = newId
    }

    /// Onde o material cairia: linha de exibição (nil se for criada uma faixa) e intervalo.
    func dropPreview(_ id: UUID, at time: Micros, row: Int?) -> (row: Int?, range: TimeRange)? {
        guard let m = project.material(id) else { return nil }
        var p = project
        let newId = TimelineEditor.insertMedia(m, into: &p, at: time, track: trackIndex(forRow: row))
        guard let loc = p.locate(newId), let seg = p.segment(newId) else { return nil }
        // Uma faixa criada para o material não existe no projeto atual.
        let trackId = p.tracks[loc.track].id
        let row = project.tracks.firstIndex { $0.id == trackId }.flatMap { displayTrackOrder().firstIndex(of: $0) }
        return (row, seg.targetRange)
    }

    private func trackIndex(forRow row: Int?) -> Int? {
        let order = displayTrackOrder()
        return row.flatMap { order.indices.contains($0) ? order[$0] : nil }
    }

    func removeMaterial(_ m: Material) {
        mutate { p in
            let ids = p.tracks.flatMap(\.segments).filter { $0.materialId == m.id }.map(\.id)
            for id in ids { TimelineEditor.delete(&p, segment: id) }
            p.materials.removeAll { $0.id == m.id }
        }
        thumbnails[m.id] = nil
        waveforms[m.id] = nil
    }

    func usageCount(_ m: Material) -> Int {
        project.tracks.flatMap(\.segments).filter { $0.materialId == m.id }.count
    }

    private func loadThumbnails(_ m: Material) {
        guard thumbnails[m.id] == nil else { return }
        if m.hasAudio {
            Task { waveforms[m.id] = try? await Waveform.peaks(url: m.url) }
        }
        Task {
            switch m.kind {
            case .video:
                let generator = AVAssetImageGenerator(asset: AVURLAsset(url: m.url))
                generator.appliesPreferredTrackTransform = true
                // Nítidas também numa faixa alta; um quadro por segundo (de 10 a 30).
                generator.maximumSize = CGSize(width: 480, height: 480)
                var images: [CGImage] = []
                let count = min(30, max(10, Int(m.duration.seconds)))
                for i in 0..<count {
                    let t = CMTime(seconds: m.duration.seconds * (Double(i) + 0.5) / Double(count), preferredTimescale: 600)
                    if let (image, _) = try? await generator.image(at: t) { images.append(image) }
                }
                thumbnails[m.id] = images
            case .photo:
                if let src = CGImageSourceCreateWithURL(m.url as CFURL, nil),
                   let image = CGImageSourceCreateThumbnailAtIndex(src, 0, [
                       kCGImageSourceCreateThumbnailFromImageAlways: true,
                       kCGImageSourceCreateThumbnailWithTransform: true,
                       kCGImageSourceThumbnailMaxPixelSize: 480,
                   ] as CFDictionary) {
                    thumbnails[m.id] = [image]
                }
            case .audio:
                thumbnails[m.id] = []
            }
        }
    }

    // MARK: - Edição

    /// Segmento a editar: o selecionado, ou o da faixa principal sob o cursor.
    private var targetSegmentId: UUID? {
        if let selection { return selection }
        guard let main = project.mainTrackIndex else { return nil }
        return project.tracks[main].segments.first { $0.targetRange.contains(playhead) }?.id
    }

    func splitAtPlayhead() {
        guard let id = targetSegmentId else { return }
        var right: UUID?
        mutate { right = TimelineEditor.split(&$0, segment: id, at: playhead) }
        if let right { selection = right }
    }

    func deleteSelection() {
        guard let id = selection else { return }
        mutate { TimelineEditor.delete(&$0, segment: id) }
        selection = nil
    }

    func duplicateSelection() {
        guard let id = selection else { return }
        var copy: UUID?
        mutate { copy = TimelineEditor.duplicate(&$0, segment: id) }
        selection = copy
    }

    func addText(style: TextStyle = TextStyle()) {
        var id: UUID?
        mutate { id = TimelineEditor.addText(TextContent(text: "Digite seu texto", style: style), to: &$0, at: playhead) }
        selection = id
        inspectorTab = .text
    }

    func importSubtitles() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "srt") ?? .plainText]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let content = try String(contentsOf: url, encoding: .utf8)
            let cues = SRT.parse(content)
            guard !cues.isEmpty else {
                errorMessage = "Nenhuma legenda encontrada em \(url.lastPathComponent)."
                return
            }
            var style = TextStyle()
            style.fontSize = 0.055
            style.strokeWidth = 3
            mutate { SRT.importCues(cues, into: &$0, style: style) }
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func exportSubtitles() {
        let cues = SRT.cues(from: project)
        guard !cues.isEmpty else {
            errorMessage = "Não há textos na timeline para exportar como legenda."
            return
        }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "srt") ?? .plainText]
        panel.nameFieldStringValue = "\(project.name).srt"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try SRT.serialize(cues).write(to: url, atomically: true, encoding: .utf8)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    // MARK: - Legendas

    /// Troca o texto de uma legenda (edição em linha do painel Legendas).
    func setText(_ id: UUID, _ text: String) {
        mutate { TimelineEditor.update(&$0, segment: id) { $0.text?.text = text } }
    }

    func deleteSegment(_ id: UUID) {
        mutate { TimelineEditor.delete(&$0, segment: id) }
        if selection == id { selection = nil }
    }

    /// O clipe selecionado, se tiver áudio para transcrever.
    var selectedClipWithAudio: Segment? {
        guard let seg = selectedSegment, let m = project.material(seg.materialId),
              m.kind == .audio || m.hasAudio else { return nil }
        return seg
    }

    /// Por que a fonte escolhida não pode ser transcrita (`nil` = pode).
    var captionSourceProblem: String? {
        switch captionGeneration.source {
        case .clip:
            return selectedClipWithAudio == nil ? "Selecione um clipe com áudio para transcrever só ele." : nil
        case .timeline:
            let audible = project.tracks.contains { track in
                !track.muted && track.segments.contains { seg in
                    guard seg.volume > 0, let m = project.material(seg.materialId) else { return false }
                    return m.kind == .audio || m.hasAudio
                }
            }
            return audible ? nil : "A timeline não tem áudio para transcrever."
        }
    }

    /// Há uma faixa de legendas automáticas no projeto.
    var hasAutoCaptions: Bool { project.tracks.contains { $0.isAutoCaptions } }

    func presentAutoCaptions() {
        let g = captionGeneration
        if !g.phase.isRunning {
            g.setPhase(.configuring)
            g.replacePrevious = hasAutoCaptions
        }
        if !g.languageChosen {
            g.language = CaptionGeneration.systemLanguage(in: Transcriber.languages.map(\.code))
            g.languageChosen = true
        }
        if selectedClipWithAudio == nil { g.source = .timeline }
        g.refreshInstalled()
        showAutoCaptions = true
    }

    /// Baixa o modelo se faltar, transcreve, monta as legendas e as põe na timeline
    /// num único passo de desfazer.
    func generateCaptions() {
        let g = captionGeneration
        guard !g.phase.isRunning, captionSourceProblem == nil else { return }
        let source: TranscriptionSource
        switch g.source {
        case .timeline: source = .timeline
        case .clip:
            guard let clip = selectedClipWithAudio else { return }
            source = .segment(clip.id)
        }
        let snapshot = project
        let range: TimeRange
        do {
            range = try Transcriber.sourceRange(project: snapshot, source: source)
        } catch {
            g.setPhase(.failed(error.localizedDescription))
            return
        }
        if isPlaying { togglePlay() }
        let whisper = g.model, preset = g.preset, language = g.language
        let replacing = g.replacePrevious, color = g.highlightColor
        let store = g.store
        g.cancelDownload(whisper)
        g.includesDownload = !store.isInstalled(whisper)
        g.setPhase(g.includesDownload ? .downloading(received: 0, total: whisper.byteSize) : .mixing(0))
        g.job = Task { [weak self, g] in
            do {
                if g.includesDownload {
                    try await store.download(whisper) { fraction in
                        Task { @MainActor in
                            guard case .downloading = g.phase else { return }
                            g.setPhase(.downloading(received: Int64(Double(whisper.byteSize) * fraction), total: whisper.byteSize))
                        }
                    }
                    g.refreshInstalled()
                    try Task.checkCancellation()
                    g.setPhase(.mixing(0))
                }
                let words = try await Transcriber.transcribe(project: snapshot, source: source, model: whisper,
                                                             language: language) { p in
                    Task { @MainActor in
                        guard g.phase.isRunning else { return }
                        g.setPhase(p.phase == .mixing ? .mixing(p.overall) : .transcribing(p.overall))
                    }
                }
                try Task.checkCancellation()
                let cues = CaptionBuilder.cues(from: words, preset: preset, aspect: snapshot.aspectValue)
                guard !cues.isEmpty else {
                    throw TranscriptionError.failed("nenhuma fala foi reconhecida nesse trecho.")
                }
                self?.applyCaptions(cues, preset: preset, accent: color, replacing: replacing, range: range)
                g.setPhase(.finished(count: cues.count))
            } catch {
                if Task.isCancelled || error is CancellationError {
                    g.setPhase(.configuring)
                } else {
                    g.setPhase(.failed(error.localizedDescription))
                }
            }
            g.job = nil
        }
    }

    private func applyCaptions(_ cues: [CaptionCue], preset: CaptionPreset, accent: RGBA,
                               replacing: Bool, range: TimeRange) {
        mutate { CaptionBuilder.apply(cues, preset: preset, accent: accent, to: &$0, replacing: replacing, range: range) }
        selection = nil
        libraryPanel = .captions
        libraryVisible = true
    }

    func cancelCaptionGeneration() {
        captionGeneration.job?.cancel()
        captionGeneration.job = nil
        captionGeneration.setPhase(.configuring)
    }

    /// Copia o estilo do texto selecionado para todas as legendas automáticas: o destaque inteiro
    /// (revelação incluída) e o visual da palavra-chave (fonte, escala e cor), sem as palavras
    /// escolhidas, que são de cada legenda.
    func applyStyleToAutoCaptions() {
        guard let source = selectedSegment?.text else { return }
        mutate { p in
            for t in p.tracks.indices where p.tracks[t].isAutoCaptions {
                for i in p.tracks[t].segments.indices {
                    guard var text = p.tracks[t].segments[i].text else { continue }
                    text.style = source.style
                    if let highlight = source.highlight, text.words != nil {
                        text.highlight = highlight
                    }
                    if var emphasis = source.emphasis {
                        emphasis.words = text.emphasis?.words ?? []
                        text.emphasis = emphasis
                    }
                    p.tracks[t].segments[i].text = text
                }
            }
        }
    }

    // MARK: - Arrastar na timeline

    /// Animação dos clipes que abrem espaço ou voltam ao lugar.
    static let slide = Animation.easeOut(duration: 0.18)

    /// Arraste do clipe: `dx` em pontos; `rows`, deslocamento vertical em linhas.
    /// O clipe segue o cursor nos dois sentidos (`dragPreview`); na faixa
    /// principal, os vizinhos deslizam para abrir espaço.
    func dragMove(_ id: UUID, dx: CGFloat, rows: Int) {
        beginGesture()
        selection = id
        guard let base = gestureBase, let seg = base.segment(id), let loc = base.locate(id) else { return }
        let row = dragTargetRow(id, rows: rows, in: base)
        let delta = Micros(Double(dx) / zoom * 1_000_000)
        if base.tracks[loc.track].isMain {
            let start = max(0, seg.targetStart + delta)
            setDragTarget(row.map { DragTarget(segment: id, rows: rows, row: $0, start: start) })
            setDragPreview(DragPreview(segment: id, start: start, rows: row == nil ? 0 : rows))
            if row != nil {
                // Saindo da faixa principal: ela fica como estava até soltar.
                slideGesture { _ in }
            } else {
                let center = start + seg.targetDuration / 2
                slideGesture { TimelineEditor.reorderMain(&$0, segment: id, toCenter: center) }
            }
            return
        }
        let points = TimelineEditor.snapPoints(base, excluding: id, extra: [playhead])
        let threshold = Micros(8 / zoom * 1_000_000)
        let start = TimelineEditor.snapMove(start: max(0, seg.targetStart + delta), duration: seg.targetDuration,
                                            to: points, threshold: threshold, forward: dx >= 0)
        setDragTarget(row.map { DragTarget(segment: id, rows: rows, row: $0, start: start) })
        setDragPreview(DragPreview(segment: id, start: start, rows: row == nil ? 0 : rows))
        updateGesture { TimelineEditor.move(&$0, segment: id, toStart: start) }
    }

    /// Ao soltar: move para outra faixa (deslocamento vertical em linhas) e
    /// resolve sobreposições. O clipe desliza do cursor até o lugar final.
    func endDragMove(_ id: UUID) {
        withAnimation(Self.slide) {
            if let target = dragTarget, target.segment == id, gestureBase != nil {
                let order = displayTrackOrder()
                if order.indices.contains(target.row) {
                    let track = order[target.row]
                    updateGestureInPlace { p in
                        TimelineEditor.moveToTrack(&p, segment: id, track: track)
                        TimelineEditor.move(&p, segment: id, toStart: target.start)
                    }
                }
            }
            dragTarget = nil
            dragPreview = nil
            updateGestureInPlace { TimelineEditor.resolveOverlap(&$0, segment: id) }
        }
        endGesture()
    }

    /// Atualiza o gesto animando os outros clipes quando a ordem da faixa principal muda.
    private func slideGesture(_ change: (inout Project) -> Void) {
        let before = project.mainTrackIndex.map { project.tracks[$0].segments.map(\.id) }
        guard var copy = gestureBase else { return }
        change(&copy)
        let after = copy.mainTrackIndex.map { copy.tracks[$0].segments.map(\.id) }
        if before != after {
            withAnimation(Self.slide) { updateGesture(change) }
        } else {
            updateGesture(change)
        }
    }

    /// Linha de destino (ordem de exibição) do arraste vertical, só quando a
    /// faixa aceita o clipe.
    private func dragTargetRow(_ id: UUID, rows: Int, in base: Project) -> Int? {
        let order = displayTrackOrder()
        guard rows != 0, let loc = base.locate(id), let row = order.firstIndex(of: loc.track),
              order.indices.contains(row + rows),
              TimelineEditor.canMove(base, segment: id, toTrack: order[row + rows])
        else { return nil }
        return row + rows
    }

    private func setDragTarget(_ target: DragTarget?) {
        if target != dragTarget { dragTarget = target }
    }

    private func setDragPreview(_ preview: DragPreview?) {
        if preview != dragPreview { dragPreview = preview }
    }

    /// Aparar pela alça: a borda arrastada segue o cursor nos dois lados.
    func dragTrim(_ id: UUID, leading: Bool, dx: CGFloat) {
        beginGesture()
        selection = id
        guard let base = gestureBase, let seg = base.segment(id), let loc = base.locate(id) else { return }
        let delta = Micros(Double(dx) / zoom * 1_000_000)
        let points = TimelineEditor.snapPoints(base, excluding: id, extra: [playhead])
        let threshold = Micros(8 / zoom * 1_000_000)
        if leading {
            let target = TimelineEditor.snap(seg.targetStart + delta, to: points, threshold: threshold)
            updateGesture { TimelineEditor.trimStart(&$0, segment: id, to: target) }
            // A faixa principal é magnética: o clipe aparado volta ao início do
            // espaço e os seguintes o acompanham. Durante o gesto eles ficam onde
            // estavam, com o início sob o cursor; o espaço fecha ao soltar.
            if base.tracks[loc.track].isMain, let now = project.segment(id) {
                let shift = seg.targetDuration - now.targetDuration
                setDragPreview(DragPreview(segment: id, start: now.targetStart + shift, rows: 0, following: shift))
            }
        } else {
            let target = TimelineEditor.snap(seg.targetEnd + delta, to: points, threshold: threshold)
            updateGesture { TimelineEditor.trimEnd(&$0, segment: id, to: target) }
        }
    }

    func endDragTrim() {
        if dragPreview != nil {
            withAnimation(Self.slide) { dragPreview = nil }
        }
        endGesture()
    }

    private func updateGestureInPlace(_ change: (inout Project) -> Void) {
        var copy = project
        change(&copy)
        guard copy != project else { return }
        project = copy
        scheduleAutosave()
        projectDidChange()
    }

    /// Ordem das faixas de cima para baixo: textos, sobreposições, principal, áudios.
    func displayTrackOrder() -> [Int] {
        let t = project.tracks
        let texts = t.indices.filter { t[$0].kind == .text }.reversed()
        let overlays = t.indices.filter { t[$0].kind == .video && !t[$0].isMain }.reversed()
        let main = t.indices.filter { t[$0].isMain }
        let audios = t.indices.filter { t[$0].kind == .audio }
        return Array(texts) + Array(overlays) + main + audios
    }

    // MARK: - Altura das faixas

    func rowHeight(_ track: Track) -> CGFloat {
        CGFloat(track.height ?? Track.defaultHeight)
    }

    /// Geometria vertical da timeline com as faixas na ordem de exibição.
    func timelineLayout(_ tracks: [Track]) -> TimelineLayout {
        TimelineLayout(heights: tracks.map(rowHeight))
    }

    /// Muda a altura de uma faixa ao vivo, durante o arraste da borda (nil volta à padrão); o desfazer
    /// é um só, ao soltar (`endRowResize`). Só a timeline muda: o preview não é refeito.
    func resizeRow(_ id: UUID, to height: CGFloat?) {
        beginGesture()
        guard var copy = gestureBase else { return }
        TimelineEditor.setTrackHeight(&copy, track: id, height: height.map(Double.init))
        guard copy != project else { return }
        project = copy
        isDirty = true
        scheduleAutosave()
    }

    /// Fim do arraste da borda da faixa (ou do duplo clique).
    func endRowResize() {
        endGesture()
    }

    /// Mesma altura para todas as faixas (nil = a padrão), num passo de desfazer.
    func setAllRowHeights(_ height: CGFloat?) {
        mutate { TimelineEditor.setAllTrackHeights(&$0, height: height.map(Double.init)) }
    }

    /// Altura comum a todas as faixas, ou nil se forem diferentes.
    var commonRowHeight: CGFloat? {
        let heights = Set(project.tracks.map(rowHeight))
        return heights.count == 1 ? heights.first : nil
    }

    // MARK: - Extrair áudio

    /// O selecionado é um vídeo com som que ainda não foi extraído.
    var canExtractAudio: Bool {
        selection.map { TimelineEditor.canExtractAudio(project, segment: $0) } ?? false
    }

    /// Extrai o som do clipe selecionado para uma faixa de áudio (um passo de desfazer) e seleciona o
    /// clipe de áudio novo.
    func extractAudio() {
        guard let id = selection, TimelineEditor.canExtractAudio(project, segment: id) else { return }
        var sound: UUID?
        mutate { sound = TimelineEditor.extractAudio(&$0, segment: id) }
        guard let sound else { return }
        selection = sound
        if let m = project.material(project.segment(sound)?.materialId), waveforms[m.id] == nil {
            loadThumbnails(m)
        }
    }

    // MARK: - Gravar áudio

    /// Abre o painel de gravação: pede a permissão do microfone e começa a escutar só o nível.
    func presentRecorder() {
        let r = recording
        guard !r.phase.isActive else { return }
        r.showPanel = true
        r.phase = .ready
        r.confirmDiscard = false
        r.inputs = AudioRecorder.inputs().map { AudioRecording.Input(id: $0.id, name: $0.name) }
        r.defaultInput = AudioRecorder.defaultInput
        if let input = r.input, !r.inputs.contains(where: { $0.id == input }) { r.input = nil }
        Task {
            guard await AudioRecorder.requestPermission() else {
                r.phase = .failed(RecordingError.permissionDenied.localizedDescription)
                return
            }
            guard r.showPanel, r.phase == .ready else { return }
            guard !r.inputs.isEmpty else {
                r.phase = .failed(RecordingError.noInput.localizedDescription)
                return
            }
            startRecordingMonitor()
        }
    }

    /// Fecha o painel (não durante a contagem nem a gravação).
    func closeRecorder() {
        let r = recording
        guard !r.phase.isActive else { return }
        stopRecordingMonitor()
        r.showPanel = false
    }

    func selectRecordingInput(_ id: String?) {
        recording.input = id
        if recording.monitor != nil { startRecordingMonitor() }
    }

    /// O nível do painel: grava o microfone num arquivo temporário, apagado ao parar.
    private func startRecordingMonitor() {
        let r = recording
        stopRecordingMonitor()
        let monitor = AudioRecorder()
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("openframe-nivel-\(UUID().uuidString).m4a")
        do {
            try monitor.start(input: r.input, to: url)
        } catch {
            r.phase = .failed(error.localizedDescription)
            return
        }
        r.monitor = monitor
        startRecordingTimer()
    }

    private func stopRecordingMonitor() {
        let r = recording
        r.monitor?.cancel()
        r.monitor = nil
        if r.phase != .recording {
            r.timer?.invalidate()
            r.timer = nil
            r.level = 0
        }
    }

    private func startRecordingTimer() {
        recording.timer?.invalidate()
        recording.timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.recordingTick() }
        }
    }

    private func recordingTick() {
        let r = recording
        guard let source = r.phase == .recording ? r.recorder : r.monitor else { return }
        r.level = max(source.takeLevel(), r.level * 0.82)
        guard r.phase == .recording else { return }
        r.peaks = source.peaks
        r.duration = source.duration
        // Fim do projeto: a reprodução parou sozinha.
        if r.stopsAtEnd, !isPlaying { stopRecording() }
    }

    /// Botão Gravar: contagem de 3 s e, depois, a gravação a partir do playhead.
    func startRecording() {
        let r = recording
        guard r.phase == .ready else { return }
        r.phase = .countdown(3)
        r.countdownTask = Task { [weak self] in
            for n in [3, 2, 1] {
                r.phase = .countdown(n)
                try? await Task.sleep(for: .seconds(1))
                if Task.isCancelled { return }
            }
            self?.beginRecording()
        }
    }

    private func beginRecording() {
        let r = recording
        stopRecordingMonitor()
        if isPlaying { togglePlay() }
        let url = library.recordingURL(for: libraryID)
        do {
            try r.recorder.start(input: r.input, to: url)
        } catch {
            r.phase = .failed(error.localizedDescription)
            return
        }
        r.url = url
        r.start = playhead
        r.peaks = []
        r.duration = 0
        r.phase = .recording
        selection = nil
        // A timeline toca sem som, para o áudio do projeto não vazar para o microfone.
        player.isMuted = true
        r.stopsAtEnd = playhead < duration - 30_000
        if r.stopsAtEnd {
            excerpt = nil
            isPlaying = true
            player.play()
        }
        startRecordingTimer()
    }

    /// Para a gravação (botão, espaço ou fim do projeto): o material entra na biblioteca de mídia e o
    /// clipe na timeline, num passo de desfazer, e fica selecionado.
    func stopRecording() {
        let r = recording
        guard r.phase == .recording else { return }
        r.phase = .finishing
        r.timer?.invalidate()
        r.timer = nil
        if isPlaying { togglePlay() }
        player.isMuted = false
        Task {
            do {
                _ = try await r.recorder.stop()
                guard let url = r.url else { return }
                let material = try await MediaProbe.probe(url)
                var id: UUID?
                mutate { id = TimelineEditor.addRecording(material, to: &$0, at: r.start) }
                loadThumbnails(material)
                selection = id
                r.phase = .ready
                r.level = 0
                r.showPanel = false
            } catch {
                r.phase = .failed(error.localizedDescription)
            }
        }
    }

    /// Esc: na contagem, volta ao painel; gravando, joga fora (perguntando antes, se passou de 3 s).
    func cancelRecording(confirmed: Bool = false) {
        let r = recording
        switch r.phase {
        case .countdown:
            r.countdownTask?.cancel()
            r.phase = .ready
        case .recording:
            if r.duration > 3_000_000, !confirmed {
                r.confirmDiscard = true
                return
            }
            r.timer?.invalidate()
            r.timer = nil
            r.recorder.cancel()
            if isPlaying { togglePlay() }
            player.isMuted = false
            r.phase = .ready
            r.peaks = []
            r.duration = 0
            startRecordingMonitor()
        default:
            break
        }
    }

    /// Linha (na ordem de exibição) onde a gravação vai cair: a primeira faixa de áudio livre no trecho;
    /// `tracks.count` = uma faixa nova embaixo.
    func recordingRow(_ tracks: [Track]) -> Int {
        let r = recording
        let range = TimeRange(start: r.start, duration: max(r.duration, 1))
        guard let t = TimelineEditor.freeTrack(kind: .audio, range: range, in: project, excluding: nil),
              let row = tracks.firstIndex(where: { $0.id == project.tracks[t].id })
        else { return tracks.count }
        return row
    }

    // MARK: - Projetos

    func refreshLibrary() {
        entries = library.list()
        fillMissingThumbnails()
    }

    /// Projetos sem miniatura (criados antes dela existir ou fechados sem voltar à
    /// tela inicial) ganham uma em segundo plano, um por vez.
    private func fillMissingThumbnails() {
        guard screen == .home, missingThumbnailsTask == nil else { return }
        let pending = entries.filter { $0.thumbnailModified == nil && $0.duration > 0 && !thumbnailAttempts.contains($0.id) }
        guard !pending.isEmpty else { return }
        missingThumbnailsTask = Task { [weak self] in
            for entry in pending {
                guard let self, self.screen == .home, !Task.isCancelled else { break }
                self.thumbnailAttempts.insert(entry.id)
                guard let project = try? self.library.load(entry.id) else { continue }
                await self.writeThumbnail(of: project, id: entry.id)
                self.entries = self.library.list()
            }
            self?.missingThumbnailsTask = nil
        }
    }

    /// Cria um projeto na biblioteca com o formato escolhido e abre o editor.
    func createProject(aspect: AspectRatio = .original) {
        do {
            let (entry, created) = try library.create(aspect: aspect)
            load(created, libraryID: entry.id, documentURL: nil)
        } catch {
            errorMessage = "Não foi possível criar o projeto: \(error.localizedDescription)"
        }
    }

    func openLibraryProject(_ id: UUID) {
        do {
            load(try library.load(id), libraryID: id, documentURL: nil)
        } catch {
            errorMessage = "Não foi possível abrir o projeto: \(error.localizedDescription)"
            refreshLibrary()
        }
    }

    /// Volta para a tela inicial, gravando o projeto e a miniatura.
    func showHome() {
        guard confirmDiscardChanges() else { return }
        if isPlaying { togglePlay() }
        saveNow()
        let id = libraryID
        let snapshot = project
        rebuildTask?.cancel()
        player.replaceCurrentItem(with: nil)
        structureKey = nil
        planBox = nil
        libraryID = nil
        documentURL = nil
        isDirty = false
        selection = nil
        screen = .home
        refreshLibrary()
        if let id {
            thumbnailTask?.cancel()
            thumbnailTask = Task { [weak self] in
                await self?.writeThumbnail(of: snapshot, id: id)
                self?.refreshLibrary()
            }
        }
    }

    func renameProject(_ id: UUID, to name: String) {
        do {
            try library.rename(id, to: name)
            refreshLibrary()
        } catch {
            errorMessage = "Não foi possível renomear: \(error.localizedDescription)"
        }
    }

    /// Nome do projeto aberto, editado na barra superior.
    func renameCurrentProject(_ name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed != project.name else { return }
        project.name = trimmed
        isDirty = true
        scheduleAutosave()
    }

    func duplicateProject(_ id: UUID) {
        do {
            try library.duplicate(id)
            refreshLibrary()
        } catch {
            errorMessage = "Não foi possível duplicar: \(error.localizedDescription)"
        }
    }

    func deleteProject(_ id: UUID) {
        do {
            try library.delete(id)
            refreshLibrary()
        } catch {
            errorMessage = "Não foi possível apagar: \(error.localizedDescription)"
        }
    }

    func revealProject(_ id: UUID) {
        NSWorkspace.shared.activateFileViewerSelecting([library.folder(for: id).appendingPathComponent(ProjectLibrary.projectFile)])
    }

    /// ⌘N: projeto novo direto no editor.
    func newProject() {
        guard confirmDiscardChanges() else { return }
        saveNow()
        createProject()
    }

    /// ⌘O: escolhe um `.openframe`, traz para a biblioteca e abre.
    func openProject() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: ProjectStore.fileExtension) ?? .json]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        open(url: url)
    }

    /// `.openframe` vindo do Finder ou do menu: importado para a biblioteca e aberto.
    func open(url: URL) {
        guard confirmDiscardChanges() else { return }
        saveNow()
        do {
            let entry = try library.importProject(from: url)
            openLibraryProject(entry.id)
        } catch {
            errorMessage = "Não foi possível abrir o projeto: \(error.localizedDescription)"
        }
    }

    /// `-open caminho`: abre o arquivo direto, fora da biblioteca (desenvolvimento e capturas).
    func openExternal(url: URL) {
        do {
            load(try ProjectStore.load(from: url), libraryID: nil, documentURL: url)
        } catch {
            errorMessage = "Não foi possível abrir o projeto: \(error.localizedDescription)"
        }
    }

    private func load(_ loaded: Project, libraryID: UUID?, documentURL: URL?) {
        missingThumbnailsTask?.cancel()
        missingThumbnailsTask = nil
        if isPlaying { togglePlay() }
        autosaveTask?.cancel()
        project = loaded
        undoStack.removeAll()
        redoStack.removeAll()
        selection = nil
        self.libraryID = libraryID
        self.documentURL = documentURL
        isDirty = false
        playhead = 0
        thumbnails.removeAll()
        waveforms.removeAll()
        loaded.materials.forEach(loadThumbnails)
        screen = .editor
        lastThumbnail = Date()
        projectDidChange()
        let missing = loaded.materials.filter { !FileManager.default.fileExists(atPath: $0.path) }
        if !missing.isEmpty {
            errorMessage = "Arquivos não encontrados:\n" + missing.map(\.path).joined(separator: "\n")
        }
    }

    // MARK: - Salvar

    /// Projetos da biblioteca se salvam sozinhos, um instante depois da última alteração.
    private func scheduleAutosave() {
        guard libraryID != nil else { return }
        autosaveTask?.cancel()
        autosaveTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(800))
            guard !Task.isCancelled else { return }
            self?.saveNow()
        }
    }

    /// Grava já o projeto da biblioteca; a miniatura acompanha no máximo a cada 10 s.
    func saveNow() {
        autosaveTask?.cancel()
        guard let id = libraryID, isDirty else { return }
        do {
            try library.save(project, id: id)
            isDirty = false
        } catch {
            errorMessage = "Não foi possível salvar: \(error.localizedDescription)"
            return
        }
        if Date().timeIntervalSince(lastThumbnail) > 10 {
            lastThumbnail = Date()
            let snapshot = project
            thumbnailTask?.cancel()
            thumbnailTask = Task { [weak self] in await self?.writeThumbnail(of: snapshot, id: id) }
        }
    }

    /// Quadro do projeto (a 1/3 da duração, até 2 s) gravado como miniatura da tela inicial.
    private func writeThumbnail(of snapshot: Project, id: UUID) async {
        guard snapshot.duration > 0 else { return }
        do {
            let built = try await CompositionBuilder.build(project: snapshot, renderSize: snapshot.canvasSize(shortSide: 360),
                                                           frameDuration: CMTime(value: 1, timescale: 30),
                                                           includeAudio: false)
            built.box.update(project: snapshot)
            let generator = AVAssetImageGenerator(asset: built.composition)
            generator.videoComposition = built.videoComposition
            generator.requestedTimeToleranceBefore = .zero
            generator.requestedTimeToleranceAfter = .zero
            let time = Micros(min(Double(snapshot.duration) / 3, 2_000_000))
            let (image, _) = try await generator.image(at: time.cmTime)
            guard !Task.isCancelled else { return }
            let data = NSMutableData()
            guard let dest = CGImageDestinationCreateWithData(data, UTType.jpeg.identifier as CFString, 1, nil) else { return }
            CGImageDestinationAddImage(dest, image, [kCGImageDestinationLossyCompressionQuality: 0.8] as CFDictionary)
            guard CGImageDestinationFinalize(dest) else { return }
            try library.writeThumbnail(data as Data, id: id)
        } catch {
            // Sem miniatura, a tela inicial mostra o formato do projeto.
        }
    }

    /// ⌘S: grava já (biblioteca) ou no arquivo aberto de fora.
    @discardableResult
    func saveProject() -> Bool {
        if libraryID != nil {
            saveNow()
            return true
        }
        return saveDocument(saveAs: false)
    }

    /// ⇧⌘S: grava uma cópia `.openframe` onde o usuário escolher.
    func exportProjectFile() {
        if libraryID == nil {
            saveDocument(saveAs: true)
            return
        }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: ProjectStore.fileExtension) ?? .json]
        panel.nameFieldStringValue = "\(project.name).\(ProjectStore.fileExtension)"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try ProjectStore.save(project, to: url)
        } catch {
            errorMessage = "Não foi possível exportar o projeto: \(error.localizedDescription)"
        }
    }

    @discardableResult
    private func saveDocument(saveAs: Bool) -> Bool {
        var url = documentURL
        if url == nil || saveAs {
            let panel = NSSavePanel()
            panel.allowedContentTypes = [UTType(filenameExtension: ProjectStore.fileExtension) ?? .json]
            panel.nameFieldStringValue = "\(project.name).\(ProjectStore.fileExtension)"
            guard panel.runModal() == .OK, let chosen = panel.url else { return false }
            url = chosen
        }
        guard let url else { return false }
        do {
            var p = project
            p.name = url.deletingPathExtension().lastPathComponent
            try ProjectStore.save(p, to: url)
            project = p
            documentURL = url
            isDirty = false
            return true
        } catch {
            errorMessage = "Não foi possível salvar: \(error.localizedDescription)"
            return false
        }
    }

    /// Só arquivos de fora da biblioteca pedem confirmação; a biblioteca se salva sozinha.
    private func confirmDiscardChanges() -> Bool {
        guard libraryID == nil, documentURL != nil || screen == .editor, isDirty,
              project.duration > 0 || !project.materials.isEmpty else { return true }
        let alert = NSAlert()
        alert.messageText = "Salvar as alterações em “\(project.name)”?"
        alert.addButton(withTitle: "Salvar")
        alert.addButton(withTitle: "Cancelar")
        alert.addButton(withTitle: "Descartar")
        switch alert.runModal() {
        case .alertFirstButtonReturn: return saveProject()
        case .alertThirdButtonReturn: return true
        default: return false
        }
    }

    // MARK: - Zoom da timeline

    static let maxZoom = 600.0

    /// Zoom em que o projeto inteiro cabe na largura visível da timeline.
    var fitZoom: Double {
        let usable = Double(timelineViewportWidth) - 48
        guard duration > 0, usable > 0 else { return 80 }
        return min(usable / duration.seconds, Self.maxZoom)
    }

    /// 8 pt/s, ou menos quando é preciso para um projeto longo caber inteiro.
    var minZoom: Double { max(0.01, min(8, fitZoom)) }

    func zoomIn() { setZoom(zoom * 1.4) }
    func zoomOut() { setZoom(zoom / 1.4) }

    /// Mostra o projeto inteiro, a partir do início.
    func zoomToFit() { setZoom(fitZoom, keeping: 0, at: 0) }

    /// Muda o zoom mantendo o tempo `time` (s) na posição `x` da área visível.
    /// Sem âncora, mantém o playhead (se estiver à vista) ou o centro.
    func setZoom(_ value: Double, keeping time: Double? = nil, at x: CGFloat? = nil) {
        let new = min(max(value, minZoom), Self.maxZoom)
        guard let scroll = timelineScrollView else {
            zoom = new
            return
        }
        let origin = timelineScrollOrigin
        let anchor: (time: Double, x: CGFloat)
        if let time, let x {
            anchor = (time, x)
        } else {
            let playheadX = CGFloat(playhead.seconds * zoom) - origin
            let width = scroll.contentView.bounds.width
            anchor = (0...width).contains(playheadX)
                ? (playhead.seconds, playheadX)
                : (Double(origin + width / 2) / zoom, width / 2)
        }
        zoom = new
        // O conteúdo só muda de largura no próximo layout; a rolagem vem depois.
        pendingTimelineScrollX = CGFloat(anchor.time * new) - anchor.x
        DispatchQueue.main.async { [weak self] in self?.applyPendingTimelineScroll() }
    }

    /// Ponto de zoom sob uma posição `x` do conteúdo da timeline.
    func zoomAnchor(contentX: CGFloat) -> (time: Double, x: CGFloat) {
        (Double(contentX) / zoom, contentX - timelineScrollOrigin)
    }

    /// Deslocamento da rolagem, contando um ajuste ainda não aplicado.
    private var timelineScrollOrigin: CGFloat {
        pendingTimelineScrollX ?? timelineScrollView?.contentView.bounds.origin.x ?? 0
    }

    private func applyPendingTimelineScroll() {
        guard let target = pendingTimelineScrollX, let scroll = timelineScrollView else { return }
        pendingTimelineScrollX = nil
        scroll.window?.contentView?.layoutSubtreeIfNeeded()
        let clip = scroll.contentView
        let maxX = max(0, (scroll.documentView?.frame.width ?? 0) - clip.bounds.width)
        clip.scroll(to: NSPoint(x: min(max(0, target), maxX), y: clip.bounds.origin.y))
        scroll.reflectScrolledClipView(clip)
    }

    /// ⌘ ou ⌥ + roda do mouse sobre a timeline: zoom no ponto do cursor.
    private func installScrollMonitor() {
        scrollMonitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { [weak self] event in
            guard !event.modifierFlags.intersection([.command, .option]).isEmpty else { return event }
            let window = event.windowNumber
            let location = event.locationInWindow
            let delta = event.scrollingDeltaY != 0 ? event.scrollingDeltaY : event.scrollingDeltaX
            let precise = event.hasPreciseScrollingDeltas
            let handled = MainActor.assumeIsolated {
                self?.handleZoomScroll(window: window, location: location, delta: delta, precise: precise) ?? false
            }
            return handled ? nil : event
        }
    }

    private func handleZoomScroll(window: Int, location: NSPoint, delta: CGFloat, precise: Bool) -> Bool {
        guard let scroll = timelineScrollView, scroll.window?.windowNumber == window else { return false }
        let point = scroll.convert(location, from: nil)
        guard scroll.visibleRect.contains(point) else { return false }
        guard delta != 0 else { return true }
        let anchor = zoomAnchor(contentX: timelineScrollOrigin + point.x)
        setZoom(zoom * exp(Double(delta) * (precise ? 0.01 : 0.12)), keeping: anchor.time, at: anchor.x)
        return true
    }

    // MARK: - Rolagem da timeline

    /// Rola a timeline para o playhead ficar à vista. Na reprodução, vira a
    /// página quando ele chega perto da borda direita.
    private func revealPlayhead() {
        guard let scroll = timelineScrollView, pendingTimelineScrollX == nil else { return }
        let clip = scroll.contentView
        let width = clip.bounds.width
        let left = clip.bounds.origin.x
        let x = CGFloat(playhead.seconds * zoom)
        guard width > 0, x < left || x > left + width - 40 else { return }
        let maxX = max(0, (scroll.documentView?.frame.width ?? 0) - width)
        clip.scroll(to: NSPoint(x: min(max(0, x - width * 0.1), maxX), y: clip.bounds.origin.y))
        scroll.reflectScrolledClipView(clip)
    }

    /// Recebe o NSScrollView horizontal da timeline e acompanha a rolagem.
    func attachTimelineScroll(_ scroll: NSScrollView) {
        guard scroll !== timelineScrollView else { return }
        if let timelineScrollObserver { NotificationCenter.default.removeObserver(timelineScrollObserver) }
        timelineScrollView = scroll
        timelineScrollX = scroll.contentView.bounds.origin.x
        scroll.contentView.postsBoundsChangedNotifications = true
        timelineScrollObserver = NotificationCenter.default.addObserver(
            forName: NSView.boundsDidChangeNotification, object: scroll.contentView, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let x = self.timelineScrollView?.contentView.bounds.origin.x,
                      x != self.timelineScrollX else { return }
                self.timelineScrollX = x
            }
        }
    }

    // MARK: - Teclado

    /// Espaço, Delete, Esc, setas, Home/End e ⇧Z funcionam em qualquer lugar, exceto ao digitar texto.
    private func installKeyMonitor() {
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            // O NSEvent não é Sendable: só o código e os modificadores entram no MainActor.
            let keyCode = event.keyCode
            let flags = event.modifierFlags
            let handled = MainActor.assumeIsolated { self?.handleKey(keyCode, flags) ?? false }
            return handled ? nil : event
        }
    }

    private func handleKey(_ keyCode: UInt16, _ flags: NSEvent.ModifierFlags) -> Bool {
        guard screen == .editor else { return false }
        if NSApp.keyWindow?.firstResponder is NSText { return false }
        if !flags.intersection([.command, .control, .option]).isEmpty { return false }
        guard NSApp.keyWindow?.sheetParent == nil, NSApp.keyWindow?.attachedSheet == nil else { return false }
        // Gravando: espaço para, Esc cancela.
        if recording.phase.isActive {
            switch keyCode {
            case 49: stopRecording()
            case 53: cancelRecording()
            default: return false
            }
            return true
        }
        switch keyCode {
        case 49: togglePlay()
        case 6 where flags.contains(.shift): zoomToFit()
        case 51, 117: deleteSelection()
        case 53:
            guard selection != nil else { return false }
            selection = nil
        case 115: goToStart()
        case 119: goToEnd()
        case 126: jumpToEdit(forward: false)
        case 125: jumpToEdit(forward: true)
        case 123: step(frames: flags.contains(.shift) ? -10 : -1)
        case 124: step(frames: flags.contains(.shift) ? 10 : 1)
        default: return false
        }
        return true
    }
}

/// Clipe arrastado para outra faixa: deslocamento em linhas, linha de destino
/// (na ordem de exibição) e início do clipe nela.
struct DragTarget: Equatable {
    let segment: UUID
    let rows: Int
    let row: Int
    let start: Micros
}

/// Onde o clipe do gesto é desenhado: segue o cursor, enquanto o projeto já
/// mostra o resultado (vizinhos no novo lugar).
struct DragPreview: Equatable {
    let segment: UUID
    /// Início desenhado do clipe.
    let start: Micros
    /// Deslocamento vertical em linhas, só sobre uma faixa que aceita o clipe.
    let rows: Int
    /// Na faixa principal, quanto os clipes seguintes ficam deslocados enquanto
    /// o início é aparado.
    var following: Micros = 0
}

/// Habilita no painel de importação só pastas e mídias aceitas pelo `MediaProbe`.
final class MediaPanelFilter: NSObject, NSOpenSavePanelDelegate {
    func panel(_ sender: Any, shouldEnable url: URL) -> Bool {
        if (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true,
           !NSWorkspace.shared.isFilePackage(atPath: url.path) {
            return true
        }
        return MediaProbe.isSupported(url)
    }
}
