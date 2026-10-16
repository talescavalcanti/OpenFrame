import AVFoundation
import CoreImage

public struct BuiltComposition: @unchecked Sendable {
    public let composition: AVMutableComposition
    public let videoComposition: AVMutableVideoComposition
    public let audioMix: AVMutableAudioMix
    public let box: PlanBox
    public let duration: Micros
}

/// Converte o `Project` numa composição do AVFoundation.
///
/// Cada faixa de vídeo do projeto vira até duas faixas da composição (A/B),
/// para que clipes sobrepostos numa transição sejam decodificados juntos.
/// Uma faixa "de preenchimento" preta cobre a timeline inteira, garantindo que
/// o compositor seja chamado também em trechos só com foto, texto ou vazio.
public enum CompositionBuilder {
    /// Partes do projeto que exigem remontar a composição. Se só efeitos
    /// mudarem, basta atualizar o `PlanBox`.
    public struct StructureKey: Hashable, Sendable {
        struct Item: Hashable, Sendable {
            var track: UUID
            var kind: TrackKind
            var muted: Bool
            var segment: UUID
            var material: UUID?
            var source: TimeRange
            var start: Micros
            var speed: Double
            var volume: Double
            var fadeIn: Micros
            var fadeOut: Micros
            var audioDetached: Bool
        }

        var items: [Item]
        var paths: [String]
    }

    public static func structureKey(_ p: Project) -> StructureKey {
        let items = p.tracks.flatMap { track in
            track.segments.map {
                StructureKey.Item(track: track.id, kind: track.kind, muted: track.muted, segment: $0.id,
                                  material: $0.materialId, source: $0.source, start: $0.targetStart,
                                  speed: $0.speed, volume: $0.volume, fadeIn: $0.fadeIn, fadeOut: $0.fadeOut,
                                  audioDetached: $0.audioDetached)
            }
        }
        return StructureKey(items: items, paths: p.materials.map(\.path))
    }

