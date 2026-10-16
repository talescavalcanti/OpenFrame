#pragma once

#include "CanvasGeometry.h"
#include "CaptionGeneration.h"
#include "ExportSettings.h"
#include "KeyedListModel.h"
#include "Project.h"
#include "ProjectLibrary.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <optional>

class QQmlEngine;
class QJSEngine;

namespace of {

class PreviewEngine;
class ExportJob;

/// Clipe arrastado para outra faixa: deslocamento em linhas, linha de destino
/// (na ordem de exibição) e início do clipe nela.
struct DragTarget {
    QUuid segment;
    int rows = 0;
    int row = 0;
    Micros start = 0;
    bool operator==(const DragTarget &) const = default;
};

/// Onde o clipe do gesto é desenhado: segue o cursor, enquanto o projeto já
/// mostra o resultado (vizinhos no novo lugar).
struct DragPreview {
    QUuid segment;
    /// Início desenhado do clipe.
    Micros start = 0;
    /// Deslocamento vertical em linhas, só sobre uma faixa que aceita o clipe.
    int rows = 0;
    /// Na faixa principal, quanto os clipes seguintes ficam deslocados enquanto
    /// o início é aparado.
    Micros following = 0;
    bool operator==(const DragPreview &) const = default;
};

/// Estado do editor, exposto ao QML como o singleton `Editor` (ver `EditorSingleton`). Toda alteração no
/// projeto passa por `mutate` (com desfazer) ou pelos métodos de gesto. É o
/// `EditorModel` do app Swift: undo por cópia do projeto (até 300 passos),
/// sliders agrupados por 1 s, autosave 0,8 s depois da última alteração.
class EditorModel : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // Tela e biblioteca
    Q_PROPERTY(QString screen READ screen NOTIFY screenChanged)
    Q_PROPERTY(KeyedListModel *entries READ entries CONSTANT)
    Q_PROPERTY(QString libraryRoot READ libraryRoot CONSTANT)

