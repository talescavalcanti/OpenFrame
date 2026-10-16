// Altura das faixas na timeline: a geometria vertical com a altura de cada faixa
// (a parte do `EditorModel` que corresponde ao `TimelineLayout.swift`).

#include "EditorModel.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace of {

qreal EditorModel::rowHeightOf(const Track &track) const
{
    return m_rowHeights.value(track.id, defaultRowHeight);
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
        y += row * (defaultRowHeight + rowSpacing);
    else if (row > count)
        y += (row - count) * (defaultRowHeight + rowSpacing);
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
    return count + int(std::floor((y - top) / (defaultRowHeight + rowSpacing)));
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
    return {QVariantMap{{"title", QStringLiteral("Compacta")}, {"height", 36}},
            QVariantMap{{"title", QStringLiteral("Normal")}, {"height", defaultRowHeight}},
            QVariantMap{{"title", QStringLiteral("Alta")}, {"height", 90}},
            QVariantMap{{"title", QStringLiteral("Muito alta")}, {"height", 140}}};
}

namespace {

/// Guarda a altura limitada; a padrão não é guardada.
void storeHeight(QHash<QUuid, qreal> &heights, const QUuid &id, qreal height)
{
    const qreal value = height > 0 ? std::clamp(height, EditorModel::minRowHeight, EditorModel::maxRowHeight)
                                   : EditorModel::defaultRowHeight;
    if (value == EditorModel::defaultRowHeight)
        heights.remove(id);
    else
        heights.insert(id, value);
}

} // namespace

void EditorModel::resizeRow(const QString &trackId, qreal height)
{
    storeHeight(m_rowHeights, QUuid::fromString(trackId), height);
    updateRowLayout();
}

void EditorModel::endRowResize() {}

void EditorModel::setAllRowHeights(qreal height)
{
    for (const auto &track : m_project.tracks)
        storeHeight(m_rowHeights, track.id, height);
    updateRowLayout();
}

} // namespace of
