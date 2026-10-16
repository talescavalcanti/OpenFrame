#include "Snapshot.h"

#include "AnimationThumbs.h"
#include "CanvasOverlay.h"
#include "CaptionsCore.h"
#include "EditorModel.h"
#include "MediaCache.h"
#include "MotionCore.h"
#include "ProjectStore.h"
#include "TimelineEditor.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <cstdio>

namespace of {

namespace {

QString idString(const QUuid &id) { return ProjectStore::uuidString(id); }

/// Primeiro segmento da faixa principal, ou vazio.
QString firstMainSegment(const Project &p)
{
    const auto main = p.mainTrackIndex();
    if (!main || p.tracks[*main].segments.isEmpty())
        return {};
    return idString(p.tracks[*main].segments.first().id);
}

QString firstOfKind(const Project &p, TrackKind kind)
{
    for (const auto &t : p.tracks)
        if (t.kind == kind && !t.segments.isEmpty())
            return idString(t.segments.first().id);
    return {};
}

} // namespace

void Snapshot::runIfRequested(QQmlApplicationEngine *engine, EditorModel *model)
{
    const QString dir = qEnvironmentVariable("OPENFRAME_SNAPSHOT_DIR");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    auto *snapshot = new Snapshot(engine, model, dir);
    const QString media = qEnvironmentVariable("OPENFRAME_SNAPSHOT_MEDIA");
    const QString scenario = qEnvironmentVariable("OPENFRAME_SNAPSHOT_SCENARIO");
    snapshot->then(1500, [] {});
    if (!media.isEmpty())
        snapshot->mediaScenario(media);
    else if (model->screen() == "home")
        snapshot->homeScenario();
    else if (scenario == "arraste")
        snapshot->dragScenario();
    else
        snapshot->editorScenario();
    snapshot->then(300, [snapshot] { snapshot->finish(); });
    snapshot->next();
}

Snapshot::Snapshot(QQmlApplicationEngine *engine, EditorModel *model, const QString &dir)
    : QObject(model), m_engine(engine), m_model(model), m_dir(dir)
{
}

void Snapshot::then(int ms, Step step)
{
    m_steps.push_back({ms, std::move(step), {}, 0, {}, true});
}

void Snapshot::waitUntil(std::function<bool()> done, int timeoutMs, const QString &what, bool required)
{
    m_steps.push_back({0, [] {}, std::move(done), timeoutMs, what, required});
}

void Snapshot::next()
{
    if (m_steps.empty())
        return;
    Pending pending = std::move(m_steps.front());
    m_steps.pop_front();
    QTimer::singleShot(pending.ms, this, [this, pending] {
        pending.step();
        if (!pending.until) {
            next();
            return;
        }
        auto *poll = new QTimer(this);
        auto *clock = new QElapsedTimer;
        clock->start();
        poll->setInterval(200);
        connect(poll, &QTimer::timeout, this, [this, poll, clock, pending] {
            const bool done = pending.until();
            if (!done && clock->elapsed() < pending.timeoutMs)
                return;
            poll->deleteLater();
            delete clock;
            if (!done) {
                const QString message = QStringLiteral("tempo esgotado esperando %1 (%2 ms)")
                                            .arg(pending.what.isEmpty() ? QStringLiteral("um passo") : pending.what)
                                            .arg(pending.timeoutMs);
                if (pending.required)
                    fail(message);
                else
                    log(message);
            }
            next();
        });
        poll->start();
    });
}

void Snapshot::finish()
{
    if (m_failures > 0) {
        log(QStringLiteral("fim, com %1 falha(s)").arg(m_failures));
        QCoreApplication::exit(1);
        return;
    }
    log("fim");
    QCoreApplication::quit();
}

void Snapshot::fail(const QString &message)
{
    ++m_failures;
    log("FALHOU: " + message);
    m_steps.clear();
    then(0, [this] { finish(); });
}

QQuickWindow *Snapshot::window() const
{
    for (QObject *root : m_engine->rootObjects())
        if (auto *w = qobject_cast<QQuickWindow *>(root))
            return w;
    return nullptr;
}

/// Procura pela árvore visual: itens criados por Loader/Repeater nem sempre são
/// filhos (QObject) do item em que aparecem.
static QQuickItem *findVisualChild(QQuickItem *item, const QString &objectName)
{
    if (item->objectName() == objectName)
        return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = findVisualChild(child, objectName))
            return found;
    return nullptr;
}

QQuickItem *Snapshot::findItem(const QString &objectName) const
{
    QQuickWindow *w = window();
    return w ? findVisualChild(w->contentItem(), objectName) : nullptr;
}

void Snapshot::log(const QString &message) const
{
    std::fprintf(stderr, "[snapshot] %s\n", qPrintable(message));
    std::fflush(stderr);
}

void Snapshot::capture(const QString &name)
{
    QQuickWindow *w = window();
    if (!w)
        return;
    const QImage image = w->grabWindow();
    const QString path = m_dir.filePath(name + ".png");
    if (image.save(path))
        log(QStringLiteral("%1 (%2×%3)").arg(name).arg(image.width()).arg(image.height()));
    else
        log("falhou: " + name);
}

void Snapshot::captureItem(const QString &objectName, const QString &name, int above)
{
    QQuickWindow *w = window();
    QQuickItem *item = findItem(objectName);
    if (!w || !item)
        return;
    const QImage image = w->grabWindow();
    const qreal dpr = image.devicePixelRatio();
    QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    r.adjust(-8, -above, 8, 0);
    const QRect pixels = QRectF(r.x() * dpr, r.y() * dpr, r.width() * dpr, r.height() * dpr).toAlignedRect()
                             .intersected(image.rect());
    image.copy(pixels).save(m_dir.filePath(name + ".png"));
    log(name);
}

void Snapshot::saveFrameSheet(const QString &name, const QList<std::pair<QString, QList<QImage>>> &rows, int columns)
{
    QSize cell(1, 1);
    for (const auto &row : rows)
        for (const QImage &f : row.second)
            cell = cell.expandedTo(f.size());
    const int label = 150, gap = 6;
    QImage sheet(label + columns * (cell.width() + gap), int(rows.size()) * (cell.height() + gap),
                 QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(40, 40, 40));
    QPainter painter(&sheet);
    painter.setPen(Qt::white);
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(13);
    painter.setFont(font);
    for (qsizetype r = 0; r < rows.size(); ++r) {
        const int y = int(r) * (cell.height() + gap);
        const QList<QImage> &frames = rows[r].second;
        painter.drawText(QRect(6, y, label - 12, cell.height()), Qt::AlignVCenter | Qt::TextWordWrap,
                         rows[r].first + QStringLiteral(" (%1)").arg(frames.size()));
        for (int c = 0; c < columns && !frames.isEmpty(); ++c) {
            const QImage &f = frames[std::min<qsizetype>(qsizetype(c) * frames.size() / columns, frames.size() - 1)];
            const QRect target(label + c * (cell.width() + gap), y, cell.width(), cell.height());
            painter.fillRect(target, QColor(20, 20, 20));
            painter.drawImage(target.topLeft() + QPoint((cell.width() - f.width()) / 2, (cell.height() - f.height()) / 2), f);
        }
    }
    painter.end();
    sheet.save(m_dir.filePath(name + ".png"));
    log(name);
}

void Snapshot::scrollInspectorTo(const QString &objectName, qreal above)
{
    QQuickItem *flick = findItem(QStringLiteral("inspectorScroll"));
    QQuickItem *item = findItem(objectName);
    auto *content = flick ? flick->property("contentItem").value<QQuickItem *>() : nullptr;
    if (!content || !item)
        return;
    const qreal travel = std::max<qreal>(0, flick->property("contentHeight").toReal() - flick->height());
    flick->setProperty("contentY", std::clamp<qreal>(item->mapToItem(content, QPointF()).y() - above, 0, travel));
}

void Snapshot::scrollInspector(double fraction)
{
    QQuickItem *flick = findItem(QStringLiteral("inspectorScroll"));
    if (!flick)
        return;
    const qreal travel = std::max<qreal>(0, flick->property("contentHeight").toReal() - flick->height());
    flick->setProperty("contentY", travel * fraction);
}

// MARK: - Cenários

/// Lista, abre o projeto mais recente (ou cria um), volta e captura de novo.
void Snapshot::homeScenario()
{
    then(500, [this] { capture("inicio-1-lista"); });
    then(0, [this] {
        if (m_model->entries()->count() > 0)
            m_model->openLibraryProject(m_model->entries()->get(0).value("id").toString());
        else
            m_model->createProject("r9x16");
    });
    then(1500, [this] { capture("inicio-2-editor"); });
    then(0, [this] { m_model->showHome(); });
    then(2500, [this] { m_model->refreshLibrary(); });
    then(800, [this] { capture("inicio-3-de-volta"); });
}