    // Projeto
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(bool libraryProject READ libraryProject NOTIFY projectChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(qreal duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY projectChanged)
    Q_PROPERTY(QString aspect READ aspect NOTIFY projectChanged)
    Q_PROPERTY(qreal aspectValue READ aspectValue NOTIFY projectChanged)
    Q_PROPERTY(QVariantList aspectOptions READ aspectOptions CONSTANT)
    Q_PROPERTY(QSize outputSize READ outputSize NOTIFY projectChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap canvas READ canvas NOTIFY projectChanged)
    Q_PROPERTY(KeyedListModel *materials READ materials CONSTANT)
    Q_PROPERTY(KeyedListModel *tracks READ tracks CONSTANT)
    Q_PROPERTY(KeyedListModel *clips READ clips CONSTANT)
    Q_PROPERTY(KeyedListModel *junctions READ junctions CONSTANT)
    Q_PROPERTY(KeyedListModel *cues READ cues CONSTANT)

    // Seleção e inspetor
    Q_PROPERTY(QString selection READ selection WRITE setSelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedSegment READ selectedSegment NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList availableTabs READ availableTabs NOTIFY selectionChanged)
    Q_PROPERTY(QString inspectorTab READ inspectorTab WRITE setInspectorTab NOTIFY uiChanged)
    Q_PROPERTY(QString libraryPanel READ libraryPanel WRITE setLibraryPanel NOTIFY uiChanged)
    Q_PROPERTY(bool libraryVisible READ libraryVisible WRITE setLibraryVisible NOTIFY uiChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY uiChanged)
    Q_PROPERTY(bool showExport READ showExport WRITE setShowExport NOTIFY uiChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage WRITE setErrorMessage NOTIFY uiChanged)
    /// Diálogos e menus abertos no QML: as teclas do editor ficam desligadas.
    Q_PROPERTY(int modalCount READ modalCount WRITE setModalCount NOTIFY uiChanged)

    // Legendas automáticas (EditorCaptions.cpp)
    Q_PROPERTY(CaptionGeneration *captions READ captions CONSTANT)
    Q_PROPERTY(bool showAutoCaptions READ showAutoCaptions WRITE setShowAutoCaptions NOTIFY uiChanged)
    Q_PROPERTY(QString captionBackdrop READ captionBackdrop NOTIFY captionStateChanged)
    /// Muda quando chegam miniaturas ao MediaCache: as ligações que dependem delas
    /// (como `filterPreview`) leem esta propriedade para se refazer.
    Q_PROPERTY(int thumbnailRevision READ thumbnailRevision NOTIFY thumbnailsChanged)
    /// Por que a fonte escolhida não pode ser transcrita (vazio = pode).
    Q_PROPERTY(QString captionSourceProblem READ captionSourceProblem NOTIFY captionStateChanged)
    /// O selecionado é um clipe com áudio (fonte "Clipe selecionado").
    Q_PROPERTY(bool clipWithAudio READ clipWithAudio NOTIFY captionStateChanged)
    Q_PROPERTY(bool hasAutoCaptions READ hasAutoCaptions NOTIFY captionStateChanged)
    /// O selecionado é uma legenda automática ("Aplicar este estilo a todas").
    Q_PROPERTY(bool selectionIsAutoCaption READ selectionIsAutoCaption NOTIFY captionStateChanged)

    // Reprodução
    Q_PROPERTY(qreal playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(QString playheadText READ playheadText NOTIFY playheadChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playingChanged)

    // Timeline
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoomValue NOTIFY zoomChanged)
    Q_PROPERTY(qreal minZoom READ minZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal maxZoom READ maxZoom CONSTANT)
    Q_PROPERTY(qreal timelineScrollX READ timelineScrollX WRITE setTimelineScrollX NOTIFY scrollChanged)
    Q_PROPERTY(qreal timelineViewportWidth READ timelineViewportWidth WRITE setTimelineViewportWidth NOTIFY zoomChanged)
    Q_PROPERTY(qreal timelineContentWidth READ timelineContentWidth NOTIFY zoomChanged)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY dragChanged)
    Q_PROPERTY(QString dragSegment READ dragSegment NOTIFY dragChanged)
    Q_PROPERTY(int dragTargetRow READ dragTargetRow NOTIFY dragChanged)
    /// Os clipes deslizam até o novo lugar (reordenação e ao soltar).
    Q_PROPERTY(bool slideAnimation READ slideAnimation NOTIFY slideAnimationChanged)
    Q_PROPERTY(QString draggingMaterial READ draggingMaterial WRITE setDraggingMaterial NOTIFY dragChanged)
    // Altura das faixas (EditorRows.cpp), na ordem de exibição. `rowTops` tem uma entrada a mais: o fim
    // da última faixa (com o espaço depois dela). Posições do conteúdo da timeline (topo da régua = 0).
    Q_PROPERTY(QVariantList rowTops READ rowTops NOTIFY rowLayoutChanged)
    Q_PROPERTY(QVariantList rowHeights READ rowHeights NOTIFY rowLayoutChanged)
    /// Altura da régua com todas as faixas.
    Q_PROPERTY(qreal rowsHeight READ rowsHeight NOTIFY rowLayoutChanged)
    /// Altura comum a todas as faixas (0 se forem diferentes), para marcar o menu.
    Q_PROPERTY(qreal commonRowHeight READ commonRowHeight NOTIFY rowLayoutChanged)
    Q_PROPERTY(QVariantList rowHeightPresets READ rowHeightPresets CONSTANT)

    // Exportação
    Q_PROPERTY(QVariantMap exportSettings READ exportSettings NOTIFY exportSettingsChanged)
    Q_PROPERTY(QVariantMap exportSummary READ exportSummary NOTIFY exportSummaryChanged)
    Q_PROPERTY(QString exportPhase READ exportPhase NOTIFY exportStateChanged)
    Q_PROPERTY(qreal exportProgress READ exportProgress NOTIFY exportStateChanged)
    Q_PROPERTY(QString exportRemaining READ exportRemaining NOTIFY exportStateChanged)
    Q_PROPERTY(QString exportResult READ exportResult NOTIFY exportStateChanged)
    Q_PROPERTY(QString exportResultPath READ exportResultPath NOTIFY exportStateChanged)
    Q_PROPERTY(QString exportError READ exportError NOTIFY exportStateChanged)

public:
    explicit EditorModel(QObject *parent = nullptr);
    ~EditorModel() override;

    static EditorModel *instance();

    static constexpr double maxZoomValue = 600;

