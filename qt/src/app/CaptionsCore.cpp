#include "CaptionsCore.h"

#include "CaptionBuilder.h"
#include "CaptionPresets.h"
#include "Renderer.h"
#include "Transcriber.h"
#include "WhisperModels.h"

#include <cmath>
#include <limits>

namespace of::CaptionsCore {

namespace {

std::optional<WhisperModel> whisper(const QString &id)
{
    return enumFromKey<WhisperModel>(id);
}

QString detailFor(WhisperModel m)
{
    switch (m) {
    case WhisperModel::Base: return QStringLiteral("O mais leve. Bom para fala clara e pouco ruído.");
    case WhisperModel::Small: return QStringLiteral("Bom equilíbrio entre velocidade e precisão.");
    case WhisperModel::Turbo: return QStringLiteral("Mais lento e mais preciso. Melhor para áudio difícil.");
    }
    return {};
}

} // namespace

QList<ModelInfo> models()
{
    QList<ModelInfo> list;
    for (auto m : allCases<WhisperModel>())
        list.append({enumKey(m), displayName(m), detailFor(m), byteSize(m)});
    return list;
}

QString recommendedModel()
{
    return enumKey(of::recommendedModel());
}

QUrl modelUrl(const QString &id)
{
    const auto m = enumFromKey<WhisperModel>(id);
    return m ? downloadUrl(*m) : QUrl();
}

QList<Language> languages()
{
    QList<Language> list;
    for (const auto &l : Transcriber::languages())
        list.append({l.code, l.name});
    return list;
}

QList<PresetInfo> presets()
{
    QList<PresetInfo> list;
    for (auto p : allCases<CaptionPreset>())
        list.append({enumKey(p), displayName(p), detail(p), enumKey(group(p)), displayName(group(p)),
                     enumKey(layout(p)), usesAccent(p)});
    return list;
}

QColor defaultHighlight()
{
    return WordHighlight().color.color();
}

bool hasAutoCaptions(const Project &p)
{
    return std::any_of(p.tracks.cbegin(), p.tracks.cend(), [](const Track &t) { return t.isAutoCaptions; });
}

bool isAutoCaption(const Project &p, const QUuid &id)
{
    const auto loc = p.locate(id);
    return loc && p.tracks[loc->track].isAutoCaptions;
}

void applyStyleToAll(Project &p, const Segment &source)
{
    if (!source.text)
        return;
    for (Track &track : p.tracks) {
        if (!track.isAutoCaptions)
            continue;
        for (Segment &seg : track.segments) {
            if (!seg.text)
                continue;
            TextContent &text = *seg.text;
            text.style = source.text->style;
            if (source.text->highlight && text.words)
                text.highlight = source.text->highlight;
            if (source.text->emphasis) {
                WordEmphasis emphasis = *source.text->emphasis;
                emphasis.words = text.emphasis ? text.emphasis->words : QList<int>();
                text.emphasis = emphasis;
            }
        }
    }
}

// MARK: - Prévia dos cartões

namespace Preview {

namespace {

const QString sample = QStringLiteral("Hoje eu vou mostrar como gerar legendas no seu computador.");

/// Uma palavra a cada ~0,26 s, a partir de 0,2 s.
QList<TranscribedWord> words()
{
    QList<TranscribedWord> list;
    Micros t = 200'000;
    for (const QString &word : sample.split(u' ')) {
        const Micros length = 170'000 + 18'000 * word.size();
        list.append({word, t, t + length, 1});
        t += length + 50'000;
    }
    return list;
}

} // namespace

AspectRatio nearestAspect(double value)
{
    AspectRatio best = AspectRatio::R16x9;
    if (!(value > 0) || !std::isfinite(value))
        return best;
    double distance = std::numeric_limits<double>::infinity();
    for (auto a : allCases<AspectRatio>())
        if (const auto r = ratio(a); r && std::abs(std::log(*r / value)) < distance) {
            distance = std::abs(std::log(*r / value));
            best = a;
        }
    return best;
}

QSize pixelSize(AspectRatio aspect)
{
    const double a = ratio(aspect).value_or(16.0 / 9);
    const auto even = [](double v) { return std::max(2, int(std::lround(v / 2)) * 2); };
    return a >= 16.0 / 9 ? QSize(384, even(384 / a)) : QSize(even(216 * a), 216);
}

QList<QImage> frames(const QString &preset, const std::optional<QColor> &accent, AspectRatio aspect)
{
    const CaptionPreset p = enumFromKey<CaptionPreset>(preset).value_or(CaptionPreset::Phrases);
    Project project;
    project.canvas.aspect = aspect;
    // O `apply` usa o `defaultStyle(preset, aspect)` pela proporção do canvas.
    const auto cues = CaptionBuilder::cues(words(), p, project.aspectValue());
    std::optional<RGBA> color;
    if (accent)
        color = RGBA{accent->redF(), accent->greenF(), accent->blueF(), accent->alphaF()};
    CaptionBuilder::apply(cues, p, color, project, false);
    const QSize size = pixelSize(aspect);
    RenderOptions options;
    options.transparent = true;
    QList<QImage> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i)
        list.append(Renderer::shared().render(project, Micros(cycle * 1'000'000 * i / count), size, options));
    return list;
}

} // namespace Preview

