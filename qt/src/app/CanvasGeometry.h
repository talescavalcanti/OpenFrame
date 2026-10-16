#pragma once

#include "CanvasCore.h"

#include <QLineF>
#include <QSizeF>
#include <QString>
#include <array>
#include <optional>

// Manipulação direta no preview (porte do `CanvasOverlay.swift`): conversão de
// coordenadas, contorno do selecionado com as alças e o estado de um gesto. São
// tipos puros; o `EditorModel` guarda o gesto e o `CanvasOverlay` desenha.

namespace of {

/// Conversão entre o espaço normalizado do canvas (x e y de -1 a 1, centro 0,
/// y para cima) e os pontos do item do preview (origem no canto superior esquerdo).
struct CanvasMapping {
    QSizeF size;

    QPointF view(QPointF p) const;
    QPointF canvas(QPointF p) const;
    /// Deslocamento na tela convertido para o espaço normalizado.
    QSizeF canvasDelta(QSizeF d) const;
    /// Distância na tela convertida para o limiar normalizado de cada eixo.
    QSizeF threshold(double points) const;
};

enum class EdgeSide { Left, Right };

struct OverlayHandle {
    enum class Kind { Body, Corner, Rotate, Edge };
    Kind kind = Kind::Body;
    /// Índice do canto (0…3) ou lado (0 = esquerdo, 1 = direito).
    int index = 0;

    static OverlayHandle body() { return {Kind::Body, 0}; }
    static OverlayHandle corner(int i) { return {Kind::Corner, i}; }
    static OverlayHandle rotate() { return {Kind::Rotate, 0}; }
    static OverlayHandle edge(EdgeSide side) { return {Kind::Edge, side == EdgeSide::Left ? 0 : 1}; }
    EdgeSide side() const { return index == 0 ? EdgeSide::Left : EdgeSide::Right; }
    bool operator==(const OverlayHandle &) const = default;
};

/// Contorno do elemento selecionado, em pontos do item.
struct SelectionFrame {
    QPointF center;
    /// Superior esquerdo, superior direito, inferior direito, inferior esquerdo (antes de girar).
    std::array<QPointF, 4> corners;
    /// Graus, positivo = horário na tela.
    double rotation = 0;
    /// Alças no meio das laterais para mudar a largura da caixa (só em texto).
    bool resizesWidth = false;

    static constexpr double rotationHandleGap = 26;

    SelectionFrame(const ElementBox &box, const CanvasMapping &mapping, double aspect);

    /// Gira um vetor em graus no sentido horário da tela (y para baixo).
    static QPointF rotate(QPointF v, double degrees);

    QPointF topCenter() const { return (corners[0] + corners[1]) / 2; }
    QPointF bottomCenter() const { return (corners[2] + corners[3]) / 2; }
    /// Direções "para cima" e "para a direita" do elemento girado, unitárias.
    QPointF up() const { return rotate(QPointF(0, -1), rotation); }
    QPointF right() const { return rotate(QPointF(1, 0), rotation); }
    QPointF rotationHandle() const { return topCenter() + up() * rotationHandleGap; }
    QPointF edgeHandle(EdgeSide side) const;
    /// Girada 0° ou 180°: as alças laterais andam na horizontal da tela.
    bool isUpright() const;
    bool contains(QPointF p) const;
    /// Alça sob o ponto: alças primeiro (com folga; a mais próxima ganha), depois o corpo.
    std::optional<OverlayHandle> handle(QPointF p, double tolerance = 9) const;
};

/// Linha de guia em pontos do item.
struct OverlayGuide {
    bool vertical = true;
    double position = 0;
    /// Centro do canvas: desenhada mais forte.
    bool strong = false;
    bool dashed = false;

    OverlayGuide(const SnapGuide &guide, const CanvasMapping &mapping);
};

/// Estado de um gesto no preview. Cada atualização parte do estado inicial,
/// como os gestos da timeline.
struct PlacementDrag {
    OverlayHandle handle;
    /// Elemento no início do gesto e os demais (alvos das guias).
    ElementBox box;
    QList<ElementBox> others;
    /// Ponto inicial do ponteiro e centro do elemento, em pontos do item.
    QPointF startPoint;
    QPointF startCenterView;
    /// Largura de onde parte o arraste lateral e a da caixa visível no início
    /// (a moldura parte dela, para a alça não pular quando as duas diferem).
    double startWidth = 0;
    double startVisibleWidth = 0;

    static constexpr double minScale = 0.05;
    static constexpr double maxScale = 10;
    /// Distância de encaixe na tela.
    static constexpr double snapPoints = 6;

    struct Placement {
        QPointF center;
        double scale = 1;
        double rotation = 0;
        QList<SnapGuide> guides;
        /// Graus mostrados na etiqueta durante o giro.
        std::optional<double> rotationLabel;
        /// Largura pedida pelas alças laterais e a da moldura.
        std::optional<double> width;
        std::optional<double> frameWidth;
    };

    PlacementDrag(OverlayHandle handle, const ElementBox &box, const QList<ElementBox> &others, QPointF point,
                  const CanvasMapping &mapping, double startWidth = 0);

    Placement placement(QPointF p, const CanvasMapping &mapping, double aspect, bool snapping) const;
    double proposedWidth(QPointF p, EdgeSide side, const CanvasMapping &mapping) const;
    QPointF proposedCenter(QPointF p, const CanvasMapping &mapping) const;
    double proposedScale(QPointF p) const;
    double proposedRotation(QPointF p) const;
    static double normalized(double degrees);
};

/// Estado do arraste lateral mostrado no preview.
struct WidthDragState {
    /// Largura pedida e a da moldura (frações da largura do canvas).
    double width = 0;
    double frameWidth = 0;
    /// Faixa de legendas automáticas: a largura vale para todas.
    bool wholeTrack = false;

    QString label() const;
    bool operator==(const WidthDragState &) const = default;
};

double length(QPointF v);

} // namespace of
