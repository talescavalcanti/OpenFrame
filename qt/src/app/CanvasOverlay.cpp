#include "CanvasOverlay.h"

#include "EditorModel.h"

#include <QCursor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <algorithm>

namespace of {

namespace {

const QColor accent(0x00, 0xC2, 0xCC);

EditorModel *editor()
{
    return EditorModel::instance();
}

void drawGuide(QPainter *painter, const OverlayGuide &guide, QSizeF size)
{
    QColor color = accent;
    color.setAlphaF(guide.strong ? 1 : 0.7);
    QPen pen(color, guide.strong ? 1.25 : 0.75);
    if (guide.dashed)
        pen.setDashPattern({4 / pen.widthF(), 3 / pen.widthF()});
    painter->setPen(pen);
    if (guide.vertical)
        painter->drawLine(QPointF(guide.position, 0), QPointF(guide.position, size.height()));
    else
        painter->drawLine(QPointF(0, guide.position), QPointF(size.width(), guide.position));
}

/// Etiqueta em cápsula centrada em `center`, empurrada para dentro de `bounds` (o
/// item recorta o que passa da borda; no Mac, o SwiftUI deixava vazar).
void drawLabel(QPainter *painter, const QString &text, QPointF center, const QSizeF &bounds, const QColor &fill,
               const QColor &textColor, const QColor &border = Qt::transparent)
{
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(11);
    font.setWeight(QFont::Medium);
    font.setFeature(QFont::Tag("tnum"), 1);
    const QFontMetricsF fm(font);
    const double w = fm.horizontalAdvance(text) + 16, h = 20;
    const double margin = 2;
    center.setX(std::clamp(center.x(), w / 2 + margin, std::max(w / 2 + margin, bounds.width() - w / 2 - margin)));
    center.setY(std::clamp(center.y(), h / 2 + margin, std::max(h / 2 + margin, bounds.height() - h / 2 - margin)));
    const QRectF rect(center.x() - w / 2, center.y() - h / 2, w, h);
    painter->setPen(border.alpha() > 0 ? QPen(border, 1) : Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, h / 2, h / 2);
    painter->setFont(font);
    painter->setPen(textColor);
    painter->drawText(rect, Qt::AlignCenter, text);
}

} // namespace

CanvasOverlay::CanvasOverlay(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    if (EditorModel *model = editor()) {
        for (auto signal : {&EditorModel::projectChanged, &EditorModel::selectionChanged,
                            &EditorModel::playheadChanged, &EditorModel::playingChanged,
                            &EditorModel::canvasOverlayChanged})
            connect(model, signal, this, [this] { update(); });
    }
    connect(this, &QQuickItem::windowChanged, this, [this] { reportViewport(); });
}

void CanvasOverlay::draw(QPainter *painter, const EditorModel *model, const CanvasMapping &mapping)
{
    if (!model)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    for (const SnapGuide &guide : model->canvasGuides())
        drawGuide(painter, OverlayGuide(guide, mapping), mapping.size);
    const auto frame = model->selectionFrame(mapping);
    if (!frame)
        return;

    QPainterPath outline;
    outline.moveTo(frame->corners[0]);
    for (size_t i = 1; i < 4; ++i)
        outline.lineTo(frame->corners[i]);
    outline.closeSubpath();
    // Contorno escuro por baixo: fica visível sobre fundos claros.
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(0, 0, 0, 90), 3));
    painter->drawPath(outline);
    painter->setPen(QPen(accent, 1.25));
    painter->drawPath(outline);
    painter->setPen(QPen(accent, 1));
    painter->drawLine(frame->topCenter(), frame->rotationHandle());

    const QPen handlePen(accent, 1.5);
    if (frame->resizesWidth) {
        // Pílula vertical curta, girada com a caixa: diferente dos cantos quadrados.
        for (EdgeSide side : {EdgeSide::Left, EdgeSide::Right}) {
            painter->save();
            painter->translate(frame->edgeHandle(side));
            painter->rotate(frame->rotation);
            painter->setPen(handlePen);
            painter->setBrush(Qt::white);
            painter->drawRoundedRect(QRectF(-3, -8, 6, 16), 3, 3);
            painter->restore();
        }
    }
    painter->setPen(handlePen);
    painter->setBrush(Qt::white);
    for (const QPointF &c : frame->corners)
        painter->drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9), 2.5, 2.5);
    const QPointF knob = frame->rotationHandle();
    painter->drawEllipse(knob, 8, 8);
    QFont icons(QStringLiteral("lucide"));
    icons.setPixelSize(10);
    painter->setFont(icons);
    painter->setPen(Qt::black);
    painter->drawText(QRectF(knob.x() - 8, knob.y() - 8, 16, 16), Qt::AlignCenter, QString(QChar(0xe149)));

    if (const auto degrees = model->canvasRotationLabel())
        drawLabel(painter, QString::number(std::lround(*degrees)) + QStringLiteral("°"),
                  frame->rotationHandle() + frame->up() * 22, mapping.size, accent, Qt::black);
    if (const auto width = model->canvasWidthDrag()) {
        QColor border = accent;
        border.setAlphaF(0.7);
        drawLabel(painter, width->label(), frame->bottomCenter() - frame->up() * 22, mapping.size,
                  QColor(0, 0, 0, 166), Qt::white, border);
    }
}

