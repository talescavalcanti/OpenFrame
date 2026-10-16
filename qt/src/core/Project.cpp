#include "Project.h"

#include <algorithm>
#include <cmath>

namespace of {

Segment Segment::makeText(const TextContent &content, Micros start, Micros duration)
{
    Segment s(std::nullopt, {0, duration}, start);
    s.text = content;
    return s;
}

std::optional<double> Track::clampedHeight(std::optional<double> height)
{
    if (!height || !std::isfinite(*height))
        return std::nullopt;
    return std::clamp(*height, heightMin, heightMax);
}

QString label(AspectRatio a)
{
    switch (a) {
    case AspectRatio::Original: return QStringLiteral("Original");
    case AspectRatio::R16x9: return QStringLiteral("16:9");
    case AspectRatio::R9x16: return QStringLiteral("9:16");
    case AspectRatio::R1x1: return QStringLiteral("1:1");
    case AspectRatio::R4x3: return QStringLiteral("4:3");
    case AspectRatio::R3x4: return QStringLiteral("3:4");
    case AspectRatio::R21x9: return QStringLiteral("21:9");
    case AspectRatio::R4x5: return QStringLiteral("4:5");
    }
    return {};
}

std::optional<double> ratio(AspectRatio a)
{
    switch (a) {
    case AspectRatio::Original: return std::nullopt;
    case AspectRatio::R16x9: return 16.0 / 9;
    case AspectRatio::R9x16: return 9.0 / 16;
    case AspectRatio::R1x1: return 1.0;
    case AspectRatio::R4x3: return 4.0 / 3;
    case AspectRatio::R3x4: return 3.0 / 4;
    case AspectRatio::R21x9: return 21.0 / 9;
    case AspectRatio::R4x5: return 4.0 / 5;
    }
    return std::nullopt;
}

const RGBA *CanvasBackground::colorValue() const
{
    if (const auto *c = std::get_if<Color>(&value))
        return &c->color;
    return nullptr;
}

std::optional<double> CanvasBackground::blurIntensity() const
{
    if (const auto *b = std::get_if<Blur>(&value))
        return b->intensity;
    return std::nullopt;
}

Micros Project::duration() const
{
    Micros end = 0;
    for (const auto &track : tracks)
        for (const auto &s : track.segments)
            end = std::max(end, s.targetEnd());
    return end;
}

const Material *Project::material(const std::optional<QUuid> &id) const
{
    if (!id)
        return nullptr;
    for (const auto &m : materials)
        if (m.id == *id)
            return &m;
    return nullptr;
}

std::optional<int> Project::mainTrackIndex() const
{
    for (int t = 0; t < tracks.size(); ++t)
        if (tracks[t].isMain)
            return t;
    return std::nullopt;
}

std::optional<Project::Location> Project::locate(const QUuid &segmentId) const
{
    for (int t = 0; t < tracks.size(); ++t)
        for (int i = 0; i < tracks[t].segments.size(); ++i)
            if (tracks[t].segments[i].id == segmentId)
                return Location{t, i};
    return std::nullopt;
}

std::optional<Segment> Project::segment(const QUuid &id) const
{
    const auto loc = locate(id);
    if (!loc)
        return std::nullopt;
    return tracks[loc->track].segments[loc->index];
}

double Project::aspectValue() const
{
    if (const auto v = ratio(canvas.aspect))
        return *v;
    if (const auto main = mainTrackIndex(); main && !tracks[*main].segments.isEmpty()) {
        const Material *m = material(tracks[*main].segments.first().materialId);
        if (m && m->width > 0 && m->height > 0)
            return double(m->width) / double(m->height);
    }
    return 16.0 / 9;
}

QSize Project::canvasSize(int shortSide) const
{
    const double aspect = aspectValue();
    const auto even = [](double v) { return std::max(2, int(Time::round(v / 2)) * 2); };
    if (aspect >= 1)
        return {even(double(shortSide) * aspect), even(double(shortSide))};
    return {even(double(shortSide)), even(double(shortSide) / aspect)};
}

} // namespace of
