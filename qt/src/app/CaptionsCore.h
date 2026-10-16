#pragma once

#include "Project.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>
#include <optional>

// Única ligação das legendas automáticas com o núcleo: `WhisperModel`,
// `ModelStore`, `Transcriber`, `CaptionBuilder`, `CaptionPreset` e
// `Track::isAutoCaptions`, e o `Renderer` nas prévias dos cartões. A transcrição
// real chega na etapa 3 do 04; até lá, use `OPENFRAME_FAKE_TRANSCRIPTION=1`.

namespace of::CaptionsCore {

struct ModelInfo {
    QString id;
    QString name;
    QString detail;
    qint64 bytes = 0;
};
QList<ModelInfo> models();
/// Recomendado na janela (nesta versão, só CPU: o Equilibrado).
QString recommendedModel();
/// Endereço HTTPS do arquivo do modelo (vazio se o id for desconhecido).
QUrl modelUrl(const QString &id);

struct Language {
    QString code;
    QString name;
};
QList<Language> languages();

struct PresetInfo {
    QString id;
    QString name;
    QString detail;
    QString group;
    QString groupName;
    /// Visual da amostra enquanto os quadros da prévia são gerados: phrases, highlight, punch, stacked.
    QString layout;
    bool usesAccent = false;
};
QList<PresetInfo> presets();
/// Cor padrão do destaque da palavra.
QColor defaultHighlight();

/// Há uma faixa de legendas automáticas no projeto.
bool hasAutoCaptions(const Project &p);
bool isAutoCaption(const Project &p, const QUuid &id);
/// Copia o estilo de `source` para todas as legendas automáticas: o destaque (com a
/// revelação) e o visual da palavra-chave, sem as palavras escolhidas de cada uma.
void applyStyleToAll(Project &p, const Segment &source);

/// Prévia animada dos cartões da janela (o `CaptionPreviewFrames` do Mac): uma frase
/// falsa com tempo por palavra, montada pelo `CaptionBuilder` com o estilo padrão
/// da predefinição e desenhada pelo `Renderer` com fundo transparente.
namespace Preview {
/// Segundos de um ciclo, quadros por ciclo e o quadro parado (no meio da frase, com
/// uma palavra destacada).
inline constexpr double cycle = 3.6;
inline constexpr int count = 72;
inline constexpr int restIndex = 24;
/// Proporção do app mais próxima (em escala logarítmica, para 9:16 e 16:9 ficarem simétricas).
AspectRatio nearestAspect(double value);
/// Pixels do quadro: cabe em 384×216 (o cartão 16:9 com escala 2).
QSize pixelSize(AspectRatio aspect);
/// `accent` só nas predefinições que usam a cor do destaque.
QList<QImage> frames(const QString &preset, const std::optional<QColor> &accent, AspectRatio aspect);
} // namespace Preview

/// Download dos modelos e transcrição (`ModelStore` e `Transcriber`).
class Engine : public QObject
{
    Q_OBJECT

public:
    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    bool isInstalled(const QString &model) const;
    void download(const QString &model);
    /// O que já veio fica guardado; o próximo download continua dali.
    void cancelDownload(const QString &model);
    bool remove(const QString &model, QString *error);

    /// Trecho da timeline a transcrever (vazio = erro em `error`).
    std::optional<TimeRange> sourceRange(const Project &p, const std::optional<QUuid> &segment, QString *error) const;
    /// Transcreve e monta as legendas (`progress`, depois `finished`).
    void transcribe(const Project &p, const std::optional<QUuid> &segment, const QString &model,
                    const QString &language, const QString &preset);
    void cancel();
    /// Põe o último resultado no projeto (um passo de desfazer, feito por quem chama).
    void apply(Project &p, const QString &preset, const QColor &accent, bool replacing, TimeRange range) const;

signals:
    void downloadProgress(const QString &model, double fraction);
    /// `error` vazio = pronto; cancelado não emite.
    void downloadFinished(const QString &model, const QString &error);
    /// `phase` = "mixing" ou "transcribing"; `overall` = progresso das duas juntas.
    void progress(const QString &phase, double overall);
    /// `count` legendas prontas para `apply`, ou `error`.
    void finished(int count, const QString &error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace of::CaptionsCore
