import AVFoundation
import CoreAudio

public enum RecordingError: LocalizedError, Equatable {
    case permissionDenied
    case noInput
    case failed(String)

    public var errorDescription: String? {
        switch self {
        case .permissionDenied:
            "O OpenFrame não tem permissão para usar o microfone. Ligue em Ajustes do Sistema → Privacidade e "
                + "Segurança → Microfone."
        case .noInput: "Nenhum microfone encontrado."
        case .failed(let detail): "Não foi possível gravar: \(detail)"
        }
    }
}

/// Grava blocos de áudio mono num M4A (AAC, 48 kHz, mono), reamostrando se a entrada vier em outra taxa, e
/// guarda o nível e os picos para a forma de onda ao vivo. Separado da entrada (o microfone) para os testes
/// alimentarem um seno. Thread-safe: `append` vem da thread do áudio; o resto, de qualquer uma.
public final class RecordingWriter: @unchecked Sendable {
    public static let sampleRate = 48_000.0
    /// Picos por segundo da forma de onda ao vivo, como o `Waveform`.
    public static let peaksPerSecond = Waveform.peaksPerSecond

    public let url: URL
    private let lock = NSLock()
    private var file: AVAudioFile?
    private let inputFormat: AVAudioFormat
    private let outputFormat: AVAudioFormat
    private let converter: AVAudioConverter?
    private var written: Int64 = 0
    private var level: Float = 0
    private var livePeaks: [Float] = []
    private var bucketPeak: Float = 0
    private var bucketFill = 0
    private var failure: Error?

    /// Cria o arquivo (e a pasta). `inputSampleRate` é a taxa dos blocos de `append`.
    public init(url: URL, inputSampleRate: Double = sampleRate) throws {
        guard inputSampleRate > 0,
              let input = AVAudioFormat(standardFormatWithSampleRate: inputSampleRate, channels: 1),
              let output = AVAudioFormat(standardFormatWithSampleRate: Self.sampleRate, channels: 1)
        else { throw RecordingError.failed("formato de áudio inválido") }
        self.url = url
        inputFormat = input
        outputFormat = output
        converter = inputSampleRate == Self.sampleRate ? nil : AVAudioConverter(from: input, to: output)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        do {
            file = try AVAudioFile(forWriting: url, settings: [
                AVFormatIDKey: kAudioFormatMPEG4AAC,
                AVSampleRateKey: Self.sampleRate,
                AVNumberOfChannelsKey: 1,
                AVEncoderBitRateKey: 128_000,
            ], commonFormat: .pcmFormatFloat32, interleaved: false)
        } catch {
            throw RecordingError.failed(error.localizedDescription)
        }
    }

    /// Acrescenta amostras mono (na taxa de entrada).
    public func append(_ samples: UnsafeBufferPointer<Float>) {
        guard !samples.isEmpty,
              let buffer = AVAudioPCMBuffer(pcmFormat: inputFormat, frameCapacity: AVAudioFrameCount(samples.count))
        else { return }
        buffer.frameLength = AVAudioFrameCount(samples.count)
        buffer.floatChannelData![0].update(from: samples.baseAddress!, count: samples.count)
        lock.lock()
        defer { lock.unlock() }
        guard file != nil, failure == nil else { return }
        if let converter {
            convert(buffer, with: converter, ending: false)
        } else {
            write(buffer)
        }
    }

    public func append(_ samples: [Float]) {
        samples.withUnsafeBufferPointer { append($0) }
    }

