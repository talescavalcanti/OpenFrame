#pragma once

#include "Project.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace of {

/// Caixa de um elemento visual em repouso (sem animação), no espaço normalizado do canvas:
/// x e y vão de -1 a 1 (centro 0, y para cima), como `Transform` e `TextStyle`.
struct ElementBox {
    QUuid id;
    bool isText = false;
    QPointF center;
    /// Tamanho com escala 1 e sem rotação, nas mesmas unidades (a largura do canvas inteiro vale 2).
    /// No texto, é a caixa do texto (mais o fundo, se ligado), sem as margens de sombra.
    QSizeF size;
    double scale = 1;
    /// Graus, positivo = horário na tela (o mesmo sentido de `Transform::rotation`).
    double rotation = 0;
    bool operator==(const ElementBox &) const = default;
};

/// Geometria dos elementos no canvas, para selecionar, desenhar contornos e alinhar no preview.
/// Segue as mesmas contas do render: a rotação acontece em pixels, por isso as funções que giram
/// recebem a proporção do canvas (largura/altura).
namespace CanvasLayout {
/// Lado menor do canvas de referência para medir texto.
inline constexpr int referenceShortSide = 1080;

/// Elementos visíveis no instante, na ordem de desenho (o primeiro fica embaixo): foto e vídeo de todas
/// as faixas de vídeo visíveis e depois os textos. Ignora faixas ocultas.
QList<ElementBox> boxes(const Project &project, Micros time);

/// Tamanho normalizado de uma mídia encaixada (`fit`) no canvas.
QSizeF fitSize(double mediaAspect, double canvasAspect);

/// Id do elemento mais acima sob o ponto (normalizado); vazio se nenhum.
std::optional<QUuid> hitTest(QPointF point, const QList<ElementBox> &boxes, double aspect);

/// Os 4 cantos da caixa girada e escalada, em coordenadas normalizadas. Ordem: superior esquerdo,
/// superior direito, inferior direito, inferior esquerdo (antes de girar).
QList<QPointF> corners(const ElementBox &box, double aspect);

/// Caixa alinhada aos eixos que envolve o elemento já girado (y para cima: `top()` é o menor y).
QRectF bounds(const ElementBox &box, double aspect);
} // namespace CanvasLayout

struct SnapGuide {
    /// `Vertical` = linha em x constante.
    enum class Axis { Vertical, Horizontal };
    enum class Kind { CanvasCenter, CanvasEdge, SafeMargin, Element };
    Axis axis = Axis::Vertical;
    /// Normalizado.
    double position = 0;
    Kind kind = Kind::CanvasCenter;
    bool operator==(const SnapGuide &) const = default;
};

/// Ímã do preview: alinha elementos ao canvas e entre si, e a rotação a múltiplos de 45°.
namespace Snapping {
/// Margem de segurança, como fração do meio canvas.
inline constexpr double safeMargin = 0.9;

struct MoveResult {
    QPointF center;
    QList<SnapGuide> guides;
};

/// Encaixa o centro proposto. Alvos: centro do canvas (0), bordas (±1), margem de segurança (±0,9),
/// e o centro e as bordas dos outros elementos. Valem as bordas e o centro da caixa alinhada aos eixos
/// (já girada) do elemento. `threshold` é normalizado por eixo. O alvo mais próximo vence em cada eixo,
/// e as guias devolvidas são as dos alvos encaixados (uma por posição, a mais forte).
MoveResult snapMove(const ElementBox &box, QPointF proposed, const QList<ElementBox> &others, double aspect,
                    QSizeF threshold);

struct RotationResult {
    double degrees = 0;
    bool snapped = false;
};

/// Encaixa em múltiplos de 45° quando está a menos de `threshold` graus.
RotationResult snapRotation(double degrees, double threshold = 4);
} // namespace Snapping

} // namespace of
