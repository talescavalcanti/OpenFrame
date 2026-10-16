#include "CaptionPresets.h"

namespace of {

QString displayName(CaptionLayout l)
{
    switch (l) {
    case CaptionLayout::Phrases: return QStringLiteral("Frases");
    case CaptionLayout::Highlight: return QStringLiteral("Destaque");
    case CaptionLayout::Punch: return QStringLiteral("Impacto");
    case CaptionLayout::Stacked: return QStringLiteral("Empilhado");
    }
    return {};
}

QString displayName(CaptionPresetGroup g)
{
    switch (g) {
    case CaptionPresetGroup::Classic: return QStringLiteral("Clássicos");
    case CaptionPresetGroup::Reveal: return QStringLiteral("Revelação");
    case CaptionPresetGroup::Editorial: return QStringLiteral("Editorial");
    }
    return {};
}

QString displayName(CaptionPreset p)
{
    switch (p) {
    case CaptionPreset::Phrases: return QStringLiteral("Frases");
    case CaptionPreset::Highlight: return QStringLiteral("Destaque");
    case CaptionPreset::Punch: return QStringLiteral("Impacto");
    case CaptionPreset::Rise: return QStringLiteral("Surgir");
    case CaptionPreset::Guided: return QStringLiteral("Leitura guiada");
    case CaptionPreset::Fill: return QStringLiteral("Preenchimento");
    case CaptionPreset::Typing: return QStringLiteral("Digitação");
    case CaptionPreset::Editorial: return QStringLiteral("Editorial");
    case CaptionPreset::Cinema: return QStringLiteral("Cinema");
    case CaptionPreset::Stacked: return QStringLiteral("Empilhado");
    }
    return {};
}

QString detail(CaptionPreset p)
{
    switch (p) {
    case CaptionPreset::Phrases: return QStringLiteral("Até duas linhas embaixo, como no cinema.");
    case CaptionPreset::Highlight: return QStringLiteral("Uma linha; a palavra falada acende.");
    case CaptionPreset::Punch: return QStringLiteral("Poucas palavras grandes no centro.");
    case CaptionPreset::Rise: return QStringLiteral("Cada palavra surge quando é dita.");
    case CaptionPreset::Guided: return QStringLiteral("A frase inteira, acendendo palavra por palavra.");
    case CaptionPreset::Fill: return QStringLiteral("A cor enche cada palavra no ritmo da fala.");
    case CaptionPreset::Typing: return QStringLiteral("As letras são digitadas no tempo da fala.");
    case CaptionPreset::Editorial: return QStringLiteral("Texto leve, com a palavra-chave em serifa itálica.");
    case CaptionPreset::Cinema: return QStringLiteral("Discreta: fina, espaçada e pequena, embaixo.");
    case CaptionPreset::Stacked: return QStringLiteral("Blocos em caixa alta, justificados, linha a linha.");
    }
    return {};
}

CaptionPresetGroup group(CaptionPreset p)
{
    switch (p) {
    case CaptionPreset::Phrases:
    case CaptionPreset::Highlight:
    case CaptionPreset::Punch: return CaptionPresetGroup::Classic;
    case CaptionPreset::Rise:
    case CaptionPreset::Guided:
    case CaptionPreset::Fill:
    case CaptionPreset::Typing: return CaptionPresetGroup::Reveal;
    case CaptionPreset::Editorial:
    case CaptionPreset::Cinema:
    case CaptionPreset::Stacked: return CaptionPresetGroup::Editorial;
    }
    return CaptionPresetGroup::Classic;
}

CaptionLayout layout(CaptionPreset p)
{
    switch (p) {
    case CaptionPreset::Phrases:
    case CaptionPreset::Guided:
    case CaptionPreset::Editorial: return CaptionLayout::Phrases;
    case CaptionPreset::Highlight:
    case CaptionPreset::Rise:
    case CaptionPreset::Fill:
    case CaptionPreset::Typing:
    case CaptionPreset::Cinema: return CaptionLayout::Highlight;
    case CaptionPreset::Punch: return CaptionLayout::Punch;
    case CaptionPreset::Stacked: return CaptionLayout::Stacked;
    }
    return CaptionLayout::Phrases;
}

bool usesAccent(CaptionPreset p)
{
    return p != CaptionPreset::Phrases && p != CaptionPreset::Punch && p != CaptionPreset::Cinema;
}

CaptionPreset presetFor(CaptionLayout l)
{
    switch (l) {
    case CaptionLayout::Phrases: return CaptionPreset::Phrases;
    case CaptionLayout::Highlight: return CaptionPreset::Highlight;
    case CaptionLayout::Punch: return CaptionPreset::Punch;
    case CaptionLayout::Stacked: return CaptionPreset::Stacked;
    }
    return CaptionPreset::Phrases;
}

} // namespace of
