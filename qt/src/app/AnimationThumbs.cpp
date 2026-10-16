#include "AnimationThumbs.h"

#include "CaptionsCore.h"
#include "EditorModel.h"
#include "MediaCache.h"
#include "MotionCore.h"
#include "ProjectStore.h"

#include <QFutureWatcher>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>

namespace of {

namespace {

/// Desenha `image` encaixada (sem cortar) e centrada no item.
void drawFitted(QPainter *painter, const QImage &image, QSizeF box)
{
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const double scale = std::min(box.width() / image.width(), box.height() / image.height());
    const QSizeF size(image.width() * scale, image.height() * scale);
    painter->drawImage(QRectF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2, size.width(),
                              size.height()),
                       image);
}

} // namespace

// MARK: - AnimationStrips

AnimationStrips *AnimationStrips::instance()
{
    static AnimationStrips *strips = new AnimationStrips(qApp);
    return strips;
}

const QList<QImage> *AnimationStrips::strip(const QString &key) const
{
    const auto it = m_strips.constFind(key);
    return it == m_strips.cend() ? nullptr : &*it;
}

void AnimationStrips::request(const QString &key, bool urgent, Maker make)
{
    if (m_strips.contains(key))
        return;
    const auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const auto &item) { return item.first == key; });
    if (it != m_queue.end()) {
        if (!urgent)
            return;
        auto item = std::move(*it);
        m_queue.erase(it);
        m_queue.push_front(std::move(item));
    } else if (urgent) {
        m_queue.emplace_front(key, std::move(make));
    } else {
        m_queue.emplace_back(key, std::move(make));
    }
    work();
}

void AnimationStrips::work()
{
    if (m_working || m_queue.empty())
        return;
    m_working = true;
    auto [key, make] = std::move(m_queue.front());
    m_queue.pop_front();
    auto *watcher = new QFutureWatcher<QList<QImage>>(this);
    connect(watcher, &QFutureWatcher<QList<QImage>>::finished, this, [this, watcher, key] {
        if (m_strips.size() > 400)
            m_strips.clear();
        m_strips.insert(key, watcher->result());
        watcher->deleteLater();
        m_working = false;
        emit stripReady(key);
        work();
    });
    watcher->setFuture(QtConcurrent::run(std::move(make)));
}

// MARK: - AnimatedThumb

AnimatedThumb::AnimatedThumb(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_tick.setInterval(33);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
    connect(this, &AnimatedThumb::sourceChanged, this, [this] {
        request(false);
        update();
        emit framesChanged();
    });
    connect(this, &AnimatedThumb::playingChanged, this, [this] {
        if (m_playing) {
            request(true);
            m_clock.restart();
            m_tick.start();
        } else {
            m_tick.stop();
        }
        update();
    });
    connect(AnimationStrips::instance(), &AnimationStrips::stripReady, this, [this](const QString &key) {
        if (key == restKey() || key == partKey()) {
            update();
            emit framesChanged();
        }
    });
    if (EditorModel *model = EditorModel::instance())
        connect(model, &EditorModel::projectChanged, this, [this] {
            if (EditorModel::instance()) {
                const auto id = QUuid::fromString(m_segment);
                const auto seg = EditorModel::instance()->project().segment(id);
                const QString subject = seg ? MotionCore::subjectKey(*seg) : QString();
                if (subject != m_subject)
                    emit sourceChanged();
            }
        });
}

QString AnimatedThumb::restKey() const
{
    return m_subject.isEmpty() ? QString() : m_subject + QStringLiteral("|rest");
}

QString AnimatedThumb::partKey() const
{
    return m_subject.isEmpty() || m_part.isEmpty() ? QString() : m_subject + '|' + m_part;
}

double AnimatedThumb::cycle() const
{
    return m_part.startsWith(QStringLiteral("loop:")) ? 4.0 : 1.2;
}

