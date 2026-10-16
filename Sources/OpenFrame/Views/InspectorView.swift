import OpenFrameCore
import SwiftUI

struct InspectorView: View {
    @Environment(EditorModel.self) private var model

    var body: some View {
        Group {
            if let seg = model.selectedSegment {
                SegmentInspector(segment: seg)
                    .id(seg.id)
            } else {
                ProjectInspector()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    }
}

// MARK: - Helpers de binding

private func seconds(_ b: Binding<Micros>) -> Binding<Double> {
    Binding(get: { b.wrappedValue.seconds }, set: { b.wrappedValue = Time.seconds($0) })
}

private func color(_ b: Binding<RGBA>) -> Binding<Color> {
    Binding(get: { b.wrappedValue.color }, set: { b.wrappedValue = RGBA($0) })
}

private let percent: (Double) -> String = { String(format: "%.0f%%", $0 * 100) }
private let signedPercent: (Double) -> String = { String(format: "%+.0f", $0 * 100) }
private let secondsFormat: (Double) -> String = { String(format: "%.1fs", $0) }

/// Cartão selecionável usado nas grades de filtros, efeitos, animações e transições.
struct ChoiceTile<Preview: View>: View {
    let title: String
    let selected: Bool
    let action: () -> Void
    /// Recebe se o ponteiro está sobre o cartão (miniaturas que tocam no hover).
    let preview: (Bool) -> Preview
    @State private var hovering = false

    init(title: String, selected: Bool, action: @escaping () -> Void, @ViewBuilder preview: @escaping () -> Preview) {
        self.init(title: title, selected: selected, action: action, hoverPreview: { _ in preview() })
    }

    init(title: String, selected: Bool, action: @escaping () -> Void,
         @ViewBuilder hoverPreview: @escaping (Bool) -> Preview) {
        self.title = title
        self.selected = selected
        self.action = action
        preview = hoverPreview
    }

    var body: some View {
        Button(action: action) {
            VStack(spacing: 6) {
                preview(hovering)
                    .frame(height: 50)
                    .frame(maxWidth: .infinity)
                    .background(Theme.raised)
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 8, style: .continuous)
                        .strokeBorder(selected ? Theme.accent : (hovering ? Color.white.opacity(0.22) : Theme.hairline),
                                      lineWidth: selected ? 1.5 : 1))
                Text(title)
                    .font(Theme.font(11, selected ? .medium : .regular))
                    .lineLimit(1)
                    .minimumScaleFactor(0.85)
                    .foregroundStyle(selected ? Theme.accent : (hovering ? Theme.textPrimary : Theme.textSecondary))
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

let tileColumns = [GridItem(.adaptive(minimum: 80), spacing: 8)]

/// Campo de texto de uma linha com a aparência dos campos do tema.
private struct InputField: View {
    let placeholder: String
    @Binding var text: String

    var body: some View {
        TextField(placeholder, text: $text)
            .textFieldStyle(.plain)
            .font(Theme.font(12))
            .padding(.horizontal, 10)
            .frame(height: 28)
            .field()
    }
}

// MARK: - Segmento

private struct SegmentInspector: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    var body: some View {
        let tabs = model.availableTabs(for: segment)
        let current = tabs.contains(model.inspectorTab) ? model.inspectorTab : (tabs.first ?? .basic)
        VStack(alignment: .leading, spacing: 0) {
            header
                .padding([.horizontal, .top], 14)
            if tabs.count > 1 {
                PillPicker(selection: Binding(get: { current }, set: { model.inspectorTab = $0 }),
                           options: tabs, title: \.title, icon: \.icon)
                    .padding(.horizontal, 14)
                    .padding(.top, 12)
            }
            Text(current.title)
                .font(Theme.font(16, .light))
                .padding(.horizontal, 14)
                .padding(.top, 14)
                .padding(.bottom, 2)
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    switch current {
                    case .basic: BasicTab(segment: segment)
                    case .adjust: AdjustTab(id: segment.id)
                    case .filters: FiltersTab(segment: segment)
                    case .effects: EffectsTab(segment: segment)
                    case .animation: AnimationTab(segment: segment)
                    case .transition: TransitionTab(segment: segment)
                    case .text: TextTab(id: segment.id)
                    case .audio: AudioTab(segment: segment)
                    }
                }
                .padding(.horizontal, 14)
                .padding(.top, Theme.scrollFade)
                .padding(.bottom, 16)
            }
            .scrollIndicators(.never)
            .scrollFadeTop()
        }
    }

    /// Ícone do tipo, nome e intervalo na timeline.
    private var header: some View {
        let material = model.project.material(segment.materialId)
        let icon = segment.text != nil ? "textformat"
            : material?.kind == .audio ? "music.note" : material?.kind == .photo ? "photo" : "film"
        let name = segment.text.map { $0.text.replacingOccurrences(of: "\n", with: " ") } ?? material?.name ?? "Clipe"
        return HStack(spacing: 10) {
            Image(systemName: icon)
                .font(Theme.font(12, .light))
                .frame(width: 30, height: 30)
                .field(radius: 9)
            VStack(alignment: .leading, spacing: 2) {
                Text(name)
                    .font(Theme.font(13))
                    .lineLimit(1)
                    .truncationMode(.middle)
                Text("\(Time.format(segment.targetStart)) – \(Time.format(segment.targetEnd)) · \(segment.targetDuration.shortSeconds)")
                    .font(Theme.mono(10))
                    .foregroundStyle(Theme.textTertiary)
            }
            Spacer(minLength: 0)
            Button { model.selection = nil } label: { Image(systemName: "xmark") }
                .buttonStyle(CircleButtonStyle(size: 24))
                .help("Fechar (mostra o projeto)")
        }
    }
}

private struct BasicTab: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    var body: some View {
        let id = segment.id
        SectionHeader(title: "Transformar") {
            model.mutate { TimelineEditor.update(&$0, segment: id) { $0.transform = Transform(); $0.opacity = 1 } }
        }
        ParamSlider(title: "Escala", value: model.binding(id, \.transform.scale), range: 0.1...4, defaultValue: 1, format: percent)
        ParamSlider(title: "Posição X", value: model.binding(id, \.transform.x), format: signedPercent)
        ParamSlider(title: "Posição Y", value: model.binding(id, \.transform.y), format: signedPercent)
        ParamSlider(title: "Rotação", value: model.binding(id, \.transform.rotation), range: -180...180,
                    format: { String(format: "%.0f°", $0) })
        ParamSlider(title: "Opacidade", value: model.binding(id, \.opacity), range: 0...1, defaultValue: 1, format: percent)

