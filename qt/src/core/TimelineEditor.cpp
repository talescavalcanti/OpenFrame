#include "TimelineEditor.h"

#include <algorithm>
#include <cmath>

namespace of::TimelineEditor {

namespace {

void sortByStart(QList<Segment> &segments)
{
    std::stable_sort(segments.begin(), segments.end(),
                     [](const Segment &a, const Segment &b) { return a.targetStart < b.targetStart; });
}

void normalize(Project &p)
{
    for (auto &track : p.tracks)
        if (!track.isMain)
            sortByStart(track.segments);
    relayoutMain(p);
    removeEmptyTracks(p);
}

bool isFree(const Track &track, TimeRange range)
{
    return std::none_of(track.segments.cbegin(), track.segments.cend(), [&](const Segment &s) {
        return s.targetStart < range.end() && range.start < s.targetEnd();
    });
}

/// Coloca o segmento na primeira faixa não principal do tipo com espaço
/// livre; cria uma nova faixa se nenhuma servir.
void place(const Segment &seg, TrackKind kind, Project &p)
{
    if (const auto t = freeTrack(kind, seg.targetRange(), p, seg.id)) {
        p.tracks[*t].segments.append(seg);
        sortByStart(p.tracks[*t].segments);
    } else {
        Track track(kind);
        track.segments = {seg};
        p.tracks.append(track);
    }
}

void addMaterial(const Material &m, Project &p)
{
    if (std::none_of(p.materials.cbegin(), p.materials.cend(), [&](const Material &x) { return x.id == m.id; }))
        p.materials.append(m);
}

int ensureMainTrack(Project &p)
{
    if (!p.mainTrackIndex())
        p.tracks.insert(0, Track(TrackKind::Video, true));
    return *p.mainTrackIndex();
}

/// Vídeo e áudio têm duração limitada pelo arquivo; foto e texto não.
bool isTimed(const Segment &s, const Project &p)
{
    const Material *m = p.material(s.materialId);
    return m && m->kind != MaterialKind::Photo;
}

Micros center(const Segment &s) { return (s.targetStart + s.targetEnd()) / 2; }

/// Os tempos das palavras são relativos ao início do segmento: quando o início muda
/// (aparar, dividir), elas continuam no mesmo instante da timeline.
void shiftWords(Segment &s, Micros delta)
{
    if (delta == 0 || !s.text || !s.text->words)
        return;
    for (auto &w : *s.text->words) {
        w.start += delta;
        w.end += delta;
    }
}

std::optional<Micros> nearestPoint(Micros t, const QList<Micros> &points, Micros threshold)
{
    std::optional<Micros> best;
    Micros bestDistance = threshold + 1;
    for (const Micros point : points) {
        const Micros d = std::abs(point - t);
        if (d <= threshold && d < bestDistance) {
            best = point;
            bestDistance = d;
        }
    }
    return best;
}

} // namespace

// MARK: - Layout

void relayoutMain(Project &p)
{
    const auto mi = p.mainTrackIndex();
    if (!mi)
        return;
    auto &segments = p.tracks[*mi].segments;
    Micros t = 0;
    for (int i = 0; i < segments.size(); ++i) {
        segments[i].targetStart = t;
        const Segment &seg = segments[i];
        Micros overlap = 0;
        if (i + 1 < segments.size() && seg.transition) {
            const Segment &next = segments[i + 1];
            overlap = std::max<Micros>(
                0, std::min({seg.transition->duration, seg.targetDuration() / 2, next.targetDuration() / 2}));
        }
        t = seg.targetEnd() - overlap;
    }
}

void removeEmptyTracks(Project &p)
{
    p.tracks.removeIf([](const Track &t) { return !t.isMain && t.segments.isEmpty(); });
}

// MARK: - Adição

TimeRange sourceRange(const Material &m)
{
    if (m.kind == MaterialKind::Photo)
        return {0, defaultPhotoDuration};
    return {0, std::max(m.duration, minDuration)};
}

QUuid addMedia(const Material &m, Project &p, Micros time)
{
    addMaterial(m, p);
    if (m.kind == MaterialKind::Audio) {
        const Segment seg(m.id, sourceRange(m), std::max<Micros>(0, time));
        place(seg, TrackKind::Audio, p);
        return seg.id;
    }
    const int mi = ensureMainTrack(p);
    const auto &main = p.tracks[mi].segments;
    const Micros end = main.isEmpty() ? 0 : main.last().targetEnd();
    const Segment seg(m.id, sourceRange(m), end);
    p.tracks[mi].segments.append(seg);
    relayoutMain(p);
    return seg.id;
}

QUuid addOverlay(const Material &m, Project &p, Micros time)
{
    addMaterial(m, p);
    Segment seg(m.id, sourceRange(m), std::max<Micros>(0, time));
    seg.transform.scale = 0.5;
    place(seg, TrackKind::Video, p);
    return seg.id;
}

QUuid insertMedia(const Material &m, Project &p, Micros time, std::optional<int> track)
{
    addMaterial(m, p);
    const std::optional<int> target =
        track && *track >= 0 && *track < p.tracks.size() ? track : std::nullopt;
    Segment seg(m.id, sourceRange(m), std::max<Micros>(0, time));
    if (m.kind == MaterialKind::Audio) {
        if (target && p.tracks[*target].kind == TrackKind::Audio && isFree(p.tracks[*target], seg.targetRange())) {
            p.tracks[*target].segments.append(seg);
            sortByStart(p.tracks[*target].segments);
        } else {
            place(seg, TrackKind::Audio, p);
        }
        return seg.id;
    }
    if (target && !p.tracks[*target].isMain && p.tracks[*target].kind != TrackKind::Audio) {
        seg.transform.scale = 0.5;
        if (p.tracks[*target].kind == TrackKind::Video && isFree(p.tracks[*target], seg.targetRange())) {
            p.tracks[*target].segments.append(seg);
            sortByStart(p.tracks[*target].segments);
        } else {
            place(seg, TrackKind::Video, p);
        }
        return seg.id;
    }
    const int mi = ensureMainTrack(p);
    auto &main = p.tracks[mi].segments;
    const auto index = std::count_if(main.cbegin(), main.cend(), [&](const Segment &s) { return center(s) < time; });
    main.insert(index, seg);
    relayoutMain(p);
    return seg.id;
}

QUuid addText(const TextContent &content, Project &p, Micros time, Micros duration)
{
    const Segment seg = Segment::makeText(content, std::max<Micros>(0, time), std::max(duration, minDuration));
    place(seg, TrackKind::Text, p);
    return seg.id;
}

// MARK: - Áudio

bool canExtractAudio(const Project &p, const QUuid &id)
{
    const auto seg = p.segment(id);
    if (!seg || seg->text || seg->audioDetached)
        return false;
    const Material *m = p.material(seg->materialId);
    return m && m->kind == MaterialKind::Video && m->hasAudio;
}

std::optional<QUuid> extractAudio(Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    if (!canExtractAudio(p, id) || !loc)
        return std::nullopt;
    const Segment seg = p.tracks[loc->track].segments[loc->index];
    const Material video = *p.material(seg.materialId);
    const QString name = QStringLiteral("Áudio de %1").arg(video.name);
    const auto existing = std::find_if(p.materials.cbegin(), p.materials.cend(), [&](const Material &m) {
        return m.kind == MaterialKind::Audio && m.path == video.path && m.name == name;
    });
    Material audio;
    if (existing != p.materials.cend()) {
        audio = *existing;
    } else {
        audio.kind = MaterialKind::Audio;
        audio.path = video.path;
        audio.name = name;
        audio.duration = video.duration;
        audio.hasAudio = video.hasAudio;
        p.materials.append(audio);
    }

    Micros crossIn = 0, crossOut = 0;
    if (p.tracks[loc->track].isMain) {
        const QList<Segment> &segs = p.tracks[loc->track].segments;
        if (loc->index > 0)
            crossIn = std::max<Micros>(0, segs[loc->index - 1].targetEnd() - seg.targetStart);
        if (loc->index + 1 < segs.size())
            crossOut = std::max<Micros>(0, seg.targetEnd() - segs[loc->index + 1].targetStart);
    }
    Segment sound(audio.id, seg.source, seg.targetStart);
    sound.speed = seg.speed;
    sound.volume = seg.volume;
    sound.fadeIn = std::max(seg.fadeIn, crossIn);
    sound.fadeOut = std::max(seg.fadeOut, crossOut);
    p.tracks[loc->track].segments[loc->index].audioDetached = true;
    place(sound, TrackKind::Audio, p);
    return sound.id;
}

QUuid addRecording(const Material &m, Project &p, Micros time)
{
    addMaterial(m, p);
    const Segment seg(m.id, sourceRange(m), std::max<Micros>(0, time));
    place(seg, TrackKind::Audio, p);
    return seg.id;
}

// MARK: - Faixas

void setTrackHeight(Project &p, const QUuid &track, std::optional<double> height)
{
    for (Track &t : p.tracks)
        if (t.id == track)
            t.height = Track::clampedHeight(height);
}

void setAllTrackHeights(Project &p, std::optional<double> height)
{
    const auto h = Track::clampedHeight(height);
    for (Track &t : p.tracks)
        t.height = h;
}

std::optional<int> freeTrack(TrackKind kind, TimeRange range, const Project &p, std::optional<QUuid> excluding)
{
    for (int t = 0; t < p.tracks.size(); ++t) {
        const Track &track = p.tracks[t];
        // A faixa das legendas automáticas é só delas (substituir não pode levar texto do usuário).
        if (track.kind != kind || track.isMain || track.isAutoCaptions)
            continue;
        const bool collides = std::any_of(track.segments.cbegin(), track.segments.cend(), [&](const Segment &other) {
            return other.id != excluding && other.targetStart < range.end() && range.start < other.targetEnd();
        });
        if (!collides)
            return t;
    }
    return std::nullopt;
}

// MARK: - Edição

std::optional<QUuid> split(Project &p, const QUuid &id, Micros time)
{
    const auto loc = p.locate(id);
    if (!loc)
        return std::nullopt;
    const Segment seg = p.tracks[loc->track].segments[loc->index];
    if (time - seg.targetStart < minDuration || seg.targetEnd() - time < minDuration)
        return std::nullopt;

    const Micros offsetSrc = Time::round(double(time - seg.targetStart) * seg.speed);
    Segment left = seg;
    Segment right = seg;
    right.id = QUuid::createUuid();
    left.source.duration = offsetSrc;
    right.source.duration = seg.source.duration - offsetSrc;
    const Material *m = p.material(seg.materialId);
    if (!seg.text && !(m && m->kind == MaterialKind::Photo))
        right.source.start = seg.source.start + offsetSrc;
    right.targetStart = time;
    shiftWords(right, seg.targetStart - time);
    left.transition.reset();
    left.animationOut.reset();
    left.fadeOut = 0;
    right.animationIn.reset();
    right.fadeIn = 0;
    auto &segments = p.tracks[loc->track].segments;
    segments[loc->index] = left;
    segments.insert(loc->index + 1, right);
    normalize(p);
    return right.id;
}

void remove(Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    if (!loc)
        return;
    p.tracks[loc->track].segments.removeAt(loc->index);
    normalize(p);
}

std::optional<QUuid> duplicate(Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    if (!loc)
        return std::nullopt;
    Segment copy = p.tracks[loc->track].segments[loc->index];
    copy.id = QUuid::createUuid();
    if (p.tracks[loc->track].isMain) {
        p.tracks[loc->track].segments.insert(loc->index + 1, copy);
    } else {
        copy.targetStart = p.tracks[loc->track].segments[loc->index].targetEnd();
        place(copy, p.tracks[loc->track].kind, p);
    }
    normalize(p);
    return copy.id;
}

void move(Project &p, const QUuid &id, Micros start)
{
    const auto loc = p.locate(id);
    if (!loc || p.tracks[loc->track].isMain)
        return;
    p.tracks[loc->track].segments[loc->index].targetStart = std::max<Micros>(0, start);
}

void reorderMain(Project &p, const QUuid &id, Micros toCenter)
{
    const auto loc = p.locate(id);
    if (!loc || !p.tracks[loc->track].isMain)
        return;
    auto &segments = p.tracks[loc->track].segments;
    const int i = loc->index;
    int index = 0;
    for (int j = 0; j < segments.size(); ++j) {
        const Micros c = center(segments[j]);
        if (j < i ? c <= toCenter : (j > i && c < toCenter))
            ++index;
    }
    const Segment seg = segments.takeAt(i);
    segments.insert(index, seg);
    relayoutMain(p);
}

void resolveOverlap(Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    if (!loc || p.tracks[loc->track].isMain)
        return;
    auto &segments = p.tracks[loc->track].segments;
    const Segment seg = segments[loc->index];
    const bool overlaps = std::any_of(segments.cbegin(), segments.cend(), [&](const Segment &s) {
        return s.id != id && s.targetStart < seg.targetEnd() && seg.targetStart < s.targetEnd();
    });
    if (overlaps) {
        segments.removeAt(loc->index);
        place(seg, p.tracks[loc->track].kind, p);
    }
    normalize(p);
}

bool canMove(const Project &p, const QUuid &id, int target)
{
    const auto loc = p.locate(id);
    if (!loc || target == loc->track || target < 0 || target >= p.tracks.size())
        return false;
    const Segment &seg = p.tracks[loc->track].segments[loc->index];
    const bool isText = seg.text.has_value();
    const Material *m = p.material(seg.materialId);
    const bool isAudio = m && m->kind == MaterialKind::Audio;
    switch (p.tracks[target].kind) {
    case TrackKind::Text: return isText;
    case TrackKind::Audio: return isAudio;
    case TrackKind::Video: return !isText && !isAudio;
    }
    return false;
}

void moveToTrack(Project &p, const QUuid &id, int target)
{
    if (!canMove(p, id, target))
        return;
    const auto loc = p.locate(id);
    const Segment seg = p.tracks[loc->track].segments[loc->index];
    const bool destIsMain = p.tracks[target].isMain;
    p.tracks[loc->track].segments.removeAt(loc->index);
    auto &dest = p.tracks[target].segments;
    if (destIsMain) {
        const Micros c = center(seg);
        const auto index = std::count_if(dest.cbegin(), dest.cend(), [&](const Segment &s) { return center(s) < c; });
        dest.insert(index, seg);
    } else {
        dest.append(seg);
    }
    normalize(p);
    resolveOverlap(p, id);
}

void trimStart(Project &p, const QUuid &id, Micros newStart)
{
    const auto loc = p.locate(id);
    if (!loc)
        return;
    Segment s = p.tracks[loc->track].segments[loc->index];
    const bool timed = isTimed(s, p);
    Micros deltaSrc = Time::round(double(newStart - s.targetStart) * s.speed);
    if (timed)
        deltaSrc = std::max(deltaSrc, -s.source.start);
    if (!p.tracks[loc->track].isMain)
        deltaSrc = std::max(deltaSrc, -Micros(double(s.targetStart) * s.speed));
    deltaSrc = std::min(deltaSrc, s.source.duration - Micros(double(minDuration) * s.speed));
    if (timed)
        s.source.start += deltaSrc;
    s.source.duration -= deltaSrc;
    const Micros oldStart = s.targetStart;
    s.targetStart += Time::round(double(deltaSrc) / s.speed);
    shiftWords(s, oldStart - s.targetStart);
    p.tracks[loc->track].segments[loc->index] = s;
    normalize(p);
}

void trimEnd(Project &p, const QUuid &id, Micros newEnd)
{
    const auto loc = p.locate(id);
    if (!loc)
        return;
    Segment s = p.tracks[loc->track].segments[loc->index];
    const Micros targetDur = std::max(minDuration, newEnd - s.targetStart);
    Micros srcDur = Time::round(double(targetDur) * s.speed);
    if (isTimed(s, p))
        if (const Material *m = p.material(s.materialId))
            srcDur = std::min(srcDur, m->duration - s.source.start);
    s.source.duration = std::max(srcDur, Micros(double(minDuration) * s.speed));
    p.tracks[loc->track].segments[loc->index] = s;
    normalize(p);
}

void setSpeed(Project &p, const QUuid &id, double speed)
{
    update(p, id, [speed](Segment &s) { s.speed = std::min(std::max(speed, 0.1), 10.0); });
}

void update(Project &p, const QUuid &id, const std::function<void(Segment &)> &change)
{
    const auto loc = p.locate(id);
    if (!loc)
        return;
    change(p.tracks[loc->track].segments[loc->index]);
    relayoutMain(p);
}

void setPlacement(Project &p, const QUuid &id, QPointF center, double scale, double rotation)
{
    scale = std::max(scale, 0.01);
    update(p, id, [&](Segment &s) {
        if (s.text) {
            s.text->style.x = center.x();
            s.text->style.y = center.y();
            s.text->style.scale = scale;
            s.text->style.rotation = rotation;
        } else {
            s.transform.x = center.x();
            s.transform.y = center.y();
            s.transform.scale = scale;
            s.transform.rotation = rotation;
        }
    });
}

void setTextWidth(Project &p, const QUuid &id, double maxWidth)
{
    const auto loc = p.locate(id);
    if (!std::isfinite(maxWidth) || !loc || !p.tracks[loc->track].segments[loc->index].text)
        return;
    const double width = std::clamp(maxWidth, TextStyle::maxWidthMin, TextStyle::maxWidthMax);
    Track &track = p.tracks[loc->track];
    if (track.isAutoCaptions) {
        for (auto &s : track.segments)
            if (s.text)
                s.text->style.maxWidth = width;
    } else {
        track.segments[loc->index].text->style.maxWidth = width;
    }
}

// MARK: - Ímã (snap)

QList<Micros> snapPoints(const Project &p, std::optional<QUuid> excluding, const QList<Micros> &extra)
{
    QList<Micros> points{0};
    points += extra;
    for (const auto &track : p.tracks)
        for (const auto &s : track.segments)
            if (s.id != excluding) {
                points.append(s.targetStart);
                points.append(s.targetEnd());
            }
    return points;
}

Micros snap(Micros t, const QList<Micros> &points, Micros threshold)
{
    return nearestPoint(t, points, threshold).value_or(t);
}

Micros snapMove(Micros start, Micros duration, const QList<Micros> &points, Micros threshold, bool forward)
{
    const Micros end = start + duration;
    const auto nearStart = nearestPoint(start, points, threshold);
    const auto nearEnd = nearestPoint(end, points, threshold);
    if (nearStart && nearEnd) {
        const Micros s = *nearStart - start;
        const Micros e = *nearEnd - end;
        if (std::abs(s) == std::abs(e))
            return start + (forward ? e : s);
        return start + (std::abs(s) < std::abs(e) ? s : e);
    }
    if (nearStart)
        return *nearStart;
    if (nearEnd)
        return start + (*nearEnd - end);
    return start;
}

} // namespace of::TimelineEditor
