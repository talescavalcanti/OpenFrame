#include "Transcriber.h"

#include "AudioMixdown.h"
#include "CaptionBuilder.h"

#include <QPointer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace of {

TranscriptionProgress TranscriptionProgress::make(Phase phase, double fraction)
{
    TranscriptionProgress p;
    p.phase = phase;
    p.fraction = std::clamp(fraction, 0.0, 1.0);
    p.overall = phase == Phase::Mixing ? mixingWeight * p.fraction : mixingWeight + (1 - mixingWeight) * p.fraction;
    return p;
}

QString message(TranscriptionError error, WhisperModel model, const QString &detail)
{
    switch (error) {
    case TranscriptionError::EngineUnavailable:
        return QStringLiteral("O motor de transcrição não está disponível nesta versão.");
    case TranscriptionError::ModelNotInstalled:
        return QStringLiteral("O modelo %1 não está instalado. Baixe-o antes de gerar.").arg(displayName(model));
    case TranscriptionError::NoAudio: return QStringLiteral("Não há áudio para transcrever nesse trecho.");
    case TranscriptionError::Failed: return QStringLiteral("A transcrição falhou: %1").arg(detail);
    }
    return {};
}

std::optional<TimeRange> Transcriber::sourceRange(const Project &project, const TranscriptionSource &source)
{
    if (!source.segment) {
        if (project.duration() <= 0)
            return std::nullopt;
        return TimeRange{0, project.duration()};
    }
    const auto seg = project.segment(*source.segment);
    const Material *m = seg ? project.material(seg->materialId) : nullptr;
    if (!m || !m->hasAudio)
        return std::nullopt;
    return seg->targetRange();
}

Project Transcriber::mixProject(const Project &project, const TranscriptionSource &source)
{
    const auto loc = source.segment ? project.locate(*source.segment) : std::nullopt;
    if (!loc)
        return project;
    Project p = project;
    for (Track &t : p.tracks)
        t.segments.removeIf([&](const Segment &s) { return s.id != *source.segment; });
    p.tracks[loc->track].muted = false;
    Segment &seg = p.tracks[loc->track].segments[0];
    if (seg.volume <= 0)
        seg.volume = 1;
    seg.audioDetached = false;
    return p;
}

QList<Transcriber::Language> Transcriber::languages()
{
    return {{"pt", "Português"}, {"en", "Inglês"},     {"es", "Espanhol"},  {"fr", "Francês"},   {"de", "Alemão"},
            {"it", "Italiano"},  {"ja", "Japonês"},    {"ko", "Coreano"},   {"zh", "Chinês"},    {"ru", "Russo"},
            {"ar", "Árabe"},     {"hi", "Hindi"},      {"nl", "Holandês"},  {"pl", "Polonês"},   {"tr", "Turco"},
            {"uk", "Ucraniano"}, {"sv", "Sueco"},      {"he", "Hebraico"},  {"id", "Indonésio"}, {"vi", "Vietnamita"}};
}

