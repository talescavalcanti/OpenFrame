import AVFoundation
import Foundation
import Testing
@testable import OpenFrameCore

/// Seno de 440 Hz com amplitude 0,5, mono.
private func sine(seconds: Double, sampleRate: Double) -> [Float] {
    (0..<Int(seconds * sampleRate)).map { Float(0.5 * sin(2 * .pi * 440 * Double($0) / sampleRate)) }
}

/// Alimenta o gravador em blocos, como o microfone.
private func feed(_ writer: RecordingWriter, _ samples: [Float], block: Int = 4096) {
    var k = 0
    while k < samples.count {
        writer.append(Array(samples[k..<min(k + block, samples.count)]))
        k += block
    }
}

@Suite("Gravação", .serialized)
struct RecorderTests {
    @Test("Um seno de 1 s vira um M4A de 1 s, mono 48 kHz, que o MediaProbe e o Waveform leem", arguments: [48_000.0, 44_100.0])
    func writesM4A(inputRate: Double) async throws {
        let url = TestMedia.directory.appendingPathComponent("rec-\(UUID().uuidString)/Gravação 1.m4a")
        let writer = try RecordingWriter(url: url, inputSampleRate: inputRate)
        feed(writer, sine(seconds: 1, sampleRate: inputRate))
        // O nível é o pico desde a última leitura.
        let level = writer.takeLevel()
        #expect(abs(level - 0.5) < 0.02)
        #expect(writer.takeLevel() == 0)
        let duration = try writer.finish()
        #expect(abs(duration - Time.second) < 2_000, "duração \(duration)")

        // Picos ao vivo: 50 por segundo, no nível real (sem normalizar).
        let live = writer.peaks
        #expect(abs(live.count - 50) <= 1)
        #expect(live.dropLast().allSatisfy { abs($0 - 0.5) < 0.02 })

        let m = try await MediaProbe.probe(url)
        #expect(m.kind == .audio)
        #expect(m.hasAudio)
        #expect(abs(m.duration - Time.second) < 30_000, "duração no arquivo \(m.duration)")
        let track = try #require(try await AVURLAsset(url: url).loadTracks(withMediaType: .audio).first)
        let format = try #require(try await track.load(.formatDescriptions).first)
        let asbd = try #require(CMAudioFormatDescriptionGetStreamBasicDescription(format)?.pointee)
        #expect(asbd.mSampleRate == 48_000)
        #expect(asbd.mChannelsPerFrame == 1)
        #expect(asbd.mFormatID == kAudioFormatMPEG4AAC)
        let peaks = try await Waveform.peaks(url: url)
        #expect(abs(peaks.count - 50) <= 2)
        #expect(peaks.max() == 1)
    }

    @Test("Cancelar apaga o arquivo")
    func cancelDeletes() throws {
        let url = TestMedia.tempURL("m4a")
        let writer = try RecordingWriter(url: url)
        feed(writer, sine(seconds: 0.3, sampleRate: 48_000))
        #expect(FileManager.default.fileExists(atPath: url.path))
        writer.cancel()
        #expect(!FileManager.default.fileExists(atPath: url.path))
    }

    @Test("Nomes \"Gravação N\" sem colidir, na pasta do projeto ou ao lado da biblioteca")
    func recordingNames() throws {
        let root = TestMedia.directory.appendingPathComponent("lib-\(UUID().uuidString)/Projetos")
        let library = ProjectLibrary(root: root, usesTrash: false)
        let id = UUID()
        let folder = library.recordingsFolder(for: id)
        #expect(folder == library.folder(for: id).appendingPathComponent("Gravações", isDirectory: true))
        #expect(library.recordingsFolder(for: nil) == root.deletingLastPathComponent().appendingPathComponent("Gravações", isDirectory: true))
        #expect(library.recordingURL(for: id).lastPathComponent == "Gravação 1.m4a")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for name in ["Gravação 1.m4a", "Gravação 3.m4a", "Gravação x.m4a", "outra.m4a"] {
            FileManager.default.createFile(atPath: folder.appendingPathComponent(name).path, contents: Data())
        }
        #expect(library.recordingURL(for: id).lastPathComponent == "Gravação 4.m4a")
        #expect(library.recordingURL(for: nil).lastPathComponent == "Gravação 1.m4a")
    }

    @Test("Gravação simulada (OPENFRAME_FAKE_RECORDING) grava em tempo real, com nível e picos")
    func fakeRecording() async throws {
        setenv("OPENFRAME_FAKE_RECORDING", "1", 1)
        defer { unsetenv("OPENFRAME_FAKE_RECORDING") }
        #expect(AudioRecorder.inputs().map(\.id) == ["fake"])
        #expect(AudioRecorder.defaultInput == "fake")
        #expect(await AudioRecorder.requestPermission())
        let url = TestMedia.directory.appendingPathComponent("fake-\(UUID().uuidString)/Gravação 1.m4a")
        let recorder = AudioRecorder()
        try recorder.start(input: nil, to: url)
        #expect(recorder.isRecording)
        try await Task.sleep(nanoseconds: 700_000_000)
        #expect(recorder.takeLevel() > 0.1)
        #expect(recorder.peaks.count >= 20)
        let duration = try await recorder.stop()
        #expect(!recorder.isRecording)
        #expect(duration > 500_000 && duration < 3_000_000, "duração \(duration)")
        #expect(recorder.duration == duration)
        let m = try await MediaProbe.probe(url)
        #expect(abs(m.duration - duration) < 50_000)

        // Cancelar apaga; os erros simulados são os da interface.
        let other = url.deletingLastPathComponent().appendingPathComponent("Gravação 2.m4a")
        try recorder.start(input: "fake", to: other)
        recorder.cancel()
        #expect(!FileManager.default.fileExists(atPath: other.path))
        await #expect(throws: RecordingError.self) { try await recorder.stop() }

        setenv("OPENFRAME_FAKE_RECORDING", "sem-permissao", 1)
        #expect(!(await AudioRecorder.requestPermission()))
        #expect(throws: RecordingError.permissionDenied) { try recorder.start(input: nil, to: other) }
        setenv("OPENFRAME_FAKE_RECORDING", "sem-microfone", 1)
        #expect(AudioRecorder.inputs().isEmpty)
        #expect(AudioRecorder.defaultInput == nil)
        #expect(throws: RecordingError.noInput) { try recorder.start(input: nil, to: other) }
        #expect(!FileManager.default.fileExists(atPath: other.path))
    }

    /// Com o microfone de verdade (pede a permissão em nome do terminal): `OPENFRAME_TEST_MIC=1`.
    @Test("Grava 1 s do microfone padrão", .enabled(if: ProcessInfo.processInfo.environment["OPENFRAME_TEST_MIC"] == "1"))
    func realMicrophone() async throws {
        #expect(!AudioRecorder.inputs().isEmpty)
        #expect(await AudioRecorder.requestPermission())
        let url = TestMedia.tempURL("m4a")
        let recorder = AudioRecorder()
        try recorder.start(input: AudioRecorder.defaultInput, to: url)
        try await Task.sleep(nanoseconds: 1_000_000_000)
        let duration = try await recorder.stop()
        #expect(duration > 700_000, "duração \(duration)")
        let m = try await MediaProbe.probe(url)
        #expect(m.kind == .audio)
        print("microfone: \(AudioRecorder.inputs().map(\.name)), \(duration) µs, pico \(recorder.peaks.max() ?? 0)")
    }
}
