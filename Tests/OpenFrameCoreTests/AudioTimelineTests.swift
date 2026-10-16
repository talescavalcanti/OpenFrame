import AVFoundation
import Foundation
import Testing
@testable import OpenFrameCore

private func video(_ seconds: Double, name: String = "clip.mov", hasAudio: Bool = true) -> Material {
    Material(kind: .video, path: "/tmp/\(name)", name: name, duration: Time.seconds(seconds),
             width: 1920, height: 1080, hasAudio: hasAudio)
}

private func audioTracks(_ p: Project) -> [Track] {
    p.tracks.filter { $0.kind == .audio }
}

@Suite("Áudio na timeline")
struct AudioTimelineTests {
    @Test("Extrair cria um material de áudio no mesmo arquivo e um segmento no mesmo trecho")
    func extract() throws {
        var p = Project()
        TimelineEditor.addMedia(video(2, name: "a.mov"), to: &p)
        let id = TimelineEditor.addMedia(video(10), to: &p)
        TimelineEditor.trimStart(&p, segment: id, to: Time.seconds(3))
        TimelineEditor.setSpeed(&p, segment: id, speed: 1.5)
        TimelineEditor.update(&p, segment: id) {
            $0.volume = 0.6
            $0.fadeIn = 200_000
            $0.fadeOut = 700_000
        }
        let clip = try #require(p.segment(id))
        #expect(TimelineEditor.canExtractAudio(p, segment: id))

        let audioId = try #require(TimelineEditor.extractAudio(&p, segment: id))
        #expect(p.materials.count == 3)
        let m = try #require(p.materials.last)
        #expect(m.kind == .audio)
        #expect(m.path == "/tmp/clip.mov")
        #expect(m.name == "Áudio de clip.mov")
        #expect(m.duration == Time.seconds(10))
        #expect(m.width == 0 && m.height == 0 && m.hasAudio)

        let tracks = audioTracks(p)
        #expect(tracks.count == 1)
        let sound = try #require(p.segment(audioId))
        #expect(tracks[0].segments.map(\.id) == [audioId])
        #expect(sound.materialId == m.id)
        #expect(sound.targetStart == clip.targetStart)
        #expect(sound.source == clip.source)
        #expect(sound.speed == 1.5)
        #expect(sound.volume == 0.6)
        #expect(sound.fadeIn == 200_000 && sound.fadeOut == 700_000)
        #expect(sound.targetRange == clip.targetRange)
        #expect(!sound.audioDetached)

        let after = try #require(p.segment(id))
        #expect(after.audioDetached)
        #expect(after.source == clip.source && after.targetStart == clip.targetStart)
        #expect(!TimelineEditor.canExtractAudio(p, segment: id))
        #expect(TimelineEditor.extractAudio(&p, segment: id) == nil)
        #expect(p.materials.count == 3)
    }

    @Test("Só vídeo com som pode ter o áudio extraído")
    func canExtractOnlyVideoWithSound() {
        var p = Project()
        let photo = Material(kind: .photo, path: "/tmp/a.png", name: "a.png", duration: 0, width: 800, height: 600, hasAudio: false)
        let song = Material(kind: .audio, path: "/tmp/song.m4a", name: "song.m4a", duration: Time.seconds(5),
                            width: 0, height: 0, hasAudio: true)
        let photoId = TimelineEditor.addMedia(photo, to: &p)
        let mutedId = TimelineEditor.addMedia(video(3, name: "mudo.mov", hasAudio: false), to: &p)
        let songId = TimelineEditor.addMedia(song, to: &p)
        let textId = TimelineEditor.addText(TextContent(text: "Olá"), to: &p, at: 0)
        for id in [photoId, mutedId, songId, textId, UUID()] {
            #expect(!TimelineEditor.canExtractAudio(p, segment: id))
            #expect(TimelineEditor.extractAudio(&p, segment: id) == nil)
        }
        #expect(p.materials.count == 3)
    }

