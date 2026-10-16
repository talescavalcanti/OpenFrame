// Porte da suíte "Nitidez do texto ampliado" (TextSharpnessTests.swift): o texto é rasterizado na escala efetiva.
#include "CanvasLayout.h"
#include "Motion.h"
#include "Renderer.h"
#include "TextRenderer.h"
#include "TestSupport.h"
#include "TimelineEditor.h"

#include <QElapsedTimer>
#include <QTest>

using namespace of;
namespace TE = of::TimelineEditor;

namespace {

const QSize size(1280, 720);

/// Canal vermelho de um quadro, linha 0 no topo.
struct Reds {
    int width = 0, height = 0;
    QList<int> values;
    explicit Reds(const QImage &image) : width(image.width()), height(image.height())
    {
        values.reserve(width * height);
        for (int y = 0; y < height; ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < width; ++x)
                values.append(qRed(line[x]));
        }
    }
    int at(int x, int y) const { return values[y * width + x]; }
};

Project project(double scale, const std::function<void(Segment &)> &change = {})
{
    Project p;
    p.canvas.aspect = AspectRatio::R16x9;
    TextStyle style;
    style.shadow = false;
    style.fontSize = 0.05;
    style.y = 0;
    style.scale = scale;
    const QUuid id = TE::addText(TextContent(QStringLiteral("IHIH"), style), p, 0, 3'000'000);
    if (change)
        TE::update(p, id, change);
    return p;
}

QImage render(const Project &p, Micros t, QSize s = size) { return Renderer::shared().render(p, t, s); }

/// Maior transição de borda (pixels entre 10% e 90% do branco) na linha com mais tinta.
int widestEdge(const Project &p, Micros t)
{
    const Reds bmp(render(p, t));
    int row = 0, best = -1;
    for (int y = 0; y < bmp.height; ++y) {
        int n = 0;
        for (int x = 0; x < bmp.width; ++x)
            n += bmp.at(x, y) > 245;
        if (n > best)
            best = n, row = y;
    }
    int widest = 0, run = 0;
    for (int x = 0; x < bmp.width; ++x) {
        const int v = bmp.at(x, row);
        if (v > 25 && v < 230)
            widest = std::max(widest, ++run);
        else
            run = 0;
    }
    return widest;
}

double median(QList<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

} // namespace

class TextSharpnessTests : public QObject
{
    Q_OBJECT

private slots:
    /// Texto com escala 3 tem borda nítida.
    void scaledTextIsSharp() { QVERIFY(widestEdge(project(3), 1'000'000) <= 2); }

