#pragma once

// Peças internas do desenho de texto, compartilhadas pelo caminho estático (`TextRenderer`) e pelo caminho por
// unidade (`TextMotion`), como no Swift (`layout`, `metrics`, `glyphInfo`, `makeShapes`, `draw`). Tudo em pixels
// da caixa de texto, com y para baixo; a imagem do texto é a caixa mais as margens `padX`/`padY`.

#include "Effects.h"

#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QRawFont>
#include <QTextLayout>
#include <memory>

class QPainter;

namespace of::textshapes {

/// Inclinação do itálico sintético (12°).
inline const double syntheticSlant = 0.21255656167002213; // tan(12°)
/// Espessura extra do negrito sintético, em em.
inline constexpr double syntheticBold = 0.03;

struct FontFace {
    QFont font;
    bool syntheticBold = false;
    bool syntheticItalic = false;
};

/// Escolhe o estilo da família como o Core Text (com as fontes do Mac trocadas pelas livres fora dele).
FontFace makeFont(const TextStyle &style, int pixelSize);
QString styleKey(const TextStyle &s);
/// Chave do conteúdo que muda o desenho: texto, estilo e palavra-chave.
QString contentKey(const QString &text, const TextContent &content);
/// Trechos (UTF-16, [início, fim)) das palavras do texto: a divisão de `TextContent::wordsOf`.
QList<std::pair<int, int>> wordRanges(const QString &text);

/// Texto montado: fontes por trecho, linhas e caixa, em pixels.
struct Layout {
    FontFace face;
    double fontPx = 0;
    /// Maior fonte do texto (palavra-chave e linhas ampliadas pelo `fitLines`): as margens contam com ela.
    double peakFontPx = 0;
    double s = 1;
    /// Espaço extra entre letras da fonte base, em pixels.
    double kern = 0;
    std::shared_ptr<QTextLayout> layout;
    struct Line {
        int start, length;
        double width, top, height, baseline;
    };
    QList<Line> lines;
    QSizeF textSize;
    /// Trechos das palavras-chave e a cor delas (vazia = a do texto).
    QList<std::pair<int, int>> accent;
    std::optional<RGBA> accentColor;
};
Layout buildLayout(const QString &text, const TextStyle &style, QSize canvas, const std::optional<WordEmphasis> &emphasis);

/// Um glifo posicionado na caixa de texto.
struct Glyph {
    /// Contorno já posicionado (vazio nos glifos coloridos e nos espaços).
    QPainterPath outline;
    bool accent = false;
    /// Glifo colorido (emoji): desenhado com a fonte, sem contorno vetorial.
    bool color = false;
    QRawFont raw;
    quint32 index = 0;
    /// Início do avanço, na linha de base.
    QPointF position;
    double advance = 0;
    int line = 0;
    /// Posição (UTF-16) do caractere no texto.
    int utf16 = 0;
    bool blank = false;
};

struct LineInfo {
    double baseline = 0;
    double ascent = 0;
    double descent = 0;
};

/// Glifos de todas as linhas e as linhas (linha de base e alturas), na ordem do layout.
std::pair<QList<Glyph>, QList<LineInfo>> glyphInfo(const Layout &l, const QString &text, const TextStyle &style);

/// Margens e medidas do desenho, iguais no caminho estático e no por unidade.
struct Metrics {
    double strokeOut = 0;
    double bgX = 0, bgY = 0;
    /// Na imagem (y para baixo).
    QPointF shadowOffset;
    double shadowBlur = 0;
    double padX = 0, padY = 0;
    int width = 0, height = 0;
};
Metrics metrics(const Layout &l, const TextStyle &style);

struct Shapes {
    QPainterPath glyphs, accent, underlines;
    struct ColorRun {
        QRawFont font;
        QList<quint32> indexes;
        QList<QPointF> positions;
    };
    QList<ColorRun> colorRuns;
    /// Caixa das formas (sem contorno).
    QRectF bounds() const;
};
Shapes makeShapes(const QList<const Glyph *> &glyphs, const QList<QRectF> &underlines, const Layout &l);
/// Desenha contorno e/ou preenchimento das formas (com o negrito sintético e os emoji).
void drawShapes(QPainter &p, const Shapes &shapes, const TextStyle &style, const Layout &l, const Metrics &m,
                bool stroke, bool fill);
/// Retângulo do sublinhado de uma linha, de `minX` a `maxX`.
QRectF underlineRect(const LineInfo &line, double minX, double maxX, double fontPx);
/// Fundo arredondado, em coordenadas da caixa de texto.
QPainterPath backgroundPath(const Layout &l, const Metrics &m, const TextStyle &style);

/// Desfoque nos valores sRGB pré-multiplicados (como o Core Graphics).
void blurRaw(QImage &image, double sigma);
QColor qcolor(const RGBA &c);
/// Limites (em unidades UTF-16) de cada letra visível (grafema).
QList<int> graphemeEnds(const QString &text);

} // namespace of::textshapes