        SectionHeader(title: "Velocidade")
        SpeedPresets(id: id, speed: segment.speed)
        ParamSlider(title: "Velocidade", value: model.binding(id, \.speed), range: 0.1...4, defaultValue: 1,
                    format: { String(format: "%.2fx", $0) })

        if model.project.material(segment.materialId)?.hasAudio == true {
            AudioControls(segment: segment)
        }
    }
}

private struct SpeedPresets: View {
    @Environment(EditorModel.self) private var model
    let id: UUID
    let speed: Double

    var body: some View {
        PillPicker(selection: Binding(get: { speed }, set: { s in
            model.mutate { TimelineEditor.setSpeed(&$0, segment: id, speed: s) }
        }), options: [0.5, 1, 1.5, 2, 3], title: { String(format: "%gx", $0) })
    }
}

private struct AudioControls: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    var body: some View {
        let id = segment.id
        SectionHeader(title: "Som")
        if segment.audioDetached {
            Label("O áudio deste clipe foi extraído para a faixa de áudio.", systemImage: "speaker.slash")
                .font(Theme.font(11))
                .foregroundStyle(Theme.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
        } else {
            sliders(id)
        }
    }

    @ViewBuilder
    private func sliders(_ id: UUID) -> some View {
        ParamSlider(title: "Volume", value: model.binding(id, \.volume), range: 0...1, defaultValue: 1, format: percent)
        ParamSlider(title: "Fade de entrada", value: seconds(model.binding(id, \.fadeIn)), range: 0...5, format: secondsFormat)
        ParamSlider(title: "Fade de saída", value: seconds(model.binding(id, \.fadeOut)), range: 0...5, format: secondsFormat)
    }
}

private struct AudioTab: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    var body: some View {
        AudioControls(segment: segment)
        SectionHeader(title: "Velocidade")
        SpeedPresets(id: segment.id, speed: segment.speed)
        ParamSlider(title: "Velocidade", value: model.binding(segment.id, \.speed), range: 0.25...4, defaultValue: 1,
                    format: { String(format: "%.2fx", $0) })
        Text("O tom é preservado ao mudar a velocidade.")
            .font(Theme.font(11))
            .foregroundStyle(Theme.textTertiary)
    }
}

private struct AdjustTab: View {
    @Environment(EditorModel.self) private var model
    let id: UUID

