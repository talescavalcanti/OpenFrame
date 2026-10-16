import CoreGraphics
import Foundation

// O modelo segue a mesma divisão do draft do CapCut: uma lista de materiais
// (arquivos importados) e uma lista de faixas, cada uma com segmentos que
// apontam para um material e guardam um intervalo de origem e uma posição na
// timeline.

public enum MaterialKind: String, Codable, Sendable {
    case video, photo, audio
}

public struct Material: Codable, Hashable, Sendable, Identifiable {
    public var id = UUID()
    public var kind: MaterialKind
    public var path: String
    public var name: String
    /// Duração do arquivo (0 para fotos).
    public var duration: Micros
    /// Dimensões de exibição, já considerando a rotação do vídeo.
    public var width: Int
    public var height: Int
    public var hasAudio: Bool

    public init(kind: MaterialKind, path: String, name: String, duration: Micros, width: Int, height: Int, hasAudio: Bool) {
        self.kind = kind
        self.path = path
        self.name = name
        self.duration = duration
        self.width = width
        self.height = height
        self.hasAudio = hasAudio
    }

    public var url: URL { URL(fileURLWithPath: path) }
    public var isVisual: Bool { kind != .audio }
}

public enum TrackKind: String, Codable, Sendable {
    case video, audio, text
}

public struct Segment: Codable, Hashable, Sendable, Identifiable {
    public var id = UUID()
    public var materialId: UUID?
    /// Trecho usado do material (para texto, `start` = 0 e `duration` = duração na timeline).
    public var source: TimeRange
    public var targetStart: Micros
    public var speed: Double = 1
    public var volume: Double = 1
    public var fadeIn: Micros = 0
    public var fadeOut: Micros = 0
    public var transform = Transform()
    public var opacity: Double = 1
    public var adjust = Adjust()
    public var filter: FilterSetting?
    public var effects: [VideoEffect] = []
    public var animationIn: SegmentAnimation?
    public var animationOut: SegmentAnimation?
    /// Sem loop, a chave não é gravada (projetos e fixtures antigos ficam iguais).
    public var animationLoop: LoopAnimation?
    public var transition: Transition?
    public var text: TextContent?
    /// O som deste clipe foi extraído para uma faixa de áudio: o clipe continua com a imagem, mas não soa mais.
    /// Só é gravado quando ligado; ausente na leitura = desligado.
    public var audioDetached = false

    public init(materialId: UUID?, source: TimeRange, targetStart: Micros) {
        self.materialId = materialId
        self.source = source
        self.targetStart = targetStart
    }

