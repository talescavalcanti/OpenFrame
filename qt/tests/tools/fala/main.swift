import AVFoundation
import Foundation

// Falas dos testes de transcrição da versão Qt (como o `Speech` dos testes Swift):
// - frase.wav: a frase do teste, dita de uma vez;
// - palavras.wav + palavras.json: a mesma frase palavra por palavra, emendada com pausas conhecidas (0,6 s
//   depois de ponto, 0,35 s depois de vírgula, 0,12 s entre palavras), com o início e o fim exatos de cada uma;
// - primeiro.wav, banana.wav e bomdia.wav: frases curtas dos testes de fonte = clipe e de idioma.
// Uso: qt/tests/tools/gerar-fala.sh

let out = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
let rate = 16_000.0
let voice = "Luciana"
let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("openframe-fala-\(UUID().uuidString)")
try FileManager.default.createDirectory(at: tmp, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: tmp) }

func say(_ text: String) throws -> [Float] {
    let url = tmp.appendingPathComponent(UUID().uuidString + ".caf")
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/say")
    p.arguments = ["-v", voice, "-o", url.path, "--file-format=caff", "--data-format=LEF32@16000", text]
    try p.run()
    p.waitUntilExit()
    let file = try AVAudioFile(forReading: url)
    let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: AVAudioFrameCount(file.length))!
    try file.read(into: buffer)
    return Array(UnsafeBufferPointer(start: buffer.floatChannelData![0], count: Int(buffer.frameLength)))
}

/// Trecho com som (sem o silêncio do começo e do fim).
func trimmed(_ s: [Float]) -> [Float] {
    guard let first = s.firstIndex(where: { abs($0) > 0.01 }), let last = s.lastIndex(where: { abs($0) > 0.01 }) else { return [] }
    return Array(s[first...last])
}

func write(_ samples: [Float], _ name: String) throws {
    let url = out.appendingPathComponent(name)
    try? FileManager.default.removeItem(at: url)
    let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1)!
    let file = try AVAudioFile(forWriting: url, settings: [
        AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: rate, AVNumberOfChannelsKey: 1,
        AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false,
    ], commonFormat: .pcmFormatFloat32, interleaved: false)
    let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(samples.count))!
    buffer.frameLength = AVAudioFrameCount(samples.count)
    samples.withUnsafeBufferPointer { buffer.floatChannelData![0].update(from: $0.baseAddress!, count: samples.count) }
    try file.write(from: buffer)
}

let sentence = "Olá, pessoal. Hoje vamos testar as legendas automáticas do editor, direto no computador."
try write(try say(sentence), "frase.wav")

var samples = [Float](repeating: 0, count: Int(0.5 * rate))
var truth: [[String: Any]] = []
for word in sentence.split(separator: " ").map(String.init) {
    let audio = trimmed(try say(word.trimmingCharacters(in: .punctuationCharacters)))
    let start = Double(samples.count) / rate
    samples += audio
    truth.append(["text": word, "start": start, "end": Double(samples.count) / rate])
    let gap = word.hasSuffix(".") ? 0.6 : word.hasSuffix(",") ? 0.35 : 0.12
    samples += [Float](repeating: 0, count: Int(gap * rate))
}
samples += [Float](repeating: 0, count: 8000)
try write(samples, "palavras.wav")
let json = try JSONSerialization.data(withJSONObject: ["sentence": sentence, "words": truth], options: [.prettyPrinted, .sortedKeys])
try json.write(to: out.appendingPathComponent("palavras.json"))

try write(try say("Primeiro clipe com uma frase qualquer."), "primeiro.wav")
try write(try say("Segundo trecho falando de bananas."), "banana.wav")
try write(try say("Bom dia. Este teste roda sem a placa de vídeo."), "bomdia.wav")
print("ok: \(out.path)")
