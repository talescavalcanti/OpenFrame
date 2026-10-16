// Legendas automáticas: a janela, a geração e a edição no painel (a parte do
// `EditorModel` do Mac que trata das legendas).

#include "EditorModel.h"
#include "CanvasCore.h"
#include "ProjectStore.h"
#include "TimelineEditor.h"

namespace of {

namespace {

std::optional<QUuid> parseUuid(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    return uuid.isNull() ? std::nullopt : std::optional<QUuid>(uuid);
}

} // namespace

void EditorModel::setupCaptions()
{
    m_captions = new CaptionGeneration(this);
    connect(this, &EditorModel::projectChanged, this, &EditorModel::captionStateChanged);
    connect(this, &EditorModel::selectionChanged, this, &EditorModel::captionStateChanged);
    // O fundo dos cartões depende da faixa principal e da miniatura do material.
    connect(this, &EditorModel::thumbnailsChanged, this, &EditorModel::captionStateChanged);
    connect(m_captions, &CaptionGeneration::optionsChanged, this, &EditorModel::captionStateChanged);

    CaptionsCore::Engine *engine = m_captions->engine();
    connect(engine, &CaptionsCore::Engine::downloadProgress, this, [this](const QString &model, double f) {
        if (m_captionJob && m_captions->phase() == "downloading" && model == m_captionJob->model)
            m_captions->setPhase(QStringLiteral("downloading"), f);
    });
    connect(engine, &CaptionsCore::Engine::downloadFinished, this, [this](const QString &model, const QString &e) {
        if (!m_captionJob || m_captions->phase() != "downloading" || model != m_captionJob->model)
            return;
        if (!e.isEmpty()) {
            m_captionJob.reset();
            m_captions->setResult(0, e);
            return;
        }
        startTranscription();
    });
    connect(engine, &CaptionsCore::Engine::progress, this, [this](const QString &phase, double overall) {
        if (m_captionJob && m_captions->running())
            m_captions->setPhase(phase, overall);
    });
    connect(engine, &CaptionsCore::Engine::finished, this, [this](int count, const QString &error) {
        if (!m_captionJob)
            return;
        const CaptionJob job = *m_captionJob;
        m_captionJob.reset();
        if (!error.isEmpty() || count == 0) {
            m_captions->setResult(0, error.isEmpty() ? QStringLiteral("Nenhuma fala foi reconhecida nesse trecho.")
                                                     : error);
            return;
        }
        mutate([&](Project &p) {
            m_captions->engine()->apply(p, job.preset, job.accent, job.replacing, job.range);
        });
        setSelection({});
        setLibraryPanel(QStringLiteral("captions"));
        setLibraryVisible(true);
        m_captions->setResult(count, {});
    });
}

void EditorModel::setShowAutoCaptions(bool show)
{
    if (show == m_showAutoCaptions)
        return;
    m_showAutoCaptions = show;
    emit uiChanged();
}

bool EditorModel::clipWithAudio() const
{
    if (!m_selection)
        return false;
    const auto seg = m_project.segment(*m_selection);
    const Material *m = seg ? m_project.material(seg->materialId) : nullptr;
    return m && (m->kind == MaterialKind::Audio || m->hasAudio);
}

QString EditorModel::captionSourceProblem() const
{
    if (m_captions->source() == "clip")
        return clipWithAudio() ? QString() : QStringLiteral("Selecione um clipe com áudio para transcrever só ele.");
    for (const Track &track : m_project.tracks) {
        if (track.muted)
            continue;
        for (const Segment &seg : track.segments) {
            const Material *m = m_project.material(seg.materialId);
            if (seg.volume > 0 && m && (m->kind == MaterialKind::Audio || m->hasAudio))
                return {};
        }
    }
    return QStringLiteral("A timeline não tem áudio para transcrever.");
}

bool EditorModel::hasAutoCaptions() const
{
    return CaptionsCore::hasAutoCaptions(m_project);
}

bool EditorModel::selectionIsAutoCaption() const
{
    return m_selection && CaptionsCore::isAutoCaption(m_project, *m_selection);
}

void EditorModel::presentAutoCaptions()
{
    if (!m_captions->running()) {
        m_captions->setPhase(QStringLiteral("configuring"));
        m_captions->setReplacePrevious(hasAutoCaptions());
    }
    if (!m_captions->languageChosen) {
        m_captions->setLanguage(CaptionGeneration::systemLanguage());
        m_captions->languageChosen = true;
    }
    if (!clipWithAudio())
        m_captions->setSource(QStringLiteral("timeline"));
    setShowAutoCaptions(true);
}

void EditorModel::closeAutoCaptions()
{
    if (!m_captions->running())
        m_captions->setPhase(QStringLiteral("configuring"));
    setShowAutoCaptions(false);
}

void EditorModel::generateCaptions()
{
    if (m_captions->running() || !captionSourceProblem().isEmpty())
        return;
    CaptionJob job;
    if (m_captions->source() == "clip")
        job.segment = m_selection;
    QString error;
    const auto range = m_captions->engine()->sourceRange(m_project, job.segment, &error);
    if (!range) {
        m_captions->setResult(0, error);
        return;
    }
    if (m_playing)
        togglePlay();
    job.range = *range;
    job.preset = m_captions->preset();
    job.accent = m_captions->highlightColor();
    job.replacing = m_captions->replacePrevious();
    job.model = m_captions->model();
    job.language = m_captions->language();
    job.snapshot = m_project;
    m_captionJob = job;
    m_captions->cancelDownload(job.model);
    const bool download = !m_captions->engine()->isInstalled(job.model);
    m_captions->setIncludesDownload(download);
    if (download) {
        m_captions->setPhase(QStringLiteral("downloading"), 0);
        m_captions->engine()->download(job.model);
    } else {
        startTranscription();
    }
}

void EditorModel::startTranscription()
{
    if (!m_captionJob)
        return;
    m_captions->setPhase(QStringLiteral("mixing"), 0);
    m_captions->engine()->transcribe(m_captionJob->snapshot, m_captionJob->segment, m_captionJob->model,
                                     m_captionJob->language, m_captionJob->preset);
}

void EditorModel::cancelCaptionGeneration()
{
    if (m_captionJob) {
        m_captions->engine()->cancelDownload(m_captionJob->model);
        m_captions->engine()->cancel();
    }
    m_captionJob.reset();
    m_captions->setPhase(QStringLiteral("configuring"));
}

void EditorModel::applyStyleToAutoCaptions()
{
    if (!m_selection)
        return;
    const auto source = m_project.segment(*m_selection);
    if (!source || !source->text)
        return;
    mutate([&](Project &p) { CaptionsCore::applyStyleToAll(p, *source); });
}

void EditorModel::setCaptionText(const QString &id, const QString &text)
{
    const auto uuid = parseUuid(id);
    const auto seg = uuid ? m_project.segment(*uuid) : std::nullopt;
    if (!seg || !seg->text)
        return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed == seg->text->text)
        return;
    mutate([&](Project &p) {
        TimelineEditor::update(p, *uuid, [&](Segment &s) {
            if (s.text)
                s.text->text = trimmed;
        });
    });
}

