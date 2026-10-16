#include "AudioRecorder.h"

#include "FFmpeg.h"
#include "MediaWriter.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtMath>
#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include <algorithm>
#include <cmath>
#include <vector>

namespace of {

// MARK: - RecordingWriter

struct RecordingWriter::Impl {
    QString path;
    int inputRate = sampleRate;
    std::unique_ptr<MediaWriter> writer;
    ff::SwrPtr swr;
    std::vector<float> converted;
    mutable QMutex mutex;
    qint64 written = 0;
    float level = 0;
    QList<float> peaks;
    float bucketPeak = 0;
    int bucketFill = 0;
    QString error;
    bool closed = false;

    /// Grava amostras já a 48 kHz e acumula o nível e os picos.
    bool write(const float *samples, int frames)
    {
        if (frames <= 0)
            return true;
        if (!writer->writeAudio(samples, frames)) {
            error = writer->errorString();
            return false;
        }
        written += frames;
        constexpr int bucket = sampleRate / peaksPerSecond;
        for (int i = 0; i < frames; ++i) {
            const float a = std::min(std::abs(samples[i]), 1.0f);
            level = std::max(level, a);
            bucketPeak = std::max(bucketPeak, a);
            if (++bucketFill == bucket) {
                peaks.append(bucketPeak);
                bucketPeak = 0;
                bucketFill = 0;
            }
        }
        return true;
    }

    /// Reamostra para 48 kHz (`in` nulo esvazia o que o reamostrador guarda).
    bool convert(const float *in, int frames)
    {
        const int capacity = swr_get_out_samples(swr.get(), frames);
        if (capacity <= 0)
            return true;
        converted.resize(size_t(capacity));
        auto *out = reinterpret_cast<uint8_t *>(converted.data());
        const auto *src = reinterpret_cast<const uint8_t *>(in);
        const int got = swr_convert(swr.get(), &out, capacity, in ? &src : nullptr, frames);
        return got <= 0 || write(converted.data(), got);
    }
};

RecordingWriter::RecordingWriter() : d(std::make_unique<Impl>()) {}

RecordingWriter::~RecordingWriter()
{
    if (!d->closed)
        cancel();
}

std::unique_ptr<RecordingWriter> RecordingWriter::create(const QString &path, int inputSampleRate, QString *error)
{
    const auto fail = [&](const QString &message) -> std::unique_ptr<RecordingWriter> {
        if (error)
            *error = message;
        return nullptr;
    };
    if (inputSampleRate <= 0)
        return fail(QStringLiteral("Não foi possível gravar: taxa de amostragem inválida."));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("Não foi possível criar a pasta das gravações: %1").arg(QFileInfo(path).absolutePath()));
    std::unique_ptr<RecordingWriter> w(new RecordingWriter);
    w->d->path = path;
    w->d->inputRate = inputSampleRate;
    if (inputSampleRate != sampleRate) {
        SwrContext *raw = nullptr;
        const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        if (swr_alloc_set_opts2(&raw, &mono, AV_SAMPLE_FMT_FLT, sampleRate, &mono, AV_SAMPLE_FMT_FLT, inputSampleRate,
                                0, nullptr) < 0)
            return fail(QStringLiteral("Não foi possível gravar: reamostrador indisponível."));
        w->d->swr.reset(raw);
        if (swr_init(raw) < 0)
            return fail(QStringLiteral("Não foi possível gravar: reamostrador indisponível."));
    }
    QString message;
    w->d->writer = MediaWriter::create(path, std::nullopt, MediaWriter::Audio{sampleRate, 1, 128'000}, &message);
    if (!w->d->writer)
        return fail(QStringLiteral("Não foi possível gravar: %1").arg(message));
    return w;
}

bool RecordingWriter::append(const float *mono, int frames)
{
    QMutexLocker lock(&d->mutex);
    if (d->closed || !d->error.isEmpty())
        return false;
    return d->swr ? d->convert(mono, frames) : d->write(mono, frames);
}

float RecordingWriter::takeLevel()
{
    QMutexLocker lock(&d->mutex);
    return std::exchange(d->level, 0.0f);
}

QList<float> RecordingWriter::peaks() const
{
    QMutexLocker lock(&d->mutex);
    return d->peaks;
}

Micros RecordingWriter::duration() const
{
    QMutexLocker lock(&d->mutex);
    return Time::round(double(d->written) * 1e6 / sampleRate);
}

bool RecordingWriter::finish(QString *error)
{
    QMutexLocker lock(&d->mutex);
    if (d->closed)
        return d->error.isEmpty();
    if (d->swr && d->error.isEmpty())
        d->convert(nullptr, 0);
    if (d->bucketFill > 0) {
        d->peaks.append(d->bucketPeak);
        d->bucketPeak = 0;
        d->bucketFill = 0;
    }
    d->closed = true;
    QString message;
    if (d->error.isEmpty() && !d->writer->finish(&message))
        d->error = message;
    if (!d->error.isEmpty()) {
        d->writer->abort();
        QFile::remove(d->path);
        if (error)
            *error = QStringLiteral("Não foi possível gravar: %1").arg(d->error);
        return false;
    }
    return true;
}

void RecordingWriter::cancel()
{
    QMutexLocker lock(&d->mutex);
    if (!d->closed) {
        d->closed = true;
        d->writer->abort();
    }
    QFile::remove(d->path);
}

QString RecordingWriter::path() const { return d->path; }

// MARK: - AudioRecorder

namespace {

QString fakeMode() { return qEnvironmentVariable("OPENFRAME_FAKE_RECORDING"); }

QString permissionMessage()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("O OpenFrame não tem permissão para usar o microfone. Ligue em Ajustes do Sistema → "
                          "Privacidade e Segurança → Microfone.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("O Windows não deixou o OpenFrame usar o microfone. Ligue em Configurações → Privacidade e "
                          "segurança → Microfone (\"Acesso ao microfone\" e \"Permitir que aplicativos da área de "
                          "trabalho acessem o microfone\").");
#else
    return QStringLiteral("O OpenFrame não tem permissão para usar o microfone.");
#endif
}

/// Mensagem do microfone que não abriu ou parou. No Windows, o acesso desligado na privacidade aparece assim.
QString openMessage()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Não foi possível abrir o microfone. Se o acesso estiver desligado, ligue em Configurações "
                          "→ Privacidade e segurança → Microfone (\"Acesso ao microfone\" e \"Permitir que "
                          "aplicativos da área de trabalho acessem o microfone\").");
#else
    return QStringLiteral("Não foi possível abrir o microfone.");
#endif
}