    /// Converte para 48 kHz; `ending` esvazia o que o conversor ainda guarda.
    private func convert(_ buffer: AVAudioPCMBuffer?, with converter: AVAudioConverter, ending: Bool) {
        var pending = buffer
        while true {
            let capacity = AVAudioFrameCount(Double(buffer?.frameLength ?? 0) * Self.sampleRate / inputFormat.sampleRate) + 1024
            guard let out = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: capacity) else { return }
            var error: NSError?
            let status = converter.convert(to: out, error: &error) { _, inputStatus in
                if let next = pending {
                    pending = nil
                    inputStatus.pointee = .haveData
                    return next
                }
                inputStatus.pointee = ending ? .endOfStream : .noDataNow
                return nil
            }
            if let error {
                failure = error
                return
            }
            if out.frameLength > 0 { write(out) }
            if status != .haveData { return }
        }
    }

    private func write(_ buffer: AVAudioPCMBuffer) {
        do {
            try file?.write(from: buffer)
        } catch {
            failure = error
            return
        }
        written += Int64(buffer.frameLength)
        let bucket = Int(Self.sampleRate / Self.peaksPerSecond)
        let samples = UnsafeBufferPointer(start: buffer.floatChannelData![0], count: Int(buffer.frameLength))
        for s in samples {
            let a = min(abs(s), 1)
            level = max(level, a)
            bucketPeak = max(bucketPeak, a)
            bucketFill += 1
            if bucketFill == bucket {
                livePeaks.append(bucketPeak)
                bucketPeak = 0
                bucketFill = 0
            }
        }
    }

    /// Pico desde a última leitura (0...1).
    public func takeLevel() -> Float {
        lock.lock()
        defer { lock.unlock() }
        let l = level
        level = 0
        return l
    }

    /// Picos de cada 1/50 s já gravado (0...1, sem normalizar: o nível real do microfone).
    public var peaks: [Float] {
        lock.lock()
        defer { lock.unlock() }
        return livePeaks
    }

    /// Duração gravada até agora.
    public var duration: Micros {
        lock.lock()
        defer { lock.unlock() }
        return Micros((Double(written) / Self.sampleRate * 1_000_000).rounded())
    }

    /// Fecha o arquivo e devolve a duração gravada.
    @discardableResult
    public func finish() throws -> Micros {
        lock.lock()
        if let converter, file != nil, failure == nil { convert(nil, with: converter, ending: true) }
        if bucketFill > 0 {
            livePeaks.append(bucketPeak)
            bucketPeak = 0
            bucketFill = 0
        }
        // O AVAudioFile fecha (e grava o cabeçalho do M4A) ao ser liberado.
        file = nil
        let error = failure
        lock.unlock()
        if let error {
            try? FileManager.default.removeItem(at: url)
            throw RecordingError.failed(error.localizedDescription)
        }
        return duration
    }

    /// Fecha e apaga o arquivo.
    public func cancel() {
        lock.lock()
        file = nil
        lock.unlock()
        try? FileManager.default.removeItem(at: url)
    }
}