    var body: some View {
        SectionHeader(title: "Luz e cor") {
            model.mutate { TimelineEditor.update(&$0, segment: id) { $0.adjust = Adjust() } }
        }
        ParamSlider(title: "Brilho", value: model.binding(id, \.adjust.brightness), format: signedPercent)
        ParamSlider(title: "Contraste", value: model.binding(id, \.adjust.contrast), format: signedPercent)
        ParamSlider(title: "Saturação", value: model.binding(id, \.adjust.saturation), format: signedPercent)
        ParamSlider(title: "Exposição", value: model.binding(id, \.adjust.exposure), format: signedPercent)
        ParamSlider(title: "Temperatura", value: model.binding(id, \.adjust.temperature), format: signedPercent)
        ParamSlider(title: "Matiz (tint)", value: model.binding(id, \.adjust.tint), format: signedPercent)
        ParamSlider(title: "Realces", value: model.binding(id, \.adjust.highlights), format: signedPercent)
        ParamSlider(title: "Sombras", value: model.binding(id, \.adjust.shadows), format: signedPercent)
        ParamSlider(title: "Tom (hue)", value: model.binding(id, \.adjust.hue), format: signedPercent)
        SectionHeader(title: "Detalhes")
        ParamSlider(title: "Nitidez", value: model.binding(id, \.adjust.sharpen), range: 0...1, format: percent)
        ParamSlider(title: "Vinheta", value: model.binding(id, \.adjust.vignette), range: 0...1, format: percent)
        ParamSlider(title: "Desbotar", value: model.binding(id, \.adjust.fade), range: 0...1, format: percent)
        ParamSlider(title: "Granulado", value: model.binding(id, \.adjust.grain), range: 0...1, format: percent)
    }
}

private struct FiltersTab: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment
    @State private var previews: [FilterPreset: CGImage] = [:]

    var body: some View {
        let id = segment.id
        let current = segment.filter?.preset ?? .none
        LazyVGrid(columns: tileColumns, spacing: 12) {
            ForEach(FilterPreset.allCases) { preset in
                ChoiceTile(title: preset.displayName, selected: current == preset) {
                    model.mutate {
                        TimelineEditor.update(&$0, segment: id) { seg in
                            seg.filter = preset == .none ? nil : FilterSetting(preset: preset, intensity: seg.filter?.intensity ?? 1)
                        }
                    }
                } preview: {
                    if let image = previews[preset] {
                        Image(decorative: image, scale: 1).resizable().aspectRatio(contentMode: .fill)
                    } else {
                        Image(systemName: "camera.filters").foregroundStyle(Theme.textTertiary)
                    }
                }
            }
        }
        .padding(.top, 8)
        .task(id: segment.materialId) { await makePreviews() }
        if segment.filter != nil {
            ParamSlider(title: "Intensidade", value: model.binding(id, \.filter!.intensity), range: 0...1,
                        defaultValue: 1, format: percent)
                .padding(.top, 6)
        }
    }

    private func makePreviews() async {
        guard let materialId = segment.materialId, let base = model.thumbnails[materialId]?.first else { return }
        let results = await Task.detached(priority: .userInitiated) {
            var out: [FilterPreset: CGImage] = [:]
            for preset in FilterPreset.allCases {
                out[preset] = preset == .none ? base : EffectPreview.filtered(base, preset: preset)
            }
            return out
        }.value
        previews = results
    }
}

private struct EffectsTab: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    private func icon(_ kind: VideoEffectKind) -> String {
        switch kind {
        case .blur: "drop"
        case .pixelate: "square.grid.3x3"
        case .rgbSplit: "camera.filters"
        case .mirror: "rectangle.split.2x1"
        case .shake: "waveform.path"
        case .flash: "bolt"
        case .zoomPulse: "plus.magnifyingglass"
        case .kaleidoscope: "hexagon"
        case .comic: "pencil.and.outline"
        case .edges: "scribble.variable"
        }
    }

    var body: some View {
        let id = segment.id
        LazyVGrid(columns: tileColumns, spacing: 12) {
            ForEach(VideoEffectKind.allCases) { kind in
                let active = segment.effects.contains { $0.kind == kind }
                ChoiceTile(title: kind.displayName, selected: active) {
                    model.mutate {
                        TimelineEditor.update(&$0, segment: id) { seg in
                            if let i = seg.effects.firstIndex(where: { $0.kind == kind }) {
                                seg.effects.remove(at: i)
                            } else {
                                seg.effects.append(VideoEffect(kind: kind))
                            }
                        }
                    }
                } preview: {
                    Image(systemName: icon(kind))
                        .font(Theme.font(17, .light))
                        .foregroundStyle(active ? Theme.accent : Theme.textSecondary)
                }
            }
        }
        .padding(.top, 8)
        if !segment.effects.isEmpty {
            SectionHeader(title: "Aplicados") {
                model.mutate { TimelineEditor.update(&$0, segment: id) { $0.effects = [] } }
            }
            ForEach(Array(segment.effects.enumerated()), id: \.element.id) { index, effect in
                ParamSlider(title: effect.kind.displayName, value: model.binding(id, \.effects[index].intensity),
                            range: 0...1, defaultValue: 0.5, format: percent)
            }
        }
    }
}

private struct TransitionTab: View {
    @Environment(EditorModel.self) private var model
    let segment: Segment