QString noInputMessage() { return QStringLiteral("Nenhum microfone encontrado."); }

bool permissionDenied()
{
#if QT_CONFIG(permissions)
    if (QCoreApplication::instance())
        return qApp->checkPermission(QMicrophonePermission{}) == Qt::PermissionStatus::Denied;
#endif
    return false;
}

/// A entrada: o `QAudioSource` (ou a voz sintética), na thread de captura. Converte para mono float e entrega ao
/// `RecordingWriter`.
class Capture : public QObject
{
public:
    std::function<void(const QString &)> onError;

    /// Abre a entrada e cria o arquivo. Vazio = gravando; senão, a mensagem do erro.
    QString start(const std::optional<QString> &input, const QString &path, std::shared_ptr<RecordingWriter> &out)
    {
        if (!fakeMode().isEmpty())
            return startFake(path, out);
        const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
        if (devices.isEmpty())
            return noInputMessage();
        QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (input)
            for (const QAudioDevice &dev : devices)
                if (QString::fromUtf8(dev.id()) == *input)
                    device = dev;
        if (device.isNull())
            device = devices.first();
        QAudioFormat format = device.preferredFormat();
        if (!format.isValid() || format.sampleRate() <= 0 || format.channelCount() <= 0)
            return openMessage();
        QString error;
        auto writer = RecordingWriter::create(path, format.sampleRate(), &error);
        if (!writer)
            return error;
        m_writer = std::move(writer);
        m_format = format;
        m_source = std::make_unique<QAudioSource>(device, format);
        // Folga de meio segundo: a thread da captura pode atrasar sem perder som.
        m_source->setBufferSize(format.bytesForDuration(500'000));
        QObject::connect(m_source.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_source && m_source->error() != QAudio::NoError && onError)
                onError(openMessage());
        });
        m_io = m_source->start();
        if (!m_io || m_source->error() != QAudio::NoError) {
            stop();
            m_writer->cancel();
            m_writer.reset();
            return openMessage();
        }
        QObject::connect(m_io, &QIODevice::readyRead, this, [this] { pull(); });
        out = m_writer;
        return {};
    }

    void stop()
    {
        if (m_source) {
            if (m_io)
                pull();
            m_source->stop();
        }
        m_source.reset();
        m_io = nullptr;
        m_fake.reset();
        m_writer.reset();
        m_pending.clear();
    }

