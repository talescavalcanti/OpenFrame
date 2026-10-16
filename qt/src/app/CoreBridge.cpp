#include "CoreBridge.h"

#include "MediaCache.h"
#include "ProjectStore.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>

#if __has_include("MediaProbe.h")
#include "MediaProbe.h"
#define OF_HAS_MEDIA 1
#endif
#if __has_include("Thumbnails.h")
#include "Thumbnails.h"
#define OF_HAS_THUMBNAILS 1
#endif
#if __has_include("Waveform.h")
#include "Waveform.h"
#define OF_HAS_WAVEFORM 1
#endif
#if __has_include("Renderer.h")
#include "Renderer.h" // também declara EffectPreview
#define OF_HAS_RENDERER 1
#endif
#if __has_include("Player.h")
#include "Player.h"
#define OF_HAS_PLAYER 1
#endif
#if __has_include("Exporter.h")
#include "Exporter.h"
#define OF_HAS_EXPORTER 1
#endif

namespace of {

namespace Bridge {

bool isSupportedMedia(const QString &path)
{
#ifdef OF_HAS_MEDIA
    return MediaProbe::isSupported(path);
#else
    static const QStringList extensions{
        "mp4", "mov", "m4v", "mkv", "avi", "webm", "mts", "m2ts", "3gp",           // vídeo
        "jpg", "jpeg", "png", "heic", "heif", "webp", "bmp", "tif", "tiff", "gif", // foto
        "mp3", "m4a", "aac", "wav", "aif", "aiff", "flac", "ogg", "opus", "caf",   // áudio
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
#endif
}

std::optional<Material> probe(const QString &path, QString *error)
{
#ifdef OF_HAS_MEDIA
    return MediaProbe::probe(path, error);
#else
    if (error)
        *error = QStringLiteral("Não foi possível importar “%1”: a leitura de mídia ainda não está disponível "
                                "nesta compilação.").arg(QFileInfo(path).fileName());
    return std::nullopt;
#endif
}

QList<QImage> thumbnails(const Material &m)
{
#ifdef OF_HAS_THUMBNAILS
    // Nítidas também numa faixa alta; um quadro por segundo (de 10 a 30).
    return Thumbnails::frames(m, std::clamp(int(Time::toSeconds(m.duration)), 10, 30), 480);
#else
    if (m.kind == MaterialKind::Photo) {
        QImage image(m.path);
        if (!image.isNull())
            return {image.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)};
    }
    return {};
#endif
}

QVector<float> peaks(const Material &m)
{
#ifdef OF_HAS_WAVEFORM
    return Waveform::peaks(m.path, MediaCache::peaksPerSecond);
#else
    Q_UNUSED(m)
    return {};
#endif
}

QImage renderFrame(const Project &p, Micros t, QSize size)
{
#ifdef OF_HAS_RENDERER
    return Renderer::shared().render(p, t, size);
#else
    Q_UNUSED(p)
    Q_UNUSED(t)
    Q_UNUSED(size)
    return {};
#endif
}

QByteArray projectThumbnail(const Project &p)
{
#ifdef OF_HAS_RENDERER
    return ProjectThumbnail::jpeg(p);
#else
    Q_UNUSED(p)
    return {};
#endif
}

QImage filterPreview(const QImage &base, FilterPreset preset)
{
#ifdef OF_HAS_RENDERER
    return EffectPreview::filtered(base, preset);
#else
    Q_UNUSED(base)
    Q_UNUSED(preset)
    return {};
#endif
}

QJsonObject segmentJson(const Segment &s)
{
    // O núcleo grava o projeto inteiro; um projeto com só este segmento dá o JSON dele.
    Project p;
    p.tracks = {Track(TrackKind::Video, true)};
    p.tracks[0].segments = {s};
    const QJsonObject o = ProjectStore::toJson(p);
    return o.value("tracks").toArray().at(0).toObject().value("segments").toArray().at(0).toObject();
}

std::optional<Segment> segmentFromJson(const QJsonObject &segment)
{
    Project p;
    p.tracks = {Track(TrackKind::Video, true)};
    QJsonObject o = ProjectStore::toJson(p);
    QJsonArray tracks = o.value("tracks").toArray();
    QJsonObject track = tracks.at(0).toObject();
    track["segments"] = QJsonArray{segment};
    tracks[0] = track;
    o["tracks"] = tracks;
    const auto decoded = ProjectStore::fromJson(o);
    if (!decoded || decoded->tracks.isEmpty() || decoded->tracks[0].segments.isEmpty())
        return std::nullopt;
    return decoded->tracks[0].segments[0];
}

bool hasPlayer()
{
#ifdef OF_HAS_PLAYER
    return true;
#else
    return false;
#endif
}

bool hasExporter()
{
#ifdef OF_HAS_EXPORTER
    return true;
#else
    return false;
#endif
}

} // namespace Bridge

// MARK: - PreviewEngine

struct PreviewEngine::Impl {
#ifdef OF_HAS_PLAYER
    Player player;
#endif
    Project project;
    Micros time = 0;
    bool playing = false;
    QTimer stillTimer;
    int generation = 0;
    int shortSide = 720;
};

PreviewEngine::PreviewEngine(QObject *parent) : QObject(parent), d(new Impl)
{
#ifdef OF_HAS_PLAYER
    d->player.setPreviewShortSide(d->shortSide);
    connect(&d->player, &Player::frameReady, this, [](const QImage &frame) {
        MediaCache::instance()->setPreviewFrame(frame);
    });
    connect(&d->player, &Player::timeChanged, this, [this](Micros t) {
        d->time = t;
        emit timeChanged(t);
    });
    connect(&d->player, &Player::playingChanged, this, [this](bool playing) { d->playing = playing; });
    connect(&d->player, &Player::finished, this, [this] {
        d->playing = false;
        emit finished();
    });
#endif
    // Sem player: quadros parados, com um pequeno atraso para juntar alterações.
    d->stillTimer.setSingleShot(true);
    d->stillTimer.setInterval(40);
    connect(&d->stillTimer, &QTimer::timeout, this, &PreviewEngine::renderStill);
}

PreviewEngine::~PreviewEngine()
{
    delete d;
}

void PreviewEngine::setProject(const Project &p)
{
    d->project = p;
#ifdef OF_HAS_PLAYER
    d->player.setProject(p);
#else
    d->stillTimer.start();
#endif
}

void PreviewEngine::clear()
{
    d->project = Project();
    d->playing = false;
    ++d->generation;
#ifdef OF_HAS_PLAYER
    d->player.pause();
    d->player.setProject(Project());
#endif
    MediaCache::instance()->setPreviewFrame({});
}

void PreviewEngine::play()
{
#ifdef OF_HAS_PLAYER
    d->playing = true;
    d->player.play();
#endif
}

void PreviewEngine::pause()
{
#ifdef OF_HAS_PLAYER
    d->player.pause();
    d->time = d->player.time();
#endif
    d->playing = false;
}

void PreviewEngine::seek(Micros t)
{
    d->time = t;
#ifdef OF_HAS_PLAYER
    d->player.seek(t);
#else
    d->stillTimer.start();
#endif
}

void PreviewEngine::setPreviewShortSide(int side)
{
    side = std::max(2, side);
    if (side == d->shortSide)
        return;
    d->shortSide = side;
#ifdef OF_HAS_PLAYER
    d->player.setPreviewShortSide(side);
#else
    d->stillTimer.start();
#endif
}

int PreviewEngine::previewShortSide() const
{
    return d->shortSide;
}

void PreviewEngine::setAudioEnabled(bool enabled)
{
#ifdef OF_HAS_PLAYER
    d->player.setAudioEnabled(enabled);
#else
    Q_UNUSED(enabled)
#endif
}

bool PreviewEngine::isPlaying() const
{
    return d->playing;
}

Micros PreviewEngine::time() const
{
    return d->time;
}

void PreviewEngine::renderStill()
{
    if (d->project.duration() <= 0) {
        MediaCache::instance()->setPreviewFrame({});
        return;
    }
    const int generation = ++d->generation;
    const Project project = d->project;
    const Micros t = std::min(d->time, std::max<Micros>(0, project.duration() - 1));
    const QSize size = project.canvasSize(d->shortSide);
    QPointer<PreviewEngine> self(this);
    auto future = QtConcurrent::run([project, t, size] { return Bridge::renderFrame(project, t, size); });
    future.then(this, [self, generation](const QImage &image) {
        if (self && generation == self->d->generation && !image.isNull())
            MediaCache::instance()->setPreviewFrame(image);
    });
}

// MARK: - ExportJob

struct ExportJob::Impl {
#ifdef OF_HAS_EXPORTER
    Exporter exporter;
#endif
};

ExportJob::ExportJob(QObject *parent) : QObject(parent), d(new Impl)
{
#ifdef OF_HAS_EXPORTER
    connect(&d->exporter, &Exporter::progress, this, &ExportJob::progress);
    connect(&d->exporter, &Exporter::finished, this, &ExportJob::finished);
#endif
}

ExportJob::~ExportJob()
{
    delete d;
}

bool ExportJob::start(const Project &p, const ExportSettings &settings, const QString &path, QString *error)
{
#ifdef OF_HAS_EXPORTER
    Q_UNUSED(error)
    d->exporter.start(p, settings, path);
    return true;
#else
    Q_UNUSED(p)
    Q_UNUSED(settings)
    Q_UNUSED(path)
    if (error)
        *error = QStringLiteral("A exportação ainda não está disponível nesta compilação.");
    return false;
#endif
}

void ExportJob::cancel()
{
#ifdef OF_HAS_EXPORTER
    d->exporter.cancel();
#endif
}

} // namespace of