    var body: some View {
        let id = segment.id
        let current = segment.transition?.kind
        Text("Para o próximo clipe da faixa principal.")
            .font(Theme.font(11))
            .foregroundStyle(Theme.textTertiary)
        LazyVGrid(columns: tileColumns, spacing: 12) {
            ChoiceTile(title: "Nenhuma", selected: current == nil) {
                model.mutate { TimelineEditor.update(&$0, segment: id) { $0.transition = nil } }
            } preview: {
                Image(systemName: "nosign").font(Theme.font(15, .light)).foregroundStyle(Theme.textTertiary)
            }
            ForEach(TransitionKind.allCases) { kind in
                ChoiceTile(title: kind.displayName, selected: current == kind) {
                    model.mutate {
                        TimelineEditor.update(&$0, segment: id) { seg in
                            seg.transition = Transition(kind: kind, duration: seg.transition?.duration ?? 500_000)
                        }
                    }
                } preview: {
                    Image(systemName: "arrow.left.arrow.right")
                        .font(Theme.font(15, .light))
                        .foregroundStyle(current == kind ? Theme.accent : Theme.textSecondary)
                }
            }
        }
        if segment.transition != nil {
            ParamSlider(title: "Duração",
                        value: Binding(get: { segment.transition?.duration.seconds ?? 0.5 },
                                       set: { v in
                                           model.mutate(coalesce: "transition\(id)") {
                                               TimelineEditor.update(&$0, segment: id) { $0.transition?.duration = Time.seconds(v) }
                                           }
                                       }),
                        range: 0.1...2, defaultValue: 0.5, format: secondsFormat)
                .padding(.top, 6)
            Text("A transição sobrepõe o fim deste clipe ao início do próximo.")
                .font(Theme.font(11))
                .foregroundStyle(Theme.textTertiary)
        }
    }
}

private struct TextTab: View {
    @Environment(EditorModel.self) private var model
    let id: UUID

    /// Famílias à mão no topo do menu; as demais instaladas vêm depois.
    private static let suggested = ["Helvetica Neue", "Avenir Next", "Futura", "Gill Sans", "Arial Rounded MT Bold",
                                    "Impact", "Georgia", "Didot", "Times New Roman", "American Typewriter",
                                    "Menlo", "Marker Felt", "Chalkboard SE", "Noteworthy", "Copperplate"]
    private static let installed = FontCatalog.families()
    @MainActor private static var faceCache: [String: [String]] = [:]

    @MainActor private static func faces(of family: String) -> [String] {
        if let cached = faceCache[family] { return cached }
        let faces = FontCatalog.faces(of: family)
        faceCache[family] = faces
        return faces
    }

    private static let caseTitles: [TextCase: String] = [.none: "Normal", .upper: "AA", .lower: "aa", .title: "Título"]
    /// Medidas em em mostradas como % do tamanho da fonte.
    private static let em: (Double) -> String = { String(format: "%.0f", $0 * 100) }
    private static let signedEm: (Double) -> String = { $0 == 0 ? "0" : String(format: "%+.0f", $0 * 100) }

    private var style: Binding<TextStyle> { model.binding(id, \.text!.style) }

    private func value<T: Equatable>(_ keyPath: WritableKeyPath<TextStyle, T>) -> Binding<T> {
        let path = (\Segment.text!.style).appending(path: keyPath)
        return model.binding(id, path)
    }

    @FocusState private var editorFocused: Bool