    ~Capture() override { stop(); }

private:
    void pull()
    {
        if (!m_io || !m_writer)
            return;
        m_pending += m_io->readAll();
        const int frameBytes = m_format.bytesPerFrame();
        const int channels = m_format.channelCount();
        const int bytes = m_format.bytesPerSample();
        const qsizetype frames = m_pending.size() / frameBytes;
        if (frames <= 0)
            return;
        m_mono.resize(size_t(frames));
        const char *data = m_pending.constData();
        for (qsizetype i = 0; i < frames; ++i) {
            float sum = 0;
            for (int c = 0; c < channels; ++c)
                sum += m_format.normalizedSampleValue(data + i * frameBytes + c * bytes);
            m_mono[size_t(i)] = sum / float(channels);
        }
        m_pending.remove(0, frames * frameBytes);
        m_writer->append(m_mono.data(), int(frames));
    }

    /// "Voz" sintética em tempo real: sílabas de 180 Hz com envelope, pausas entre as palavras.
    QString startFake(const QString &path, std::shared_ptr<RecordingWriter> &out)
    {
        if (fakeMode() == QLatin1String("sem-permissao"))
            return permissionMessage();
        if (fakeMode() == QLatin1String("sem-microfone"))
            return noInputMessage();
        QString error;
        auto writer = RecordingWriter::create(path, RecordingWriter::sampleRate, &error);
        if (!writer)
            return error;
        m_writer = std::move(writer);
        m_fake = std::make_unique<QTimer>();
        m_clock.start();
        m_sent = 0;
        QObject::connect(m_fake.get(), &QTimer::timeout, this, [this] {
            const qint64 due = m_clock.nsecsElapsed() * RecordingWriter::sampleRate / 1'000'000'000;
            if (due <= m_sent || !m_writer)
                return;
            m_mono.resize(size_t(due - m_sent));
            for (size_t i = 0; i < m_mono.size(); ++i) {
                const double t = double(m_sent + qint64(i)) / RecordingWriter::sampleRate;
                const double syllable = std::max(0.0, std::sin(M_PI * t * 4.3)) *
                                        (0.35 + 0.3 * std::sin(t * 1.7) * std::sin(t * 0.9));
                const double word = std::fmod(t, 2.2) < 1.7 ? 1 : 0;
                m_mono[i] = float(std::sin(2 * M_PI * 180 * t) * syllable * word);
            }
            m_sent = due;
            m_writer->append(m_mono.data(), int(m_mono.size()));
        });
        m_fake->start(20);
        out = m_writer;
        return {};
    }

    std::shared_ptr<RecordingWriter> m_writer;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    QByteArray m_pending;
    std::vector<float> m_mono;
    std::unique_ptr<QTimer> m_fake;
    QElapsedTimer m_clock;
    qint64 m_sent = 0;
};

} // namespace

struct AudioRecorder::Impl {
    AudioRecorder *q = nullptr;
    QThread thread;
    Capture *capture = nullptr;
    std::shared_ptr<RecordingWriter> writer;
    bool recording = false;
    QTimer meter;
    qsizetype reportedPeaks = 0;

    void ensureThread()
    {
        if (capture)
            return;
        capture = new Capture;
        capture->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, capture, &QObject::deleteLater);
        // O erro chega na thread da captura; o fim é tratado na do gravador.
        capture->onError = [r = QPointer<AudioRecorder>(q)](const QString &message) {
            QMetaObject::invokeMethod(r, [r, message] {
                if (r)
                    r->d->end(message);
            }, Qt::QueuedConnection);
        };
        thread.setObjectName(QStringLiteral("OpenFrame gravação"));
        thread.start();
    }

    void stopCapture()
    {
        if (capture)
            QMetaObject::invokeMethod(capture, [c = capture] { c->stop(); }, Qt::BlockingQueuedConnection);
        meter.stop();
    }

    /// A gravação parou sozinha (o microfone sumiu ou deu erro): o que foi gravado fica no arquivo.
    void end(const QString &message)
    {
        if (!recording)
            return;
        recording = false;
        stopCapture();
        QString error;
        const bool saved = writer->finish(&error) && writer->duration() > 0;
        report();
        if (!saved)
            QFile::remove(writer->path());
        emit q->finished(saved, saved ? writer->duration() : 0, error.isEmpty() ? message : error);
    }

    void report()
    {
        emit q->levelChanged(writer ? writer->takeLevel() : 0.0f);
        const qsizetype n = writer ? writer->peaks().size() : 0;
        if (n != reportedPeaks) {
            reportedPeaks = n;
            emit q->peaksChanged();
        }
    }

    void fail(const QString &message)
    {
        QMetaObject::invokeMethod(q, [r = QPointer<AudioRecorder>(q), message] {
            if (r)
                emit r->finished(false, 0, message);
        }, Qt::QueuedConnection);
    }
};

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->meter.setInterval(33);
    connect(&d->meter, &QTimer::timeout, this, [this] { d->report(); });
}

