#include "CanvasGeometry.h"

#include <algorithm>
#include <cmath>

namespace of {

double length(QPointF v)
{
    return std::hypot(v.x(), v.y());
}

// MARK: - CanvasMapping

QPointF CanvasMapping::view(QPointF p) const
{
    return QPointF((p.x() + 1) / 2 * size.width(), (1 - p.y()) / 2 * size.height());
}

QPointF CanvasMapping::canvas(QPointF p) const
{
    return QPointF(p.x() / std::max(size.width(), 1.0) * 2 - 1, 1 - p.y() / std::max(size.height(), 1.0) * 2);
}

QSizeF CanvasMapping::canvasDelta(QSizeF d) const
{
    return QSizeF(d.width() * 2 / std::max(size.width(), 1.0), -d.height() * 2 / std::max(size.height(), 1.0));
}

QSizeF CanvasMapping::threshold(double points) const
{
    return QSizeF(points * 2 / std::max(size.width(), 1.0), points * 2 / std::max(size.height(), 1.0));
}

// MARK: - SelectionFrame

SelectionFrame::SelectionFrame(const ElementBox &box, const CanvasMapping &mapping, double aspect)
    : center(mapping.view(box.center)), rotation(box.rotation), resizesWidth(box.isText)
{
    const auto pts = CanvasCore::corners(box, aspect);
    for (size_t i = 0; i < 4; ++i)
        corners[i] = mapping.view(pts[i]);
}

QPointF SelectionFrame::rotate(QPointF v, double degrees)
{
    const double a = degrees * M_PI / 180;
    return QPointF(v.x() * std::cos(a) - v.y() * std::sin(a), v.x() * std::sin(a) + v.y() * std::cos(a));
}

QPointF SelectionFrame::edgeHandle(EdgeSide side) const
{
    return side == EdgeSide::Left ? (corners[0] + corners[3]) / 2 : (corners[1] + corners[2]) / 2;
}

bool SelectionFrame::isUpright() const
{
    const double r = std::abs(std::fmod(rotation, 180.0));
    return r < 1 || r > 179;
}

bool SelectionFrame::contains(QPointF p) const
{
    // Dentro do quadrilátero convexo: do mesmo lado de todas as arestas.
    double sign = 0;
    for (size_t i = 0; i < corners.size(); ++i) {
        const QPointF a = corners[i], b = corners[(i + 1) % corners.size()];
        const double cross = (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
        if (cross != 0) {
            if (sign == 0)
                sign = cross;
            else if ((cross > 0) != (sign > 0))
                return false;
        }
    }
    return true;
}

std::optional<OverlayHandle> SelectionFrame::handle(QPointF p, double tolerance) const
{
    if (length(p - rotationHandle()) <= tolerance + 3)
        return OverlayHandle::rotate();
    std::optional<std::pair<OverlayHandle, double>> nearest;
    auto consider = [&](OverlayHandle h, QPointF at) {
        const double d = length(p - at);
        if (!nearest || d < nearest->second)
            nearest = {h, d};
    };
    for (int i = 0; i < 4; ++i)
        consider(OverlayHandle::corner(i), corners[size_t(i)]);
    if (resizesWidth)
        for (EdgeSide side : {EdgeSide::Left, EdgeSide::Right})
            consider(OverlayHandle::edge(side), edgeHandle(side));
    if (nearest && nearest->second <= tolerance)
        return nearest->first;
    if (contains(p))
        return OverlayHandle::body();
    return std::nullopt;
}

// MARK: - OverlayGuide

OverlayGuide::OverlayGuide(const SnapGuide &guide, const CanvasMapping &mapping)
{
    vertical = guide.axis == SnapGuide::Axis::Vertical;
    const QPointF p = mapping.view(QPointF(guide.position, guide.position));
    position = vertical ? p.x() : p.y();
    strong = guide.kind == SnapGuide::Kind::CanvasCenter;
    dashed = guide.kind == SnapGuide::Kind::SafeMargin;
}

// MARK: - PlacementDrag

PlacementDrag::PlacementDrag(OverlayHandle handle, const ElementBox &box, const QList<ElementBox> &others,
                             QPointF point, const CanvasMapping &mapping, double startWidth)
    : handle(handle), box(box), others(others), startPoint(point), startCenterView(mapping.view(box.center)),
      startWidth(startWidth), startVisibleWidth(box.size.width() / 2)
{
}

PlacementDrag::Placement PlacementDrag::placement(QPointF p, const CanvasMapping &mapping, double aspect,
                                                  bool snapping) const
{
    Placement out{box.center, box.scale, box.rotation, {}, {}, {}, {}};
    switch (handle.kind) {
    case OverlayHandle::Kind::Body: {
        const QPointF proposed = proposedCenter(p, mapping);
        if (snapping) {
            const auto snapped = CanvasCore::snapMove(box, proposed, others, aspect, mapping.threshold(snapPoints));
            out.center = snapped.center;
            out.guides = snapped.guides;
        } else {
            out.center = proposed;
        }
        break;
    }
    case OverlayHandle::Kind::Corner:
        out.scale = proposedScale(p);
        break;
    case OverlayHandle::Kind::Rotate: {
        const double r = proposedRotation(p);
        out.rotation = snapping ? CanvasCore::snapRotation(r) : r;
        out.rotationLabel = out.rotation;
        break;
    }
    case OverlayHandle::Kind::Edge: {
        const double width = proposedWidth(p, handle.side(), mapping);
        out.width = width;
        // A moldura anda o mesmo que a largura, a partir da caixa visível.
        out.frameWidth = std::max(startVisibleWidth + width - startWidth, 0.01);
        break;
    }
    }
    return out;
}

double PlacementDrag::proposedWidth(QPointF p, EdgeSide side, const CanvasMapping &mapping) const
{
    // Simétrica em torno do centro: o quanto o ponteiro andou no eixo girado,
    // para fora, conta dos dois lados. Desfaz a escala do elemento.
    const QPointF axis = SelectionFrame::rotate(QPointF(1, 0), box.rotation);
    const QPointF d = p - startPoint;
    const double outward = (d.x() * axis.x() + d.y() * axis.y()) * (side == EdgeSide::Left ? -1 : 1);
    const double pointsPerWidth = std::max(std::abs(box.scale), 0.01) * mapping.size.width();
    const double w = startWidth + 2 * outward / std::max(pointsPerWidth, 1.0);
    return std::clamp(w, CanvasCore::minTextWidth, CanvasCore::maxTextWidth);
}

QPointF PlacementDrag::proposedCenter(QPointF p, const CanvasMapping &mapping) const
{
    const QSizeF d = mapping.canvasDelta(QSizeF(p.x() - startPoint.x(), p.y() - startPoint.y()));
    return QPointF(box.center.x() + d.width(), box.center.y() + d.height());
}

double PlacementDrag::proposedScale(QPointF p) const
{
    const double start = length(startPoint - startCenterView);
    if (start <= 1)
        return box.scale;
    const double s = box.scale * length(p - startCenterView) / start;
    return std::clamp(s, minScale, maxScale);
}

double PlacementDrag::proposedRotation(QPointF p) const
{
    auto angle = [&](QPointF q) { return std::atan2(q.y() - startCenterView.y(), q.x() - startCenterView.x()); };
    const double delta = (angle(p) - angle(startPoint)) * 180 / M_PI;
    return normalized(box.rotation + delta);
}

double PlacementDrag::normalized(double degrees)
{
    double d = std::fmod(degrees, 360.0);
    if (d > 180)
        d -= 360;
    if (d <= -180)
        d += 360;
    return d;
}

// MARK: - WidthDragState

QString WidthDragState::label() const
{
    const QString percent = QString::number(std::lround(width * 100)) + QStringLiteral("%");
    return wholeTrack ? percent + QStringLiteral(" · todas as legendas") : percent;
}

} // namespace of
