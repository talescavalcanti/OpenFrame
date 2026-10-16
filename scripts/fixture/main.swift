import Foundation

// Gera um projeto que usa todos os campos do formato .openframe. A versão
// multiplataforma (qt/) lê este arquivo nos testes e precisa gravá-lo de volta
// com o mesmo conteúdo: é o que garante que os dois apps abrem os mesmos projetos.
// Uso: scripts/make-fixture.sh  (grava qt/tests/fixtures/completo.openframe e os outros ao lado)

func uuid(_ n: Int) -> UUID { UUID(uuidString: String(format: "00000000-0000-4000-8000-%012d", n))! }

var p = Project()
p.name = "Projeto completo"
p.fps = 29.97
p.canvas.aspect = .r9x16
p.canvas.background = .color(RGBA(0.1, 0.2, 0.3, 1))
p.tracks[0].id = uuid(1)

var video = Material(kind: .video, path: "/media/clipe.mov", name: "clipe.mov",
                     duration: 12_345_678, width: 1080, height: 1920, hasAudio: true)
video.id = uuid(10)
var photo = Material(kind: .photo, path: "/media/foto.jpg", name: "foto.jpg",
                     duration: 0, width: 4032, height: 3024, hasAudio: false)
photo.id = uuid(11)
var audio = Material(kind: .audio, path: "/media/música.m4a", name: "música.m4a",
                     duration: 60_000_000, width: 0, height: 0, hasAudio: true)
audio.id = uuid(12)
p.materials = [video, photo, audio]

var a = Segment(materialId: video.id, source: TimeRange(start: 1_000_000, duration: 4_000_000), targetStart: 0)
a.id = uuid(20)
a.speed = 2
a.volume = 0.8
a.fadeIn = 250_000
a.fadeOut = 500_000
a.transform.scale = 1.25
a.transform.x = -0.1
a.transform.y = 0.2
a.transform.rotation = 15
a.opacity = 0.9
a.adjust.brightness = 0.1
a.adjust.contrast = -0.2
a.adjust.saturation = 0.3
a.adjust.exposure = 0.05
a.adjust.temperature = 0.4
a.adjust.tint = -0.1
a.adjust.highlights = 0.2
a.adjust.shadows = -0.3
a.adjust.hue = 0.15
a.adjust.sharpen = 0.5
a.adjust.vignette = 0.6
a.adjust.fade = 0.25
a.adjust.grain = 0.35
a.filter = FilterSetting(preset: .cinematic, intensity: 0.75)
var e1 = VideoEffect(kind: .rgbSplit, intensity: 0.4); e1.id = uuid(30)
var e2 = VideoEffect(kind: .shake, intensity: 0.7); e2.id = uuid(31)
a.effects = [e1, e2]
a.animationIn = SegmentAnimation(kind: .zoomIn, duration: 400_000)
a.animationOut = SegmentAnimation(kind: .slideLeft, duration: 600_000)
a.transition = Transition(kind: .dissolve, duration: 500_000)

var b = Segment(materialId: photo.id, source: TimeRange(start: 0, duration: 3_000_000), targetStart: 0)
b.id = uuid(21)
p.tracks[0].segments = [a, b]
TimelineEditor.relayoutMain(&p)

var music = Track(kind: .audio)
music.id = uuid(2)
music.muted = true
var m = Segment(materialId: audio.id, source: TimeRange(start: 0, duration: 5_000_000), targetStart: 500_000)
m.id = uuid(22)
music.segments = [m]

var titles = Track(kind: .text)
titles.id = uuid(3)
titles.hidden = true
var style = TextStyle()
style.fontName = "Inter"
style.fontFace = "Semibold"
style.fontSize = 0.05
style.bold = false
style.italic = true
style.underline = true
style.textCase = .upper
style.color = RGBA(1, 0.85, 0.1, 1)
style.opacity = 0.95
style.strokeColor = RGBA(0, 0, 0, 0.5)
style.strokeWidth = 8
style.letterSpacing = 0.05
style.lineSpacing = -0.1
style.backgroundEnabled = true
style.backgroundColor = RGBA(0.2, 0.2, 0.2, 0.7)
style.backgroundRadius = 0.3
style.backgroundPaddingX = 0.4
style.backgroundPaddingY = 0.2
style.shadow = false
style.shadowColor = RGBA(0, 0, 0, 0.4)
style.shadowDistance = 0.06
style.shadowAngle = -45
style.shadowBlur = 0.2
style.alignment = .left
style.x = 0.1
style.y = 0.5
style.scale = 1.5
style.rotation = -5
style.maxWidth = 0.8
var t = Segment.text(TextContent(text: "Olá, mundo!\nSegunda linha 🎬", style: style), at: 1_000_000, duration: 2_000_000)
t.id = uuid(23)
t.animationIn = SegmentAnimation(kind: .typewriter, duration: 1_000_000)
titles.segments = [t]