AudioRecorder::~AudioRecorder()
{
    if (d->recording) {
        d->recording = false;
        d->stopCapture();
        d->writer->cancel();
    }
    d->thread.quit();
    d->thread.wait();
}

QList<AudioInput> AudioRecorder::inputs()
{
    const QString mode = fakeMode();
    if (!mode.isEmpty()) {
        if (mode == QLatin1String("sem-microfone"))
            return {};
        return {{QStringLiteral("fake"), QStringLiteral("Microfone de teste")}};
    }
    QList<AudioInput> out;
    for (const QAudioDevice &dev : QMediaDevices::audioInputs())
        out.append({QString::fromUtf8(dev.id()), dev.description()});
    return out;
}

std::optional<QString> AudioRecorder::defaultInput()
{
    const QString mode = fakeMode();
    if (!mode.isEmpty()) {
        if (mode == QLatin1String("sem-microfone"))
            return std::nullopt;
        return QStringLiteral("fake");
    }
    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull())
        return std::nullopt;
    return QString::fromUtf8(dev.id());
}

void AudioRecorder::requestPermission(QObject *context, const std::function<void(bool)> &done)
{
    const QString mode = fakeMode();
    if (!mode.isEmpty()) {
        const bool granted = mode != QLatin1String("sem-permissao");
        QMetaObject::invokeMethod(context, [done, granted] { done(granted); }, Qt::QueuedConnection);
        return;
    }
#if QT_CONFIG(permissions)
    if (QCoreApplication::instance()) {
        const QMicrophonePermission permission;
        switch (qApp->checkPermission(permission)) {
        case Qt::PermissionStatus::Undetermined:
            qApp->requestPermission(permission, context, [done](const QPermission &p) {
                done(p.status() == Qt::PermissionStatus::Granted);
            });
            return;
        case Qt::PermissionStatus::Denied:
            QMetaObject::invokeMethod(context, [done] { done(false); }, Qt::QueuedConnection);
            return;
        case Qt::PermissionStatus::Granted:
            break;
        }
    }
#endif
    QMetaObject::invokeMethod(context, [done] { done(true); }, Qt::QueuedConnection);
}

bool AudioRecorder::start(const std::optional<QString> &input, const QString &path)
{
    if (d->recording)
        cancel();
    d->writer.reset();
    d->reportedPeaks = 0;
    if (fakeMode().isEmpty() && permissionDenied()) {
        d->fail(permissionMessage());
        return false;
    }
    d->ensureThread();
    QString error;
    std::shared_ptr<RecordingWriter> writer;
    QMetaObject::invokeMethod(
        d->capture, [&, c = d->capture] { error = c->start(input, path, writer); }, Qt::BlockingQueuedConnection);
    if (!error.isEmpty() || !writer) {
        d->fail(error.isEmpty() ? openMessage() : error);
        return false;
    }
    d->writer = std::move(writer);
    d->recording = true;
    d->meter.start();
    return true;
}

void AudioRecorder::stop()
{
    if (!d->recording)
        return;
    d->recording = false;
    d->stopCapture();
    QString error;
    const bool ok = d->writer->finish(&error);
    d->report();
    const Micros duration = ok ? d->writer->duration() : 0;
    QMetaObject::invokeMethod(this, [r = QPointer<AudioRecorder>(this), ok, duration, error] {
        if (r)
            emit r->finished(ok, duration, error);
    }, Qt::QueuedConnection);
}

void AudioRecorder::cancel()
{
    if (!d->recording)
        return;
    d->recording = false;
    d->stopCapture();
    d->writer->cancel();
    d->writer.reset();
    QMetaObject::invokeMethod(this, [r = QPointer<AudioRecorder>(this)] {
        if (r)
            emit r->finished(false, 0, QString());
    }, Qt::QueuedConnection);
}

bool AudioRecorder::isRecording() const { return d->recording; }

QList<float> AudioRecorder::peaks() const { return d->writer ? d->writer->peaks() : QList<float>{}; }

Micros AudioRecorder::duration() const { return d->writer ? d->writer->duration() : 0; }

} // namespace of
