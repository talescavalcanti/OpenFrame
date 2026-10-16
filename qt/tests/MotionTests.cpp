// Porte da suíte "Animações de motion" (MotionTests.swift): as 17 animações, os 4 loops, o caminho por unidade e
// a prévia animada.
#include "AnimationPreview.h"
#include "MediaProbe.h"
#include "Motion.h"
#include "Renderer.h"
#include "TestMedia.h"
#include "TextMotion.h"
#include "TestSupport.h"
#include "TimelineEditor.h"

#include <QElapsedTimer>
#include <QSet>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

const QSize size(480, 270);
constexpr int area = 480 * 270;

const QList<AnimationKind> newKinds = {AnimationKind::Focus,      AnimationKind::Cascade,    AnimationKind::Elastic,
                                       AnimationKind::Wave,       AnimationKind::WordByWord, AnimationKind::MaskReveal,
                                       AnimationKind::Tracking,   AnimationKind::Decode};
const QList<LoopKind> loopKinds = {LoopKind::Float, LoopKind::Pulse, LoopKind::Glow, LoopKind::Shine};

/// Bytes RGBA 8 bits pré-multiplicados, como o `Bitmap` dos testes Swift.
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
};

std::pair<Project, QUuid> textProject(const std::function<void(Segment &)> &change = {})
{
    Project p;
    p.canvas.aspect = AspectRatio::R16x9;
    TextStyle style;
    style.y = 0;
    style.fontSize = 0.15;
    const QUuid id = TE::addText(TextContent(QStringLiteral("Motion 42"), style), p, 0, 3'000'000);
    if (change)
        TE::update(p, id, change);
    return {p, id};
}

Bitmap render(const Project &p, Micros t, QSize s = size) { return Bitmap(Renderer::shared().render(p, t, s)); }

/// Soma do brilho (canal vermelho) de todos os pixels.
qint64 ink(const Bitmap &b)
{
    qint64 sum = 0;
    for (qsizetype i = 0; i < b.data.size(); i += 4)
        sum += b.at(i);
    return sum;
}

int maxDifference(const Bitmap &a, const Bitmap &b)
{
    int m = 0;
    for (qsizetype i = 0; i < a.data.size(); ++i)
        m = std::max(m, std::abs(a.at(i) - b.at(i)));
    return m;
}

qint64 difference(const Bitmap &a, const Bitmap &b)
{
    qint64 sum = 0;
    for (qsizetype i = 0; i < a.data.size(); ++i)
        sum += std::abs(a.at(i) - b.at(i));
    return sum;
}

QByteArray bytes(const QImage &image) { return Bitmap(image).data; }

int distinct(const QList<QImage> &frames)
{
    QSet<QByteArray> set;
    for (const QImage &f : frames)
        set.insert(bytes(f));
    return int(set.size());
}

Material photoMaterial(QColor c, int side = 200)
{
    return *MediaProbe::probe(makeImage(QStringLiteral("png"), side, side, c, c));
}

double median(QList<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

} // namespace