    // Leitura (QML)
    QString screen() const { return m_screen; }
    KeyedListModel *entries() const { return m_entries; }
    QString libraryRoot() const { return m_library.root(); }
    QString projectName() const { return m_project.name; }
    bool libraryProject() const { return m_libraryId.has_value(); }
    bool isDirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    qreal duration() const { return Time::toSeconds(m_project.duration()); }
    QString durationText() const { return Time::format(m_project.duration()); }
    QString aspect() const { return enumKey(m_project.canvas.aspect); }
    qreal aspectValue() const { return m_project.aspectValue(); }
    QVariantList aspectOptions() const;
    QSize outputSize() const { return m_exportSettings.outputSize(m_project); }
    int clipCount() const;
    QVariantMap canvas() const;
    KeyedListModel *materials() const { return m_materials; }
    KeyedListModel *tracks() const { return m_tracks; }
    KeyedListModel *clips() const { return m_clips; }
    KeyedListModel *junctions() const { return m_junctions; }
    KeyedListModel *cues() const { return m_cues; }

    QString selection() const;
    void setSelection(const QString &id);
    QVariantMap selectedSegment() const { return m_selectedSegment; }
    int selectedRow() const;
    QVariantList availableTabs() const;
    QString inspectorTab() const { return m_inspectorTab; }
    void setInspectorTab(const QString &tab);
    QString libraryPanel() const { return m_libraryPanel; }
    void setLibraryPanel(const QString &panel);
    bool libraryVisible() const { return m_libraryVisible; }
    void setLibraryVisible(bool visible);
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool show);
    bool showExport() const { return m_showExport; }
    void setShowExport(bool show);
    QString errorMessage() const { return m_errorMessage; }
    void setErrorMessage(const QString &message);
    int modalCount() const { return m_modalCount; }
    void setModalCount(int count);

    qreal playhead() const { return Time::toSeconds(m_playhead); }
    QString playheadText() const { return Time::format(m_playhead); }
    bool isPlaying() const { return m_playing; }

    qreal zoom() const { return m_zoom; }
    void setZoomValue(qreal zoom) { setZoom(zoom); }
    qreal minZoom() const;
    qreal maxZoom() const { return maxZoomValue; }
    qreal fitZoom() const;
    qreal timelineScrollX() const { return m_scrollX; }
    void setTimelineScrollX(qreal x);
    qreal timelineViewportWidth() const { return m_viewportWidth; }
    void setTimelineViewportWidth(qreal width);
    qreal timelineContentWidth() const;
    bool gestureActive() const { return m_dragPreview.has_value(); }
    QString dragSegment() const;
    int dragTargetRow() const { return m_dragTarget ? m_dragTarget->row : -1; }
    bool slideAnimation() const { return m_slideAnimation; }
    QString draggingMaterial() const { return m_draggingMaterial; }
    void setDraggingMaterial(const QString &id);

    QVariantMap exportSettings() const;
    QVariantMap exportSummary() const;
    QString exportPhase() const { return m_exportPhase; }
    qreal exportProgress() const { return m_exportProgress; }
    QString exportRemaining() const { return m_exportRemaining; }
    QString exportResult() const { return m_exportResult; }
    QString exportResultPath() const { return m_exportResultPath; }
    QString exportError() const { return m_exportError; }

    // Acesso direto (capturas e testes)
    const Project &project() const { return m_project; }
    std::optional<QUuid> selectionId() const { return m_selection; }
    QList<int> displayTrackOrder() const;
    Micros playheadMicros() const { return m_playhead; }

    // Alterações e desfazer
    void mutate(const std::function<void(Project &)> &change, const QString &coalesce = {});
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Telas e projetos
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void createProject(const QString &aspect = QStringLiteral("original"));
    Q_INVOKABLE void openLibraryProject(const QString &id);
    Q_INVOKABLE void showHome();
    Q_INVOKABLE void renameProject(const QString &id, const QString &name);
    Q_INVOKABLE void renameCurrentProject(const QString &name);
    Q_INVOKABLE void duplicateProject(const QString &id);
    Q_INVOKABLE void deleteProject(const QString &id);
    Q_INVOKABLE void revealProject(const QString &id);
    Q_INVOKABLE void newProject();
    /// `.openframe` escolhido no diálogo: importado para a biblioteca e aberto.
    Q_INVOKABLE void openProjectFile(const QUrl &url);
    /// Abre o arquivo direto, fora da biblioteca (`--open`, desenvolvimento e capturas).
    void openExternal(const QString &path);
    Q_INVOKABLE void saveProject();
    /// Grava uma cópia `.openframe` (ou o próprio arquivo, fora da biblioteca).
    Q_INVOKABLE void exportProjectFile(const QUrl &url);
    Q_INVOKABLE QString suggestedFileName(const QString &extension) const;
    void saveNow();

    // Mídia
    Q_INVOKABLE void importMedia(const QList<QUrl> &urls);
    Q_INVOKABLE bool isSupportedMedia(const QUrl &url) const;
    Q_INVOKABLE void addToTimeline(const QString &materialId);
    Q_INVOKABLE void addAsOverlay(const QString &materialId);
    Q_INVOKABLE void removeMaterial(const QString &materialId);
    Q_INVOKABLE QVariantMap dropPreview(const QString &materialId, qreal time, int row) const;
    /// Linha exibida sem clipes entre `from` e `to` (s). Abaixo da última há espaço livre; acima da primeira, a régua.
    Q_INVOKABLE bool isRowFree(int row, qreal from, qreal to) const;

    // Altura das faixas (EditorRows.cpp)
    static constexpr qreal rulerHeight = 30;
    static constexpr qreal rowSpacing = 6;
    static constexpr qreal defaultRowHeight = 50;
    static constexpr qreal minRowHeight = 32;
    static constexpr qreal maxRowHeight = 200;
    qreal rowHeightOf(const Track &track) const;
    QVariantList rowTops() const;
    QVariantList rowHeights() const;
    qreal rowsHeight() const;
    qreal commonRowHeight() const;
    QVariantList rowHeightPresets() const;
    /// Topo da linha `row` (`row` = número de faixas dá o fim da última).
    Q_INVOKABLE qreal rowTop(int row) const;
    /// Linha sob a posição `y` (cada linha vai do topo dela ao da seguinte); -1 sobre a régua.
    /// Abaixo da última faixa, devolve índices além dela.
    Q_INVOKABLE int rowAt(qreal y) const;
    /// Altura de uma faixa ao vivo, durante o arraste da borda (0 = a padrão).
    Q_INVOKABLE void resizeRow(const QString &trackId, qreal height);
    /// Fim do arraste da borda.
    Q_INVOKABLE void endRowResize();
    /// Mesma altura para todas as faixas (0 = a padrão).
    Q_INVOKABLE void setAllRowHeights(qreal height);
    Q_INVOKABLE void dropMaterial(const QString &materialId, qreal time, int row);

    // Edição
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void addText(const QString &preset = {});
    Q_INVOKABLE void importSubtitles(const QUrl &url);
    Q_INVOKABLE void exportSubtitles(const QUrl &url);
    Q_INVOKABLE bool canSplitSelection() const;
    Q_INVOKABLE void toggleTrack(const QString &trackId, const QString &flag);
    Q_INVOKABLE void setAspect(const QString &key);
    Q_INVOKABLE void setCanvasBackground(const QString &kind);
    Q_INVOKABLE void setCanvasColor(const QColor &color);
    Q_INVOKABLE void setCanvasBlur(qreal amount);
    Q_INVOKABLE void setProjectName(const QString &name);

    /// Altera um campo do segmento pelo caminho das chaves do JSON
    /// ("transform.scale", "text.style.color", "filter.intensity"…). Um valor
    /// indefinido remove o campo (opcionais como `filter` e `transition`).
    Q_INVOKABLE void setSegmentValue(const QString &path, const QVariant &value);
    Q_INVOKABLE void resetSegment(const QString &group);
    Q_INVOKABLE void setSpeed(qreal speed);
    Q_INVOKABLE void setFilter(const QString &preset);
    Q_INVOKABLE void toggleEffect(const QString &kind);
    Q_INVOKABLE void setAnimation(const QString &which, const QString &kind);
    Q_INVOKABLE void setTransition(const QString &kind);
    Q_INVOKABLE void applyTextPreset(const QString &preset);
    Q_INVOKABLE void setFontFamily(const QString &family);
    Q_INVOKABLE void setFontFace(const QString &face);
    Q_INVOKABLE QStringList fontFamilies() const;
    Q_INVOKABLE QStringList suggestedFontFamilies() const;
    Q_INVOKABLE QStringList fontFaces(const QString &family) const;
    Q_INVOKABLE QVariantList filterOptions() const;
    /// `image://filters/<material>/<filtro>`, ou vazio se o material ainda não tem
    /// miniatura (mídia ausente ou em leitura): assim a aba não pede imagem que não existe.
    Q_INVOKABLE QString filterPreview(const QString &materialId, const QString &filter) const;
    int thumbnailRevision() const { return m_thumbnailRevision; }
    Q_INVOKABLE QVariantList effectOptions() const;
    Q_INVOKABLE QVariantList transitionOptions() const;
    Q_INVOKABLE QVariantList animationOptions(bool text) const;
    // Aba Animação (EditorMotion.cpp)
    Q_INVOKABLE QVariantList loopOptions() const;
    /// `kind` vazio tira o loop. Escolher toca dois ciclos no preview.
    Q_INVOKABLE void setLoop(const QString &kind);
    /// "periodSeconds" (0,5…6) ou "intensity" (0…1).
    Q_INVOKABLE void setLoopValue(const QString &key, qreal value);
    /// Toca o trecho uma vez e volta ao ponto em que o playhead estava (prévia de um efeito).
    void playExcerpt(Micros start, Micros end);
    Q_INVOKABLE QVariantList textPresets() const;

    // Reprodução
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qreal seconds);
    Q_INVOKABLE void scrub(qreal seconds);
    Q_INVOKABLE void step(int frames);
    Q_INVOKABLE void goToStart();
    Q_INVOKABLE void goToEnd();
    Q_INVOKABLE void jumpToEdit(bool forward);
    void seekMicros(Micros t);

    // Zoom e rolagem
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit();
    /// Muda o zoom mantendo o tempo `time` (s) na posição `x` da área visível.
    /// Sem âncora (x < 0), mantém o playhead (se estiver à vista) ou o centro.
    Q_INVOKABLE void setZoom(qreal value, qreal time = -1, qreal x = -1);
    /// Roda do mouse com Ctrl/Alt: zoom no ponto `viewportX` da área visível.
    Q_INVOKABLE void zoomAtViewportX(qreal factor, qreal viewportX);
    Q_INVOKABLE void scrollTimelineBy(qreal dx);

    // Arrastes na timeline
    Q_INVOKABLE void dragMove(const QString &id, qreal dx, int rows);
    Q_INVOKABLE void endDragMove(const QString &id);
    Q_INVOKABLE void dragTrim(const QString &id, bool leading, qreal dx);
    Q_INVOKABLE void endDragTrim();

    // Exportação
    Q_INVOKABLE void setExportValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantList exportOptions(const QString &key) const;
    Q_INVOKABLE void startExport(const QUrl &url);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void resetExport();
    Q_INVOKABLE void revealFile(const QString &path);
    Q_INVOKABLE void openFile(const QString &path);

    /// Pede ao QML um diálogo de arquivo ("openProject", "importMedia", "importSrt",
    /// "exportSrt", "exportProject", "exportVideo").
    Q_INVOKABLE void requestDialog(const QString &kind) { emit dialogRequested(kind); }

    /// Teclas do editor (espaço, Delete, Esc, setas, Home/End, Shift+Z).
    bool handleKey(int key, Qt::KeyboardModifiers modifiers);

    // Legendas automáticas (EditorCaptions.cpp)
    CaptionGeneration *captions() const { return m_captions; }
    bool showAutoCaptions() const { return m_showAutoCaptions; }
    void setShowAutoCaptions(bool show);
    QString captionSourceProblem() const;
    bool clipWithAudio() const;
    bool hasAutoCaptions() const;
    bool selectionIsAutoCaption() const;
    /// Abre a janela: volta às opções (se não estiver gerando), idioma do sistema na primeira vez.
    Q_INVOKABLE void presentAutoCaptions();
    /// Baixa o modelo se faltar, transcreve, monta as legendas e as põe na timeline num
    /// único passo de desfazer.
    Q_INVOKABLE void generateCaptions();
    Q_INVOKABLE void cancelCaptionGeneration();
    /// Fecha a janela e volta às opções.
    Q_INVOKABLE void closeAutoCaptions();
    /// Copia o estilo do texto selecionado para todas as legendas automáticas.
    Q_INVOKABLE void applyStyleToAutoCaptions();
    /// Edição em linha no painel: só apara as pontas e não grava se não mudou.
    Q_INVOKABLE void setCaptionText(const QString &id, const QString &text);
    Q_INVOKABLE void deleteCaption(const QString &id);
    /// Largura da caixa do texto selecionado (como as alças: numa faixa automática, vale para todas).
    Q_INVOKABLE void setTextWidth(qreal width);
    /// Revelações das palavras: [{value, title}].
    Q_INVOKABLE QVariantList revealOptions() const;
    /// Liga ou desliga a ênfase de uma palavra (índice de `TextContent::wordsOf`).
    Q_INVOKABLE void toggleEmphasisWord(int index);
    /// Fonte da palavra-chave; o estilo volta ao padrão se a família nova não o tiver.
    Q_INVOKABLE void setEmphasisFont(const QString &family);
    /// Fundo dos cartões de estilo: o primeiro quadro do vídeo da faixa principal (vazio sem vídeo
    /// ou sem miniatura ainda).
    QString captionBackdrop() const;
    /// Proporção (largura/altura) mais próxima entre as do app, para as prévias dos cartões.
    Q_INVOKABLE qreal captionPreviewAspect() const;

    // Manipulação no preview (EditorCanvas.cpp)
    /// Elementos visíveis no playhead (em repouso).
    QList<ElementBox> canvasBoxes() const;
    /// Contorno do selecionado, se ele aparece no playhead. Some durante a reprodução;
    /// no arraste lateral, mostra a largura pedida.
    std::optional<SelectionFrame> selectionFrame(const CanvasMapping &mapping) const;
    /// Clique no preview: alça ou corpo do selecionado, senão o elemento mais acima sob o
    /// ponteiro, senão tira a seleção. Duplo clique num texto abre a aba Texto.
    void canvasPointerDown(QPointF p, const CanvasMapping &mapping, int clickCount = 1);
    /// `snapping` falso com Ctrl segurado.
    void canvasPointerMoved(QPointF p, const CanvasMapping &mapping, bool snapping = true);
    void canvasPointerUp();
    const std::optional<PlacementDrag> &placementDrag() const { return m_placementDrag; }
    const QList<SnapGuide> &canvasGuides() const { return m_canvasGuides; }
    std::optional<double> canvasRotationLabel() const { return m_canvasRotationLabel; }
    std::optional<WidthDragState> canvasWidthDrag() const { return m_canvasWidthDrag; }
    /// Seleciona o texto e abre a aba Texto.
    void editText(const QUuid &id);

    /// Tamanho do player em pontos e a escala da tela. O primeiro vale na hora; os
    /// seguintes, ~300 ms depois que o tamanho parar.
    Q_INVOKABLE void previewViewportChanged(qreal width, qreal height, qreal devicePixelRatio);
    int previewShortSide() const;
    void setPreviewShortSide(int side);
    void updatePreviewResolution();