/// Grava o microfone num arquivo de áudio (M4A/AAC, 48 kHz, mono), com o nível e a forma de onda ao vivo. A
/// interface põe o resultado na timeline com `TimelineEditor.addRecording`.
///
/// `OPENFRAME_FAKE_RECORDING` (para as capturas, que não têm microfone): `1` grava uma "voz" sintética em tempo
/// real no lugar do microfone; `sem-permissao` e `sem-microfone` simulam os erros.
public final class AudioRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var engine: AVAudioEngine?
    private var fake: DispatchSourceTimer?
    /// A gravação em andamento; depois do `stop`, continua para o nível, os picos e a duração.
    private var writer: RecordingWriter?
    private var recording = false

    public init() {}

    private static var fakeMode: String? { ProcessInfo.processInfo.environment["OPENFRAME_FAKE_RECORDING"] }

    // MARK: - Entradas

    /// Microfones disponíveis (id = o UID do Core Audio), na ordem do sistema.
    public static func inputs() -> [(id: String, name: String)] {
        if let mode = fakeMode { return mode == "sem-microfone" ? [] : [("fake", "Microfone de teste")] }
        return inputDevices().map { ($0.uid, $0.name) }
    }

    /// O microfone padrão do sistema.
    public static var defaultInput: String? {
        if let mode = fakeMode { return mode == "sem-microfone" ? nil : "fake" }
        var id = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultInputDevice,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &id) == noErr,
              id != kAudioObjectUnknown else { return nil }
        return inputDevices().first { $0.id == id }?.uid
    }

    /// Pede a permissão do microfone (a janela do sistema, na primeira vez).
    public static func requestPermission() async -> Bool {
        if let mode = fakeMode { return mode != "sem-permissao" }
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized: return true
        case .notDetermined: return await AVCaptureDevice.requestAccess(for: .audio)
        default: return false
        }
    }

    private struct Device {
        var id: AudioObjectID
        var uid: String
        var name: String
    }

    private static func inputDevices() -> [Device] {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                                 mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        let system = AudioObjectID(kAudioObjectSystemObject)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &address, 0, nil, &size) == noErr else { return [] }
        var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(system, &address, 0, nil, &size, &ids) == noErr else { return [] }
        return ids.compactMap { id in
            guard inputChannels(id) > 0,
                  let uid = string(id, kAudioDevicePropertyDeviceUID),
                  let name = string(id, kAudioObjectPropertyName) else { return nil }
            return Device(id: id, uid: uid, name: name)
        }
    }

    private static func inputChannels(_ id: AudioObjectID) -> Int {
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration,
                                                 mScope: kAudioObjectPropertyScopeInput,
                                                 mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(id, &address, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(id, &address, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    private static func string(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
        var address = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                                                 mElement: kAudioObjectPropertyElementMain)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(id, &address, 0, nil, &size, &value) == noErr, let value else { return nil }
        return value.takeRetainedValue() as String
    }

    // MARK: - Gravação

    /// Começa a gravar o microfone `input` (nil = o padrão do sistema) em `url` (a pasta é criada). Chame
    /// `requestPermission` antes: sem a permissão, dá `RecordingError.permissionDenied`.
    public func start(input: String?, to url: URL) throws {
        cancel()
        if let mode = Self.fakeMode {
            if mode == "sem-permissao" { throw RecordingError.permissionDenied }
            if mode == "sem-microfone" { throw RecordingError.noInput }
            try startFake(to: url)
            return
        }
        guard AVCaptureDevice.authorizationStatus(for: .audio) == .authorized else {
            throw RecordingError.permissionDenied
        }
        let devices = Self.inputDevices()
        guard !devices.isEmpty else { throw RecordingError.noInput }

        let engine = AVAudioEngine()
        let node = engine.inputNode
        if let input, let device = devices.first(where: { $0.uid == input }), let unit = node.audioUnit {
            var id = device.id
            let status = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                              &id, UInt32(MemoryLayout<AudioObjectID>.size))
            if status != noErr { throw RecordingError.failed("não foi possível abrir o microfone (\(status))") }
        }
        let format = node.outputFormat(forBus: 0)
        guard format.sampleRate > 0, format.channelCount > 0 else { throw RecordingError.noInput }
        let writer = try RecordingWriter(url: url, inputSampleRate: format.sampleRate)
        node.installTap(onBus: 0, bufferSize: 4096, format: format) { buffer, _ in
            guard let data = buffer.floatChannelData else { return }
            let frames = Int(buffer.frameLength)
            let channels = Int(buffer.format.channelCount)
            var mono = [Float](repeating: 0, count: frames)
            for c in 0..<channels {
                for i in 0..<frames { mono[i] += data[c][i] }
            }
            if channels > 1 {
                for i in 0..<frames { mono[i] /= Float(channels) }
            }
            writer.append(mono)
        }
        engine.prepare()
        do {
            try engine.start()
        } catch {
            node.removeTap(onBus: 0)
            writer.cancel()
            throw RecordingError.failed(error.localizedDescription)
        }
        lock.lock()
        self.engine = engine
        self.writer = writer
        recording = true
        lock.unlock()
    }

    /// "Voz" sintética em tempo real: sílabas de 180 Hz com envelope, pausas entre as palavras.
    private func startFake(to url: URL) throws {
        let writer = try RecordingWriter(url: url)
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "openframe.fake-recording"))
        let start = DispatchTime.now()
        final class Counter: @unchecked Sendable { var sent = 0 }
        let counter = Counter()
        timer.schedule(deadline: .now(), repeating: .milliseconds(20))
        timer.setEventHandler {
            let elapsed = Double(DispatchTime.now().uptimeNanoseconds - start.uptimeNanoseconds) / 1e9
            let due = Int(elapsed * RecordingWriter.sampleRate)
            let sent = counter.sent
            guard due > sent else { return }
            var block = [Float](repeating: 0, count: due - sent)
            for i in block.indices {
                let t = Double(sent + i) / RecordingWriter.sampleRate
                let syllable = max(0, sin(.pi * t * 4.3)) * (0.35 + 0.3 * sin(t * 1.7) * sin(t * 0.9))
                let word = fmod(t, 2.2) < 1.7 ? 1.0 : 0.0
                block[i] = Float(sin(2 * .pi * 180 * t) * syllable * word)
            }
            counter.sent = due
            writer.append(block)
        }
        lock.lock()
        self.writer = writer
        fake = timer
        recording = true
        lock.unlock()
        timer.resume()
    }

    /// Para a entrada; devolve a gravação que estava em andamento.
    private func detach() -> RecordingWriter? {
        lock.lock()
        defer { lock.unlock() }
        if let engine {
            engine.stop()
            engine.inputNode.removeTap(onBus: 0)
        }
        fake?.cancel()
        engine = nil
        fake = nil
        guard recording else { return nil }
        recording = false
        return writer
    }

    /// Para e fecha o arquivo; devolve a duração gravada.
    public func stop() async throws -> Micros {
        guard let writer = detach() else { throw RecordingError.failed("nada sendo gravado") }
        return try writer.finish()
    }

    /// Para e apaga o arquivo.
    public func cancel() {
        detach()?.cancel()
        lock.lock()
        writer = nil
        lock.unlock()
    }

    public var isRecording: Bool {
        lock.lock()
        defer { lock.unlock() }
        return recording
    }

    /// Nível de pico desde a última leitura (0...1).
    public func takeLevel() -> Float { currentWriter?.takeLevel() ?? 0 }
    /// Picos de cada 1/50 s já gravado (0...1), para a forma de onda ao vivo.
    public var peaks: [Float] { currentWriter?.peaks ?? [] }
    public var duration: Micros { currentWriter?.duration ?? 0 }

    private var currentWriter: RecordingWriter? {
        lock.lock()
        defer { lock.unlock() }
        return writer
    }
}
