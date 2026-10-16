#include "Player.h"

#include "AudioMix.h"
#include "Renderer.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

namespace of {

namespace {

/// Entrega a mixagem ao QAudioSink (modo pull), em float ou 16 bits, presa ao
/// relógio do vídeo (veja o `ClockedMixer`).
class MixDevice : public QIODevice
{
public:
    MixDevice(QList<AudioClip> clips, Micros start, const QElapsedTimer &clock, QAudioFormat::SampleFormat format)
        : m_mixer(std::move(clips), start, clock), m_format(format)
    {
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
    Micros skipped() const { return m_mixer.skipped(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const int bytes = m_format == QAudioFormat::Float ? 4 : 2;
        const int frames = int(maxSize / (bytes * AudioMix::channels));
        if (frames <= 0)
            return 0;
        const bool first = !m_started;
        m_started = true;
        if (m_format == QAudioFormat::Float) {
            m_mixer.read(reinterpret_cast<float *>(data), frames);
        } else {
            m_buffer.resize(size_t(frames) * AudioMix::channels);
            m_mixer.read(m_buffer.data(), frames);
            auto *out = reinterpret_cast<qint16 *>(data);
            for (size_t i = 0; i < m_buffer.size(); ++i)
                out[i] = qint16(std::lround(m_buffer[i] * 32767));
        }
        if (qEnvironmentVariableIsSet("OPENFRAME_AUDIO_LOG")) {
            if (first)
                qInfo("[openframe] som: o 1º bloco saiu %.0f ms depois do play (pulados para alcançar o vídeo)",
                      double(m_mixer.skipped()) / 1000);
            if (m_mixer.resyncs() != m_resyncs) {
                m_resyncs = m_mixer.resyncs();
                qInfo("[openframe] som: faltou som e ele ficou para trás; pulou para o vídeo (%d vez(es))", m_resyncs);
            }
        }
        return qint64(frames) * bytes * AudioMix::channels;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    ClockedMixer m_mixer;
    QAudioFormat::SampleFormat m_format;
    std::vector<float> m_buffer;
    bool m_started = false;
    int m_resyncs = 0;
};

/// Saída de som; mora na thread de áudio (a decodificação acontece lá). O
/// dispositivo e o formato ficam guardados entre um play e outro (consultá-los
/// é lento no Windows) e são refeitos quando a saída padrão do sistema muda.
class AudioOutput : public QObject
{
public:
    /// Escolhe o dispositivo e o formato antes do primeiro play.
    void prepare()
    {
        if (!m_devices) {
            m_devices = std::make_unique<QMediaDevices>();
            QObject::connect(m_devices.get(), &QMediaDevices::audioOutputsChanged, this, [this] { m_resolved = false; });
        }
        if (m_resolved)
            return;
        m_resolved = true;
        m_device = QMediaDevices::defaultAudioOutput();
        m_usable = false;
        if (m_device.isNull())
            return;
        m_format = QAudioFormat();
        m_format.setSampleRate(AudioMix::sampleRate);
        m_format.setChannelCount(AudioMix::channels);
        m_format.setSampleFormat(QAudioFormat::Float);
        if (!m_device.isFormatSupported(m_format)) {
            m_format.setSampleFormat(QAudioFormat::Int16);
            if (!m_device.isFormatSupported(m_format)) {
                qWarning("OpenFrame: a saída de som não aceita 48 kHz estéreo; tocando mudo");
                return;
            }
        }
        m_usable = true;
    }

    /// Toca a partir de `t`, com o relógio do Player que começou em `t`.
    void start(const QList<AudioClip> &clips, Micros t, const QElapsedTimer &clock)
    {
        stop();
        if (clips.isEmpty())
            return;
        prepare();
        if (!m_usable)
            return;
        m_mix = std::make_unique<MixDevice>(clips, t, clock, m_format.sampleFormat());
        m_mix->open(QIODevice::ReadOnly);
        m_sink = std::make_unique<QAudioSink>(m_device, m_format);
        // ~100 ms de buffer: folga para decodificar sem atrasar demais a resposta.
        m_sink->setBufferSize(m_format.bytesForDuration(100'000));
        m_sink->start(m_mix.get());
    }

    void stop()
    {
        if (m_sink)
            m_sink->stop();
        m_sink.reset();
        m_mix.reset();
    }

    ~AudioOutput() override { stop(); }

private:
    std::unique_ptr<QMediaDevices> m_devices;
    QAudioDevice m_device;
    QAudioFormat m_format;
    bool m_resolved = false;
    bool m_usable = false;
    std::unique_ptr<MixDevice> m_mix;
    std::unique_ptr<QAudioSink> m_sink;
};

struct RenderResult {
    QImage image;
    qint64 nanoseconds = 0;
};

} // namespace

struct Player::Impl {
    Player *q;
    Project project;
    QList<AudioClip> clips;
    Micros time = 0;
    bool playing = false;
    int shortSide = 720;
    bool audioEnabled = true;

    // Relógio: posição de partida + tempo decorrido desde o play/seek.
    QElapsedTimer clock;
    Micros clockBase = 0;
    QTimer tick;

    // Render: um quadro por vez; o pedido mais recente espera a vez.
    Renderer renderer;
    QFutureWatcher<RenderResult> watcher;
    bool rendering = false;
    std::optional<Micros> pending;
    /// Fração da resolução durante a reprodução e média do tempo de render.
    double scale = 1;
    double averageMs = 0;
    int framesSinceScale = 0;

    QThread audioThread;
    AudioOutput *audio = nullptr;

    explicit Impl(Player *player) : q(player) {}

    Micros now() const { return playing ? clockBase + clock.nsecsElapsed() / 1000 : time; }

    void requestFrame(Micros t)
    {
        if (rendering) {
            pending = t;
            return;
        }
        startRender(t);
    }

    void startRender(Micros t)
    {
        rendering = true;
        pending.reset();
        // No fim (ou além), o último quadro, como o AVPlayer.
        const Micros total = project.duration();
        const Micros at = std::clamp<Micros>(t, 0, std::max<Micros>(0, total - 1));
        const int side = playing ? std::max(120, int(shortSide * scale) / 2 * 2) : shortSide;
        const QSize size = project.canvasSize(side);
        watcher.setFuture(QtConcurrent::run([r = &renderer, p = project, at, size] {
            QElapsedTimer elapsed;
            elapsed.start();
            QImage image = r->render(p, at, size);
            return RenderResult{std::move(image), elapsed.nsecsElapsed()};
        }));
    }

    void renderFinished()
    {
        rendering = false;
        const RenderResult result = watcher.result();
        if (playing)
            adapt(double(result.nanoseconds) / 1e6);
        emit q->frameReady(result.image);
        if (pending)
            startRender(*pending);
    }

    /// Ajusta a resolução da reprodução ao tempo de render (alvo: 30 quadros/s).
    void adapt(double ms)
    {
        averageMs = averageMs == 0 ? ms : averageMs * 0.8 + ms * 0.2;
        if (++framesSinceScale < 8)
            return;
        if (averageMs > 40 && scale > 0.34) {
            scale = std::max(1.0 / 3, scale * 0.75);
            framesSinceScale = 0;
        } else if (averageMs < 16 && scale < 1) {
            scale = std::min(1.0, scale / 0.75);
            framesSinceScale = 0;
        }
    }

    void onTick()
    {
        const Micros total = project.duration();
        const Micros t = now();
        if (t >= total) {
            time = total;
            setPlaying(false);
            emit q->timeChanged(time);
            requestFrame(time);
            emit q->finished();
            return;
        }
        time = t;
        emit q->timeChanged(t);
        requestFrame(t);
    }

    void setPlaying(bool on)
    {
        if (playing == on)
            return;
        playing = on;
        if (on) {
            clockBase = time;
            clock.start();
            tick.start();
            startAudio();
        } else {
            tick.stop();
            stopAudio();
        }
        emit q->playingChanged(on);
    }

    void ensureAudioThread()
    {
        if (audio)
            return;
        audio = new AudioOutput;
        audio->moveToThread(&audioThread);
        QObject::connect(&audioThread, &QThread::finished, audio, &QObject::deleteLater);
        audioThread.start();
    }

    /// Prepara o dispositivo de som antes do primeiro play, para o som não
    /// demorar a sair (o vídeo não espera por ele).
    void prepareAudio()
    {
        if (!audioEnabled || clips.isEmpty() || audio)
            return;
        ensureAudioThread();
        QMetaObject::invokeMethod(audio, [a = audio] { a->prepare(); }, Qt::QueuedConnection);
    }

    void startAudio()
    {
        if (!audioEnabled || !playing)
            return;
        ensureAudioThread();
        // O relógio vai junto: o som alcança o vídeo quando começa a sair.
        QMetaObject::invokeMethod(audio, [a = audio, c = clips, t = clockBase, k = clock] { a->start(c, t, k); },
                                  Qt::QueuedConnection);
    }

    void stopAudio()
    {
        if (audio)
            QMetaObject::invokeMethod(audio, [a = audio] { a->stop(); }, Qt::QueuedConnection);
    }

    /// Recomeça o relógio e o som em `time` (seek tocando).
    void restartClock()
    {
        clockBase = time;
        clock.start();
        startAudio();
    }
};

Player::Player(QObject *parent) : QObject(parent), d(std::make_unique<Impl>(this))
{
    d->tick.setInterval(33);
    d->tick.setTimerType(Qt::PreciseTimer);
    connect(&d->tick, &QTimer::timeout, this, [this] { d->onTick(); });
    connect(&d->watcher, &QFutureWatcher<RenderResult>::finished, this, [this] { d->renderFinished(); });
    d->audioThread.setObjectName(QStringLiteral("OpenFrame áudio"));
}

Player::~Player()
{
    d->watcher.waitForFinished();
    if (d->audio) {
        QMetaObject::invokeMethod(d->audio, [a = d->audio] { a->stop(); }, Qt::BlockingQueuedConnection);
        d->audioThread.quit();
        d->audioThread.wait();
    }
}

void Player::setProject(const Project &project)
{
    d->project = project;
    const QList<AudioClip> clips = AudioMix::clips(project);
    if (clips != d->clips) {
        d->clips = clips;
        d->prepareAudio();
        if (d->playing) {
            d->time = d->now();
            d->restartClock();
        }
    }
    if (!d->playing) {
        const Micros total = project.duration();
        if (d->time > total) {
            d->time = total;
            emit timeChanged(d->time);
        }
        d->requestFrame(d->time);
    }
}

const Project &Player::project() const { return d->project; }

void Player::setPreviewShortSide(int shortSide)
{
    shortSide = std::max(2, shortSide);
    if (shortSide == d->shortSide)
        return;
    d->shortSide = shortSide;
    if (!d->playing)
        d->requestFrame(d->time);
}

int Player::previewShortSide() const { return d->shortSide; }

void Player::setAudioEnabled(bool enabled)
{
    if (enabled == d->audioEnabled)
        return;
    d->audioEnabled = enabled;
    if (!enabled)
        d->stopAudio();
    else if (d->playing) {
        d->time = d->now();
        d->restartClock();
    }
}

bool Player::isAudioEnabled() const { return d->audioEnabled; }

void Player::play()
{
    const Micros total = d->project.duration();
    if (d->playing || total <= 0)
        return;
    // No fim, recomeça do início (como o editor Swift).
    if (d->time >= total - 30'000) {
        d->time = 0;
        emit timeChanged(0);
    }
    d->scale = 1;
    d->averageMs = 0;
    d->framesSinceScale = 0;
    d->setPlaying(true);
    d->requestFrame(d->time);
}

void Player::pause()
{
    if (!d->playing)
        return;
    d->time = std::min(d->now(), d->project.duration());
    d->setPlaying(false);
    emit timeChanged(d->time);
    d->requestFrame(d->time);
}

void Player::seek(Micros t)
{
    d->time = std::clamp<Micros>(t, 0, d->project.duration());
    if (d->playing)
        d->restartClock();
    emit timeChanged(d->time);
    d->requestFrame(d->time);
}

Micros Player::time() const { return std::min(d->now(), d->project.duration()); }

Micros Player::duration() const { return d->project.duration(); }

bool Player::isPlaying() const { return d->playing; }

bool Player::isRendering() const { return d->rendering || d->pending.has_value(); }

} // namespace of