    var body: some View {
        TextEditor(text: model.binding(id, \.text!.text))
            .font(Theme.font(13))
            .frame(height: 72)
            .scrollContentBackground(.hidden)
            .padding(6)
            .field()
            .focused($editorFocused)
            .onAppear(perform: takeFocusIfRequested)
            .onChange(of: model.focusTextEditor) { takeFocusIfRequested() }

        SectionHeader(title: "Estilos prontos")
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 72), spacing: 6)], spacing: 6) {
            ForEach(TextStylePreset.allCases) { preset in
                Button(preset.title) { style.wrappedValue = preset.applied(to: style.wrappedValue) }
                    .buttonStyle(PillButtonStyle(compact: true))
            }
        }

        fontSection
        spacingSection
        colorSection
        highlightSection
        emphasisSection
        backgroundSection
        shadowSection
        positionSection
    }

    private func takeFocusIfRequested() {
        guard model.focusTextEditor else { return }
        model.focusTextEditor = false
        // Depois do layout: a aba acabou de aparecer.
        DispatchQueue.main.async { editorFocused = true }
    }

    @ViewBuilder private var fontSection: some View {
        SectionHeader(title: "Fonte")
        FieldRow(label: "Família") { familyMenu }
        FieldRow(label: "Estilo") {
            // Um estilo da família ("Light", "Medium"…) substitui o negrito/itálico ligados.
            MenuField(selection: Binding(get: { style.wrappedValue.fontFace }, set: { face in
                var s = style.wrappedValue
                s.fontFace = face
                if !face.isEmpty { s.bold = false; s.italic = false }
                style.wrappedValue = s
            }), options: [""] + Self.faces(of: style.wrappedValue.fontName), title: { $0.isEmpty ? "Padrão" : $0 })
        }
        ParamSlider(title: "Tamanho", value: value(\.fontSize), range: 0.02...0.25,
                    defaultValue: 0.07, format: { String(format: "%.0f", $0 * 1000) })
        HStack(spacing: 6) {
            PillPicker(selection: value(\.alignment),
                       options: TextAlignmentKind.allCases,
                       title: { ["left": "Esquerda", "center": "Centro", "right": "Direita"][$0.rawValue] ?? "" },
                       icon: { "text.align\($0.rawValue)" })
            Toggle(isOn: value(\.bold)) { Image(systemName: "bold") }
                .toggleStyle(IconToggleStyle())
                .help("Negrito")
            Toggle(isOn: value(\.italic)) { Image(systemName: "italic") }
                .toggleStyle(IconToggleStyle())
                .help("Itálico")
            Toggle(isOn: value(\.underline)) { Image(systemName: "underline") }
                .toggleStyle(IconToggleStyle())
                .help("Sublinhado")
        }
        // Na largura toda: ao lado de um rótulo, "Normal" não cabia na pílula.
        PillPicker(selection: value(\.textCase), options: TextCase.allCases, title: { Self.caseTitles[$0] ?? "" })
            .help("Maiúsculas e minúsculas")
    }

    private var familyMenu: some View {
        FontFamilyMenu(current: style.wrappedValue.fontName, suggested: Self.suggested, installed: Self.installed) { family in
            var s = style.wrappedValue
            s.fontName = family
            if !Self.faces(of: family).contains(s.fontFace) { s.fontFace = "" }
            style.wrappedValue = s
        }
    }

    @ViewBuilder private var spacingSection: some View {
        SectionHeader(title: "Espaçamento") {
            model.mutate {
                TimelineEditor.update(&$0, segment: id) {
                    $0.text?.style.letterSpacing = 0
                    $0.text?.style.lineSpacing = 0
                }
            }
        }
        ParamSlider(title: "Entre letras", value: value(\.letterSpacing), range: -0.5...2, format: Self.signedEm)
        ParamSlider(title: "Entre linhas", value: value(\.lineSpacing), range: -0.5...3, format: Self.signedEm)
    }

    @ViewBuilder private var colorSection: some View {
        SectionHeader(title: "Cor")
        ColorRow(title: "Texto", color: color(value(\.color)))
        ParamSlider(title: "Opacidade", value: value(\.opacity), range: 0...1, defaultValue: 1, format: percent)

        SectionHeader(title: "Contorno")
        ParamSlider(title: "Espessura", value: value(\.strokeWidth), range: 0...20, format: { String(format: "%.1f", $0) })
        if style.wrappedValue.strokeWidth > 0 {
            ColorRow(title: "Cor", color: color(value(\.strokeColor)))
        }
    }

    /// Só para textos com tempo por palavra (legendas automáticas).
    @ViewBuilder private var highlightSection: some View {
        if let text = model.project.segment(id)?.text, text.words?.isEmpty == false {
            let highlight = model.binding(id, \.text!.highlight)
            SectionHeader(title: "Destaque da palavra")
            SwitchRow(title: "Destacar a palavra falada",
                      isOn: Binding(get: { highlight.wrappedValue != nil },
                                    set: { highlight.wrappedValue = $0 ? (highlight.wrappedValue ?? WordHighlight()) : nil }))
            if let current = highlight.wrappedValue {
                if text.activeHighlight == nil {
                    Text("O texto foi editado e as palavras não batem mais com a fala: o destaque fica desligado até o número de palavras voltar a ser o mesmo.")
                        .font(Theme.font(11))
                        .foregroundStyle(Color(red: 1, green: 0.62, blue: 0.35))
                        .fixedSize(horizontal: false, vertical: true)
                }
                FieldRow(label: "Modo") {
                    PillPicker(selection: highlightValue(\.mode), options: HighlightMode.allCases,
                               title: { $0 == .current ? "Só a atual" : "Progressivo" })
                }
                FieldRow(label: "Revelação") {
                    MenuField(selection: highlightValue(\.reveal), options: WordReveal.allCases,
                              title: \.displayName)
                }
                if current.reveal == .dim {
                    ParamSlider(title: "Opacidade das próximas", value: highlightValue(\.dimOpacity), range: 0...1,
                                defaultValue: WordHighlight().dimOpacity, format: percent)
                }
                ColorRow(title: "Cor", color: color(highlightValue(\.color)))
                ParamSlider(title: "Escala", value: highlightValue(\.scale), range: 1...1.6,
                            defaultValue: WordHighlight().scale, format: percent)
                SwitchRow(title: "Pílula atrás da palavra", isOn: highlightValue(\.backgroundEnabled))
                if current.backgroundEnabled {
                    ColorRow(title: "Cor da pílula", color: color(highlightValue(\.backgroundColor)))
                }
            }
        }
    }

    /// Só para textos com palavra-chave (visuais Editorial e Empilhado).
    @ViewBuilder private var emphasisSection: some View {
        if let text = model.project.segment(id)?.text, let emphasis = text.emphasis {
            let words = TextContent.words(of: text.text)
            SectionHeader(title: "Palavra-chave")
            Text("Clique numa palavra para ligar ou desligar a ênfase.")
                .font(Theme.font(11))
                .foregroundStyle(Theme.textTertiary)
            WordChips(words: words.map(String.init), selected: Set(emphasis.words)) { index in
                var e = emphasis
                if let i = e.words.firstIndex(of: index) { e.words.remove(at: i) } else { e.words = (e.words + [index]).sorted() }
                model.mutate { TimelineEditor.update(&$0, segment: id) { $0.text?.emphasis = e } }
            }
            ColorRow(title: "Cor", color: color(Binding(
                get: { emphasisValue(\.color).wrappedValue ?? style.wrappedValue.color },
                set: { emphasisValue(\.color).wrappedValue = $0 })))
            FieldRow(label: "Fonte") {
                FontFamilyMenu(current: emphasis.fontName, suggested: Self.suggested, installed: Self.installed) { family in
                    var e = emphasis
                    e.fontName = family
                    if !Self.faces(of: family).contains(e.fontFace) { e.fontFace = "" }
                    model.mutate { TimelineEditor.update(&$0, segment: id) { $0.text?.emphasis = e } }
                }
            }
            FieldRow(label: "Estilo") {
                MenuField(selection: emphasisValue(\.fontFace), options: [""] + Self.faces(of: emphasis.fontName),
                          title: { $0.isEmpty ? "Padrão" : $0 })
            }
            ParamSlider(title: "Tamanho", value: emphasisValue(\.scale), range: 0.6...2.5,
                        defaultValue: WordEmphasis().scale, format: percent)
        }
    }

    private func emphasisValue<T: Equatable>(_ keyPath: WritableKeyPath<WordEmphasis, T>) -> Binding<T> {
        let emphasis = model.binding(id, \.text!.emphasis)
        return Binding(
            get: { (emphasis.wrappedValue ?? WordEmphasis())[keyPath: keyPath] },
            set: { value in
                var e = emphasis.wrappedValue ?? WordEmphasis()
                e[keyPath: keyPath] = value
                emphasis.wrappedValue = e
            })
    }

    private func highlightValue<T: Equatable>(_ keyPath: WritableKeyPath<WordHighlight, T>) -> Binding<T> {
        let highlight = model.binding(id, \.text!.highlight)
        return Binding(
            get: { (highlight.wrappedValue ?? WordHighlight())[keyPath: keyPath] },
            set: { value in
                var h = highlight.wrappedValue ?? WordHighlight()
                h[keyPath: keyPath] = value
                highlight.wrappedValue = h
            })
    }

    @ViewBuilder private var backgroundSection: some View {
        let d = TextStyle()
        SectionHeader(title: "Fundo")
        SwitchRow(title: "Mostrar fundo", isOn: value(\.backgroundEnabled))
        if style.wrappedValue.backgroundEnabled {
            ColorRow(title: "Cor", color: color(value(\.backgroundColor)))
            ParamSlider(title: "Opacidade", value: value(\.backgroundColor.a), range: 0...1,
                        defaultValue: d.backgroundColor.a, format: percent)
            ParamSlider(title: "Arredondamento", value: value(\.backgroundRadius), range: 0...1,
                        defaultValue: d.backgroundRadius, format: Self.em)
            ParamSlider(title: "Margem lateral", value: value(\.backgroundPaddingX), range: 0...1,
                        defaultValue: d.backgroundPaddingX, format: Self.em)
            ParamSlider(title: "Margem vertical", value: value(\.backgroundPaddingY), range: 0...1,
                        defaultValue: d.backgroundPaddingY, format: Self.em)
        }
    }

    @ViewBuilder private var shadowSection: some View {
        let d = TextStyle()
        SectionHeader(title: "Sombra")
        SwitchRow(title: "Mostrar sombra", isOn: value(\.shadow))
        if style.wrappedValue.shadow {
            ColorRow(title: "Cor", color: color(value(\.shadowColor)))
            ParamSlider(title: "Opacidade", value: value(\.shadowColor.a), range: 0...1,
                        defaultValue: d.shadowColor.a, format: percent)
            ParamSlider(title: "Distância", value: value(\.shadowDistance), range: 0...1,
                        defaultValue: d.shadowDistance, format: Self.em)
            ParamSlider(title: "Ângulo", value: value(\.shadowAngle), range: -180...180,
                        defaultValue: d.shadowAngle, format: { String(format: "%.0f°", $0) })
            ParamSlider(title: "Desfoque", value: value(\.shadowBlur), range: 0...1,
                        defaultValue: d.shadowBlur, format: Self.em)
        }
    }

    @ViewBuilder private var positionSection: some View {
        SectionHeader(title: "Posição") {
            model.mutate {
                TimelineEditor.update(&$0, segment: id) {
                    $0.text?.style.x = 0
                    $0.text?.style.y = -0.7
                    $0.text?.style.scale = 1
                    $0.text?.style.rotation = 0
                }
            }
        }
        ParamSlider(title: "Posição X", value: value(\.x), format: signedPercent)
        ParamSlider(title: "Posição Y", value: value(\.y), defaultValue: -0.7, format: signedPercent)
        ParamSlider(title: "Escala", value: value(\.scale), range: 0.2...3, defaultValue: 1, format: percent)
        ParamSlider(title: "Rotação", value: value(\.rotation), range: -180...180,
                    format: { String(format: "%.0f°", $0) })
        BoxWidthControl(value: boxWidth, wholeTrack: model.isInAutoCaptions(id))
    }

    /// Largura da caixa pela mesma função das alças do preview: numa legenda automática, vale para a faixa.
    private var boxWidth: Binding<Double> {
        Binding(get: { style.wrappedValue.maxWidth },
                set: { w in model.mutate(coalesce: "\(id)boxWidth") { TimelineEditor.setTextWidth(&$0, segment: id, maxWidth: w) } })
    }
}