    private enum CodingKeys: String, CodingKey {
        case id, materialId, source, targetStart, speed, volume, fadeIn, fadeOut, transform, opacity, adjust, filter,
             effects, animationIn, animationOut, animationLoop, transition, text, audioDetached
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id)
        materialId = try c.decodeIfPresent(UUID.self, forKey: .materialId)
        source = try c.decode(TimeRange.self, forKey: .source)
        targetStart = try c.decode(Micros.self, forKey: .targetStart)
        speed = try c.decode(Double.self, forKey: .speed)
        volume = try c.decode(Double.self, forKey: .volume)
        fadeIn = try c.decode(Micros.self, forKey: .fadeIn)
        fadeOut = try c.decode(Micros.self, forKey: .fadeOut)
        transform = try c.decode(Transform.self, forKey: .transform)
        opacity = try c.decode(Double.self, forKey: .opacity)
        adjust = try c.decode(Adjust.self, forKey: .adjust)
        filter = try c.decodeIfPresent(FilterSetting.self, forKey: .filter)
        effects = try c.decode([VideoEffect].self, forKey: .effects)
        animationIn = try c.decodeIfPresent(SegmentAnimation.self, forKey: .animationIn)
        animationOut = try c.decodeIfPresent(SegmentAnimation.self, forKey: .animationOut)
        animationLoop = try c.decodeIfPresent(LoopAnimation.self, forKey: .animationLoop)
        transition = try c.decodeIfPresent(Transition.self, forKey: .transition)
        text = try c.decodeIfPresent(TextContent.self, forKey: .text)
        audioDetached = try c.decodeIfPresent(Bool.self, forKey: .audioDetached) ?? false
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(id, forKey: .id)
        try c.encodeIfPresent(materialId, forKey: .materialId)
        try c.encode(source, forKey: .source)
        try c.encode(targetStart, forKey: .targetStart)
        try c.encode(speed, forKey: .speed)
        try c.encode(volume, forKey: .volume)
        try c.encode(fadeIn, forKey: .fadeIn)
        try c.encode(fadeOut, forKey: .fadeOut)
        try c.encode(transform, forKey: .transform)
        try c.encode(opacity, forKey: .opacity)
        try c.encode(adjust, forKey: .adjust)
        try c.encodeIfPresent(filter, forKey: .filter)
        try c.encode(effects, forKey: .effects)
        try c.encodeIfPresent(animationIn, forKey: .animationIn)
        try c.encodeIfPresent(animationOut, forKey: .animationOut)
        try c.encodeIfPresent(animationLoop, forKey: .animationLoop)
        try c.encodeIfPresent(transition, forKey: .transition)
        try c.encodeIfPresent(text, forKey: .text)
        if audioDetached { try c.encode(true, forKey: .audioDetached) }
    }

    public static func text(_ content: TextContent, at start: Micros, duration: Micros) -> Segment {
        var s = Segment(materialId: nil, source: TimeRange(start: 0, duration: duration), targetStart: start)
        s.text = content
        return s
    }

    public var targetDuration: Micros {
        Micros((Double(source.duration) / max(speed, 0.01)).rounded())
    }

    public var targetEnd: Micros { targetStart + targetDuration }
    public var targetRange: TimeRange { TimeRange(start: targetStart, duration: targetDuration) }
}

public struct Track: Codable, Hashable, Sendable, Identifiable {
    public var id = UUID()
    public var kind: TrackKind
    /// A faixa principal é magnética: os clipes ficam encostados, sem buracos.
    public var isMain: Bool
    public var muted = false
    public var hidden = false
    /// Faixa das legendas automáticas ("substituir anteriores" e o painel Legendas).
    /// Só é gravada quando ligada; ausente na leitura = desligada.
    public var isAutoCaptions = false
    /// Altura da faixa na timeline, em pontos (nil = a padrão do app, `defaultHeight`). Preferência de
    /// visualização gravada no projeto, para a faixa voltar com a mesma altura. Só é gravada quando não é nil;
    /// na leitura, limitada a `heightRange`.
    public var height: Double?
    public var segments: [Segment] = []

    public static let heightRange: ClosedRange<Double> = 32...200
    public static let defaultHeight: Double = 50

    public init(kind: TrackKind, isMain: Bool = false) {
        self.kind = kind
        self.isMain = isMain
    }

    /// Altura limitada a `heightRange` (nil e valores não finitos voltam à padrão).
    public static func clampedHeight(_ height: Double?) -> Double? {
        guard let height, height.isFinite else { return nil }
        return min(max(height, heightRange.lowerBound), heightRange.upperBound)
    }

    private enum CodingKeys: String, CodingKey {
        case id, kind, isMain, muted, hidden, isAutoCaptions, height, segments
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id)
        kind = try c.decode(TrackKind.self, forKey: .kind)
        isMain = try c.decode(Bool.self, forKey: .isMain)
        muted = try c.decode(Bool.self, forKey: .muted)
        hidden = try c.decode(Bool.self, forKey: .hidden)
        isAutoCaptions = try c.decodeIfPresent(Bool.self, forKey: .isAutoCaptions) ?? false
        height = Self.clampedHeight(try c.decodeIfPresent(Double.self, forKey: .height))
        segments = try c.decode([Segment].self, forKey: .segments)
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(id, forKey: .id)
        try c.encode(kind, forKey: .kind)
        try c.encode(isMain, forKey: .isMain)
        try c.encode(muted, forKey: .muted)
        try c.encode(hidden, forKey: .hidden)
        if isAutoCaptions { try c.encode(true, forKey: .isAutoCaptions) }
        try c.encodeIfPresent(height, forKey: .height)
        try c.encode(segments, forKey: .segments)
    }
}