var overlays = Track(kind: .video)
overlays.id = uuid(4)
var o = Segment(materialId: photo.id, source: TimeRange(start: 0, duration: 2_000_000), targetStart: 750_000)
o.id = uuid(24)
o.transform.scale = 0.5
overlays.segments = [o]

p.tracks += [overlays, music, titles]

let out = URL(fileURLWithPath: CommandLine.arguments[1])
try ProjectStore.save(p, to: out)

// Variações do fundo do canvas (o enum com valor associado tem codificação própria).
for (name, bg) in [("fundo-preto", CanvasBackground.black), ("fundo-desfoque", .blur(0.6))] {
    var q = Project()
    q.tracks[0].id = uuid(1)
    q.canvas.background = bg
    try ProjectStore.save(q, to: out.deletingLastPathComponent().appendingPathComponent("\(name).openframe"))
}
// Texto e motion: todas as animações de entrada e saída e os quatro loops, em texto e em vídeo.
do {
    var q = Project()
    q.name = "Motion"
    q.tracks[0].id = uuid(1)
    q.materials = [video]
    var clip = Segment(materialId: video.id, source: TimeRange(start: 0, duration: 6_000_000), targetStart: 0)
    clip.id = uuid(100)
    clip.animationIn = SegmentAnimation(kind: .focus, duration: 300_000)
    clip.animationLoop = LoopAnimation(kind: .shine, period: 1_500_000, intensity: 0.8)
    q.tracks[0].segments = [clip]
    var texts = Track(kind: .text)
    texts.id = uuid(101)
    let kinds = AnimationKind.allCases
    let loops = LoopKind.allCases
    for (k, kind) in kinds.enumerated() {
        var seg = Segment.text(TextContent(text: "Animação \(kind.rawValue)"),
                               at: Micros(k) * 1_000_000, duration: 900_000)
        seg.id = uuid(200 + k)
        seg.animationIn = SegmentAnimation(kind: kind, duration: 400_000)
        seg.animationOut = SegmentAnimation(kind: kinds[(k + 5) % kinds.count], duration: 250_000)
        if k < loops.count {
            seg.animationLoop = LoopAnimation(kind: loops[k], period: Micros(1_000_000 + 250_000 * k),
                                              intensity: 0.25 * Double(k + 1))
        }
        texts.segments.append(seg)
    }
    q.tracks.append(texts)
    try ProjectStore.save(q, to: out.deletingLastPathComponent().appendingPathComponent("motion.openframe"))
}