    @Test("O áudio extraído vai para uma faixa de áudio livre no trecho; se não houver, para uma nova")
    func extractToFreeTrack() throws {
        var p = Project()
        let a = TimelineEditor.addMedia(video(4, name: "a.mov"), to: &p)
        let b = TimelineEditor.addMedia(video(4, name: "b.mov"), to: &p)
        // Música ocupando só o trecho do primeiro clipe.
        let song = Material(kind: .audio, path: "/tmp/song.m4a", name: "song.m4a", duration: Time.seconds(3),
                            width: 0, height: 0, hasAudio: true)
        TimelineEditor.addMedia(song, to: &p, at: Time.seconds(1))
        #expect(audioTracks(p).count == 1)

        let soundA = try #require(TimelineEditor.extractAudio(&p, segment: a))
        #expect(audioTracks(p).count == 2)
        #expect(p.locate(soundA)?.track == p.tracks.count - 1)

        // O segundo clipe (4–8 s) cabe na primeira faixa de áudio, que só está ocupada até 4 s.
        let soundB = try #require(TimelineEditor.extractAudio(&p, segment: b))
        #expect(audioTracks(p).count == 2)
        #expect(audioTracks(p)[0].segments.contains { $0.id == soundB })
    }

    @Test("Extrair as duas metades de um vídeo reaproveita o material de áudio")
    func extractReusesMaterial() throws {
        var p = Project()
        let id = TimelineEditor.addMedia(video(6), to: &p)
        let right = try #require(TimelineEditor.split(&p, segment: id, at: Time.seconds(2)))
        let left = try #require(TimelineEditor.extractAudio(&p, segment: id))
        let rightSound = try #require(TimelineEditor.extractAudio(&p, segment: right))
        #expect(p.materials.count == 2)
        #expect(p.segment(left)?.materialId == p.segment(rightSound)?.materialId)
        #expect(p.segment(rightSound)?.source == TimeRange(start: Time.seconds(2), duration: Time.seconds(4)))
    }

    @Test("O cruzamento de uma transição vira fade no áudio extraído")
    func extractKeepsTransitionCrossfade() throws {
        var p = Project()
        let a = TimelineEditor.addMedia(video(4, name: "a.mov"), to: &p)
        let b = TimelineEditor.addMedia(video(4, name: "b.mov"), to: &p)
        TimelineEditor.update(&p, segment: a) {
            $0.transition = Transition(kind: .dissolve, duration: 600_000)
            $0.fadeOut = 100_000
        }
        TimelineEditor.update(&p, segment: b) { $0.fadeIn = 900_000 }
        let soundA = try #require(TimelineEditor.extractAudio(&p, segment: a))
        let soundB = try #require(TimelineEditor.extractAudio(&p, segment: b))
        #expect(p.segment(soundA)?.fadeOut == 600_000)
        #expect(p.segment(soundA)?.fadeIn == 0)
        #expect(p.segment(soundB)?.fadeIn == 900_000)
        #expect(p.segment(soundB)?.targetStart == p.segment(b)?.targetStart)
    }

    @Test("Dividir, duplicar, aparar e mover mantêm o som extraído; apagar o áudio não devolve o som")
    func editsKeepDetached() throws {
        var p = Project()
        let id = TimelineEditor.addMedia(video(8), to: &p)
        let sound = try #require(TimelineEditor.extractAudio(&p, segment: id))
        let right = try #require(TimelineEditor.split(&p, segment: id, at: Time.seconds(3)))
        let copy = try #require(TimelineEditor.duplicate(&p, segment: right))
        TimelineEditor.trimStart(&p, segment: id, to: Time.seconds(1))
        TimelineEditor.trimEnd(&p, segment: copy, to: p.segment(copy)!.targetEnd - Time.seconds(1))
        for seg in p.tracks[p.mainTrackIndex!].segments {
            #expect(seg.audioDetached)
        }
        // O áudio extraído é um clipe de áudio comum.
        let overlay = TimelineEditor.addOverlay(video(2, name: "pip.mov"), to: &p, at: 0)
        TimelineEditor.extractAudio(&p, segment: overlay)
        TimelineEditor.move(&p, segment: overlay, toStart: Time.seconds(4))
        TimelineEditor.moveToTrack(&p, segment: overlay, track: p.mainTrackIndex!)
        #expect(p.locate(overlay)?.track == p.mainTrackIndex)
        #expect(p.segment(overlay)?.audioDetached == true)

        TimelineEditor.trimEnd(&p, segment: sound, to: Time.seconds(2))
        #expect(p.segment(sound)?.targetEnd == Time.seconds(2))
        TimelineEditor.delete(&p, segment: sound)
        #expect(p.segment(id)?.audioDetached == true)
        #expect(!TimelineEditor.canExtractAudio(p, segment: id))
    }

