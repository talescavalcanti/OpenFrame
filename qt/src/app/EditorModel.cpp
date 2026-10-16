#include "EditorModel.h"

#include "CoreBridge.h"
#include "FontCatalog.h"
#include "MediaCache.h"
#include "MotionCore.h"
#include "ProjectStore.h"
#include "Subtitles.h"
#include "TextPresets.h"
#include "TimelineEditor.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QUrl>
#include <QtConcurrent>
#include <cmath>

namespace of {

namespace {

EditorModel *g_instance = nullptr;

QString idString(const QUuid &id) { return ProjectStore::uuidString(id); }

std::optional<QUuid> parseId(const QString &s)
{
    const QUuid id = QUuid::fromString(s);
    if (id.isNull())
        return std::nullopt;
    return id;
}

/// Caminho de um arquivo local a partir da URL do diálogo do QML.
QString localPath(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }

/// "agora", "há 5 minutos", "há 2 horas", "ontem", "há 3 dias"…, como o
/// `RelativeDateTimeFormatter` em português do app Swift.
QString relativeDate(const QDateTime &date)
{
    const qint64 s = std::max<qint64>(0, date.secsTo(QDateTime::currentDateTime()));
    auto plural = [](qint64 n, const char *one, const char *many) {
        return QStringLiteral("há %1 %2").arg(n).arg(QString::fromUtf8(n == 1 ? one : many));
    };
    if (s < 60)
        return QStringLiteral("agora");
    if (s < 3600)
        return plural(s / 60, "minuto", "minutos");
    if (s < 86400)
        return plural(s / 3600, "hora", "horas");
    const qint64 days = s / 86400;
    if (days == 1)
        return QStringLiteral("ontem");
    if (days < 7)
        return plural(days, "dia", "dias");
    if (days < 30)
        return plural(days / 7, "semana", "semanas");
    if (days < 365)
        return plural(days / 30, "mês", "meses");
    return plural(days / 365, "ano", "anos");
}

QString shortSeconds(Micros t) { return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(Time::toSeconds(t), 'f', 1) + "s"; }

/// Lê/grava um valor no JSON pelo caminho "a.b.c".
QJsonObject setPath(QJsonObject o, const QStringList &keys, int i, const QJsonValue &value)
{
    const QString key = keys[i];
    if (i == keys.size() - 1) {
        if (value.isUndefined() || value.isNull())
            o.remove(key);
        else
            o[key] = value;
        return o;
    }
    bool numeric = false;
    keys[i + 1].toInt(&numeric);
    if (numeric && o.value(key).isArray()) {
        // Índice numérico no próximo nível (ex.: "effects.0.intensity").
        QJsonArray array = o.value(key).toArray();
        const int index = keys[i + 1].toInt();
        if (index >= 0 && index < array.size()) {
            if (i + 1 == keys.size() - 1)
                array[index] = value;
            else
                array[index] = setPath(array[index].toObject(), keys, i + 2, value);
        }
        o[key] = array;
        return o;
    }
    o[key] = setPath(o.value(key).toObject(), keys, i + 1, value);
    return o;
}

const QStringList tabKeys{"basic", "adjust", "filters", "effects", "animation", "transition", "text", "audio"};
const QStringList tabTitles{"Básico", "Ajustar", "Filtros", "Efeitos", "Animação", "Transição", "Texto", "Áudio"};
const QStringList tabIcons{"move", "sliders-horizontal", "blend", "sparkles", "wand-sparkles", "arrow-left-right", "type",
                           "audio-waveform"};

QString effectIcon(VideoEffectKind k)
{
    switch (k) {
    case VideoEffectKind::Blur: return "droplet";
    case VideoEffectKind::Pixelate: return "grid-3x3";
    case VideoEffectKind::RgbSplit: return "blend";
    case VideoEffectKind::Mirror: return "columns-2";
    case VideoEffectKind::Shake: return "activity";
    case VideoEffectKind::Flash: return "zap";
    case VideoEffectKind::ZoomPulse: return "zoom-in";
    case VideoEffectKind::Kaleidoscope: return "hexagon";
    case VideoEffectKind::Comic: return "pen-tool";
    case VideoEffectKind::Edges: return "spline";
    }
    return "sparkles";
}

} // namespace

EditorModel::EditorModel(QObject *parent)
    : QObject(parent),
      m_library(qEnvironmentVariableIsSet("OPENFRAME_LIBRARY_DIR") ? qEnvironmentVariable("OPENFRAME_LIBRARY_DIR")
                                                                   : ProjectLibrary::defaultRoot()),
      m_entries(new KeyedListModel({"id", "name", "subtitle", "durationText", "aspectValue", "thumbnail"}, this)),
      m_materials(new KeyedListModel({"id", "name", "kind", "durationText", "used", "isVisual"}, this)),
      m_tracks(new KeyedListModel({"id", "kind", "isMain", "muted", "hidden", "row", "icon"}, this)),
      m_clips(new KeyedListModel({"id", "row", "start", "duration", "kind", "icon", "title", "badges", "materialId",
                                  "sourceStart", "sourceDuration", "materialDuration", "aspect", "volume", "isMain",
                                  "dragged", "rowShift", "hidden", "hasAudio", "fullText", "durationText", "autoCaption"},
                                 this)),
      m_junctions(new KeyedListModel({"id", "time", "hasTransition", "nextId", "help"}, this)),
      m_cues(new KeyedListModel({"id", "start", "startText", "caption"}, this)),
      m_preview(new PreviewEngine(this)),
      m_exportJob(new ExportJob(this))
{
    g_instance = this;
    m_lastCoalesce.start();
    m_lastThumbnail = QDateTime::currentDateTime().addSecs(-60);

    connect(MediaCache::instance(), &MediaCache::changed, this, [this] {
        ++m_thumbnailRevision;
        emit thumbnailsChanged();
    });

    m_autosave.setSingleShot(true);
    m_autosave.setInterval(800);
    connect(&m_autosave, &QTimer::timeout, this, &EditorModel::saveNow);

    // O resumo da exportação depende das opções e do projeto (duração, proporção).
    connect(this, &EditorModel::exportSettingsChanged, this, &EditorModel::exportSummaryChanged);
    connect(this, &EditorModel::projectChanged, this, &EditorModel::exportSummaryChanged);

    setupCaptions();

    // Resolução do preview: ~300 ms depois que o tamanho do player parar.
    m_previewResolutionTimer.setSingleShot(true);
    m_previewResolutionTimer.setInterval(300);
    connect(&m_previewResolutionTimer, &QTimer::timeout, this, &EditorModel::updatePreviewResolution);

    m_slideTimer.setSingleShot(true);
    m_slideTimer.setInterval(200);
    connect(&m_slideTimer, &QTimer::timeout, this, [this] {
        m_slideAnimation = false;
        emit slideAnimationChanged();
    });

    connect(m_preview, &PreviewEngine::timeChanged, this, &EditorModel::playerTimeChanged);
    connect(m_preview, &PreviewEngine::finished, this, [this] {
        setPlaying(false);
        setPlayheadMicros(m_project.duration());
    });

    connect(m_exportJob, &ExportJob::progress, this, [this](double value) {
        if (m_exportPhase != "exporting")
            return;
        m_exportProgress = value;
        if (value > 0.02) {
            const double elapsed = m_exportClock.elapsed() / 1000.0;
            m_exportRemaining = QStringLiteral("Restam ~%1 s").arg(qRound(elapsed / value * (1 - value)));
        }
        emit exportStateChanged();
    });
    connect(m_exportJob, &ExportJob::finished, this, [this](bool ok, const QString &error) {
        if (ok) {
            const QFileInfo info(m_exportResultPath);
            m_exportResult = info.fileName() + " · " + formattedFileSize(info.size());
            m_exportPhase = "finished";
        } else if (error.isEmpty()) {
            m_exportPhase = "configuring";
        } else {
            m_exportError = error;
            m_exportPhase = "failed";
        }
        emit exportStateChanged();
    });

    qApp->installEventFilter(this);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &EditorModel::saveNow);
    rebuildModels();
    refreshLibrary();
}

EditorModel::~EditorModel()
{
    if (g_instance == this)
        g_instance = nullptr;
}

EditorModel *EditorSingleton::create(QQmlEngine *, QJSEngine *engine)
{
    EditorModel *model = EditorModel::instance();
    Q_ASSERT(model);
    // O main é o dono; o motor não pode apagar o objeto.
    engine->setObjectOwnership(model, QJSEngine::CppOwnership);
    return model;
}

EditorModel *EditorModel::instance()
{
    return g_instance;
}

// MARK: - Alterações e desfazer

