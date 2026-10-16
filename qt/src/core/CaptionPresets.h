#pragma once

#include "Enums.h"

#include <QString>

namespace of {

/// Segmentação das legendas.
enum class CaptionLayout {
    /// Frases: até 2 linhas, ~42 caracteres por linha (no 16:9), até ~5 s.
    Phrases,
    /// Destaque: frases de 1 linha (~32 caracteres no 16:9) com destaque da palavra falada.
    Highlight,
    /// Impacto: 2 a 4 palavras por vez, fonte grande no centro, com animação de entrada.
    Punch,
    /// Empilhado: 2 a 5 palavras em 2 ou 3 linhas curtas, com a palavra-chave sozinha numa linha. É o único em
    /// que o texto leva `\n`.
    Stacked
};
OF_ENUM_KEYS(CaptionLayout, "phrases", "highlight", "punch", "stacked")
QString displayName(CaptionLayout);

enum class CaptionPresetGroup { Classic, Reveal, Editorial };
OF_ENUM_KEYS(CaptionPresetGroup, "classic", "reveal", "editorial")
QString displayName(CaptionPresetGroup);

/// Visuais prontos das legendas automáticas: a segmentação, o estilo, o destaque, a palavra-chave e as
/// animações de cada um.
enum class CaptionPreset { Phrases, Highlight, Punch, Rise, Guided, Fill, Typing, Editorial, Cinema, Stacked };
OF_ENUM_KEYS(CaptionPreset, "phrases", "highlight", "punch", "rise", "guided", "fill", "typing", "editorial", "cinema",
             "stacked")
QString displayName(CaptionPreset);
/// Uma linha para o cartão da janela.
QString detail(CaptionPreset);
CaptionPresetGroup group(CaptionPreset);
/// A segmentação usada.
CaptionLayout layout(CaptionPreset);
/// Se a cor de destaque escolhida na janela é usada: na palavra atual (Destaque, Surgir, Leitura guiada),
/// no preenchimento, no cursor da digitação ou na palavra-chave (Editorial, Empilhado).
bool usesAccent(CaptionPreset);
/// O visual clássico de cada segmentação (a API antiga por `CaptionLayout`).
CaptionPreset presetFor(CaptionLayout);

} // namespace of
