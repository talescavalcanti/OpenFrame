#include "CanvasLayout.h"

#include "TextRenderer.h"

#include <algorithm>
#include <cmath>

namespace of {

QList<ElementBox> CanvasLayout::boxes(const Project &project, Micros time)
{
    const double aspect = project.aspectValue();
    QList<ElementBox> result;
    for (const Track &track : project.tracks) {
        if (track.kind != TrackKind::Video || track.hidden)
            continue;
        QList<Segment> active;
        for (const Segment &s : track.segments)
            if (s.targetRange().contains(time))
                active.append(s);
        std::stable_sort(active.begin(), active.end(),
                         [](const Segment &a, const Segment &b) { return a.targetStart < b.targetStart; });
        for (const Segment &seg : active) {
            const Material *m = project.material(seg.materialId);
            if (!m || !m->isVisual() || m->width <= 0 || m->height <= 0)
                continue;
            const Transform &t = seg.transform;
            result.append({seg.id, false, QPointF(t.x, t.y), fitSize(double(m->width) / m->height, aspect), t.scale,
                           t.rotation});
        }
    }
    const QSize canvas = project.canvasSize(referenceShortSide);
    for (const Track &track : project.tracks) {
        if (track.kind != TrackKind::Text || track.hidden)
            continue;
        for (const Segment &seg : track.segments) {
            if (!seg.targetRange().contains(time) || !seg.text)
                continue;
            const auto px = TextRenderer::shared().boxSize(*seg.text, canvas);
            if (!px)
                continue;
            const TextStyle &s = seg.text->style;
            result.append({seg.id, true, QPointF(s.x, s.y),
                           QSizeF(px->width() / canvas.width() * 2, px->height() / canvas.height() * 2), s.scale,
                           s.rotation});
        }
    }
    return result;
}

QSizeF CanvasLayout::fitSize(double mediaAspect, double canvasAspect)
{
    return mediaAspect >= canvasAspect ? QSizeF(2, 2 * canvasAspect / mediaAspect)
                                       : QSizeF(2 * mediaAspect / canvasAspect, 2);
}

std::optional<QUuid> CanvasLayout::hitTest(QPointF point, const QList<ElementBox> &boxes, double aspect)
{
    for (auto it = boxes.crbegin(); it != boxes.crend(); ++it) {
        const ElementBox &box = *it;
        // Para o espaço da caixa: em unidades de meia altura do canvas, desfazendo a rotação.
        const double dx = (point.x() - box.center.x()) * aspect;
        const double dy = point.y() - box.center.y();
        const double a = -box.rotation * M_PI / 180;
        const double u = dx * std::cos(a) + dy * std::sin(a);
        const double v = -dx * std::sin(a) + dy * std::cos(a);
        const double halfW = box.size.width() * std::abs(box.scale) * aspect / 2;
        const double halfH = box.size.height() * std::abs(box.scale) / 2;
        if (std::abs(u) <= halfW && std::abs(v) <= halfH)
            return box.id;
    }
    return std::nullopt;
}

QList<QPointF> CanvasLayout::corners(const ElementBox &box, double aspect)
{
    const double halfW = box.size.width() * box.scale * aspect / 2;
    const double halfH = box.size.height() * box.scale / 2;
    // Horário na tela com y para cima = ângulo negativo.
    const double a = -box.rotation * M_PI / 180;
    QList<QPointF> out;
    for (const auto &[u, v] : {std::pair{-halfW, halfH}, {halfW, halfH}, {halfW, -halfH}, {-halfW, -halfH}}) {
        const double x = u * std::cos(a) - v * std::sin(a);
        const double y = u * std::sin(a) + v * std::cos(a);
        out.append(QPointF(box.center.x() + x / aspect, box.center.y() + y));
    }
    return out;
}

QRectF CanvasLayout::bounds(const ElementBox &box, double aspect)
{
    const QList<QPointF> pts = corners(box, aspect);
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
    for (const QPointF &p : pts) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

namespace {

int rank(SnapGuide::Kind kind) { return int(kind); }

struct Target {
    double value;
    SnapGuide::Kind kind;
};

struct AxisResult {
    double center;
    QList<SnapGuide> guides;
};

/// Novo centro no eixo e as guias dos alvos alinhados depois do encaixe.
AxisResult snapAxis(double center, const std::array<double, 3> &offsets, const QList<Target> &targets,
                    double threshold, SnapGuide::Axis axis)
{
    std::optional<std::pair<double, double>> best; // distância, centro
    for (const double offset : offsets)
        for (const Target &t : targets) {
            const double d = std::abs(t.value - (center + offset));
            if (d <= threshold && (!best || d < best->first))
                best = std::pair{d, t.value - offset};
        }
    if (!best)
        return {center, {}};
    const double snapped = best->second;
    // Um alvo por posição; se houver mais de um, fica o mais forte.
    QList<Target> byPosition;
    for (const double offset : offsets)
        for (const Target &t : targets) {
            if (std::abs(t.value - (snapped + offset)) >= 1e-9)
                continue;
            const auto it = std::find_if(byPosition.begin(), byPosition.end(),
                                         [&](const Target &x) { return std::abs(x.value - t.value) < 1e-9; });
            if (it == byPosition.end())
                byPosition.append(t);
            else if (rank(t.kind) < rank(it->kind))
                it->kind = t.kind;
        }
    QList<SnapGuide> guides;
    for (const Target &t : byPosition)
        guides.append({axis, t.value, t.kind});
    return {snapped, guides};
}

} // namespace

Snapping::MoveResult Snapping::snapMove(const ElementBox &box, QPointF proposed, const QList<ElementBox> &others,
                                        double aspect, QSizeF threshold)
{
    // Bordas relativas ao centro: o encaixe pelo centro dá exatamente o valor do alvo.
    ElementBox origin = box;
    origin.center = QPointF();
    const QRectF offsets = CanvasLayout::bounds(origin, aspect);
    QList<QRectF> otherBounds;
    for (const ElementBox &o : others)
        if (o.id != box.id)
            otherBounds.append(CanvasLayout::bounds(o, aspect));

    const auto targets = [&](bool xAxis) {
        using K = SnapGuide::Kind;
        QList<Target> t{{0, K::CanvasCenter}, {-1, K::CanvasEdge}, {1, K::CanvasEdge},
                        {-safeMargin, K::SafeMargin}, {safeMargin, K::SafeMargin}};
        for (const QRectF &r : otherBounds) {
            if (xAxis)
                t += {{r.left(), K::Element}, {r.center().x(), K::Element}, {r.right(), K::Element}};
            else
                t += {{r.top(), K::Element}, {r.center().y(), K::Element}, {r.bottom(), K::Element}};
        }
        return t;
    };
    const AxisResult x = snapAxis(proposed.x(), {offsets.left(), 0, offsets.right()}, targets(true),
                                  threshold.width(), SnapGuide::Axis::Vertical);
    const AxisResult y = snapAxis(proposed.y(), {offsets.top(), 0, offsets.bottom()}, targets(false),
                                  threshold.height(), SnapGuide::Axis::Horizontal);
    return {QPointF(x.center, y.center), x.guides + y.guides};
}

Snapping::RotationResult Snapping::snapRotation(double degrees, double threshold)
{
    const double nearest = std::round(degrees / 45) * 45;
    return std::abs(nearest - degrees) < threshold ? RotationResult{nearest, true} : RotationResult{degrees, false};
}

} // namespace of
