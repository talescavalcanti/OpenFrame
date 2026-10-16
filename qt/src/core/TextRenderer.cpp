#include "TextRenderer.h"

#include "FontCatalog.h"
#include "LinearImage.h"
#include "TextShapes.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <cmath>

namespace of {

namespace textshapes {

/// Escolhe o estilo da família como o Core Text: o estilo pedido (ou o normal),
/// depois a variante com negrito/itálico se a família tiver; senão, sintéticos.
FontFace makeFont(const TextStyle &style, int pixelSize)
{
    // Fora do Mac, as fontes do Mac viram as livres que o núcleo distribui (`FontCatalog::resolve`).
    const FontCatalog::ResolvedFont resolved = FontCatalog::resolve(style.fontName, style.fontFace);
    const QString family = resolved.family;
    const QString wantedFace = resolved.face;
    struct Face {
        QString name;
        int weight;
        bool italic;
    };
    QList<Face> faces;
    for (const QString &s : QFontDatabase::styles(family))
        faces.append({s, QFontDatabase::weight(family, s), QFontDatabase::italic(family, s)});

    std::optional<Face> current;
    if (!wantedFace.isEmpty())
        for (const Face &f : faces)
            if (f.name.compare(wantedFace, Qt::CaseInsensitive) == 0)
                current = f;
    if (!current)
        for (const Face &f : faces)
            if (!f.italic && (!current || std::abs(f.weight - QFont::Normal) < std::abs(current->weight - QFont::Normal)))
                current = f;

    FontFace face;
    if (current && style.bold && current->weight < QFont::DemiBold) {
        std::optional<Face> bold;
        for (const Face &f : faces)
            if (f.italic == current->italic && f.weight >= QFont::Bold &&
                (!bold || std::abs(f.weight - QFont::Bold) < std::abs(bold->weight - QFont::Bold)))
                bold = f;
        if (bold)
            current = bold;
        else
            face.syntheticBold = true;
    }
    if (current && style.italic && !current->italic) {
        std::optional<Face> italic;
        for (const Face &f : faces)
            if (f.italic && (!italic || std::abs(f.weight - current->weight) < std::abs(italic->weight - current->weight)))
                italic = f;
        if (italic)
            current = italic;
        else
            face.syntheticItalic = true;
    }
    if (!current) {
        // Família sem estilos listados: o Qt sintetiza o que faltar.
        face.syntheticBold = style.bold;
        face.syntheticItalic = style.italic;
    }
    face.font = current ? QFontDatabase::font(family, current->name, 12) : QFont(family);
    face.font.setPixelSize(pixelSize);
    face.font.setHintingPreference(QFont::PreferNoHinting);
    face.font.setKerning(true);
    return face;
}

QString styleKey(const TextStyle &s)
{
    const auto n = [](double v) { return QString::number(v, 'g', 17); };
    const auto c = [&](const RGBA &v) { return n(v.r) + u',' + n(v.g) + u',' + n(v.b) + u',' + n(v.a); };
    return QStringList{s.fontName, s.fontFace, n(s.fontSize), QString::number(s.bold), QString::number(s.italic),
                       QString::number(s.underline), QString::number(int(s.textCase)), c(s.color), n(s.opacity),
                       c(s.strokeColor), n(s.strokeWidth), n(s.letterSpacing), n(s.lineSpacing),
                       QString::number(s.backgroundEnabled), c(s.backgroundColor), n(s.backgroundRadius),
                       n(s.backgroundPaddingX), n(s.backgroundPaddingY), QString::number(s.shadow), c(s.shadowColor),
                       n(s.shadowDistance), n(s.shadowAngle), n(s.shadowBlur), QString::number(int(s.alignment)),
                       n(s.maxWidth), QString::number(s.balanceLines), QString::number(s.fitLines)}
        .join(u'|');
}

/// Chave do conteúdo que muda o desenho: texto, estilo e palavra-chave (os tempos não mudam a imagem estática).
QString contentKey(const QString &text, const TextContent &content)
{
    QString key = text + u'\x1f' + styleKey(content.style);
    if (const auto &e = content.emphasis) {
        QStringList words;
        for (const int w : e->words)
            words.append(QString::number(w));
        key += u'\x1f' + words.join(u',') + u'|' + e->fontName + u'|' + e->fontFace + u'|' +
               QString::number(e->scale, 'g', 17);
        if (e->color)
            key += QStringLiteral("|%1,%2,%3,%4").arg(e->color->r).arg(e->color->g).arg(e->color->b).arg(e->color->a);
    }
    return key;
}

/// Trechos (UTF-16, [início, fim)) das palavras do texto: a divisão de `TextContent::wordsOf`.
QList<std::pair<int, int>> wordRanges(const QString &text)
{
    QList<std::pair<int, int>> ranges;
    int start = -1;
    for (int i = 0; i <= text.size(); ++i) {
        const bool blank = i == text.size() || text[i].isSpace();
        if (blank && start >= 0) {
            ranges.append({start, i});
            start = -1;
        } else if (!blank && start < 0) {
            start = i;
        }
    }
    return ranges;
}

/// Tamanho mínimo da fonte no QTextLayout. O QFont só aceita pixels inteiros: montar o texto grande e escalar
/// o resultado deixa o erro de arredondamento das fontes ampliadas ou reduzidas (palavra-chave, `fitLines`) em
/// ~0,1%. Sem hinting, o layout é linear no tamanho.
constexpr int minLayoutPx = 512;

int layoutPixels(double fontPx) { return std::max(minLayoutPx, int(std::lround(fontPx))); }

/// Faixa de escala de cada linha no `fitLines`.
constexpr double fitMin = 0.6, fitMax = 2.5;

/// Número de linhas da quebra automática na largura `width` (pixels de layout).
int countLines(QTextLayout &layout, double width)
{
    int count = 0;
    layout.beginLayout();
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        ++count;
    }
    layout.endLayout();
    return count;
}

/// Monta o texto: fonte, palavras-chave, linhas ajustadas à largura (`fitLines`) e quebra (comum, equilibrada
/// ou só nos `\n`). Mesmas regras do `TextRenderer.layout` do Swift.
Layout buildLayout(const QString &text, const TextStyle &style, QSize canvas, const std::optional<WordEmphasis> &emphasis)
{
    Layout l;
    l.fontPx = std::max(4.0, style.fontSize * canvas.height());
    const int layoutPx = layoutPixels(l.fontPx);
    l.s = l.fontPx / layoutPx;
    l.face = makeFont(style, layoutPx);
    l.kern = style.letterSpacing * l.fontPx;
    l.peakFontPx = l.fontPx;
    const double s = l.s;

    // Fonte de cada posição: 0 = a do texto, 1 = a da palavra-chave; e o tamanho em pixels.
    const int n = int(text.size());
    QList<char> kind(n, 0);
    QList<double> size(n, l.fontPx);
    TextStyle accentStyle = style;
    double accentPx = l.fontPx;
    if (emphasis && !emphasis->words.isEmpty()) {
        accentStyle.fontName = emphasis->fontName;
        accentStyle.fontFace = emphasis->fontFace;
        accentStyle.bold = false;
        accentStyle.italic = false;
        accentPx = l.fontPx * std::clamp(emphasis->scale, 0.2, 5.0);
        const auto words = wordRanges(text);
        QList<int> picked = emphasis->words;
        std::sort(picked.begin(), picked.end());
        picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
        for (const int w : picked) {
            if (w < 0 || w >= words.size())
                continue;
            for (int i = words[w].first; i < words[w].second; ++i) {
                kind[i] = 1;
                size[i] = accentPx;
            }
            l.accent.append(words[w]);
        }
        if (!l.accent.isEmpty()) {
            l.peakFontPx = std::max(l.peakFontPx, accentPx);
            l.accentColor = emphasis->color;
        }
    }

    // Fontes já criadas por (tipo, tamanho de layout).
    QHash<QString, QFont> fonts;
    const auto fontFor = [&](int k, double px) {
        const int layoutSize = std::max(1, int(std::lround(px / s)));
        const QString key = QStringLiteral("%1|%2").arg(k).arg(layoutSize);
        if (const auto it = fonts.constFind(key); it != fonts.constEnd())
            return *it;
        QFont font = k == 0 ? l.face.font : makeFont(accentStyle, layoutSize).font;
        font.setPixelSize(layoutSize);
        if (style.letterSpacing != 0)
            font.setLetterSpacing(QFont::AbsoluteSpacing, style.letterSpacing * px / s);
        fonts.insert(key, font);
        return font;
    };
    const auto formats = [&](int from, int to) {
        QList<QTextLayout::FormatRange> ranges;
        for (int i = from; i < to;) {
            int j = i + 1;
            while (j < to && kind[j] == kind[i] && size[j] == size[i])
                ++j;
            QTextLayout::FormatRange r;
            r.start = i - from;
            r.length = j - i;
            r.format.setFont(fontFor(kind[i], size[i]));
            ranges.append(r);
            i = j;
        }
        return ranges;
    };
    const auto naturalHeight = [&](double px) {
        const QFontMetricsF m(fontFor(0, px));
        return (m.ascent() + m.descent() + m.leading()) * s;
    };
    // Altura fixa de linha quando há espaçamento extra; senão, a natural da fonte.
    const auto fixedHeight = [&](double px) -> std::optional<double> {
        if (style.lineSpacing == 0)
            return std::nullopt;
        return std::max(px * 0.3, naturalHeight(px) + style.lineSpacing * px);
    };

    // Parágrafos (separados por `\n`) e a altura fixa de cada um.
    QList<std::pair<int, int>> paragraphs;
    for (int i = 0, start = 0; i <= n; ++i)
        if (i == n || text[i] == u'\n') {
            paragraphs.append({start, i});
            start = i + 1;
        }
    QList<std::optional<double>> paragraphHeight(paragraphs.size(), fixedHeight(l.fontPx));

    const double boxWidth = std::max(l.fontPx, style.maxWidth * canvas.width());
    if (style.fitLines) {
        // Cada linha ocupa a largura da caixa, dentro da faixa de escala, com a altura de linha da própria fonte.
        for (int p = 0; p < paragraphs.size(); ++p) {
            const auto [from, to] = paragraphs[p];
            if (to <= from)
                continue;
            QTextLayout measure(text.mid(from, to - from));
            measure.setFormats(formats(from, to));
            QTextOption option;
            option.setWrapMode(QTextOption::NoWrap);
            measure.setTextOption(option);
            measure.beginLayout();
            QTextLine line = measure.createLine();
            line.setLineWidth(1e6);
            measure.endLayout();
            const double natural = line.naturalTextWidth() * s - l.kern;
            if (natural <= 0)
                continue;
            const double k = std::clamp(boxWidth / natural, fitMin, fitMax);
            for (int i = from; i < to; ++i)
                size[i] *= k;
            paragraphHeight[p] = fixedHeight(l.fontPx * k);
            l.peakFontPx = std::max(l.peakFontPx, l.fontPx * k);
        }
    }

    // O QTextLayout só quebra a linha no separador de linha (U+2028), não no "\n";
    // a troca é de um caractere por um, então os índices continuam valendo.
    QString layoutText = text;
    layoutText.replace(u'\n', QChar::LineSeparator);
    l.layout = std::make_shared<QTextLayout>(layoutText, l.face.font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    l.layout->setTextOption(option);
    if (style.letterSpacing != 0 || !l.accent.isEmpty() || style.fitLines)
        l.layout->setFormats(formats(0, n));
    else {
        QFont font = l.face.font;
        if (l.kern != 0)
            font.setLetterSpacing(QFont::AbsoluteSpacing, l.kern / s);
        l.layout->setFont(font);
    }

    double wrap = boxWidth;
    if (style.fitLines) {
        // As linhas já têm a largura da caixa: só os `\n` quebram (o que passar dos limites de escala, sai).
        wrap = 1e6;
    } else if (style.balanceLines) {
        // Menor largura com o mesmo número de linhas (`text-wrap: balance`), até meio pixel.
        const int lines = countLines(*l.layout, wrap / s);
        if (lines > 1) {
            double lo = 0, hi = wrap;
            while (hi - lo > 0.5) {
                const double mid = (lo + hi) / 2;
                (countLines(*l.layout, mid / s) <= lines ? hi : lo) = mid;
            }
            wrap = hi;
        }
    }

    double y = 0, widest = 0;
    l.layout->beginLayout();
    for (;;) {
        QTextLine line = l.layout->createLine();
        if (!line.isValid())
            break;
        line.setLeadingIncluded(true);
        line.setLineWidth(wrap / s);
        const int start = line.textStart();
        int paragraph = 0;
        while (paragraph + 1 < paragraphs.size() && paragraphs[paragraph].second < start)
            ++paragraph;
        const double natural = line.height() * s;
        const double height = paragraphHeight[paragraph].value_or(natural);
        // Altura natural: a linha de base fica a (descida + entrelinha) do fim da linha. Altura fixa: o Core Text
        // tira (ou põe) a diferença meio a meio em cima e embaixo, com a metade de cima arredondada.
        const double lineDescent = line.descent() * s + line.leading() * s;
        const double baseline = paragraphHeight[paragraph]
                                    ? y + line.ascent() * s - std::round((natural - height) / 2)
                                    : y + height - lineDescent;
        l.lines.append({start, line.textLength(), line.naturalTextWidth() * s, y, height, baseline});
        widest = std::max(widest, line.naturalTextWidth() * s);
        y += height;
    }
    l.layout->endLayout();
    l.textSize = QSizeF(std::ceil(widest) + 1, std::ceil(y) + 1);
    return l;
}

/// Desfoque da sombra nos valores sRGB pré-multiplicados (como o Core Graphics).
void blurRaw(QImage &image, double sigma)
{
    if (sigma < 0.3)
        return;
    LinearImage buffer(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            buffer.at(x, y) = {qRed(src[x]) / 255.0f, qGreen(src[x]) / 255.0f, qBlue(src[x]) / 255.0f,
                               qAlpha(src[x]) / 255.0f};
    }
    buffer.blur(sigma);
    for (int y = 0; y < image.height(); ++y) {
        auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const Pixel &p = buffer.at(x, y);
            const auto q = [](float v) { return std::clamp(int(std::lround(v * 255)), 0, 255); };
            const int a = q(p.a);
            dst[x] = qRgba(std::min(q(p.r), a), std::min(q(p.g), a), std::min(q(p.b), a), a);
        }
    }
}

QColor qcolor(const RGBA &c)
{
    return QColor::fromRgbF(float(std::clamp(c.r, 0.0, 1.0)), float(std::clamp(c.g, 0.0, 1.0)),
                            float(std::clamp(c.b, 0.0, 1.0)), float(std::clamp(c.a, 0.0, 1.0)));
}

/// Limites (em unidades UTF-16) de cada letra visível (grafema).
QList<int> graphemeEnds(const QString &text)
{
    QList<int> ends;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    while (finder.toNextBoundary() > 0)
        ends.append(int(finder.position()));
    return ends;
}

std::pair<QList<Glyph>, QList<LineInfo>> glyphInfo(const Layout &l, const QString &text, const TextStyle &style)
{
    QList<Glyph> glyphs;
    QList<LineInfo> lines;
    const double s = l.s;
    const QTextLayout &layout = *l.layout;
    const auto isAccent = [&](qsizetype index) {
        return std::any_of(l.accent.cbegin(), l.accent.cend(),
                           [&](const auto &r) { return index >= r.first && index < r.second; });
    };
    // O espaço entre letras também vai depois da última da linha: desconta a sobra.
    const double shift = style.alignment == TextAlignment::Left     ? 0
                         : style.alignment == TextAlignment::Center ? l.kern / 2
                                                                    : l.kern;
    for (int k = 0; k < l.lines.size(); ++k) {
        const Layout::Line &info = l.lines[k];
        const double offset = style.alignment == TextAlignment::Left     ? 0
                              : style.alignment == TextAlignment::Center ? (l.textSize.width() - info.width) / 2
                                                                         : l.textSize.width() - info.width;
        const QTextLine line = layout.lineForTextPosition(info.start);
        lines.append({info.baseline, line.ascent() * s, line.descent() * s});
        const auto runs = layout.glyphRuns(info.start, info.length,
                                           QTextLayout::RetrieveGlyphIndexes | QTextLayout::RetrieveGlyphPositions |
                                               QTextLayout::RetrieveStringIndexes);
        for (const QGlyphRun &run : runs) {
            const QRawFont raw = run.rawFont();
            const auto indexes = run.glyphIndexes();
            const auto positions = run.positions();
            const auto strings = run.stringIndexes();
            const auto advances = raw.advancesForGlyphIndexes(indexes);
            for (qsizetype i = 0; i < indexes.size(); ++i) {
                Glyph g;
                g.utf16 = int(i < strings.size() ? strings[i] : info.start);
                g.line = k;
                g.raw = raw;
                g.index = indexes[i];
                // Posição no layout → pixels: x com o alinhamento, y na linha de base.
                g.position = QPointF((positions[i].x() - line.x()) * s + offset + shift,
                                     info.baseline + (positions[i].y() - line.y() - line.ascent()) * s);
                g.advance = advances[i].x() * s;
                const QChar ch = g.utf16 < text.size() ? text[g.utf16] : QChar();
                g.blank = ch.isSpace();
                g.accent = isAccent(g.utf16);
                const QPainterPath outline = raw.pathForGlyph(indexes[i]);
                if (outline.isEmpty()) {
                    g.color = !g.blank;
                } else {
                    // A palavra-chave tem a fonte própria (sem negrito nem itálico sintéticos).
                    QTransform t;
                    t.translate(g.position.x(), g.position.y());
                    t.scale(s, s);
                    if (l.face.syntheticItalic && !g.accent)
                        t.shear(-syntheticSlant, 0);
                    g.outline = t.map(outline);
                }
                glyphs.append(g);
            }
        }
    }
    return {glyphs, lines};
}

Metrics metrics(const Layout &l, const TextStyle &style)
{
    Metrics m;
    const double fontPx = l.fontPx;
    // Margens: o bastante para contorno, fundo, sombra e partes das letras que
    // passam da caixa tipográfica (itálico, linhas apertadas).
    m.strokeOut = std::max(0.0, style.strokeWidth) / 100 * fontPx / 2 +
                  (l.face.syntheticBold ? syntheticBold * fontPx / 2 : 0);
    m.bgX = style.backgroundEnabled ? std::max(0.0, style.backgroundPaddingX) * fontPx : 0;
    m.bgY = style.backgroundEnabled ? std::max(0.0, style.backgroundPaddingY) * fontPx : 0;
    const double distance = std::max(0.0, style.shadowDistance) * fontPx;
    const double angle = style.shadowAngle * M_PI / 180;
    // Ângulo com y para cima (90 = cima); na imagem, y cresce para baixo.
    m.shadowOffset = QPointF(std::cos(angle) * distance, -std::sin(angle) * distance);
    m.shadowBlur = style.shadow ? std::max(0.0, style.shadowBlur) * fontPx : 0;
    const double shadowX = style.shadow ? std::abs(m.shadowOffset.x()) + m.shadowBlur * 1.5 : 0;
    const double shadowY = style.shadow ? std::abs(m.shadowOffset.y()) + m.shadowBlur * 1.5 : 0;
    // A palavra-chave (às vezes itálica) e as linhas ampliadas pedem margens pela maior fonte.
    const double peak = l.peakFontPx;
    const double slant = (style.italic || l.face.syntheticItalic || !l.accent.isEmpty()) ? peak * 0.25 : 0;
    const double tight = std::max(0.0, -style.lineSpacing) * peak;
    const double base = std::ceil(peak * 0.35);
    m.padX = std::max(base, std::ceil(fontPx * 0.12 + std::max(m.strokeOut, m.bgX) + shadowX + slant));
    m.padY = std::max(base, std::ceil(fontPx * 0.12 + std::max(m.strokeOut, m.bgY) + shadowY + tight));
    m.width = int(l.textSize.width() + m.padX * 2);
    m.height = int(l.textSize.height() + m.padY * 2);
    return m;
}

QRectF Shapes::bounds() const
{
    QRectF r = glyphs.boundingRect().united(accent.boundingRect()).united(underlines.boundingRect());
    for (const ColorRun &run : colorRuns)
        for (qsizetype i = 0; i < run.indexes.size(); ++i) {
            const QRectF b = run.font.boundingRect(run.indexes[i]);
            r = r.united(b.translated(run.positions[i]));
        }
    return r;
}

Shapes makeShapes(const QList<const Glyph *> &glyphs, const QList<QRectF> &underlines, const Layout &l)
{
    Shapes shapes;
    shapes.glyphs.setFillRule(Qt::WindingFill);
    shapes.accent.setFillRule(Qt::WindingFill);
    QHash<QString, int> runOf;
    for (const Glyph *g : glyphs) {
        if (g->blank)
            continue;
        if (g->color) {
            QRawFont raw = g->raw;
            raw.setPixelSize(raw.pixelSize() * l.s);
            const QString key = raw.familyName() + u'|' + raw.styleName() + u'|' + QString::number(raw.pixelSize());
            auto it = runOf.find(key);
            if (it == runOf.end()) {
                it = runOf.insert(key, int(shapes.colorRuns.size()));
                shapes.colorRuns.append({raw, {}, {}});
            }
            shapes.colorRuns[*it].indexes.append(g->index);
            shapes.colorRuns[*it].positions.append(g->position);
            continue;
        }
        (g->accent ? shapes.accent : shapes.glyphs).addPath(g->outline);
    }
    for (const QRectF &r : underlines)
        shapes.underlines.addRect(r);
    return shapes;
}

void drawShapes(QPainter &p, const Shapes &shapes, const TextStyle &style, const Layout &l, const Metrics &m,
                bool stroke, bool fill)
{
    p.setRenderHint(QPainter::Antialiasing);
    if (stroke && m.strokeOut > 0) {
        const QPen pen(qcolor(style.strokeColor), m.strokeOut * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.strokePath(shapes.glyphs, pen);
        p.strokePath(shapes.accent, pen);
        p.strokePath(shapes.underlines, pen);
    }
    if (!fill)
        return;
    const QColor color = qcolor(style.color);
    if (l.face.syntheticBold && !shapes.glyphs.isEmpty())
        p.strokePath(shapes.glyphs, QPen(color, syntheticBold * l.fontPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(shapes.glyphs, color);
    p.fillPath(shapes.underlines, color);
    if (!shapes.accent.isEmpty())
        p.fillPath(shapes.accent, qcolor(l.accentColor.value_or(style.color)));
    // Glifos coloridos (emoji) não têm contorno vetorial: são desenhados como estão.
    for (const Shapes::ColorRun &run : shapes.colorRuns) {
        QGlyphRun glyphRun;
        glyphRun.setRawFont(run.font);
        glyphRun.setGlyphIndexes(run.indexes);
        glyphRun.setPositions(run.positions);
        p.drawGlyphRun(QPointF(0, 0), glyphRun);
    }
}

QRectF underlineRect(const LineInfo &line, double minX, double maxX, double fontPx)
{
    // Posição e espessura em em, como `underline_offset`/`underline_width` do CapCut.
    return QRectF(minX, line.baseline + fontPx * 0.12, maxX - minX, fontPx * 0.05);
}

QPainterPath backgroundPath(const Layout &l, const Metrics &m, const TextStyle &style)
{
    const QRectF box = QRectF(QPointF(0, 0), l.textSize).adjusted(-m.bgX, -m.bgY, m.bgX, m.bgY);
    const double radius = std::min(std::max(0.0, style.backgroundRadius) * l.fontPx, std::min(box.width(), box.height()) / 2);
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);
    return path;
}

} // namespace textshapes

using namespace textshapes;

TextRenderer &TextRenderer::shared()
{
    static TextRenderer renderer;
    return renderer;
}

QString TextRenderer::cased(const QString &text, TextCase textCase)
{
    const QLocale locale;
    switch (textCase) {
    case TextCase::None: return text;
    case TextCase::Upper: return locale.toUpper(text);
    case TextCase::Lower: return locale.toLower(text);
    case TextCase::Title: {
        // Primeira letra de cada palavra em maiúscula, o resto em minúscula.
        QString out = locale.toLower(text);
        bool start = true;
        for (qsizetype i = 0; i < out.size(); ++i) {
            const QChar ch = out[i];
            if (ch.isLetterOrNumber()) {
                if (start)
                    out.replace(i, 1, locale.toUpper(QString(ch)));
                start = false;
            } else if (ch.isSpace() || ch.isPunct()) {
                start = ch != u'\'' && ch != u'’';
            }
        }
        return out;
    }
    }
    return text;
}

QImage TextRenderer::image(const TextContent &content, QSize canvas, double reveal)
{
    const QString text = cased(content.text, content.style.textCase);
    const QList<int> ends = graphemeEnds(text);
    const int characters = int(ends.size());
    if (characters == 0 || canvas.height() <= 0)
        return {};
    const int visible = reveal >= 1 ? characters : int(std::ceil(characters * reveal));
    const QString key = QStringLiteral("%1\x1f%2x%3\x1f%4")
                            .arg(contentKey(text, content))
                            .arg(canvas.width())
                            .arg(canvas.height())
                            .arg(visible);
    {
        QMutexLocker locker(&m_lock);
        if (const QImage *cached = m_cache.object(key))
            return *cached;
    }
    const QImage image = render(text, content.style, content.emphasis, canvas, visible);
    QMutexLocker locker(&m_lock);
    m_cache.insert(key, new QImage(image));
    return image;
}

QImage TextRenderer::render(const QString &text, const TextStyle &style, const std::optional<WordEmphasis> &emphasis,
                            QSize canvas, int visibleCharacters)
{
    const Layout l = buildLayout(text, style, canvas, emphasis);
    const Metrics m = metrics(l, style);
    const auto [glyphs, lines] = glyphInfo(l, text, style);

    // Letras visíveis e o sublinhado de cada linha (até a última letra visível dela).
    const QList<int> ends = graphemeEnds(text);
    const int cut = visibleCharacters <= 0 ? 0 : ends[std::min(visibleCharacters, int(ends.size())) - 1];
    QList<const Glyph *> shown;
    QList<double> minX(lines.size(), INFINITY), maxX(lines.size(), -INFINITY);
    for (const Glyph &g : glyphs) {
        if (g.utf16 >= cut)
            continue;
        shown.append(&g);
        if (!g.blank) {
            minX[g.line] = std::min(minX[g.line], g.position.x());
            maxX[g.line] = std::max(maxX[g.line], g.position.x() + g.advance);
        }
    }
    QList<QRectF> underlines;
    if (style.underline)
        for (int k = 0; k < lines.size(); ++k)
            if (maxX[k] > minX[k])
                underlines.append(underlineRect(lines[k], minX[k], maxX[k], l.fontPx));
    const Shapes shapes = makeShapes(shown, underlines, l);

    // Letras, contorno e sublinhado numa camada só: a sombra sai uma vez, do conjunto.
    QImage ink(m.width, m.height, QImage::Format_ARGB32_Premultiplied);
    ink.fill(Qt::transparent);
    {
        QPainter p(&ink);
        p.translate(m.padX, m.padY);
        drawShapes(p, shapes, style, l, m, true, true);
    }

    QImage group(m.width, m.height, QImage::Format_ARGB32_Premultiplied);
    group.fill(Qt::transparent);
    {
        QPainter p(&group);
        p.setRenderHint(QPainter::Antialiasing);
        if (style.backgroundEnabled)
            p.fillPath(backgroundPath(l, m, style).translated(m.padX, m.padY), qcolor(style.backgroundColor));
        if (style.shadow) {
            QImage shadow(m.width, m.height, QImage::Format_ARGB32_Premultiplied);
            shadow.fill(Qt::transparent);
            {
                QPainter sp(&shadow);
                sp.drawImage(m.shadowOffset, ink);
                sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                sp.fillRect(shadow.rect(), qcolor(style.shadowColor));
            }
            // O raio do Core Graphics corresponde a cerca de 2 desvios-padrão.
            blurRaw(shadow, m.shadowBlur / 2);
            p.drawImage(0, 0, shadow);
        }
        p.drawImage(0, 0, ink);
    }

    const double opacity = std::clamp(style.opacity, 0.0, 1.0);
    if (opacity >= 1)
        return group;
    QImage faded(m.width, m.height, QImage::Format_ARGB32_Premultiplied);
    faded.fill(Qt::transparent);
    QPainter p(&faded);
    p.setOpacity(opacity);
    p.drawImage(0, 0, group);
    return faded;
}

std::optional<QSizeF> TextRenderer::boxSize(const TextContent &content, QSize canvas)
{
    const QString text = cased(content.text, content.style.textCase);
    if (text.isEmpty() || canvas.height() <= 0)
        return std::nullopt;
    const QString key = QStringLiteral("box\x1f%1\x1f%2x%3").arg(contentKey(text, content)).arg(canvas.width()).arg(canvas.height());
    {
        QMutexLocker locker(&m_lock);
        if (const QSizeF *cached = m_sizes.object(key))
            return *cached;
    }
    const TextStyle &style = content.style;
    const Layout l = buildLayout(text, style, canvas, content.emphasis);
    const double bgX = style.backgroundEnabled ? std::max(0.0, style.backgroundPaddingX) * l.fontPx : 0;
    const double bgY = style.backgroundEnabled ? std::max(0.0, style.backgroundPaddingY) * l.fontPx : 0;
    const QSizeF size(l.textSize.width() + bgX * 2, l.textSize.height() + bgY * 2);
    QMutexLocker locker(&m_lock);
    m_sizes.insert(key, new QSizeF(size));
    return size;
}

std::optional<QSize> TextRenderer::imageSize(const TextContent &content, QSize canvas)
{
    const QString text = cased(content.text, content.style.textCase);
    if (text.isEmpty() || canvas.height() <= 0)
        return std::nullopt;
    const QString key = QStringLiteral("img\x1f%1\x1f%2x%3").arg(contentKey(text, content)).arg(canvas.width()).arg(canvas.height());
    {
        QMutexLocker locker(&m_lock);
        if (const QSizeF *cached = m_sizes.object(key))
            return cached->toSize();
    }
    const Metrics m = metrics(buildLayout(text, content.style, canvas, content.emphasis), content.style);
    const QSize size(m.width, m.height);
    QMutexLocker locker(&m_lock);
    m_sizes.insert(key, new QSizeF(size));
    return size;
}

double TextRenderer::rasterScale(const TextContent &content, QSize canvas, double peak)
{
    const double wanted = std::max(1.0, std::ceil(std::abs(content.style.scale) * peak * 8) / 8);
    const auto size = imageSize(content, canvas);
    if (!size || std::max(size->width(), size->height()) <= 0)
        return wanted;
    return std::min(wanted, maxRasterSide / std::max(size->width(), size->height()));
}

double TextRenderer::lineWidth(const QString &text, const TextStyle &style, double fontPx)
{
    const int layoutPx = layoutPixels(fontPx);
    const double s = fontPx / layoutPx;
    QFont font = makeFont(style, layoutPx).font;
    if (style.letterSpacing != 0)
        font.setLetterSpacing(QFont::AbsoluteSpacing, style.letterSpacing * fontPx / s);
    QTextLayout layout(cased(text, style.textCase), font);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
        layout.endLayout();
        return 0;
    }
    line.setLineWidth(1e9);
    layout.endLayout();
    // Como o `CTLineGetTypographicBounds`: com os espaços e o espaçamento do fim.
    return line.horizontalAdvance() * s;
}

QStringList TextRenderer::lineTexts(const TextContent &content, QSize canvas)
{
    const QString text = cased(content.text, content.style.textCase);
    if (text.isEmpty() || canvas.height() <= 0)
        return {};
    const Layout l = buildLayout(text, content.style, canvas, content.emphasis);
    QStringList out;
    for (const Layout::Line &line : l.lines)
        out.append(text.mid(line.start, line.length).trimmed());
    return out;
}

} // namespace of