void EditorModel::mutate(const std::function<void(Project &)> &change, const QString &coalesce)
{
    Project copy = m_project;
    change(copy);
    if (copy == m_project)
        return;
    const bool merges = !coalesce.isEmpty() && coalesce == m_lastCoalesceKey && m_lastCoalesce.elapsed() < 1000;
    if (!merges)
        pushUndo(m_project);
    m_lastCoalesceKey = coalesce;
    m_lastCoalesce.restart();
    setProject(copy);
}

void EditorModel::pushUndo(const Project &snapshot)
{
    m_undo.append(snapshot);
    if (m_undo.size() > 300)
        m_undo.removeFirst();
    m_redo.clear();
}

void EditorModel::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_project);
    m_lastCoalesceKey.clear();
    setProject(m_undo.takeLast());
}

void EditorModel::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_project);
    m_lastCoalesceKey.clear();
    setProject(m_redo.takeLast());
}

void EditorModel::setProject(const Project &p)
{
    m_project = p;
    m_dirty = true;
    scheduleAutosave();
    if (m_selection && !m_project.segment(*m_selection))
        m_selection.reset();
    if (m_playhead > m_project.duration())
        setPlayheadMicros(m_project.duration());
    projectDidChange();
}

// Gestos (arrastar clipes): cada atualização parte do estado do início do
// gesto; o desfazer é registrado uma vez, ao soltar.

void EditorModel::beginGesture()
{
    if (!m_gestureBase)
        m_gestureBase = m_project;
}

void EditorModel::updateGesture(const std::function<void(Project &)> &change)
{
    if (!m_gestureBase)
        return;
    Project copy = *m_gestureBase;
    change(copy);
    if (copy == m_project)
        return;
    m_project = copy;
    m_dirty = true;
    scheduleAutosave();
    projectDidChange();
}

void EditorModel::updateGestureInPlace(const std::function<void(Project &)> &change)
{
    Project copy = m_project;
    change(copy);
    if (copy == m_project)
        return;
    m_project = copy;
    m_dirty = true;
    scheduleAutosave();
    projectDidChange();
}

void EditorModel::endGesture()
{
    if (m_gestureBase && *m_gestureBase != m_project) {
        pushUndo(*m_gestureBase);
        m_lastCoalesceKey.clear();
    }
    m_gestureBase.reset();
    emit projectChanged();
}

void EditorModel::projectDidChange()
{
    m_preview->setProject(m_project);
    rebuildModels();
    emit projectChanged();
}

// MARK: - Listas para o QML

QList<int> EditorModel::displayTrackOrder() const
{
    // De cima para baixo: textos, sobreposições, principal, áudios.
    const auto &t = m_project.tracks;
    QList<int> texts, overlays, main, audios;
    for (int i = 0; i < t.size(); ++i) {
        if (t[i].kind == TrackKind::Text)
            texts.prepend(i);
        else if (t[i].kind == TrackKind::Video && !t[i].isMain)
            overlays.prepend(i);
        else if (t[i].isMain)
            main.append(i);
        else if (t[i].kind == TrackKind::Audio)
            audios.append(i);
    }
    return texts + overlays + main + audios;
}

void EditorModel::rebuildModels()
{
    const auto &p = m_project;
    const QList<int> order = displayTrackOrder();

    QHash<QUuid, int> usage;
    for (const auto &track : p.tracks)
        for (const auto &seg : track.segments)
            if (seg.materialId)
                usage[*seg.materialId] += 1;

    QList<QVariantMap> materials;
    for (const auto &m : p.materials) {
        materials.append({{"id", idString(m.id)},
                          {"name", m.name},
                          {"kind", enumKey(m.kind)},
                          {"durationText", m.kind == MaterialKind::Photo ? QString() : Time::format(m.duration)},
                          {"used", usage.value(m.id) > 0},
                          {"isVisual", m.isVisual()}});
    }
    m_materials->setItems(materials);

    QList<QVariantMap> tracks, clips, junctions, cues;
    const std::optional<QUuid> dragged = m_dragPreview ? std::optional(m_dragPreview->segment) : std::nullopt;
    for (int row = 0; row < order.size(); ++row) {
        const Track &track = p.tracks[order[row]];
        QString icon = track.kind == TrackKind::Audio ? "audio-waveform"
                     : track.kind == TrackKind::Text  ? "type"
                     : track.isMain                   ? "film"
                                                      : "layers";
        tracks.append({{"id", idString(track.id)},
                       {"kind", enumKey(track.kind)},
                       {"isMain", track.isMain},
                       {"muted", track.muted},
                       {"hidden", track.hidden},
                       {"row", row},
                       {"icon", icon}});
        int draggedIndex = -1;
        for (int i = 0; i < track.segments.size(); ++i)
            if (dragged && track.segments[i].id == *dragged)
                draggedIndex = i;

        for (int i = 0; i < track.segments.size(); ++i) {
            const Segment &seg = track.segments[i];
            const Material *m = p.material(seg.materialId);
            QString kind = seg.text ? "text" : m ? enumKey(m->kind) : "missing";
            QString clipIcon = seg.text ? "type"
                             : !m      ? "film"
                             : m->kind == MaterialKind::Audio ? "music"
                             : m->kind == MaterialKind::Photo ? "image"
                             : track.isMain                   ? "film"
                                                              : "layers";
            QString title = seg.text ? QString(seg.text->text).replace('\n', ' ') : m ? m->name : QString();
            QStringList badges;
            if (std::abs(seg.speed - 1) > 0.001)
                badges << QString::number(seg.speed, 'g', 2) + "x";
            if (seg.filter && seg.filter->preset != FilterPreset::None)
                badges << displayName(seg.filter->preset);
            if (!seg.effects.isEmpty())
                badges << QStringLiteral("✦%1").arg(seg.effects.size());
            if (!seg.adjust.isIdentity())
                badges << QStringLiteral("◐");
            if (seg.animationIn || seg.animationOut)
                badges << QStringLiteral("↻");

            // Início desenhado: o clipe do gesto segue o cursor; na faixa principal,
            // ao aparar o início, os seguintes ficam onde estavam até soltar.
            Micros start = seg.targetStart;
            int rowShift = 0;
            const bool isDragged = dragged && seg.id == *dragged;
            if (isDragged) {
                start = m_dragPreview->start;
                rowShift = m_dragPreview->rows;
            } else if (m_dragPreview && m_dragPreview->following != 0 && track.isMain && draggedIndex >= 0
                       && i > draggedIndex) {
                start += m_dragPreview->following;
            }
            clips.append({{"id", idString(seg.id)},
                          {"row", row},
                          {"start", Time::toSeconds(start)},
                          {"duration", Time::toSeconds(seg.targetDuration())},
                          {"kind", kind},
                          {"icon", clipIcon},
                          {"title", title},
                          {"badges", badges.join("  ")},
                          {"materialId", m ? idString(m->id) : QString()},
                          {"sourceStart", Time::toSeconds(seg.source.start)},
                          {"sourceDuration", Time::toSeconds(seg.source.duration)},
                          {"materialDuration", m ? Time::toSeconds(m->duration) : 0.0},
                          {"aspect", m && m->height > 0 ? double(m->width) / m->height : 16.0 / 9},
                          {"volume", seg.volume},
                          {"isMain", track.isMain},
                          {"dragged", isDragged},
                          {"rowShift", rowShift},
                          {"hidden", track.hidden},
                          {"hasAudio", m && m->kind == MaterialKind::Video && m->hasAudio},
                          {"fullText", seg.text ? seg.text->text : QString()},
                          {"durationText", shortSeconds(seg.targetDuration())},
                          {"autoCaption", track.isAutoCaptions}});
            if (seg.text)
                cues.append({{"id", idString(seg.id)},
                             {"start", Time::toSeconds(seg.targetStart)},
                             {"startText", Time::format(seg.targetStart)},
                             {"caption", QString(seg.text->text).replace(QLatin1Char(10), QLatin1Char(32))}});
        }
        if (track.isMain && draggedIndex < 0) {
            for (int i = 0; i + 1 < track.segments.size(); ++i) {
                const Segment &seg = track.segments[i];
                const Segment &next = track.segments[i + 1];
                junctions.append({{"id", idString(seg.id)},
                                  {"time", Time::toSeconds(seg.targetEnd() + next.targetStart) / 2},
                                  {"hasTransition", seg.transition.has_value()},
                                  {"nextId", idString(next.id)},
                                  {"help", seg.transition ? "Transição: " + displayName(seg.transition->kind)
                                                          : QStringLiteral("Adicionar transição")}});
            }
        }
    }
    std::sort(cues.begin(), cues.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("start").toDouble() < b.value("start").toDouble();
    });
    updateRowLayout();
    m_tracks->setItems(tracks);
    m_clips->setItems(clips);
    m_junctions->setItems(junctions);
    m_cues->setItems(cues);
    refreshSelection();
    emit zoomChanged(); // largura do conteúdo e zoom mínimo dependem da duração
}

