#pragma once

#include "Project.h"
#include "VideoDecoder.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>

namespace of {

/// Opções internas do render, para as prévias (`AnimationPreview`).
struct RenderOptions {
    /// Fotos em memória por material, no lugar do arquivo (que pode nem existir).
    QHash<QUuid, QImage> stills;
    /// Fundo transparente: o fundo do canvas não é desenhado e a imagem sai com alfa.
    bool transparent = false;
};

/// Desenha um quadro do projeto na CPU, como o FrameRenderer/OpenFrameCompositor
/// do Swift: fundo (preto, cor ou desfoque), faixas de vídeo com ajustes,
/// filtros, efeitos, encaixe × transformação × animação × geometria, opacidade
/// e transições, e por fim os textos. O mesmo renderizador serve ao preview,
/// à exportação e às miniaturas. Thread-safe.
///
/// Trabalha em luz linear (como o Core Image) e devolve sRGB 8 bits opaco
/// (`Format_ARGB32_Premultiplied`).
class Renderer
{
public:
    Renderer();
    ~Renderer();

    /// Renderizador compartilhado (miniaturas, tela inicial).
    static Renderer &shared();

    /// Quadro do instante `t` no tamanho `size` (que define a escala: os
    /// efeitos usam altura/1080).
    QImage render(const Project &project, Micros t, QSize size);
    QImage render(const Project &project, Micros t, QSize size, const RenderOptions &options);

    /// Esquece decodificadores e fotos (ao trocar de projeto).
    void clearCaches();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// Miniaturas de pré-visualização para a aba de filtros.
namespace EffectPreview {
QImage filtered(const QImage &image, FilterPreset preset);
} // namespace EffectPreview

/// Miniatura do projeto para a tela inicial, como no Swift: o quadro em 1/3 da
/// duração (no máximo 2 s), com lado menor 360. O app grava o JPEG com
/// `ProjectLibrary::writeThumbnail`.
namespace ProjectThumbnail {
/// Nula se o projeto estiver vazio.
QImage image(const Project &project);
/// JPEG com qualidade 80 (vazio se o projeto estiver vazio).
QByteArray jpeg(const Project &project);
} // namespace ProjectThumbnail

} // namespace of
