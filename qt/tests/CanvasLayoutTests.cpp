// Porte da suíte "Layout do canvas e ímã" (CanvasLayoutTests.swift): caixas, cantos, clique, ímã e
// setPlacement, com os mesmos números.
#include "CanvasLayout.h"
#include "Renderer.h"
#include "TestSupport.h"

#include <QGuiApplication>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

constexpr double aspect = 16.0 / 9;

ElementBox box(double x, double y, double w = 0.4, double h = 0.4, double scale = 1, double rotation = 0)
{
    return {QUuid::createUuid(), false, QPointF(x, y), QSizeF(w, h), scale, rotation};
}

bool close(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

const QSizeF threshold(0.02, 0.02);

SnapGuide guide(SnapGuide::Axis axis, double position, SnapGuide::Kind kind) { return {axis, position, kind}; }

} // namespace

class CanvasLayoutTests : public QObject
{
    Q_OBJECT

private slots:
    // Caixas em ordem de desenho, com tamanho encaixado e faixas ocultas de fora
    void boxesOrderAndSize()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        const QUuid main = TE::addMedia(photo(400, 300), p);
        const QUuid overlay = TE::addOverlay(photo(100, 400, QStringLiteral("b.png")), p, 0);
        TE::update(p, overlay, [](Segment &s) {
            s.transform.x = 0.3;
            s.transform.y = -0.2;
            s.transform.rotation = 15;
        });
        const QUuid textId = TE::addText(text(QStringLiteral("Olá")), p, 0);
        const QUuid hidden = TE::addText(text(QStringLiteral("Some")), p, 0);
        p.tracks[p.locate(hidden)->track].hidden = true;

        const auto boxes = CanvasLayout::boxes(p, 1'000'000);
        QCOMPARE(boxes.size(), 3);
        QCOMPARE(boxes[0].id, main);
        QCOMPARE(boxes[1].id, overlay);
        QCOMPARE(boxes[2].id, textId);
        // Foto 4:3 num canvas 16:9: altura inteira (2) e largura 2·(4/3)/(16/9) = 1,5.
        QVERIFY(close(boxes[0].size.width(), 1.5) && close(boxes[0].size.height(), 2));
        QVERIFY(!boxes[0].isText && boxes[2].isText);
        // Foto em pé 1:4: altura inteira, largura 2·(1/4)/(16/9).
        QVERIFY(close(boxes[1].size.width(), 2 * 0.25 / aspect) && close(boxes[1].size.height(), 2));
        QCOMPARE(boxes[1].center, QPointF(0.3, -0.2));
        QCOMPARE(boxes[1].scale, 0.5);
        QCOMPARE(boxes[1].rotation, 15.0);
        QCOMPARE(boxes[2].center, QPointF(0, -0.7));