void EditorModel::refreshSelection()
{
    QVariantMap map;
    if (m_selection) {
        if (const auto seg = m_project.segment(*m_selection))
            map = segmentMap(*seg);
    }
    if (map != m_selectedSegment) {
        m_selectedSegment = map;
        ensureValidTab();
        emit selectionChanged();
    }
}

QVariantMap EditorModel::segmentMap(const Segment &seg) const
{
    QVariantMap map = Bridge::segmentJson(seg).toVariantMap();
    const Material *m = m_project.material(seg.materialId);
    map["id"] = idString(seg.id);
    map["kind"] = seg.text ? "text" : m ? enumKey(m->kind) : "missing";
    map["icon"] = seg.text ? "type" : !m ? "film" : m->kind == MaterialKind::Audio ? "music"
                                    : m->kind == MaterialKind::Photo               ? "image"
                                                                                   : "film";
    map["title"] = seg.text ? QString(seg.text->text).replace('\n', ' ') : m ? m->name : QStringLiteral("Clipe");
    map["rangeText"] = Time::format(seg.targetStart) + " – " + Time::format(seg.targetEnd()) + " · "
                     + shortSeconds(seg.targetDuration());
    map["startText"] = Time::format(seg.targetStart);
    map["endText"] = Time::format(seg.targetEnd());
    map["durationText"] = shortSeconds(seg.targetDuration());
    map["hasAudio"] = m && m->hasAudio;
    map["isText"] = seg.text.has_value();
    map["fadeInSeconds"] = Time::toSeconds(seg.fadeIn);
    map["fadeOutSeconds"] = Time::toSeconds(seg.fadeOut);
    map["start"] = Time::toSeconds(seg.targetStart);
    map["end"] = Time::toSeconds(seg.targetEnd());
    if (seg.text) {
        const QColor color = seg.text->style.color.color();
        map["textColor"] = color;
    }
    if (seg.text) {
        map["wordList"] = TextContent::wordsOf(seg.text->text);
        map["highlightActive"] = seg.text->activeHighlight().has_value();
        map["isAutoCaption"] = CaptionsCore::isAutoCaption(m_project, seg.id);
    }
    if (const auto loop = MotionCore::loop(m_project, seg.id))
        map["loop"] = QVariantMap{{"kind", loop->kind},
                                  {"periodSeconds", Time::toSeconds(loop->period)},
                                  {"intensity", loop->intensity}};
    return map;
}

// MARK: - Seleção e inspetor

QString EditorModel::selection() const
{
    return m_selection ? idString(*m_selection) : QString();
}

void EditorModel::setSelection(const QString &id)
{
    const auto parsed = parseId(id);
    if (parsed == m_selection)
        return;
    m_selection = parsed && m_project.segment(*parsed) ? parsed : std::nullopt;
    m_selectedSegment.clear();
    refreshSelection();
    emit selectionChanged();
}

int EditorModel::selectedRow() const
{
    if (!m_selection)
        return -1;
    const auto loc = m_project.locate(*m_selection);
    return loc ? int(displayTrackOrder().indexOf(loc->track)) : -1;
}

bool EditorModel::isOnMainTrack(const QUuid &id) const
{
    const auto loc = m_project.locate(id);
    return loc && m_project.tracks[loc->track].isMain;
}

bool EditorModel::isLastOnMainTrack(const QUuid &id) const
{
    const auto main = m_project.mainTrackIndex();
    if (!main)
        return true;
    const auto &segs = m_project.tracks[*main].segments;
    return segs.isEmpty() || segs.last().id == id;
}

QStringList EditorModel::availableTabKeys(const Segment &seg) const
{
    if (seg.text)
        return {"text", "animation"};
    const Material *m = m_project.material(seg.materialId);
    if (!m)
        return {};
    if (m->kind == MaterialKind::Audio)
        return {"audio"};
    QStringList tabs{"basic", "adjust", "filters", "effects", "animation"};
    if (isOnMainTrack(seg.id) && !isLastOnMainTrack(seg.id))
        tabs << "transition";
    return tabs;
}

QVariantList EditorModel::availableTabs() const
{
    QVariantList list;
    if (!m_selection)
        return list;
    const auto seg = m_project.segment(*m_selection);
    if (!seg)
        return list;
    for (const auto &key : availableTabKeys(*seg)) {
        const int i = int(tabKeys.indexOf(key));
        list.append(QVariantMap{{"value", key}, {"title", tabTitles[i]}, {"icon", tabIcons[i]}});
    }
    return list;
}

void EditorModel::ensureValidTab()
{
    if (!m_selection)
        return;
    const auto seg = m_project.segment(*m_selection);
    if (!seg)
        return;
    const QStringList tabs = availableTabKeys(*seg);
    if (!tabs.contains(m_inspectorTab) && !tabs.isEmpty()) {
        m_inspectorTab = tabs.first();
        emit uiChanged();
    }
}

void EditorModel::setInspectorTab(const QString &tab)
{
    if (tab == m_inspectorTab)
        return;
    m_inspectorTab = tab;
    ensureValidTab();
    emit uiChanged();
}

void EditorModel::setLibraryPanel(const QString &panel)
{
    if (panel == m_libraryPanel)
        return;
    m_libraryPanel = panel;
    emit uiChanged();
}

void EditorModel::setLibraryVisible(bool visible)
{
    if (visible == m_libraryVisible)
        return;
    m_libraryVisible = visible;
    emit uiChanged();
}

void EditorModel::setShowGrid(bool show)
{
    if (show == m_showGrid)
        return;
    m_showGrid = show;
    emit uiChanged();
}

void EditorModel::setShowExport(bool show)
{
    if (show == m_showExport)
        return;
    if (show && m_playing)
        togglePlay();
    if (show && m_exportPhase != "exporting")
        resetExport();
    m_showExport = show;
    emit uiChanged();
}

void EditorModel::setErrorMessage(const QString &message)
{
    if (message == m_errorMessage)
        return;
    m_errorMessage = message;
    emit uiChanged();
}

void EditorModel::setModalCount(int count)
{
    count = std::max(0, count);
    if (count == m_modalCount)
        return;
    m_modalCount = count;
    emit uiChanged();
}

// MARK: - Projeto (leitura)

QVariantList EditorModel::aspectOptions() const
{
    QVariantList list;
    for (auto a : allCases<AspectRatio>())
        list.append(QVariantMap{{"value", enumKey(a)}, {"title", label(a)}, {"ratio", ratio(a).value_or(0)}});
    return list;
}

int EditorModel::clipCount() const
{
    int n = 0;
    for (const auto &t : m_project.tracks)
        n += int(t.segments.size());
    return n;
}

QVariantMap EditorModel::canvas() const
{
    const auto &bg = m_project.canvas.background;
    QVariantMap map{{"aspect", enumKey(m_project.canvas.aspect)}};
    if (const RGBA *c = bg.colorValue()) {
        map["background"] = "color";
        map["color"] = c->color();
    } else if (const auto blur = bg.blurIntensity()) {
        map["background"] = "blur";
        map["blur"] = *blur;
    } else {
        map["background"] = "black";
    }
    return map;
}

// MARK: - Biblioteca e telas

void EditorModel::refreshLibrary()
{
    m_entryList = m_library.list();
    rebuildEntries();
    fillMissingThumbnails();
}

void EditorModel::rebuildEntries()
{
    QList<QVariantMap> items;
    for (const auto &e : m_entryList) {
        QString thumbnail;
        if (e.thumbnailModified)
            thumbnail = QUrl::fromLocalFile(e.thumbnailPath()).toString() + "?v="
                      + QString::number(e.thumbnailModified->toMSecsSinceEpoch());
        QString duration = e.duration > 0 ? Time::format(e.duration).section('.', 0, 0) : QString();
        items.append({{"id", idString(e.id)},
                      {"name", e.name},
                      {"subtitle", "Editado " + relativeDate(e.modified) + " · " + label(e.aspect)},
                      {"durationText", duration},
                      {"aspectValue", e.aspectValue},
                      {"thumbnail", thumbnail}});
    }
    m_entries->setItems(items);
}

/// Projetos sem miniatura ganham uma em segundo plano, um por vez.
void EditorModel::fillMissingThumbnails()
{
    if (m_screen != "home")
        return;
    for (const auto &e : m_entryList) {
        if (e.thumbnailModified || e.duration <= 0 || m_thumbnailAttempts.contains(e.id))
            continue;
        m_thumbnailAttempts.insert(e.id);
        if (const auto project = m_library.load(e.id))
            writeThumbnail(*project, e.id);
    }
}