/// Projeto novo com as mídias de uma pasta (a CI gera vídeo, foto e áudio de teste).
void Snapshot::mediaScenario(const QString &mediaDir)
{
    auto expected = std::make_shared<int>(0);
    then(0, [this, mediaDir, expected] {
        m_model->createProject("original");
        QList<QUrl> urls;
        QDirIterator it(mediaDir, QDir::Files);
        while (it.hasNext())
            urls << QUrl::fromLocalFile(it.next());
        std::sort(urls.begin(), urls.end());
        log(QStringLiteral("importando %1 arquivos de %2").arg(urls.size()).arg(mediaDir));
        if (urls.isEmpty())
            return fail("nenhuma mídia em " + mediaDir);
        *expected = int(urls.size());
        m_model->importMedia(urls);
    });
    waitUntil([this, expected] { return m_model->project().materials.size() >= *expected; }, 30'000,
              QStringLiteral("a importação das mídias"));
    // Folga para as miniaturas, que chegam depois dos materiais.
    then(3000, [this] {
        // Visuais na faixa principal, áudio na faixa de áudio, e um texto.
        const Project p = m_model->project();
        for (const auto &m : p.materials)
            m_model->addToTimeline(idString(m.id));
        m_model->seekMicros(500'000);
        m_model->addText("classic");
        m_model->setSelection({});
        log(QStringLiteral("projeto: %1 materiais, %2 clipes").arg(p.materials.size()).arg(m_model->clipCount()));
    });
    then(2000, [] {});
    const QString scenario = qEnvironmentVariable("OPENFRAME_SNAPSHOT_SCENARIO");
    if (scenario == "motion")
        motionScenario();
    else if (scenario == "legendas")
        captionsScenario();
    else if (scenario == "som")
        soundScenario();
    else if (scenario == "pacote")
        packageScenario();
    else if (scenario == "audio")
        audioScenario();
    else
        editorScenario();
}

void Snapshot::editorScenario()
{
    then(0, [this] { m_model->seek(1.5); });
    then(1200, [this] { capture("1-projeto"); });

    then(0, [this] {
        m_model->setSelection(firstMainSegment(m_model->project()));
        m_model->setInspectorTab("basic");
    });
    then(800, [this] { capture("2-clipe-basico"); });
    then(0, [this] { m_model->setInspectorTab("filters"); });
    then(1500, [this] { capture("3-clipe-filtros"); });
    then(0, [this] { m_model->setInspectorTab("transition"); });
    then(800, [this] { capture("4-clipe-transicao"); });

    // Texto: aba Texto, depois os recursos de estilo ligados e o inspetor rolado.
    then(0, [this] {
        m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Text));
        m_model->setInspectorTab("text");
    });
    then(800, [this] {
        if (!m_model->selection().isEmpty())
            capture("5-texto");
    });
    then(0, [this] {
        if (m_model->selection().isEmpty())
            return;
        const QList<std::pair<QString, QVariant>> values{
            {"text.style.letterSpacing", 0.12}, {"text.style.lineSpacing", 0.3}, {"text.style.underline", true},
            {"text.style.strokeWidth", 6},      {"text.style.backgroundEnabled", true},
            {"text.style.backgroundRadius", 0.5}, {"text.style.shadowAngle", -45},
            {"text.style.shadowDistance", 0.1},
        };
        for (const auto &[path, value] : values)
            m_model->setSegmentValue(path, value);
    });
    then(600, [this] { scrollInspector(0.5); });
    then(500, [this] {
        if (!m_model->selection().isEmpty())
            capture("5b-texto-rolado-1");
    });
    then(0, [this] { scrollInspector(1); });
    then(500, [this] {
        if (!m_model->selection().isEmpty())
            capture("5b-texto-rolado-2");
    });
    then(0, [this] { scrollInspector(0); });
    inspectorTabs(QStringLiteral("text"), QStringLiteral("17-inspetor-texto"));
    then(0, [this] {
        if (m_model->canUndo())
            m_model->undo();
    });
    inspectorTabs(QStringLiteral("main"), QStringLiteral("18-inspetor-clipe"));

    then(0, [this] { m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Audio)); });
    then(800, [this] {
        if (!m_model->selection().isEmpty())
            capture("7-audio");
    });

    // Apagar o clipe selecionado com o inspetor aberto (derrubava o app Swift).
    then(0, [this] {
        m_model->setSelection(firstMainSegment(m_model->project()));
        m_model->setInspectorTab("basic");
    });
    then(500, [this] { m_model->deleteSelection(); });
    then(500, [this] {
        log("apagar com o inspetor aberto: app continua aberto");
        m_model->undo();
    });

    // Barra de ações do clipe na primeira e na última faixa exibidas.
    then(300, [this] {
        const Project &p = m_model->project();
        const QList<int> order = m_model->displayTrackOrder();
        for (int i : order) {
            if (!p.tracks[i].segments.isEmpty()) {
                m_model->setSelection(idString(p.tracks[i].segments.first().id));
                break;
            }
        }
    });
    then(800, [this] { capture("10-barra-primeira-faixa"); });
    then(0, [this] {
        const Project &p = m_model->project();
        const QList<int> order = m_model->displayTrackOrder();
        if (order.size() < 2 || p.tracks[order.last()].segments.isEmpty())
            return;
        const Segment last = p.tracks[order.last()].segments.last();
        m_model->setSelection(idString(last.id));
        m_model->seekMicros(last.targetEnd() - 200'000);
    });
    then(800, [this] { capture("11-barra-ultima-faixa"); });

    // Zoom: o playhead (ou o ponto da roda) fica no mesmo lugar da tela.
    then(0, [this] {
        m_model->setSelection({});
        m_model->seek(m_model->duration() * 3 / 4);
    });
    then(400, [this] {
        const auto x = [this] { return m_model->playhead() * m_model->zoom() - m_model->timelineScrollX(); };
        const double before = x();
        m_model->setZoom(m_model->zoom() * 3);
        log(QStringLiteral("zoom ×3: %1 pt/s; playhead na tela em x=%2 → %3")
                .arg(m_model->zoom(), 0, 'f', 2).arg(before, 0, 'f', 1).arg(x(), 0, 'f', 1));
    });
    then(800, [this] { capture("12-zoom-aproximado"); });
    then(0, [this] {
        const double time = (m_model->timelineScrollX() + 200) / m_model->zoom();
        m_model->zoomAtViewportX(0.5, 200);
        log(QStringLiteral("zoom ÷2 na roda em x=200: %1 s agora em x=%2")
                .arg(time, 0, 'f', 3)
                .arg(time * m_model->zoom() - m_model->timelineScrollX(), 0, 'f', 1));
        m_model->zoomToFit();
    });
    then(800, [this] { capture("13-zoom-ajustado"); });

    // Arraste vertical: o clipe mostra a faixa de destino antes de soltar.
    then(0, [this] {
        const QString first = firstMainSegment(m_model->project());
        if (!first.isEmpty())
            m_model->dragMove(first, 120, -1);
        log("arraste: linha de destino " + QString::number(m_model->dragTargetRow()));
    });
    then(800, [this] { capture("14-arraste-outra-faixa"); });
    then(0, [this] {
        const QString dragged = m_model->dragSegment();
        if (!dragged.isEmpty())
            m_model->endDragMove(dragged);
    });
    then(800, [this] {
        capture("15-apos-soltar");
        if (m_model->canUndo())
            m_model->undo();
    });

    // Cortes e reprodução com rolagem automática.
    then(300, [this] {
        m_model->seek(0);
        for (int i = 0; i < 3; ++i)
            m_model->jumpToEdit(true);
        log("cortes: 3× próximo → " + m_model->playheadText());
        m_model->setZoom(500);
        m_model->goToStart();
        m_model->togglePlay();
    });
    then(3000, [this] {
        if (m_model->isPlaying())
            m_model->togglePlay();
        log(QStringLiteral("reprodução: playhead %1, rolagem %2").arg(m_model->playheadText()).arg(m_model->timelineScrollX()));
        capture("16-reproducao-rolagem");
        m_model->zoomToFit();
    });

    // Painéis da biblioteca e grade.
    then(300, [this] {
        m_model->setSelection({});
        m_model->setShowGrid(true);
        m_model->setLibraryPanel("text");
    });
    then(800, [this] { capture("8-painel-texto-grade"); });
    then(0, [this] {
        m_model->setShowGrid(false);
        m_model->setLibraryPanel("captions");
    });
    then(800, [this] { capture("9-painel-legendas"); });
    then(0, [this] { m_model->setLibraryPanel("shortcuts"); });
    then(800, [this] { capture("19-painel-atalhos"); });
    then(0, [this] { m_model->setLibraryPanel("media"); });
    then(800, [this] { capture("20-painel-midia"); });
    then(0, [this] { m_model->setShowExport(true); });
    then(1200, [this] { capture("6-exportar"); });

    // Exportação de verdade (480p, para ser rápida), durante e no fim.
    then(0, [this] {
        m_model->setExportValue("resolution", "p480");
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath("openframe-captura.mp4");
        QFile::remove(path);
        m_model->startExport(QUrl::fromLocalFile(path));
        log("exportando para " + path);
    });
    waitUntil([this] { return m_model->exportProgress() > 0.3 || m_model->exportPhase() != "exporting"; }, 60'000,
              QStringLiteral("30% da exportação"), false);
    then(0, [this] { capture("6b-exportando"); });
    waitUntil([this] { return m_model->exportPhase() != "exporting"; }, 300'000, QStringLiteral("o fim da exportação"));
    then(300, [this] {
        log(QStringLiteral("exportação: %1 %2%3")
                .arg(m_model->exportPhase(), m_model->exportResult(), m_model->exportError()));
        capture("6c-exportado");
        QFile::remove(m_model->exportResultPath());
        m_model->resetExport();
        m_model->setShowExport(false);
    });
}

/// Todas as abas do inspetor do segmento (texto ou primeiro da faixa principal),
/// no topo, no meio e no fim da rolagem, só a área do inspetor.
void Snapshot::inspectorTabs(const QString &segment, const QString &prefix)
{
    static const QStringList tabs{"basic", "adjust", "filters", "effects", "animation", "transition", "text", "audio"};
    for (const QString &tab : tabs) {
        then(0, [this, segment, tab] {
            const Project &p = m_model->project();
            m_model->setSelection(segment == "text" ? firstOfKind(p, TrackKind::Text) : firstMainSegment(p));
            m_model->setInspectorTab(tab);
        });
        for (int i = 0; i < 3; ++i) {
            then(i == 0 ? 700 : 0, [this, i] { scrollInspector(i * 0.5); });
            then(400, [this, tab, prefix, i] {
                if (m_model->selection().isEmpty() || m_model->inspectorTab() != tab)
                    return;
                captureItem(QStringLiteral("inspector"), QStringLiteral("%1-%2-%3").arg(prefix, tab).arg(i + 1));
            });
        }
        then(0, [this] { scrollInspector(0); });
    }
}

/// Arrastes espelhados (mesma distância para a direita e para a esquerda),
/// capturados no meio do gesto, para comparar os dois sentidos.
void Snapshot::dragScenario()
{
    then(0, [this] {
        m_model->setZoom(120);
        m_model->seek(0);
    });
    const QList<qreal> distances{-560, -260, 260, 560};
    for (qreal dx : distances) {
        then(300, [this, dx] {
            const Project &p = m_model->project();
            const auto main = p.mainTrackIndex();
            if (!main || p.tracks[*main].segments.size() < 3)
                return;
            m_model->dragMove(idString(p.tracks[*main].segments[1].id), dx, 0);
        });
        then(700, [this, dx] {
            if (m_model->dragSegment().isEmpty())
                return;
            capture(QStringLiteral("arraste-principal%1%2").arg(dx > 0 ? "+" : "").arg(int(dx)));
            m_model->endDragMove(m_model->dragSegment());
        });
        then(400, [this] {
            if (m_model->canUndo())
                m_model->undo();
        });
    }
    for (bool leading : {true, false}) {
        then(300, [this, leading] {
            const Project &p = m_model->project();
            const auto main = p.mainTrackIndex();
            if (!main || p.tracks[*main].segments.size() < 2)
                return;
            m_model->dragTrim(idString(p.tracks[*main].segments[1].id), leading, leading ? 90 : -90);
        });
        then(700, [this, leading] {
            capture(leading ? "aparar-inicio" : "aparar-fim");
            m_model->endDragTrim();
        });
        then(400, [this] {
            if (m_model->canUndo())
                m_model->undo();
        });
    }
}

// MARK: - Motion (manipulação no preview e aba Animação)

void Snapshot::canvasDrag(QPointF from, QPointF to, bool snapping, bool release)
{
    auto *overlay = qobject_cast<CanvasOverlay *>(findItem(QStringLiteral("canvasOverlay")));
    if (!overlay) {
        return fail("overlay do preview não encontrado");
    }
    const CanvasMapping mapping = overlay->mapping();
    m_model->canvasPointerDown(from, mapping);
    const int steps = 12;
    for (int i = 1; i <= steps; ++i)
        m_model->canvasPointerMoved(from + (to - from) * (double(i) / steps), mapping, snapping);
    if (release)
        m_model->canvasPointerUp();
}

void Snapshot::motionScenario()
{
    struct Probe {
        Snapshot *self;
        CanvasOverlay *overlay() const
        {
            return qobject_cast<CanvasOverlay *>(self->findItem(QStringLiteral("canvasOverlay")));
        }
        std::optional<SelectionFrame> frame() const
        {
            auto *o = overlay();
            return o ? self->m_model->selectionFrame(o->mapping()) : std::nullopt;
        }
        QString describe() const
        {
            const auto id = self->m_model->selectionId();
            const auto seg = id ? self->m_model->project().segment(*id) : std::nullopt;
            if (!seg)
                return QStringLiteral("sem seleção");
            if (seg->text) {
                const TextStyle &st = seg->text->style;
                return QStringLiteral("texto x %1 y %2 escala %3 rotação %4 maxWidth %5")
                    .arg(st.x, 0, 'f', 3).arg(st.y, 0, 'f', 3).arg(st.scale, 0, 'f', 3)
                    .arg(st.rotation, 0, 'f', 1).arg(st.maxWidth, 0, 'f', 3);
            }
            const Transform &t = seg->transform;
            return QStringLiteral("clipe x %1 y %2 escala %3 rotação %4")
                .arg(t.x, 0, 'f', 3).arg(t.y, 0, 'f', 3).arg(t.scale, 0, 'f', 3).arg(t.rotation, 0, 'f', 1);
        }
    };
    auto probe = std::make_shared<Probe>(Probe{this});
    // Passos de desfazer disponíveis (desfaz tudo e refaz de volta).
    auto countUndo = [this] {
        const QString selection = m_model->selection();
        const Micros playhead = m_model->playheadMicros();
        int n = 0;
        while (m_model->canUndo() && n < 400) {
            m_model->undo();
            ++n;
        }
        for (int i = 0; i < n; ++i)
            m_model->redo();
        m_model->seekMicros(playhead);
        m_model->setSelection(selection);
        return n;
    };

    then(0, [this] {
        m_model->seek(1.0);
        m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Text));
        m_model->setInspectorTab("text");
    });
    then(1200, [this, probe] {
        if (auto *o = probe->overlay())
            log(QStringLiteral("motion: overlay %1×%2 pt, preview com lado menor %3 px")
                    .arg(o->width()).arg(o->height()).arg(m_model->previewShortSide()));
        log("motion: " + probe->describe());
        capture("motion-1-selecao-texto");
    });
    then(0, [this] { m_model->setSelection(firstMainSegment(m_model->project())); });
    then(800, [this, probe] {
        log("motion: " + probe->describe() + (probe->frame() && probe->frame()->resizesWidth
                                                  ? ", com alças laterais" : ", sem alças laterais"));
        capture("motion-2-selecao-video");
    });

    // Giro pela alça, com a etiqueta de graus (gesto aberto na captura).
    then(0, [this, probe, countUndo] {
        m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Text));
        const int before = countUndo();
        const auto f = probe->frame();
        if (!f)
            return fail("motion: texto fora do playhead");
        const QPointF knob = f->rotationHandle();
        const QPointF to = f->center + SelectionFrame::rotate(knob - f->center, 32);
        canvasDrag(knob, to, true, false);
        log(QStringLiteral("motion: girando, etiqueta %1°, desfazer antes: %2")
                .arg(m_model->canvasRotationLabel().value_or(-999), 0, 'f', 0).arg(before));
    });
    then(500, [this] { capture("motion-3-girando"); });
    then(0, [this, probe, countUndo] {
        m_model->canvasPointerUp();
        log("motion: depois do giro, " + probe->describe() + QStringLiteral(", desfazer: %1").arg(countUndo()));
    });

    // Escala pelo canto inferior direito, para fora.
    then(300, [this, probe] {
        const auto f = probe->frame();
        if (!f)
            return;
        const QPointF corner = f->corners[2];
        canvasDrag(corner, corner + (corner - f->center) * 0.35, true, false);
    });
    then(500, [this] { capture("motion-4-escala"); });
    then(0, [this, probe, countUndo] {
        m_model->canvasPointerUp();
        log("motion: depois da escala, " + probe->describe() + QStringLiteral(", desfazer: %1").arg(countUndo()));
    });

    // Mover o corpo perto do centro (com ímã: guias quando o núcleo tiver o Snapping) e com Ctrl.
    then(300, [this, probe] {
        const auto f = probe->frame();
        auto *o = probe->overlay();
        if (!f || !o)
            return;
        const QPointF target(o->width() / 2 + 3, o->height() / 2 + 2);
        canvasDrag(f->center, target, true, false);
        log(QStringLiteral("motion: movendo com ímã, %1 guias; ").arg(m_model->canvasGuides().size()) + probe->describe());
    });
    then(500, [this] { capture("motion-5-movendo"); });
    then(0, [this, probe] {
        auto *o = probe->overlay();
        if (!o || !m_model->placementDrag())
            return;
        m_model->canvasPointerMoved(QPointF(o->width() / 2 + 3, o->height() / 2 + 2), o->mapping(), false);
        log(QStringLiteral("motion: mesmo ponto com Ctrl (sem ímã), %1 guias; ").arg(m_model->canvasGuides().size())
            + probe->describe());
        m_model->canvasPointerUp();
    });

    // Largura pela alça lateral direita: etiqueta "N%".
    then(300, [this, probe, countUndo] {
        const int before = countUndo();
        const auto f = probe->frame();
        if (!f)
            return;
        const QPointF edge = f->edgeHandle(EdgeSide::Right);
        log(QStringLiteral("motion: alça sob o ponto da direita = %1")
                .arg(f->handle(edge) && f->handle(edge)->kind == OverlayHandle::Kind::Edge ? "lateral" : "outra"));
        canvasDrag(edge, edge + f->right() * 70, true, false);
        log(QStringLiteral("motion: largura arrastando, etiqueta %1, desfazer antes %2")
                .arg(m_model->canvasWidthDrag() ? m_model->canvasWidthDrag()->label() : "-").arg(before));
    });
    then(500, [this] { capture("motion-6-largura"); });
    then(0, [this, probe, countUndo] {
        m_model->canvasPointerUp();
        log("motion: depois da largura, " + probe->describe() + QStringLiteral(", desfazer: %1").arg(countUndo()));
        m_model->undo();
        log("motion: um desfazer, " + probe->describe());
        m_model->redo();
    });

    // Duplo clique no texto abre a aba Texto; clique fora tira a seleção.
    then(300, [this, probe] {
        auto *o = probe->overlay();
        const auto f = probe->frame();
        if (!o || !f)
            return;
        const QPointF center = f->center;
        m_model->setInspectorTab("basic");
        m_model->setSelection({});
        m_model->canvasPointerDown(center, o->mapping(), 2);
        m_model->canvasPointerUp();
        log(QStringLiteral("motion: duplo clique no texto → aba %1, seleção %2")
                .arg(m_model->inspectorTab(), m_model->selection().isEmpty() ? "vazia" : "o texto"));
    });

    // Aba Animação: texto (todos os tipos) e vídeo (sem os só de texto).
    then(300, [this] {
        m_model->seek(1.0);
        m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Text));
        m_model->setInspectorTab("animation");
    });
    then(1000, [this] {
        log(QStringLiteral("motion: animações de texto %1, de vídeo %2, loops %3")
                .arg(m_model->animationOptions(true).size()).arg(m_model->animationOptions(false).size())
                .arg(m_model->loopOptions().size()));
        capture("motion-8-animacao-texto");
    });
    // Escolher toca o trecho e volta ao playhead.
    then(0, [this] {
        log(QStringLiteral("motion: escolhendo a entrada com o playhead em %1").arg(m_model->playheadText()));
        m_model->setAnimation("in", "zoomIn");
        log(QStringLiteral("motion: tocando o trecho = %1, playhead %2")
                .arg(m_model->isPlaying() ? "sim" : "não", m_model->playheadText()));
    });
    then(400, [this] { capture("motion-9-animacao-tocando"); });
    waitUntil([this] { return !m_model->isPlaying(); }, 5000, QStringLiteral("o fim do trecho da entrada"));
    then(300, [this] {
        log(QStringLiteral("motion: depois do trecho, playhead %1, entrada %2")
                .arg(m_model->playheadText(),
                     m_model->selectedSegment().value("animationIn").toMap().value("kind").toString()));
        m_model->setLoop("pulse");
    });
    waitUntil([this] { return !m_model->isPlaying(); }, 8000, QStringLiteral("o fim do trecho do loop"));
    then(300, [this] { scrollInspector(1); });
    then(500, [this] {
        log("motion: loop " + QString::fromUtf8(QJsonDocument::fromVariant(m_model->selectedSegment().value("loop"))
                                                    .toJson(QJsonDocument::Compact)));
        capture("motion-10-animacao-loop");
        scrollInspector(0);
    });
    then(0, [this] {
        m_model->setSelection(firstMainSegment(m_model->project()));
        m_model->setInspectorTab("animation");
    });
    then(800, [this] { capture("motion-11-animacao-video"); });

    // Quadros das miniaturas (os mesmos do AnimatedThumb), do texto e do vídeo.
    then(0, [this] {
        const Project p = m_model->project();
        const auto text = p.segment(QUuid::fromString(firstOfKind(p, TrackKind::Text)));
        const auto video = p.segment(QUuid::fromString(firstMainSegment(p)));
        const QList<QImage> thumbs =
            video && video->materialId ? MediaCache::instance()->thumbnails(idString(*video->materialId)) : QList<QImage>();
        QList<std::pair<QString, QList<QImage>>> rows;
        const QSize size = AnimatedThumb::pixelSize;
        for (const QString part : {"entry:fade", "entry:zoomIn", "entry:typewriter", "entry:wordByWord", "entry:cascade",
                                   "entry:decode", "exit:slideLeft", "loop:pulse", "loop:shine"})
            if (text)
                rows.append({"texto " + part, MotionCore::frames(*text, {}, part, size, part.startsWith("loop") ? 60 : 30)});
        for (const QString part : {"entry:zoomIn", "entry:spin", "loop:float"})
            if (video && !thumbs.isEmpty())
                rows.append({"vídeo " + part, MotionCore::frames(*video, thumbs.first(), part, size,
                                                                 part.startsWith("loop") ? 60 : 30)});
        int empty = 0;
        for (const auto &row : rows)
            empty += row.second.isEmpty() ? 1 : 0;
        log(QStringLiteral("motion: miniaturas, %1 tiras, %2 vazias").arg(rows.size()).arg(empty));
        if (rows.isEmpty() || empty > 0)
            return fail("motion: miniaturas sem quadros");
        saveFrameSheet(QStringLiteral("motion-12-miniaturas-quadros"), rows, 6);
    });

    // Na reprodução, o contorno some e o clique não responde.
    then(300, [this] { m_model->togglePlay(); });
    then(700, [this, probe] {
        log(QStringLiteral("motion: reproduzindo, contorno %1").arg(probe->frame() ? "visível" : "escondido"));
        capture("motion-7-reproduzindo");
        m_model->togglePlay();
    });
}

