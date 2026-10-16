#pragma once

#include "Timing.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace of {

/// Grava blocos de áudio mono num M4A (AAC, 48 kHz, mono) pelo `MediaWriter`, reamostrando se a entrada vier em
/// outra taxa, e guarda o nível e os picos para a forma de onda ao vivo. Separado da entrada (o microfone) para os
/// testes alimentarem um seno. Thread-safe: `append` vem da thread do áudio; o resto, de qualquer uma.
class RecordingWriter
{
public:
    static constexpr int sampleRate = 48000;
    /// Picos por segundo da forma de onda ao vivo, como o `Waveform`.
    static constexpr int peaksPerSecond = 50;

    /// Cria o arquivo (e a pasta). `inputSampleRate` é a taxa dos blocos de `append`.
    static std::unique_ptr<RecordingWriter> create(const QString &path, int inputSampleRate = sampleRate,
                                                   QString *error = nullptr);
    ~RecordingWriter();

    /// Acrescenta `frames` amostras mono (na taxa de entrada). Falso depois de um erro do codificador.
    bool append(const float *mono, int frames);
    /// Pico desde a última leitura (0…1).
    float takeLevel();
    /// Picos de cada 1/50 s já gravado (0…1, sem normalizar: o nível real do microfone).
    QList<float> peaks() const;
    /// Duração gravada até agora.
    Micros duration() const;
    /// Fecha o arquivo (falha = arquivo apagado).
    bool finish(QString *error = nullptr);
    /// Fecha e apaga o arquivo.
    void cancel();
    QString path() const;

private:
    RecordingWriter();
    struct Impl;
    std::unique_ptr<Impl> d;
};

struct AudioInput {
    /// `QAudioDevice::id()`.
    QString id;
    QString name;
    bool operator==(const AudioInput &) const = default;
};

/// Grava o microfone (`QAudioSource`, numa thread própria) num arquivo M4A/AAC 48 kHz mono, com o nível e a forma
/// de onda ao vivo. A interface põe o resultado na timeline com `TimelineEditor::addRecording`. Não usa o
/// `QMediaRecorder` (ele precisa do plugin de mídia FFmpeg do Qt, que os pacotes não levam).
///
/// `OPENFRAME_FAKE_RECORDING` (para as capturas, que não têm microfone): `1` grava uma "voz" sintética em tempo
/// real no lugar do microfone; `sem-permissao` e `sem-microfone` simulam os erros.
class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    /// Cancela a gravação em andamento (apaga o arquivo).
    ~AudioRecorder() override;

    /// Microfones disponíveis, na ordem do sistema.
    static QList<AudioInput> inputs();
    /// O microfone padrão do sistema.
    static std::optional<QString> defaultInput();
    /// Pede a permissão do microfone (`QMicrophonePermission`, onde existir: no Mac, a janela do sistema). Onde não
    /// há permissão por app (Windows, Linux), devolve verdadeiro; lá, o acesso negado aparece como erro no `start`.
    /// `done` chega na thread de `context`.
    static void requestPermission(QObject *context, const std::function<void(bool)> &done);

    /// Começa a gravar o microfone `input` (vazio = o padrão) em `path` (a pasta é criada). Falso (com
    /// `finished(false, 0, mensagem)` logo depois) se não deu: sem permissão, sem microfone, falha ao abrir.
    bool start(const std::optional<QString> &input, const QString &path);
    /// Para e fecha o arquivo: `finished(true, duração, "")` (ou `false` com a mensagem, se o arquivo falhou).
    void stop();
    /// Para e apaga o arquivo: `finished(false, 0, "")`.
    void cancel();
    bool isRecording() const;

    /// Picos de cada 1/50 s já gravado (0…1); continuam depois do `stop`, até o próximo `start`.
    QList<float> peaks() const;
    Micros duration() const;

signals:
    /// Pico desde o último aviso (0…1), ~30 vezes por segundo durante a gravação.
    void levelChanged(float level);
    /// Novos picos em `peaks()`.
    void peaksChanged();
    /// Cancelado = `ok` falso com `error` vazio. Um erro durante a gravação (microfone desligado) também termina.
    void finished(bool ok, Micros duration, const QString &error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace of
