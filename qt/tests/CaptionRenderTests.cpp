// Render das legendas: porte das suítes "Legendas: revelação pela fala" (CaptionRevealTests.swift), "Legendas
// automáticas: destaque da palavra" (WordHighlightRenderTests) e "Legendas: palavra-chave e linhas ajustadas"
// (CaptionEmphasisTests, em CaptionTests.swift).
#include "CaptionBuilder.h"
#include "Motion.h"
#include "Renderer.h"
#include "TextMotion.h"
#include "TextRenderer.h"
#include "TestSupport.h"
#include "TimelineEditor.h"

#include <QElapsedTimer>
#include <QTest>
#include <cmath>

using namespace of;
namespace TE = of::TimelineEditor;
namespace CB = of::CaptionBuilder;

namespace {

/// Bytes RGBA 8 bits pré-multiplicados, linha 0 no topo, como o `Bitmap` dos testes Swift.
struct Bitmap {
    int width = 0, height = 0;
    QByteArray data;
    explicit Bitmap(const QImage &image)
    {
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        width = rgba.width();
        height = rgba.height();
        data.resize(qsizetype(width) * height * 4);
        for (int y = 0; y < height; ++y)
            memcpy(data.data() + qsizetype(y) * width * 4, rgba.constScanLine(y), size_t(width) * 4);
    }
    int at(qsizetype i) const { return uchar(data[i]); }
    int r(int x, int y) const { return at((qsizetype(y) * width + x) * 4); }
    int g(int x, int y) const { return at((qsizetype(y) * width + x) * 4 + 1); }
    int b(int x, int y) const { return at((qsizetype(y) * width + x) * 4 + 2); }
};

/// Colunas [first, last].
struct Span {
    int first = 0, last = -1;
    int count() const { return last - first + 1; }
};

using ColorTest = std::function<bool(int, int, int)>;
const ColorTest ink = [](int r, int g, int b) { return std::max({r, g, b}) > 40; };
const ColorTest blue = [](int r, int, int b) { return b > 150 && r < 90; };
const ColorTest white = [](int r, int g, int b) { return r > 200 && g > 200 && b > 200; };
const ColorTest red = [](int r, int g, int b) { return r > 200 && g < 60 && b < 60; };
const ColorTest yellow = [](int r, int g, int b) { return r > 200 && g > 160 && b < 90; };
const ColorTest strongBlue = [](int r, int, int b) { return b > 200 && r < 90; };

int count(const Bitmap &b, Span span, const ColorTest &test)
{
    int n = 0;
    for (int y = 0; y < b.height; ++y)
        for (int x = span.first; x <= span.last; ++x)
            n += test(b.r(x, y), b.g(x, y), b.b(x, y));
    return n;
}

Span all(const Bitmap &b) { return {0, b.width - 1}; }

/// Soma do brilho no trecho.
qint64 light(const Bitmap &b, Span span)
{
    qint64 sum = 0;
    for (int y = 0; y < b.height; ++y)
        for (int x = span.first; x <= span.last; ++x)
            sum += std::max({b.r(x, y), b.g(x, y), b.b(x, y)});
    return sum;
}

int maxDiff(const Bitmap &a, const Bitmap &b, Span span)
{
    int worst = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = span.first; x <= span.last; ++x)
            worst = std::max({worst, std::abs(a.r(x, y) - b.r(x, y)), std::abs(a.g(x, y) - b.g(x, y)),
                              std::abs(a.b(x, y) - b.b(x, y))});
    return worst;
}

/// Pixels que passam no teste e a média de x deles (0...1).
std::pair<int, double> pixels(const Bitmap &b, const ColorTest &test)
{
    int n = 0;
    qint64 sum = 0;
    for (int y = 0; y < b.height; ++y)
        for (int x = 0; x < b.width; ++x)
            if (test(b.r(x, y), b.g(x, y), b.b(x, y)))
                ++n, sum += x;
    return {n, n > 0 ? double(sum) / n / b.width : 0};
}

int inkRows(const Bitmap &b)
{
    int rows = 0;
    for (int y = 0; y < b.height; ++y)
        for (int x = 0; x < b.width; ++x)
            if (b.r(x, y) > 128) {
                ++rows;
                break;
            }
    return rows;
}

