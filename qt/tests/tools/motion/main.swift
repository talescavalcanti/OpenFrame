import CoreGraphics
import CoreImage
import CoreText
import Foundation

// Referência do render do motion e das legendas para a versão Qt. Cada caso guarda o projeto (no formato do
// `.openframe`) e, em alguns instantes, medidas do quadro: tinta (contagem e caixa), centroide e brilho médio, e a
// cor média numa grade 8×4. As fontes do Mac são trocadas pelas OFL que o Qt distribui (registradas a partir de
// `qt/src/core/fonts` e `qt/src/app/fonts`), para a comparação valer em qualquer sistema.
// Uso: qt/tests/tools/gerar-motion.sh

let args = CommandLine.arguments
for dir in args.dropFirst().dropLast() {
    for file in try FileManager.default.contentsOfDirectory(at: URL(fileURLWithPath: dir), includingPropertiesForKeys: nil)
    where file.pathExtension == "ttf" && !file.lastPathComponent.hasPrefix("lucide") {
        CTFontManagerRegisterFontsForURL(file as CFURL, .process, nil)
    }
}

let size = CGSize(width: 480, height: 270)
let gridColumns = 8, gridRows = 4

/// Mesma troca do `FontCatalog::resolve` do Qt quando a família do Mac não existe.
func bundled(_ name: String, _ face: String) -> (String, String) {
    let n = name.lowercased(), f = face.lowercased()
    if ["helvetica neue", "avenir next", "futura", "gill sans"].contains(n) && f.hasPrefix("condensed") {
        return ("Barlow Condensed", f.contains("black") ? "Black" : "Bold")
    }
    if ["menlo", "sf mono", "courier new"].contains(n) { return ("JetBrains Mono", face) }
    if ["didot", "bodoni 72"].contains(n) { return ("Playfair Display", face) }
    if ["helvetica neue", "avenir next", "futura", "gill sans"].contains(n) { return ("Inter", face) }
    return (name, face)
}

func portable(_ p: inout Project) {
    for t in p.tracks.indices {
        for s in p.tracks[t].segments.indices where p.tracks[t].segments[s].text != nil {
            var c = p.tracks[t].segments[s].text!
            (c.style.fontName, c.style.fontFace) = bundled(c.style.fontName, c.style.fontFace)
            if var e = c.emphasis {
                (e.fontName, e.fontFace) = bundled(e.fontName, e.fontFace)
                c.emphasis = e
            }
            p.tracks[t].segments[s].text = c
        }
    }
}

