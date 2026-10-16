// Manipulação no preview e resolução do preview: a parte do `EditorModel` que
// corresponde à extensão do `CanvasOverlay.swift` e ao `PreviewResolution`.

#include "CoreBridge.h"
#include "EditorModel.h"
#include "ProjectStore.h"

#include <cmath>

namespace of {

namespace {

QString idString(const QUuid &id) { return ProjectStore::uuidString(id); }

/// Degraus do lado menor do preview (o `PreviewResolution` do Mac), sem o teto da exportação.
constexpr int resolutionSteps[] = {540, 720, 1080, 1440, 2160};

int shortSideFor(QSizeF points, qreal scale)
{
    const int physical = int(std::ceil(std::min(points.width(), points.height()) * scale));
    for (int step : resolutionSteps)
        if (step >= physical)
            return step;
    return resolutionSteps[std::size(resolutionSteps) - 1];
}

} // namespace

QList<ElementBox> EditorModel::canvasBoxes() const
{
    return CanvasCore::boxes(m_project, m_playhead);
}

std::optional<SelectionFrame> EditorModel::selectionFrame(const CanvasMapping &mapping) const
{
    if (m_playing || !m_selection)
        return std::nullopt;
    for (ElementBox box : canvasBoxes()) {
        if (box.id != *m_selection)
            continue;
        if (m_canvasWidthDrag)
            box.size.setWidth(m_canvasWidthDrag->frameWidth * 2);
        return SelectionFrame(box, mapping, m_project.aspectValue());
    }
    return std::nullopt;
}

void EditorModel::editText(const QUuid &id)
{
    setSelection(idString(id));
    setInspectorTab(QStringLiteral("text"));
}

void EditorModel::canvasPointerDown(QPointF p, const CanvasMapping &mapping, int clickCount)
{
    if (m_playing)
        return;
    const QList<ElementBox> boxes = canvasBoxes();
    const double aspect = m_project.aspectValue();
    std::optional<std::pair<ElementBox, OverlayHandle>> target;
    for (const ElementBox &box : boxes) {
        if (!m_selection || box.id != *m_selection)
            continue;
        if (const auto handle = SelectionFrame(box, mapping, aspect).handle(p))
            target = {box, *handle};
    }
    if (!target) {
        if (const auto id = CanvasCore::hitTest(mapping.canvas(p), boxes, aspect))
            for (const ElementBox &box : boxes)
                if (box.id == *id)
                    target = {box, OverlayHandle::body()};
    }
    if (!target) {
        setSelection({});
        return;
    }
    const ElementBox &box = target->first;
    if (clickCount >= 2 && box.isText)
        editText(box.id);
    else
        setSelection(idString(box.id));

    // Largura de onde parte o arraste lateral. Numa legenda automática, a da faixa
    // (partir do texto curto desta legenda estreitaria todas); num texto solto, a
    // caixa que se vê.
    double startWidth = 0;
    if (box.isText)
        if (const auto seg = m_project.segment(box.id); seg && seg->text) {
            const double maxWidth = seg->text->style.maxWidth;
            startWidth = CanvasCore::isInAutoCaptions(m_project, box.id) ? maxWidth
                                                                         : std::min(box.size.width() / 2, maxWidth);
        }
    QList<ElementBox> others;
    for (const ElementBox &b : boxes)
        if (b.id != box.id)
            others.append(b);
    m_placementDrag.emplace(target->second, box, others, p, mapping, startWidth);
    beginGesture();
    emit canvasOverlayChanged();
}

void EditorModel::canvasPointerMoved(QPointF p, const CanvasMapping &mapping, bool snapping)
{
    if (!m_placementDrag)
        return;
    const PlacementDrag drag = *m_placementDrag;
    const auto result = drag.placement(p, mapping, m_project.aspectValue(), snapping);
    bool changed = false;
    if (m_canvasGuides != result.guides) {
        m_canvasGuides = result.guides;
        changed = true;
    }
    if (m_canvasRotationLabel != result.rotationLabel) {
        m_canvasRotationLabel = result.rotationLabel;
        changed = true;
    }
    if (drag.handle.kind == OverlayHandle::Kind::Edge && result.width && result.frameWidth) {
        // O clique sem arrastar não muda nada (nem cria um passo de desfazer).
        if (p == drag.startPoint && !m_canvasWidthDrag) {
            if (changed)
                emit canvasOverlayChanged();
            return;
        }
        m_canvasWidthDrag = WidthDragState{*result.width, *result.frameWidth,
                                           CanvasCore::isInAutoCaptions(m_project, drag.box.id)};
        updateGesture([&](Project &proj) { CanvasCore::setTextWidth(proj, drag.box.id, *result.width); });
        emit canvasOverlayChanged();
        return;
    }
    updateGesture([&](Project &proj) {
        CanvasCore::setPlacement(proj, drag.box.id, result.center, result.scale, result.rotation);
    });
    if (changed)
        emit canvasOverlayChanged();
}

void EditorModel::canvasPointerUp()
{
    if (!m_placementDrag)
        return;
    m_placementDrag.reset();
    m_canvasGuides.clear();
    m_canvasRotationLabel.reset();
    m_canvasWidthDrag.reset();
    endGesture();
    emit canvasOverlayChanged();
}

// MARK: - Resolução do preview

void EditorModel::previewViewportChanged(qreal width, qreal height, qreal devicePixelRatio)
{
    if (width <= 1 || height <= 1)
        return;
    const bool first = m_previewPoints.isEmpty();
    m_previewPoints = QSizeF(width, height);
    m_previewScale = devicePixelRatio > 0 ? devicePixelRatio : 1;
    if (first) {
        updatePreviewResolution();
        return;
    }
    m_previewResolutionTimer.start();
}

void EditorModel::updatePreviewResolution()
{
    setPreviewShortSide(shortSideFor(m_previewPoints, m_previewScale));
}

int EditorModel::previewShortSide() const
{
    return m_preview->previewShortSide();
}

void EditorModel::setPreviewShortSide(int side)
{
    m_preview->setPreviewShortSide(side);
}

} // namespace of
