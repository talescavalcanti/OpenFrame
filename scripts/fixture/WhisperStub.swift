import Foundation

// Substituto do `WhisperEngine` (que precisa do alvo CWhisper) para compilar o OpenFrameCore com `swiftc`
// nos geradores de referência (fixtures e medidas para o Qt). Nenhum deles transcreve.
final class WhisperEngine: @unchecked Sendable {
    static let shared = WhisperEngine()

    struct Word: Equatable {
        var text: String
        var start: Double
        var end: Double
        var probability: Double
    }

    struct Options: Sendable {
        var language: String?
    }

    final class Flag: @unchecked Sendable {
        func set() {}
    }

    func unload() {}

    func transcribe(_ samples: [Float], modelURL: URL, model: WhisperModel?, options: Options, cancelled: Flag,
                    progress: @escaping (Double) -> Void) throws -> [Word] {
        throw TranscriptionError.engineUnavailable
    }
}
