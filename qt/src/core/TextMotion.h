#pragma once

#include "LinearImage.h"
#include "Motion.h"

#include <QRectF>

namespace of {

/// Uma letra, palavra ou linha desenhada à parte. Coordenadas da imagem do texto (as mesmas da imagem estática,
/// com as margens), y para baixo.
struct TextSprite {
    /// Valores sRGB pré-multiplicados (`LinearImage::fromImageEncoded`), como os sprites sem gerência de cor do
    /// Swift; `origin` é o canto superior esquerdo na imagem do texto.
    LinearImage image;
    QPoint origin;
    bool isNull() const { return image.isNull(); }
    QRectF rect() const { return QRectF(origin, QSizeF(image.width(), image.height())); }
};

struct TextUnit {
    /// Preenchimento (com sublinhado, negrito sintético e emoji) e contorno.
    TextSprite fill;
    TextSprite stroke;
    QRectF bounds;
    /// Primeiro caractere (grafema) da unidade (para a máquina de escrever).
    int character = 0;
    /// Palavra do primeiro caractere: o destaque das legendas.
    int word = 0;
    int line = 0;
    /// Centro da linha da unidade (animação "espaçamento").
    double lineCenterX = 0;
    /// Centro e largura da célula da letra e linha de base (animação "decodificar").
    double cellCenterX = 0;
    double cellWidth = 0;
    double baseline = 0;
};

/// Sprites de um texto quebrado em unidades, guardados em cache por conteúdo, canvas e granularidade.
struct TextUnits {
    QSize size;
    QList<TextUnit> units;
    /// Faixa vertical de cada linha (com o contorno), para o recorte do "revelar por máscara".
    QList<QRectF> lineBands;
    TextSprite background;
    TextStyle style;
    double fontPx = 0;
    QPointF shadowOffset;
    double shadowBlur = 0;
    int characterCount = 0;
    /// Caixa do texto (sem o fundo) e, por linha, o trecho horizontal com letras.
    QRectF textBox;
    QList<std::pair<double, double>> lineSpans;
    /// Folga e raio do fundo, em pixels.
    QSizeF backgroundPadding;
    double backgroundRadius = 0;
};

/// Glifo avulso do "decodificar", com a origem na linha de base e no começo do avanço.
struct ScrambleGlyph {
    TextSprite fill;
    TextSprite stroke;
    double advance = 0;
};

/// Composição por quadro das animações de texto por unidade, do destaque da palavra e das revelações das
/// legendas. Mesmas regras e números do `TextMotion.swift`.
namespace TextMotion {
/// Caracteres sorteados no "decodificar".
inline const QString scrambleCharacters = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#%&*@$");

/// Sprites do texto (em cache). Vazio se o texto estiver vazio.
std::shared_ptr<const TextUnits> units(const TextContent &content, QSize canvas, TextGranularity granularity);
/// Glifos do conjunto do "decodificar" no estilo do texto (em cache).
std::shared_ptr<const QList<ScrambleGlyph>> scrambleGlyphs(const TextStyle &style, QSize canvas);

struct Image {
    /// Luz linear pré-multiplicada, com a opacidade do estilo. Pode ser maior que `size` (o que as unidades
    /// posadas, a sombra e os loops levam para fora), com a mesma margem dos dois lados: o centro é o mesmo.
    LinearImage image;
    /// Tamanho nominal (o da imagem estática).
    QSize size;
};
/// Imagem do texto no instante.
std::optional<Image> image(const TextContent &content, QSize canvas, const AnimationState &state,
                           TextGranularity granularity);

/// Índices dos glifos sorteáveis para uma célula: os que cabem com até 30% de folga; se forem menos de 4, os
/// 4 de largura mais próxima.
QList<int> scrambleCandidates(const QList<ScrambleGlyph> &glyphs, double cellWidth);
/// Caractere aleatório determinístico da unidade `index` no passo de tempo `step` (~20 trocas/s).
int scrambleIndex(int index, int step, int count);

/// Instante em que cada unidade (letra) aparece na digitação.
QList<Micros> typingTimes(const QList<CaptionWord> &words, const QList<TextUnit> &units);
/// Quanto cada palavra está destacada (`color` e `emphasis`, 0...1).
struct HighlightAmounts {
    QList<double> color;
    QList<double> emphasis;
};
HighlightAmounts highlightAmounts(const QList<CaptionWord> &words, HighlightMode mode, Micros local);

/// Duração da transição do destaque entre palavras, centrada no início da palavra.
inline constexpr Micros highlightRamp = 80'000;
/// Duração da entrada de cada palavra no "surgir".
inline constexpr Micros riseDuration = 250'000;
/// O cursor da digitação fica este tempo depois da última letra e some.
inline constexpr Micros cursorHold = 300'000;
/// Parado (sem letra nova) por mais que isto, o cursor pisca.
inline constexpr Micros cursorIdle = 500'000;
/// Transição do fundo da digitação para cada letra (começa `typingLead` antes de ela aparecer).
inline constexpr Micros typingGrow = 80'000;
inline constexpr Micros typingLead = 60'000;

/// Halo: a própria camada desfocada, por baixo dela (`amount` 0...1, `sigma` em pixels).
void applyGlow(LinearImage &image, double amount, double sigma);
/// Reflexo de luz: faixa diagonal (~20°) que atravessa a imagem da esquerda para a direita conforme `position`
/// vai de 0 a 1; luz quente em source-atop dentro da camada e um brilho que vaza em volta. `linear`: a imagem
/// está em luz linear (fotos e vídeos); senão, em valores sRGB (texto). `rect`: a caixa que a faixa atravessa
/// (vazio = a imagem toda; numa imagem com folga, a parte sem a folga).
void applyShine(LinearImage &image, double position, double strength, bool linear, const QRectF &rect = {});
} // namespace TextMotion

} // namespace of
