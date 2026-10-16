import AppKit
import AVFoundation
import CoreImage
import OpenFrameCore
import SwiftUI

/// Ferramenta de desenvolvimento: com `OPENFRAME_SNAPSHOT_DIR` definido, o app
/// percorre algumas telas, salva capturas das próprias janelas e fecha.
/// Um app pode capturar as próprias views sem permissão de gravação de tela.
@MainActor
enum DebugSnapshot {
    static func runIfRequested(_ model: EditorModel) {
        guard let path = ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_DIR"] else { return }
        let dir = URL(fileURLWithPath: path, isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        Task { @MainActor in
            func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
            await wait(4)
            // O macOS pode restaurar a janela minimizada da sessão anterior.
            for window in NSApp.windows where window.isMiniaturized { window.deminiaturize(nil) }
            await wait(1)
            if model.screen == .home {
                await homeScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "resolucao" {
                await resolutionScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "motion" {
                await motionScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "legendas" {
                await captionsScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "legendas-estilos" {
                await captionStylesScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "audio" {
                await audioScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            if ProcessInfo.processInfo.environment["OPENFRAME_SNAPSHOT_SCENARIO"] == "arraste" {
                await dragScenario(model, dir: dir)
                NSApp.terminate(nil)
                return
            }
            model.seek(to: 1_500_000)
            await wait(1)
            capture("1-projeto", to: dir)
            await capturePlayerFrame(model, to: dir.appendingPathComponent("0-preview-frame.png"))

            let p = model.project
            if let main = p.mainTrackIndex, let first = p.tracks[main].segments.first {
                model.selection = first.id
                model.inspectorTab = .basic
                await wait(1)
                capture("2-clipe-basico", to: dir)
                model.inspectorTab = .filters
                await wait(2)
                capture("3-clipe-filtros", to: dir)
                model.inspectorTab = .transition
                await wait(1)
                capture("4-clipe-transicao", to: dir)
            }
            if let text = p.tracks.first(where: { $0.kind == .text })?.segments.first {
                model.selection = text.id
                model.inspectorTab = .text
                await wait(1)
                capture("5-texto", to: dir)
                // Recursos novos de texto ligados: espaçamentos, sublinhado, contorno, fundo e sombra.
                model.mutate {
                    TimelineEditor.update(&$0, segment: text.id) {
                        $0.text?.style.letterSpacing = 0.12
                        $0.text?.style.lineSpacing = 0.3
                        $0.text?.style.underline = true
                        $0.text?.style.strokeWidth = 6
                        $0.text?.style.backgroundEnabled = true
                        $0.text?.style.backgroundRadius = 0.5
                        $0.text?.style.shadow = true
                        $0.text?.style.shadowAngle = -45
                        $0.text?.style.shadowDistance = 0.1
                    }
                }
                await wait(1)
                await captureInspectorScrolled("5b-texto-rolado", to: dir)
                await capturePlayerFrame(model, at: (text.targetStart + text.targetEnd) / 2,
                                         to: dir.appendingPathComponent("5c-texto-estilizado-frame.png"))
                await captureInspectorTabs(model, segment: text, prefix: "17-inspetor-texto", to: dir)
                model.undo()
                await wait(0.5)
            }
            if let main = p.mainTrackIndex, let first = p.tracks[main].segments.first {
                await captureInspectorTabs(model, segment: first, prefix: "18-inspetor-clipe", to: dir)
            }
            if let audio = p.tracks.first(where: { $0.kind == .audio })?.segments.first {
                model.selection = audio.id
                await wait(1)
                capture("7-audio", to: dir)
            }
            // Apagar o clipe selecionado com o inspetor aberto (já derrubou o app).
            if let main = p.mainTrackIndex, let first = p.tracks[main].segments.first {
                model.selection = first.id
                model.inspectorTab = .basic
                await wait(1)
                model.deleteSelection()
                await wait(1)
                log("apagar com o inspetor aberto: app continua aberto")
                model.undo()
                await wait(0.5)
            }
            // Barra de ações do clipe na primeira e na última faixa exibidas.
            let rows = model.displayTrackOrder().map { p.tracks[$0] }
            if let first = rows.first?.segments.first {
                model.selection = first.id
                await wait(1)
                capture("10-barra-primeira-faixa", to: dir)
            }
            if rows.count > 1, let last = rows.last?.segments.last {
                model.selection = last.id
                model.seek(to: last.targetEnd - 200_000)
                await wait(1)
                capture("11-barra-ultima-faixa", to: dir)
            }
            // Zoom: o playhead (ou o ponto da pinça/roda) fica no mesmo lugar da tela.
            model.selection = nil
            model.seek(to: model.duration * 3 / 4)
            await wait(0.5)
            logZoom("antes", model)
            model.setZoom(model.zoom * 3)
            await wait(1)
            logZoom("aproximar ×3 (âncora: playhead)", model)
            capture("12-zoom-aproximado", to: dir)
            let anchor = model.zoomAnchor(contentX: model.timelineScrollX + 200)
            log("zoom: âncora da roda em x=200 → \(String(format: "%.3f", anchor.time)) s")
            model.setZoom(model.zoom / 2, keeping: anchor.time, at: anchor.x)
            await wait(1)
            logZoom("afastar ÷2 (âncora: x=200)", model)
            log("zoom: \(String(format: "%.3f", anchor.time)) s agora em x="
                + String(format: "%.1f", anchor.time * model.zoom - model.timelineScrollX))
            model.zoomToFit()
            await wait(1)
            logZoom("ajustar à janela", model)
            capture("13-zoom-ajustado", to: dir)
            // Arraste vertical: o clipe mostra a faixa de destino antes de soltar.
            let order = model.displayTrackOrder()
            if let main = p.mainTrackIndex, let row = order.firstIndex(of: main), row > 0,
               let first = p.tracks[main].segments.first {
                model.dragMove(first.id, dx: 120, rows: -1)
                await wait(1)
                log("arraste: destino \(String(describing: model.dragTarget))")
                capture("14-arraste-outra-faixa", to: dir)
                model.endDragMove(first.id)
                await wait(1)
                capture("15-apos-soltar", to: dir)
                model.undo()
            }
            // Navegação pelos cortes e rolagem automática na reprodução.
            model.seek(to: 0)
            for _ in 0..<3 { model.jumpToEdit(forward: true) }
            log("cortes: 3× próximo → \(Time.format(model.playhead))")
            model.jumpToEdit(forward: false)
            log("cortes: anterior → \(Time.format(model.playhead))")
            model.setZoom(500)
            model.goToStart()
            await wait(1)
            model.togglePlay()
            await wait(4)
            if model.isPlaying { model.togglePlay() }
            await wait(0.5)
            logZoom("reprodução a 500 pt/s por 4 s", model)
            capture("16-reproducao-rolagem", to: dir)
            model.goToEnd()
            await wait(0.5)
            logZoom("fim (End)", model)
            model.zoomToFit()
            model.selection = nil
            model.showGrid = true
            model.libraryPanel = .text
            await wait(1)
            capture("8-painel-texto-grade", to: dir)
            model.showGrid = false
            model.libraryPanel = .captions
            await wait(1)
            capture("9-painel-legendas", to: dir)
            model.libraryPanel = .media
            model.showExport = true
            await wait(1.5)
            capture("6-exportar", to: dir)
            NSApp.terminate(nil)
        }
    }

    /// Lê o quadro que o AVPlayer está exibindo (a camada de vídeo não entra no cacheDisplay).
    private static func capturePlayerFrame(_ model: EditorModel, at time: Micros = 2_000_000, to url: URL) async {
        guard let item = model.player.currentItem else {
            log("preview: sem item no player")
            return
        }
        log("preview: status=\(item.status.rawValue) erro=\(String(describing: item.error))")
        let output = AVPlayerItemVideoOutput(pixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        ])
        item.add(output)
        model.seek(to: time)
        try? await Task.sleep(for: .seconds(1.5))
        let time = item.currentTime()
        guard let buffer = output.copyPixelBuffer(forItemTime: time, itemTimeForDisplay: nil) else {
            log("preview: nenhum quadro disponível em \(time.seconds)s")
            return
        }
        let image = CIImage(cvPixelBuffer: buffer)
        try? CIContext().writePNGRepresentation(of: image, to: url, format: .RGBA8,
                                                colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!)
        log("preview: quadro \(CVPixelBufferGetWidth(buffer))x\(CVPixelBufferGetHeight(buffer)) salvo")
        item.remove(output)
    }

    /// Arrastes espelhados (mesma distância para a direita e para a esquerda),
    /// capturados no meio do gesto, para comparar os dois sentidos.
    /// Sem `-open`, o app começa na tela inicial: captura a lista, abre o projeto
    /// mais recente (ou cria um), volta e captura de novo com a miniatura.
    private static func homeScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        capture("inicio-1-lista", to: dir)
        if let first = model.entries.first {
            model.openLibraryProject(first.id)
        } else {
            model.createProject(aspect: .r9x16)
        }
        await wait(2)
        capture("inicio-2-editor", to: dir)
        model.showHome()
        await wait(3)
        model.refreshLibrary()
        await wait(1)
        capture("inicio-3-de-volta", to: dir)
    }

    /// Texto motion: seleção, guias, giro e escala no preview (num instante com texto e
    /// foto), duplo clique num texto e a aba Animação do texto e do clipe.
    private static func motionScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        let original = model.project
        let p = model.project
        let photos = p.tracks.flatMap(\.segments).filter { p.material($0.materialId)?.kind == .photo }
        let texts = p.tracks.filter { $0.kind == .text }.flatMap(\.segments)
        // Um instante com texto e foto juntos (senão, o meio do primeiro texto).
        var time = texts.first.map { ($0.targetStart + $0.targetEnd) / 2 } ?? 1_000_000
        outer: for t in texts {
            for ph in photos where ph.targetStart < t.targetEnd && t.targetStart < ph.targetEnd {
                time = (max(t.targetStart, ph.targetStart) + min(t.targetEnd, ph.targetEnd)) / 2
                break outer
            }
        }
        model.seek(to: time)
        await wait(1.5)
        guard let playerView = playerView() else {
            log("motion: view do player não encontrada")
            return
        }
        let mapping = CanvasMapping(size: playerView.bounds.size)
        let boxes = model.canvasBoxes()
        func describe(_ b: ElementBox) -> String {
            String(format: "%@ centro (%.3f, %.3f) tamanho %.3f×%.3f escala %.2f rotação %.1f°",
                   b.isText ? "texto" : (p.material(p.segment(b.id)?.materialId)?.name ?? "?"),
                   b.center.x, b.center.y, b.size.width, b.size.height, b.scale, b.rotation)
        }
        log("motion: \(Time.format(time)), preview \(Int(mapping.size.width))×\(Int(mapping.size.height)) pt, \(boxes.count) elementos")
        for b in boxes { log("motion:   \(describe(b))") }
        func current(_ id: UUID) -> String {
            model.canvasBoxes().first { $0.id == id }.map(describe) ?? "?"
        }
        func guides() -> String {
            model.canvasGuides.map { "\($0.axis == .vertical ? "x" : "y")=\(String(format: "%.3f", $0.position)) \($0.kind)" }
                .joined(separator: ", ")
        }

        if let text = boxes.last(where: \.isText) {
            // Clique: seleciona o texto mais acima.
            let start = mapping.view(text.center)
            model.canvasPointerDown(at: start, mapping: mapping)
            model.canvasPointerUp()
            log("motion: clique no texto → seleção \(model.selection == text.id ? "texto" : "outro")")
            await wait(0.5)
            await captureComposite("motion-1a-selecao", model: model, to: dir)

            // Arraste do corpo até perto do centro: encaixa e mostra as guias.
            let near = mapping.view(CGPoint(x: 0.012, y: -0.015))
            model.canvasPointerDown(at: start, mapping: mapping)
            model.canvasPointerMoved(to: start + (near - start), mapping: mapping)
            log("motion: arraste até (0,012; -0,015) → \(current(text.id)); guias: \(guides())")
            await wait(0.6)
            await captureComposite("motion-1b-guias-centro", model: model, to: dir)
            model.canvasPointerUp()

            // Giro de ~43°: encaixa em 45° e mostra a etiqueta.
            if let frame = model.selectionFrame(mapping: mapping) {
                let h = frame.rotationHandle
                let target = frame.center + SelectionFrame.rotate(h - frame.center, by: 43)
                model.canvasPointerDown(at: h, mapping: mapping)
                model.canvasPointerMoved(to: target, mapping: mapping)
                log("motion: giro de 43° → \(current(text.id)); etiqueta \(String(describing: model.canvasRotationLabel))")
                await wait(0.6)
                await captureComposite("motion-1c-giro", model: model, to: dir)
                model.canvasPointerUp()
            }
            // Alça de canto: afastar do centro 1,4× escala 1,4×.
            if let frame = model.selectionFrame(mapping: mapping) {
                let corner = frame.corners[2]
                model.canvasPointerDown(at: corner, mapping: mapping)
                model.canvasPointerMoved(to: frame.center + (corner - frame.center) * 1.4, mapping: mapping)
                log("motion: canto ×1,4 → \(current(text.id))")
                await wait(0.6)
                await captureComposite("motion-1d-escala", model: model, to: dir)
                model.canvasPointerUp()
            }
        }
        // Outro elemento (o mais acima num canto): arrastar até alinhar a borda com o texto.
        let corner = mapping.view(CGPoint(x: -0.8, y: 0.8))
        model.canvasPointerDown(at: corner, mapping: mapping)
        if let id = model.selection, let box = model.canvasBoxes().first(where: { $0.id == id }),
           let text = model.canvasBoxes().last(where: \.isText) {
            let textBounds = CanvasLayout.bounds(of: text, aspect: p.aspectValue)
            let bounds = CanvasLayout.bounds(of: box, aspect: p.aspectValue)
            // Borda esquerda do elemento quase na esquerda do texto.
            let goal = CGPoint(x: box.center.x + (textBounds.minX - bounds.minX) + 0.01, y: box.center.y - 0.1)
            model.canvasPointerMoved(to: corner + (mapping.view(goal) - mapping.view(box.center)), mapping: mapping)
            log("motion: arraste de \(describe(box)) → \(current(id)); guias: \(guides())")
            await wait(0.6)
            await captureComposite("motion-1e-guias-elemento", model: model, to: dir)
        }
        model.canvasPointerUp()
        log("motion: desfazer por gesto: \(model.canUndo ? "sim" : "não")")
        for _ in 0..<10 where model.project != original { model.undo() }
        log("motion: depois de desfazer, igual ao original: \(model.project == original)")
        await wait(0.5)

        // Duplo clique num texto: aba Texto com o foco no campo.
        if let text = model.canvasBoxes().last(where: \.isText) {
            model.selection = nil
            model.inspectorTab = .basic
            await wait(0.3)
            model.canvasPointerDown(at: mapping.view(text.center), mapping: mapping, clickCount: 2)
            model.canvasPointerUp()
            await wait(0.8)
            let responder = playerView.window?.firstResponder.map { String(describing: type(of: $0)) } ?? "nenhum"
            log("motion: duplo clique → aba \(model.inspectorTab.rawValue), foco em \(responder)")
            capture("motion-1f-duplo-clique", to: dir)
        }
        model.selection = nil

        if let textSeg = texts.first {
            model.mutate {
                TimelineEditor.update(&$0, segment: textSeg.id) {
                    $0.animationIn = SegmentAnimation(kind: .cascade)
                    $0.animationLoop = LoopAnimation(kind: .shine)
                }
            }
            await captureAnimationTab(model, segment: textSeg, name: "motion-2-animacao-texto", to: dir)
            model.undo()
        }
        if let main = p.mainTrackIndex, let first = p.tracks[main].segments.first {
            await captureAnimationTab(model, segment: first, name: "motion-3-animacao-clipe", to: dir)
        }
        // Tocar o efeito ao escolher: trecho da entrada e volta ao playhead.
        if let textSeg = texts.first {
            model.seek(to: textSeg.targetEnd + 100_000)
            await wait(0.5)
            let before = model.playhead
            model.mutate { TimelineEditor.update(&$0, segment: textSeg.id) { $0.animationIn = SegmentAnimation(kind: .zoomIn) } }
            let range = AnimationPlayback.entry(model.project.segment(textSeg.id)!, duration: 500_000)
            model.playExcerpt(range)
            await wait(0.4)
            log("motion: trecho \(Time.format(range.lowerBound))–\(Time.format(range.upperBound)), "
                + "a 0,4 s: \(Time.format(model.playhead)) tocando=\(model.isPlaying)")
            await wait(1.5)
            log("motion: depois: \(Time.format(model.playhead)) tocando=\(model.isPlaying) (antes \(Time.format(before)))")
            model.undo()
        }
    }

    /// Resolução do preview: degrau escolhido para o player na tela, quadro de 720 contra o
    /// novo (recorte ampliado do texto) e quadros entregues por segundo em cada degrau, com
    /// vídeo e texto animado (Cascata + Reflexo de luz).
    private static func resolutionScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        let auto = model.previewShortSide
        if let view = playerView() {
            log("resolução: player \(Int(view.bounds.width))×\(Int(view.bounds.height)) pt, escala "
                + "\(view.window?.backingScaleFactor ?? 0), contentsScale \(view.layer?.sublayers?.first?.contentsScale ?? 0), "
                + "degrau automático \(auto), exportação \(model.exportSettings.resolution.label)")
        }
        let texts = model.project.tracks.filter { $0.kind == .text }.flatMap(\.segments)
        guard let text = texts.last else { return }
        model.mutate {
            TimelineEditor.update(&$0, segment: text.id) {
                $0.animationIn = SegmentAnimation(kind: .cascade, duration: 1_000_000)
                $0.animationLoop = LoopAnimation(kind: .shine, intensity: 1)
            }
        }
        let rest = (text.targetStart + text.targetEnd) / 2
        let box = CanvasLayout.boxes(in: model.project, at: rest).first { $0.id == text.id }

        // Quadros de 720, do degrau automático e de 2160, e o recorte ampliado do texto.
        var frames: [(Int, CGImage)] = []
        for side in Array(Set([720, auto, 2160])).sorted() {
            model.setPreviewShortSide(side)
            await waitForPresentation(model, side: side)
            model.seek(to: rest)
            guard let frame = await playerFrame(model) else { continue }
            frames.append((side, frame))
            try? NSBitmapImageRep(cgImage: frame).representation(using: .png, properties: [:])?
                .write(to: dir.appendingPathComponent("0-preview-frame-\(side).png"))
            log("resolução: quadro \(side) → \(frame.width)×\(frame.height)")
        }
        if let box {
            let b = CanvasLayout.bounds(of: box, aspect: model.project.aspectValue)
            // Primeiro terço da caixa do texto, para ver as bordas das letras.
            let region = CGRect(x: b.minX, y: b.minY, width: b.width / 3, height: b.height)
            saveCrops(frames, region: region, height: 420, to: dir.appendingPathComponent("0-preview-texto-comparacao.png"))
        }

        // Desempenho: reprodução de 3 s a partir de 0,3 s antes do texto, em cada degrau.
        for side in [720, 1080, 1440, 2160] {
            model.setPreviewShortSide(side)
            await waitForPresentation(model, side: side)
            model.seek(to: text.targetStart - 300_000)
            await wait(0.8)
            let (fps, advance) = await measurePlayback(model, seconds: 3)
            log(String(format: "resolução: degrau %d → %.1f quadros/s entregues, playhead andou %.2f s em 3 s", side, fps, advance))
        }
        model.setPreviewShortSide(auto)
        model.undo()

        // Debounce: encolher a janela em 6 passos rápidos; o degrau só muda ~300 ms depois.
        guard let window = playerView()?.window else { return }
        let original = window.frame
        for i in 1...6 {
            var f = original
            f.size.width = original.width * (1 - 0.07 * Double(i))
            f.size.height = original.height * (1 - 0.07 * Double(i))
            window.setFrame(f, display: true)
            await wait(0.05)
        }
        let size = playerView()?.bounds.size ?? .zero
        log("resolução: janela encolhida, player \(Int(size.width))×\(Int(size.height)) pt; degrau logo depois: \(model.previewShortSide)")
        await wait(0.6)
        log("resolução: 0,6 s depois: \(model.previewShortSide)")
        window.setFrame(original, display: true)
        await wait(0.6)
        log("resolução: janela restaurada, 0,6 s depois: \(model.previewShortSide)")
        for (pts, scale) in [(CGSize(width: 939, height: 528), 2.0), (CGSize(width: 400, height: 225), 2.0),
                             (CGSize(width: 1600, height: 900), 2.0), (CGSize(width: 939, height: 528), 1.0)] {
            log("resolução: \(Int(pts.width))×\(Int(pts.height)) pt ×\(scale) → "
                + "\(PreviewResolution.shortSide(points: pts, scale: scale))")
        }
    }

    /// Espera o player mostrar o item novo no tamanho pedido.
    private static func waitForPresentation(_ model: EditorModel, side: Int) async {
        for _ in 0..<80 {
            if let size = model.player.currentItem?.presentationSize, Int(min(size.width, size.height)) == side,
               model.player.currentItem?.status == .readyToPlay {
                try? await Task.sleep(for: .seconds(0.5))
                return
            }
            try? await Task.sleep(for: .seconds(0.1))
        }
        log("resolução: o player não chegou a \(side)")
    }

    /// Conta os quadros novos que o compositor entrega durante a reprodução.
    private static func measurePlayback(_ model: EditorModel, seconds: Double) async -> (fps: Double, advance: Double) {
        guard let item = model.player.currentItem else { return (0, 0) }
        let output = AVPlayerItemVideoOutput(pixelBufferAttributes: nil)
        item.add(output)
        defer { item.remove(output) }
        try? await Task.sleep(for: .seconds(0.3))
        let start = model.player.currentTime().seconds
        model.togglePlay()
        let count = await Task.detached(priority: .userInitiated) { () -> Int in
            var frames = 0
            let end = CACurrentMediaTime() + seconds
            while CACurrentMediaTime() < end {
                let t = output.itemTime(forHostTime: CACurrentMediaTime())
                if output.hasNewPixelBuffer(forItemTime: t),
                   output.copyPixelBuffer(forItemTime: t, itemTimeForDisplay: nil) != nil {
                    frames += 1
                }
                usleep(2000)
            }
            return frames
        }.value
        let advance = model.player.currentTime().seconds - start
        if model.isPlaying { model.togglePlay() }
        return (Double(count) / seconds, advance)
    }

    /// Recortes da mesma região (normalizada, y para cima) de cada quadro, ampliados sem
    /// suavização até a mesma altura, lado a lado (do menor para o maior).
    private static func saveCrops(_ frames: [(Int, CGImage)], region: CGRect, height: Int, to url: URL) {
        let crops: [CGImage] = frames.compactMap { _, image in
            let w = CGFloat(image.width), h = CGFloat(image.height)
            let rect = CGRect(x: (region.minX + 1) / 2 * w, y: (1 - region.maxY) / 2 * h,
                              width: region.width / 2 * w, height: region.height / 2 * h).integral
            return image.cropping(to: rect)
        }
        guard !crops.isEmpty else { return }
        let widths = crops.map { Int(Double($0.width) * Double(height) / Double($0.height)) }
        let gap = 16
        guard let ctx = CGContext(data: nil, width: widths.reduce(0, +) + gap * (crops.count - 1), height: height,
                                  bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.interpolationQuality = .none
        ctx.setFillColor(CGColor(gray: 0, alpha: 1))
        ctx.fill(CGRect(x: 0, y: 0, width: ctx.width, height: ctx.height))
        var x = 0
        for (crop, w) in zip(crops, widths) {
            ctx.draw(crop, in: CGRect(x: x, y: 0, width: w, height: height))
            x += w + gap
        }
        if let image = ctx.makeImage() {
            try? NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])?.write(to: url)
        }
        log("resolução: recortes \(frames.map { String($0.0) }.joined(separator: " | ")) → \(url.lastPathComponent)")
    }

    private static func captureAnimationTab(_ model: EditorModel, segment original: Segment, name: String, to dir: URL) async {
        let segment = model.project.segment(original.id) ?? original
        model.selection = segment.id
        model.inspectorTab = .animation
        // Miniaturas geradas em segundo plano.
        try? await Task.sleep(for: .seconds(5))
        await captureInspectorScrolled(name, fractions: [0, 0.5, 1], region: true, to: dir)
        let strips = model.animationStrips.strips
        let animated = strips.filter { $0.value.count > 1 }
        log("\(name): \(strips.count) tiras prontas, \(animated.count) animadas")
        // Folhas de contato (a cada 3 quadros) das tiras dos efeitos novos.
        let chosen = ["cascade", "elastic", "wave", "wordByWord", "maskReveal", "tracking", "decode", "focus"]
            .map { "entry(OpenFrameCore.AnimationKind.\($0))" }
            + LoopKind.allCases.map { "loop(OpenFrameCore.LoopKind.\($0))" }
        var written = Set<String>()
        for (key, frames) in animated {
            let text = String(describing: key)
            guard let match = chosen.compactMap({ $0 }).first(where: { text.contains($0) }),
                  text.contains(segment.text.map { "text: \"\(ThumbSubject.short($0.text))\"" } ?? "") else { continue }
            let file = "\(name)-tira-\(match.split(separator: ".").last!.filter(\.isLetter)).png"
            guard written.insert(file).inserted else { continue }
            let picks = stride(from: 0, to: frames.count, by: 3).map { frames[$0] }
            let w = picks[0].width, h = picks[0].height
            guard let ctx = CGContext(data: nil, width: w * picks.count, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { continue }
            ctx.setFillColor(CGColor(gray: 0.1, alpha: 1))
            ctx.fill(CGRect(x: 0, y: 0, width: w * picks.count, height: h))
            for (i, image) in picks.enumerated() { ctx.draw(image, in: CGRect(x: i * w, y: 0, width: w, height: h)) }
            if let image = ctx.makeImage() {
                try? NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])?
                    .write(to: dir.appendingPathComponent(file))
            }
            log("\(name): \(match) com \(frames.count) quadros → \(file)")
        }
    }

    private static func playerView() -> NSView? {
        func find(_ view: NSView) -> NSView? {
            if view is PlayerLayerView.PlayerNSView { return view }
            return view.subviews.lazy.compactMap(find).first
        }
        return NSApp.windows.lazy.compactMap { $0.contentView.flatMap(find) }.first
    }

    /// Janela com o quadro do player na área dele (a camada de vídeo não entra no
    /// cacheDisplay) e, por cima, o overlay desenhado pela mesma view do app
    /// (`SelectionOverlayDrawing`) com o estado atual do modelo.
    private static func captureComposite(_ name: String, model: EditorModel, to dir: URL) async {
        guard let player = playerView(), let window = player.window,
              let root = window.contentView?.superview ?? window.contentView,
              let rep = root.bitmapImageRepForCachingDisplay(in: root.bounds) else { return }
        let frame = await playerFrame(model)
        root.cacheDisplay(in: root.bounds, to: rep)
        let mapping = CanvasMapping(size: player.bounds.size)
        let renderer = ImageRenderer(content: SelectionOverlayDrawing(
            frame: model.selectionFrame(mapping: mapping),
            guides: model.canvasGuides.map { OverlayGuide($0, mapping: mapping) },
            rotationLabel: model.canvasRotationLabel,
            widthLabel: model.canvasWidthDrag?.label)
            .frame(width: mapping.size.width, height: mapping.size.height))
        let scale = rep.size.width > 0 ? CGFloat(rep.pixelsWide) / rep.size.width : 2
        renderer.scale = scale
        guard let ui = rep.cgImage, let frame, let overlay = renderer.cgImage,
              let ctx = CGContext(data: nil, width: ui.width, height: ui.height, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            capture(name, to: dir)
            return
        }
        let r = player.convert(player.bounds, to: nil)
        let area = CGRect(x: r.minX * scale, y: r.minY * scale, width: r.width * scale, height: r.height * scale)
        ctx.draw(ui, in: CGRect(x: 0, y: 0, width: ui.width, height: ui.height))
        ctx.draw(frame, in: area)
        ctx.draw(overlay, in: area)
        guard let image = ctx.makeImage() else { return }
        try? NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])?
            .write(to: dir.appendingPathComponent("\(name).png"))
    }

    /// Quadro que o player exibe no playhead.
    private static func playerFrame(_ model: EditorModel) async -> CGImage? {
        guard let item = model.player.currentItem else { return nil }
        let output = AVPlayerItemVideoOutput(pixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        ])
        item.add(output)
        defer { item.remove(output) }
        model.seek(to: model.playhead)
        try? await Task.sleep(for: .seconds(1.2))
        guard let buffer = output.copyPixelBuffer(forItemTime: item.currentTime(), itemTimeForDisplay: nil) else { return nil }
        let image = CIImage(cvPixelBuffer: buffer)
        return CIContext().createCGImage(image, from: image.extent)
    }

    private static func dragScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        model.setZoom(120)
        model.seek(to: 0)
        let original = model.project
        func describe(_ id: UUID) -> String {
            let p = model.project
            guard let loc = p.locate(id) else { return "?" }
            let seg = p.tracks[loc.track].segments[loc.index]
            let names = p.tracks[loc.track].segments.map { s in
                s.text?.text ?? p.material(s.materialId)?.name ?? "?"
            }
            return String(format: "índice %d, início %.2f s, fim %.2f s, ordem %@",
                          loc.index, seg.targetStart.seconds, seg.targetEnd.seconds, names.joined(separator: " | "))
        }
        func drag(_ name: String, _ id: UUID, dx: CGFloat) async {
            model.dragMove(id, dx: dx, rows: 0)
            await wait(0.8)
            log("arraste \(name) dx=\(Int(dx)): \(describe(id)), visual \(String(describing: model.dragPreview))")
            capture("arraste-\(name)\(dx > 0 ? "+" : "")\(Int(dx))", to: dir)
            model.endDragMove(id)
            await wait(0.3)
            if model.project != original { model.undo() }
            await wait(0.5)
        }
        let p = model.project
        if let main = p.mainTrackIndex, p.tracks[main].segments.count >= 3 {
            let middle = p.tracks[main].segments[1]
            for dx: CGFloat in [-560, -260, 260, 560] { await drag("principal", middle.id, dx: dx) }
        }
        if let texts = p.tracks.first(where: { $0.kind == .text }), texts.segments.count >= 2 {
            let a = texts.segments[0], b = texts.segments[1]
            let gap = CGFloat((b.targetStart - a.targetStart).seconds * model.zoom)
            await drag("texto-1", a.id, dx: gap * 0.7)
            await drag("texto-2", b.id, dx: -gap * 0.7)
        }
        if let main = p.mainTrackIndex {
            let seg = p.tracks[main].segments[1]
            for (leading, dx) in [(true, CGFloat(90)), (false, CGFloat(-90))] {
                model.dragTrim(seg.id, leading: leading, dx: dx)
                await wait(0.8)
                log("aparar \(leading ? "início" : "fim") dx=\(Int(dx)): \(describe(seg.id)), visual \(String(describing: model.dragPreview))")
                capture("aparar-\(leading ? "inicio" : "fim")", to: dir)
                model.endDragTrim()
                await wait(0.3)
                if model.project != original { model.undo() }
                await wait(0.5)
            }
        }
    }

    /// Áudio na timeline: faixas em alturas diferentes (todas compactas, normais e muito altas; misturadas),
    /// com miniaturas, forma de onda e texto acompanhando a altura; o arraste da borda e o arraste de um
    /// clipe para a faixa de cima, achando a linha pela geometria com alturas diferentes.
    private static func audioScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        // Timeline mais alta durante o cenário (a altura que o usuário deixou volta no fim).
        let defaults = UserDefaults.standard
        let savedTimelineHeight = defaults.object(forKey: "timelineHeight")
        defer { defaults.set(savedTimelineHeight, forKey: "timelineHeight") }
        defaults.set(620.0, forKey: "timelineHeight")
        model.zoomToFit()
        model.seek(to: 1_500_000)
        await wait(1.5)
        func rows() -> [Track] { model.displayTrackOrder().map { model.project.tracks[$0] } }
        func describe(_ step: String) {
            let tracks = rows()
            let layout = model.timelineLayout(tracks)
            let items = tracks.indices.map { i in
                "\(tracks[i].kind)\(tracks[i].isMain ? "*" : "") topo \(Int(layout.top(i))) altura \(Int(layout.height(i)))"
            }
            log("alturas \(step): \(items.joined(separator: "; ")); conteúdo \(Int(layout.contentHeight)) pt")
        }
        describe("padrão")
        capture("audio-1-normal", to: dir)
        for (name, height) in [("compacta", CGFloat(36)), ("muito-alta", 140)] {
            model.setAllRowHeights(height)
            await wait(1)
            describe(name)
            log("menu marca \(model.commonRowHeight.map { "\(Int($0))" } ?? "nenhuma")")
            capture("audio-\(name == "compacta" ? 2 : 3)-\(name)", to: dir)
        }
        // Misturadas: a principal muito alta (miniaturas com o som embaixo), o texto alto, o áudio
        // compacto e a sobreposição normal; com o vídeo principal selecionado.
        model.setAllRowHeights(nil)
        for track in rows() {
            let height: CGFloat? = track.isMain ? 140 : track.kind == .text ? 90 : track.kind == .audio ? 36 : nil
            model.resizeRow(track.id, to: height)
        }
        model.endRowResize()
        if let main = model.project.mainTrackIndex, let first = model.project.tracks[main].segments.first {
            model.selection = first.id
        }
        await wait(1.5)
        describe("misturadas")
        log("menu marca \(model.commonRowHeight.map { "\(Int($0))" } ?? "nenhuma")")
        capture("audio-4-misturadas", to: dir)
        // Áudio alto: a forma de onda ocupa o clipe abaixo do nome.
        if let audio = rows().first(where: { $0.kind == .audio }) {
            model.resizeRow(audio.id, to: 120)
            model.selection = audio.segments.first?.id
            await wait(1)
            capture("audio-5-audio-alto", to: dir)
        }
        // Borda da faixa: limites e duplo clique.
        if let text = rows().first(where: { $0.kind == .text }) {
            model.resizeRow(text.id, to: 10)
            log("borda: pedir 10 → \(Int(model.rowHeight(text)))")
            model.resizeRow(text.id, to: 500)
            log("borda: pedir 500 → \(Int(model.rowHeight(text)))")
            model.resizeRow(text.id, to: nil)
            log("borda: duplo clique → \(Int(model.rowHeight(text))) (padrão)")
            model.resizeRow(text.id, to: 90)
        }
        // Arraste do vídeo da sobreposição para a faixa de cima (texto não aceita) e para baixo
        // (a principal, muito alta): a linha vem da posição y do cursor.
        let tracks = rows()
        let layout = model.timelineLayout(tracks)
        if let row = tracks.firstIndex(where: { $0.kind == .video && !$0.isMain }),
           let seg = tracks[row].segments.first {
            for (name, y) in [("meio da principal", layout.top(row + 1) + layout.height(row + 1) / 2),
                              ("fim da principal", layout.top(row + 2) - 1),
                              ("sobre a régua", CGFloat(10))] {
                let target = (layout.row(atY: y) ?? -1) - row
                let before = model.project
                model.dragMove(seg.id, dx: 0, rows: target)
                await wait(0.6)
                log("arraste para \(name) (y \(Int(y))): linhas \(target), destino \(String(describing: model.dragTarget?.row))")
                if name == "meio da principal" { capture("audio-6-arraste-alturas", to: dir) }
                model.endDragMove(seg.id)
                await wait(0.3)
                if model.project != before { model.undo() }
                await wait(0.3)
            }
        }
    }

    /// Legendas automáticas (rodar com `OPENFRAME_FAKE_TRANSCRIPTION=1` e `OPENFRAME_MODELS_DIR` numa pasta
    /// temporária; `OPENFRAME_SNAPSHOT_SPEECH` = áudio de fala, como o do `say`, posto na timeline antes).
    /// A janela em cada estado, a geração de verdade com as palavras falsas, os três layouts no preview com
    /// o destaque no meio de uma frase, o inspetor "Destaque da palavra" e a edição no painel.
    private static func captionsScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        let env = ProcessInfo.processInfo.environment
        let generation = model.captionGeneration
        model.libraryPanel = .captions
        model.libraryVisible = true

        if let speech = env["OPENFRAME_SNAPSHOT_SPEECH"] {
            let url = URL(fileURLWithPath: speech)
            model.importMedia([url])
            for _ in 0..<50 where !model.project.materials.contains(where: { $0.path == url.path }) { await wait(0.1) }
            if let m = model.project.materials.first(where: { $0.path == url.path }) { model.addToTimeline(m) }
            log("fala na timeline: \(url.lastPathComponent), duração \(Time.format(model.project.duration))")
        }
        await wait(1)
        capture("legendas-0-painel", to: dir)

        // Janela sem modelo instalado.
        model.presentAutoCaptions()
        await wait(1.5)
        capture("legendas-1-opcoes-sem-modelo", to: dir)
        generation.preset = .highlight
        generation.downloads[.turbo] = 0.37
        await wait(1.5)
        capture("legendas-2-destaque-baixando-modelo", to: dir)
        generation.downloads = [:]
        generation.preset = .punch
        await wait(1.5)
        capture("legendas-3-impacto", to: dir)
        generation.includesDownload = true
        generation.setPhase(.downloading(received: 182_000_000, total: WhisperModel.small.byteSize))
        await wait(0.6)
        capture("legendas-4-gerando-baixando", to: dir)
        generation.setPhase(.failed(TranscriptionError.engineUnavailable.localizedDescription))
        await wait(0.6)
        capture("legendas-5-erro", to: dir)
        generation.setPhase(.configuring)

        // Um "modelo" do tamanho certo na pasta temporária conta como instalado (a transcrição é falsa).
        if env["OPENFRAME_MODELS_DIR"] != nil, env["OPENFRAME_FAKE_TRANSCRIPTION"] == "1" {
            let url = generation.store.url(for: .base)
            try? FileManager.default.createDirectory(at: generation.store.directory, withIntermediateDirectories: true)
            FileManager.default.createFile(atPath: url.path, contents: nil)
            if let h = try? FileHandle(forWritingTo: url) {
                try? h.truncate(atOffset: UInt64(WhisperModel.base.byteSize))
                try? h.close()
            }
            generation.refreshInstalled()
        }
        generation.model = .base
        generation.preset = .phrases
        await wait(0.6)
        capture("legendas-6-opcoes-modelo-instalado", to: dir)

        // Geração de verdade (palavras falsas), com captura durante a transcrição.
        var shotProgress = false
        for (index, layout) in CaptionLayout.allCases.enumerated() {
            if !model.showAutoCaptions { model.presentAutoCaptions() }
            generation.preset = CaptionPreset(rawValue: layout.rawValue) ?? .phrases
            generation.replacePrevious = true
            await wait(0.5)
            model.generateCaptions()
            for _ in 0..<300 where generation.phase.isRunning {
                if !shotProgress, case .transcribing(let overall) = generation.phase, overall > 0.3 {
                    capture("legendas-7-gerando-transcrevendo", to: dir)
                    shotProgress = true
                }
                await wait(0.02)
            }
            log("\(layout.rawValue): \(generation.phase)")
            if index == 0 {
                await wait(0.4)
                capture("legendas-8-pronto", to: dir)
            }
            model.showAutoCaptions = false
            generation.setPhase(.configuring)
            await wait(0.8)

            guard let track = model.project.tracks.first(where: { $0.isAutoCaptions }) else { continue }
            log("\(layout.rawValue): \(model.project.tracks.filter(\.isAutoCaptions).count) faixa(s) automática(s), "
                + "\(track.segments.count) legendas; desfazer disponível: \(model.canUndo)")
            // Meio de uma frase: a palavra do meio da 3ª legenda (ou da última).
            let seg = track.segments[min(2, track.segments.count - 1)]
            if let words = seg.text?.words, !words.isEmpty {
                let w = words[words.count / 2]
                model.seek(to: seg.targetStart + (w.start + w.end) / 2)
            }
            model.selection = nil
            await wait(1.2)
            await captureComposite("legendas-9-preview-\(layout.rawValue)", model: model, to: dir)
            if index == 0 {
                capture("legendas-10-painel-automaticas", to: dir)
                model.editingCaption = seg.id
                await wait(0.8)
                capture("legendas-11-painel-editando", to: dir)
                model.editingCaption = nil
            }
            if layout == .highlight {
                model.selection = seg.id
                model.inspectorTab = .text
                await wait(1)
                await captureInspectorScrolled("legendas-12-inspetor-destaque", fractions: [0.35, 0.5], to: dir)
                // Texto editado com outra contagem de palavras: o destaque é ignorado e o inspetor avisa.
                model.setText(seg.id, (seg.text?.text ?? "") + " extra")
                await wait(0.8)
                await captureInspectorScrolled("legendas-13-inspetor-destaque-desligado", fractions: [0.35], to: dir)
                model.undo()
                // Aplicar estilo a todas: muda a cor da selecionada e copia.
                model.mutate { TimelineEditor.update(&$0, segment: seg.id) { $0.text?.style.color = RGBA(0.4, 1, 0.9) } }
                model.applyStyleToAutoCaptions()
                let colors = Set(model.project.tracks.filter(\.isAutoCaptions).flatMap(\.segments).compactMap { $0.text?.style.color })
                log("aplicar estilo a todas: \(colors.count) cor(es) nas legendas automáticas")
                await wait(1)
                capture("legendas-14-aplicar-estilo", to: dir)
                model.undo()
                model.undo()
            }
        }
        // Um desfazer tira a geração inteira.
        let before = model.project.tracks.first(where: { $0.isAutoCaptions })?.segments.count ?? 0
        model.undo()
        let after = model.project.tracks.first(where: { $0.isAutoCaptions })?.segments.count ?? 0
        log("desfazer a última geração: \(before) → \(after) legendas automáticas")
    }

    /// Rodada 3 das legendas (estilos e largura da caixa): a janela com as predefinições agrupadas,
    /// cada predefinição nova no preview no meio da fala, as alças laterais e a mesma legenda antes
    /// e depois de alargar pela alça (o gesto passa pelas mesmas funções do mouse).
    /// Mesmo ambiente do cenário `legendas` (fala, transcrição falsa, pasta de modelos temporária).
    private static func captionStylesScenario(_ model: EditorModel, dir: URL) async {
        func wait(_ s: Double) async { try? await Task.sleep(for: .seconds(s)) }
        let env = ProcessInfo.processInfo.environment
        let generation = model.captionGeneration
        model.libraryPanel = .captions
        model.libraryVisible = true
        if let speech = env["OPENFRAME_SNAPSHOT_SPEECH"] {
            let url = URL(fileURLWithPath: speech)
            model.importMedia([url])
            for _ in 0..<50 where !model.project.materials.contains(where: { $0.path == url.path }) { await wait(0.1) }
            if let m = model.project.materials.first(where: { $0.path == url.path }) { model.addToTimeline(m) }
        }
        if env["OPENFRAME_MODELS_DIR"] != nil, env["OPENFRAME_FAKE_TRANSCRIPTION"] == "1" {
            let url = generation.store.url(for: .base)
            try? FileManager.default.createDirectory(at: generation.store.directory, withIntermediateDirectories: true)
            FileManager.default.createFile(atPath: url.path, contents: nil)
            if let h = try? FileHandle(forWritingTo: url) {
                try? h.truncate(atOffset: UInt64(WhisperModel.base.byteSize))
                try? h.close()
            }
            generation.refreshInstalled()
        }
        generation.model = .base
        await wait(1)

        // A janela: as prévias de todos os cartões precisam de alguns segundos.
        model.presentAutoCaptions()
        generation.preset = .phrases
        await wait(6)
        capture("estilos-1-janela", to: dir)
        generation.preset = .rise
        await wait(1.5)
        capture("estilos-2-janela-surgir-cor", to: dir)
        // Projeto 9:16: as prévias seguem a proporção (estilo e segmentação do núcleo).
        model.mutate { $0.canvas.aspect = .r9x16 }
        await wait(6)
        capture("estilos-2b-janela-9x16", to: dir)
        model.undo()
        await wait(0.5)

        /// Gera com a predefinição e devolve a faixa automática.
        func generate(_ preset: CaptionPreset) async -> Track? {
            if !model.showAutoCaptions { model.presentAutoCaptions() }
            generation.preset = preset
            generation.replacePrevious = true
            await wait(0.3)
            model.generateCaptions()
            for _ in 0..<300 where generation.phase.isRunning { await wait(0.02) }
            log("\(preset.rawValue): \(generation.phase)")
            model.showAutoCaptions = false
            generation.setPhase(.configuring)
            await wait(0.8)
            return model.project.tracks.first(where: { $0.isAutoCaptions })
        }
        /// Playhead no meio da palavra do meio da legenda.
        func seekMid(_ seg: Segment) {
            guard let words = seg.text?.words, !words.isEmpty else { return model.seek(to: seg.targetStart) }
            let w = words[words.count / 2]
            model.seek(to: seg.targetStart + (w.start + w.end) / 2)
        }

        for preset in CaptionPreset.allCases where preset.group != .classic {
            guard let track = await generate(preset) else { continue }
            seekMid(track.segments[min(2, track.segments.count - 1)])
            model.selection = nil
            await wait(1.2)
            await captureComposite("estilos-preview-\(preset.rawValue)", model: model, to: dir)
            let segs = track.segments
            log("\(preset.rawValue): revelação \(segs.first?.text?.highlight?.reveal.rawValue ?? "-"), "
                + "palavras-chave \(segs.map { $0.text?.emphasis?.words ?? [] }), "
                + "\\n no texto: \(segs.contains { $0.text?.text.contains("\n") == true })")
            guard preset == .guided || preset == .editorial else { continue }
            let seg = segs[min(2, segs.count - 1)]
            model.selection = seg.id
            model.inspectorTab = .text
            await wait(1)
            await captureInspectorScrolled("estilos-7-inspetor-\(preset.rawValue)", fractions: [0.3, 0.45, 0.6], model: model, to: dir)
            if preset == .editorial {
                // Ficha: liga a primeira palavra; aplicar a todas copia fonte/escala/cor, não as palavras.
                model.mutate { TimelineEditor.update(&$0, segment: seg.id) {
                    $0.text?.emphasis?.words = [0]
                    $0.text?.emphasis?.color = RGBA(0.4, 1, 0.9)
                    $0.text?.emphasis?.scale = 1.5
                } }
                model.applyStyleToAutoCaptions()
                let after = model.project.tracks.first(where: { $0.isAutoCaptions })?.segments ?? []
                log("aplicar a todas (editorial): cores \(Set(after.compactMap { $0.text?.emphasis?.color }).count), "
                    + "escalas \(Set(after.compactMap { $0.text?.emphasis?.scale })), "
                    + "palavras \(after.map { $0.text?.emphasis?.words ?? [] })")
                model.undo()
                model.undo()
            }
            model.selection = nil
        }

        // Largura da caixa: Frases, a legenda de 2 linhas mais curta.
        guard let track = await generate(.phrases), let player = playerView() else { return }
        let boxes = Dictionary(uniqueKeysWithValues: track.segments.compactMap { seg in
            CanvasLayout.boxes(in: model.project, at: seg.targetStart + 1).first { $0.id == seg.id }.map { (seg.id, $0) }
        })
        for seg in track.segments {
            log(String(format: "largura: legenda de %d caracteres, caixa %.3f × %.3f", seg.text?.text.count ?? 0,
                       boxes[seg.id]?.size.width ?? 0, boxes[seg.id]?.size.height ?? 0))
        }
        let twoLines = track.segments.filter { (boxes[$0.id]?.size.height ?? 0) > 0.2 }
        guard let seg = twoLines.min(by: { ($0.text?.text.count ?? 0) < ($1.text?.text.count ?? 0) }) ?? track.segments.first
        else { return }
        seekMid(seg)
        model.selection = seg.id
        await wait(1.2)
        let mapping = CanvasMapping(size: player.bounds.size)
        func describe() -> String {
            let p = model.project
            let widths = Set(p.tracks.first(where: { $0.isAutoCaptions })?.segments.compactMap { $0.text?.style.maxWidth } ?? [])
            let box = model.canvasBoxes().first { $0.id == seg.id }
            return "maxWidth na faixa \(widths.sorted().map { String(format: "%.3f", $0) }), caixa "
                + (box.map { String(format: "%.3f × %.3f", $0.size.width, $0.size.height) } ?? "?")
                + "; outros textos: \(p.tracks.filter { !$0.isAutoCaptions }.flatMap(\.segments).compactMap { $0.text?.style.maxWidth })"
        }
        guard let frame = model.selectionFrame(mapping: mapping) else { return log("largura: sem moldura") }
        let start = frame.edgeHandle(.right)
        log("largura: alça sob o ponto da direita = \(String(describing: frame.handle(at: start))), "
            + "da esquerda = \(String(describing: frame.handle(at: frame.edgeHandle(.left))))")
        log("largura antes: \(describe())")
        await captureComposite("estilos-3-alcas", model: model, to: dir)

        model.canvasPointerDown(at: start, mapping: mapping)
        // A alça não pode pular: depois de 2 pt, a da moldura fica a 2 pt de onde estava.
        model.canvasPointerMoved(to: start + frame.right * 2, mapping: mapping)
        if let moved = model.selectionFrame(mapping: mapping)?.edgeHandle(.right) {
            log(String(format: "largura: alça depois de 2 pt andou %.2f pt", (moved - start).length))
        }
        let steps = 12
        // Até 108% do quadro (uma legenda de ~42 caracteres em uma linha): a alça anda metade da
        // diferença, na escala do texto.
        let startWidth = seg.text?.style.maxWidth ?? 0.9
        let scale = seg.text?.style.scale ?? 1
        let travel = CGFloat(1.08 - startWidth) * CGFloat(scale) * mapping.size.width / 2 - 2
        for step in 1...steps {
            model.canvasPointerMoved(to: start + frame.right * (travel * CGFloat(step) / CGFloat(steps)), mapping: mapping)
            await wait(0.05)
        }
        await wait(1)
        log("largura arrastando: etiqueta \(model.canvasWidthDrag?.label ?? "-"), \(describe())")
        await captureComposite("estilos-4-arrastando", model: model, to: dir)
        model.canvasPointerUp()
        await wait(1.2)
        log("largura depois: \(describe())")
        await captureComposite("estilos-5-depois-de-alargar", model: model, to: dir)
        model.undo()
        log("largura, um desfazer: \(describe())")
        model.redo()
        await wait(0.5)

        model.inspectorTab = .text
        await wait(1)
        await captureInspectorScrolled("estilos-6-inspetor-largura", fractions: [1], model: model, to: dir)
    }

    private static func logZoom(_ step: String, _ model: EditorModel) {
        let playheadX = model.playhead.seconds * model.zoom - model.timelineScrollX
        let projectWidth = model.duration.seconds * model.zoom
        log(String(format: "zoom: %@ → %.3f pt/s (mín. %.3f), rolagem %.1f, playhead na tela em x=%.1f, "
                   + "projeto %.0f pt de %.0f visíveis", step, model.zoom, model.minZoom,
                   model.timelineScrollX, playheadX, projectWidth, model.timelineViewportWidth))
    }

    private static func log(_ message: String) {
        FileHandle.standardError.write(Data("[snapshot] \(message)\n".utf8))
    }

    /// Rola o inspetor (a rolagem mais à direita da janela) até cada fração e captura.
    /// Com `region`, salva só o inspetor com o título e as abas acima da rolagem. Com `model`,
    /// a janela inteira leva o quadro do player (`captureComposite`), que o cacheDisplay deixa preto.
    private static func captureInspectorScrolled(_ name: String, fractions: [Double] = [0.5, 1],
                                                 region: Bool = false, model: EditorModel? = nil,
                                                 to dir: URL) async {
        guard let window = NSApp.windows.first(where: { $0.isVisible && $0.frame.height > 150 }),
              let root = window.contentView else { return }
        func scrollViews(_ view: NSView) -> [NSScrollView] {
            (view as? NSScrollView).map { [$0] } ?? view.subviews.flatMap(scrollViews)
        }
        guard let scroll = scrollViews(root).filter({ $0.documentView != nil })
            .max(by: { $0.convert($0.bounds, to: nil).minX < $1.convert($1.bounds, to: nil).minX }),
              let document = scroll.documentView else {
            log("inspetor: rolagem não encontrada")
            return
        }
        let travel = max(0, document.frame.height - scroll.contentView.bounds.height)
        log("inspetor \(name): conteúdo \(Int(document.frame.height)) pt, visível \(Int(scroll.contentView.bounds.height)) pt")
        var area = scroll.convert(scroll.bounds, to: root)
        let above: CGFloat = 150
        if root.isFlipped { area.origin.y -= above }
        area.size.height += above
        area = area.insetBy(dx: -8, dy: 0).intersection(root.bounds)
        for (index, fraction) in fractions.enumerated() {
            let y = document.isFlipped ? travel * fraction : travel * (1 - fraction)
            scroll.contentView.scroll(to: NSPoint(x: 0, y: y))
            scroll.reflectScrolledClipView(scroll.contentView)
            try? await Task.sleep(for: .seconds(0.5))
            let file = "\(name)-\(index + 1)"
            if region {
                guard let rep = root.bitmapImageRepForCachingDisplay(in: area) else { continue }
                root.cacheDisplay(in: area, to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent("\(file).png"))
            } else if let model {
                await captureComposite(file, model: model, to: dir)
            } else {
                capture(file, to: dir)
            }
        }
        scroll.contentView.scroll(to: .zero)
        scroll.reflectScrolledClipView(scroll.contentView)
    }

    /// Todas as abas do inspetor do segmento, no topo, no meio e no fim da rolagem.
    private static func captureInspectorTabs(_ model: EditorModel, segment: Segment, prefix: String, to dir: URL) async {
        model.selection = segment.id
        for tab in model.availableTabs(for: segment) {
            model.inspectorTab = tab
            try? await Task.sleep(for: .seconds(tab == .filters ? 2 : 0.8))
            await captureInspectorScrolled("\(prefix)-\(tab.rawValue)", fractions: [0, 0.5, 1], region: true, to: dir)
        }
    }

    private static func capture(_ name: String, to dir: URL) {
        let windows = NSApp.windows.filter { $0.isVisible && $0.frame.height > 150 }
        for (index, window) in windows.enumerated() {
            // A moldura da janela (superview) inclui os botões de fechar/minimizar/zoom.
            guard let view = window.contentView?.superview ?? window.contentView,
                  let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { continue }
            view.cacheDisplay(in: view.bounds, to: rep)
            let suffix = windows.count > 1 ? "-\(index)" : ""
            try? rep.representation(using: .png, properties: [:])?
                .write(to: dir.appendingPathComponent("\(name)\(suffix).png"))
        }
    }
}