QList<TranscribedWord> Transcriber::fakeWords(TimeRange range)
{
    static const QStringList tokens =
        QStringLiteral("Olá, pessoal! Hoje eu vou mostrar como gerar legendas automáticas no OpenFrame. "
                       "É tudo feito no seu computador, sem enviar nada para a internet. "
                       "Você escolhe o estilo, o idioma e a precisão, e pronto.")
            .split(u' ');
    QList<TranscribedWord> words;
    Micros t = range.start + 300'000;
    for (int k = 0; t < range.end() - 200'000; ++k) {
        const QString &text = tokens[k % tokens.size()];
        const Micros length = 180'000 + 45'000 * std::min<qsizetype>(text.size(), 10);
        const Micros end = std::min(t + length, range.end());
        words.append({text, t, end, 0.9});
        t = end + (CaptionBuilder::endsSentence(text) ? 450'000 : text.endsWith(u',') ? 250'000 : 60'000);
    }
    return words;
}

std::optional<QList<TranscribedWord>> Transcriber::run(const Project &project, const TranscriptionSource &source,
                                                       const QString &modelPath, std::optional<WhisperModel> model,
                                                       const WhisperEngine::Options &options,
                                                       const std::atomic<bool> &cancelled,
                                                       const std::function<void(const TranscriptionProgress &)> &progress,
                                                       QString *error)
{
    using P = TranscriptionProgress::Phase;
    const auto report = [&](P phase, double fraction) {
        if (progress)
            progress(TranscriptionProgress::make(phase, fraction));
    };
    const auto fail = [&](const QString &message) -> std::optional<QList<TranscribedWord>> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const auto range = sourceRange(project, source);
    if (!range)
        return fail(message(TranscriptionError::NoAudio));
    if (!WhisperEngine::isAvailable())
        return fail(message(TranscriptionError::EngineUnavailable));
    report(P::Mixing, 0);
    const auto samples = AudioMixdown::samples(mixProject(project, source), *range, [&](double f) {
        report(P::Mixing, f);
        return !cancelled;
    });
    if (cancelled)
        return fail({});
    if (!samples || std::none_of(samples->begin(), samples->end(), [](float x) { return std::abs(x) > 1e-4f; }))
        return fail(message(TranscriptionError::NoAudio));

    report(P::Transcribing, 0);
    QString engineError;
    const auto words = WhisperEngine::transcribe(*samples, modelPath, model, options, cancelled,
                                                 [&](double f) { report(P::Transcribing, f); }, &engineError);
    if (cancelled)
        return fail({});
    if (!words)
        return fail(message(TranscriptionError::Failed, model.value_or(WhisperModel::Small), engineError));
    QList<TranscribedWord> out;
    for (const EngineWord &w : *words) {
        const Micros start = range->start + Time::seconds(w.start);
        const Micros end = std::min(range->end(), range->start + Time::seconds(w.end));
        if (start >= range->end())
            continue;
        out.append({w.text, start, std::max(end, start), w.probability});
    }
    return out;
}

struct Transcriber::Impl {
    std::atomic<bool> cancelled{false};
    QThread *thread = nullptr;
    bool running = false;
};

Transcriber::Transcriber(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}

Transcriber::~Transcriber()
{
    if (d->thread) {
        d->cancelled = true;
        d->thread->wait();
        delete d->thread;
    }
}

void Transcriber::start(const Project &project, const TranscriptionSource &source, WhisperModel model,
                        const QString &language)
{
    if (d->running)
        return;
    if (d->thread) {
        d->thread->wait();
        delete d->thread;
        d->thread = nullptr;
    }
    d->running = true;
    d->cancelled = false;
    const bool fake = qEnvironmentVariable("OPENFRAME_FAKE_TRANSCRIPTION") == QStringLiteral("1");
    const bool installed = ModelStore::shared().isInstalled(model);
    QPointer<Transcriber> self(this);
    d->thread = QThread::create([this, self, project, source, model, language, fake, installed] {
        const auto report = [&](TranscriptionProgress::Phase phase, double fraction) {
            const auto p = TranscriptionProgress::make(phase, fraction);
            QMetaObject::invokeMethod(this, [self, p] {
                if (self)
                    emit self->progress(p);
            });
        };
        bool ok = false;
        QList<TranscribedWord> words;
        QString error;
        const auto range = sourceRange(project, source);
        if (!range) {
            error = message(TranscriptionError::NoAudio);
        } else if (fake) {
            using P = TranscriptionProgress::Phase;
            bool stopped = false;
            for (int step = 1; step <= 13 && !stopped; ++step) {
                QThread::msleep(60);
                stopped = d->cancelled;
                if (!stopped)
                    report(step <= 3 ? P::Mixing : P::Transcribing, step <= 3 ? step / 3.0 : (step - 3) / 10.0);
            }
            if (!stopped) {
                words = fakeWords(*range);
                ok = true;
            }
        } else if (!installed) {
            error = message(TranscriptionError::ModelNotInstalled, model);
        } else {
            WhisperEngine::Options options;
            options.language = language;
            const auto result = run(project, source, ModelStore::shared().path(model), model, options, d->cancelled,
                                    [&](const TranscriptionProgress &p) { report(p.phase, p.fraction); }, &error);
            if (result) {
                words = *result;
                ok = true;
            }
        }
        QMetaObject::invokeMethod(this, [self, ok, words, error] {
            if (!self)
                return;
            self->d->running = false;
            emit self->finished(ok, words, error);
        });
    });
    d->thread->start();
}

void Transcriber::cancel() { d->cancelled = true; }

bool Transcriber::isRunning() const { return d->running; }

} // namespace of