void CanvasOverlay::paint(QPainter *painter)
{
    draw(painter, editor(), mapping());
}

void CanvasOverlay::mousePressEvent(QMouseEvent *event)
{
    EditorModel *model = editor();
    if (!model || model->isPlaying()) {
        event->ignore();
        return;
    }
    model->canvasPointerDown(event->position(), mapping(), 1);
    updateCursor(event->position());
    event->accept();
}

void CanvasOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    EditorModel *model = editor();
    if (!model || model->isPlaying()) {
        event->ignore();
        return;
    }
    model->canvasPointerDown(event->position(), mapping(), 2);
    event->accept();
}

void CanvasOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (EditorModel *model = editor())
        model->canvasPointerMoved(event->position(), mapping(), !(event->modifiers() & Qt::ControlModifier));
    updateCursor(event->position());
}

void CanvasOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (EditorModel *model = editor())
        model->canvasPointerUp();
    updateCursor(event->position());
}

void CanvasOverlay::hoverMoveEvent(QHoverEvent *event)
{
    updateCursor(event->position());
}

void CanvasOverlay::updateCursor(QPointF p)
{
    EditorModel *model = editor();
    if (!model || model->isPlaying()) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const auto frame = model->selectionFrame(mapping());
    const bool upright = !frame || frame->isUpright();
    if (const auto &drag = model->placementDrag()) {
        setCursor(drag->handle.kind == OverlayHandle::Kind::Edge && upright ? Qt::SizeHorCursor
                                                                           : Qt::ClosedHandCursor);
        return;
    }
    const auto handle = frame ? frame->handle(p) : std::nullopt;
    if (!handle) {
        const auto hit = CanvasCore::hitTest(mapping().canvas(p), model->canvasBoxes(), model->aspectValue());
        setCursor(hit ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    switch (handle->kind) {
    case OverlayHandle::Kind::Rotate:
    case OverlayHandle::Kind::Corner: setCursor(Qt::CrossCursor); break;
    case OverlayHandle::Kind::Edge: setCursor(upright ? Qt::SizeHorCursor : Qt::CrossCursor); break;
    case OverlayHandle::Kind::Body: setCursor(Qt::OpenHandCursor); break;
    }
}

void CanvasOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        reportViewport();
}

void CanvasOverlay::reportViewport()
{
    if (EditorModel *model = editor())
        model->previewViewportChanged(width(), height(), window() ? window()->effectiveDevicePixelRatio() : 1);
}

} // namespace of