    public static func build(project: Project, renderSize: CGSize, frameDuration: CMTime,
                             includeAudio: Bool = true) async throws -> BuiltComposition {
        let composition = AVMutableComposition()
        let total = max(project.duration, frameDuration.micros)
        var trackIDs: [UUID: CMPersistentTrackID] = [:]
        var orientations: [UUID: CGImagePropertyOrientation] = [:]
        var assets: [String: AVURLAsset] = [:]

        func asset(_ m: Material) -> AVURLAsset {
            if let a = assets[m.path] { return a }
            let a = AVURLAsset(url: m.url, options: [AVURLAssetPreferPreciseDurationAndTimingKey: true])
            assets[m.path] = a
            return a
        }

        // Preenchimento preto em toda a duração.
        let filler = AVURLAsset(url: try await FillerClip.shared.url())
        if let fillerTrack = try await filler.loadTracks(withMediaType: .video).first,
           let ct = composition.addMutableTrack(withMediaType: .video, preferredTrackID: kCMPersistentTrackID_Invalid) {
            let fillerRange = try await fillerTrack.load(.timeRange)
            try ct.insertTimeRange(fillerRange, of: fillerTrack, at: .zero)
            ct.scaleTimeRange(CMTimeRange(start: .zero, duration: fillerRange.duration), toDuration: total.cmTime)
        }

        // Fades extras para cruzar o áudio durante transições da faixa principal.
        var crossFadeIn: [UUID: Micros] = [:]
        var crossFadeOut: [UUID: Micros] = [:]
        if let main = project.mainTrackIndex {
            let segs = project.tracks[main].segments
            for i in segs.indices.dropLast() where segs[i + 1].targetStart < segs[i].targetEnd {
                let overlap = segs[i].targetEnd - segs[i + 1].targetStart
                crossFadeOut[segs[i].id] = overlap
                crossFadeIn[segs[i + 1].id] = overlap
            }
        }

        struct AudioItem {
            var segment: Segment
            var material: Material
            var lanesKey: UUID
        }
        var audioItems: [AudioItem] = []

        // Vídeo.
        for track in project.tracks where track.kind == .video {
            var lanes: [(track: AVMutableCompositionTrack, end: Micros)] = []
            for seg in track.segments.sorted(by: { $0.targetStart < $1.targetStart }) {
                guard let m = project.material(seg.materialId) else { continue }
                // Com o som extraído para uma faixa de áudio, o clipe não soa (como volume 0).
                if includeAudio, m.kind == .video, m.hasAudio, !track.muted, seg.volume > 0, !seg.audioDetached {
                    audioItems.append(AudioItem(segment: seg, material: m, lanesKey: track.id))
                }
                guard m.kind == .video,
                      let vt = try await asset(m).loadTracks(withMediaType: .video).first
                else { continue }
                let lane = try laneIndex(for: seg, in: &lanes, composition: composition, mediaType: .video)
                try await insert(seg, from: vt, into: lanes[lane].track)
                lanes[lane].end = seg.targetEnd
                trackIDs[seg.id] = lanes[lane].track.trackID
                orientations[seg.id] = orientation(from: try await vt.load(.preferredTransform))
            }
        }

        // Áudio das faixas de áudio. O áudio extraído é um material de áudio que aponta para o vídeo: a trilha de
        // áudio do vídeo toca. Um vídeo com som numa faixa de áudio também toca.
        if includeAudio {
            for track in project.tracks where track.kind == .audio && !track.muted {
                for seg in track.segments where seg.volume > 0 {
                    if let m = project.material(seg.materialId), m.kind == .audio || (m.kind == .video && m.hasAudio) {
                        audioItems.append(AudioItem(segment: seg, material: m, lanesKey: track.id))
                    }
                }
            }
        }

        var audioLanes: [UUID: [(track: AVMutableCompositionTrack, end: Micros)]] = [:]
        var params: [CMPersistentTrackID: AVMutableAudioMixInputParameters] = [:]
        for item in audioItems.sorted(by: { $0.segment.targetStart < $1.segment.targetStart }) {
            guard let at = try await asset(item.material).loadTracks(withMediaType: .audio).first else { continue }
            var lanes = audioLanes[item.lanesKey] ?? []
            let lane = try laneIndex(for: item.segment, in: &lanes, composition: composition, mediaType: .audio)
            let ct = lanes[lane].track
            try await insert(item.segment, from: at, into: ct)
            lanes[lane].end = item.segment.targetEnd
            audioLanes[item.lanesKey] = lanes

            let p = params[ct.trackID] ?? {
                let p = AVMutableAudioMixInputParameters(track: ct)
                p.audioTimePitchAlgorithm = .spectral
                return p
            }()
            applyVolume(item.segment,
                        fadeIn: max(item.segment.fadeIn, crossFadeIn[item.segment.id] ?? 0),
                        fadeOut: max(item.segment.fadeOut, crossFadeOut[item.segment.id] ?? 0),
                        to: p)
            params[ct.trackID] = p
        }

        let box = PlanBox(RenderPlan(project: project, renderSize: renderSize,
                                     trackIDs: trackIDs, orientations: orientations))
        let videoTrackIDs = composition.tracks(withMediaType: .video).map(\.trackID)
        let instruction = OpenFrameInstruction(timeRange: CMTimeRange(start: .zero, duration: composition.duration),
                                             trackIDs: videoTrackIDs, box: box)
        let videoComposition = AVMutableVideoComposition()
        videoComposition.customVideoCompositorClass = OpenFrameCompositor.self
        videoComposition.renderSize = renderSize
        videoComposition.frameDuration = frameDuration
        videoComposition.instructions = [instruction]
        videoComposition.colorPrimaries = AVVideoColorPrimaries_ITU_R_709_2
        videoComposition.colorTransferFunction = AVVideoTransferFunction_ITU_R_709_2
        videoComposition.colorYCbCrMatrix = AVVideoYCbCrMatrix_ITU_R_709_2

        let audioMix = AVMutableAudioMix()
        audioMix.inputParameters = Array(params.values)

        return BuiltComposition(composition: composition, videoComposition: videoComposition,
                                audioMix: audioMix, box: box, duration: total)
    }

    /// Escolhe uma faixa da composição livre a partir do início do segmento,
    /// criando outra (B) quando há sobreposição.
    private static func laneIndex(for seg: Segment,
                                  in lanes: inout [(track: AVMutableCompositionTrack, end: Micros)],
                                  composition: AVMutableComposition, mediaType: AVMediaType) throws -> Int {
        let tolerance: Micros = 2_000
        if let i = lanes.firstIndex(where: { $0.end <= seg.targetStart + tolerance }) { return i }
        guard let ct = composition.addMutableTrack(withMediaType: mediaType, preferredTrackID: kCMPersistentTrackID_Invalid) else {
            throw NSError(domain: "OpenFrame", code: -2, userInfo: [NSLocalizedDescriptionKey: "Não foi possível criar faixa da composição"])
        }
        lanes.append((ct, 0))
        return lanes.count - 1
    }

