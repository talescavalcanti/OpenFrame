#pragma once

#include "CanvasGeometry.h"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace of {

class EditorModel;

/// Camada sobre o quadro do preview (do tamanho do canvas): recebe cliques e
/// arrastes, troca o cursor e desenha a seleção, as alças, as guias e as
/// etiquetas de graus e de largura. A regra do gesto fica no `EditorModel`.
/// Ctrl segurado desliga o ímã; durante a reprodução não responde.
class CanvasOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit CanvasOverlay(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    CanvasMapping mapping() const { return {QSizeF(width(), height())}; }

    /// Desenha o overlay atual num pintor com o tamanho dado (capturas compostas).
    static void draw(QPainter *painter, const EditorModel *model, const CanvasMapping &mapping);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void updateCursor(QPointF p);
    void reportViewport();
};

} // namespace of
