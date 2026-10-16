// Aba Animação: loops e o trecho tocado no preview ao escolher um efeito
// (a parte do `EditorModel` que corresponde ao `AnimationTab.swift`).

#include "CoreBridge.h"
#include "EditorModel.h"
#include "MotionCore.h"
#include "ProjectStore.h"

namespace of {

QVariantList EditorModel::loopOptions() const
{
    return MotionCore::loopKinds();
}

void EditorModel::setLoop(const QString &kind)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto old = MotionCore::loop(m_project, id);
    std::optional<MotionCore::Loop> loop;
    if (!kind.isEmpty())
        loop = MotionCore::Loop{kind, old ? old->period : 2'000'000, old ? old->intensity : 0.5};
    mutate([&](Project &p) { MotionCore::setLoop(p, id, loop); });
    // Dois ciclos a partir do playhead, ou do início do clipe se ele estiver fora.
    const auto seg = m_project.segment(id);
    if (!seg || !loop)
        return;
    const Micros start = seg->targetRange().contains(m_playhead) ? m_playhead : seg->targetStart;
    playExcerpt(start, std::min(seg->targetEnd(), start + 2 * loop->period));
}

void EditorModel::setLoopValue(const QString &key, qreal value)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    auto loop = MotionCore::loop(m_project, id);
    if (!loop)
        return;
    if (key == "periodSeconds")
        loop->period = Time::seconds(std::clamp<qreal>(value, 0.5, 6));
    else if (key == "intensity")
        loop->intensity = std::clamp<qreal>(value, 0, 1);
    mutate([&](Project &p) { MotionCore::setLoop(p, id, loop); }, ProjectStore::uuidString(id) + "loop" + key);
}

void EditorModel::playExcerpt(Micros start, Micros end)
{
    const Micros resume = m_excerpt ? m_excerpt->second : m_playhead;
    if (m_playing) {
        m_preview->pause();
        setPlaying(false);
    }
    start = std::clamp<Micros>(start, 0, m_project.duration());
    end = std::min(end, m_project.duration());
    if (end <= start || !Bridge::hasPlayer())
        return;
    seekMicros(start);
    m_excerpt = {end, resume};
    setPlaying(true);
    m_preview->play();
}

} // namespace of
