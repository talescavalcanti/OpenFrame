// Gravar áudio na timeline: o painel com o microfone e o nível, a contagem, a
// gravação com a timeline tocando muda e o clipe no fim (a parte do
// `EditorModel` que corresponde ao `presentRecorder`/`startRecording` do Mac).

#include "AudioRecorder.h"
#include "CoreBridge.h"
#include "EditorModel.h"
#include "MediaCache.h"
#include "MediaProbe.h"
#include "ProjectStore.h"
#include "TimelineEditor.h"

#include <QDir>
#include <QUuid>
#include <algorithm>

namespace of {

QString RecordingSession::inputName() const
{
    QString defaultName;
    for (const QVariant &v : m_inputs) {
        const QVariantMap input = v.toMap();
        if (input.value("id").toString() == m_input && !m_input.isEmpty())
            return input.value("name").toString();
        if (input.value("id").toString() == m_defaultInput)
            defaultName = input.value("name").toString();
    }
    return defaultName.isEmpty() ? QStringLiteral("Padrão do sistema") : QStringLiteral("Padrão (%1)").arg(defaultName);
}

QString RecordingSession::durationText() const
{
    return Time::format(Micros(m_duration * 1'000'000));
}

void EditorModel::setupRecording()
{
    m_recording = new RecordingSession(this);
    m_recorder = new AudioRecorder(this);
    connect(m_recorder, &AudioRecorder::levelChanged, this, &EditorModel::recordingLevel);
    connect(m_recorder, &AudioRecorder::peaksChanged, this, [this] {
        const QList<float> peaks = m_recorder->peaks();
        MediaCache::instance()->setPeaks(RecordingSession::peaksId, QVector<float>(peaks.begin(), peaks.end()));
    });
    connect(m_recorder, &AudioRecorder::finished, this, &EditorModel::recordingFinished);
    // Fim do projeto: a reprodução parou sozinha.
    connect(m_preview, &PreviewEngine::finished, this, [this] {
        if (m_recording->m_phase == "recording" && m_stopsAtEnd)
            stopRecording();
    });
    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_recording->m_countdown > 0) {
            emit m_recording->changed();
            return;
        }
        m_countdownTimer.stop();
        beginRecording();
    });
}

void EditorModel::presentRecorder()
{
    RecordingSession *r = m_recording;
    if (r->active())
        return;
    r->m_showPanel = true;
    r->m_phase = QStringLiteral("ready");
    r->m_error.clear();
    r->m_confirmDiscard = false;
    r->m_inputs.clear();
    for (const AudioInput &input : AudioRecorder::inputs())
        r->m_inputs.append(QVariantMap{{"id", input.id}, {"name", input.name}});
    r->m_defaultInput = AudioRecorder::defaultInput().value_or(QString());
    const bool known = std::any_of(r->m_inputs.begin(), r->m_inputs.end(),
                                   [&](const QVariant &v) { return v.toMap().value("id").toString() == r->m_input; });
    if (!known)
        r->m_input.clear();
    emit r->changed();
    // Sem permissão ou sem microfone, o `start` do monitor falha com a mensagem pronta.
    AudioRecorder::requestPermission(this, [this](bool) {
        if (m_recording->m_showPanel && m_recording->m_phase == "ready")
            startRecordingMonitor();
    });
}

void EditorModel::closeRecorder()
{
    if (m_recording->active())
        return;
    stopRecordingMonitor();
    m_recording->m_showPanel = false;
    emit m_recording->changed();
}

void EditorModel::selectRecordingInput(const QString &id)
{
    m_recording->m_input = id;
    emit m_recording->changed();
    if (m_monitor)
        startRecordingMonitor();
}