/// Menu de famílias: sugeridas primeiro, depois todas as instaladas.
private struct FontFamilyMenu: View {
    let current: String
    let suggested: [String]
    let installed: [String]
    let pick: (String) -> Void

    var body: some View {
        Menu {
            Section("Sugeridas") {
                ForEach(suggested.filter(installed.contains), id: \.self) { family in
                    Toggle(family, isOn: Binding(get: { family == current }, set: { _ in pick(family) }))
                }
            }
            Section("Todas") {
                ForEach(installed, id: \.self) { family in
                    Toggle(family, isOn: Binding(get: { family == current }, set: { _ in pick(family) }))
                }
            }
        } label: {
            HStack(spacing: 6) {
                Text(current)
                    .font(Theme.font(12))
                    .foregroundStyle(Theme.textPrimary)
                    .lineLimit(1)
                Spacer(minLength: 4)
                Image(systemName: "chevron.down")
                    .font(Theme.font(9, .medium))
                    .foregroundStyle(Theme.textTertiary)
            }
            .padding(.horizontal, 10)
            .frame(height: 28)
            .frame(maxWidth: .infinity)
            .field()
            .contentShape(Rectangle())
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
    }
}

/// Palavras da legenda como fichas que ligam e desligam a ênfase; quebram linha como texto.
private struct WordChips: View {
    let words: [String]
    let selected: Set<Int>
    let toggle: (Int) -> Void

