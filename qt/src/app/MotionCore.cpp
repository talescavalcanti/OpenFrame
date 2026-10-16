#include "MotionCore.h"

#include "AnimationPreview.h"
#include "CoreBridge.h"
#include "ProjectStore.h"
#include "TimelineEditor.h"

#include <QHash>
#include <QJsonDocument>
#include <QVariantMap>

namespace of::MotionCore {

namespace {

/// Ícones (Lucide) enquanto os quadros da miniatura são gerados, como no Mac.
const QHash<QString, QString> &icons()
{
    static const QHash<QString, QString> map{
        {"fade", "contrast"},          {"zoomIn", "zoom-in"},         {"zoomOut", "zoom-out"},
        {"slideLeft", "arrow-left"},   {"slideRight", "arrow-right"}, {"slideUp", "arrow-up"},
        {"slideDown", "arrow-down"},   {"spin", "rotate-cw"},         {"typewriter", "text-cursor"},
        {"focus", "focus"},            {"cascade", "rows-3"},         {"elastic", "spline"},
        {"wave", "waves"},             {"wordByWord", "letter-text"}, {"maskReveal", "scan-text"},
        {"tracking", "move-horizontal"}, {"decode", "binary"},
        {"float", "arrow-up-down"},    {"pulse", "circle-dot"},       {"glow", "sun"},
        {"shine", "sparkle"},
    };
    return map;
}

/// Nomes curtos para caber numa linha do cartão (o nome completo fica na dica).
const QHash<QString, QString> &shortTitles()
{
    static const QHash<QString, QString> map{
        {"zoomIn", "Aproximar"},       {"zoomOut", "Afastar"},  {"typewriter", "Digitação"},
        {"wordByWord", "Por palavra"}, {"maskReveal", "Máscara"},
    };
    return map;
}

QVariantMap option(const QString &value, const QString &title)
{
    return {{"value", value},
            {"title", title},
            {"short", shortTitles().value(value, title)},
            {"icon", icons().value(value, "sparkles")}};
}

} // namespace

QVariantList animationKinds(bool text)
{
    QVariantList list;
    for (auto a : allCases<AnimationKind>())
        if (text || !isTextOnly(a))
            list.append(option(enumKey(a), displayName(a)));
    return list;
}

QVariantList loopKinds()
{
    QVariantList list;
    for (auto k : allCases<LoopKind>())
        list.append(option(enumKey(k), displayName(k)));
    return list;
}

std::optional<Loop> loop(const Project &p, const QUuid &id)
{
    const auto seg = p.segment(id);
    if (!seg || !seg->animationLoop)
        return std::nullopt;
    return Loop{enumKey(seg->animationLoop->kind), seg->animationLoop->period, seg->animationLoop->intensity};
}

void setLoop(Project &p, const QUuid &id, const std::optional<Loop> &value)
{
    TimelineEditor::update(p, id, [&](Segment &s) {
        const auto kind = value ? enumFromKey<LoopKind>(value->kind) : std::nullopt;
        if (!kind)
            s.animationLoop.reset();
        else
            s.animationLoop = LoopAnimation{*kind, value->period, value->intensity};
    });
}

QList<QImage> frames(const Segment &seg, const QImage &mediaImage, const QString &part, QSize size, int count)
{
    const QString kind = part.section(':', 0, 0);
    const QString value = part.section(':', 1);
    AnimationPreview::Part p;
    if (kind == QLatin1String("loop")) {
        const auto loop = enumFromKey<LoopKind>(value);
        if (!loop)
            return {};
        p = AnimationPreview::Part::loopOf(*loop);
    } else {
        const auto animation = enumFromKey<AnimationKind>(value);
        if (!animation || (kind != QLatin1String("entry") && kind != QLatin1String("exit")))
            return {};
        p = kind == QLatin1String("entry") ? AnimationPreview::Part::entry(*animation)
                                           : AnimationPreview::Part::exit(*animation);
    }
    AnimationPreview::Subject subject;
    if (seg.text) {
        TextContent content;
        content.text = shortText(seg.text->text);
        content.style = seg.text->style;
        subject = content;
    } else if (!mediaImage.isNull()) {
        subject = mediaImage;
    } else {
        return {};
    }
    return AnimationPreview::frames(subject, p, size, count);
}

QString subjectKey(const Segment &seg)
{
    if (seg.text) {
        TextContent content = *seg.text;
        content.text = shortText(content.text);
        Segment s = Segment::makeText(content, 0, 1'000'000);
        s.id = QUuid();
        return QString::fromUtf8(QJsonDocument(Bridge::segmentJson(s)).toJson(QJsonDocument::Compact));
    }
    return seg.materialId ? ProjectStore::uuidString(*seg.materialId) : QString();
}

QString shortText(const QString &text)
{
    const int limit = 14;
    const QString line = text.split('\n').value(0).trimmed();
    if (line.isEmpty())
        return QStringLiteral("Aa");
    if (line.size() <= limit)
        return line;
    QString out;
    for (const QString &word : line.split(' ', Qt::SkipEmptyParts)) {
        const QString next = out.isEmpty() ? word : out + ' ' + word;
        if (next.size() > limit && !out.isEmpty())
            break;
        out = next;
    }
    return out.size() > limit ? out.left(limit - 2) + QStringLiteral("…") : out;
}

} // namespace of::MotionCore