void EditorModel::deleteCaption(const QString &id)
{
    const auto uuid = parseUuid(id);
    if (!uuid || !m_project.segment(*uuid))
        return;
    mutate([&](Project &p) { TimelineEditor::remove(p, *uuid); });
}

QString EditorModel::captionBackdrop() const
{
    const auto main = m_project.mainTrackIndex();
    if (!main)
        return {};
    for (const Segment &seg : m_project.tracks[*main].segments)
        if (const Material *m = m_project.material(seg.materialId); m && m->isVisual())
            return filterPreview(ProjectStore::uuidString(m->id), QStringLiteral("none"));
    return {};
}

qreal EditorModel::captionPreviewAspect() const
{
    return ratio(CaptionsCore::Preview::nearestAspect(m_project.aspectValue())).value_or(16.0 / 9);
}

void EditorModel::setTextWidth(qreal width)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) { CanvasCore::setTextWidth(p, id, width); }, ProjectStore::uuidString(id) + "boxWidth");
}

QVariantList EditorModel::revealOptions() const
{
    QVariantList list;
    for (auto r : allCases<WordReveal>())
        list.append(QVariantMap{{"value", enumKey(r)}, {"title", displayName(r)}});
    return list;
}

void EditorModel::toggleEmphasisWord(int index)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (!s.text || !s.text->emphasis)
                return;
            QList<int> &words = s.text->emphasis->words;
            if (words.contains(index))
                words.removeAll(index);
            else
                words.append(index);
            std::sort(words.begin(), words.end());
        });
    });
}

void EditorModel::setEmphasisFont(const QString &family)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const QStringList faces = fontFaces(family);
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (!s.text || !s.text->emphasis)
                return;
            s.text->emphasis->fontName = family;
            if (!faces.contains(s.text->emphasis->fontFace))
                s.text->emphasis->fontFace.clear();
        });
    });
}

} // namespace of
