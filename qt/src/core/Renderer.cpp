#include "Renderer.h"

#include "ImageEffects.h"
#include "LinearImage.h"
#include "MediaProbe.h"
#include "Motion.h"
#include "TextMotion.h"
#include "TextRenderer.h"

#include <QBuffer>
#include <cmath>

namespace of {

namespace {

/// Uma camada pronta para desenhar: imagem de trabalho, posição e opacidade.
struct Layer {
    LinearImage image;
    QTransform transform;
    float opacity = 1;
};

/// Copia a imagem no meio de uma maior, com `pad` pixels transparentes de cada lado.
LinearImage padded(const LinearImage &image, int pad)
{
    if (pad <= 0)
        return image;
    LinearImage out(image.width() + 2 * pad, image.height() + 2 * pad);
    out.drawAt(image, pad, pad);
    return out;
}

/// Instante do arquivo exibido no instante `t` da timeline.
Micros sourceTime(const Segment &seg, Micros t)
{
    const Micros offset = Time::round(double(t - seg.targetStart) * seg.speed);
    return std::clamp(seg.source.start + offset, seg.source.start,
                      seg.source.start + std::max<Micros>(0, seg.source.duration - 1));
}

} // namespace

struct Renderer::Impl {
    DecoderCache decoders{24};
    QMutex photoLock;
    /// Fotos decodificadas (até 4K, orientadas) e versões de trabalho por tamanho.
    QCache<QString, QImage> photos{64};
    QCache<QString, LinearImage> scaledPhotos{256};

    QImage photo(const Material &m)
    {
        {
            QMutexLocker locker(&photoLock);
            if (const QImage *cached = photos.object(m.path))
                return *cached;
        }
        // Fotos são decodificadas uma vez (reduzidas a no máximo 4K) e mantidas em cache.
        const QImage image = Images::load(m.path, 3840);
        QMutexLocker locker(&photoLock);
        photos.insert(m.path, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() >> 20));
        return image;
    }

