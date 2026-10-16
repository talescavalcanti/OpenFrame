import Foundation
import Observation
import OpenFrameCore

/// Estado do painel "Gravar áudio": o microfone escolhido, o nível ao vivo e a fase
/// da gravação. Fica no `EditorModel` (que faz a gravação) para a timeline desenhar o
/// clipe que cresce e para o DebugSnapshot montar cada estado do painel.
@MainActor
@Observable
final class AudioRecording {
    struct Input: Hashable, Identifiable {
        let id: String
        let name: String
    }

    enum Phase: Equatable {
        /// Painel aberto: o microfone só dá o nível, nada é gravado.
        case ready
        /// Contagem antes de gravar (3, 2, 1); Esc cancela.
        case countdown(Int)
        case recording
        /// Fechando o arquivo e pondo o clipe na timeline.
        case finishing
        case failed(String)

        /// Contagem, gravação ou fechamento: o painel não fecha e as teclas são do gravador.
        var isActive: Bool {
            switch self {
            case .countdown, .recording, .finishing: true
            default: false
            }
        }
    }

    var showPanel = false
    var phase = Phase.ready
    var inputs: [Input] = []
    /// Microfone escolhido (nil = o padrão do sistema).
    var input: String?
    /// Nível do microfone (0...1), com uma queda suave para o medidor.
    var level: Float = 0
    /// Picos da gravação em andamento (50 por segundo, sem normalizar).
    var peaks: [Float] = []
    var duration: Micros = 0
    /// Onde a gravação começa na timeline.
    var start: Micros = 0
    /// Pergunta antes de jogar fora uma gravação de mais de 3 s.
    var confirmDiscard = false

    @ObservationIgnored let recorder = AudioRecorder()
    /// Escuta o microfone para o nível enquanto o painel está aberto (o arquivo é apagado).
    @ObservationIgnored var monitor: AudioRecorder?
    @ObservationIgnored var timer: Timer?
    @ObservationIgnored var countdownTask: Task<Void, Never>?
    @ObservationIgnored var url: URL?
    /// A gravação começou antes do fim do projeto: para quando a reprodução chegar ao fim.
    @ObservationIgnored var stopsAtEnd = false

    /// O microfone padrão do sistema, quando o painel abriu.
    var defaultInput: String?

    /// Nome do microfone escolhido, para o menu.
    var inputName: String {
        if let name = inputs.first(where: { $0.id == input })?.name { return name }
        return inputs.first { $0.id == defaultInput }.map { "Padrão (\($0.name))" } ?? "Padrão do sistema"
    }
}
