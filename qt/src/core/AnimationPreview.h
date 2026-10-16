#pragma once

#include "Project.h"

#include <QImage>
#include <variant>

namespace of {

/// Miniaturas animadas para o inspetor e para a janela das legendas, feitas com o mesmo `Renderer` da
/// exportação (prévia = resultado). Como o `AnimationPreview.swift`.
namespace AnimationPreview {
/// Texto ou imagem (foto/quadro do vídeo) a animar.
using Subject = std::variant<TextContent, QImage>;

struct Part {
    enum class Kind { Entry, Exit, Loop };
    Kind kind = Kind::Entry;
    AnimationKind animation = AnimationKind::Fade;
    LoopKind loop = LoopKind::Float;

    static Part entry(AnimationKind k) { return {Kind::Entry, k, LoopKind::Float}; }
    static Part exit(AnimationKind k) { return {Kind::Exit, k, LoopKind::Float}; }
    static Part loopOf(LoopKind k) { return {Kind::Loop, AnimationKind::Fade, k}; }
    bool operator==(const Part &) const = default;
};

/// Duração da animação de entrada/saída na prévia e pausa no repouso.
inline constexpr Micros animationDuration = 600'000;
inline constexpr Micros hold = 600'000;

/// `count` quadros de um ciclo que se repete. Entrada: a animação e depois uma pausa no repouso. Saída: repouso e
/// depois a saída. Loop: dois períodos, na intensidade máxima. O texto é centralizado e redimensionado para caber
/// (pela fonte, não pela escala, para ficar nítido); a imagem é encaixada. Fundo transparente
/// (`Format_ARGB32_Premultiplied`). Vazio se o texto estiver vazio, a imagem nula ou `count` ≤ 0.
QList<QImage> frames(const Subject &subject, const Part &part, QSize size, int count);
} // namespace AnimationPreview

} // namespace of