/// Foto do teste: 200×200, metade de cima laranja e de baixo azul (cores que o reflexo e o brilho alteram).
/// O Qt monta a mesma imagem.
let photo: CIImage = {
    var bytes = [UInt8](repeating: 255, count: 200 * 200 * 4)
    for y in 0..<200 {
        for x in 0..<200 {
            let i = (y * 200 + x) * 4
            let (r, g, b): (UInt8, UInt8, UInt8) = y < 100 ? (230, 120, 40) : (40, 90, 200)
            bytes[i] = r; bytes[i + 1] = g; bytes[i + 2] = b
        }
    }
    let provider = CGDataProvider(data: Data(bytes) as CFData)!
    let cg = CGImage(width: 200, height: 200, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 800,
                     space: CGColorSpace(name: CGColorSpace.sRGB)!,
                     bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                     provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
    return CIImage(cgImage: cg)
}()

struct Case {
    let name: String
    var project: Project
    let times: [Micros]
    var stills: [UUID: CIImage] = [:]
}
var cases: [Case] = []

func textStyle() -> TextStyle {
    var s = TextStyle()
    s.fontName = "Inter"
    s.fontFace = "Bold"
    s.bold = false
    s.shadow = false
    s.y = 0
    s.fontSize = 0.15
    return s
}

func textCase(_ name: String, text: String = "Motion 42", times: [Micros], style: (inout TextStyle) -> Void = { _ in },
              _ change: (inout Segment) -> Void) {
    var p = Project()
    p.canvas.aspect = .r16x9
    var s = textStyle()
    style(&s)
    let id = TimelineEditor.addText(TextContent(text: text, style: s), to: &p, at: 0, duration: 3_000_000)
    TimelineEditor.update(&p, segment: id, change)
    cases.append(Case(name: name, project: p, times: times))
}

func photoCase(_ name: String, times: [Micros], _ change: (inout Segment) -> Void) {
    var p = Project()
    p.canvas.aspect = .r16x9
    let m = Material(kind: .photo, path: "", name: "foto", duration: 0, width: 200, height: 200, hasAudio: false)
    let id = TimelineEditor.addMedia(m, to: &p)
    TimelineEditor.update(&p, segment: id) {
        $0.source.duration = 3_000_000
        $0.transform.scale = 0.5
        change(&$0)
    }
    cases.append(Case(name: name, project: p, times: times, stills: [m.id: photo]))
}

// Entrada e saída de cada animação (600 ms), em texto de duas linhas para as unidades por linha.
for kind in AnimationKind.allCases {
    textCase("entrada-\(kind.rawValue)", text: "Motion\ndesign 42", times: [60_000, 180_000, 300_000, 450_000, 600_000]) {
        $0.animationIn = SegmentAnimation(kind: kind, duration: 600_000)
    }
    textCase("saida-\(kind.rawValue)", text: "Motion\ndesign 42", times: [2_450_000, 2_600_000, 2_750_000, 2_900_000]) {
        $0.animationOut = SegmentAnimation(kind: kind, duration: 600_000)
    }
}
// Loops no texto colorido e no branco.
for kind in LoopKind.allCases {
    textCase("loop-\(kind.rawValue)", times: [0, 250_000, 500_000, 580_000, 1_000_000, 1_500_000],
             style: { $0.color = RGBA(0.9, 0.3, 0.2) }) {
        $0.animationLoop = LoopAnimation(kind: kind, intensity: 1)
    }
}
textCase("loop-shine-branco", times: [400_000, 580_000, 800_000]) {
    $0.animationLoop = LoopAnimation(kind: .shine, intensity: 1)
}
// Estilo completo (contorno, sublinhado, fundo, sombra) no caminho por unidade.
textCase("estilo-cascade", text: "Motion\ndesign 42", times: [150_000, 300_000, 1_500_000], style: {
    $0.strokeWidth = 10
    $0.underline = true
    $0.backgroundEnabled = true
    $0.shadow = true
    $0.opacity = 0.8
}) {
    $0.animationIn = SegmentAnimation(kind: .cascade, duration: 600_000)
}
// Foto: foco, zoom (controle) e os loops.
photoCase("foto-entrada-focus", times: [0, 100_000, 250_000, 400_000, 600_000]) {
    $0.animationIn = SegmentAnimation(kind: .focus, duration: 600_000)
}
photoCase("foto-entrada-zoomIn", times: [100_000, 300_000]) {
    $0.animationIn = SegmentAnimation(kind: .zoomIn, duration: 600_000)
}
for kind in LoopKind.allCases {
    photoCase("foto-loop-\(kind.rawValue)", times: [0, 250_000, 500_000, 580_000, 1_000_000]) {
        $0.animationLoop = LoopAnimation(kind: kind, intensity: 1)
    }
}

// Legendas: "MAR SOL CEU", palavras de 0,9 s a cada 1 s.
func captionCase(_ name: String, times: [Micros], text: String = "MAR SOL CEU", style: (inout TextStyle) -> Void = { _ in },
                 emphasis: WordEmphasis? = nil, _ highlight: ((inout WordHighlight) -> Void)?) {
    var p = Project()
    p.canvas.aspect = .r16x9
    var s = textStyle()
    s.fontSize = 0.16
    style(&s)
    var content = TextContent(text: text, style: s)
    content.words = (0..<3).map { CaptionWord(text: "w\($0)", start: Micros($0) * 1_000_000, end: Micros($0 + 1) * 1_000_000 - 100_000) }
    if let highlight {
        var h = WordHighlight()
        h.color = RGBA(0, 0.4, 1)
        h.scale = 1
        highlight(&h)
        content.highlight = h
    }
    content.emphasis = emphasis
    TimelineEditor.addText(content, to: &p, at: 0, duration: 3_400_000)
    cases.append(Case(name: name, project: p, times: times))
}
let captionTimes: [Micros] = [500_000, 1_010_000, 1_100_000, 1_300_000, 1_450_000, 2_500_000, 3_350_000]
for reveal in WordReveal.allCases {
    captionCase("revelacao-\(reveal.rawValue)", times: captionTimes) { $0.reveal = reveal }
}
captionCase("digitacao-fundo", times: captionTimes, style: {
    $0.backgroundEnabled = true
    $0.backgroundColor = RGBA(1, 0, 0)
    $0.backgroundPaddingX = 0.3
}) { $0.reveal = .type }
captionCase("destaque-escala-pilula", times: [500_000, 980_000, 1_040_000, 1_500_000]) {
    $0.color = RGBA(1, 0.85, 0.1)
    $0.scale = 1.3
    $0.backgroundEnabled = true
}
captionCase("destaque-progressivo", times: [500_000, 1_500_000, 2_500_000]) {
    $0.color = RGBA(1, 0.85, 0.1)
    $0.mode = .progressive
}
var serif = WordEmphasis(words: [1])
serif.fontName = "Playfair Display"
serif.color = RGBA(1, 0, 0)
captionCase("enfase", times: [500_000], emphasis: serif, nil)
captionCase("fit-lines", times: [500_000], text: "sozinho\nmontanha\nrio azul", style: {
    $0.fontName = "Barlow Condensed"
    $0.fontFace = "Black"
    $0.fontSize = 0.1
    $0.textCase = .upper
    $0.fitLines = true
    $0.maxWidth = 0.4
    $0.lineSpacing = -0.15
}, nil)
// As 10 predefinições aplicadas pelo CaptionBuilder, com as fontes trocadas.
func spoken(_ text: String) -> [TranscribedWord] {
    var t: Micros = 0
    return text.split(separator: " ").map { token in
        let w = String(token)
        let end = t + 200_000 + Micros(w.count) * 20_000
        defer { t = end + (CaptionBuilder.endsSentence(w) ? 500_000 : 50_000) }
        return TranscribedWord(text: w, start: t, end: end, probability: 0.9)
    }
}
for preset in CaptionPreset.allCases {
    var p = Project()
    p.canvas.aspect = .r16x9
    let cues = CaptionBuilder.cues(from: spoken("Hoje eu vou mostrar para vocês como gerar legendas"), preset: preset)
    CaptionBuilder.apply(cues, preset: preset, accent: RGBA(1, 0.8, 0.1), to: &p, replacing: false)
    portable(&p)
    let seg = p.tracks.last!.segments[0]
    let d = seg.targetEnd - seg.targetStart
    cases.append(Case(name: "predefinicao-\(preset.rawValue)", project: p,
                      times: [1, 4, 7, 10].map { seg.targetStart + d * Micros($0) / 12 }))
}

// Medidas de um quadro (as mesmas do MotionReferenceTests): v = max(r, g, b) em sRGB 8 bits.
func measure(_ image: CIImage) -> [String: Any] {
    let w = Int(size.width), h = Int(size.height)
    var data = [UInt8](repeating: 0, count: w * h * 4)
    CIContext().render(image, toBitmap: &data, rowBytes: w * 4, bounds: CGRect(origin: .zero, size: size),
                       format: .RGBA8, colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!)
    var ink = 0, minX = w, minY = h, maxX = -1, maxY = -1
    var sum = 0.0, sx = 0.0, sy = 0.0
    var cells = [[Double]](repeating: [0, 0, 0], count: gridColumns * gridRows)
    for y in 0..<h {
        for x in 0..<w {
            let i = (y * w + x) * 4
            let r = Int(data[i]), g = Int(data[i + 1]), b = Int(data[i + 2])
            let v = max(r, g, b)
            if v > 64 {
                ink += 1
                minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y)
            }
            sum += Double(v); sx += Double(v) * (Double(x) + 0.5); sy += Double(v) * (Double(y) + 0.5)
            let c = (y * gridRows / h) * gridColumns + x * gridColumns / w
            cells[c][0] += Double(r); cells[c][1] += Double(g); cells[c][2] += Double(b)
        }
    }
    let cellArea = Double(w * h / (gridColumns * gridRows))
    func round1(_ v: Double) -> Double { (v * 10).rounded() / 10 }
    var out: [String: Any] = [
        "ink": ink,
        "mean": round1(sum / Double(w * h)),
        "cells": cells.map { $0.map { round1($0 / cellArea) } },
    ]
    if ink > 0 { out["box"] = [minX, minY, maxX, maxY] }
    if sum > 0 { out["centroid"] = [round1(sx / sum), round1(sy / sum)] }
    return out
}