/// Quadro do projeto (a 1/3 da duração, até 2 s) gravado como miniatura da tela inicial.
void EditorModel::writeThumbnail(const Project &snapshot, const QUuid &id)
{
    if (snapshot.duration() <= 0)
        return;
    QPointer<EditorModel> self(this);
    const ProjectLibrary library = m_library;
    auto future = QtConcurrent::run([snapshot, id, library] {
        const QByteArray data = Bridge::projectThumbnail(snapshot);
        return !data.isEmpty() && library.writeThumbnail(data, id);
    });
    future.then(this, [self](bool written) {
        if (self && written && self->m_screen == "home")
            self->refreshLibrary();
    });
}

void EditorModel::createProject(const QString &aspect)
{
    QString error;
    const auto created = m_library.create(enumFromKey<AspectRatio>(aspect).value_or(AspectRatio::Original), &error);
    if (!created) {
        setErrorMessage("Não foi possível criar o projeto: " + error);
        return;
    }
    load(created->project, created->entry.id, {});
}

void EditorModel::openLibraryProject(const QString &id)
{
    const auto uuid = parseId(id);
    QString error;
    const auto project = uuid ? m_library.load(*uuid, &error) : std::nullopt;
    if (!project) {
        setErrorMessage("Não foi possível abrir o projeto: " + error);
        refreshLibrary();
        return;
    }
    load(*project, uuid, {});
}

void EditorModel::showHome()
{
    if (m_playing)
        togglePlay();
    saveNow();
    const auto id = m_libraryId;
    const Project snapshot = m_project;
    m_preview->clear();
    m_libraryId.reset();
    m_documentPath.clear();
    m_dirty = false;
    m_selection.reset();
    m_showExport = false;
    m_screen = "home";
    emit screenChanged();
    emit uiChanged();
    refreshLibrary();
    if (id)
        writeThumbnail(snapshot, *id);
}

void EditorModel::renameProject(const QString &id, const QString &name)
{
    QString error;
    const auto uuid = parseId(id);
    if (uuid && !m_library.rename(*uuid, name, &error))
        setErrorMessage("Não foi possível renomear: " + error);
    refreshLibrary();
}

void EditorModel::renameCurrentProject(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_project.name)
        return;
    m_project.name = trimmed;
    m_dirty = true;
    scheduleAutosave();
    emit projectChanged();
}

void EditorModel::setProjectName(const QString &name)
{
    if (name == m_project.name)
        return;
    mutate([&](Project &p) { p.name = name; }, "name");
}

void EditorModel::duplicateProject(const QString &id)
{
    QString error;
    const auto uuid = parseId(id);
    if (uuid && !m_library.duplicate(*uuid, &error))
        setErrorMessage("Não foi possível duplicar: " + error);
    refreshLibrary();
}

void EditorModel::deleteProject(const QString &id)
{
    QString error;
    const auto uuid = parseId(id);
    if (uuid && !m_library.remove(*uuid, &error))
        setErrorMessage("Não foi possível apagar: " + error);
    refreshLibrary();
}

void EditorModel::revealProject(const QString &id)
{
    if (const auto uuid = parseId(id))
        revealFile(QDir(m_library.folder(*uuid)).filePath(ProjectLibrary::projectFile));
}

void EditorModel::newProject()
{
    saveNow();
    createProject();
}

void EditorModel::openProjectFile(const QUrl &url)
{
    saveNow();
    QString error;
    const auto entry = m_library.importProject(localPath(url), &error);
    if (!entry) {
        setErrorMessage("Não foi possível abrir o projeto: " + error);
        return;
    }
    openLibraryProject(idString(entry->id));
}

void EditorModel::openExternal(const QString &path)
{
    QString error;
    const auto project = ProjectStore::load(path, &error);
    if (!project) {
        setErrorMessage("Não foi possível abrir o projeto: " + error);
        return;
    }
    load(*project, std::nullopt, path);
}

void EditorModel::load(const Project &loaded, std::optional<QUuid> libraryId, const QString &documentPath)
{
    if (m_playing)
        togglePlay();
    m_autosave.stop();
    m_project = loaded;
    m_undo.clear();
    m_redo.clear();
    m_selection.reset();
    m_rowHeights.clear();
    m_libraryId = libraryId;
    m_documentPath = documentPath;
    m_dirty = false;
    m_playhead = 0;
    m_scrollX = 0;
    MediaCache::instance()->clear();
    for (const auto &m : loaded.materials)
        loadMediaPreviews(m);
    m_screen = "editor";
    m_lastThumbnail = QDateTime::currentDateTime();
    m_preview->setProject(m_project);
    m_preview->seek(0);
    rebuildModels();
    emit screenChanged();
    emit projectChanged();
    emit playheadChanged();
    emit scrollChanged();
    QStringList missing;
    for (const auto &m : loaded.materials)
        if (!QFileInfo::exists(m.path))
            missing << m.path;
    if (!missing.isEmpty())
        setErrorMessage("Arquivos não encontrados:\n" + missing.join('\n'));
}

// MARK: - Salvar

void EditorModel::scheduleAutosave()
{
    if (m_libraryId)
        m_autosave.start();
}

/// Grava já o projeto da biblioteca; a miniatura acompanha no máximo a cada 10 s.
void EditorModel::saveNow()
{
    m_autosave.stop();
    if (!m_libraryId || !m_dirty)
        return;
    QString error;
    if (!m_library.save(m_project, *m_libraryId, &error)) {
        setErrorMessage("Não foi possível salvar: " + error);
        return;
    }
    m_dirty = false;
    emit projectChanged();
    if (m_lastThumbnail.secsTo(QDateTime::currentDateTime()) > 10) {
        m_lastThumbnail = QDateTime::currentDateTime();
        writeThumbnail(m_project, *m_libraryId);
    }
}

void EditorModel::saveProject()
{
    if (m_libraryId) {
        saveNow();
        return;
    }
    if (m_documentPath.isEmpty())
        return;
    QString error;
    if (!ProjectStore::save(m_project, m_documentPath, &error)) {
        setErrorMessage("Não foi possível salvar: " + error);
        return;
    }
    m_dirty = false;
    emit projectChanged();
}

void EditorModel::exportProjectFile(const QUrl &url)
{
    QString path = localPath(url);
    if (QFileInfo(path).suffix().isEmpty())
        path += "." + ProjectStore::fileExtension;
    Project p = m_project;
    if (!m_libraryId)
        p.name = QFileInfo(path).completeBaseName();
    QString error;
    if (!ProjectStore::save(p, path, &error)) {
        setErrorMessage("Não foi possível exportar o projeto: " + error);
        return;
    }
    if (!m_libraryId) {
        m_project = p;
        m_documentPath = path;
        m_dirty = false;
        emit projectChanged();
    }
}

QString EditorModel::suggestedFileName(const QString &extension) const
{
    QString name = m_project.name;
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(invalid, "-");
    return name + "." + extension;
}

// MARK: - Mídia

bool EditorModel::isSupportedMedia(const QUrl &url) const
{
    return Bridge::isSupportedMedia(localPath(url));
}

void EditorModel::importMedia(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const auto &url : urls)
        paths << localPath(url);
    QPointer<EditorModel> self(this);
    auto future = QtConcurrent::run([paths] {
        QList<Material> found;
        QStringList failures;
        for (const auto &path : paths) {
            QString error;
            if (const auto m = Bridge::probe(path, &error))
                found.append(*m);
            else
                failures << error;
        }
        return std::make_pair(found, failures);
    });
    future.then(this, [self](const std::pair<QList<Material>, QStringList> &result) {
        if (!self)
            return;
        for (const auto &m : result.first) {
            bool exists = false;
            for (const auto &existing : self->m_project.materials)
                exists = exists || existing.path == m.path;
            if (exists)
                continue;
            self->mutate([&](Project &p) { p.materials.append(m); });
            self->loadMediaPreviews(m);
        }
        if (!result.second.isEmpty())
            self->setErrorMessage(result.second.join('\n'));
    });
}

void EditorModel::loadMediaPreviews(const Material &m)
{
    const QString id = idString(m.id);
    auto *cache = MediaCache::instance();
    if (cache->hasThumbnails(id))
        return;
    QPointer<EditorModel> self(this);
    if (m.hasAudio) {
        QtConcurrent::run([m] { return Bridge::peaks(m); }).then(this, [self, id](const QVector<float> &peaks) {
            if (self && !peaks.isEmpty())
                MediaCache::instance()->setPeaks(id, peaks);
        });
    }
    QtConcurrent::run([m] { return Bridge::thumbnails(m); }).then(this, [self, id](const QList<QImage> &images) {
        if (self)
            MediaCache::instance()->setThumbnails(id, images);
    });
}

