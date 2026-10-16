// Altura das faixas na timeline: a geometria vertical com a altura de cada faixa
// (a parte do `EditorModel` que corresponde ao `TimelineLayout.swift`).

#include "EditorModel.h"
#include "TimelineEditor.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace of {

qreal EditorModel::rowHeightOf(const Track &track) const
{
    return track.height.value_or(Track::defaultHeight);
}

void EditorModel::updateRowLayout()
{
    QList<qreal> heights;
    for (int index : displayTrackOrder())
        heights.append(rowHeightOf(m_project.tracks[index]));
    if (heights == m_rowLayout)
        return;
    m_rowLayout = heights;
    emit rowLayoutChanged();
}

qreal EditorModel::rowTop(int row) const
{
    qreal y = rulerHeight + rowSpacing;
    const int count = int(m_rowLayout.size());
    for (int i = 0; i < std::min(std::max(row, 0), count); ++i)
        y += m_rowLayout[i] + rowSpacing;
    // Fora das faixas, linhas de altura padrão.
    if (row < 0)
        y += row * (Track::defaultHeight + rowSpacing);
    else if (row > count)
        y += (row - count) * (Track::defaultHeight + rowSpacing);
    return y;
}

int EditorModel::rowAt(qreal y) const
{
    qreal top = rulerHeight + rowSpacing;
    if (y < top)
        return -1;
    const int count = int(m_rowLayout.size());
    for (int i = 0; i < count; ++i) {
        top += m_rowLayout[i] + rowSpacing;
        if (y < top)
            return i;
    }
    return count + int(std::floor((y - top) / (Track::defaultHeight + rowSpacing)));
}

QVariantList EditorModel::rowTops() const
{
    QVariantList tops;
    for (int i = 0; i <= m_rowLayout.size(); ++i)
        tops.append(rowTop(i));
    return tops;
}

QVariantList EditorModel::rowHeights() const
{
    QVariantList list;
    for (qreal h : m_rowLayout)
        list.append(h);
    return list;
}

qreal EditorModel::rowsHeight() const
{
    return rowTop(int(m_rowLayout.size())) - rowSpacing;
}

qreal EditorModel::commonRowHeight() const
{
    QSet<qreal> heights;
    for (const auto &track : m_project.tracks)
        heights.insert(rowHeightOf(track));
    return heights.size() == 1 ? *heights.begin() : 0;
}

QVariantList EditorModel::rowHeightPresets() const
{
    // `height` = o que o menu grava (0 = a padrão, sem altura própria); `value` = a altura que ele marca.
    return {QVariantMap{{"title", QStringLiteral("Compacta")}, {"height", 36}, {"value", 36}},
            QVariantMap{{"title", QStringLiteral("Normal")}, {"height", 0}, {"value", Track::defaultHeight}},
            QVariantMap{{"title", QStringLiteral("Alta")}, {"height", 90}, {"value", 90}},
            QVariantMap{{"title", QStringLiteral("Muito alta")}, {"height", 140}, {"value", 140}}};
}

namespace {

std::optional<double> heightOrDefault(qreal height)
{
    return height > 0 ? std::optional<double>(height) : std::nullopt;
}

} // namespace

void EditorModel::resizeRow(const QString &trackId, qreal height)
{
    // Um desfazer por gesto (ao soltar, `endRowResize`); só a timeline muda, o preview não é refeito.
    beginGesture();
    Project copy = *m_gestureBase;
    TimelineEditor::setTrackHeight(copy, QUuid::fromString(trackId), heightOrDefault(height));
    if (copy == m_project)
        return;
    m_project = copy;
    m_dirty = true;
    scheduleAutosave();
    updateRowLayout();
}

void EditorModel::endRowResize()
{
    endGesture();
}

void EditorModel::setAllRowHeights(qreal height)
{
    mutate([&](Project &p) { TimelineEditor::setAllTrackHeights(p, heightOrDefault(height)); });
}

} // namespace of
