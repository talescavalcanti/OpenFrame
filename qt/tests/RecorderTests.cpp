// Porte da suíte "Gravação" (Tests/OpenFrameCoreTests/RecorderTests.swift): o RecordingWriter com um seno (sem
// microfone), os nomes das gravações e o AudioRecorder com a gravação simulada. O microfone de verdade só com
// OPENFRAME_TEST_MIC=1.
#include "AudioRecorder.h"
#include "MediaProbe.h"
#include "ProjectLibrary.h"
#include "TestMedia.h"
#include "Waveform.h"

#include <QSignalSpy>
#include <QTest>

extern "C" {
#include <libavformat/avformat.h>
}

using namespace of;

namespace {

/// Seno de 440 Hz com amplitude 0,5, mono.
std::vector<float> tone(double seconds, int rate)
{
    std::vector<float> out(size_t(seconds * rate));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = float(0.5 * std::sin(2 * M_PI * 440 * double(i) / rate));
    return out;
}

/// Alimenta o gravador em blocos, como o microfone.
void feed(RecordingWriter &w, const std::vector<float> &samples, int block = 4096)
{
    for (size_t k = 0; k < samples.size(); k += size_t(block))
        QVERIFY(w.append(samples.data() + k, int(std::min(samples.size() - k, size_t(block)))));
}

struct StreamInfo {
    int sampleRate = 0;
    int channels = 0;
    QString codec;
};

StreamInfo audioStream(const QString &path)
{
    AVFormatContext *raw = nullptr;
    StreamInfo info;
    // O FFmpeg espera UTF-8 em todos os sistemas (no Windows, o encodeName daria a página ANSI)
    if (avformat_open_input(&raw, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return info;
    if (avformat_find_stream_info(raw, nullptr) >= 0) {
        const int i = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (i >= 0) {
            const AVCodecParameters *p = raw->streams[i]->codecpar;
            info = {p->sample_rate, p->ch_layout.nb_channels, QString::fromLatin1(avcodec_get_name(p->codec_id))};
        }
    }
    avformat_close_input(&raw);
    return info;
}

QString folder() { return test::mediaDirectory() + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces); }

} // namespace

class RecorderTests : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_KEEP_MEDIA"))
            QDir(test::mediaDirectory()).removeRecursively();
    }

    // Um seno de 1 s vira um M4A de 1 s, mono 48 kHz, que o MediaProbe e o Waveform leem
    void writesM4A_data()
    {
        QTest::addColumn<int>("rate");
        QTest::newRow("48 kHz") << 48000;
        QTest::newRow("44,1 kHz") << 44100;
    }

    void writesM4A()
    {
        QFETCH(int, rate);
        const QString path = folder() + QStringLiteral("/Gravação 1.m4a");
        QString error;
        auto writer = RecordingWriter::create(path, rate, &error);
        QVERIFY2(writer, qPrintable(error));
        feed(*writer, tone(1, rate));
        // O nível é o pico desde a última leitura.
        QVERIFY(std::abs(writer->takeLevel() - 0.5f) < 0.02f);
        QCOMPARE(writer->takeLevel(), 0.0f);
        QVERIFY2(writer->finish(&error), qPrintable(error));
        QVERIFY2(std::abs(writer->duration() - Time::second) < 2000, qPrintable(QString::number(writer->duration())));

        // Picos ao vivo: 50 por segundo, no nível real (sem normalizar).
        const QList<float> live = writer->peaks();
        QVERIFY2(std::abs(live.size() - 50) <= 1, qPrintable(QString::number(live.size())));
        for (qsizetype i = 0; i + 1 < live.size(); ++i)
            QVERIFY2(std::abs(live[i] - 0.5f) < 0.02f, qPrintable(QStringLiteral("%1: %2").arg(i).arg(live[i])));

        const auto m = MediaProbe::probe(path, &error);
        QVERIFY2(m, qPrintable(error));
        QCOMPARE(m->kind, MaterialKind::Audio);
        QVERIFY(m->hasAudio);
        QVERIFY2(std::abs(m->duration - Time::second) < 30'000, qPrintable(QString::number(m->duration)));
        const StreamInfo info = audioStream(path);
        QCOMPARE(info.sampleRate, 48000);
        QCOMPARE(info.channels, 1);
        QCOMPARE(info.codec, QStringLiteral("aac"));
        const QList<float> peaks = Waveform::peaks(path, 50, &error);
        QVERIFY2(std::abs(peaks.size() - 50) <= 2, qPrintable(QString::number(peaks.size())));
        QCOMPARE(*std::max_element(peaks.cbegin(), peaks.cend()), 1.0f);
    }

    // Cancelar apaga o arquivo
    void cancelDeletes()
    {
        const QString path = test::tempPath(QStringLiteral("m4a"));
        auto writer = RecordingWriter::create(path);
        QVERIFY(writer);
        feed(*writer, tone(0.3, 48000));
        QVERIFY(QFile::exists(path));
        writer->cancel();
        QVERIFY(!QFile::exists(path));
        QVERIFY(!writer->append(tone(0.1, 48000).data(), 100));
    }

    // Nomes "Gravação N" sem colidir, na pasta do projeto ou ao lado da biblioteca
    void recordingNames()
    {
        const QString root = folder() + QStringLiteral("/Projetos");
        const ProjectLibrary library(root, false);
        const QUuid id = QUuid::createUuid();
        const QString dir = library.recordingsFolder(id);
        QCOMPARE(dir, QDir(library.folder(id)).filePath(QStringLiteral("Gravações")));
        QCOMPARE(library.recordingsFolder(std::nullopt), QDir(QFileInfo(root).absolutePath()).filePath(QStringLiteral("Gravações")));
        QCOMPARE(QFileInfo(library.recordingPath(id)).fileName(), QStringLiteral("Gravação 1.m4a"));
        QVERIFY(QDir().mkpath(dir));
        for (const char *name : {"Gravação 1.m4a", "Gravação 3.m4a", "Gravação x.m4a", "outra.m4a"}) {
            QFile f(QDir(dir).filePath(QString::fromUtf8(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QCOMPARE(QFileInfo(library.recordingPath(id)).fileName(), QStringLiteral("Gravação 4.m4a"));
        QCOMPARE(QFileInfo(library.recordingPath(std::nullopt)).fileName(), QStringLiteral("Gravação 1.m4a"));
    }

    // Gravação simulada (OPENFRAME_FAKE_RECORDING) grava em tempo real, com nível e picos
    void fakeRecording()
    {
        qputenv("OPENFRAME_FAKE_RECORDING", "1");
        const auto restore = qScopeGuard([] { qunsetenv("OPENFRAME_FAKE_RECORDING"); });
        QCOMPARE(AudioRecorder::inputs(), (QList<AudioInput>{{QStringLiteral("fake"), QStringLiteral("Microfone de teste")}}));
        QCOMPARE(AudioRecorder::defaultInput(), std::optional<QString>(QStringLiteral("fake")));
        std::optional<bool> granted;
        AudioRecorder::requestPermission(this, [&](bool ok) { granted = ok; });
        QTRY_VERIFY(granted.has_value());
        QVERIFY(*granted);

        const QString dir = folder();
        const QString path = dir + QStringLiteral("/Gravação 1.m4a");
        AudioRecorder recorder;
        QSignalSpy levels(&recorder, &AudioRecorder::levelChanged);
        QSignalSpy peaksChanged(&recorder, &AudioRecorder::peaksChanged);
        QSignalSpy finished(&recorder, &AudioRecorder::finished);
        QVERIFY(recorder.start(std::nullopt, path));
        QVERIFY(recorder.isRecording());
        QTest::qWait(700);
        QVERIFY2(levels.size() >= 5, qPrintable(QString::number(levels.size())));
        float loudest = 0;
        for (const auto &args : levels)
            loudest = std::max(loudest, args.at(0).toFloat());
        QVERIFY(loudest > 0.1f);
        QVERIFY(peaksChanged.size() >= 3);
        QVERIFY(recorder.peaks().size() >= 20);
        recorder.stop();
        QVERIFY(!recorder.isRecording());
        QVERIFY(finished.wait(5000));
        const auto args = finished.takeFirst();
        QVERIFY(args.at(0).toBool());
        const Micros duration = args.at(1).value<Micros>();
        QVERIFY2(duration > 500'000 && duration < 3'000'000, qPrintable(QString::number(duration)));
        QCOMPARE(args.at(2).toString(), QString());
        QCOMPARE(recorder.duration(), duration);
        QString error;
        const auto m = MediaProbe::probe(path, &error);
        QVERIFY2(m, qPrintable(error));
        QVERIFY(std::abs(m->duration - duration) < 50'000);

        // Cancelar apaga; os erros simulados são os da interface.
        const QString other = dir + QStringLiteral("/Gravação 2.m4a");
        QVERIFY(recorder.start(QStringLiteral("fake"), other));
        recorder.cancel();
        QVERIFY(!QFile::exists(other));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.takeFirst(), (QVariantList{false, QVariant::fromValue(Micros(0)), QString()}));

        qputenv("OPENFRAME_FAKE_RECORDING", "sem-permissao");
        granted.reset();
        AudioRecorder::requestPermission(this, [&](bool ok) { granted = ok; });
        QTRY_VERIFY(granted.has_value());
        QVERIFY(!*granted);
        QVERIFY(!recorder.start(std::nullopt, other));
        QVERIFY(finished.wait(5000));
#if defined(Q_OS_WIN)
        QVERIFY(finished.last().at(2).toString().contains(QStringLiteral("não deixou o OpenFrame usar o microfone")));
#else
        QVERIFY(finished.last().at(2).toString().contains(QStringLiteral("permissão")));
#endif
        qputenv("OPENFRAME_FAKE_RECORDING", "sem-microfone");
        QVERIFY(AudioRecorder::inputs().isEmpty());
        QCOMPARE(AudioRecorder::defaultInput(), std::nullopt);
        QVERIFY(!recorder.start(std::nullopt, other));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(2).toString(), QStringLiteral("Nenhum microfone encontrado."));
        QVERIFY(!QFile::exists(other));
    }

    // Grava 1 s do microfone padrão (OPENFRAME_TEST_MIC=1)
    void realMicrophone()
    {
        if (qEnvironmentVariable("OPENFRAME_TEST_MIC") != QLatin1String("1"))
            QSKIP("sem OPENFRAME_TEST_MIC=1");
        QVERIFY(!AudioRecorder::inputs().isEmpty());
        std::optional<bool> granted;
        AudioRecorder::requestPermission(this, [&](bool ok) { granted = ok; });
        QTRY_VERIFY_WITH_TIMEOUT(granted.has_value(), 60'000);
        QVERIFY(*granted);
        const QString path = test::tempPath(QStringLiteral("m4a"));
        AudioRecorder recorder;
        QSignalSpy finished(&recorder, &AudioRecorder::finished);
        QVERIFY(recorder.start(AudioRecorder::defaultInput(), path));
        QTest::qWait(1000);
        recorder.stop();
        QVERIFY(finished.wait(5000));
        const auto args = finished.takeFirst();
        QVERIFY2(args.at(0).toBool(), qPrintable(args.at(2).toString()));
        QVERIFY(args.at(1).value<Micros>() > 700'000);
        QVERIFY(MediaProbe::probe(path));
        const QList<float> peaks = recorder.peaks();
        qInfo() << "microfone:" << AudioRecorder::defaultInput() << args.at(1).value<Micros>() << "µs, pico"
                << (peaks.isEmpty() ? 0.0f : *std::max_element(peaks.cbegin(), peaks.cend()));
    }
};

QTEST_GUILESS_MAIN(RecorderTests)
#include "RecorderTests.moc"