    private static func insert(_ seg: Segment, from assetTrack: AVAssetTrack, into ct: AVMutableCompositionTrack) async throws {
        let available = try await assetTrack.load(.timeRange)
        let source = seg.source.cmTimeRange.intersection(available)
        guard source.duration > .zero else { return }

        // Sempre inserimos no fim da faixa: um vão vira trecho vazio explícito.
        let trackEnd = ct.segments.isEmpty ? CMTime.zero : ct.timeRange.end
        var at = seg.targetStart.cmTime
        if at > trackEnd {
            ct.insertEmptyTimeRange(CMTimeRange(start: trackEnd, end: at))
        } else {
            at = trackEnd
        }
        try ct.insertTimeRange(source, of: assetTrack, at: at)
        if abs(seg.speed - 1) > 0.0001 {
            ct.scaleTimeRange(CMTimeRange(start: at, duration: source.duration),
                              toDuration: Micros((Double(source.duration.micros) / seg.speed).rounded()).cmTime)
        }
    }

    private static func applyVolume(_ seg: Segment, fadeIn: Micros, fadeOut: Micros, to p: AVMutableAudioMixInputParameters) {
        let volume = Float(min(max(seg.volume, 0), 1))
        let duration = seg.targetDuration
        let fi = min(fadeIn, duration / 2)
        let fo = min(fadeOut, duration / 2)
        let start = seg.targetStart
        if fi > 0 {
            p.setVolumeRamp(fromStartVolume: 0, toEndVolume: volume,
                            timeRange: CMTimeRange(start: start.cmTime, duration: fi.cmTime))
        } else {
            p.setVolume(volume, at: start.cmTime)
        }
        if fo > 0 {
            p.setVolumeRamp(fromStartVolume: volume, toEndVolume: 0,
                            timeRange: CMTimeRange(start: (seg.targetEnd - fo).cmTime, duration: fo.cmTime))
        }
    }

    static func orientation(from t: CGAffineTransform) -> CGImagePropertyOrientation {
        func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.01 }
        if near(t.a, 0), near(t.b, 1), near(t.c, -1), near(t.d, 0) { return .right }
        if near(t.a, 0), near(t.b, -1), near(t.c, 1), near(t.d, 0) { return .left }
        if near(t.a, -1), near(t.b, 0), near(t.c, 0), near(t.d, -1) { return .down }
        return .up
    }
}

/// Gera (uma vez) um clipe preto curto usado como faixa de preenchimento.
public actor FillerClip {
    public static let shared = FillerClip()

    private var cached: URL?

    public func url() async throws -> URL {
        if let cached, FileManager.default.fileExists(atPath: cached.path) { return cached }
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("OpenFrame", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("filler-v1.mov")
        if !FileManager.default.fileExists(atPath: url.path) {
            let tmp = dir.appendingPathComponent("filler-\(UUID().uuidString).mov")
            try await Self.write(to: tmp)
            try? FileManager.default.removeItem(at: url)
            try FileManager.default.moveItem(at: tmp, to: url)
        }
        cached = url
        return url
    }

    private static func write(to url: URL) async throws {
        let side = 64
        let writer = try AVAssetWriter(outputURL: url, fileType: .mov)
        let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: side,
            AVVideoHeightKey: side,
        ])
        input.expectsMediaDataInRealTime = false
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: nil)
        writer.add(input)
        guard writer.startWriting() else { throw writer.error ?? NSError(domain: "OpenFrame", code: -3) }
        writer.startSession(atSourceTime: .zero)
        for frame in 0..<30 {
            while !input.isReadyForMoreMediaData { try await Task.sleep(nanoseconds: 1_000_000) }
            var buffer: CVPixelBuffer?
            CVPixelBufferCreate(nil, side, side, kCVPixelFormatType_32BGRA, nil, &buffer)
            guard let buffer else { throw NSError(domain: "OpenFrame", code: -4) }
            CVPixelBufferLockBaseAddress(buffer, [])
            memset(CVPixelBufferGetBaseAddress(buffer), 0, CVPixelBufferGetDataSize(buffer))
            CVPixelBufferUnlockBaseAddress(buffer, [])
            adaptor.append(buffer, withPresentationTime: CMTime(value: CMTimeValue(frame), timescale: 30))
        }
        input.markAsFinished()
        await writer.finishWriting()
        guard writer.status == .completed else { throw writer.error ?? NSError(domain: "OpenFrame", code: -5) }
    }
}
