import CoreGraphics
import OpenFrameCore

/// Geometria vertical da timeline: a régua e, abaixo dela, as faixas na ordem de
/// exibição, cada uma com a sua altura. As posições são do conteúdo da timeline
/// (o topo da régua é 0).
struct TimelineLayout {
    static let rowSpacing: CGFloat = 6
    static let rulerHeight: CGFloat = 30
    static let headerWidth: CGFloat = 58
    static let clipInset: CGFloat = 4
    static let defaultRowHeight = CGFloat(Track.defaultHeight)
    /// Alturas do controle da barra da timeline (Normal = a padrão, sem altura própria).
    static let rowHeightPresets: [(name: String, height: CGFloat?)] = [
        ("Compacta", 36), ("Normal", nil), ("Alta", 90), ("Muito alta", 140),
    ]
    /// Espaço de coordenadas do conteúdo, para o arraste achar a linha sob o cursor.
    static let space = "timeline"

    let heights: [CGFloat]
    private let tops: [CGFloat]

    init(heights: [CGFloat]) {
        self.heights = heights
        var y = Self.rulerHeight + Self.rowSpacing
        var tops: [CGFloat] = []
        for height in heights {
            tops.append(y)
            y += height + Self.rowSpacing
        }
        tops.append(y)
        self.tops = tops
    }

    /// Topo da linha `row`; `heights.count` dá o fim da última linha (com o espaço depois dela).
    func top(_ row: Int) -> CGFloat {
        if row < 0 { return tops[0] + CGFloat(row) * (Self.defaultRowHeight + Self.rowSpacing) }
        if row < tops.count { return tops[row] }
        return tops[tops.count - 1] + CGFloat(row - heights.count) * (Self.defaultRowHeight + Self.rowSpacing)
    }

    func height(_ row: Int) -> CGFloat {
        heights.indices.contains(row) ? heights[row] : Self.defaultRowHeight
    }

    /// Linha sob a posição `y` (cada linha vai do topo dela ao topo da seguinte); nil
    /// sobre a régua. Abaixo da última faixa, devolve índices além dela.
    func row(atY y: CGFloat) -> Int? {
        guard y >= tops[0] else { return nil }
        if let row = tops.indices.dropLast().last(where: { tops[$0] <= y }), y < tops[row + 1] { return row }
        let end = tops[tops.count - 1]
        return heights.count + Int(((y - end) / (Self.defaultRowHeight + Self.rowSpacing)).rounded(.down))
    }

    /// Altura da régua com todas as faixas.
    var contentHeight: CGFloat { tops[tops.count - 1] - Self.rowSpacing }
}
