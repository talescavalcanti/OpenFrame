#pragma once

#include "CanvasLayout.h"
#include "Project.h"

#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QUuid>
#include <array>
#include <optional>

// Única ligação do overlay do preview com a geometria do núcleo (`CanvasLayout`,
// `Snapping` e as edições do `TimelineEditor`). O resto do app só chama
// `of::CanvasCore`.

namespace of::CanvasCore {

/// Faixa de `TextStyle::maxWidth` aceita pelas alças laterais.
inline constexpr double minTextWidth = TextStyle::maxWidthMin;
inline constexpr double maxTextWidth = TextStyle::maxWidthMax;

/// Elementos visíveis no instante, na ordem de desenho (o primeiro fica embaixo).
QList<ElementBox> boxes(const Project &project, Micros time);
/// Cantos normalizados da caixa girada: superior esquerdo, superior direito,
/// inferior direito, inferior esquerdo.
std::array<QPointF, 4> corners(const ElementBox &box, double aspect);
/// Id do elemento mais acima sob o ponto (normalizado).
std::optional<QUuid> hitTest(QPointF point, const QList<ElementBox> &boxes, double aspect);

struct SnappedMove {
    QPointF center;
    QList<SnapGuide> guides;
};
SnappedMove snapMove(const ElementBox &box, QPointF proposed, const QList<ElementBox> &others, double aspect,
                     QSizeF threshold);
double snapRotation(double degrees);

/// Posição (centro normalizado), escala e rotação de um clipe ou texto.
void setPlacement(Project &p, const QUuid &id, QPointF center, double scale, double rotation);
/// Largura de quebra do texto (fração da largura do canvas); numa faixa automática, vale para todas.
void setTextWidth(Project &p, const QUuid &id, double maxWidth);
/// O texto está numa faixa de legendas automáticas (a largura vale para todas).
bool isInAutoCaptions(const Project &p, const QUuid &id);

} // namespace of::CanvasCore
