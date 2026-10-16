#include "Motion.h"

#include <cmath>

namespace of {

namespace {

/// `raw` vai de 0 (invisível) a 1 (posição final), linear no tempo.
void apply(AnimationKind kind, double raw, bool exiting, AnimationState &st)
{
    const double p = Animations::ease(raw);
    const double hidden = 1 - p;
    // Na saída, "deslizar ←" continua indo para a esquerda.
    const double direction = exiting ? -1 : 1;
    switch (kind) {
    case AnimationKind::Fade: st.alpha *= p; break;
    case AnimationKind::ZoomIn:
        st.scale *= 0.5 + 0.5 * p;
        st.alpha *= p;
        break;
    case AnimationKind::ZoomOut:
        st.scale *= 1.5 - 0.5 * p;
        st.alpha *= p;
        break;
    case AnimationKind::SlideLeft: st.dx += hidden * 1.2 * direction; break;
    case AnimationKind::SlideRight: st.dx -= hidden * 1.2 * direction; break;
    case AnimationKind::SlideUp: st.dy -= hidden * 1.2 * direction; break;
    case AnimationKind::SlideDown: st.dy += hidden * 1.2 * direction; break;
    case AnimationKind::Spin:
        st.rotation += hidden * 360;
        st.scale *= std::max(p, 0.001);
        break;
    case AnimationKind::Typewriter: st.reveal = std::min(st.reveal, p); break;
    case AnimationKind::Focus: {
        const double e = Animations::expoOut(raw);
        st.blur = std::max(st.blur, (1 - e) * 0.02);
        st.scale *= 1.08 - 0.08 * e;
        st.alpha *= e;
        break;
    }
    case AnimationKind::Cascade:
    case AnimationKind::Elastic:
    case AnimationKind::Wave:
    case AnimationKind::WordByWord:
    case AnimationKind::MaskReveal:
    case AnimationKind::Tracking:
    case AnimationKind::Decode:
        st.unit = UnitAnimation{kind, Animations::clamp(raw), exiting};
        st.backgroundAlpha *= p;
        break;
    }
}

void applyLoop(const LoopAnimation &loop, Micros local, AnimationState &st)
{
    const double wave = std::sin(2 * M_PI * Animations::loopPhase(loop, local));
    const double intensity = Animations::clamp(loop.intensity);
    switch (loop.kind) {
    case LoopKind::Float: st.dy += wave * 0.02 * intensity; break;
    case LoopKind::Pulse: st.scale *= 1 + 0.05 * intensity * wave; break;
    case LoopKind::Glow:
    case LoopKind::Shine: break; // desenhados pelo renderizador (`glow()` e `shine()` do estado)
    }
}

TextGranularity baseGranularity(const Segment &seg, const AnimationState &state)
{
    if (state.unit)
        return Animations::granularity(state.unit->kind).value_or(TextGranularity::Line);
    if (state.reveal < 1)
        return TextGranularity::Letter;
    for (const auto &a : {seg.animationIn, seg.animationOut})
        if (a && Animations::isPerUnit(a->kind))
            if (const auto g = Animations::granularity(a->kind))
                return *g;
    return TextGranularity::Line;
}

/// Mistura linear (o CIDissolveTransition), em luz linear e pré-multiplicada.
LinearImage dissolve(const LinearImage &a, const LinearImage &b, double t)
{
    LinearImage out(a.width(), a.height());
    const float u = float(std::clamp(t, 0.0, 1.0)), v = 1 - u;
    parallelFor(out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const Pixel *pa = a.row(y), *pb = b.row(y);
            Pixel *po = out.row(y);
            for (int x = 0; x < out.width(); ++x)
                po[x] = {pa[x].r * v + pb[x].r * u, pa[x].g * v + pb[x].g * u, pa[x].b * v + pb[x].b * u,
                         pa[x].a * v + pb[x].a * u};
        }
    });
    return out;
}

LinearImage over(const LinearImage &top, const LinearImage &bottom)
{
    LinearImage out = bottom;
    out.drawAt(top, 0, 0);
    return out;
}

} // namespace

double AnimationState::glow() const
{
    if (!loop || loop->kind != LoopKind::Glow)
        return 0;
    return Animations::clamp(loop->intensity) * (0.5 + 0.5 * std::sin(2 * M_PI * Animations::loopPhase(*loop, local)));
}

std::optional<AnimationState::Shine> AnimationState::shine() const
{
    if (!loop || loop->kind != LoopKind::Shine)
        return std::nullopt;
    const double phase = Animations::loopPhase(*loop, local);
    const double f = (phase - std::floor(phase)) / 0.58;
    if (f >= 1)
        return std::nullopt;
    // Quase linear, com entrada e saída levemente suaves.
    const double eased = 0.8 * f + 0.2 * f * f * (3 - 2 * f);
    return Shine{eased, Animations::clamp(loop->intensity)};
}

AnimationState Animations::state(const Segment &seg, Micros local)
{
    AnimationState st;
    st.local = local;
    const Micros duration = seg.targetDuration();
    if (seg.animationIn && seg.animationIn->duration > 0 && local < seg.animationIn->duration)
        apply(seg.animationIn->kind, double(local) / double(seg.animationIn->duration), false, st);
    if (seg.animationOut && seg.animationOut->duration > 0 && local > duration - seg.animationOut->duration)
        apply(seg.animationOut->kind, double(duration - local) / double(seg.animationOut->duration), true, st);
    if (seg.animationLoop && seg.animationLoop->period > 0) {
        st.loop = seg.animationLoop;
        applyLoop(*seg.animationLoop, local, st);
    }
    return st;
}