void EditorModel::addToTimeline(const QString &materialId)
{
    const auto id = parseId(materialId);
    const Material *m = id ? m_project.material(*id) : nullptr;
    if (!m)
        return;
    const Material material = *m;
    QUuid added;
    mutate([&](Project &p) { added = TimelineEditor::addMedia(material, p, m_playhead); });
    setSelection(idString(added));
}

void EditorModel::addAsOverlay(const QString &materialId)
{
    const auto id = parseId(materialId);
    const Material *m = id ? m_project.material(*id) : nullptr;
    if (!m)
        return;
    const Material material = *m;
    QUuid added;
    mutate([&](Project &p) { added = TimelineEditor::addOverlay(material, p, m_playhead); });
    setSelection(idString(added));
}

void EditorModel::removeMaterial(const QString &materialId)
{
    const auto id = parseId(materialId);
    if (!id)
        return;
    mutate([&](Project &p) {
        QList<QUuid> ids;
        for (const auto &t : p.tracks)
            for (const auto &s : t.segments)
                if (s.materialId == id)
                    ids << s.id;
        for (const auto &segment : ids)
            TimelineEditor::remove(p, segment);
        p.materials.removeIf([&](const Material &m) { return m.id == *id; });
    });
    MediaCache::instance()->remove(materialId);
}

std::optional<int> EditorModel::trackIndex(int row) const
{
    const QList<int> order = displayTrackOrder();
    if (row < 0 || row >= order.size())
        return std::nullopt;
    return order[row];
}

bool EditorModel::isRowFree(int row, qreal from, qreal to) const
{
    const QList<int> order = displayTrackOrder();
    if (row < 0)
        return false;
    if (row >= order.size())
        return true;
    const Micros a = Time::seconds(from), b = Time::seconds(to);
    for (const Segment &s : m_project.tracks[order[row]].segments)
        if (s.targetStart < b && s.targetEnd() > a)
            return false;
    return true;
}

QVariantMap EditorModel::dropPreview(const QString &materialId, qreal time, int row) const
{
    const auto id = parseId(materialId);
    const Material *m = id ? m_project.material(*id) : nullptr;
    const Micros t = Time::seconds(std::max<qreal>(0, time));
    if (!m)
        return {{"row", -1}, {"start", Time::toSeconds(t)}, {"duration", 0}};
    Project p = m_project;
    const QUuid added = TimelineEditor::insertMedia(*m, p, t, trackIndex(row));
    const auto loc = p.locate(added);
    const auto seg = p.segment(added);
    if (!loc || !seg)
        return {{"row", -1}, {"start", Time::toSeconds(t)}, {"duration", 0}};
    // Uma faixa criada para o material não existe no projeto atual.
    const QUuid trackId = p.tracks[loc->track].id;
    int displayRow = -1;
    for (int i = 0; i < m_project.tracks.size(); ++i)
        if (m_project.tracks[i].id == trackId)
            displayRow = int(displayTrackOrder().indexOf(i));
    return {{"row", displayRow},
            {"start", Time::toSeconds(seg->targetStart)},
            {"duration", Time::toSeconds(seg->targetDuration())}};
}

void EditorModel::dropMaterial(const QString &materialId, qreal time, int row)
{
    const auto id = parseId(materialId);
    const Material *m = id ? m_project.material(*id) : nullptr;
    setDraggingMaterial({});
    if (!m)
        return;
    const Material material = *m;
    const auto track = trackIndex(row);
    QUuid added;
    mutate([&](Project &p) {
        added = TimelineEditor::insertMedia(material, p, Time::seconds(std::max<qreal>(0, time)), track);
    });
    setSelection(idString(added));
}

void EditorModel::setDraggingMaterial(const QString &id)
{
    if (id == m_draggingMaterial)
        return;
    m_draggingMaterial = id;
    emit dragChanged();
}

// MARK: - Edição

std::optional<QUuid> EditorModel::targetSegmentId() const
{
    if (m_selection)
        return m_selection;
    const auto main = m_project.mainTrackIndex();
    if (!main)
        return std::nullopt;
    for (const auto &s : m_project.tracks[*main].segments)
        if (s.targetRange().contains(m_playhead))
            return s.id;
    return std::nullopt;
}

bool EditorModel::canSplitSelection() const
{
    if (!m_selection)
        return false;
    const auto seg = m_project.segment(*m_selection);
    return seg && seg->targetRange().contains(m_playhead) && m_playhead > seg->targetStart;
}

void EditorModel::splitAtPlayhead()
{
    const auto id = targetSegmentId();
    if (!id)
        return;
    std::optional<QUuid> right;
    mutate([&](Project &p) { right = TimelineEditor::split(p, *id, m_playhead); });
    if (right)
        setSelection(idString(*right));
}

void EditorModel::deleteSelection()
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) { TimelineEditor::remove(p, id); });
    setSelection({});
}

void EditorModel::duplicateSelection()
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    std::optional<QUuid> copy;
    mutate([&](Project &p) { copy = TimelineEditor::duplicate(p, id); });
    if (copy)
        setSelection(idString(*copy));
}

void EditorModel::addText(const QString &preset)
{
    TextStyle style;
    if (!preset.isEmpty())
        style = TextPresets::applied(preset, TextStyle());
    QUuid added;
    mutate([&](Project &p) {
        added = TimelineEditor::addText(TextContent{QStringLiteral("Digite seu texto"), style}, p, m_playhead);
    });
    setSelection(idString(added));
    setInspectorTab("text");
}

void EditorModel::importSubtitles(const QUrl &url)
{
    QFile file(localPath(url));
    if (!file.open(QIODevice::ReadOnly)) {
        setErrorMessage(file.errorString());
        return;
    }
    const auto cues = SRT::parse(QString::fromUtf8(file.readAll()));
    if (cues.isEmpty()) {
        setErrorMessage("Nenhuma legenda encontrada em " + QFileInfo(file).fileName() + ".");
        return;
    }
    TextStyle style;
    style.fontSize = 0.055;
    style.strokeWidth = 3;
    mutate([&](Project &p) { SRT::importCues(cues, p, style); });
}

void EditorModel::exportSubtitles(const QUrl &url)
{
    const auto cues = SRT::cues(m_project);
    if (cues.isEmpty()) {
        setErrorMessage("Não há textos na timeline para exportar como legenda.");
        return;
    }
    QFile file(localPath(url));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErrorMessage(file.errorString());
        return;
    }
    file.write(SRT::serialize(cues).toUtf8());
}

void EditorModel::toggleTrack(const QString &trackId, const QString &flag)
{
    const auto id = parseId(trackId);
    mutate([&](Project &p) {
        for (auto &t : p.tracks) {
            if (t.id != id)
                continue;
            if (flag == "muted")
                t.muted = !t.muted;
            else
                t.hidden = !t.hidden;
        }
    });
}

void EditorModel::setAspect(const QString &key)
{
    const auto aspect = enumFromKey<AspectRatio>(key);
    if (aspect)
        mutate([&](Project &p) { p.canvas.aspect = *aspect; }, "aspect");
}

void EditorModel::setCanvasBackground(const QString &kind)
{
    mutate([&](Project &p) {
        if (kind == "color")
            p.canvas.background = CanvasBackground::color(RGBA{0.1, 0.1, 0.12, 1});
        else if (kind == "blur")
            p.canvas.background = CanvasBackground::blur(0.5);
        else
            p.canvas.background = CanvasBackground::black();
    });
}

void EditorModel::setCanvasColor(const QColor &c)
{
    mutate([&](Project &p) { p.canvas.background = CanvasBackground::color(RGBA{c.redF(), c.greenF(), c.blueF(), c.alphaF()}); },
           "bgcolor");
}

void EditorModel::setCanvasBlur(qreal amount)
{
    mutate([&](Project &p) { p.canvas.background = CanvasBackground::blur(amount); }, "bgblur");
}

void EditorModel::setSegmentValue(const QString &path, const QVariant &value)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto seg = m_project.segment(id);
    if (!seg)
        return;
    QJsonValue json;
    if (value.metaType().id() == QMetaType::QColor) {
        const QColor c = value.value<QColor>();
        json = QJsonObject{{"r", c.redF()}, {"g", c.greenF()}, {"b", c.blueF()}, {"a", c.alphaF()}};
    } else if (value.isValid() && !value.isNull()) {
        json = QJsonValue::fromVariant(value);
    }
    // Tempos chegam do QML em segundos quando o caminho termina em "Seconds".
    QString key = path;
    if (key.endsWith("Seconds")) {
        key.chop(int(strlen("Seconds")));
        json = double(Time::seconds(value.toDouble()));
    }
    const QJsonObject edited = setPath(Bridge::segmentJson(*seg), key.split('.'), 0, json);
    const auto decoded = Bridge::segmentFromJson(edited);
    if (!decoded)
        return;
    const Segment updated = *decoded;
    mutate([&](Project &p) { TimelineEditor::update(p, id, [&](Segment &s) { s = updated; }); },
           idString(id) + key);
}

