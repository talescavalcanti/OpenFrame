#include "CaptionGeneration.h"

#include <QLocale>
#include <QVariantMap>
#include <cmath>

namespace of {

namespace {

bool sameStage(const QString &a, const QString &b)
{
    const auto transcribing = [](const QString &p) { return p == "mixing" || p == "transcribing"; };
    return a == b || (transcribing(a) && transcribing(b));
}

std::optional<CaptionsCore::ModelInfo> modelInfo(const QString &id)
{
    for (const auto &m : CaptionsCore::models())
        if (m.id == id)
            return m;
    return std::nullopt;
}

} // namespace

CaptionGeneration::CaptionGeneration(QObject *parent)
    : QObject(parent), m_engine(new CaptionsCore::Engine(this))
{
    m_phaseClock.start();
    connect(m_engine, &CaptionsCore::Engine::downloadProgress, this, [this](const QString &model, double f) {
        if (m_downloads.contains(model)) {
            m_downloads[model] = f;
            emit modelsChanged();
        }
    });
    connect(m_engine, &CaptionsCore::Engine::downloadFinished, this, [this](const QString &model, const QString &e) {
        m_downloads.remove(model);
        if (!e.isEmpty())
            m_modelError = e;
        emit modelsChanged();
    });
}

QString CaptionGeneration::megabytes(qint64 bytes)
{
    return QString::number(std::lround(bytes / 1e6)) + QStringLiteral(" MB");
}

QString CaptionGeneration::systemLanguage()
{
    const QString code = QLocale::system().name().section('_', 0, 0);
    for (const auto &l : CaptionsCore::languages())
        if (l.code == code)
            return code;
    return {};
}

// MARK: - Escolhas

void CaptionGeneration::setSource(const QString &v)
{
    if (v == m_source)
        return;
    m_source = v;
    emit optionsChanged();
}

void CaptionGeneration::setLanguage(const QString &v)
{
    if (v == m_language)
        return;
    m_language = v;
    emit optionsChanged();
}

void CaptionGeneration::setModel(const QString &v)
{
    if (v == m_model)
        return;
    m_model = v;
    emit optionsChanged();
    emit modelsChanged();
}

void CaptionGeneration::setPreset(const QString &v)
{
    if (v == m_preset)
        return;
    m_preset = v;
    emit optionsChanged();
}

void CaptionGeneration::setHighlightColor(const QColor &v)
{
    if (v == m_highlightColor)
        return;
    m_highlightColor = v;
    emit optionsChanged();
}

void CaptionGeneration::setReplacePrevious(bool v)
{
    if (v == m_replacePrevious)
        return;
    m_replacePrevious = v;
    emit optionsChanged();
}

bool CaptionGeneration::usesAccent() const
{
    for (const auto &p : CaptionsCore::presets())
        if (p.id == m_preset)
            return p.usesAccent;
    return false;
}

QVariantList CaptionGeneration::languages() const
{
    QVariantList list{QVariantMap{{"value", QString()}, {"title", QStringLiteral("Detectar automaticamente")}}};
    for (const auto &l : CaptionsCore::languages())
        list.append(QVariantMap{{"value", l.code}, {"title", l.name}});
    return list;
}

QVariantList CaptionGeneration::presetGroups() const
{
    QVariantList groups;
    for (const auto &p : CaptionsCore::presets())
        if (groups.isEmpty() || groups.last().toMap().value("id") != p.group)
            groups.append(QVariantMap{{"id", p.group}, {"name", p.groupName}});
    return groups;
}

QVariantList CaptionGeneration::presetsIn(const QString &group) const
{
    QVariantList items;
    for (const auto &p : CaptionsCore::presets())
        if (p.group == group)
            items.append(QVariantMap{{"id", p.id},
                                     {"name", p.name},
                                     {"detail", p.detail},
                                     {"layout", p.layout},
                                     {"usesAccent", p.usesAccent}});
    return items;
}

// MARK: - Modelos

QVariantList CaptionGeneration::models() const
{
    QVariantList list;
    for (const auto &m : CaptionsCore::models()) {
        const bool downloading = m_downloads.contains(m.id);
        const double f = m_downloads.value(m.id);
        list.append(QVariantMap{
            {"id", m.id},
            {"name", m.name},
            {"detail", m.detail},
            {"sizeText", megabytes(m.bytes)},
            {"recommended", m.id == CaptionsCore::recommendedModel()},
            {"installed", m_engine->isInstalled(m.id)},
            {"downloading", downloading},
            {"fraction", f},
            {"progressText", megabytes(qint64(m.bytes * f)) + " de " + megabytes(m.bytes)},
        });
    }
    return list;
}

QString CaptionGeneration::footerNote() const
{
    if (!modelPending())
        return QStringLiteral("A transcrição roda neste computador, sem enviar nada para a internet.");
    const auto m = modelInfo(m_model);
    return QStringLiteral("Baixa o modelo %1 (%2) uma vez. A transcrição roda neste computador.")
        .arg(m ? m->name : m_model, m ? megabytes(m->bytes) : QString());
}

void CaptionGeneration::downloadModel(const QString &id)
{
    if (m_downloads.contains(id) || m_engine->isInstalled(id))
        return;
    m_modelError.clear();
    m_downloads.insert(id, 0);
    m_engine->download(id);
    emit modelsChanged();
}

void CaptionGeneration::cancelDownload(const QString &id)
{
    m_engine->cancelDownload(id);
    m_downloads.remove(id);
    emit modelsChanged();
}

void CaptionGeneration::deleteModel(const QString &id)
{
    cancelDownload(id);
    QString error;
    if (!m_engine->remove(id, &error))
        m_modelError = error;
    emit modelsChanged();
}

// MARK: - Fase

bool CaptionGeneration::running() const
{
    return m_phase == "downloading" || m_phase == "mixing" || m_phase == "transcribing";
}

void CaptionGeneration::setPhase(const QString &phase, qreal progress)
{
    if (!sameStage(phase, m_phase))
        m_phaseClock.restart();
    m_phase = phase;
    m_progress = std::clamp<qreal>(progress, 0, 1);
    emit phaseChanged();
}

void CaptionGeneration::setIncludesDownload(bool v)
{
    m_includesDownload = v;
    emit phaseChanged();
}

void CaptionGeneration::setResult(int count, const QString &error)
{
    m_resultCount = count;
    m_error = error;
    setPhase(error.isEmpty() ? QStringLiteral("finished") : QStringLiteral("failed"), 1);
}

QString CaptionGeneration::detail() const
{
    const auto m = modelInfo(m_model);
    const QString name = m ? m->name : m_model;
    if (m_phase == "downloading") {
        const qint64 total = m ? m->bytes : 0;
        return QStringLiteral("Baixando o modelo %1: %2 de %3")
            .arg(name, megabytes(qint64(total * m_progress)), megabytes(total));
    }
    if (m_phase == "mixing")
        return m_source == "clip" ? QStringLiteral("Preparando o áudio do clipe")
                                  : QStringLiteral("Misturando o áudio da timeline");
    return QStringLiteral("Transcrevendo com o modelo %1").arg(name);
}

QString CaptionGeneration::remaining() const
{
    if (m_progress <= 0.03 || m_progress >= 1)
        return {};
    const double elapsed = m_phaseClock.elapsed() / 1000.0;
    if (elapsed <= 1)
        return {};
    const int seconds = int(std::lround(elapsed / m_progress * (1 - m_progress)));
    return seconds < 60 ? QStringLiteral("Restam ~%1 s").arg(seconds)
                        : QStringLiteral("Restam ~%1 min").arg(std::lround(seconds / 60.0));
}

} // namespace of