// MARK: - Som (reprodução com o QAudioSink, para a CI gravar a saída)

void Snapshot::soundScenario()
{
    auto start = std::make_shared<Micros>(0);
    then(0, [this, start] {
        m_model->setSelection({});
        m_model->seekMicros(0);
        *start = m_model->playheadMicros();
        m_model->togglePlay();
        log(QStringLiteral("som: tocando = %1").arg(m_model->isPlaying() ? "sim" : "não"));
    });
    then(3000, [this, start] {
        log(QStringLiteral("som: depois de 3 s, playhead %1 (andou %2 ms)")
                .arg(m_model->playheadText())
                .arg((m_model->playheadMicros() - *start) / 1000));
        capture("som-1-tocando");
        const Micros moved = m_model->playheadMicros() - *start;
        m_model->togglePlay();
        if (moved < 1'000'000)
            fail(QStringLiteral("som: a reprodução não andou (%1 ms em 3 s)").arg(moved / 1000));
    });
}

namespace {

/// Faixa automática de legendas, ou nada.
const Track *autoCaptionTrack(const Project &p)
{
    for (const auto &t : p.tracks)
        if (t.isAutoCaptions && !t.segments.isEmpty())
            return &t;
    return nullptr;
}

/// Legenda `index` da faixa automática (a última, se faltar), ou nada.
std::optional<Segment> autoCaption(const Project &p, int index)
{
    const Track *t = autoCaptionTrack(p);
    if (!t)
        return std::nullopt;
    return t->segments[std::min<qsizetype>(index, t->segments.size() - 1)];
}

/// Primeira legenda com U+00A0 (quebra suave), ou a segunda.
std::optional<Segment> captionWithGlue(const Project &p)
{
    if (const Track *t = autoCaptionTrack(p))
        for (const auto &s : t->segments)
            if (s.text && s.text->text.contains(QChar(0xA0)))
                return s;
    return autoCaption(p, 1);
}

} // namespace

