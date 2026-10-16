import CoreGraphics
import CoreText
import Foundation

// Referência da caixa do texto para a versão Qt: cada caso tem o conteúdo (texto, estilo e palavra-chave),
// o canvas e o `boxSize` medido aqui, mais as linhas (`lineTexts`). Todos usam a Inter que o Qt distribui
// (registrada a partir de `qt/src/core/fonts` e `qt/src/app/fonts`), para a comparação valer em qualquer sistema.
// Uso: qt/tests/tools/gerar-caixas.sh

// Argumentos: as pastas de fontes e, por último, o arquivo de saída.
let args = CommandLine.arguments
for dir in args.dropFirst().dropLast() {
    for file in try FileManager.default.contentsOfDirectory(at: URL(fileURLWithPath: dir), includingPropertiesForKeys: nil)
    where file.pathExtension == "ttf" && !file.lastPathComponent.hasPrefix("lucide") {
        CTFontManagerRegisterFontsForURL(file as CFURL, .process, nil)
    }
}

func inter(_ change: (inout TextStyle) -> Void = { _ in }) -> TextStyle {
    var s = TextStyle()
    s.fontName = "Inter"
    s.bold = false
    s.shadow = false
    change(&s)
    return s
}

let long = "O rato roeu a roupa do rei de Roma e a rainha, com raiva, resolveu remendar tudo sozinha"
var cases: [(String, TextContent, CGSize)] = []
func add(_ name: String, _ text: String, _ style: TextStyle, emphasis: WordEmphasis? = nil,
         canvas: CGSize = CGSize(width: 1920, height: 1080)) {
    var c = TextContent(text: text, style: style)
    c.emphasis = emphasis
    cases.append((name, c, canvas))
}

add("simples", "Caixa de texto", inter())
add("negrito-sintetico", "Caixa de texto", inter { $0.bold = true })
add("quebra", long, inter { $0.maxWidth = 0.4; $0.fontSize = 0.06 })
add("equilibrada", long, inter { $0.maxWidth = 0.4; $0.fontSize = 0.06; $0.balanceLines = true })
add("equilibrada-larga", long, inter { $0.maxWidth = 0.9; $0.fontSize = 0.06; $0.balanceLines = true })
add("quebras-manuais", "Primeira linha\nsegunda\nterceira linha maior", inter { $0.lineSpacing = -0.15 })
add("entrelinha-maior", "Uma\nduas", inter { $0.lineSpacing = 0.4 })
add("espacamento-light", "Cinema discreto e espaçado", inter { $0.fontFace = "Light"; $0.letterSpacing = 0.15 })
add("fundo", "Com fundo", inter { $0.backgroundEnabled = true; $0.backgroundPaddingX = 0.45; $0.backgroundPaddingY = 0.25 })
add("caixa-alta", "Caixa alta e acentuação", inter { $0.textCase = .upper })
add("espaco-sem-quebra", "gerar legendas automáticas de\u{00A0}manhã com\u{00A0}o editor", inter { $0.maxWidth = 0.3 })
add("fit-lines", "HOJE\nEU VOU\nMOSTRAR", inter {
    $0.fitLines = true; $0.fontSize = 0.2031 * 0.36; $0.maxWidth = 0.36 / (16.0 / 9); $0.lineSpacing = -0.15
    $0.textCase = .upper
})
add("fit-lines-limite", "A\nPALAVRA MUITO MAIS COMPRIDA QUE A CAIXA", inter {
    $0.fitLines = true; $0.fontSize = 0.0731; $0.maxWidth = 0.2025
})
var light = WordEmphasis(words: [1])
light.fontName = "Inter"
light.fontFace = "Light"
add("enfase", "uma palavra-chave aqui", inter(), emphasis: light)
var big = WordEmphasis(words: [2, 9])
big.fontName = "Inter"
big.fontFace = "Medium"
big.scale = 1.6
add("enfase-quebra", long, inter { $0.maxWidth = 0.5; $0.balanceLines = true }, emphasis: big)
var stacked = WordEmphasis(words: [2])
stacked.fontName = "Inter"
stacked.fontFace = ""
stacked.scale = 1
add("enfase-fit", "HOJE\nEU VOU\nMOSTRAR", inter {
    $0.fitLines = true; $0.fontSize = 0.0731; $0.maxWidth = 0.2025; $0.lineSpacing = -0.15
}, emphasis: stacked)
add("vertical-9x16", long, inter { $0.maxWidth = 0.9; $0.fontSize = 0.07 * 9 / 16; $0.balanceLines = true },
    canvas: CGSize(width: 1080, height: 1920))
// As predefinições das legendas com a Inter no lugar da fonte do Mac.
for preset in CaptionPreset.allCases {
    var style = CaptionBuilder.defaultStyle(preset)
    style.fontName = "Inter"
    let text = preset == .stacked ? "HOJE\nEU VOU\nMOSTRAR" : "Hoje eu vou mostrar como\u{00A0}gerar legendas automáticas"
    var emphasis: WordEmphasis?
    if preset == .editorial || preset == .stacked {
        emphasis = CaptionBuilder.emphasis(preset, style: style, accent: nil)
        emphasis!.fontName = "Inter"
        emphasis!.fontFace = preset == .editorial ? "Medium" : style.fontFace
        emphasis!.words = [preset == .editorial ? 5 : 2]
    }
    add("predefinicao-\(preset.rawValue)", text, style, emphasis: emphasis)
}

var out: [[String: Any]] = []
for (name, content, canvas) in cases {
    let size = TextRenderer.shared.boxSize(for: content, canvas: canvas)!
    let contentJSON = try JSONSerialization.jsonObject(with: JSONEncoder().encode(content))
    out.append(["name": name, "content": contentJSON, "canvas": [canvas.width, canvas.height],
                "box": [size.width, size.height],
                "lines": TextRenderer.shared.lineTexts(for: content, canvas: canvas)])
}
let data = try JSONSerialization.data(withJSONObject: ["cases": out], options: [.prettyPrinted, .sortedKeys])
try data.write(to: URL(fileURLWithPath: args.last!))
print("ok: \(out.count) casos")