void EditorModel::startRecordingMonitor()
{
    stopRecordingMonitor();
    m_monitor = new AudioRecorder(this);
    connect(m_monitor, &AudioRecorder::levelChanged, this, &EditorModel::recordingLevel);
    connect(m_monitor, &AudioRecorder::finished, this, [this](bool, Micros, const QString &error) {
        if (error.isEmpty() || m_recording->m_phase != "ready")
            return;
        m_recording->m_phase = QStringLiteral("failed");
        m_recording->m_error = error;
        emit m_recording->changed();
    });
    const QString path = QDir::temp().filePath(
        QStringLiteral("openframe-nivel-%1.m4a").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QString input = m_recording->m_input;
    m_monitor->start(input.isEmpty() ? std::nullopt : std::optional<QString>(input), path);
}

void EditorModel::stopRecordingMonitor()
{
    if (!m_monitor)
        return;
    m_monitor->disconnect(this);
    m_monitor->cancel();
    m_monitor->deleteLater();
    m_monitor = nullptr;
    if (m_recording->m_phase != "recording") {
        m_recording->m_level = 0;
        emit m_recording->levelChanged();
    }
}

void EditorModel::recordingLevel(float level)
{
    RecordingSession *r = m_recording;
    r->m_level = std::max<qreal>(level, r->m_level * 0.82);
    if (r->m_phase == "recording" && sender() == m_recorder) {
        r->m_duration = Time::toSeconds(m_recorder->duration());
        updateRecordingRow();
    }
    emit r->levelChanged();
}

void EditorModel::updateRecordingRow()
{
    const TimeRange range{Micros(m_recording->m_start * 1'000'000),
                          std::max<Micros>(1, Micros(m_recording->m_duration * 1'000'000))};
    const QList<int> order = displayTrackOrder();
    int row = int(order.size());
    if (const auto t = TimelineEditor::freeTrack(TrackKind::Audio, range, m_project, std::nullopt))
        row = int(order.indexOf(*t));
    m_recording->m_row = row;
}

void EditorModel::startRecording()
{
    if (m_recording->m_phase != "ready")
        return;
    m_recording->m_phase = QStringLiteral("countdown");
    m_recording->m_countdown = 3;
    emit m_recording->changed();
    m_countdownTimer.start();
}

void EditorModel::beginRecording()
{
    RecordingSession *r = m_recording;
    stopRecordingMonitor();
    if (m_playing)
        togglePlay();
    m_recordingPath = m_library.recordingPath(m_libraryId);
    MediaCache::instance()->setPeaks(RecordingSession::peaksId, {});
    r->m_start = Time::toSeconds(m_playhead);
    r->m_duration = 0;
    r->m_phase = QStringLiteral("recording");
    updateRecordingRow();
    emit r->changed();
    emit r->levelChanged();
    const QString input = r->m_input;
    // Erro ao abrir: o `finished(false, 0, mensagem)` chega depois (recordingFinished).
    if (!m_recorder->start(input.isEmpty() ? std::nullopt : std::optional<QString>(input), m_recordingPath))
        return;
    setSelection({});
    // A timeline toca sem som, para o áudio do projeto não vazar para o microfone.
    m_preview->setAudioEnabled(false);
    m_stopsAtEnd = m_playhead < m_project.duration() - 30'000;
    if (m_stopsAtEnd && Bridge::hasPlayer()) {
        m_excerpt.reset();
        setPlaying(true);
        m_preview->play();
    }
}

void EditorModel::stopRecording()
{
    if (m_recording->m_phase != "recording")
        return;
    m_recording->m_phase = QStringLiteral("finishing");
    emit m_recording->changed();
    m_recorder->stop();
}

void EditorModel::cancelRecording(bool confirmed)
{
    RecordingSession *r = m_recording;
    if (r->m_phase == "countdown") {
        m_countdownTimer.stop();
        r->m_phase = QStringLiteral("ready");
        emit r->changed();
    } else if (r->m_phase == "recording") {
        if (r->m_duration > 3 && !confirmed) {
            r->m_confirmDiscard = true;
            emit r->changed();
            return;
        }
        r->m_confirmDiscard = false;
        m_recorder->cancel();
    }
}

void EditorModel::keepRecording()
{
    m_recording->m_confirmDiscard = false;
    emit m_recording->changed();
}

void EditorModel::recordingFinished(bool ok, Micros duration, const QString &error)
{
    RecordingSession *r = m_recording;
    if (m_playing)
        togglePlay();
    m_preview->setAudioEnabled(true);
    r->m_confirmDiscard = false;
    r->m_level = 0;
    if (!ok) {
        if (error.isEmpty()) {
            // Cancelada: volta ao painel, escutando o microfone de novo.
            r->m_phase = QStringLiteral("ready");
            r->m_duration = 0;
            emit r->changed();
            emit r->levelChanged();
            startRecordingMonitor();
        } else {
            r->m_phase = QStringLiteral("failed");
            r->m_error = error;
            emit r->changed();
        }
        return;
    }
    // Gravou (mesmo que o microfone tenha caído no meio: o gravado fica, e a mensagem aparece).
    QString probeError;
    const auto material = MediaProbe::probe(m_recordingPath, &probeError);
    if (!material) {
        r->m_phase = QStringLiteral("failed");
        r->m_error = probeError;
        emit r->changed();
        return;
    }
    const Micros at = Micros(r->m_start * 1'000'000);
    QUuid added;
    mutate([&](Project &p) { added = TimelineEditor::addRecording(*material, p, at); });
    loadMediaPreviews(*material);
    setSelection(ProjectStore::uuidString(added));
    r->m_duration = Time::toSeconds(duration);
    if (error.isEmpty()) {
        r->m_phase = QStringLiteral("ready");
        r->m_showPanel = false;
    } else {
        r->m_phase = QStringLiteral("failed");
        r->m_error = error;
    }
    emit r->changed();
    emit r->levelChanged();
}

} // namespace of
