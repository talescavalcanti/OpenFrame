#include "CanvasCore.h"

#include "TimelineEditor.h"

namespace of::CanvasCore {

QList<ElementBox> boxes(const Project &project, Micros time)
{
    return CanvasLayout::boxes(project, time);
}

std::array<QPointF, 4> corners(const ElementBox &box, double aspect)
{
    const QList<QPointF> pts = CanvasLayout::corners(box, aspect);
    return {pts.value(0), pts.value(1), pts.value(2), pts.value(3)};
}

std::optional<QUuid> hitTest(QPointF point, const QList<ElementBox> &boxes, double aspect)
{
    return CanvasLayout::hitTest(point, boxes, aspect);
}

SnappedMove snapMove(const ElementBox &box, QPointF proposed, const QList<ElementBox> &others, double aspect,
                     QSizeF threshold)
{
    const auto snapped = Snapping::snapMove(box, proposed, others, aspect, threshold);
    return {snapped.center, snapped.guides};
}

double snapRotation(double degrees)
{
    return Snapping::snapRotation(degrees).degrees;
}

void setPlacement(Project &p, const QUuid &id, QPointF center, double scale, double rotation)
{
    TimelineEditor::setPlacement(p, id, center, scale, rotation);
}

void setTextWidth(Project &p, const QUuid &id, double maxWidth)
{
    TimelineEditor::setTextWidth(p, id, maxWidth);
}

bool isInAutoCaptions(const Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    return loc && p.tracks[loc->track].isAutoCaptions;
}

} // namespace of::CanvasCore
