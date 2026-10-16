#include "Effects.h"

namespace of {

QString displayName(FilterPreset p)
{
    switch (p) {
    case FilterPreset::None: return QStringLiteral("Nenhum");
    case FilterPreset::Vivid: return QStringLiteral("Vívido");
    case FilterPreset::Warm: return QStringLiteral("Quente");
    case FilterPreset::Cool: return QStringLiteral("Frio");
    case FilterPreset::Cinematic: return QStringLiteral("Cinema");
    case FilterPreset::Vintage: return QStringLiteral("Vintage");
    case FilterPreset::Mono: return QStringLiteral("Mono");
    case FilterPreset::Noir: return QStringLiteral("Noir");
    case FilterPreset::Chrome: return QStringLiteral("Cromo");
    case FilterPreset::Fade: return QStringLiteral("Desbotado");
    case FilterPreset::Instant: return QStringLiteral("Instantânea");
    case FilterPreset::Process: return QStringLiteral("Processo");
    case FilterPreset::Tonal: return QStringLiteral("Tonal");
    case FilterPreset::Transfer: return QStringLiteral("Transferência");
    case FilterPreset::Sepia: return QStringLiteral("Sépia");
    }
    return {};
}

QString displayName(VideoEffectKind k)
{
    switch (k) {
    case VideoEffectKind::Blur: return QStringLiteral("Desfoque");
    case VideoEffectKind::Pixelate: return QStringLiteral("Pixelizar");
    case VideoEffectKind::RgbSplit: return QStringLiteral("Glitch RGB");
    case VideoEffectKind::Mirror: return QStringLiteral("Espelho");
    case VideoEffectKind::Shake: return QStringLiteral("Tremor");
    case VideoEffectKind::Flash: return QStringLiteral("Flash");
    case VideoEffectKind::ZoomPulse: return QStringLiteral("Pulso de zoom");
    case VideoEffectKind::Kaleidoscope: return QStringLiteral("Caleidoscópio");
    case VideoEffectKind::Comic: return QStringLiteral("Quadrinhos");
    case VideoEffectKind::Edges: return QStringLiteral("Contornos neon");
    }
    return {};
}

QString displayName(AnimationKind k)
{
    switch (k) {
    case AnimationKind::Fade: return QStringLiteral("Esmaecer");
    case AnimationKind::ZoomIn: return QStringLiteral("Zoom de aproximação");
    case AnimationKind::ZoomOut: return QStringLiteral("Zoom de afastamento");
    case AnimationKind::SlideLeft: return QStringLiteral("Deslizar ←");
    case AnimationKind::SlideRight: return QStringLiteral("Deslizar →");
    case AnimationKind::SlideUp: return QStringLiteral("Deslizar ↑");
    case AnimationKind::SlideDown: return QStringLiteral("Deslizar ↓");
    case AnimationKind::Spin: return QStringLiteral("Girar");
    case AnimationKind::Typewriter: return QStringLiteral("Máquina de escrever");
    case AnimationKind::Focus: return QStringLiteral("Foco");
    case AnimationKind::Cascade: return QStringLiteral("Cascata");
    case AnimationKind::Elastic: return QStringLiteral("Elástico");
    case AnimationKind::Wave: return QStringLiteral("Onda");
    case AnimationKind::WordByWord: return QStringLiteral("Palavra a palavra");
    case AnimationKind::MaskReveal: return QStringLiteral("Revelar por máscara");
    case AnimationKind::Tracking: return QStringLiteral("Espaçamento");
    case AnimationKind::Decode: return QStringLiteral("Decodificar");
    }
    return {};
}

bool isTextOnly(AnimationKind k)
{
    switch (k) {
    case AnimationKind::Typewriter:
    case AnimationKind::Cascade:
    case AnimationKind::Elastic:
    case AnimationKind::Wave:
    case AnimationKind::WordByWord:
    case AnimationKind::MaskReveal:
    case AnimationKind::Tracking:
    case AnimationKind::Decode: return true;
    default: return false;
    }
}

QString displayName(LoopKind k)
{
    switch (k) {
    case LoopKind::Float: return QStringLiteral("Flutuar");
    case LoopKind::Pulse: return QStringLiteral("Pulsar");
    case LoopKind::Glow: return QStringLiteral("Respirar brilho");
    case LoopKind::Shine: return QStringLiteral("Reflexo de luz");
    }
    return {};
}

QString displayName(WordReveal r)
{
    switch (r) {
    case WordReveal::None: return QStringLiteral("Nenhuma");
    case WordReveal::Rise: return QStringLiteral("Surgir");
    case WordReveal::Dim: return QStringLiteral("Leitura guiada");
    case WordReveal::Fill: return QStringLiteral("Preenchimento");
    case WordReveal::Type: return QStringLiteral("Digitação");
    }
    return {};
}

QStringList TextContent::wordsOf(const QString &text)
{
    // Espaço = o `isWhitespace` do Swift: inclui o espaço sem quebra (U+00A0) e as quebras de linha.
    QStringList words;
    qsizetype start = -1;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const bool blank = i == text.size() || text[i].isSpace();
        if (blank && start >= 0) {
            words.append(text.mid(start, i - start));
            start = -1;
        } else if (!blank && start < 0) {
            start = i;
        }
    }
    return words;
}

std::optional<TextContent::ActiveHighlight> TextContent::activeHighlight() const
{
    if (!words || !highlight || words->isEmpty() || wordsOf(text).size() != words->size())
        return std::nullopt;
    return ActiveHighlight{*words, *highlight};
}

QString displayName(TransitionKind k)
{
    switch (k) {
    case TransitionKind::Dissolve: return QStringLiteral("Dissolver");
    case TransitionKind::FadeBlack: return QStringLiteral("Escurecer");
    case TransitionKind::FadeWhite: return QStringLiteral("Clarear");
    case TransitionKind::WipeLeft: return QStringLiteral("Cortina ←");
    case TransitionKind::WipeRight: return QStringLiteral("Cortina →");
    case TransitionKind::SlideLeft: return QStringLiteral("Empurrar ←");
    case TransitionKind::SlideRight: return QStringLiteral("Empurrar →");
    case TransitionKind::Zoom: return QStringLiteral("Zoom");
    case TransitionKind::Blur: return QStringLiteral("Desfoque");
    }
    return {};
}

} // namespace of
