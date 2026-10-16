#pragma once

#include "Color.h"
#include "Enums.h"
#include "Timing.h"

#include <QList>
#include <QString>
#include <optional>

// Legendas automáticas: tempos das palavras, destaque da palavra falada e palavra-chave.
// Mesmos campos e padrões do `Model/Captions.swift`.

namespace of {

/// Tempo de cada palavra de uma legenda, relativo ao início do segmento.
struct CaptionWord {
    QString text;
    Micros start = 0;
    Micros end = 0;
    bool operator==(const CaptionWord &) const = default;
};

enum class HighlightMode {
    /// Só a palavra falada agora.
    Current,
    /// As palavras já faladas ficam destacadas.
    Progressive
};
OF_ENUM_KEYS(HighlightMode, "current", "progressive")

/// Como as palavras aparecem conforme são ditas (junto com o destaque).
enum class WordReveal {
    /// Todas visíveis desde o início da legenda.
    None,
    /// Surgir: cada palavra aparece quando é dita.
    Rise,
    /// Leitura guiada: as não ditas ficam com `dimOpacity`; cada uma acende ao ser dita.
    Dim,
    /// Preenchimento: a cor do destaque enche a palavra da esquerda para a direita.
    Fill,
    /// Digitação: as letras aparecem distribuídas no tempo da palavra, com um cursor.
    Type
};
OF_ENUM_KEYS(WordReveal, "none", "rise", "dim", "fill", "type")
QString displayName(WordReveal);

/// Destaque da palavra falada ("karaokê") num texto com `words`. Na leitura, campos ausentes ficam
/// com o padrão e um `mode`/`reveal` desconhecido vira o padrão.
struct WordHighlight {
    /// Cor da palavra destacada.
    RGBA color = RGBA::yellow();
    /// 1 = sem crescer.
    double scale = 1.12;
    /// "Pílula" atrás da palavra.
    bool backgroundEnabled = false;
    RGBA backgroundColor{0.1, 0.45, 1, 1};
    HighlightMode mode = HighlightMode::Current;
    /// Revelação pela fala. No preenchimento e na digitação, a cor vai para o preenchimento e para o
    /// cursor; a escala e a pílula continuam valendo em todas.
    WordReveal reveal = WordReveal::None;
    /// Opacidade das palavras ainda não ditas na leitura guiada.
    double dimOpacity = 0.35;
    bool operator==(const WordHighlight &) const = default;
};

/// Palavra-chave (visual Editorial): algumas palavras em outra fonte, maiores e numa cor própria.
/// Na leitura, campos ausentes ficam com o padrão.
struct WordEmphasis {
    /// Índices das palavras enfatizadas (a divisão de `TextContent::words(of:)`). Os fora do texto são ignorados.
    QList<int> words;
    /// Serifa do Mac; fora dele, o `TextRenderer` troca por uma equivalente.
    QString fontName = QStringLiteral("Didot");
    QString fontFace = QStringLiteral("Italic");
    /// Relativo ao tamanho do texto.
    double scale = 1.3;
    /// Vazio = a cor do texto.
    std::optional<RGBA> color;
    bool operator==(const WordEmphasis &) const = default;
};

/// Palavra reconhecida pela transcrição, com tempos da timeline.
struct TranscribedWord {
    QString text;
    Micros start = 0;
    Micros end = 0;
    double probability = 1;
    bool operator==(const TranscribedWord &) const = default;
};

} // namespace of