    /// Imagem do material no instante `t`, em luz linear, com no máximo `work`
    /// pixels (o tamanho com que vai aparecer). `native` recebe o tamanho cheio.
    LinearImage source(const Project &p, const Segment &seg, Micros t, double drawScale, QSize &native,
                       const RenderOptions &options)
    {
        const Material *m = p.material(seg.materialId);
        if (!m)
            return {};
        native = QSize(m->width, m->height);
        const auto workSize = [&](QSize full) {
            const double s = std::min(1.0, drawScale);
            return QSize(std::max(1, int(std::ceil(full.width() * s))), std::max(1, int(std::ceil(full.height() * s))));
        };
        if (m->kind == MaterialKind::Video) {
            const QSize want = native.isEmpty() ? QSize() : workSize(native);
            const QImage frame = decoders.frame(seg.id, m->path, sourceTime(seg, t), want);
            if (frame.isNull())
                return {};
            if (native.isEmpty())
                native = frame.size();
            return LinearImage::fromImage(frame);
        }
        if (m->kind == MaterialKind::Photo) {
            const auto still = options.stills.constFind(m->id);
            const bool inMemory = still != options.stills.cend();
            const QImage full = inMemory ? *still : photo(*m);
            if (full.isNull())
                return {};
            native = full.size();
            const QSize want = workSize(native);
            // Fotos em memória não entram no cache (não têm caminho).
            if (inMemory)
                return LinearImage::fromImage(
                    want == native ? full : full.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            const QString key = QStringLiteral("%1|%2x%3").arg(m->path).arg(want.width()).arg(want.height());
            {
                QMutexLocker locker(&photoLock);
                if (const LinearImage *cached = scaledPhotos.object(key))
                    return *cached;
            }
            const QImage scaled =
                want == native ? full : full.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            LinearImage image = LinearImage::fromImage(scaled);
            QMutexLocker locker(&photoLock);
            scaledPhotos.insert(key, new LinearImage(image),
                                std::max<qsizetype>(1, qsizetype(image.width()) * image.height() * 16 >> 20));
            return image;
        }
        return {};
    }

    std::optional<Layer> layer(const Project &p, const Segment &seg, Micros t, QSize canvas, double k,
                               const RenderOptions &options)
    {
        const Micros local = t - seg.targetStart;
        const double seconds = Time::toSeconds(local);
        const AnimationState anim = Animations::state(seg, local);
        const ImageEffects::Geometry geo = ImageEffects::geometry(seg.effects, seconds);
        const Material *m = p.material(seg.materialId);
        if (!m || m->kind == MaterialKind::Audio)
            return std::nullopt;
        const double W = canvas.width(), H = canvas.height();
        const double relative = seg.transform.scale * anim.scale * geo.scale;
        const double guessFit = m->width > 0 && m->height > 0 ? std::min(W / m->width, H / m->height) : 1;
        QSize native;
        LinearImage img = source(p, seg, t, guessFit * relative, native, options);
        if (img.isNull() || native.isEmpty())
            return std::nullopt;
        // A imagem de trabalho pode ser menor que a original: os raios dos efeitos acompanham.
        const double pre = double(img.width()) / native.width();
        ImageEffects::adjust(img, seg.adjust, k * pre, seconds);
        if (seg.filter)
            ImageEffects::filter(img, *seg.filter);
        ImageEffects::effects(img, seg.effects, k * pre, seconds);

        const double fit = std::min(W / native.width(), H / native.height());
        const double scale = fit * relative / pre;
        const double cx = W / 2 + (seg.transform.x + anim.dx + geo.dx) * W / 2;
        const double cy = H / 2 - (seg.transform.y + anim.dy + geo.dy) * H / 2;
        // Reflexo (na imagem, antes de posicionar), foco e brilho (em pixels do canvas, aqui divididos pela
        // escala): com folga transparente para o que passa da borda, sem mudar o centro.
        const auto shine = anim.shine();
        const double focus = scale > 0 ? anim.blur * H / scale : 0;
        const double glow = anim.glow() > 0 && scale > 0 ? 0.012 * H / scale : 0;
        const double spread = shine ? 0.06 * std::min(img.width(), img.height()) + 2 : 0;
        const int pad = focus > 0.3 || glow > 0 || shine ? int(std::ceil(3 * std::max({focus, glow, spread}))) + 1 : 0;
        const double w = img.width(), h = img.height();
        if (pad > 0) {
            img = padded(img, pad);
            if (shine)
                TextMotion::applyShine(img, shine->position, shine->strength, true, QRectF(pad, pad, w, h));
            if (focus > 0.3)
                img.blur(focus);
            if (glow > 0)
                TextMotion::applyGlow(img, anim.glow(), glow);
        }
        Layer out;
        out.transform.translate(cx, cy);
        out.transform.rotate(seg.transform.rotation + anim.rotation);
        out.transform.scale(scale, scale);
        out.transform.translate(-w / 2.0 - pad, -h / 2.0 - pad);
        out.opacity = float(std::clamp(seg.opacity * anim.alpha, 0.0, 1.0));
        out.image = std::move(img);
        return out;
    }

    std::optional<Layer> textLayer(const Segment &seg, Micros t, QSize canvas)
    {
        if (!seg.text)
            return std::nullopt;
        const TextContent &content = *seg.text;
        const AnimationState anim = Animations::state(seg, t - seg.targetStart);
        // O texto é rasterizado já na escala efetiva (canvas × r: fonte, margens, sombra e largura máxima
        // crescem juntas, o layout é o mesmo) e só reduzido depois: ampliar uma imagem pequena serrilha.
        const double r = TextRenderer::shared().rasterScale(content, canvas, Animations::peakScale(seg));
        const QSize raster(std::max(1, int(std::lround(canvas.width() * r))), std::max(1, int(std::lround(canvas.height() * r))));
        LinearImage image;
        if (Animations::usesUnits(seg)) {
            // Por unidade no clipe inteiro.
            auto out = TextMotion::image(content, raster, anim, Animations::granularityFor(seg, anim));
            if (!out)
                return std::nullopt;
            image = std::move(out->image);
        } else {
            const QImage still = TextRenderer::shared().image(content, raster, anim.reveal);
            if (still.isNull())
                return std::nullopt;
            image = LinearImage::fromImage(still);
        }
        const TextStyle &style = content.style;
        const double W = canvas.width(), H = canvas.height();
        const double scale = style.scale * anim.scale / r;
        // Desfoque do foco em pixels do canvas, aqui na imagem rasterizada.
        const double focus = scale > 0 ? anim.blur * H / scale : 0;
        int pad = 0;
        if (focus > 0.3) {
            pad = int(std::ceil(3 * focus)) + 1;
            image = padded(image, pad);
            image.blur(focus);
        }
        Layer out;
        out.transform.translate(W / 2 + (style.x + anim.dx) * W / 2, H / 2 - (style.y + anim.dy) * H / 2);
        out.transform.rotate(style.rotation + anim.rotation);
        out.transform.scale(scale, scale);
        // A imagem tem o mesmo centro da nominal (margens iguais dos dois lados).
        out.transform.translate(-image.width() / 2.0, -image.height() / 2.0);
        out.opacity = float(std::clamp(seg.opacity * anim.alpha, 0.0, 1.0));
        out.image = std::move(image);
        return out;
    }

    void background(LinearImage &canvas, const Project &p, Micros t, double k, const RenderOptions &options)
    {
        const CanvasBackground &bg = p.canvas.background;
        if (options.transparent)
            return;
        if (const RGBA *c = bg.colorValue()) {
            const float a = float(std::clamp(c->a, 0.0, 1.0));
            // Cor sobre o preto, como o CIImage(color:) recortado sobre o fundo.
            canvas.fill({toLinear(float(c->r)) * a, toLinear(float(c->g)) * a, toLinear(float(c->b)) * a, 1});
            return;
        }
        const auto amount = bg.blurIntensity();
        const auto main = p.mainTrackIndex();
        if (!amount || !main)
            return;
        const Segment *seg = nullptr;
        for (const Segment &s : p.tracks[*main].segments)
            if (s.targetRange().contains(t)) {
                seg = &s;
                break;
            }
        if (!seg)
            return;
        const double sigma = (8 + *amount * 50) * k;
        // Desfoque grande: calculado numa versão reduzida e ampliado de volta.
        const double factor = std::clamp(sigma / 6, 1.0, 8.0);
        const QSize small(std::max(1, int(std::lround(canvas.width() / factor))),
                          std::max(1, int(std::lround(canvas.height() / factor))));
        const Material *m = p.material(seg->materialId);
        const double guess = m && m->width > 0 && m->height > 0
                                 ? std::max(double(small.width()) / m->width, double(small.height()) / m->height) * 1.1
                                 : 1;
        QSize native;
        const LinearImage img = source(p, *seg, t, guess, native, options);
        if (img.isNull())
            return;
        const double s = std::max(double(small.width()) / img.width(), double(small.height()) / img.height()) * 1.1;
        LinearImage filled(small.width(), small.height());
        QTransform transform;
        transform.translate(small.width() / 2.0, small.height() / 2.0);
        transform.scale(s, s);
        transform.translate(-img.width() / 2.0, -img.height() / 2.0);
        filled.draw(img, transform);
        filled.blur(sigma / factor);
        ImageEffects::brightness(filled, -0.08);
        LinearImage full = filled.scaled(canvas.size());
        // Sobre o preto.
        for (int y = 0; y < full.height(); ++y) {
            Pixel *row = full.row(y);
            for (int x = 0; x < full.width(); ++x)
                row[x].a = 1, row[x].r = std::max(0.0f, row[x].r), row[x].g = std::max(0.0f, row[x].g),
                row[x].b = std::max(0.0f, row[x].b);
        }
        canvas = std::move(full);
    }
};

Renderer::Renderer() : d(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

Renderer &Renderer::shared()
{
    static Renderer renderer;
    return renderer;
}

void Renderer::clearCaches()
{
    d->decoders.clear();
    QMutexLocker locker(&d->photoLock);
    d->photos.clear();
    d->scaledPhotos.clear();
}

QImage Renderer::render(const Project &project, Micros t, QSize size)
{
    return render(project, t, size, RenderOptions{});
}

QImage Renderer::render(const Project &project, Micros t, QSize size, const RenderOptions &options)
{
    if (size.isEmpty())
        return {};
    const double k = std::max(size.height(), 1) / 1080.0;
    LinearImage canvas(size.width(), size.height(), Pixel{0, 0, 0, options.transparent ? 0.0f : 1.0f});
    d->background(canvas, project, t, k, options);

    for (const Track &track : project.tracks) {
        if (track.kind != TrackKind::Video || track.hidden)
            continue;
        QList<const Segment *> active;
        for (const Segment &s : track.segments)
            if (s.targetRange().contains(t))
                active.append(&s);
        std::stable_sort(active.begin(), active.end(),
                         [](const Segment *a, const Segment *b) { return a->targetStart < b->targetStart; });
        if (active.size() >= 2 && active[0]->transition && active[1]->targetStart < active[0]->targetEnd()) {
            // Transição: as duas camadas em imagens do tamanho do canvas, misturadas.
            const auto place = [&](const Segment &seg) {
                LinearImage img(size.width(), size.height());
                if (auto l = d->layer(project, seg, t, size, k, options))
                    img.draw(l->image, l->transform, l->opacity);
                return img;
            };
            const LinearImage a = place(*active[0]), b = place(*active[1]);
            const Micros start = active[1]->targetStart, end = active[0]->targetEnd();
            const double progress = double(t - start) / double(std::max<Micros>(1, end - start));
            canvas.drawAt(Transitions::apply(active[0]->transition->kind, a, b, progress, k), 0, 0);
            continue;
        }
        for (const Segment *seg : active)
            if (auto l = d->layer(project, *seg, t, size, k, options))
                canvas.draw(l->image, l->transform, l->opacity);
    }

    for (const Track &track : project.tracks) {
        if (track.kind != TrackKind::Text || track.hidden)
            continue;
        for (const Segment &seg : track.segments)
            if (seg.targetRange().contains(t))
                if (auto l = d->textLayer(seg, t, size))
                    canvas.draw(l->image, l->transform, l->opacity);
    }
    return canvas.toImage();
}

QImage EffectPreview::filtered(const QImage &image, FilterPreset preset)
{
    LinearImage img = LinearImage::fromImage(image);
    ImageEffects::filter(img, FilterSetting{preset, 1});
    return img.toImage();
}

QImage ProjectThumbnail::image(const Project &project)
{
    const Micros total = project.duration();
    if (total <= 0)
        return {};
    const Micros t = std::min<Micros>(total / 3, Time::seconds(2));
    return Renderer::shared().render(project, t, project.canvasSize(360));
}

QByteArray ProjectThumbnail::jpeg(const Project &project)
{
    const QImage frame = image(project);
    if (frame.isNull())
        return {};
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    frame.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", 80);
    return data;
}

} // namespace of