// MARK: - Pacote (TLS e whisper de verdade, para a CI conferir o pacote extraído)

void Snapshot::packageScenario()
{
    CaptionGeneration *gen = m_model->captions();
    struct Head {
        bool done = false;
        int status = 0;
        QString error;
    };
    auto head = std::make_shared<Head>();
    then(0, [this, head] {
        // O padrão primeiro: o supportsSsl() fixa o backend, e depois dele não dá para trocar.
        log(QStringLiteral("pacote: backends [%1], padrão %2")
                .arg(QSslSocket::availableBackends().join(", "), QSslSocket::activeBackend()));
        // OPENFRAME_TLS_BACKEND força um backend (a CI do Windows prova o HTTPS só com o schannel).
        const QString forced = qEnvironmentVariable("OPENFRAME_TLS_BACKEND");
        if (!forced.isEmpty() && !QSslSocket::setActiveBackend(forced))
            return fail(QStringLiteral("pacote: o backend de TLS %1 não pôde ser ativado").arg(forced));
        log(QStringLiteral("pacote: TLS %1, ativo %2")
                .arg(QSslSocket::supportsSsl() ? "sim" : "não", QSslSocket::activeBackend()));
        // O mesmo endereço que o ModelStore baixa; HEAD, para não trazer o arquivo.
        auto *network = new QNetworkAccessManager(this);
        QNetworkRequest request(CaptionsCore::modelUrl(QStringLiteral("base")));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = network->head(request);
        connect(reply, &QNetworkReply::finished, this, [reply, head] {
            head->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            head->error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
            head->done = true;
            reply->deleteLater();
        });
    });
    waitUntil([head] { return head->done; }, 60'000, QStringLiteral("a resposta HTTPS do servidor do modelo"));
    then(0, [this, head] {
        log(QStringLiteral("pacote: HTTPS do modelo, HTTP %1 %2").arg(head->status).arg(head->error));
        if (!head->error.isEmpty() || head->status != 200)
            fail(QStringLiteral("pacote: o HTTPS do modelo falhou (HTTP %1, %2)").arg(head->status).arg(head->error));
    });

    // Transcrição de verdade com o primeiro modelo instalado (a CI tem o base).
    then(0, [this, gen] {
        QString installed;
        for (const auto &m : CaptionsCore::models())
            if (installed.isEmpty() && gen->engine()->isInstalled(m.id))
                installed = m.id;
        if (installed.isEmpty())
            return fail(QStringLiteral("pacote: nenhum modelo instalado em %1")
                            .arg(qEnvironmentVariable("OPENFRAME_MODELS_DIR", QStringLiteral("(pasta padrão)"))));
        m_model->setSelection({});
        m_model->presentAutoCaptions();
        gen->setLanguage(QStringLiteral("pt"));
        gen->setModel(installed);
        gen->setPreset(QStringLiteral("phrases"));
        gen->setReplacePrevious(false);
        log("pacote: transcrevendo com o modelo " + installed);
        m_model->generateCaptions();
    });
    waitUntil([gen] { return !gen->running(); }, 300'000, QStringLiteral("a transcrição"));
    then(300, [this, gen] {
        const Track *track = autoCaptionTrack(m_model->project());
        QStringList texts;
        if (track)
            for (const auto &s : track->segments)
                if (s.text)
                    texts << QString(s.text->text).replace(u'\n', u' ').replace(QChar(0xA0), u' ');
        log(QStringLiteral("pacote: fase %1, %2 legendas: \"%3\"").arg(gen->phase()).arg(texts.size())
                .arg(texts.join(QStringLiteral(" | "))));
        capture("pacote-1-transcricao");
        if (gen->phase() != QLatin1String("finished") || texts.isEmpty())
            fail(QStringLiteral("pacote: a transcrição falhou (fase %1, erro \"%2\")").arg(gen->phase(), gen->error()));
    });
}

// MARK: - Legendas automáticas (janela, estilos no preview, painel e inspetor)


void Snapshot::captionsScenario()
{
    CaptionGeneration *gen = m_model->captions();
    // A segunda legenda, a 45% dela: no meio da fala, com palavras antes e depois.
    auto seekMidCaption = [this] {
        const auto seg = autoCaption(m_model->project(), 1);
        if (!seg)
            return fail("legendas: sem faixa automática");
        m_model->seekMicros(seg->targetStart + seg->targetDuration() * 45 / 100);
    };
    // Modelo instalado: gera sem baixar. O cenário nunca baixa nada (o download é de verdade
    // desde a etapa 3); os estados do download são simulados.
    auto installed = std::make_shared<QString>();
    auto generate = [this, gen, installed](const QString &preset) {
        gen->setModel(*installed);
        gen->setPreset(preset);
        gen->setReplacePrevious(m_model->hasAutoCaptions());
        m_model->generateCaptions();
    };
    // A geração tem que terminar com legendas numa faixa automática; senão, as
    // capturas seguintes sairiam vazias.
    auto awaitGeneration = [this, gen](const QString &preset) {
        waitUntil([gen] { return !gen->running(); }, 10'000, "a geração " + preset);
        then(0, [this, gen, preset] {
            if (gen->phase() != QLatin1String("finished") || !autoCaptionTrack(m_model->project()))
                fail(QStringLiteral("legendas: a geração %1 falhou (fase %2, erro \"%3\")")
                         .arg(preset, gen->phase(), gen->error()));
        });
    };

    // Janela: opções, modelos e os cartões dos estilos. Sem o texto padrão do projeto,
    // que ficaria por cima das legendas nas capturas.
    then(0, [this, gen, installed] {
        for (const auto &m : CaptionsCore::models())
            if (installed->isEmpty() && gen->engine()->isInstalled(m.id))
                *installed = m.id;
        log(QStringLiteral("legendas: modelo instalado \"%1\"").arg(*installed));
        if (installed->isEmpty())
            return fail(QStringLiteral("legendas: nenhum modelo instalado em %1; o cenário gera com um modelo "
                                       "instalado (OPENFRAME_MODELS_DIR) e a transcrição falsa")
                            .arg(qEnvironmentVariable("OPENFRAME_MODELS_DIR", QStringLiteral("(pasta padrão)"))));
        m_model->deleteCaption(firstOfKind(m_model->project(), TrackKind::Text));
        m_model->setSelection({});
        m_model->presentAutoCaptions();
        gen->setLanguage(QStringLiteral("pt"));
        log(QStringLiteral("legendas: modelo sugerido %1, problema da fonte \"%2\", fundo dos cartões \"%3\"")
                .arg(gen->model(), m_model->captionSourceProblem(), m_model->captionBackdrop()));
    });
    then(1200, [this] { capture("legendas-1-janela"); });
    auto scrollForm = [this] {
        if (QQuickItem *form = findItem(QStringLiteral("captionsForm")))
            form->setProperty("contentY", std::max<qreal>(0, form->property("contentHeight").toReal() - form->height()));
    };
    then(0, [gen] { gen->setPreset(QStringLiteral("highlight")); });
    then(400, [this, gen, scrollForm] {
        scrollForm();
        log(QStringLiteral("legendas: estilo %1, usa a cor do destaque = %2")
                .arg(gen->preset(), gen->usesAccent() ? "sim" : "não"));
    });
    then(800, [this] { capture("legendas-2-estilos"); });
    then(0, [this, gen, scrollForm] {
        gen->setPreset(QStringLiteral("cinema"));
        scrollForm();
        log(QStringLiteral("legendas: estilo %1, usa a cor do destaque = %2")
                .arg(gen->preset(), gen->usesAccent() ? "sim" : "não"));
    });
    then(500, [this] { capture("legendas-3-estilo-sem-destaque"); });

    // Estados: baixando e erro do download (simulados, sem rede), gerando, pronto.
    then(0, [gen] {
        gen->setIncludesDownload(true);
        gen->setPhase(QStringLiteral("downloading"), 0.42);
    });
    then(500, [this] { capture("legendas-4-baixando"); });
    then(0, [gen] {
        gen->setResult(0, QStringLiteral("O download veio incompleto (15 de 488 MB). Tente de novo."));
    });
    then(500, [this, gen] {
        log(QStringLiteral("legendas: fase %1, erro (simulado) = %2").arg(gen->phase(), gen->error()));
        capture("legendas-5-erro");
        gen->backToOptions();
    });
    then(0, [generate] { generate(QStringLiteral("highlight")); });
    waitUntil([gen] { return gen->phase() == "transcribing"; }, 3000, QStringLiteral("a fase transcrevendo"));
    then(0, [this, gen] {
        log(QStringLiteral("legendas: %1, %2%").arg(gen->phase()).arg(qRound(gen->progress() * 100)));
        capture("legendas-6-transcrevendo");
    });
    awaitGeneration(QStringLiteral("highlight"));
    then(300, [this, gen] {
        log(QStringLiteral("legendas: fase %1, %2 legendas, erro \"%3\"")
                .arg(gen->phase()).arg(gen->resultCount()).arg(gen->error()));
        capture("legendas-7-pronto");
    });

    // Cancelar no meio volta às opções e não mexe no projeto.
    then(0, [this, generate] {
        const int before = autoCaptionTrack(m_model->project()) ? autoCaptionTrack(m_model->project())->segments.size() : 0;
        generate(QStringLiteral("punch"));
        m_model->cancelCaptionGeneration();
        log(QStringLiteral("legendas: cancelado, fase %1, legendas %2 → %3")
                .arg(m_model->captions()->phase()).arg(before)
                .arg(autoCaptionTrack(m_model->project()) ? autoCaptionTrack(m_model->project())->segments.size() : 0));
    });

    // Cada estilo no preview, no meio da fala (a substituir as anteriores).
    const auto presets = CaptionsCore::presets();
    for (qsizetype i = 0; i < presets.size(); ++i) {
        const QString id = presets[i].id;
        const QString name = QStringLiteral("legendas-estilo-%1-%2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(id);
        then(400, [generate, id] { generate(id); });
        awaitGeneration(id);
        then(0, [this, seekMidCaption] {
            m_model->closeAutoCaptions();
            m_model->setSelection({});
            seekMidCaption();
        });
        then(900, [this, id, name] {
            const Track *t = autoCaptionTrack(m_model->project());
            log(QStringLiteral("legendas: %1, %2 legendas na faixa, %3 faixas automáticas")
                    .arg(id).arg(t ? t->segments.size() : 0)
                    .arg(std::count_if(m_model->project().tracks.begin(), m_model->project().tracks.end(),
                                       [](const Track &k) { return k.isAutoCaptions; })));
            captureItem(QStringLiteral("canvasOverlay"), name);
        });
        then(0, [this] { m_model->presentAutoCaptions(); });
    }
    // Painel Legendas: lista, edição em linha (com o U+00A0, que o Frases usa) e a lixeira.
    then(400, [generate] { generate(QStringLiteral("phrases")); });
    awaitGeneration(QStringLiteral("phrases"));
    then(300, [this] {
        m_model->closeAutoCaptions();
        m_model->setLibraryPanel(QStringLiteral("captions"));
        m_model->setLibraryVisible(true);
        const auto seg = captionWithGlue(m_model->project());
        if (!seg)
            return;
        m_model->setSelection(ProjectStore::uuidString(seg->id));
        if (QQuickItem *panel = findItem(QStringLiteral("captionsPanel")))
            panel->setProperty("editingId", ProjectStore::uuidString(seg->id));
    });
    then(800, [this] {
        capture("legendas-8-painel-editando");
        const auto seg = captionWithGlue(m_model->project());
        if (!seg || !seg->text)
            return;
        const QString id = ProjectStore::uuidString(seg->id);
        const QString before = seg->text->text;
        m_model->setCaptionText(id, QStringLiteral("  ") + before + QStringLiteral(" (editada) "));
        const QString after = m_model->project().segment(seg->id)->text->text;
        log(QStringLiteral("legendas: editada \"%1\" → \"%2\", U+00A0 %3 → %4")
                .arg(QString(before).replace(QChar(0xA0), u'·').replace(u'\n', u'/'),
                     QString(after).replace(QChar(0xA0), u'·').replace(u'\n', u'/')).arg(before.count(QChar(0xA0))).arg(after.count(QChar(0xA0))));
        if (QQuickItem *panel = findItem(QStringLiteral("captionsPanel")))
            panel->setProperty("editingId", QString());
        const int count = autoCaptionTrack(m_model->project())->segments.size();
        const auto last = autoCaption(m_model->project(), 999);
        m_model->deleteCaption(ProjectStore::uuidString(last->id));
        log(QStringLiteral("legendas: lixeira, %1 → %2 legendas")
                .arg(count).arg(autoCaptionTrack(m_model->project())->segments.size()));
        m_model->setSelection(id);
    });
    then(600, [this] { capture("legendas-9-painel"); });

    // Inspetor da legenda: o destaque com a revelação (Leitura guiada), depois a
    // palavra-chave e a largura da caixa (Editorial).
    auto inspectCaption = [this, generate, awaitGeneration, seekMidCaption](const QString &preset) {
        then(0, [this, generate, preset] {
            m_model->presentAutoCaptions();
            generate(preset);
        });
        awaitGeneration(preset);
        then(0, [this, seekMidCaption] {
            m_model->closeAutoCaptions();
            m_model->setLibraryVisible(false);
            seekMidCaption();
            if (const auto seg = autoCaption(m_model->project(), 1))
                m_model->setSelection(ProjectStore::uuidString(seg->id));
            m_model->setInspectorTab("text");
        });
    };
    auto describeText = [this](const QString &key) {
        return QString::fromUtf8(
            QJsonDocument::fromVariant(m_model->selectedSegment().value("text").toMap().value(key))
                .toJson(QJsonDocument::Compact));
    };
    inspectCaption(QStringLiteral("guided"));
    then(800, [this, describeText] {
        const QVariantMap seg = m_model->selectedSegment();
        log(QStringLiteral("legendas: selecionada, legenda automática = %1, palavras %2, destaque ativo = %3, "
                           "destaque %4")
                .arg(seg.value("isAutoCaption").toBool() ? "sim" : "não")
                .arg(seg.value("wordList").toStringList().join(u'|'))
                .arg(seg.value("highlightActive").toBool() ? "sim" : "não")
                .arg(describeText("highlight")));
        log(QStringLiteral("legendas: revelações %1").arg(m_model->revealOptions().size()));
        scrollInspectorTo(QStringLiteral("highlightSection"), 40);
    });
    then(600, [this] { capture("legendas-10-inspetor-destaque"); });

    inspectCaption(QStringLiteral("editorial"));
    then(800, [this, describeText] {
        log("legendas: ênfase " + describeText("emphasis"));
        m_model->toggleEmphasisWord(0);
        log("legendas: ênfase depois de ligar a palavra 0: " + describeText("emphasis"));
        scrollInspectorTo(QStringLiteral("emphasisSection"), 40);
    });
    then(600, [this] { capture("legendas-11-inspetor-palavra-chave"); });
    then(0, [this] { scrollInspector(1); });
    then(600, [this] { capture("legendas-12-inspetor-largura"); });

    // Largura pela alça do preview: "N% · todas as legendas", e vale para a faixa toda.
    then(0, [this] {
        auto *o = qobject_cast<CanvasOverlay *>(findItem(QStringLiteral("canvasOverlay")));
        const auto f = o ? m_model->selectionFrame(o->mapping()) : std::nullopt;
        if (!f)
            return fail("legendas: sem contorno na legenda");
        // Até 30%: o texto passa de 2 para 3 linhas e a quebra muda de verdade. A largura
        // anda 2 × deslocamento da alça ÷ (largura do quadro × escala), a partir da da faixa.
        const auto seg = m_model->project().segment(*m_model->selectionId());
        if (!seg || !seg->text)
            return;
        const TextStyle &style = seg->text->style;
        const QPointF edge = f->edgeHandle(EdgeSide::Right);
        canvasDrag(edge, edge + f->right() * ((0.3 - style.maxWidth) / 2 * o->width() * style.scale), true, false);
        log(QStringLiteral("legendas: largura arrastando, etiqueta \"%1\"")
                .arg(m_model->canvasWidthDrag() ? m_model->canvasWidthDrag()->label() : "-"));
    });
    then(500, [this] { capture("legendas-13-largura-alca"); });
    then(0, [this] {
        m_model->canvasPointerUp();
        QStringList widths;
        if (const Track *t = autoCaptionTrack(m_model->project()))
            for (const auto &s : t->segments)
                if (s.text && !widths.contains(QString::number(s.text->style.maxWidth, 'f', 3)))
                    widths << QString::number(s.text->style.maxWidth, 'f', 3);
        log("legendas: larguras na faixa depois do gesto: " + widths.join(", "));
    });
    then(600, [this] { capture("legendas-14-depois-da-largura"); });

    // Quadros das prévias dos cartões (as mesmas tiras da janela), na proporção do projeto.
    then(0, [this, gen] {
        const AspectRatio aspect = CaptionsCore::Preview::nearestAspect(m_model->project().aspectValue());
        QList<std::pair<QString, QList<QImage>>> rows;
        int empty = 0;
        for (const auto &p : CaptionsCore::presets()) {
            const auto accent = p.usesAccent ? std::optional<QColor>(gen->highlightColor()) : std::nullopt;
            rows.append({p.name, CaptionsCore::Preview::frames(p.id, accent, aspect)});
            empty += rows.last().second.isEmpty() ? 1 : 0;
        }
        log(QStringLiteral("legendas: prévias dos cartões, %1 tiras, %2 vazias").arg(rows.size()).arg(empty));
        if (empty > 0)
            return fail("legendas: prévias dos cartões sem quadros");
        saveFrameSheet(QStringLiteral("legendas-15-cartoes-quadros"), rows, 6);
    });

    // "Aplicar este estilo a todas": copia o destaque (reveal/dimOpacity) e o estilo da ênfase, sem os índices.
    then(0, [this] {
        m_model->setSegmentValue(QStringLiteral("text.style.fontSize"), 0.09);
        m_model->applyStyleToAutoCaptions();
        int same = 0, total = 0;
        if (const Track *t = autoCaptionTrack(m_model->project()))
            for (const auto &s : t->segments)
                if (s.text) {
                    ++total;
                    same += qFuzzyCompare(s.text->style.fontSize, 0.09) ? 1 : 0;
                }
        log(QStringLiteral("legendas: aplicar a todas, fontSize 0,09 em %1 de %2").arg(same).arg(total));
    });
}

/// Linhas da timeline com o topo e a altura de cada uma, no log.
void Snapshot::describeRows(const QString &step)
{
    const Project &p = m_model->project();
    const QList<int> order = m_model->displayTrackOrder();
    QStringList items;
    for (int row = 0; row < order.size(); ++row) {
        const Track &t = p.tracks[order[row]];
        items << QStringLiteral("%1%2 topo %3 altura %4")
                     .arg(t.kind == TrackKind::Text ? "text" : t.kind == TrackKind::Audio ? "audio" : "video",
                          t.isMain ? "*" : "")
                     .arg(m_model->rowTop(row))
                     .arg(m_model->rowHeightOf(t));
    }
    log(QStringLiteral("alturas %1: %2; conteúdo %3 pt; menu marca %4")
            .arg(step, items.join("; "))
            .arg(m_model->rowsHeight())
            .arg(m_model->commonRowHeight()));
}

/// Áudio na timeline: faixas em alturas diferentes (todas compactas, normais e muito altas; misturadas),
/// com miniaturas, forma de onda e texto acompanhando a altura; os limites da borda e o arraste de um
/// clipe achando a linha pela geometria com alturas diferentes.
void Snapshot::audioScenario()
{
    const auto trackOf = [this](auto pick) -> QString {
        for (const auto &t : m_model->project().tracks)
            if (pick(t))
                return idString(t.id);
        return {};
    };
    // Timeline maximizada durante o cenário (a altura de antes volta no fim).
    const auto toggleTimeline = [this] {
        if (QQuickItem *editor = findItem("editor"))
            QMetaObject::invokeMethod(editor, "toggleMaximized");
    };
    then(0, toggleTimeline);
    then(0, [this] {
        // Uma sobreposição (vídeo sobre vídeo), para arrastar entre faixas.
        const Project &p = m_model->project();
        for (const auto &m : p.materials)
            if (m.kind == MaterialKind::Video) {
                m_model->seekMicros(2'000'000);
                m_model->addAsOverlay(idString(m.id));
                break;
            }
        m_model->setSelection({});
        m_model->zoomToFit();
        m_model->seek(1.5);
    });
    then(1500, [this] {
        describeRows("padrão");
        capture("audio-1-normal");
        m_model->setAllRowHeights(36);
    });
    then(1000, [this] {
        describeRows("compacta");
        capture("audio-2-compacta");
        m_model->setAllRowHeights(140);
    });
    then(1000, [this] {
        describeRows("muito alta");
        capture("audio-3-muito-alta");
        // Misturadas: a principal muito alta (miniaturas com o som embaixo), o texto alto, o áudio
        // compacto e a sobreposição normal; com o vídeo principal selecionado.
        m_model->undo();
        describeRows("desfazer o menu (volta à compacta)");
        if (m_model->commonRowHeight() != 36)
            return fail("o desfazer do menu não voltou à compacta");
        m_model->setAllRowHeights(0);
        // Cada faixa num gesto da borda (vários passos, um desfazer).
        for (const auto &t : m_model->project().tracks) {
            const QList<qreal> steps = t.kind == TrackKind::Text    ? QList<qreal>{70, 90}
                                     : t.kind == TrackKind::Audio ? QList<qreal>{40, 36}
                                                                    : QList<qreal>{};
            for (qreal h : steps)
                m_model->resizeRow(idString(t.id), h);
            m_model->endRowResize();
        }
        const QString main = idString(m_model->project().tracks[*m_model->project().mainTrackIndex()].id);
        for (qreal h : {80, 110, 140})
            m_model->resizeRow(main, h);
        m_model->endRowResize();
        m_model->undo();
        const qreal undone = m_model->rowHeightOf(m_model->project().tracks[*m_model->project().mainTrackIndex()]);
        log(QStringLiteral("borda: um desfazer depois do arraste 80 → 110 → 140 deixa a principal em %1").arg(undone));
        if (undone != Track::defaultHeight)
            return fail("o desfazer da borda não voltou à altura padrão");
        m_model->redo();
        m_model->setSelection(firstMainSegment(m_model->project()));
    });
    then(1200, [this] {
        describeRows("misturadas");
        capture("audio-4-misturadas");
    });
    then(0, [this, trackOf] {
        // Áudio alto: a forma de onda ocupa o clipe abaixo do nome.
        m_model->resizeRow(trackOf([](const Track &t) { return t.kind == TrackKind::Audio; }), 120);
        m_model->endRowResize();
        m_model->setSelection(firstOfKind(m_model->project(), TrackKind::Audio));
    });
    then(1000, [this, trackOf] {
        capture("audio-5-audio-alto");
        // Borda da faixa: limites e duplo clique.
        const QString text = trackOf([](const Track &t) { return t.kind == TrackKind::Text; });
        const auto height = [this, text] {
            for (const auto &t : m_model->project().tracks)
                if (idString(t.id) == text)
                    return m_model->rowHeightOf(t);
            return qreal(-1);
        };
        m_model->resizeRow(text, 10);
        log(QStringLiteral("borda: pedir 10 → %1").arg(height()));
        m_model->resizeRow(text, 500);
        log(QStringLiteral("borda: pedir 500 → %1").arg(height()));
        m_model->endRowResize();
        m_model->resizeRow(text, 0);
        m_model->endRowResize();
        log(QStringLiteral("borda: duplo clique → %1 (padrão)").arg(height()));
        if (height() != Track::defaultHeight)
            return fail("a borda não voltou à altura padrão");
        m_model->resizeRow(text, 90);
        m_model->endRowResize();
    });
    // Arraste da sobreposição: para o meio e para o fim da principal (muito alta), e sobre a régua.
    const QList<std::pair<QString, int>> targets{{"meio da principal", 0}, {"fim da principal", 1}, {"sobre a régua", 2}};
    auto tracksBefore = std::make_shared<QList<Track>>();
    for (const auto &[name, which] : targets) {
        then(0, [this, name, which, tracksBefore] {
            *tracksBefore = m_model->project().tracks;
            const Project &p = m_model->project();
            const QList<int> order = m_model->displayTrackOrder();
            int row = -1;
            for (int i = 0; i < order.size(); ++i)
                if (p.tracks[order[i]].kind == TrackKind::Video && !p.tracks[order[i]].isMain)
                    row = i;
            if (row < 0 || p.tracks[order[row]].segments.isEmpty())
                return fail("sem sobreposição para arrastar");
            const qreal y = which == 0 ? m_model->rowTop(row + 1) + 70 : which == 1 ? m_model->rowTop(row + 2) - 1 : 10;
            const int rows = m_model->rowAt(y) - row;
            m_model->dragMove(idString(p.tracks[order[row]].segments.first().id), 0, rows);
            log(QStringLiteral("arraste para %1 (y %2): linhas %3, destino %4")
                    .arg(name).arg(y).arg(rows).arg(m_model->dragTargetRow()));
            const int expected = which == 2 ? -1 : row + 1;
            if (m_model->dragTargetRow() != expected)
                return fail(QStringLiteral("destino %1, esperado %2").arg(m_model->dragTargetRow()).arg(expected));
        });
        then(600, [this, which, tracksBefore] {
            if (which == 0)
                capture("audio-6-arraste-alturas");
            const QString dragged = m_model->dragSegment();
            if (!dragged.isEmpty())
                m_model->endDragMove(dragged);
            if (m_model->project().tracks != *tracksBefore)
                m_model->undo();
        });
        then(300, [] {});
    }
    extractScenario();
    if (qEnvironmentVariable("OPENFRAME_FAKE_RECORDING") == "1")
        recordScenario();
    else
        then(0, [this] { log("gravação pulada: rode com OPENFRAME_FAKE_RECORDING=1"); });
    then(0, toggleTimeline);
}

/// Extrair o áudio do primeiro clipe da principal (faixas altas, para ver a forma de onda sumir): timeline e aba
/// de som antes e depois, e um desfazer que volta tudo.
void Snapshot::extractScenario()
{
    auto before = std::make_shared<Project>();
    auto video = std::make_shared<QString>();
    then(0, [this, video] {
        m_model->setAllRowHeights(90);
        m_model->zoomToFit();
        *video = firstMainSegment(m_model->project());
        m_model->setSelection(*video);
        m_model->setInspectorTab("basic");
    });
    then(1200, [this] {
        log(QStringLiteral("extrair: pode %1, materiais %2")
                .arg(m_model->selectedSegment().value("canExtractAudio").toBool())
                .arg(m_model->project().materials.size()));
        if (!m_model->selectedSegment().value("canExtractAudio").toBool())
            return fail("o primeiro clipe da principal não pode ter o áudio extraído");
        capture("audio-7-antes-de-extrair");
        scrollInspectorTo(QStringLiteral("audioControls"), 10);
    });
    then(500, [this, before] {
        captureItem(QStringLiteral("inspector"), QStringLiteral("audio-7b-aba-som-antes"));
        scrollInspector(0);
        *before = m_model->project();
        m_model->extractAudio();
    });
    then(1500, [this, video] {
        const Project &p = m_model->project();
        const auto sound = QUuid::fromString(m_model->selection());
        const auto seg = p.segment(sound);
        const auto original = p.segment(QUuid::fromString(*video));
        const Material *m = seg ? p.material(seg->materialId) : nullptr;
        if (!seg || !m || m->kind != MaterialKind::Audio || !original || !original->audioDetached)
            return fail("extrair não selecionou um áudio novo, ou o vídeo não ficou sem som");
        log(QStringLiteral("extrair: selecionado o áudio novo \"%1\", início %2 = %3; vídeo audioDetached %4, "
                           "pode extrair de novo %5, materiais %6")
                .arg(m->name, Time::format(seg->targetStart), Time::format(original->targetStart))
                .arg(original->audioDetached)
                .arg(TimelineEditor::canExtractAudio(p, original->id))
                .arg(p.materials.size()));
        capture("audio-8-depois-de-extrair");
        m_model->setSelection(*video);
        m_model->setInspectorTab("basic");
    });
    then(800, [this] { scrollInspectorTo(QStringLiteral("audioControls"), 10); });
    then(500, [this, before] {
        capture("audio-9-video-extraido");
        captureItem(QStringLiteral("inspector"), QStringLiteral("audio-9b-aba-som-extraido"));
        scrollInspector(0);
        m_model->undo();
        log(QStringLiteral("extrair: um desfazer volta tudo %1").arg(m_model->project() == *before));
        if (!(m_model->project() == *before))
            return fail("o desfazer não voltou o projeto de antes de extrair");
        m_model->redo();
        m_model->setSelection({});
    });
}

/// Gravar com a voz sintética (`OPENFRAME_FAKE_RECORDING=1`): o painel pronto, Esc na contagem, a contagem,
/// gravando (o clipe vermelho cresce e a timeline toca muda), o clipe no fim e os erros de permissão e de
/// microfone.
void Snapshot::recordScenario()
{
    RecordingSession *rec = m_model->recording();
    auto audioTracks = [this] {
        int n = 0;
        for (const auto &t : m_model->project().tracks)
            n += t.kind == TrackKind::Audio;
        return n;
    };
    auto tracksBefore = std::make_shared<int>(0);
    then(0, [this] {
        m_model->setAllRowHeights(0);
        m_model->seek(2.0);
        m_model->presentRecorder();
    });
    then(2000, [this, rec] {
        QStringList names;
        for (const QVariant &v : rec->inputs())
            names << v.toMap().value("name").toString();
        log(QStringLiteral("gravar: microfones [%1] (%2), fase %3, nível %4")
                .arg(names.join(", "), rec->inputName(), rec->phase())
                .arg(rec->level(), 0, 'f', 2));
        if (rec->phase() != "ready")
            return fail("o painel de gravação não ficou pronto: " + rec->error());
        capture("audio-10-gravar-pronto");
        m_model->startRecording();
    });
    then(500, [this, rec] {
        m_model->cancelRecording();
        log("gravar: Esc na contagem → " + rec->phase());
        if (rec->phase() != "ready")
            return fail("cancelar a contagem não voltou ao painel");
        m_model->startRecording();
    });
    then(400, [this, tracksBefore, audioTracks] {
        capture("audio-11-contagem");
        *tracksBefore = audioTracks();
    });
    then(3000, [this, rec] {
        log(QStringLiteral("gravar: fase %1, tocando %2, linha de destino %3 de %4")
                .arg(rec->phase())
                .arg(m_model->isPlaying())
                .arg(rec->row())
                .arg(m_model->displayTrackOrder().size()));
        if (rec->phase() != "recording")
            return fail("a gravação não começou depois da contagem: " + rec->error());
    });
    then(2500, [this, rec] {
        log(QStringLiteral("gravar: %1 gravados, %2 picos, playhead %3")
                .arg(rec->durationText())
                .arg(MediaCache::instance()->peaks(RecordingSession::peaksId).size())
                .arg(m_model->playheadText()));
        capture("audio-12-gravando");
        m_model->stopRecording();
    });
    waitUntil([rec] { return rec->phase() == "ready" && !rec->showPanel(); }, 10'000,
              QStringLiteral("a gravação entrar na timeline"));
    then(800, [this, tracksBefore, audioTracks] {
        const Project &p = m_model->project();
        const auto seg = p.segment(QUuid::fromString(m_model->selection()));
        const Material *m = seg ? p.material(seg->materialId) : nullptr;
        if (!seg || !m)
            return fail("a gravação não ficou selecionada na timeline");
        log(QStringLiteral("gravar: \"%1\" %2 na biblioteca, clipe em %3, faixas de áudio %4 → %5, arquivo %6")
                .arg(m->name, Time::format(m->duration), Time::format(seg->targetStart))
                .arg(*tracksBefore)
                .arg(audioTracks())
                .arg(m->path));
        capture("audio-13-gravado");
        const int materials = int(p.materials.size());
        m_model->undo();
        log(QStringLiteral("gravar: um desfazer tira o clipe e o material (%1 → %2 materiais)")
                .arg(materials)
                .arg(m_model->project().materials.size()));
        m_model->redo();
    });
    const QList<std::pair<int, QString>> errors{{14, "sem-permissao"}, {15, "sem-microfone"}};
    for (const auto &[index, mode] : errors) {
        then(0, [this, mode] {
            qputenv("OPENFRAME_FAKE_RECORDING", mode.toUtf8());
            m_model->presentRecorder();
        });
        then(1500, [this, rec, index, mode] {
            log(QStringLiteral("gravar %1: %2 \"%3\"").arg(mode, rec->phase(), rec->error()));
            if (rec->phase() != "failed")
                return fail("sem o erro de " + mode);
            capture(QStringLiteral("audio-%1-gravar-%2").arg(index).arg(mode));
            m_model->closeRecorder();
        });
        then(500, [] {});
    }
    then(0, [] { qputenv("OPENFRAME_FAKE_RECORDING", "1"); });
}

} // namespace of