// MARK: - Engine

struct Engine::Impl {
    Transcriber transcriber;
    QList<CaptionCue> cues;
    CaptionPreset preset = CaptionPreset::Phrases;
    double aspect = 16.0 / 9;
};

Engine::Engine(QObject *parent) : QObject(parent), d(std::make_unique<Impl>())
{
    ModelStore &store = ModelStore::shared();
    connect(&store, &ModelStore::downloadProgress, this,
            [this](WhisperModel m, double fraction) { emit downloadProgress(enumKey(m), fraction); });
    connect(&store, &ModelStore::downloadFinished, this, [this](WhisperModel m, bool ok, const QString &error) {
        // Cancelado: ok falso e erro vazio.
        if (ok || !error.isEmpty())
            emit downloadFinished(enumKey(m), ok ? QString() : error);
    });
    connect(&d->transcriber, &Transcriber::progress, this, [this](const TranscriptionProgress &p) {
        emit progress(p.phase == TranscriptionProgress::Phase::Mixing ? QStringLiteral("mixing")
                                                                       : QStringLiteral("transcribing"),
                      p.overall);
    });
    connect(&d->transcriber, &Transcriber::finished, this,
            [this](bool ok, const QList<TranscribedWord> &words, const QString &error) {
                if (!ok && error.isEmpty())
                    return; // cancelado
                if (!ok) {
                    emit finished(0, error);
                    return;
                }
                d->cues = CaptionBuilder::cues(words, d->preset, d->aspect);
                emit finished(int(d->cues.size()), {});
            });
}

Engine::~Engine() = default;

bool Engine::isInstalled(const QString &model) const
{
    const auto m = whisper(model);
    return m && ModelStore::shared().isInstalled(*m);
}

void Engine::download(const QString &model)
{
    if (const auto m = whisper(model))
        ModelStore::shared().download(*m);
}

void Engine::cancelDownload(const QString &model)
{
    if (const auto m = whisper(model); m && ModelStore::shared().isDownloading(*m))
        ModelStore::shared().cancel(*m);
}

bool Engine::remove(const QString &model, QString *error)
{
    const auto m = whisper(model);
    return m && ModelStore::shared().remove(*m, error);
}

std::optional<TimeRange> Engine::sourceRange(const Project &p, const std::optional<QUuid> &segment,
                                             QString *error) const
{
    const auto range = Transcriber::sourceRange(p, segment ? TranscriptionSource::of(*segment)
                                                           : TranscriptionSource::timeline());
    if (!range && error)
        *error = message(TranscriptionError::NoAudio);
    return range;
}

void Engine::transcribe(const Project &p, const std::optional<QUuid> &segment, const QString &model,
                        const QString &language, const QString &preset)
{
    d->preset = enumFromKey<CaptionPreset>(preset).value_or(CaptionPreset::Phrases);
    d->aspect = p.aspectValue();
    d->cues.clear();
    d->transcriber.start(p, segment ? TranscriptionSource::of(*segment) : TranscriptionSource::timeline(),
                         whisper(model).value_or(of::recommendedModel()), language);
}

void Engine::cancel()
{
    d->transcriber.cancel();
}

void Engine::apply(Project &p, const QString &preset, const QColor &accent, bool replacing, TimeRange range) const
{
    const RGBA color{accent.redF(), accent.greenF(), accent.blueF(), accent.alphaF()};
    CaptionBuilder::apply(d->cues, enumFromKey<CaptionPreset>(preset).value_or(CaptionPreset::Phrases), color, p,
                          replacing, range);
}

} // namespace of::CaptionsCore