    var body: some View {
        ChipFlow(spacing: 5) {
            ForEach(Array(words.enumerated()), id: \.offset) { index, word in
                let on = selected.contains(index)
                Button { toggle(index) } label: {
                    Text(word)
                        .font(Theme.font(12, on ? .medium : .regular))
                        .foregroundStyle(on ? Theme.onAccent : Theme.textPrimary)
                        .padding(.horizontal, 8)
                        .frame(height: 24)
                        .background(on ? Theme.accent : Theme.raised, in: Capsule())
                        .overlay(Capsule().strokeBorder(on ? .clear : Theme.hairline))
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
    }
}

/// Fileiras que quebram como texto.
private struct ChipFlow: Layout {
    var spacing: CGFloat

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = arrange(width: proposal.width ?? .infinity, subviews: subviews)
        let width = rows.map { $0.width }.max() ?? 0
        let height = rows.map(\.height).reduce(0, +) + spacing * CGFloat(max(rows.count - 1, 0))
        return CGSize(width: proposal.width ?? width, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in arrange(width: bounds.width, subviews: subviews) {
            var x = bounds.minX
            for i in row.items {
                let size = subviews[i].sizeThatFits(.unspecified)
                subviews[i].place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += row.height + spacing
        }
    }

    private struct Row { var items: [Int] = []; var width: CGFloat = 0; var height: CGFloat = 0 }

    private func arrange(width: CGFloat, subviews: Subviews) -> [Row] {
        var rows: [Row] = [Row()]
        for i in subviews.indices {
            let size = subviews[i].sizeThatFits(.unspecified)
            let extra = rows[rows.count - 1].items.isEmpty ? size.width : size.width + spacing
            if rows[rows.count - 1].width + extra > width, !rows[rows.count - 1].items.isEmpty {
                rows.append(Row())
            }
            let last = rows.count - 1
            rows[last].width += rows[last].items.isEmpty ? size.width : size.width + spacing
            rows[last].height = max(rows[last].height, size.height)
            rows[last].items.append(i)
        }
        return rows
    }
}

/// "Largura da caixa": slider e campo em % da largura do quadro. Passa de 100% para caber numa linha só.
private struct BoxWidthControl: View {
    @Binding var value: Double
    let wholeTrack: Bool
    @State private var typed = 0
    @FocusState private var focused: Bool

    private static let defaultValue = TextStyle().maxWidth

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 4) {
                Text(wholeTrack ? "Largura da caixa · todas as legendas" : "Largura da caixa")
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { value = Self.defaultValue }
                    .help(wholeTrack ? "Vale para todas as legendas automáticas da faixa. Clique duas vezes para redefinir."
                                     : "Clique duas vezes para redefinir")
                Spacer(minLength: 6)
                TextField("", value: $typed, format: .number.grouping(.never))
                    .textFieldStyle(.plain)
                    .multilineTextAlignment(.trailing)
                    .monospacedDigit()
                    .frame(width: 34)
                    .focused($focused)
                    .onSubmit(commit)
                    .onChange(of: focused) { if !focused { commit() } }
                Text("%").foregroundStyle(Theme.textTertiary)
            }
            .font(Theme.font(12))
            ThinSlider(value: $value, range: TextStyle.maxWidthRange)
        }
        .onAppear { typed = Self.percent(value) }
        .onChange(of: value) { if !focused { typed = Self.percent(value) } }
    }

    private static func percent(_ w: Double) -> Int { Int((w * 100).rounded()) }

    private func commit() {
        let range = TextStyle.maxWidthRange
        let w = min(max(Double(typed) / 100, range.lowerBound), range.upperBound)
        if Self.percent(w) != Self.percent(value) { value = w }
        typed = Self.percent(w)
    }
}

/// Liga/desliga em forma de botão redondo; ligado fica em ciano.
private struct IconToggleStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        Button { configuration.isOn.toggle() } label: { configuration.label }
            .buttonStyle(CircleButtonStyle(size: 28, prominent: configuration.isOn))
    }
}