class MotionTests : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase() { QDir(mediaDirectory()).removeRecursively(); }

    // MARK: - Modelo

    void newKindsModel()
    {
        QCOMPARE(displayName(AnimationKind::Focus), QStringLiteral("Foco"));
        QVERIFY(!isTextOnly(AnimationKind::Focus));
        for (AnimationKind kind : newKinds)
            if (kind != AnimationKind::Focus)
                QVERIFY(isTextOnly(kind));
        QCOMPARE(displayName(AnimationKind::Decode), QStringLiteral("Decodificar"));
        QStringList names;
        for (LoopKind k : loopKinds)
            names << displayName(k);
        QCOMPARE(names, (QStringList{"Flutuar", "Pulsar", "Respirar brilho", "Reflexo de luz"}));
    }

    void splitKeepsLoop()
    {
        auto [p, id] = textProject([](Segment &s) {
            s.animationIn = SegmentAnimation{AnimationKind::Cascade};
            s.animationOut = SegmentAnimation{AnimationKind::Wave};
            s.animationLoop = LoopAnimation{LoopKind::Float};
        });
        const auto right = TE::split(p, id, 1'500'000);
        QVERIFY(right);
        const Segment l = *p.segment(id), r = *p.segment(*right);
        QVERIFY(l.animationLoop && l.animationLoop->kind == LoopKind::Float);
        QVERIFY(r.animationLoop && r.animationLoop->kind == LoopKind::Float);
        QVERIFY(l.animationIn && !l.animationOut && !r.animationIn && r.animationOut);
    }

    // MARK: - Render

    void entryEndsAtRest_data()
    {
        QTest::addColumn<AnimationKind>("kind");
        for (AnimationKind k : newKinds)
            QTest::newRow(qPrintable(enumKey(k))) << k;
    }

    /// Cada tipo novo começa invisível, termina no repouso e não pula.
    void entryEndsAtRest()
    {
        QFETCH(AnimationKind, kind);
        const auto [p, id] = textProject([&](Segment &s) { s.animationIn = SegmentAnimation{kind, 500'000}; });
        const Bitmap middle = render(p, 1'500'000);
        QVERIFY2(ink(render(p, 0)) < ink(middle) / 20, qPrintable(QString::number(ink(render(p, 0)))));
        // Sem pulo: o quadro logo depois da entrada é igual ao do meio do clipe.
        QCOMPARE(difference(render(p, 500'000), middle), 0);
        // Vai se aproximando do repouso.
        QVERIFY(difference(render(p, 450'000), middle) < difference(render(p, 250'000), middle));
    }

    void exitDisappears_data() { entryEndsAtRest_data(); }

    void exitDisappears()
    {
        QFETCH(AnimationKind, kind);
        const auto [p, id] = textProject([&](Segment &s) { s.animationOut = SegmentAnimation{kind, 500'000}; });
        const Bitmap middle = render(p, 1'500'000);
        QCOMPARE(difference(render(p, 2'500'000), middle), 0);
        QVERIFY(ink(render(p, 2'999'999)) < ink(middle) / 20);
        QVERIFY(difference(render(p, 2'750'000), middle) > 0);
    }

    /// Em repouso, o caminho por unidade desenha como o estático.
    void unitRestMatchesStatic()
    {
        const auto styled = [&](const std::function<void(Segment &)> &change) {
            return textProject([&](Segment &s) {
                       s.text->text = QStringLiteral("Motion\ndesign 42");
                       s.text->style.strokeWidth = 10;
                       s.text->style.underline = true;
                       s.text->style.backgroundEnabled = true;
                       s.text->style.opacity = 0.8;
                       change(s);
                   })
                .first;
        };
        const Bitmap still = render(styled([](Segment &) {}), 1'500'000);
        for (AnimationKind g : {AnimationKind::Cascade, AnimationKind::WordByWord, AnimationKind::MaskReveal}) {
            const Bitmap unit = render(styled([&](Segment &s) { s.animationIn = SegmentAnimation{g}; }), 1'500'000);
            const int diff = maxDifference(still, unit);
            QVERIFY2(diff <= 8, qPrintable(QStringLiteral("%1: %2").arg(enumKey(g)).arg(diff)));
        }
    }

    /// Decodificar é determinístico e troca os caracteres com o tempo.
    void decodeDeterministic()
    {
        const auto [p, id] =
            textProject([](Segment &s) { s.animationIn = SegmentAnimation{AnimationKind::Decode, 1'000'000}; });
        QCOMPARE(difference(render(p, 400'000), render(p, 400'000)), 0);
        QVERIFY(difference(render(p, 400'000), render(p, 460'000)) > 0);
        QCOMPARE(TextMotion::scrambleIndex(3, 7, 42), TextMotion::scrambleIndex(3, 7, 42));
        QSet<int> picks;
        for (int i = 0; i < 40; ++i)
            picks.insert(TextMotion::scrambleIndex(i, 1, 42));
        QVERIFY(picks.size() > 15);
    }

    /// Decodificar sorteia no tamanho da fonte, na linha de base e com largura parecida.
    void decodeGlyphSize()
    {
        TextStyle style;
        style.fontSize = 0.15;
        const QSize canvas(960, 540);
        const auto glyphs = TextMotion::scrambleGlyphs(style, canvas);
        QVERIFY(glyphs);
        QCOMPARE(glyphs->size(), TextMotion::scrambleCharacters.size());
        // Um "E" sorteado tem a mesma altura de um "E" do texto: o glifo não é reduzido.
        const auto set = TextMotion::units(TextContent(QStringLiteral("E"), style), canvas, TextGranularity::Letter);
        QVERIFY(set);
        const TextUnit &unit = set->units[0];
        const ScrambleGlyph &e = (*glyphs)[TextMotion::scrambleCharacters.indexOf(u'E')];
        QVERIFY2(std::abs(e.fill.rect().height() - unit.fill.rect().height()) <= 1,
                 qPrintable(QStringLiteral("%1 × %2").arg(e.fill.rect().height()).arg(unit.fill.rect().height())));
        // y para baixo: o topo do glifo (relativo à linha de base) + a linha de base = o topo da letra.
        QVERIFY(std::abs(e.fill.rect().top() + unit.baseline - unit.fill.rect().top()) <= 1);

        // Célula estreita (um "i"): só glifos estreitos; célula larga: quase todos.
        double narrowest = 1e9, widest = 0;
        for (const ScrambleGlyph &g : *glyphs)
            narrowest = std::min(narrowest, g.advance), widest = std::max(widest, g.advance);
        const auto narrow = TextMotion::scrambleCandidates(*glyphs, narrowest);
        QVERIFY(narrow.size() >= 4 && narrow.size() < glyphs->size() / 2);
        QCOMPARE(TextMotion::scrambleCandidates(*glyphs, widest).size(), glyphs->size());

        // No quadro, a altura do que aparece não muda entre o sorteio e a letra final.
        AnimationState state;
        state.unit = UnitAnimation{AnimationKind::Decode, 0.3, false};
        const TextContent word(QStringLiteral("EEEE"), style);
        const auto scrambled = TextMotion::image(word, canvas, state, TextGranularity::Letter);
        const auto final = TextMotion::image(word, canvas, AnimationState{}, TextGranularity::Letter);
        QVERIFY(scrambled && final);
        const auto inkRows = [](const TextMotion::Image &r) -> std::pair<int, int> {
            const QImage img = r.image.toImage();
            int first = -1, last = -1;
            for (int y = 0; y < img.height(); ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(line[x]) > 200) {
                        if (first < 0)
                            first = y;
                        last = y;
                        break;
                    }
            }
            return {first, last};
        };
        const auto a = inkRows(*scrambled), b = inkRows(*final);
        QVERIFY(a.first >= 0 && b.first >= 0);
        // Caixa alta e números ficam entre a linha de base e a altura das maiúsculas (o "@" e o "$" passam um
        // pouco). y para baixo: a linha de base é a última linha com tinta.
        QVERIFY2(std::abs(a.second - b.second) <= int(0.25 * 0.15 * 540),
                 qPrintable(QStringLiteral("%1 × %2").arg(a.second).arg(b.second)));
        QVERIFY(a.second - a.first + 1 >= b.second - b.first + 1 - 2);
    }

    /// Revelar por máscara não desenha fora da faixa da linha.
    void maskRevealStaysInBand()
    {
        TextStyle style;
        style.shadow = false;
        style.fontSize = 0.15;
        const TextContent content(QStringLiteral("Linha um\nLinha dois"), style);
        const QSize canvas(960, 540);
        const auto set = TextMotion::units(content, canvas, TextGranularity::Line);
        QVERIFY(set);
        QVERIFY(set->units.size() == 2 && set->lineBands.size() == 2);
        for (double progress : {0.2, 0.5, 0.8}) {
            AnimationState state;
            state.unit = UnitAnimation{AnimationKind::MaskReveal, progress, false};
            const auto r = TextMotion::image(content, canvas, state, TextGranularity::Line);
            QVERIFY(r);
            const QImage img = r->image.toImage();
            int outside = 0, inside = 0;
            for (int y = 0; y < img.height(); ++y) {
                const double cy = y + 0.5;
                bool inBand = false;
                for (const QRectF &band : set->lineBands)
                    inBand = inBand || (cy >= band.top() - 1 && cy <= band.bottom() + 1);
                const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    if (qRed(line[x]) > 8)
                        (inBand ? inside : outside)++;
            }
            QVERIFY2(outside == 0 && inside > 0,
                     qPrintable(QStringLiteral("progresso %1: fora %2, dentro %3").arg(progress).arg(outside).arg(inside)));
        }
    }

    /// Máquina de escrever continua revelando no caminho por unidade.
    void typewriterInUnitPath()
    {
        const auto [p, id] = textProject([](Segment &s) {
            s.animationIn = SegmentAnimation{AnimationKind::Typewriter, 1'000'000};
            s.animationOut = SegmentAnimation{AnimationKind::Cascade};
        });
        QList<qint64> inks;
        for (Micros t : {100'000, 400'000, 700'000, 1'500'000})
            inks.append(ink(render(p, t)));
        QList<qint64> sorted = inks;
        std::sort(sorted.begin(), sorted.end());
        QCOMPARE(inks, sorted);
        QVERIFY(inks[0] < inks[3] / 2);
    }

    /// Foco desfoca a foto no começo.
    void focusBlursPhoto()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        const Material m = photoMaterial(Qt::white);
        p.materials.append(m);
        const QUuid id = TE::addMedia(m, p);
        TE::update(p, id, [](Segment &s) {
            s.transform.scale = 0.5;
            s.animationIn = SegmentAnimation{AnimationKind::Focus, 500'000};
        });
        const AnimationState st = Animations::state(*p.segment(id), 0);
        QVERIFY(st.blur > 0.019 && st.alpha == 0 && st.scale > 1.07);
        QCOMPARE(Animations::state(*p.segment(id), 500'000).blur, 0.0);

        // Pixels de transição (nem fundo nem foto) na linha do meio.
        const auto soft = [](const Bitmap &b) {
            const int row = b.height / 2;
            QList<int> values;
            for (int x = 0; x < b.width; ++x)
                values.append(b.at((qsizetype(row) * b.width + x) * 4));
            const int top = *std::max_element(values.begin(), values.end());
            return int(std::count_if(values.begin(), values.end(),
                                     [&](int v) { return v > top / 10 && v < top * 9 / 10; }));
        };
        const int early = soft(render(p, 60'000)), rest = soft(render(p, 1'000'000));
        QVERIFY2(early > rest + 6, qPrintable(QStringLiteral("%1 × %2").arg(early).arg(rest)));
    }

    void loopsChange_data()
    {
        QTest::addColumn<LoopKind>("kind");
        for (LoopKind k : loopKinds)
            QTest::newRow(qPrintable(enumKey(k))) << k;
    }

    /// Loops mexem na imagem ao longo do tempo.
    void loopsChange()
    {
        QFETCH(LoopKind, kind);
        // Texto colorido: o reflexo é branco e só aparece sobre o que não é branco.
        const auto [p, id] = textProject([&](Segment &s) {
            s.text->style.color = RGBA{0.9, 0.3, 0.2, 1};
            s.animationLoop = LoopAnimation{kind, 2'000'000, 1};
        });
        // Um quarto de ciclo depois do início: seno no máximo e faixa de luz no meio do texto.
        const qint64 diff = difference(render(p, 0), render(p, 500'000));
        QVERIFY2(diff > area / 20, qPrintable(QString::number(diff)));
    }

    /// Reflexo aparece em texto branco, dentro e em volta das letras.
    void shineOnWhiteText()
    {
        const auto [rest, restId] = textProject();
        const auto [p, id] =
            textProject([](Segment &s) { s.animationLoop = LoopAnimation{LoopKind::Shine, 2'000'000, 1}; });
        // Fora da passagem (60% em diante do ciclo), igual ao repouso.
        QVERIFY(maxDifference(render(p, 1'600'000), render(rest, 1'600'000)) <= 8);
        // No meio da varredura, muda o texto branco e o entorno.
        const Bitmap lit = render(p, 580'000), plain = render(rest, 580'000);
        QVERIFY2(difference(lit, plain) > area / 4, qPrintable(QString::number(difference(lit, plain))));
        int around = 0, on = 0;
        for (qsizetype i = 0; i < lit.data.size(); i += 4)
            if (lit.at(i) != plain.at(i) || lit.at(i + 2) != plain.at(i + 2))
                (plain.at(i) > 240 ? on : around)++;
        QVERIFY2(on > 100 && around > 100, qPrintable(QStringLiteral("%1 / %2").arg(on).arg(around)));
        // A varredura passa em vários quadros do ciclo (8 por ciclo).
        int swept = 0;
        for (int i = 0; i < 8; ++i)
            swept += difference(render(p, i * 250'000), render(rest, i * 250'000)) > area / 8;
        QVERIFY2(swept >= 4, qPrintable(QString::number(swept)));
    }

    /// Loop numa foto também.
    void loopOnPhoto()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        const Material m = photoMaterial(QColor::fromRgbF(0.3f, 0.3f, 0.3f));
        p.materials.append(m);
        const QUuid id = TE::addMedia(m, p);
        for (LoopKind kind : loopKinds) {
            TE::update(p, id, [&](Segment &s) {
                s.transform.scale = 0.5;
                s.animationLoop = LoopAnimation{kind, 2'000'000, 1};
            });
            QVERIFY2(difference(render(p, 0), render(p, 500'000)) > 0, qPrintable(enumKey(kind)));
        }
    }

    /// Prévia devolve quadros diferentes, com fundo transparente.
    void previewFrames()
    {
        using AnimationPreview::Part;
        const TextContent content(QStringLiteral("Prévia"), TextStyle{});
        const QSize small(160, 90);
        const auto frames = AnimationPreview::frames(content, Part::entry(AnimationKind::ZoomIn), small, 12);
        QCOMPARE(frames.size(), 12);
        for (const QImage &f : frames)
            QCOMPARE(f.size(), small);
        QVERIFY(distinct(frames) > 6);
        // Canto sem nada: transparente.
        QCOMPARE(qAlpha(frames.last().pixel(0, 0)), 0);
        // Na entrada, o último quadro já está em repouso e o texto ocupa boa parte da largura.
        int columns = 0;
        for (int x = 0; x < small.width(); ++x)
            for (int y = 0; y < small.height(); ++y)
                if (qAlpha(frames.last().pixel(x, y)) > 128) {
                    ++columns;
                    break;
                }
        QVERIFY2(columns > small.width() / 2, qPrintable(QString::number(columns)));

        const auto cascade = AnimationPreview::frames(content, Part::entry(AnimationKind::Cascade), small, 12);
        QVERIFY(cascade.size() == 12 && distinct(cascade) > 6);
        const auto exit = AnimationPreview::frames(content, Part::exit(AnimationKind::Fade), small, 8);
        QVERIFY(exit.size() == 8 && bytes(exit[0]) != bytes(exit[7]));
        // 5 quadros por ciclo: fases que não se repetem dentro do ciclo.
        const auto loop = AnimationPreview::frames(content, Part::loopOf(LoopKind::Float), small, 10);
        QVERIFY(loop.size() == 10 && distinct(loop) >= 5);

        // Imagem: encaixada e animada.
        QImage green(64, 64, QImage::Format_ARGB32_Premultiplied);
        green.fill(QColor(0, 255, 0));
        const auto image = AnimationPreview::frames(green, Part::entry(AnimationKind::SlideLeft), small, 10);
        QVERIFY(image.size() == 10 && distinct(image) > 5);
        const QRgb center = image.last().pixel(80, 45);
        QVERIFY(qGreen(center) > 240 && qAlpha(center) == 255);
        QCOMPARE(qAlpha(image.last().pixel(0, 0)), 0); // canto: fora da foto quadrada

        QVERIFY(AnimationPreview::frames(TextContent(QString(), {}), Part::entry(AnimationKind::Fade), small, 4).isEmpty());
        QVERIFY(AnimationPreview::frames(content, Part::entry(AnimationKind::Fade), small, 0).isEmpty());
    }

    /// Desempenho: 40 letras em 1080p (e 720p, a resolução reduzida do preview).
    void performance()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        const QString text = QStringLiteral("Quarenta letras animadas em cascata aqui");
        const QUuid id = TE::addText(TextContent(text), p, 0, 3'000'000);
        QStringList report;
        for (const QSize s : {QSize(1920, 1080), QSize(1280, 720)}) {
            for (AnimationKind kind :
                 {AnimationKind::Cascade, AnimationKind::Elastic, AnimationKind::Decode, AnimationKind::MaskReveal}) {
                Project q = p;
                TE::update(q, id, [&](Segment &seg) {
                    seg.animationIn = SegmentAnimation{kind, 1'000'000};
                    seg.animationLoop = LoopAnimation{LoopKind::Shine};
                });
                Renderer renderer;
                QElapsedTimer timer;
                timer.start();
                renderer.render(q, 0, s); // monta os sprites
                const qint64 build = timer.elapsed();
                for (int i = 0; i < 3; ++i)
                    renderer.render(q, i * 33'333, s);
                QList<double> times;
                for (int i = 0; i < 30; ++i) {
                    timer.restart();
                    renderer.render(q, i * 33'333, s);
                    times.append(timer.nsecsElapsed() / 1e6);
                }
                const double perFrame = median(times);
                report << QStringLiteral("%1p %2: %3 ms de montagem, mediana %4 ms/quadro, pior %5 ms")
                              .arg(s.height())
                              .arg(enumKey(kind))
                              .arg(build)
                              .arg(perFrame, 0, 'f', 1)
                              .arg(*std::max_element(times.begin(), times.end()), 0, 'f', 1);
                // Folga para a máquina ocupada com os outros testes em paralelo (a CI é mais lenta).
                QVERIFY2(perFrame < frameBudgetMs(), qPrintable(report.last()));
            }
        }
        qInfo().noquote() << "DESEMPENHO, " << text.size() << " letras:\n  " << report.join(QStringLiteral("\n  "));
    }
};

QTEST_MAIN(MotionTests)
#include "MotionTests.moc"