        // Depois do fim dos clipes não sobra nada.
        QVERIFY(CanvasLayout::boxes(p, 60'000'000).isEmpty());
    }

    // Caixa do texto bate com o que é desenhado e cresce com o fundo
    void textBoxMatchesRender()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        TextStyle style;
        style.shadow = false;
        style.backgroundEnabled = true;
        style.backgroundColor = RGBA::white();
        style.x = 0.2;
        style.y = 0.1;
        const QUuid id = TE::addText(TextContent(QStringLiteral("Caixa de texto"), style), p, 0);
        const auto all = CanvasLayout::boxes(p, 0);
        const ElementBox b = *std::find_if(all.begin(), all.end(), [&](const ElementBox &x) { return x.id == id; });

        // Sem fundo, a caixa diminui exatamente o enchimento do fundo (0,21 em de cada lado).
        Project plain = p;
        TE::update(plain, id, [](Segment &s) { s.text->style.backgroundEnabled = false; });
        const ElementBox small = CanvasLayout::boxes(plain, 0).first();
        const double padY = 2 * 0.21 * style.fontSize * 2;
        QVERIFY2(close(b.size.height() - small.size.height(), padY, 0.004),
                 qPrintable(QString::number(b.size.height() - small.size.height())));

        // O fundo branco desenhado ocupa a caixa (até ~2 px de arredondamento a 1280×720).
        const QSize size(1280, 720);
        const QImage img = Renderer::shared().render(p, 0, size).convertToFormat(QImage::Format_ARGB32);
        int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qRed(img.pixel(x, y)) > 128) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
        const double expectedW = b.size.width() / 2 * size.width();
        const double expectedH = b.size.height() / 2 * size.height();
        QVERIFY2(std::abs(double(maxX - minX + 1) - expectedW) < 3, qPrintable(QStringLiteral("%1 %2").arg(maxX - minX + 1).arg(expectedW)));
        QVERIFY2(std::abs(double(maxY - minY + 1) - expectedH) < 3, qPrintable(QStringLiteral("%1 %2").arg(maxY - minY + 1).arg(expectedH)));
        const double cx = (double(minX + maxX + 1) / 2) / size.width() * 2 - 1;
        const double cy = 1 - (double(minY + maxY + 1) / 2) / size.height() * 2;
        QVERIFY2(close(cx, 0.2, 0.005) && close(cy, 0.1, 0.005), qPrintable(QStringLiteral("%1 %2").arg(cx).arg(cy)));
    }

    // Cantos giram em pixels
    void cornersRotate()
    {
        const ElementBox b = box(0.1, 0.2, 0.5, 0.4, 2);
        const auto straight = CanvasLayout::corners(b, aspect);
        QVERIFY(close(straight[0].x(), 0.1 - 0.5) && close(straight[0].y(), 0.2 + 0.4));
        QVERIFY(close(straight[2].x(), 0.1 + 0.5) && close(straight[2].y(), 0.2 - 0.4));

        // 90° horário: a largura em pixels vira altura.
        ElementBox turned = b;
        turned.rotation = 90;
        const QRectF r = CanvasLayout::bounds(turned, aspect);
        QVERIFY(close(r.width(), 0.8 / aspect) && close(r.height(), 1.0 * aspect));
        // Horário na tela: o canto superior esquerdo vai para a direita, em cima.
        const QPointF tl = CanvasLayout::corners(turned, aspect)[0];
        QVERIFY(tl.x() > 0.1 && tl.y() > 0.2);
    }

    // Clique acerta o elemento mais acima e respeita a rotação
    void hitTest()
    {
        const ElementBox below = box(0, 0, 1, 1);
        const ElementBox above = box(0.2, 0, 0.4, 0.4);
        const QList<ElementBox> boxes{below, above};
        QCOMPARE(CanvasLayout::hitTest(QPointF(0.2, 0), boxes, aspect), std::optional(above.id));
        QCOMPARE(CanvasLayout::hitTest(QPointF(-0.3, 0), boxes, aspect), std::optional(below.id));
        QVERIFY(!CanvasLayout::hitTest(QPointF(0.9, 0.9), boxes, aspect));

        // Quadrado em pixels girado 45°: o canto da caixa reta fica de fora, a ponta do losango dentro.
        const ElementBox diamond = box(0, 0, 0.4 / aspect, 0.4, 1, 45);
        QVERIFY(!CanvasLayout::hitTest(QPointF(0.19 / aspect, 0.19), {diamond}, aspect));
        QCOMPARE(CanvasLayout::hitTest(QPointF(0, 0.27), {diamond}, aspect), std::optional(diamond.id));
        // Escala aumenta a área de clique.
        ElementBox big = box(0, 0, 0.2, 0.2);
        QVERIFY(!CanvasLayout::hitTest(QPointF(0.15, 0), {big}, aspect));
        big.scale = 2;
        QCOMPARE(CanvasLayout::hitTest(QPointF(0.15, 0), {big}, aspect), std::optional(big.id));
    }

    // Encaixa no centro do canvas
    void snapCenter()
    {
        const auto r = Snapping::snapMove(box(0, 0), QPointF(0.01, -0.015), {}, aspect, threshold);
        QCOMPARE(r.center, QPointF(0, 0));
        QCOMPARE(r.guides, (QList<SnapGuide>{guide(SnapGuide::Axis::Vertical, 0, SnapGuide::Kind::CanvasCenter),
                                             guide(SnapGuide::Axis::Horizontal, 0, SnapGuide::Kind::CanvasCenter)}));
    }

    // Sem alvo por perto, nada muda
    void snapNothing()
    {
        const auto r = Snapping::snapMove(box(0, 0), QPointF(0.3, 0.25), {}, aspect, threshold);
        QCOMPARE(r.center, QPointF(0.3, 0.25));
        QVERIFY(r.guides.isEmpty());
    }

    // Bordas encaixam nas bordas e na margem de segurança
    void snapEdges()
    {
        // Caixa de 0,4: a borda esquerda em -0,895 vai para a margem -0,9.
        const auto r = Snapping::snapMove(box(0, 0), QPointF(-0.695, 0.5), {}, aspect, threshold);
        QVERIFY(close(r.center.x(), -0.7));
        QCOMPARE(r.guides, QList<SnapGuide>{guide(SnapGuide::Axis::Vertical, -0.9, SnapGuide::Kind::SafeMargin)});

        // Borda de cima em 0,99 vai para a borda do canvas.
        const auto top = Snapping::snapMove(box(0, 0), QPointF(0.5, 0.79), {}, aspect, threshold);
        QVERIFY(close(top.center.y(), 0.8));
        QCOMPARE(top.guides, QList<SnapGuide>{guide(SnapGuide::Axis::Horizontal, 1, SnapGuide::Kind::CanvasEdge)});
    }

    // O alvo mais próximo vence e outros elementos servem de alvo
    void snapElements()
    {
        const ElementBox other = box(0.5, 0.3, 0.2, 0.2);
        // Centro proposto em x 0,505: o centro do outro (0,5) está mais perto que qualquer borda.
        const auto r = Snapping::snapMove(box(0, 0, 0.3, 0.3), QPointF(0.505, -0.5), {other}, aspect, threshold);
        QVERIFY(close(r.center.x(), 0.5));
        QCOMPARE(r.guides, QList<SnapGuide>{guide(SnapGuide::Axis::Vertical, 0.5, SnapGuide::Kind::Element)});

        // Topo da caixa (y + 0,15) a 0,005 da base do outro (0,2); o centro está a 0,045 do centro do canvas.
        const auto e = Snapping::snapMove(box(0, 0, 0.3, 0.3), QPointF(-0.4, 0.045), {other}, aspect, threshold);
        QVERIFY(close(e.center.y(), 0.05));
        QCOMPARE(e.guides.size(), 1);
        QVERIFY(e.guides[0].axis == SnapGuide::Axis::Horizontal && close(e.guides[0].position, 0.2) &&
                e.guides[0].kind == SnapGuide::Kind::Element);

        // O próprio elemento na lista de outros é ignorado.
        const ElementBox me = box(0.3, 0.3);
        QCOMPARE(Snapping::snapMove(me, QPointF(0.305, 0.5), {me}, aspect, threshold).center.x(), 0.305);
    }

    // Elemento girado encaixa pela caixa alinhada aos eixos
    void snapRotatedBounds()
    {
        const ElementBox b = box(0, 0, 0.4 / aspect, 0.4, 1, 45);
        // Girado 45°, a meia altura vira 0,2·√2 ≈ 0,283: a ponta de cima encosta em 1 com o centro em ~0,717.
        const auto r = Snapping::snapMove(b, QPointF(0.3, 0.71), {}, aspect, threshold);
        QVERIFY(close(r.center.y(), 1 - 0.2 * std::sqrt(2.0), 1e-9));
        QVERIFY(r.guides.contains(guide(SnapGuide::Axis::Horizontal, 1, SnapGuide::Kind::CanvasEdge)));
    }

    // Guias iguais na mesma posição viram uma só, a mais forte
    void snapDeduplicates()
    {
        const ElementBox other = box(0, 0.5, 0.2, 0.2);
        const auto r = Snapping::snapMove(box(0, 0), QPointF(0.01, -0.5), {other}, aspect, threshold);
        QList<SnapGuide> vertical;
        for (const SnapGuide &g : r.guides)
            if (g.axis == SnapGuide::Axis::Vertical)
                vertical.append(g);
        QCOMPARE(vertical, QList<SnapGuide>{guide(SnapGuide::Axis::Vertical, 0, SnapGuide::Kind::CanvasCenter)});
    }

    // Rotação encaixa em múltiplos de 45°
    void snapRotation()
    {
        const auto check = [](double in, double threshold, double degrees, bool snapped) {
            const auto r = Snapping::snapRotation(in, threshold);
            return r.degrees == degrees && r.snapped == snapped;
        };
        QVERIFY(check(43, 4, 45, true));
        QVERIFY(check(40, 4, 40, false));
        QVERIFY(check(-88.5, 4, -90, true));
        QVERIFY(check(181, 4, 180, true));
        QVERIFY(check(2, 1, 2, false));
        QCOMPARE(Snapping::snapRotation(43).degrees, 45.0);
    }

    // setPlacement grava no estilo do texto e no transform do visual
    void setPlacement()
    {
        Project p;
        const QUuid photoId = TE::addMedia(photo(400, 300), p);
        const QUuid textId = TE::addText(text(QStringLiteral("Oi")), p, 0);
        TE::setPlacement(p, textId, QPointF(0.1, 0.2), 1.5, 30);
        TE::setPlacement(p, photoId, QPointF(-0.3, 0.4), 0.5, -10);
        const Segment s = *p.segment(textId);
        QVERIFY(s.text->style.x == 0.1 && s.text->style.y == 0.2 && s.text->style.scale == 1.5 &&
                s.text->style.rotation == 30);
        QCOMPARE(s.transform, Transform());
        const Transform t = p.segment(photoId)->transform;
        QVERIFY(t.x == -0.3 && t.y == 0.4 && t.scale == 0.5 && t.rotation == -10);
        // Escala mínima de 0,01.
        TE::setPlacement(p, photoId, QPointF(), -2, 0);
        QCOMPARE(p.segment(photoId)->transform.scale, 0.01);

        const auto all = CanvasLayout::boxes(p, 0);
        const ElementBox b = *std::find_if(all.begin(), all.end(), [&](const ElementBox &x) { return x.id == textId; });
        QVERIFY(b.center == QPointF(0.1, 0.2) && b.scale == 1.5 && b.rotation == 30);
    }
};

QTEST_MAIN(CanvasLayoutTests)
#include "CanvasLayoutTests.moc"