signals:
    void screenChanged();
    void projectChanged();
    void selectionChanged();
    void uiChanged();
    void thumbnailsChanged();
    void playheadChanged();
    void playingChanged();
    void zoomChanged();
    void scrollChanged();
    void dragChanged();
    void slideAnimationChanged();
    void exportSettingsChanged();
    void exportSummaryChanged();
    void exportStateChanged();
    void dialogRequested(const QString &kind);
    /// Gesto, guias e etiquetas do overlay do preview.
    void canvasOverlayChanged();
    void captionStateChanged();
    void rowLayoutChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setProject(const Project &p);
    void pushUndo(const Project &snapshot);
    void beginGesture();
    void updateGesture(const std::function<void(Project &)> &change);
    void updateGestureInPlace(const std::function<void(Project &)> &change);
    void endGesture();
    void slideGesture(const std::function<void(Project &)> &change);
    void projectDidChange();
    void rebuildModels();
    void rebuildEntries();
    void refreshSelection();
    void ensureValidTab();
    void setPlayheadMicros(Micros t);
    void setPlaying(bool playing);
    void revealPlayhead();
    void setScrollClamped(qreal x);
    void animateSlides();
    void scheduleAutosave();
    void load(const Project &p, std::optional<QUuid> libraryId, const QString &documentPath);
    void loadMediaPreviews(const Material &m);
    void writeThumbnail(const Project &snapshot, const QUuid &id);
    void fillMissingThumbnails();
    QStringList availableTabKeys(const Segment &seg) const;
    bool isOnMainTrack(const QUuid &id) const;
    bool isLastOnMainTrack(const QUuid &id) const;
    std::optional<QUuid> targetSegmentId() const;
    std::optional<int> trackIndex(int row) const;
    std::optional<int> dragTargetRowFor(const QUuid &id, int rows, const Project &base) const;
    void setDragTarget(const std::optional<DragTarget> &target);
    void setDragPreview(const std::optional<DragPreview> &preview);
    QVariantMap segmentMap(const Segment &seg) const;
    void playerTimeChanged(Micros t);

    Project m_project;
    QList<Project> m_undo;
    QList<Project> m_redo;
    QString m_lastCoalesceKey;
    QElapsedTimer m_lastCoalesce;
    std::optional<Project> m_gestureBase;

    std::optional<QUuid> m_selection;
    QVariantMap m_selectedSegment;
    QString m_inspectorTab = QStringLiteral("basic");
    QString m_libraryPanel = QStringLiteral("media");
    bool m_libraryVisible = true;
    bool m_showGrid = false;
    bool m_showExport = false;
    QString m_errorMessage;
    int m_modalCount = 0;
    int m_thumbnailRevision = 0;

    Micros m_playhead = 0;
    bool m_playing = false;

    qreal m_zoom = 80;
    qreal m_scrollX = 0;
    qreal m_viewportWidth = 0;
    std::optional<DragTarget> m_dragTarget;
    /// Altura de cada faixa (ausente = a padrão). Provisório: fica só na interface até o núcleo
    /// gravar a altura no projeto.
    QHash<QUuid, qreal> m_rowHeights;
    /// Alturas na ordem de exibição, para avisar só quando a geometria muda.
    QList<qreal> m_rowLayout;
    void updateRowLayout();
    std::optional<DragPreview> m_dragPreview;
    bool m_slideAnimation = false;
    QTimer m_slideTimer;
    QString m_draggingMaterial;

    QString m_screen = QStringLiteral("home");
    ProjectLibrary m_library;
    QList<ProjectEntry> m_entryList;
    std::optional<QUuid> m_libraryId;
    QString m_documentPath;
    bool m_dirty = false;
    QTimer m_autosave;
    QDateTime m_lastThumbnail;
    QSet<QUuid> m_thumbnailAttempts;

    ExportSettings m_exportSettings;
    QString m_exportPhase = QStringLiteral("configuring");
    qreal m_exportProgress = 0;
    QString m_exportRemaining;
    QString m_exportResult;
    QString m_exportResultPath;
    QString m_exportError;
    QElapsedTimer m_exportClock;

    KeyedListModel *m_entries;
    KeyedListModel *m_materials;
    KeyedListModel *m_tracks;
    KeyedListModel *m_clips;
    KeyedListModel *m_junctions;
    KeyedListModel *m_cues;

    CaptionGeneration *m_captions = nullptr;
    bool m_showAutoCaptions = false;
    /// Trecho e opções da geração em andamento.
    struct CaptionJob {
        TimeRange range;
        QString preset;
        QColor accent;
        bool replacing = true;
        QString model;
        QString language;
        std::optional<QUuid> segment;
        Project snapshot;
    };
    std::optional<CaptionJob> m_captionJob;
    void startTranscription();
    void setupCaptions();

    /// Trecho tocado ao escolher um efeito: onde parar e para onde voltar.
    std::optional<std::pair<Micros, Micros>> m_excerpt;
    std::optional<PlacementDrag> m_placementDrag;
    QList<SnapGuide> m_canvasGuides;
    std::optional<double> m_canvasRotationLabel;
    std::optional<WidthDragState> m_canvasWidthDrag;
    QSizeF m_previewPoints;
    qreal m_previewScale = 1;
    QTimer m_previewResolutionTimer;

    PreviewEngine *m_preview = nullptr;
    ExportJob *m_exportJob = nullptr;
};

/// Expõe ao QML a instância única criada no main, como o singleton `Editor`
/// (o padrão do Qt para um objeto que já existe: QML_FOREIGN + create).
struct EditorSingleton {
    Q_GADGET
    QML_FOREIGN(EditorModel)
    QML_SINGLETON
    QML_NAMED_ELEMENT(Editor)

public:
    static EditorModel *create(QQmlEngine *, QJSEngine *engine);
};

} // namespace of
