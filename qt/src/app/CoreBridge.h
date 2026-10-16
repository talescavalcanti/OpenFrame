#pragma once

#include "ExportSettings.h"
#include "Project.h"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <optional>

// Adaptador entre o app e as partes do núcleo que chegam por etapas (mídia,
// render, reprodução e exportação). Cada parte é detectada pelo cabeçalho
// (`__has_include`), para o app compilar em qualquer etapa do núcleo.

namespace of {

namespace Bridge {
/// Extensões aceitas no seletor de importação (como o `MediaProbe.isSupported`).
bool isSupportedMedia(const QString &path);
std::optional<Material> probe(const QString &path, QString *error);
/// Miniaturas de um material (10 quadros de vídeo ou a foto); roda fora da thread principal.
QList<QImage> thumbnails(const Material &m);
/// Picos de áudio (`MediaCache::peaksPerSecond` por segundo); roda fora da thread principal.
QVector<float> peaks(const Material &m);
/// Um quadro do projeto renderizado; imagem nula se o render não estiver disponível.
QImage renderFrame(const Project &p, Micros t, QSize size);
/// Miniatura do projeto em JPEG (vazia se não houver render ou o projeto estiver vazio).
QByteArray projectThumbnail(const Project &p);
/// Miniatura com o filtro aplicado (aba Filtros); nula se não houver render.
QImage filterPreview(const QImage &base, FilterPreset preset);

/// Segmento ↔ JSON, com as mesmas chaves do arquivo `.openframe`.
QJsonObject segmentJson(const Segment &s);
std::optional<Segment> segmentFromJson(const QJsonObject &o);

bool hasPlayer();
bool hasExporter();
} // namespace Bridge

/// Preview e reprodução (o `of::Player` do núcleo). Sem ele, o preview mostra
/// quadros parados renderizados sob demanda, ou nada.
class PreviewEngine : public QObject
{
    Q_OBJECT

public:
    explicit PreviewEngine(QObject *parent = nullptr);
    ~PreviewEngine() override;

    void setProject(const Project &p);
    void clear();
    void play();
    void pause();
    void seek(Micros t);
    bool isPlaying() const;
    Micros time() const;
    /// Liga ou desliga o som do preview (a gravação toca a timeline muda).
    void setAudioEnabled(bool enabled);
    /// Lado menor do quadro do preview em pixels (degraus do `PreviewResolution`).
    void setPreviewShortSide(int side);
    int previewShortSide() const;

signals:
    void timeChanged(Micros t);
    void finished();

private:
    void renderStill();

    struct Impl;
    Impl *d;
};

/// Exportação em segundo plano (o `of::Exporter` do núcleo).
class ExportJob : public QObject
{
    Q_OBJECT

public:
    explicit ExportJob(QObject *parent = nullptr);
    ~ExportJob() override;

    bool start(const Project &p, const ExportSettings &settings, const QString &path, QString *error);
    void cancel();

signals:
    void progress(double value);
    /// `ok` falso com `error` vazio = cancelada.
    void finished(bool ok, const QString &error);

private:
    struct Impl;
    Impl *d;
};

} // namespace of