double Animations::clamp(double x) { return std::clamp(x, 0.0, 1.0); }

double Animations::ease(double p) { return 1 - std::pow(1 - clamp(p), 3); }

double Animations::expoOut(double x) { return (1 - std::pow(2, -10 * clamp(x))) / (1 - std::pow(2, -10)); }

double Animations::quartOut(double x) { return 1 - std::pow(1 - clamp(x), 4); }

double Animations::quintOut(double x) { return 1 - std::pow(1 - clamp(x), 5); }

double Animations::spring(double x)
{
    x = clamp(x);
    return 1 - std::exp(-6 * x) * std::cos(3.5 * M_PI * x);
}

double Animations::loopPhase(const LoopAnimation &loop, Micros local)
{
    return double(std::max<Micros>(0, local)) / double(std::max<Micros>(loop.period, 1));
}

double Animations::peakScale(const Segment &seg)
{
    const auto peak = [](const std::optional<SegmentAnimation> &a) {
        if (!a)
            return 1.0;
        switch (a->kind) {
        case AnimationKind::ZoomOut: return 1.5;
        case AnimationKind::Focus: return 1.08;
        case AnimationKind::Elastic: return 1.15; // 0,3 + 0,7 × pico da mola (~1,2)
        case AnimationKind::Wave: return 1.12;
        default: return 1.0;
        }
    };
    double scale = std::max(peak(seg.animationIn), peak(seg.animationOut));
    if (seg.animationLoop && seg.animationLoop->kind == LoopKind::Pulse)
        scale *= 1 + 0.05 * clamp(seg.animationLoop->intensity);
    if (seg.text)
        if (const auto h = seg.text->activeHighlight())
            scale *= std::max(1.0, h->style.scale);
    return scale;
}

std::optional<TextGranularity> Animations::granularity(AnimationKind kind)
{
    switch (kind) {
    case AnimationKind::Cascade:
    case AnimationKind::Elastic:
    case AnimationKind::Wave:
    case AnimationKind::Tracking:
    case AnimationKind::Decode:
    case AnimationKind::Typewriter: return TextGranularity::Letter;
    case AnimationKind::WordByWord: return TextGranularity::Word;
    case AnimationKind::MaskReveal: return TextGranularity::Line;
    default: return std::nullopt;
    }
}

bool Animations::isPerUnit(AnimationKind kind) { return kind != AnimationKind::Typewriter && granularity(kind); }

bool Animations::usesUnits(const Segment &seg)
{
    if (seg.text && seg.text->activeHighlight())
        return true;
    if (seg.animationIn && isPerUnit(seg.animationIn->kind))
        return true;
    if (seg.animationOut && isPerUnit(seg.animationOut->kind))
        return true;
    return seg.animationLoop && (seg.animationLoop->kind == LoopKind::Glow || seg.animationLoop->kind == LoopKind::Shine);
}

TextGranularity Animations::granularityFor(const Segment &seg, const AnimationState &state)
{
    const TextGranularity g = baseGranularity(seg, state);
    const auto h = seg.text ? seg.text->activeHighlight() : std::nullopt;
    if (!h)
        return g;
    // A digitação revela letra a letra.
    if (h->style.reveal == WordReveal::Type)
        return TextGranularity::Letter;
    return g == TextGranularity::Line ? TextGranularity::Word : g;
}

LinearImage Transitions::apply(TransitionKind kind, const LinearImage &a, const LinearImage &b, double raw, double k)
{
    const double p = std::clamp(raw, 0.0, 1.0);
    const int w = a.width(), h = a.height();
    switch (kind) {
    case TransitionKind::Dissolve: return dissolve(a, b, p);
    case TransitionKind::FadeBlack:
    case TransitionKind::FadeWhite: {
        const float c = kind == TransitionKind::FadeBlack ? 0 : 1;
        const LinearImage color(w, h, Pixel{c, c, c, 1});
        return p < 0.5 ? dissolve(over(a, color), color, p * 2) : dissolve(color, over(b, color), p * 2 - 1);
    }
    case TransitionKind::WipeLeft:
    case TransitionKind::WipeRight: {
        // A nova imagem aparece numa faixa que cresce da direita (←) ou da esquerda (→).
        LinearImage out = a;
        const int cut = int(std::lround(w * p));
        const int from = kind == TransitionKind::WipeLeft ? w - cut : 0;
        LinearImage strip(cut, h);
        for (int y = 0; y < h; ++y)
            std::copy(b.row(y) + from, b.row(y) + from + cut, strip.row(y));
        out.drawAt(strip, from, 0);
        return out;
    }
    case TransitionKind::SlideLeft:
    case TransitionKind::SlideRight: {
        const double eased = Animations::ease(p);
        const double dir = kind == TransitionKind::SlideLeft ? 1 : -1;
        LinearImage out(w, h);
        out.drawAt(a, int(std::lround(-dir * w * eased)), 0);
        out.drawAt(b, int(std::lround(dir * w * (1 - eased))), 0);
        return out;
    }
    case TransitionKind::Zoom: {
        const double s = 1 + 0.6 * p;
        LinearImage zoomed(w, h);
        QTransform t;
        t.translate(w / 2.0, h / 2.0);
        t.scale(s, s);
        t.translate(-w / 2.0, -h / 2.0);
        zoomed.draw(a, t);
        return dissolve(zoomed, b, p);
    }
    case TransitionKind::Blur: {
        LinearImage mixed = dissolve(a, b, p);
        const double sigma = std::sin(p * M_PI) * 22 * k;
        if (sigma > 0.5)
            mixed.blur(sigma);
        return mixed;
    }
    }
    return dissolve(a, b, p);
}

} // namespace of