    /// Elástico no meio da animação, com escala 3, também.
    void elasticIsSharp()
    {
        const Project p = project(3, [](Segment &s) { s.animationIn = SegmentAnimation{AnimationKind::Elastic, 1'000'000}; });
        const int edge = widestEdge(p, 800'000);
        QVERIFY2(edge <= 2, qPrintable(QString::number(edge)));
    }

    void boxUnchanged_data()
    {
        QTest::addColumn<double>("scale");
        for (double s : {1.0, 2.5, 3.0})
            QTest::newRow(qPrintable(QString::number(s))) << s;
    }

    /// Ampliado, o fundo do texto continua batendo com a caixa do CanvasLayout.
    void boxUnchanged()
    {
        QFETCH(double, scale);
        Project p = project(scale, [](Segment &s) {
            s.text->style.backgroundEnabled = true;
            s.text->style.backgroundColor = RGBA::white();
            s.text->style.x = 0.1;
            s.text->style.y = -0.2;
        });
        p.canvas.background = CanvasBackground::black();
        const auto boxes = CanvasLayout::boxes(p, 0);
        QVERIFY(!boxes.isEmpty());
        const auto corners = CanvasLayout::corners(boxes.first(), 16.0 / 9);
        const Reds bmp(render(p, 0));
        // Caixa dos pixels com vermelho acima de 128, com y para cima.
        int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
        for (int y = 0; y < bmp.height; ++y)
            for (int x = 0; x < bmp.width; ++x)
                if (bmp.at(x, y) > 128) {
                    minX = std::min(minX, x), maxX = std::max(maxX, x);
                    minY = std::min(minY, bmp.height - 1 - y), maxY = std::max(maxY, bmp.height - 1 - y);
                }
        const auto px = [](double v, double side) { return (v + 1) / 2 * side; };
        QVERIFY(std::abs(minX - px(corners[0].x(), size.width())) < 3);
        QVERIFY(std::abs(maxX + 1 - px(corners[2].x(), size.width())) < 3);
        QVERIFY(std::abs(maxY + 1 - px(corners[0].y(), size.height())) < 3);
        QVERIFY(std::abs(minY - px(corners[2].y(), size.height())) < 3);
    }

    /// Sombra, contorno, fundo e reflexo mantêm o tamanho visual na escala 2.
    void effectsKeepVisualSize()
    {
        // Escala 2 com fonte f desenha igual a escala 1 com fonte 2f (e largura máxima dobrada): as medidas em em e
        // em fração da caixa acompanham a rasterização maior.
        const auto styled = [](double scale, double font, double maxWidth) {
            return project(scale, [&](Segment &s) {
                s.text->text = QStringLiteral("Brilho\nsombra");
                s.text->style.fontSize = font;
                s.text->style.maxWidth = maxWidth;
                s.text->style.shadow = true;
                s.text->style.strokeWidth = 12;
                s.text->style.underline = true;
                s.text->style.backgroundEnabled = true;
                s.text->style.color = RGBA{0.9, 0.4, 0.2, 1};
                s.animationLoop = LoopAnimation{LoopKind::Shine, 2'000'000, 1};
            });
        };
        const Project a = styled(2, 0.05, 0.9), b = styled(1, 0.1, 1.8);
        for (Micros t : {600'000, 1'800'000}) {
            const QImage ia = render(a, t).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            const QImage ib = render(b, t).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            int worst = 0;
            for (int y = 0; y < ia.height(); ++y) {
                const uchar *ra = ia.constScanLine(y), *rb = ib.constScanLine(y);
                for (int i = 0; i < ia.width() * 4; ++i)
                    worst = std::max(worst, std::abs(int(ra[i]) - int(rb[i])));
            }
            QVERIFY2(worst <= 3, qPrintable(QStringLiteral("t = %1: %2").arg(t).arg(worst)));
        }
    }

    /// Escala de raster: passos de 1/8 para cima, mínimo 1 e limite de 8192 px.
    void rasterScaleRules()
    {
        TextStyle style;
        const QSize canvas(1920, 1080);
        const auto r = [&](double scale, double peak = 1, const QString &text = QStringLiteral("Oi")) {
            style.scale = scale;
            return TextRenderer::shared().rasterScale(TextContent(text, style), canvas, peak);
        };
        QVERIFY(r(1) == 1 && r(0.4) == 1);
        QVERIFY(r(2.01) == 2.125 && r(2) == 2);
        QCOMPARE(r(2, 1.15), 2.375); // elástico
        Segment plain(std::nullopt, TimeRange{0, 1}, 0);
        plain.text = TextContent(QStringLiteral("a"));
        QCOMPARE(Animations::peakScale(plain), 1.0);
        const QString longText = QStringLiteral("Texto comprido ").repeated(4);
        style.scale = 50;
        const auto imageSize = TextRenderer::shared().imageSize(TextContent(longText, style), canvas);
        QVERIFY(imageSize);
        const double big = r(50, 1, longText);
        QVERIFY(big < 50 && std::max(imageSize->width(), imageSize->height()) * big <= 8192.5);
    }

    /// Desempenho ao arrastar a escala (cache por passos de 1/8).
    void dragPerformance()
    {
        const QSize hd(1920, 1080);
        QStringList report;
        for (const std::optional<AnimationKind> kind : {std::optional<AnimationKind>{}, std::optional{AnimationKind::Cascade}}) {
            Project p = project(1, [](Segment &s) { s.text->text = QStringLiteral("Quarenta letras animadas em cascata aqui"); });
            const QUuid id = p.tracks.last().segments[0].id;
            if (kind)
                TE::update(p, id, [&](Segment &s) { s.animationIn = SegmentAnimation{*kind, 500'000}; });
            Renderer renderer;
            QList<double> times;
            QElapsedTimer timer;
            // Arraste de 1× a 3× em 60 quadros, parado no repouso (depois da entrada).
            for (int i = 0; i <= 60; ++i) {
                TE::update(p, id, [&](Segment &s) { s.text->style.scale = 1 + 2 * double(i) / 60; });
                timer.start();
                renderer.render(p, 1'000'000, hd);
                times.append(timer.nsecsElapsed() / 1e6);
            }
            const double average = median(times);
            report << QStringLiteral("%1: mediana %2 ms, primeiro %3 ms, pior dos demais %4 ms")
                          .arg(kind ? enumKey(*kind) : QStringLiteral("estático"))
                          .arg(average, 0, 'f', 1)
                          .arg(times[0], 0, 'f', 1)
                          .arg(*std::max_element(times.begin() + 1, times.end()), 0, 'f', 1);
            QVERIFY2(average < test::frameBudgetMs(), qPrintable(report.last()));
        }
        qInfo().noquote() << "ARRASTE DE ESCALA 1080p:\n  " << report.join(QStringLiteral("\n  "));
    }
};

QTEST_MAIN(TextSharpnessTests)
#include "TextSharpnessTests.moc"
