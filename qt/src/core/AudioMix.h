#pragma once

#include "Project.h"

#include <QElapsedTimer>
#include <QList>
#include <memory>

namespace of {

/// Um trecho de áudio da mixagem: o som de um segmento, já com as regras do
/// `CompositionBuilder` do Swift aplicadas (volume limitado, fades com o
/// cruzamento das transições e limitados à metade da duração).
struct AudioClip {
    QUuid segment;
    QString path;
    /// Trecho do arquivo e posição/duração na timeline.
    TimeRange source;
    Micros targetStart = 0;
    Micros targetDuration = 0;
    double speed = 1;
    /// 0…1.
    float volume = 1;
    Micros fadeIn = 0;
    Micros fadeOut = 0;

    Micros targetEnd() const { return targetStart + targetDuration; }
    bool operator==(const AudioClip &) const = default;
};

namespace AudioMix {

/// Formato da mixagem: 48 kHz estéreo, float intercalado.
constexpr int sampleRate = 48000;
constexpr int channels = 2;

/// Os trechos que soam no projeto, como no Swift:
/// - vídeos (com áudio) das faixas de vídeo não mudas, com volume > 0;
/// - áudios das faixas de áudio não mudas, com volume > 0;
/// - nas transições da faixa principal, a sobreposição vira fade extra
///   (o som cruza junto com a imagem).
QList<AudioClip> clips(const Project &project);

/// Ganho do trecho no instante `t` da timeline (rampas lineares, como o
/// `setVolumeRamp` do AVFoundation); 0 fora do trecho.
float gain(const AudioClip &clip, Micros t);

/// Amostra (a 48 kHz) do instante `t`, arredondada.
qint64 sampleAt(Micros t);

} // namespace AudioMix

/// Mistura os trechos em sequência, a partir de uma posição. Cada trecho tem um
/// decodificador próprio (FFmpeg), reamostrado para 48 kHz; a velocidade muda a
/// duração sem mudar o tom (filtro atempo, como o time-pitch do AVFoundation).
/// Os decodificadores abrem perto da hora de tocar e fecham depois do fim.
///
/// Não é thread-safe: use de uma thread só (a do áudio ou a da exportação).
class AudioMixer
{
public:
    explicit AudioMixer(QList<AudioClip> clips);
    ~AudioMixer();

    /// Próxima leitura começa em `t`.
    void seek(Micros t);
    /// Posição atual, em amostras de 48 kHz.
    qint64 position() const;
    /// Escreve `frames` quadros estéreo (intercalados) e avança. A soma é
    /// limitada a −1…1.
    void read(float *out, int frames);

    const QList<AudioClip> &clips() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// Som do preview preso ao relógio do vídeo. O relógio do Player começa no play,
/// mas o som só sai depois que o dispositivo parte (no Windows, com o WASAPI,
/// isso passa de meio segundo, mais ainda numa máquina virtual). Tocar a partir do
/// instante do play deixaria o som atrasado o tempo todo por essa partida.
///
/// Aqui os decodificadores abrem na construção (antes de o dispositivo pedir o
/// primeiro bloco) e a primeira leitura pula para `base + tempo decorrido no
/// relógio`: o primeiro som que sai já é o do instante que o vídeo mostra.
///
/// Depois, o dispositivo lê adiantado (o buffer dele), então a posição fica à
/// frente do relógio. Se ela ficar para trás mais que `maxLag` (faltou som: a
/// máquina engasgou mais que o buffer, um decodificador demorou a abrir no meio
/// da reprodução), a leitura pula de novo para o relógio, em vez de deixar o som
/// atrasado até o próximo play.
class ClockedMixer
{
public:
    /// `clock` é uma cópia do relógio do Player (o `QElapsedTimer` é monotônico e
    /// pode ser lido de outra thread); `base` é a posição da timeline na partida dele.
    ClockedMixer(QList<AudioClip> clips, Micros base, const QElapsedTimer &clock);

    /// Atraso do som em relação ao relógio a partir do qual a leitura pula para alcançá-lo.
    static constexpr Micros maxLag = 60'000;

    void read(float *out, int frames);
    /// Quanto a primeira leitura pulou para alcançar o relógio (0 antes dela).
    Micros skipped() const { return m_skipped; }
    /// Quantas vezes, depois da primeira leitura, o som ficou para trás e pulou para o relógio.
    int resyncs() const { return m_resyncs; }
    qint64 position() const { return m_mixer.position(); }

private:
    AudioMixer m_mixer;
    Micros m_base;
    QElapsedTimer m_clock;
    bool m_aligned = false;
    Micros m_skipped = 0;
    int m_resyncs = 0;
};

} // namespace of