// MARK: - Projeto

private struct ProjectInspector: View {
    @Environment(EditorModel.self) private var model

    private enum BackgroundKind: String, CaseIterable, Identifiable {
        case black, color, blur
        var id: String { rawValue }
        var title: String {
            switch self {
            case .black: "Preto"
            case .color: "Cor"
            case .blur: "Desfoque"
            }
        }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                PanelHeader(title: "Projeto")
                FieldRow(label: "Nome") {
                    InputField(placeholder: "Nome", text: model.projectBinding(\.name))
                }
                FieldRow(label: "Duração") {
                    Text(Time.format(model.duration)).font(Theme.mono(12))
                }
                FieldRow(label: "Clipes") {
                    Text("\(model.project.tracks.flatMap(\.segments).count)").font(Theme.mono(12))
                }

                SectionHeader(title: "Canvas")
                FieldRow(label: "Proporção") {
                    MenuField(selection: model.projectBinding(\.canvas.aspect), options: AspectRatio.allCases, title: \.label)
                }
                // Rótulo em cima: ao lado dele, "Desfoque" não cabia na pílula.
                VStack(alignment: .leading, spacing: 6) {
                    Text("Fundo")
                        .font(Theme.font(12))
                        .foregroundStyle(Theme.textSecondary)
                    PillPicker(selection: backgroundKind, options: BackgroundKind.allCases, title: \.title)
                }
                switch model.project.canvas.background {
                case .color(let c):
                    ColorRow(title: "Cor do fundo", color: Binding(
                        get: { c.color },
                        set: { new in model.mutate(coalesce: "bgcolor") { $0.canvas.background = .color(RGBA(new)) } }))
                case .blur(let amount):
                    ParamSlider(title: "Intensidade do desfoque", value: Binding(
                        get: { amount },
                        set: { v in model.mutate(coalesce: "bgblur") { $0.canvas.background = .blur(v) } }),
                                range: 0...1, defaultValue: 0.5, format: percent)
                case .black:
                    EmptyView()
                }
                Text("O fundo aparece onde o vídeo não cobre o canvas, por exemplo ao converter 16:9 para 9:16.")
                    .font(Theme.font(11))
                    .foregroundStyle(Theme.textTertiary)

                SectionHeader(title: "Dica")
                Text("Selecione um clipe na timeline para ajustar transformação, cor, filtros, efeitos, animações e transições.")
                    .font(Theme.font(11))
                    .foregroundStyle(Theme.textTertiary)
            }
            .padding(14)
        }
        .scrollIndicators(.never)
    }

    private var backgroundKind: Binding<BackgroundKind> {
        Binding(
            get: {
                switch model.project.canvas.background {
                case .black: .black
                case .color: .color
                case .blur: .blur
                }
            },
            set: { kind in
                model.mutate {
                    switch kind {
                    case .black: $0.canvas.background = .black
                    case .color: $0.canvas.background = .color(RGBA(0.1, 0.1, 0.12))
                    case .blur: $0.canvas.background = .blur(0.5)
                    }
                }
            })
    }
}
