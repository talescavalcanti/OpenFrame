#pragma once

#include "Captions.h"
#include "Project.h"
#include "WhisperEngine.h"
#include "WhisperModels.h"

#include <QObject>
#include <atomic>
#include <functional>
#include <memory>

namespace of {

/// Progresso da transcrição: a fase, quanto dela já foi e o total (mistura ~10%, transcrição ~90%).
struct TranscriptionProgress {
    enum class Phase { Mixing, Transcribing };
    Phase phase = Phase::Mixing;
    /// 0...1 dentro da fase.
    double fraction = 0;
    /// 0...1 no total.
    double overall = 0;

    /// Peso da mistura no total.
    static constexpr double mixingWeight = 0.1;
    static TranscriptionProgress make(Phase phase, double fraction);
    bool operator==(const TranscriptionProgress &) const = default;
};

/// O que transcrever: a timeline inteira ou um segmento (com áudio).
struct TranscriptionSource {
    /// Vazio = a timeline inteira.
    std::optional<QUuid> segment;
    static TranscriptionSource timeline() { return {}; }
    static TranscriptionSource of(const QUuid &id) { return {id}; }
    bool operator==(const TranscriptionSource &) const = default;
};

enum class TranscriptionError { EngineUnavailable, ModelNotInstalled, NoAudio, Failed };
/// Mensagem para a janela ("O modelo Equilibrado não está instalado…"); `detail` vai no `Failed`.
QString message(TranscriptionError error, WhisperModel model = WhisperModel::Small, const QString &detail = {});

/// Transcrição das legendas automáticas: mistura o áudio como a exportação ouve, converte para 16 kHz mono,
/// transcreve com o whisper.cpp na CPU e devolve palavras com tempos na timeline.
///
/// Com `OPENFRAME_FAKE_TRANSCRIPTION=1`, devolve palavras falsas espalhadas pelo trecho (~2,6 por segundo, com
/// pausas nas pontuações) e um progresso de ~0,8 s (capturas de tela e testes da interface).
///
/// Roda numa thread de trabalho; os sinais chegam na thread do Transcriber.
class Transcriber : public QObject
{
    Q_OBJECT

public:
    explicit Transcriber(QObject *parent = nullptr);
    /// Cancela e espera a transcrição em andamento.
    ~Transcriber() override;

    /// Começa (ignorado se já houver uma em andamento). `language` vazio = detectar.
    void start(const Project &project, const TranscriptionSource &source, WhisperModel model,
               const QString &language = {});
    /// Interrompe; termina com `finished(false, {}, "")`.
    void cancel();
    bool isRunning() const;

    /// Trecho da timeline a transcrever: o projeto inteiro ou o segmento (é o `range` do
    /// `CaptionBuilder::apply` ao substituir). Vazio (com `NoAudio`) se não houver o que transcrever.
    static std::optional<TimeRange> sourceRange(const Project &project, const TranscriptionSource &source);
    /// Projeto que a mistura ouve: o inteiro, ou só o segmento escolhido (na faixa dele, audível mesmo se a
    /// faixa estiver muda, o volume em zero ou o som extraído).
    static Project mixProject(const Project &project, const TranscriptionSource &source);

    struct Language {
        QString code;
        QString name;
    };
    /// Idiomas oferecidos (códigos do Whisper), com os nomes em português.
    static QList<Language> languages();

    /// Palavras falsas no trecho (as de `OPENFRAME_FAKE_TRANSCRIPTION=1`).
    static QList<TranscribedWord> fakeWords(TimeRange range);

    /// A transcrição, síncrona, com o arquivo do modelo e as opções do motor à escolha (é o que o `start` roda
    /// na thread; os testes e as medições chamam direto). Vazio com `cancelled` (sem erro) ou com a mensagem
    /// em `error` (sem áudio, motor indisponível, falha do whisper).
    static std::optional<QList<TranscribedWord>> run(const Project &project, const TranscriptionSource &source,
                                                     const QString &modelPath, std::optional<WhisperModel> model,
                                                     const WhisperEngine::Options &options,
                                                     const std::atomic<bool> &cancelled,
                                                     const std::function<void(const TranscriptionProgress &)> &progress,
                                                     QString *error);

signals:
    void progress(const of::TranscriptionProgress &progress);
    /// Cancelado = `ok` falso com `error` vazio.
    void finished(bool ok, const QList<of::TranscribedWord> &words, const QString &error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace of
