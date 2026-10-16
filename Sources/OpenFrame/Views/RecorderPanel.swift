import OpenFrameCore
import SwiftUI

/// Vermelho da gravação (botão, clipe que cresce).
let recordingRed = Color(red: 1, green: 0.27, blue: 0.27)

/// Painel "Gravar áudio" (popover do botão de microfone da barra da timeline):
/// o microfone, o nível ao vivo e o botão vermelho; depois, a contagem, o tempo
/// gravado com a forma de onda, e os erros de permissão ou de microfone.
struct RecorderPanel: View {
    @Environment(EditorModel.self) private var model

    var body: some View {
        let r = model.recording
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Gravar áudio")
                    .font(Theme.font(13, .medium))
                Spacer()
                if r.phase == .recording {
                    Circle().fill(recordingRed).frame(width: 7, height: 7)
                    Text(Time.format(r.duration))
                        .font(Theme.mono(11, .medium))
                        .foregroundStyle(Theme.textSecondary)
                }
            }
            microphoneMenu(r)
            LevelMeter(level: r.level)
            content(r)
        }
        .padding(16)
        .frame(width: 280)
        // Fundo opaco do tema (o material do popover sai transparente nas capturas).
        .background(Theme.raised)
        .alert("Descartar a gravação?", isPresented: Binding(get: { r.confirmDiscard },
                                                             set: { r.confirmDiscard = $0 })) {
            Button("Descartar", role: .destructive) { model.cancelRecording(confirmed: true) }
            Button("Continuar gravando", role: .cancel) {}
        } message: {
            Text("O áudio gravado até agora será apagado.")
        }
    }

    private func microphoneMenu(_ r: AudioRecording) -> some View {
        Menu {
            Button {
                model.selectRecordingInput(nil)
            } label: {
                if r.input == nil { Label("Padrão do sistema", systemImage: "checkmark") } else { Text("Padrão do sistema") }
            }
            if !r.inputs.isEmpty { Divider() }
            ForEach(r.inputs) { input in
                Button {
                    model.selectRecordingInput(input.id)
                } label: {
                    if r.input == input.id { Label(input.name, systemImage: "checkmark") } else { Text(input.name) }
                }
            }
        } label: {
            Label(r.inputName, systemImage: "mic")
                .font(Theme.font(12))
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .disabled(r.phase.isActive)
        .help("Microfone")
    }

    @ViewBuilder
    private func content(_ r: AudioRecording) -> some View {
        switch r.phase {
        case .ready:
            recordButton(recording: false) { model.startRecording() }
            Text("A timeline toca sem som enquanto você grava. Espaço para; Esc cancela.")
                .font(Theme.font(11))
                .foregroundStyle(Theme.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
        case .countdown(let n):
            HStack(spacing: 12) {
                Text("\(n)")
                    .font(Theme.font(34, .light))
                    .foregroundStyle(recordingRed)
                    .frame(width: 40)
                Text("Gravando em \(n)…\nEsc cancela.")
                    .font(Theme.font(12))
                    .foregroundStyle(Theme.textSecondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        case .recording, .finishing:
            WaveformBars(peaks: r.peaks, from: max(0, r.duration.seconds - 6), to: max(r.duration.seconds, 0.02),
                         color: recordingRed.opacity(0.85), bar: 2, gap: 1)
                .frame(height: 34)
            recordButton(recording: true) { model.stopRecording() }
                .disabled(r.phase == .finishing)
        case .failed(let message):
            Label(message, systemImage: "exclamationmark.triangle")
                .font(Theme.font(12))
                .foregroundStyle(Color(red: 1, green: 0.62, blue: 0.35))
                .fixedSize(horizontal: false, vertical: true)
            Button("Tentar de novo") { model.presentRecorder() }
                .buttonStyle(PillButtonStyle(kind: .secondary, compact: true))
        }
    }

    private func recordButton(recording: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 8) {
                RoundedRectangle(cornerRadius: recording ? 2 : 6, style: .continuous)
                    .fill(.white)
                    .frame(width: recording ? 10 : 12, height: recording ? 10 : 12)
                Text(recording ? "Parar" : "Gravar")
                    .font(Theme.font(13, .medium))
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .frame(height: 34)
            .background(recordingRed, in: Capsule())
            .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .help(recording ? "Parar (espaço)" : "Gravar com contagem de 3 s")
    }
}

/// Medidor de nível em segmentos (verde, amarelo perto do pico).
private struct LevelMeter: View {
    let level: Float

    var body: some View {
        Canvas { ctx, size in
            let count = 24
            let gap: CGFloat = 2
            let w = (size.width - gap * CGFloat(count - 1)) / CGFloat(count)
            // Escala em dB (−60...0), como os medidores de áudio.
            let db = 20 * log10(max(Double(level), 0.000_001))
            let lit = Int((max(0, min(1, (db + 60) / 60)) * Double(count)).rounded())
            for i in 0..<count {
                let rect = CGRect(x: CGFloat(i) * (w + gap), y: 0, width: w, height: size.height)
                let color: Color = i >= lit ? Color.white.opacity(0.08)
                    : i >= count - 3 ? recordingRed : i >= count - 7 ? Color.yellow : Theme.accent
                ctx.fill(Path(roundedRect: rect, cornerRadius: 1.5), with: .color(color))
            }
        }
        .frame(height: 8)
        .accessibilityLabel("Nível do microfone")
    }
}