// OPENFRAME_MOTION_DUMP=<pasta>: grava também cada quadro em PNG (<caso>-<ms>.png), para comparar a olho.
let dump = ProcessInfo.processInfo.environment["OPENFRAME_MOTION_DUMP"].map { URL(fileURLWithPath: $0) }
func write(_ image: CIImage, _ name: String) {
    guard let dump else { return }
    try? FileManager.default.createDirectory(at: dump, withIntermediateDirectories: true)
    try? CIContext().writePNGRepresentation(of: image.cropped(to: CGRect(origin: .zero, size: size)),
                                            to: dump.appendingPathComponent(name + ".png"), format: .RGBA8,
                                            colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!)
}

let renderer = FrameRenderer()
var out: [[String: Any]] = []
for c in cases {
    var plan = RenderPlan(project: c.project, renderSize: size)
    plan.stills = c.stills
    let frames: [[String: Any]] = c.times.map { t in
        let image = renderer.render(plan: plan, time: t) { _ in nil }
        write(image, "\(c.name)-\(t / 1000)")
        var m = measure(image)
        m["t"] = t
        return m
    }
    let projectJSON = try JSONSerialization.jsonObject(with: ProjectStore.encode(c.project))
    out.append(["name": c.name, "project": projectJSON, "photo": !c.stills.isEmpty, "frames": frames])
}
let data = try JSONSerialization.data(withJSONObject: ["size": [size.width, size.height], "grid": [gridColumns, gridRows],
                                                       "cases": out], options: [.sortedKeys])
try data.write(to: URL(fileURLWithPath: args.last!))
print("ok: \(out.count) casos, \(out.reduce(0) { $0 + ($1["frames"] as! [Any]).count }) quadros")
