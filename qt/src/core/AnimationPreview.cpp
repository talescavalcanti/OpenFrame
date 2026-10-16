#include "AnimationPreview.h"

#include "Renderer.h"
#include "TextRenderer.h"
#include "TimelineEditor.h"

namespace of {

namespace {

/// Centraliza o texto e muda o tamanho da fonte até a caixa ocupar boa parte da prévia, mantendo as quebras.
std::optional<TextContent> fit(const TextContent &content, QSize canvas)
{
    TextContent c = content;
    c.style.x = 0;
    c.style.y = 0;
    c.style.scale = 1;
    c.style.rotation = 0;
    const auto box = TextRenderer::shared().boxSize(c, canvas);
    if (!box || box->width() <= 0 || box->height() <= 0)
        return std::nullopt;
    const double factor = std::min(canvas.width() * 0.8 / box->width(), canvas.height() * 0.6 / box->height());
    c.style.fontSize *= factor;
    c.style.maxWidth *= factor;
    return c;
}

} // namespace

QList<QImage> AnimationPreview::frames(const Subject &subject, const Part &part, QSize size, int count)
{
    const QSize pixels(std::max(2, size.width()), std::max(2, size.height()));
    if (count <= 0)
        return {};

    // Projeto de um elemento só, centralizado, num canvas transparente do tamanho da prévia.
    Project project;
    project.canvas.background = CanvasBackground::color(RGBA{0, 0, 0, 0});
    RenderOptions options;
    options.transparent = true;
    QUuid id;
    if (const auto *text = std::get_if<TextContent>(&subject)) {
        const auto fitted = fit(*text, pixels);
        if (!fitted)
            return {};
        id = TimelineEditor::addText(*fitted, project, 0);
    } else {
        const QImage &image = std::get<QImage>(subject);
        if (image.isNull())
            return {};
        Material m;
        m.kind = MaterialKind::Photo;
        m.width = image.width();
        m.height = image.height();
        project.materials.append(m);
        options.stills.insert(m.id, image);
        id = TimelineEditor::addMedia(m, project);
    }

    Micros cycle = 0, duration = 0;
    switch (part.kind) {
    case Part::Kind::Entry:
        cycle = animationDuration + hold;
        duration = cycle * 10;
        TimelineEditor::update(project, id,
                               [&](Segment &s) { s.animationIn = SegmentAnimation{part.animation, animationDuration}; });
        break;
    case Part::Kind::Exit:
        cycle = animationDuration + hold;
        duration = cycle;
        TimelineEditor::update(project, id,
                               [&](Segment &s) { s.animationOut = SegmentAnimation{part.animation, animationDuration}; });
        break;
    case Part::Kind::Loop: {
        // Intensidade máxima: na miniatura, o efeito padrão mal aparece.
        LoopAnimation loop;
        loop.kind = part.loop;
        loop.intensity = 1;
        cycle = loop.period * 2;
        duration = cycle * 10;
        TimelineEditor::update(project, id, [&](Segment &s) { s.animationLoop = loop; });
        break;
    }
    }
    TimelineEditor::update(project, id, [&](Segment &s) { s.source.duration = duration; });

    QList<QImage> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const Micros t = Micros(std::llround(double(cycle) * i / count));
        const QImage image = Renderer::shared().render(project, std::min(t, duration - 1), pixels, options);
        if (!image.isNull())
            out.append(image);
    }
    return out;
}

} // namespace of