/// Última coluna com algum pixel que passa no teste (-1 se nenhuma).
int lastColumn(const Bitmap &b, const ColorTest &test)
{
    for (int x = b.width - 1; x >= 0; --x)
        if (count(b, {x, x}, test) > 0)
            return x;
    return -1;
}

QString str(const std::pair<int, double> &p) { return QStringLiteral("(%1, %2)").arg(p.first).arg(p.second, 0, 'f', 3); }

QList<TranscribedWord> spokenWords(const QString &text, Micros start = 0)
{
    Micros t = start;
    QList<TranscribedWord> words;
    for (const QString &token : text.split(u' ', Qt::SkipEmptyParts)) {
        const Micros end = t + 200'000 + Micros(token.size()) * 20'000;
        words.append({token, t, end, 0.9});
        t = end + (CB::endsSentence(token) ? 500'000 : 50'000);
    }
    return words;
}

double median(QList<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

const QSize size(640, 360);
const RGBA accent{0, 0.4, 1, 1};

/// Três palavras de 0,9 s a cada 1 s, a partir de 0.
QList<CaptionWord> threeWords()
{
    QList<CaptionWord> words;
    for (int i = 0; i < 3; ++i)
        words.append({QStringLiteral("w%1").arg(i), Micros(i) * 1'000'000, Micros(i + 1) * 1'000'000 - 100'000});
    return words;
}

TextStyle captionStyle()
{
    TextStyle style;
    style.y = 0;
    style.fontSize = 0.16;
    style.shadow = false;
    return style;
}

/// "MAR SOL CEU" com a revelação; o segmento dura 3,4 s.
Project revealProject(WordReveal reveal, const std::function<void(WordHighlight &)> &change = {},
                      bool highlight = true, const QString &text = QStringLiteral("MAR SOL CEU"))
{
    Project p;
    p.canvas.aspect = AspectRatio::R16x9;
    TextContent content(text, captionStyle());
    content.words = threeWords();
    if (highlight) {
        WordHighlight h;
        h.color = accent;
        h.scale = 1;
        h.reveal = reveal;
        if (change)
            change(h);
        content.highlight = h;
    }
    TE::addText(content, p, 0, 3'400'000);
    return p;
}

/// "MAR SOL CEU" com o destaque padrão; o segmento dura 3 s.
Project highlightProject(const std::function<void(WordHighlight &)> &change = {},
                         const std::function<void(Segment &)> &segment = {},
                         const QString &text = QStringLiteral("MAR SOL CEU"))
{
    Project p;
    p.canvas.aspect = AspectRatio::R16x9;
    TextContent content(text, captionStyle());
    content.words = threeWords();
    WordHighlight h;
    if (change)
        change(h);
    content.highlight = h;
    const QUuid id = TE::addText(content, p, 0, 3'000'000);
    if (segment)
        TE::update(p, id, segment);
    return p;
}

Segment &textSegment(Project &p)
{
    for (Track &t : p.tracks)
        if (t.kind == TrackKind::Text)
            return t.segments[0];
    qFatal("sem faixa de texto");
}

Bitmap render(const Project &p, Micros t, QSize s = size) { return Bitmap(Renderer::shared().render(p, t, s)); }

/// Colunas com tinta de cada palavra (pelo desenho estático, todas visíveis).
QList<Span> wordSpans()
{
    const Bitmap b = render(revealProject(WordReveal::None, {}, false), 1'500'000);
    QList<Span> spans;
    int start = -1, gap = 0;
    for (int x = 0; x < b.width; ++x) {
        bool any = false;
        for (int y = 0; y < b.height && !any; ++y)
            any = b.r(x, y) > 60;
        if (any) {
            if (start < 0)
                start = x;
            gap = 0;
        } else if (start >= 0) {
            if (++gap > 8) {
                spans.append({start, x - gap});
                start = -1;
            }
        }
    }
    return spans;
}

} // namespace

class CaptionRenderTests : public QObject
{
    Q_OBJECT

private slots:
    // MARK: - Revelação pela fala

    /// Surgir: a palavra não aparece antes de ser dita; ~300 ms depois, igual ao estático.
    void rise()
    {
        const auto spans = wordSpans();
        QCOMPARE(spans.size(), 3);
        const Project p = revealProject(WordReveal::Rise);
        const Bitmap before = render(p, 950'000);
        QVERIFY(count(before, spans[0], ink) > 300);
        QCOMPARE(count(before, spans[1], ink), 0);
        QCOMPARE(count(before, spans[2], ink), 0);
        // No meio da entrada, a palavra está aparecendo (menos luz que no fim).
        const Bitmap settled = render(p, 1'300'000);
        const qint64 mid = light(render(p, 1'010'000), spans[1]);
        QVERIFY(mid > 0 && double(mid) < 0.9 * double(light(settled, spans[1])));
        // Sem pulo: depois da entrada, igual ao destaque sem revelação (a terceira ainda não foi dita).
        const Bitmap still = render(revealProject(WordReveal::None), 1'300'000);
        for (int i = 0; i < 2; ++i)
            QVERIFY2(maxDiff(settled, still, spans[i]) <= 3, qPrintable(QString::number(maxDiff(settled, still, spans[i]))));
    }

    /// Leitura guiada: as não ditas com a opacidade escolhida; a dita acende.
    void dim()
    {
        const auto spans = wordSpans();
        const auto whiteHighlight = [](WordHighlight &h) { h.color = RGBA::white(); };
        const Bitmap dimmed = render(revealProject(WordReveal::Dim, whiteHighlight), 500'000);
        const auto brightest = [](const Bitmap &b, Span span) {
            int m = 0;
            for (int y = 0; y < b.height; ++y)
                for (int x = span.first; x <= span.last; ++x)
                    m = std::max(m, b.r(x, y));
            // O texto branco sobre o preto, com alfa a, sai com a luz linear a.
            const double v = m / 255.0;
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        QVERIFY(brightest(dimmed, spans[0]) > 0.97);
        QVERIFY2(std::abs(brightest(dimmed, spans[2]) - 0.35) < 0.05, qPrintable(QString::number(brightest(dimmed, spans[2]))));
        QVERIFY(std::abs(brightest(dimmed, spans[1]) - 0.35) < 0.05);
        const Bitmap half = render(revealProject(WordReveal::Dim,
                                                 [](WordHighlight &h) {
                                                     h.dimOpacity = 0.6;
                                                     h.color = RGBA::white();
                                                 }),
                                   500'000);
        QVERIFY(std::abs(brightest(half, spans[2]) - 0.6) < 0.05);
    }

    /// Preenchimento: no meio da palavra, a metade esquerda na cor e a direita não.
    void fill()
    {
        const auto spans = wordSpans();
        const Project p = revealProject(WordReveal::Fill);
        const Bitmap b = render(p, 1'450'000);
        const Span sol = spans[1];
        const Span left{sol.first, sol.first + sol.count() / 3};
        const Span right{sol.last - sol.count() / 3, sol.last};
        QVERIFY(count(b, left, blue) > 100);
        QVERIFY(count(b, left, white) < 20);
        QCOMPARE(count(b, right, blue), 0);
        QVERIFY(count(b, right, white) > 100);
        // A já dita fica cheia; a futura, sem cor.
        QVERIFY(count(b, spans[0], white) < 20 && count(b, spans[0], blue) > 200);
        QCOMPARE(count(b, spans[2], blue), 0);
        // Antes de começar, sem cor nenhuma; depois do fim, cheia.
        QCOMPARE(count(render(p, 950'000), sol, blue), 0);
        QVERIFY(count(render(p, 1'950'000), sol, white) < 20);
    }

    /// Digitação: as letras aparecem em ordem e no fim da legenda não há cursor.
    void typing()
    {
        const auto spans = wordSpans();
        const Project p = revealProject(WordReveal::Type);
        // SOL: S em 1,0 s, O em 1,3 s e L em 1,6 s.
        const auto rightmostInk = [&](Micros t) {
            const Bitmap b = render(p, t);
            for (int x = spans[1].last; x >= spans[1].first; --x)
                if (count(b, {x, x}, white) > 0)
                    return x;
            return 0;
        };
        const int s = rightmostInk(1'050'000), so = rightmostInk(1'350'000), sol = rightmostInk(1'650'000);
        QVERIFY2(s > 0 && s < so && so < sol, qPrintable(QStringLiteral("%1 %2 %3").arg(s).arg(so).arg(sol)));
        QCOMPARE(count(render(p, 950'000), spans[1], white), 0);
        QCOMPARE(count(render(p, 950'000), spans[2], white), 0);
        // Cursor na cor do destaque enquanto digita; a palavra não é tingida.
        const Bitmap typingNow = render(p, 1'350'000);
        QVERIFY(count(typingNow, spans[1], blue) > 10);
        QCOMPARE(count(typingNow, spans[0], blue), 0);
        // No fim da legenda, tudo digitado e sem cursor.
        const Bitmap end = render(p, 3'350'000);
        QCOMPARE(count(end, all(end), blue), 0);
        for (const Span &span : spans)
            QVERIFY(count(end, span, white) > 200);
    }

    /// Digitação: o fundo cresce com o texto digitado, sem pulo, e termina igual ao estático.
    void typingBackground()
    {
        const auto spans = wordSpans();
        const auto boxed = [](WordReveal reveal, bool highlight = true) {
            Project p = revealProject(reveal, {}, highlight);
            TextStyle &style = textSegment(p).text->style;
            style.backgroundEnabled = true;
            style.backgroundColor = RGBA{1, 0, 0, 1};
            style.backgroundPaddingX = 0.3;
            return p;
        };
        const Project p = boxed(WordReveal::Type);
        const auto rightEdge = [&](Micros t) { return lastColumn(render(p, t), red); };
        // Digitando SOL (S e O já apareceram): o fundo cobre MAR e o começo de SOL, mas não chega ao CEU.
        const Bitmap mid = render(p, 1'400'000);
        QVERIFY(count(mid, spans[0], red) > 300);
        QVERIFY(count(mid, {spans[1].first, spans[1].first + spans[1].count() / 3}, red) > 100);
        QCOMPARE(count(mid, spans[2], red), 0);
        // A borda direita só avança e passa por posições intermediárias em volta de cada letra (O em 1,3 s).
        QList<int> edges;
        for (Micros t = 1'200'000; t <= 1'500'000; t += 16'667)
            edges.append(rightEdge(t));
        QString list;
        for (int e : edges)
            list += QString::number(e) + u' ';
        for (int i = 1; i < edges.size(); ++i)
            QVERIFY2(edges[i - 1] <= edges[i], qPrintable(list));
        const int before = rightEdge(1'200'000), after = rightEdge(1'500'000);
        QVERIFY2(after - before > 10, qPrintable(QStringLiteral("%1 %2").arg(before).arg(after)));
        QVERIFY2(std::count_if(edges.begin(), edges.end(), [&](int e) { return e > before && e < after; }) >= 2,
                 qPrintable(list));
        // O cursor (na cor do destaque) fica sempre dentro do fundo.
        for (Micros t = 0; t <= 2'700'000; t += 33'333) {
            const Bitmap b = render(p, t);
            QVERIFY2(lastColumn(b, blue) < lastColumn(b, red) - 2,
                     qPrintable(QStringLiteral("%1: %2 %3").arg(t).arg(lastColumn(b, blue)).arg(lastColumn(b, red))));
        }
        // As letras já digitadas não se mexem: MAR igual ao do fim.
        const Bitmap end = render(p, 3'350'000);
        QVERIFY(maxDiff(mid, end, spans[0]) <= 3);
        // No fim, o fundo é o da imagem estática.
        const int diff = maxDiff(end, render(boxed(WordReveal::None, false), 3'350'000), all(end));
        QVERIFY2(diff <= 3, qPrintable(QString::number(diff)));
    }

    void edited_data()
    {
        QTest::addColumn<WordReveal>("reveal");
        for (WordReveal r : allCases<WordReveal>())
            QTest::newRow(qPrintable(enumKey(r))) << r;
    }

    /// Texto editado (a contagem não bate): tudo visível, sem revelação.
    void edited()
    {
        QFETCH(WordReveal, reveal);
        const QString text = QStringLiteral("MAR SOL CEU AZUL");
        const Bitmap a = render(revealProject(reveal, {}, true, text), 100'000);
        const Bitmap b = render(revealProject(WordReveal::None, {}, false, text), 100'000);
        QVERIFY2(maxDiff(a, b, all(a)) <= 3, qPrintable(QString::number(maxDiff(a, b, all(a)))));
    }

    /// A revelação some sem destaque e convive com a pílula.
    void pillTogether()
    {
        const auto spans = wordSpans();
        const Bitmap b = render(revealProject(WordReveal::Rise,
                                              [](WordHighlight &h) {
                                                  h.backgroundEnabled = true;
                                                  h.backgroundColor = RGBA{1, 0, 0, 1};
                                              }),
                                500'000);
        QVERIFY(count(b, spans[0], red) > 100);
        QCOMPARE(count(b, spans[2], ink), 0);
    }

    // MARK: - Destaque da palavra

    /// A cor segue a palavra falada.
    void colorFollowsWord()
    {
        const Project p = highlightProject();
        const auto a = pixels(render(p, 500'000), yellow);
        const auto b = pixels(render(p, 1'500'000), yellow);
        const auto c = pixels(render(p, 2'500'000), yellow);
        QVERIFY(a.first > 200 && b.first > 200 && c.first > 200);
        QVERIFY2(a.second < 0.42 && b.second >= 0.44 && b.second <= 0.56 && c.second > 0.58,
                 qPrintable(str(a) + str(b) + str(c)));
        // No vão depois do fim da palavra (0,9 s a 1 s), a anterior continua destacada.
        QVERIFY(pixels(render(p, 950'000), yellow).second < 0.42);
    }

    /// Modo progressivo deixa as já faladas destacadas.
    void progressive()
    {
        const Project p = highlightProject([](WordHighlight &h) { h.mode = HighlightMode::Progressive; });
        const auto current = pixels(render(highlightProject(), 2'500'000), yellow);
        const auto prog = pixels(render(p, 2'500'000), yellow);
        QVERIFY(prog.first > current.first * 2);
        QVERIFY2(prog.second >= 0.44 && prog.second <= 0.56, qPrintable(str(prog)));
    }

    /// A palavra atual cresce e a pílula aparece atrás dela.
    void scaleAndPill()
    {
        const Project still = highlightProject([](WordHighlight &h) { h.scale = 1; });
        const Project grown = highlightProject([](WordHighlight &h) { h.scale = 1.4; });
        QVERIFY(inkRows(render(grown, 1'500'000)) > inkRows(render(still, 1'500'000)) + 8);

        QCOMPARE(pixels(render(highlightProject(), 1'500'000), strongBlue).first, 0);
        const auto pill =
            pixels(render(highlightProject([](WordHighlight &h) { h.backgroundEnabled = true; }), 1'500'000), strongBlue);
        QVERIFY(pill.first > 500);
        QVERIFY2(pill.second >= 0.44 && pill.second <= 0.56, qPrintable(str(pill)));
    }

    /// Antes da primeira palavra e com contagem diferente, nada é destacado.
    void inactive()
    {
        Project p = highlightProject();
        (*textSegment(p).text->words)[0].start = 400'000;
        QCOMPARE(pixels(render(p, 100'000), yellow).first, 0);
        const Project mismatch = highlightProject({}, {}, QStringLiteral("MAR SOL CEU AZUL"));
        QCOMPARE(pixels(render(mismatch, 1'500'000), yellow).first, 0);
    }

    /// Transição suave de ~80 ms entre palavras.
    void smoothTransition()
    {
        const QList<CaptionWord> words = {{QStringLiteral("a"), 0, 900'000}, {QStringLiteral("b"), 1'000'000, 2'000'000}};
        const auto at = [&](Micros t) { return TextMotion::highlightAmounts(words, HighlightMode::Current, t).emphasis; };
        QCOMPARE(at(-50'000), (QList<double>{0, 0}));
        QCOMPARE(at(40'000), (QList<double>{1, 0}));
        QCOMPARE(at(950'000), (QList<double>{1, 0}));
        const auto mid = at(1'000'000);
        QVERIFY(std::abs(mid[0] - 0.5) < 1e-9 && std::abs(mid[1] - 0.5) < 1e-9);
        QCOMPARE(at(1'040'000), (QList<double>{0, 1}));
        const auto prog = TextMotion::highlightAmounts(words, HighlightMode::Progressive, 1'500'000);
        QCOMPARE(prog.color, (QList<double>{1, 1}));
        QCOMPARE(prog.emphasis, (QList<double>{0, 1}));
    }

    void withAnimations_data()
    {
        QTest::addColumn<AnimationKind>("kind");
        for (AnimationKind k :
             {AnimationKind::Cascade, AnimationKind::WordByWord, AnimationKind::MaskReveal, AnimationKind::Fade})
            QTest::newRow(qPrintable(enumKey(k))) << k;
    }

    /// Funciona junto com entrada, saída e loop.
    void withAnimations()
    {
        QFETCH(AnimationKind, kind);
        const Project p = highlightProject({}, [&](Segment &s) {
            s.animationIn = SegmentAnimation{kind, 600'000};
            s.animationOut = SegmentAnimation{AnimationKind::Cascade, 600'000};
            s.animationLoop = LoopAnimation{LoopKind::Pulse};
        });
        // No meio da entrada, já com a primeira palavra destacada.
        QVERIFY(pixels(render(p, 300'000), yellow).first > 0);
        // Em repouso, igual ao destaque sem animações (a menos do pulsar).
        const auto rest = pixels(render(p, 1'500'000), yellow);
        QVERIFY2(rest.second >= 0.42 && rest.second <= 0.58, qPrintable(str(rest)));
        // No começo da saída, a última palavra.
        QVERIFY(pixels(render(p, 2'450'000), yellow).second > 0.55);
    }

    /// O texto destacado é rasterizado na escala do destaque.
    void peakIncludesHighlight()
    {
        Project p = highlightProject([](WordHighlight &h) { h.scale = 1.3; });
        QCOMPARE(Animations::peakScale(textSegment(p)), 1.3);
        QVERIFY(Animations::usesUnits(textSegment(p)));
    }

    // MARK: - Palavra-chave e linhas ajustadas

    /// A palavra enfatizada é mais larga e sai na cor; índices inválidos não quebram.
    void emphasis()
    {
        const QSize canvas(1280, 720);
        TextStyle style;
        style.fontSize = 0.08;
        style.shadow = false;
        style.y = 0;
        const TextContent plain(QStringLiteral("um teste bonito"), style);
        TextContent marked = plain;
        WordEmphasis e;
        e.words = {2};
        e.color = RGBA{1, 0, 0, 1};
        marked.emphasis = e;
        TextRenderer &r = TextRenderer::shared();
        const auto a = r.boxSize(plain, canvas), b = r.boxSize(marked, canvas);
        QVERIFY(a && b);
        QVERIFY(b->width() > a->width() + 10);
        // A palavra em si: a mesma palavra sozinha, com e sem ênfase.
        TextContent alone(QStringLiteral("bonito"), style);
        const double word = r.boxSize(alone, canvas)->width();
        WordEmphasis first;
        first.words = {0};
        alone.emphasis = first;
        QVERIFY(r.boxSize(alone, canvas)->width() > word * 1.1);

        TextContent invalid = plain;
        WordEmphasis bad;
        bad.words = {-1, 7, 99};
        invalid.emphasis = bad;
        QCOMPARE(r.boxSize(invalid, canvas), a);
        QCOMPARE(r.lineTexts(invalid, canvas), QStringList{QStringLiteral("um teste bonito")});

        // Desenho: estático e por unidade, com a cor da ênfase só na palavra.
        for (bool highlight : {false, true}) {
            Project p;
            p.canvas.aspect = AspectRatio::R16x9;
            TextContent content = marked;
            if (highlight) {
                QList<CaptionWord> words;
                for (int i = 0; i < 3; ++i)
                    words.append({QString(), Micros(i) * 500'000, Micros(i + 1) * 500'000});
                content.words = words;
                WordHighlight h;
                h.color = RGBA::white();
                h.scale = 1;
                content.highlight = h;
            }
            TE::addText(content, p, 0, 2'000'000);
            const Bitmap bmp = render(p, 100'000, canvas);
            int n = 0;
            qint64 sumX = 0;
            for (int y = 0; y < bmp.height; ++y)
                for (int x = 0; x < bmp.width; ++x)
                    if (bmp.r(x, y) > 200 && bmp.g(x, y) < 60)
                        ++n, sumX += x;
            QVERIFY(n > 300);
            QVERIFY2(double(sumX) / std::max(n, 1) > canvas.width() * 0.55, highlight ? "destaque" : "estático");
        }
    }

    /// fitLines: linhas de larguras diferentes saem com a mesma largura.
    void fitLines()
    {
        const QSize canvas(1280, 720);
        TextStyle style;
        style.fontName = QStringLiteral("Helvetica Neue");
        style.fontFace = QStringLiteral("Condensed Black");
        style.fontSize = 0.1;
        style.textCase = TextCase::Upper;
        style.fitLines = true;
        style.maxWidth = 0.4;
        style.lineSpacing = -0.15;
        const TextContent content(QStringLiteral("sozinho\nmontanha\nrio azul"), style);
        TextRenderer &r = TextRenderer::shared();
        QCOMPARE(r.lineTexts(content, canvas).size(), 3);
        const double target = style.maxWidth * canvas.width();
        // A caixa acompanha a largura pedida; estreitar redimensiona tudo.
        const auto box = r.boxSize(content, canvas);
        QVERIFY(box);
        QVERIFY2(std::abs(box->width() - target) <= 3, qPrintable(QString::number(box->width())));
        TextContent narrow = content;
        narrow.style.maxWidth = 0.3;
        QVERIFY(r.boxSize(narrow, canvas)->height() < box->height());

        // Desenho: a tinta de cada linha ocupa quase a mesma largura.
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        TextContent drawn = content;
        drawn.style.shadow = false;
        drawn.style.y = 0;
        TE::addText(drawn, p, 0, 1'000'000);
        const Bitmap bmp = render(p, 0, canvas);
        QList<std::pair<int, int>> bands;
        bool inBand = false;
        for (int y = 0; y < bmp.height; ++y) {
            int a = -1, b = -1;
            for (int x = 0; x < bmp.width; ++x)
                if (bmp.r(x, y) > 128) {
                    if (a < 0)
                        a = x;
                    b = x;
                }
            if (a >= 0) {
                if (!inBand)
                    bands.append({a, b}), inBand = true;
                else
                    bands.last() = {std::min(bands.last().first, a), std::max(bands.last().second, b)};
            } else {
                inBand = false;
            }
        }
        QCOMPARE(bands.size(), 3);
        // A tinta perde só as margens laterais das letras.
        for (const auto &[a, b] : bands)
            QVERIFY2(std::abs((b - a) - target) < 0.06 * target, qPrintable(QString::number(b - a)));
    }

    /// Os estilos de legenda em 1080p (e 720p, a resolução reduzida do preview).
    void performance()
    {
        const auto words =
            spokenWords(QStringLiteral("Hoje eu vou mostrar para vocês como gerar legendas automáticas no editor sem internet"));
        QStringList report;
        for (const QSize s : {QSize(1920, 1080), QSize(1280, 720)}) {
            for (CaptionPreset preset : {CaptionPreset::Rise, CaptionPreset::Stacked, CaptionPreset::Fill,
                                         CaptionPreset::Typing, CaptionPreset::Editorial}) {
                Project p;
                p.canvas.aspect = AspectRatio::R16x9;
                CB::apply(CB::cues(words, preset), preset, RGBA{1, 0.8, 0.1, 1}, p, false);
                const Segment seg = p.tracks.last().segments[0];
                Renderer renderer;
                const Micros span = seg.targetEnd() - seg.targetStart;
                for (int i = 0; i < 3; ++i)
                    renderer.render(p, seg.targetStart + Micros(i) * span / 3, s);
                QList<double> times;
                QElapsedTimer timer;
                for (int i = 0; i < 30; ++i) {
                    timer.start();
                    renderer.render(p, seg.targetStart + Micros(i) * span / 30, s);
                    times.append(timer.nsecsElapsed() / 1e6);
                }
                const double perFrame = median(times);
                report << QStringLiteral("%1p %2: mediana %3 ms/quadro, pior %4 ms")
                              .arg(s.height())
                              .arg(enumKey(preset))
                              .arg(perFrame, 0, 'f', 1)
                              .arg(*std::max_element(times.begin(), times.end()), 0, 'f', 1);
                QVERIFY2(perFrame < test::frameBudgetMs(), qPrintable(report.last()));
            }
        }
        qInfo().noquote() << "DESEMPENHO DAS LEGENDAS:\n  " << report.join(QStringLiteral("\n  "));
    }
};

QTEST_MAIN(CaptionRenderTests)
#include "CaptionRenderTests.moc"