void AnimatedThumb::request(bool urgent)
{
    EditorModel *model = EditorModel::instance();
    const auto seg = model ? model->project().segment(QUuid::fromString(m_segment)) : std::nullopt;
    m_subject = seg ? MotionCore::subjectKey(*seg) : QString();
    if (!seg || m_subject.isEmpty())
        return;
    QImage media;
    if (!seg->text && seg->materialId) {
        const QList<QImage> thumbs = MediaCache::instance()->thumbnails(ProjectStore::uuidString(*seg->materialId));
        if (thumbs.isEmpty())
            return;
        media = thumbs.first();
    }
    const Segment s = *seg;
    // A saída começa no repouso.
    AnimationStrips::instance()->request(restKey(), urgent, [s, media] {
        return MotionCore::frames(s, media, QStringLiteral("exit:fade"), pixelSize, 1).mid(0, 1);
    });
    if (m_part.isEmpty())
        return;
    const QString part = m_part;
    const int count = part.startsWith(QStringLiteral("loop:")) ? 60 : 30;
    AnimationStrips::instance()->request(partKey(), urgent || m_playing, [s, media, part, count] {
        return MotionCore::frames(s, media, part, pixelSize, count);
    });
}

bool AnimatedThumb::hasFrames() const
{
    const QList<QImage> *rest = AnimationStrips::instance()->strip(restKey());
    return rest && !rest->isEmpty();
}

void AnimatedThumb::paint(QPainter *painter)
{
    const QList<QImage> *frames = m_playing ? AnimationStrips::instance()->strip(partKey()) : nullptr;
    QImage image;
    if (frames && !frames->isEmpty()) {
        const double fps = frames->size() / std::max(cycle(), 0.1);
        image = frames->at(int(m_clock.elapsed() / 1000.0 * fps) % frames->size());
    } else if (const QList<QImage> *rest = AnimationStrips::instance()->strip(restKey()); rest && !rest->isEmpty()) {
        image = rest->first();
    }
    if (!image.isNull())
        drawFitted(painter, image, QSizeF(width(), height()));
}

// MARK: - CaptionPreviewStrip

namespace {

AspectRatio projectAspect()
{
    EditorModel *model = EditorModel::instance();
    return CaptionsCore::Preview::nearestAspect(model ? model->project().aspectValue() : 16.0 / 9);
}

} // namespace

CaptionPreviewStrip::CaptionPreviewStrip(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_tick.setInterval(33);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
    connect(this, &CaptionPreviewStrip::sourceChanged, this, [this] {
        request();
        update();
        emit framesChanged();
    });
    connect(this, &CaptionPreviewStrip::playingChanged, this, [this] {
        if (m_playing) {
            m_clock.restart();
            m_tick.start();
        } else {
            m_tick.stop();
        }
        update();
    });
    connect(AnimationStrips::instance(), &AnimationStrips::stripReady, this, [this](const QString &k) {
        if (k == key()) {
            update();
            emit framesChanged();
        }
    });
    // A proporção do projeto muda a segmentação e o estilo padrão.
    if (EditorModel *model = EditorModel::instance())
        connect(model, &EditorModel::projectChanged, this, [this] {
            if (!m_preset.isEmpty() && !hasFrames())
                emit sourceChanged();
        });
}

QString CaptionPreviewStrip::key() const
{
    if (m_preset.isEmpty())
        return {};
    return QStringLiteral("caption|%1|%2|%3")
        .arg(m_preset, m_usesAccent ? m_accent.name(QColor::HexArgb) : QString())
        .arg(enumKey(projectAspect()));
}

void CaptionPreviewStrip::request()
{
    const QString k = key();
    if (k.isEmpty())
        return;
    const QString preset = m_preset;
    const std::optional<QColor> accent = m_usesAccent ? std::optional<QColor>(m_accent) : std::nullopt;
    const AspectRatio aspect = projectAspect();
    AnimationStrips::instance()->request(k, true, [preset, accent, aspect] {
        return CaptionsCore::Preview::frames(preset, accent, aspect);
    });
}

bool CaptionPreviewStrip::hasFrames() const
{
    const QList<QImage> *frames = AnimationStrips::instance()->strip(key());
    return frames && !frames->isEmpty();
}

void CaptionPreviewStrip::paint(QPainter *painter)
{
    const QList<QImage> *frames = AnimationStrips::instance()->strip(key());
    if (!frames || frames->isEmpty())
        return;
    int index = std::min<int>(CaptionsCore::Preview::restIndex, int(frames->size()) - 1);
    if (m_playing) {
        const double fps = frames->size() / CaptionsCore::Preview::cycle;
        index = int(m_clock.elapsed() / 1000.0 * fps) % int(frames->size());
    }
    drawFitted(painter, frames->at(index), QSizeF(width(), height()));
}

} // namespace of