    @Test("Gravação entra numa faixa de áudio livre no trecho")
    func addRecording() throws {
        var p = Project()
        TimelineEditor.addMedia(video(10), to: &p)
        let take = Material(kind: .audio, path: "/tmp/Gravação 1.m4a", name: "Gravação 1.m4a",
                            duration: Time.seconds(3), width: 0, height: 0, hasAudio: true)
        let first = TimelineEditor.addRecording(take, to: &p, at: Time.seconds(2))
        #expect(p.materials.count == 2)
        #expect(p.segment(first)?.targetRange == TimeRange(start: Time.seconds(2), duration: Time.seconds(3)))
        #expect(audioTracks(p).count == 1)

        // Sobre a primeira: faixa nova. Depois dela: a primeira serve.
        let second = Material(kind: .audio, path: "/tmp/Gravação 2.m4a", name: "Gravação 2.m4a",
                              duration: Time.seconds(2), width: 0, height: 0, hasAudio: true)
        let over = TimelineEditor.addRecording(second, to: &p, at: Time.seconds(4))
        #expect(audioTracks(p).count == 2)
        let later = TimelineEditor.addRecording(second, to: &p, at: Time.seconds(6))
        #expect(audioTracks(p).count == 2)
        #expect(p.locate(later)?.track == p.locate(first)?.track)
        #expect(p.locate(over)?.track != p.locate(first)?.track)
        #expect(p.materials.count == 3)
        // Tempo negativo vai para o início.
        let start = TimelineEditor.addRecording(take, to: &p, at: -5)
        #expect(p.segment(start)?.targetStart == 0)
    }

    @Test("Altura das faixas: limitada e nil volta à padrão")
    func trackHeight() {
        var p = Project()
        TimelineEditor.addText(TextContent(text: "Olá"), to: &p, at: 0)
        let main = p.tracks[0].id
        #expect(p.tracks[0].height == nil)
        TimelineEditor.setTrackHeight(&p, track: main, height: 90)
        #expect(p.tracks[0].height == 90)
        TimelineEditor.setTrackHeight(&p, track: main, height: 10)
        #expect(p.tracks[0].height == Track.heightRange.lowerBound)
        TimelineEditor.setTrackHeight(&p, track: main, height: 1000)
        #expect(p.tracks[0].height == Track.heightRange.upperBound)
        TimelineEditor.setTrackHeight(&p, track: main, height: .nan)
        #expect(p.tracks[0].height == nil)
        TimelineEditor.setTrackHeight(&p, track: main, height: 140)
        TimelineEditor.setTrackHeight(&p, track: main, height: nil)
        #expect(p.tracks[0].height == nil)
        #expect(p.tracks[1].height == nil)
        TimelineEditor.setTrackHeight(&p, track: UUID(), height: 80)

        TimelineEditor.setAllTrackHeights(&p, height: 36)
        #expect(p.tracks.map(\.height) == [36, 36])
        TimelineEditor.setAllTrackHeights(&p, height: 500)
        #expect(p.tracks.map(\.height) == [200, 200])
        TimelineEditor.setAllTrackHeights(&p, height: nil)
        #expect(p.tracks.map(\.height) == [nil, nil])
        #expect(Track.defaultHeight == 50)
    }

