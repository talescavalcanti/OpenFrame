#pragma once

#include "CaptionPresets.h"
#include "Project.h"

#include <QSet>

namespace of {

/// Uma legenda: o texto e as palavras dela, com tempos da timeline. O texto não tem `\n` (a quebra é a da
/// largura da caixa, com `balanceLines`), exceto no Empilhado; entre um artigo ou preposição e a palavra
/// seguinte vai um espaço sem quebra (U+00A0), para a linha não terminar neles.
struct CaptionCue {
    QString text;
    Micros start = 0;
    Micros end = 0;
    QList<TranscribedWord> words;
    bool operator==(const CaptionCue &) const = default;
};

/// Quebra as palavras transcritas em legendas e as coloca no projeto. Porte do `CaptionBuilder` do Swift,
/// com as mesmas regras e números. Diferença: sem o etiquetador gramatical do macOS, a palavra-chave é
/// escolhida pelo tamanho, pelo fim de frase e pelas listas de palavras funcionais e fracas (`keywords`).
/// Usa o `TextRenderer` para medir: precisa de `QGuiApplication`.
namespace CaptionBuilder {

/// Limites de cada segmentação.
struct Limits {
    int lines = 1;
    /// Caracteres de uma linha típica no 16:9 (a largura padrão da caixa).
    int lineChars = 0;
    Micros maxDuration = 0;
    int maxWords = std::numeric_limits<int>::max();
    /// Número de palavras preferido (impacto e empilhado).
    std::optional<int> idealWords;
};
Limits limits(CaptionLayout layout);
Limits limits(CaptionPreset preset);

/// Pausa que já vale como ponto de quebra.
inline constexpr Micros pause = 350'000;
/// Silêncio que sempre separa duas legendas.
inline constexpr Micros forcedBreak = 1'200'000;
inline constexpr Micros minDuration = 700'000;
inline constexpr Micros closeGap = 250'000;
/// Espaço sem quebra: liga um artigo ou preposição à palavra seguinte.
inline constexpr QChar glue{0x00A0};

/// Medidas da quebra em em (frações do tamanho da fonte): cada palavra, o espaço e a largura da caixa.
struct Measure {
    QList<double> widths;
    double space = 0;
    double line = 0;
    /// Quanto cada palavra alarga se virar palavra-chave (0 nas que não podem).
    QList<double> extras;

    Measure(const QList<TranscribedWord> &words, const TextStyle &style, double aspect,
            const std::optional<WordEmphasis> &emphasis = std::nullopt);
    /// Folga para as palavras-chave das palavras [from, to): uma, ou duas com mais de 6 palavras.
    double reserve(int from, int to) const;
    /// Linhas da quebra gulosa das palavras [from, to), com a folga das palavras-chave. `glued(k)`: não quebra
    /// depois de `k`.
    int lineCount(int from, int to, const std::function<bool(int)> &glued = {}) const;
    double length(int from, int to) const;
};

/// Quebra natural nas pausas e na pontuação, cabendo nas linhas da segmentação (medidas com a fonte do visual
/// na largura da caixa, que depende da proporção), sem artigo ou preposição solto no fim, com duração mínima
/// de ~0,7 s e vãos curtos fechados.
QList<CaptionCue> cues(const QList<TranscribedWord> &words, CaptionPreset preset, double aspect = 16.0 / 9);
/// A segmentação do visual clássico de `layout`.
QList<CaptionCue> cues(const QList<TranscribedWord> &words, CaptionLayout layout, double aspect = 16.0 / 9);

/// Custo de uma legenda com as palavras [i, j).
double cueCost(const QList<TranscribedWord> &w, int i, int j, const Limits &limits, const Measure &measure);
/// Quanto custa terminar uma legenda depois de `word` (0 = ótimo ponto de quebra).
double breakCost(const TranscribedWord &word, const TranscribedWord &next);
/// Texto da legenda numa linha só, com os artigos e preposições ligados à palavra seguinte quando couber.
QString text(int from, int to, const QList<TranscribedWord> &words, const Measure &measure, const Limits &limits);
/// Linhas do Empilhado ([início, fim) de cada uma): grupos seguidos de 1 ou 2 palavras, em 2 ou 3 linhas.
QList<std::pair<int, int>> stackLines(const QStringList &words, const QSet<int> &keywords);

/// Palavras-chave de uma legenda (Editorial e Empilhado): no máximo uma (duas com mais de 6 palavras, não
/// vizinhas), entre as que `canBeKeyword`. Peso: letras (até 12), −3 nos advérbios em "-mente", +2 no fim de
/// frase ou de legenda; no empate, a mais adiante. Determinística.
QList<int> keywords(const CaptionCue &cue);
/// Pode ser palavra-chave: 5 letras ou mais, fora das palavras funcionais e das fracas.
bool canBeKeyword(const QString &word);

bool endsSentence(const QString &word);
bool endsClause(const QString &word);
/// Artigos, preposições e conjunções curtas (pt, en, es, fr, it, de).
bool isFunctionWord(const QString &word);

/// Tira espaços, junta pontuação solta à palavra anterior e garante a ordem dos tempos.
QList<TranscribedWord> normalized(const QList<TranscribedWord> &words);
/// Duração mínima (estendendo até a próxima, sem sobrepor) e vãos curtos fechados.
void retime(QList<CaptionCue> &cues);

/// Cria (ou substitui, se `replacing`) uma faixa de texto só das legendas automáticas e devolve o id dela.
/// Cada legenda vira um segmento de texto com o estilo, o destaque, a palavra-chave e as animações da
/// predefinição, e `words` (relativo ao segmento). `accent` vai para onde a predefinição usa a cor (sem ela,
/// amarelo). Substituir troca só as legendas automáticas que tocam `range` (sem ele, o trecho das novas).
QUuid apply(const QList<CaptionCue> &cues, CaptionPreset preset, const std::optional<RGBA> &accent, Project &project,
            bool replacing, const std::optional<TimeRange> &range = std::nullopt);
/// Igual, com o visual clássico de `layout` e, se dado, outro estilo.
QUuid apply(const QList<CaptionCue> &cues, CaptionLayout layout, const std::optional<TextStyle> &style,
            Project &project, bool replacing, const std::optional<TimeRange> &range = std::nullopt);

/// Destaque da palavra falada de cada visual (vazio = sem destaque).
std::optional<WordHighlight> highlight(CaptionPreset preset, const std::optional<RGBA> &accent);
/// Estilo da palavra-chave de cada visual, sem os índices (vazio = sem palavra-chave).
std::optional<WordEmphasis> emphasis(CaptionPreset preset, const TextStyle &style, const std::optional<RGBA> &accent);
/// Entrada e saída de cada visual.
void animate(Segment &seg, CaptionPreset preset, Micros duration);

/// Largura máxima da caixa das legendas (fração do quadro), em qualquer proporção.
inline constexpr double widestBox = 0.9;
/// Largura padrão da caixa do Empilhado em frações da altura do quadro, e a fonte em frações dessa largura.
inline constexpr double stackedBlock = 0.36;
inline constexpr double stackedFont = 0.2031;

/// Estilo padrão de cada visual no 16:9, usado também nas prévias.
TextStyle defaultStyle(CaptionPreset preset);
/// O estilo padrão na proporção `aspect` (largura/altura) do canvas: em pé, a fonte é medida pelo lado menor;
/// a caixa mantém a largura em em do 16:9, sem passar de 90% do quadro. No Empilhado, o bloco fica em ~45%
/// da altura em qualquer proporção.
TextStyle defaultStyle(CaptionPreset preset, double aspect);
} // namespace CaptionBuilder

} // namespace of