// Legendas: a faixa automática gerada pelo CaptionBuilder (palavras, destaque com as revelações, palavra-chave,
// Empilhado com fitLines) e textos feitos à mão com os campos raros (U+00A0, maxWidth > 1, ênfase sem cor).
do {
    var q = Project()
    q.name = "Legendas"
    q.canvas.aspect = .r16x9
    q.tracks[0].id = uuid(1)
    let speech = "Olá, pessoal! Hoje eu vou mostrar como gerar legendas automáticas no editor, sem internet. " +
        "Primeiro escolha o idioma; depois escolha o estilo que combina com o seu vídeo."
    var spoken: [TranscribedWord] = []
    var t: Micros = 200_000
    for word in speech.split(separator: " ") {
        let end = t + 180_000 + 40_000 * Micros(min(word.count, 10))
        spoken.append(TranscribedWord(text: String(word), start: t, end: end))
        t = end + (word.hasSuffix(".") || word.hasSuffix("!") ? 500_000 : 80_000)
    }
    var n = 300
    for (preset, accent) in [(CaptionPreset.highlight, RGBA(0, 0.8, 1)), (.rise, nil), (.guided, RGBA(1, 0.5, 0)),
                             (.fill, nil), (.typing, RGBA(0.2, 1, 0.4)), (.editorial, nil), (.stacked, RGBA(1, 0.2, 0.3))] {
        let cues = CaptionBuilder.cues(from: spoken, preset: preset, aspect: q.aspectValue)
        // Cada visual no seu trecho da timeline (substituir só troca o que toca o trecho).
        let offset = Micros(n - 300) / 40 * 20_000_000
        let shifted = cues.map { cue -> CaptionCue in
            var c = cue
            c.start += offset
            c.end += offset
            c.words = c.words.map { TranscribedWord(text: $0.text, start: $0.start + offset, end: $0.end + offset) }
            return c
        }
        CaptionBuilder.apply(shifted, preset: preset, accent: accent, to: &q, replacing: true)
        n += 40
    }
    // Ids fixos: o arquivo não muda a cada geração.
    var id = 300
    for t in q.tracks.indices where t > 0 {
        q.tracks[t].id = uuid(id)
        id += 1
        for k in q.tracks[t].segments.indices {
            q.tracks[t].segments[k].id = uuid(id)
            id += 1
        }
    }
    var manual = Track(kind: .text)
    manual.id = uuid(900)
    var style = TextStyle()
    style.maxWidth = 2.5
    style.balanceLines = true
    var content = TextContent(text: "de\u{00A0}manhã cedo\ncom\u{00A0}o sol", style: style)
    content.words = [CaptionWord(text: "de", start: 0, end: 200_000), CaptionWord(text: "manhã", start: 250_000, end: 600_000),
                     CaptionWord(text: "cedo", start: 650_000, end: 900_000), CaptionWord(text: "com", start: 1_000_000, end: 1_200_000),
                     CaptionWord(text: "o", start: 1_250_000, end: 1_300_000), CaptionWord(text: "sol", start: 1_350_000, end: 1_700_000)]
    var h = WordHighlight()
    h.backgroundEnabled = true
    h.mode = .progressive
    h.reveal = .dim
    h.dimOpacity = 0.5
    content.highlight = h
    content.emphasis = WordEmphasis(words: [1, 5])
    var seg = Segment.text(content, at: 0, duration: 2_000_000)
    seg.id = uuid(901)
    manual.segments = [seg]
    q.tracks.append(manual)
    try ProjectStore.save(q, to: out.deletingLastPathComponent().appendingPathComponent("legendas.openframe"))
}

// Áudio na timeline: um clipe com o som extraído (material de áudio no mesmo arquivo do vídeo), uma gravação e
// faixas com altura própria (e uma na padrão, sem a chave).
do {
    var q = Project()
    q.name = "Áudio"
    q.tracks[0].id = uuid(1)
    var take = Material(kind: .audio, path: "/media/Gravações/Gravação 1.m4a", name: "Gravação 1.m4a",
                        duration: 2_500_000, width: 0, height: 0, hasAudio: true)
    take.id = uuid(1001)
    let first = TimelineEditor.addMedia(video, to: &q)
    TimelineEditor.addMedia(video, to: &q)
    TimelineEditor.update(&q, segment: first) {
        $0.speed = 1.5
        $0.volume = 0.8
        $0.fadeIn = 200_000
        $0.transition = Transition(kind: .dissolve, duration: 400_000)
    }
    TimelineEditor.extractAudio(&q, segment: first)
    TimelineEditor.addRecording(take, to: &q, at: 1_000_000)
    TimelineEditor.setAllTrackHeights(&q, height: 90)
    TimelineEditor.setTrackHeight(&q, track: q.tracks[1].id, height: 36)
    TimelineEditor.setTrackHeight(&q, track: q.tracks[2].id, height: nil)
    // Ids fixos: o arquivo não muda a cada geração.
    if let k = q.materials.firstIndex(where: { $0.kind == .audio && $0.path == video.path }) {
        let old = q.materials[k].id
        q.materials[k].id = uuid(1000)
        for t in q.tracks.indices {
            for s in q.tracks[t].segments.indices where q.tracks[t].segments[s].materialId == old {
                q.tracks[t].segments[s].materialId = uuid(1000)
            }
        }
    }
    var id = 1100
    for t in q.tracks.indices {
        if t > 0 { q.tracks[t].id = uuid(id); id += 1 }
        for k in q.tracks[t].segments.indices {
            q.tracks[t].segments[k].id = uuid(id)
            id += 1
        }
    }
    try ProjectStore.save(q, to: out.deletingLastPathComponent().appendingPathComponent("audio.openframe"))
}

print("ok: \(out.path)")