    @Test("JSON: projeto antigo abre igual e os campos novos só são gravados fora do padrão")
    func json() throws {
        var p = Project()
        let id = TimelineEditor.addMedia(video(4), to: &p)
        let plain = try ProjectStore.encode(p)
        let text = String(decoding: plain, as: UTF8.self)
        #expect(!text.contains("audioDetached"))
        let tracks = try #require((try JSONSerialization.jsonObject(with: plain) as? [String: Any])?["tracks"] as? [[String: Any]])
        #expect(tracks.allSatisfy { $0["height"] == nil })
        #expect(try ProjectStore.decode(plain) == p)

        TimelineEditor.extractAudio(&p, segment: id)
        TimelineEditor.setTrackHeight(&p, track: p.tracks[0].id, height: 120)
        let data = try ProjectStore.encode(p)
        let back = try ProjectStore.decode(data)
        #expect(back == p)
        #expect(back.segment(id)?.audioDetached == true)
        #expect(back.tracks[0].height == 120)
        #expect(back.tracks[1].height == nil)

        // Altura fora da faixa (outra versão, arquivo editado à mão) é limitada na leitura.
        var object = try #require(try JSONSerialization.jsonObject(with: data) as? [String: Any])
        var list = try #require(object["tracks"] as? [[String: Any]])
        list[0]["height"] = 5
        list[1]["height"] = 900.5
        object["tracks"] = list
        let edited = try ProjectStore.decode(JSONSerialization.data(withJSONObject: object))
        #expect(edited.tracks.map(\.height) == [32, 200])
    }

    // MARK: - Som

    /// Mistura do projeto como a exportação ouve (a composição e o `audioMix` da exportação).
    private func mix(_ p: Project) async throws -> [Float] {
        try await AudioMixdown.samples(project: p, range: TimeRange(start: 0, duration: p.duration)) { _ in } ?? []
    }

    /// Envelope do som: RMS em janelas de 20 ms (320 amostras a 16 kHz).
    private func envelope(_ x: [Float]) -> [Float] {
        stride(from: 0, to: x.count, by: 320).map { k in
            let w = x[k..<min(k + 320, x.count)]
            return (w.reduce(0) { $0 + $1 * $1 } / Float(w.count)).squareRoot()
        }
    }

    /// Maior diferença de envelope e a maior diferença amostra a amostra.
    private func compare(_ a: [Float], _ b: [Float]) -> (envelope: Float, samples: Float) {
        let ea = envelope(a), eb = envelope(b)
        var env: Float = 0
        for i in 0..<min(ea.count, eb.count) { env = max(env, abs(ea[i] - eb[i])) }
        var worst: Float = 0
        for i in 0..<min(a.count, b.count) { worst = max(worst, abs(a[i] - b[i])) }
        return (env, worst)
    }

    @Test("O som não muda ao extrair (velocidade, volume, fades e transição)")
    func soundUnchanged() async throws {
        let url = try await TestMedia.makeVideo(seconds: 3)
        let clip = try await MediaProbe.probe(url)
        var p = Project()
        let a = TimelineEditor.addMedia(clip, to: &p)
        let b = TimelineEditor.addMedia(clip, to: &p)
        TimelineEditor.update(&p, segment: a) {
            $0.fadeIn = 300_000
            $0.volume = 0.7
            $0.transition = Transition(kind: .dissolve, duration: 500_000)
        }
        TimelineEditor.setSpeed(&p, segment: b, speed: 2)
        TimelineEditor.update(&p, segment: b) { $0.fadeOut = 400_000 }

        let before = try await mix(p)
        var q = p
        let soundA = try #require(TimelineEditor.extractAudio(&q, segment: a))
        let soundB = try #require(TimelineEditor.extractAudio(&q, segment: b))
        let after = try await mix(q)
        #expect(abs(before.count - after.count) <= 1)
        #expect(envelope(before).max() ?? 0 > 0.1)
        // O time-pitch do AVFoundation não é determinístico no trecho acelerado: a mesma mistura feita duas vezes
        // já difere em até ~0,04 amostra a amostra (medido) no fade do clipe 2×. Por isso a comparação é pelo
        // envelope (20 ms), e amostra a amostra só até o clipe acelerado começar.
        let (env, _) = compare(before, after)
        #expect(env < 0.01, "envelope difere \(env)")
        let normal = Int(Double(try #require(q.segment(b)).targetStart) / 1e6 * AudioMixdown.sampleRate)
        let (_, worst) = compare(Array(before.prefix(normal)), Array(after.prefix(normal)))
        #expect(worst < 1e-4, "amostras diferem \(worst)")

        // Sem o áudio extraído, o vídeo destacado não soa.
        TimelineEditor.delete(&q, segment: soundA)
        TimelineEditor.delete(&q, segment: soundB)
        let silent = try await mix(q)
        #expect(silent.allSatisfy { abs($0) < 1e-4 })
    }
}