void EditorModel::resetSegment(const QString &group)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (group == "transform") {
                s.transform = Transform();
                s.opacity = 1;
            } else if (group == "adjust") {
                s.adjust = Adjust();
            } else if (group == "effects") {
                s.effects.clear();
            } else if (group == "textSpacing" && s.text) {
                s.text->style.letterSpacing = 0;
                s.text->style.lineSpacing = 0;
            } else if (group == "textPosition" && s.text) {
                s.text->style.x = 0;
                s.text->style.y = -0.7;
                s.text->style.scale = 1;
                s.text->style.rotation = 0;
            }
        });
    });
}

void EditorModel::setSpeed(qreal speed)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) { TimelineEditor::setSpeed(p, id, speed); }, idString(id) + "speed");
}

void EditorModel::setFilter(const QString &preset)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto kind = enumFromKey<FilterPreset>(preset).value_or(FilterPreset::None);
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (kind == FilterPreset::None)
                s.filter.reset();
            else
                s.filter = FilterSetting{kind, s.filter ? s.filter->intensity : 1.0};
        });
    });
}

void EditorModel::toggleEffect(const QString &key)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto kind = enumFromKey<VideoEffectKind>(key);
    if (!kind)
        return;
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            for (int i = 0; i < s.effects.size(); ++i) {
                if (s.effects[i].kind == *kind) {
                    s.effects.removeAt(i);
                    return;
                }
            }
            VideoEffect effect;
            effect.kind = *kind;
            s.effects.append(effect);
        });
    });
}

void EditorModel::setAnimation(const QString &which, const QString &key)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto kind = enumFromKey<AnimationKind>(key);
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            auto &anim = which == "out" ? s.animationOut : s.animationIn;
            if (!kind)
                anim.reset();
            else
                anim = SegmentAnimation{*kind, anim ? anim->duration : 500'000};
        });
    });
    // Toca o trecho do efeito: a entrada com um respiro de 0,3 s, a saída até o fim.
    const auto seg = m_project.segment(id);
    const auto &anim = seg ? (which == "out" ? seg->animationOut : seg->animationIn) : std::nullopt;
    if (!seg || !anim)
        return;
    if (which == "out")
        playExcerpt(std::max(seg->targetStart, seg->targetEnd() - anim->duration), seg->targetEnd());
    else
        playExcerpt(seg->targetStart, std::min(seg->targetEnd(), seg->targetStart + anim->duration + 300'000));
}

void EditorModel::setTransition(const QString &key)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const auto kind = enumFromKey<TransitionKind>(key);
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (!kind)
                s.transition.reset();
            else
                s.transition = Transition{*kind, s.transition ? s.transition->duration : 500'000};
        });
    });
}

void EditorModel::applyTextPreset(const QString &preset)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (s.text)
                s.text->style = TextPresets::applied(preset, s.text->style);
        });
    });
}

void EditorModel::setFontFamily(const QString &family)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    const QStringList faces = fontFaces(family);
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (!s.text)
                return;
            s.text->style.fontName = family;
            if (!faces.contains(s.text->style.fontFace))
                s.text->style.fontFace.clear();
        });
    });
}

void EditorModel::setFontFace(const QString &face)
{
    if (!m_selection)
        return;
    const QUuid id = *m_selection;
    // Um estilo da família ("Light", "Medium"…) substitui o negrito/itálico ligados.
    mutate([&](Project &p) {
        TimelineEditor::update(p, id, [&](Segment &s) {
            if (!s.text)
                return;
            s.text->style.fontFace = face;
            if (!face.isEmpty()) {
                s.text->style.bold = false;
                s.text->style.italic = false;
            }
        });
    });
}

QStringList EditorModel::fontFamilies() const
{
    static const QStringList families = FontCatalog::families();
    return families;
}

QStringList EditorModel::suggestedFontFamilies() const
{
    // Famílias comuns no Windows e no Linux, à mão no topo do menu.
    static const QStringList suggested{"Inter", "Arial", "Helvetica Neue", "Segoe UI", "Verdana", "Tahoma",
                                       "Trebuchet MS", "Impact", "Georgia", "Times New Roman", "Courier New",
                                       "Comic Sans MS", "DejaVu Sans", "Liberation Sans", "Noto Sans",
                                       "Ubuntu", "Cantarell"};
    QStringList available;
    const QStringList all = fontFamilies();
    for (const auto &f : suggested)
        if (all.contains(f))
            available << f;
    return available;
}

QStringList EditorModel::fontFaces(const QString &family) const
{
    static QHash<QString, QStringList> cache;
    if (!cache.contains(family))
        cache.insert(family, FontCatalog::faces(family));
    return cache.value(family);
}

QVariantList EditorModel::filterOptions() const
{
    QVariantList list;
    for (auto f : allCases<FilterPreset>())
        list.append(QVariantMap{{"value", enumKey(f)}, {"title", displayName(f)}});
    return list;
}

QString EditorModel::filterPreview(const QString &materialId, const QString &filter) const
{
    if (materialId.isEmpty() || MediaCache::instance()->thumbnails(materialId).isEmpty())
        return {};
    return QStringLiteral("image://filters/%1/%2").arg(materialId, filter);
}

QVariantList EditorModel::effectOptions() const
{
    QVariantList list;
    for (auto e : allCases<VideoEffectKind>())
        list.append(QVariantMap{{"value", enumKey(e)}, {"title", displayName(e)}, {"icon", effectIcon(e)}});
    return list;
}

QVariantList EditorModel::transitionOptions() const
{
    QVariantList list;
    for (auto t : allCases<TransitionKind>())
        list.append(QVariantMap{{"value", enumKey(t)}, {"title", displayName(t)}});
    return list;
}

QVariantList EditorModel::animationOptions(bool text) const
{
    return MotionCore::animationKinds(text);
}

QVariantList EditorModel::textPresets() const
{
    return TextPresets::list();
}

// MARK: - Reprodução

void EditorModel::setPlayheadMicros(Micros t)
{
    if (t == m_playhead)
        return;
    m_playhead = t;
    emit playheadChanged();
}

void EditorModel::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void EditorModel::playerTimeChanged(Micros t)
{
    if (!m_playing)
        return;
    if (m_excerpt && t >= m_excerpt->first) {
        // Fim do trecho: volta depois do sinal, porque o Player ainda pede o quadro do
        // tick em seguida e ele passaria à frente do quadro da volta.
        const Micros resume = m_excerpt->second;
        m_excerpt.reset();
        QTimer::singleShot(0, this, [this, resume] {
            if (m_excerpt || !m_playing)
                return;
            m_preview->pause();
            setPlaying(false);
            seekMicros(resume);
        });
        return;
    }
    setPlayheadMicros(std::min(t, m_project.duration()));
    revealPlayhead();
    if (m_playhead >= m_project.duration())
        setPlaying(false);
}