public enum AspectRatio: String, Codable, CaseIterable, Sendable, Identifiable {
    case original, r16x9, r9x16, r1x1, r4x3, r3x4, r21x9, r4x5

    public var id: String { rawValue }

    public var label: String {
        switch self {
        case .original: "Original"
        case .r16x9: "16:9"
        case .r9x16: "9:16"
        case .r1x1: "1:1"
        case .r4x3: "4:3"
        case .r3x4: "3:4"
        case .r21x9: "21:9"
        case .r4x5: "4:5"
        }
    }

    public var value: Double? {
        switch self {
        case .original: nil
        case .r16x9: 16.0 / 9
        case .r9x16: 9.0 / 16
        case .r1x1: 1
        case .r4x3: 4.0 / 3
        case .r3x4: 3.0 / 4
        case .r21x9: 21.0 / 9
        case .r4x5: 4.0 / 5
        }
    }
}

public enum CanvasBackground: Codable, Hashable, Sendable {
    case black
    case color(RGBA)
    /// Fundo desfocado com o próprio vídeo (intensidade 0...1), como o "canvas_blur" do CapCut.
    case blur(Double)
}

public struct Canvas: Codable, Hashable, Sendable {
    public var aspect = AspectRatio.original
    public var background = CanvasBackground.black

    public init() {}
}

public struct Project: Codable, Hashable, Sendable {
    public var formatVersion = 1
    public var name = "Sem título"
    public var canvas = Canvas()
    public var fps: Double = 30
    public var materials: [Material] = []
    public var tracks: [Track]

    public init() {
        tracks = [Track(kind: .video, isMain: true)]
    }

    public var duration: Micros {
        tracks.lazy.flatMap(\.segments).map(\.targetEnd).max() ?? 0
    }

    public func material(_ id: UUID?) -> Material? {
        guard let id else { return nil }
        return materials.first { $0.id == id }
    }

    public var mainTrackIndex: Int? {
        tracks.firstIndex { $0.isMain }
    }

    public func locate(_ segmentId: UUID) -> (track: Int, index: Int)? {
        for (t, track) in tracks.enumerated() {
            if let i = track.segments.firstIndex(where: { $0.id == segmentId }) {
                return (t, i)
            }
        }
        return nil
    }

    public func segment(_ id: UUID) -> Segment? {
        guard let loc = locate(id) else { return nil }
        return tracks[loc.track].segments[loc.index]
    }

    /// Proporção largura/altura do canvas. Em "Original", usa o primeiro
    /// clipe visual da faixa principal, como o CapCut faz.
    public var aspectValue: Double {
        if let v = canvas.aspect.value { return v }
        if let main = mainTrackIndex,
           let first = tracks[main].segments.first,
           let m = material(first.materialId), m.width > 0, m.height > 0 {
            return Double(m.width) / Double(m.height)
        }
        return 16.0 / 9
    }

    /// Tamanho do canvas com o lado menor igual a `shortSide`, em pixels pares.
    public func canvasSize(shortSide: Int) -> CGSize {
        let aspect = aspectValue
        func even(_ v: Double) -> Int { max(2, Int((v / 2).rounded()) * 2) }
        if aspect >= 1 {
            return CGSize(width: even(Double(shortSide) * aspect), height: even(Double(shortSide)))
        }
        return CGSize(width: even(Double(shortSide)), height: even(Double(shortSide) / aspect))
    }
}