void EditorModel::togglePlay()
{
    m_excerpt.reset();
    if (m_playing) {
        m_preview->pause();
        setPlaying(false);
        setPlayheadMicros(std::min(m_preview->time(), m_project.duration()));
        return;
    }
    if (m_project.duration() <= 0 || !Bridge::hasPlayer())
        return;
    if (m_playhead >= m_project.duration() - 30'000)
        seekMicros(0);
    setPlaying(true);
    m_preview->play();
}

void EditorModel::seekMicros(Micros t)
{
    setPlayheadMicros(std::clamp<Micros>(t, 0, m_project.duration()));
    m_preview->seek(m_playhead);
    emit selectionChanged(); // "dividir" depende do playhead
}

void EditorModel::seek(qreal seconds)
{
    seekMicros(Time::seconds(seconds));
}

void EditorModel::scrub(qreal seconds)
{
    if (m_playing)
        togglePlay();
    seek(seconds);
}

void EditorModel::step(int frames)
{
    if (m_playing)
        togglePlay();
    seekMicros(m_playhead + Micros(double(frames) / m_project.fps * 1'000'000));
    revealPlayhead();
}

void EditorModel::goToStart()
{
    if (m_playing)
        togglePlay();
    seekMicros(0);
    revealPlayhead();
}

void EditorModel::goToEnd()
{
    if (m_playing)
        togglePlay();
    seekMicros(m_project.duration());
    revealPlayhead();
}

/// Vai ao corte anterior ou seguinte: início ou fim de qualquer clipe.
void EditorModel::jumpToEdit(bool forward)
{
    if (m_playing)
        togglePlay();
    QList<Micros> edges{0, m_project.duration()};
    for (const auto &t : m_project.tracks)
        for (const auto &s : t.segments)
            edges << s.targetStart << s.targetEnd();
    std::optional<Micros> target;
    for (Micros e : edges) {
        if (forward && e > m_playhead && (!target || e < *target))
            target = e;
        if (!forward && e < m_playhead && (!target || e > *target))
            target = e;
    }
    if (!target)
        return;
    seekMicros(*target);
    revealPlayhead();
}

// MARK: - Zoom e rolagem

qreal EditorModel::fitZoom() const
{
    const double usable = m_viewportWidth - 48;
    const double seconds = Time::toSeconds(m_project.duration());
    if (seconds <= 0 || usable <= 0)
        return 80;
    return std::min(usable / seconds, maxZoomValue);
}

/// 8 pt/s, ou menos quando é preciso para um projeto longo caber inteiro.
qreal EditorModel::minZoom() const
{
    return std::max(0.01, std::min(8.0, fitZoom()));
}

qreal EditorModel::timelineContentWidth() const
{
    // 15 s depois do fim (e no mínimo 160, para a barra de ações do último clipe).
    const double width = Time::toSeconds(m_project.duration()) * m_zoom + std::max(15 * m_zoom, 160.0);
    return std::max<double>(m_viewportWidth, width) + 24;
}

void EditorModel::setScrollClamped(qreal x)
{
    const qreal maxX = std::max<qreal>(0, timelineContentWidth() - m_viewportWidth);
    x = std::clamp<qreal>(x, 0, maxX);
    if (std::abs(x - m_scrollX) < 0.01)
        return;
    m_scrollX = x;
    emit scrollChanged();
}

void EditorModel::setTimelineScrollX(qreal x)
{
    setScrollClamped(x);
}

void EditorModel::setTimelineViewportWidth(qreal width)
{
    if (std::abs(width - m_viewportWidth) < 0.5)
        return;
    m_viewportWidth = width;
    emit zoomChanged();
    setScrollClamped(m_scrollX);
}

void EditorModel::scrollTimelineBy(qreal dx)
{
    setScrollClamped(m_scrollX + dx);
}

void EditorModel::zoomIn()
{
    setZoom(m_zoom * 1.4);
}

void EditorModel::zoomOut()
{
    setZoom(m_zoom / 1.4);
}

void EditorModel::zoomToFit()
{
    setZoom(fitZoom(), 0, 0);
}

void EditorModel::setZoom(qreal value, qreal time, qreal x)
{
    const qreal next = std::clamp<qreal>(value, minZoom(), maxZoomValue);
    qreal anchorTime = time, anchorX = x;
    if (time < 0 || x < 0) {
        const qreal playheadX = Time::toSeconds(m_playhead) * m_zoom - m_scrollX;
        if (playheadX >= 0 && playheadX <= m_viewportWidth) {
            anchorTime = Time::toSeconds(m_playhead);
            anchorX = playheadX;
        } else {
            anchorTime = (m_scrollX + m_viewportWidth / 2) / m_zoom;
            anchorX = m_viewportWidth / 2;
        }
    }
    if (std::abs(next - m_zoom) > 1e-9) {
        m_zoom = next;
        emit zoomChanged();
    }
    setScrollClamped(anchorTime * next - anchorX);
}

void EditorModel::zoomAtViewportX(qreal factor, qreal viewportX)
{
    const qreal time = (m_scrollX + viewportX) / m_zoom;
    setZoom(m_zoom * factor, time, viewportX);
}

/// Rola a timeline para o playhead ficar à vista. Na reprodução, vira a
/// página quando ele chega perto da borda direita.
void EditorModel::revealPlayhead()
{
    const qreal x = Time::toSeconds(m_playhead) * m_zoom;
    if (m_viewportWidth <= 0 || (x >= m_scrollX && x <= m_scrollX + m_viewportWidth - 40))
        return;
    setScrollClamped(x - m_viewportWidth * 0.1);
}

// MARK: - Arrastes na timeline

QString EditorModel::dragSegment() const
{
    return m_dragPreview ? idString(m_dragPreview->segment) : QString();
}

void EditorModel::animateSlides()
{
    if (!m_slideAnimation) {
        m_slideAnimation = true;
        emit slideAnimationChanged();
    }
    m_slideTimer.start();
}

void EditorModel::setDragTarget(const std::optional<DragTarget> &target)
{
    if (target == m_dragTarget)
        return;
    m_dragTarget = target;
    emit dragChanged();
}

void EditorModel::setDragPreview(const std::optional<DragPreview> &preview)
{
    if (preview == m_dragPreview)
        return;
    m_dragPreview = preview;
    rebuildModels();
    emit dragChanged();
}

std::optional<int> EditorModel::dragTargetRowFor(const QUuid &id, int rows, const Project &base) const
{
    const QList<int> order = displayTrackOrder();
    const auto loc = base.locate(id);
    if (rows == 0 || !loc)
        return std::nullopt;
    const int row = int(order.indexOf(loc->track));
    const int target = row + rows;
    if (row < 0 || target < 0 || target >= order.size() || !TimelineEditor::canMove(base, id, order[target]))
        return std::nullopt;
    return target;
}

/// Arraste do clipe: `dx` em pontos; `rows`, deslocamento vertical em linhas.
/// O clipe segue o cursor nos dois sentidos; na faixa principal, os vizinhos
/// deslizam para abrir espaço.
void EditorModel::dragMove(const QString &segmentId, qreal dx, int rows)
{
    const auto id = parseId(segmentId);
    if (!id)
        return;
    beginGesture();
    if (m_selection != id)
        setSelection(segmentId);
    const Project base = *m_gestureBase;
    const auto seg = base.segment(*id);
    const auto loc = base.locate(*id);
    if (!seg || !loc)
        return;
    const auto row = dragTargetRowFor(*id, rows, base);
    const Micros delta = Micros(dx / m_zoom * 1'000'000);
    if (base.tracks[loc->track].isMain) {
        const Micros start = std::max<Micros>(0, seg->targetStart + delta);
        setDragTarget(row ? std::optional(DragTarget{*id, rows, *row, start}) : std::nullopt);
        if (row) {
            // Saindo da faixa principal: ela fica como estava até soltar.
            updateGesture([](Project &) {});
        } else {
            const Micros center = start + seg->targetDuration() / 2;
            const auto before = m_project.tracks[*m_project.mainTrackIndex()].segments;
            Project copy = base;
            TimelineEditor::reorderMain(copy, *id, center);
            bool orderChanged = before.size() != copy.tracks[*copy.mainTrackIndex()].segments.size();
            for (int i = 0; !orderChanged && i < before.size(); ++i)
                orderChanged = before[i].id != copy.tracks[*copy.mainTrackIndex()].segments[i].id;
            if (orderChanged)
                animateSlides();
            updateGesture([&](Project &p) { TimelineEditor::reorderMain(p, *id, center); });
        }
        setDragPreview(DragPreview{*id, start, row ? rows : 0, 0});
        return;
    }
    const QList<Micros> points = TimelineEditor::snapPoints(base, *id, {m_playhead});
    const Micros threshold = Micros(8 / m_zoom * 1'000'000);
    const Micros start = TimelineEditor::snapMove(std::max<Micros>(0, seg->targetStart + delta), seg->targetDuration(),
                                                  points, threshold, dx >= 0);
    setDragTarget(row ? std::optional(DragTarget{*id, rows, *row, start}) : std::nullopt);
    updateGesture([&](Project &p) { TimelineEditor::move(p, *id, start); });
    setDragPreview(DragPreview{*id, start, row ? rows : 0, 0});
}

/// Ao soltar: move para outra faixa e resolve sobreposições. O clipe desliza
/// do cursor até o lugar final.
void EditorModel::endDragMove(const QString &segmentId)
{
    const auto id = parseId(segmentId);
    animateSlides();
    if (id && m_dragTarget && m_dragTarget->segment == *id && m_gestureBase) {
        const QList<int> order = displayTrackOrder();
        if (m_dragTarget->row >= 0 && m_dragTarget->row < order.size()) {
            const int track = order[m_dragTarget->row];
            const Micros start = m_dragTarget->start;
            updateGestureInPlace([&](Project &p) {
                TimelineEditor::moveToTrack(p, *id, track);
                TimelineEditor::move(p, *id, start);
            });
        }
    }
    setDragTarget(std::nullopt);
    m_dragPreview.reset();
    if (id)
        updateGestureInPlace([&](Project &p) { TimelineEditor::resolveOverlap(p, *id); });
    rebuildModels();
    emit dragChanged();
    endGesture();
}

/// Aparar pela alça: a borda arrastada segue o cursor nos dois lados.
void EditorModel::dragTrim(const QString &segmentId, bool leading, qreal dx)
{
    const auto id = parseId(segmentId);
    if (!id)
        return;
    beginGesture();
    if (m_selection != id)
        setSelection(segmentId);
    const Project base = *m_gestureBase;
    const auto seg = base.segment(*id);
    const auto loc = base.locate(*id);
    if (!seg || !loc)
        return;
    const Micros delta = Micros(dx / m_zoom * 1'000'000);
    const QList<Micros> points = TimelineEditor::snapPoints(base, *id, {m_playhead});
    const Micros threshold = Micros(8 / m_zoom * 1'000'000);
    if (leading) {
        const Micros target = TimelineEditor::snap(seg->targetStart + delta, points, threshold);
        updateGesture([&](Project &p) { TimelineEditor::trimStart(p, *id, target); });
        // A faixa principal é magnética: durante o gesto os clipes seguintes ficam
        // onde estavam, com o início sob o cursor; o espaço fecha ao soltar.
        if (base.tracks[loc->track].isMain) {
            if (const auto now = m_project.segment(*id)) {
                const Micros shift = seg->targetDuration() - now->targetDuration();
                setDragPreview(DragPreview{*id, now->targetStart + shift, 0, shift});
            }
        }
    } else {
        const Micros target = TimelineEditor::snap(seg->targetEnd() + delta, points, threshold);
        updateGesture([&](Project &p) { TimelineEditor::trimEnd(p, *id, target); });
    }
}

void EditorModel::endDragTrim()
{
    if (m_dragPreview) {
        animateSlides();
        setDragPreview(std::nullopt);
    }
    endGesture();
}

// MARK: - Exportação

QVariantMap EditorModel::exportSettings() const
{
    const auto &s = m_exportSettings;
    return {{"resolution", enumKey(s.resolution)},
            {"customWidth", s.customWidth},
            {"customHeight", s.customHeight},
            {"frameRate", enumKey(s.frameRate)},
            {"codec", enumKey(s.codec)},
            {"container", enumKey(s.container)},
            {"bitrateMode", enumKey(s.bitrateMode)},
            {"customMbps", s.customMbps},
            {"targetMegabytes", s.targetMegabytes},
            {"keyframeInterval", s.keyframeInterval},
            {"audioEnabled", s.audioEnabled},
            {"audioKbps", s.audioKbps},
            {"audioSampleRate", s.audioSampleRate},
            {"audioOnly", isAudioOnly(s.container)},
            {"extension", fileExtension(s.container)}};
}

QVariantList EditorModel::exportOptions(const QString &key) const
{
    QVariantList list;
    auto add = [&](const QString &value, const QString &title) {
        list.append(QVariantMap{{"value", value}, {"title", title}});
    };
    if (key == "container")
        for (auto c : allCases<ContainerFormat>())
            add(enumKey(c), label(c));
    else if (key == "resolution")
        for (auto r : allCases<ResolutionPreset>())
            add(enumKey(r), label(r));
    else if (key == "frameRate")
        for (auto f : allCases<FrameRate>())
            add(enumKey(f), label(f));
    else if (key == "codec")
        for (auto c : allCases<VideoCodec>())
            add(enumKey(c), label(c));
    else if (key == "bitrateMode")
        for (auto b : allCases<BitrateMode>())
            add(enumKey(b), label(b));
    else if (key == "audioKbps")
        for (int k : ExportSettings::audioBitrates)
            list.append(QVariantMap{{"value", k}, {"title", QStringLiteral("%1 kbps").arg(k)}});
    else if (key == "audioSampleRate")
        for (int r : ExportSettings::sampleRates)
            list.append(QVariantMap{{"value", r}, {"title", QStringLiteral("%1 Hz").arg(r)}});
    return list;
}

void EditorModel::setExportValue(const QString &key, const QVariant &value)
{
    auto &s = m_exportSettings;
    const QString v = value.toString();
    if (key == "resolution")
        s.resolution = enumFromKey<ResolutionPreset>(v).value_or(s.resolution);
    else if (key == "frameRate")
        s.frameRate = enumFromKey<FrameRate>(v).value_or(s.frameRate);
    else if (key == "codec")
        s.codec = enumFromKey<VideoCodec>(v).value_or(s.codec);
    else if (key == "container")
        s.container = enumFromKey<ContainerFormat>(v).value_or(s.container);
    else if (key == "bitrateMode")
        s.bitrateMode = enumFromKey<BitrateMode>(v).value_or(s.bitrateMode);
    else if (key == "customWidth")
        s.customWidth = std::clamp(value.toInt(), 16, 16384);
    else if (key == "customHeight")
        s.customHeight = std::clamp(value.toInt(), 16, 16384);
    else if (key == "customMbps")
        s.customMbps = std::clamp(value.toDouble(), 0.5, 800.0);
    else if (key == "targetMegabytes")
        s.targetMegabytes = std::clamp(value.toDouble(), 1.0, 20000.0);
    else if (key == "keyframeInterval")
        s.keyframeInterval = std::clamp(value.toDouble(), 0.5, 10.0);
    else if (key == "audioEnabled")
        s.audioEnabled = value.toBool();
    else if (key == "audioKbps")
        s.audioKbps = value.toInt();
    else if (key == "audioSampleRate")
        s.audioSampleRate = value.toInt();
    emit exportSettingsChanged();
    emit projectChanged(); // tamanho de saída na barra superior
}

QVariantMap EditorModel::exportSummary() const
{
    const auto &s = m_exportSettings;
    const QSize size = s.outputSize(m_project);
    QVariantMap map{{"estimated", formattedFileSize(s.estimatedBytes(m_project))},
                    {"dimensions", QStringLiteral("%1 × %2").arg(size.width()).arg(size.height())},
                    {"frameRate", label(s.frameRate)},
                    {"audio", s.audioEnabled ? QStringLiteral("%1 kbps").arg(s.audioKbps) : QStringLiteral("sem áudio")},
                    {"duration", Time::format(m_project.duration())},
                    {"error", s.validationError(m_project).value_or(QString())},
                    {"keyframeText", QStringLiteral("a cada %1 s").arg(QLocale(QLocale::Portuguese, QLocale::Brazil)
                                                                          .toString(s.keyframeInterval, 'f', 1))}};
    if (s.codec == VideoCodec::ProRes422)
        map["video"] = QStringLiteral("variável (ProRes)");
    else
        map["video"] = QStringLiteral("%1 Mbps").arg(QLocale(QLocale::Portuguese, QLocale::Brazil)
                                                         .toString(s.videoBitrate(size, m_project.duration()) / 1e6, 'f', 2));
    return map;
}

void EditorModel::startExport(const QUrl &url)
{
    QString path = localPath(url);
    const QString ext = fileExtension(m_exportSettings.container);
    if (QFileInfo(path).suffix().toLower() != ext)
        path += "." + ext;
    if (m_playing)
        togglePlay();
    m_exportResultPath = path;
    m_exportProgress = 0;
    m_exportRemaining.clear();
    m_exportError.clear();
    QString error;
    if (!m_exportJob->start(m_project, m_exportSettings, path, &error)) {
        m_exportError = error;
        m_exportPhase = "failed";
    } else {
        m_exportClock.start();
        m_exportPhase = "exporting";
    }
    emit exportStateChanged();
}

void EditorModel::cancelExport()
{
    m_exportJob->cancel();
}

void EditorModel::resetExport()
{
    m_exportPhase = "configuring";
    m_exportProgress = 0;
    m_exportError.clear();
    emit exportStateChanged();
}

void EditorModel::revealFile(const QString &path)
{
    const QFileInfo info(path);
#if defined(Q_OS_WIN)
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", info.absoluteFilePath()});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

void EditorModel::openFile(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// MARK: - Teclado

bool EditorModel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && watched->isWindowType()) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (handleKey(key->key(), key->modifiers()))
            return true;
    }
    return QObject::eventFilter(watched, event);
}

/// Espaço, Delete, Esc, setas, Home/End e Shift+Z funcionam em qualquer lugar,
/// exceto ao digitar texto ou com um diálogo aberto.
bool EditorModel::handleKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (m_screen != "editor" || m_showExport || m_modalCount > 0)
        return false;
    if (QObject *focus = QGuiApplication::focusObject()) {
        if (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit"))
            return false;
    }
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const bool shift = modifiers & Qt::ShiftModifier;
    switch (key) {
    case Qt::Key_Space: togglePlay(); break;
    case Qt::Key_Z:
        if (!shift)
            return false;
        zoomToFit();
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelection(); break;
    case Qt::Key_Escape:
        if (!m_selection)
            return false;
        setSelection({});
        break;
    case Qt::Key_Home: goToStart(); break;
    case Qt::Key_End: goToEnd(); break;
    case Qt::Key_Up: jumpToEdit(false); break;
    case Qt::Key_Down: jumpToEdit(true); break;
    case Qt::Key_Left: step(shift ? -10 : -1); break;
    case Qt::Key_Right: step(shift ? 10 : 1); break;
    default: return false;
    }
    return true;
}

} // namespace of
