#include "AudioMix.h"
#include "Exporter.h"
#include "FFmpeg.h"
#include "MediaProbe.h"
#include "Renderer.h"
#include "TestMedia.h"
#include "TestSupport.h"
#include "VideoDecoder.h"

#include <QSignalSpy>
#include <QTest>

using namespace of;

namespace {

/// O que o FFmpeg lê do arquivo exportado.
struct StreamInfo {
    QString codec;
    QString tag;
    int width = 0, height = 0;
    double fps = 0;
    qint64 bitrate = 0;
    qint64 frames = 0;
    int sampleRate = 0, channels = 0;
    AVColorPrimaries primaries = AVCOL_PRI_UNSPECIFIED;
    AVColorTransferCharacteristic transfer = AVCOL_TRC_UNSPECIFIED;
    AVColorSpace matrix = AVCOL_SPC_UNSPECIFIED;
};

struct FileInfo {
    double seconds = 0;
    std::optional<StreamInfo> video, audio;
    int audioStreams = 0;
};

FileInfo inspect(const QString &path)
{
    FileInfo info;
    const ff::InputPtr input = ff::openInput(path);
    if (!input)
        return info;
    info.seconds = double(input->duration) / AV_TIME_BASE;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        const AVStream *st = input->streams[i];
        const AVCodecParameters *par = st->codecpar;
        StreamInfo s;
        s.codec = QString::fromUtf8(avcodec_get_name(par->codec_id));
        char tag[AV_FOURCC_MAX_STRING_SIZE] = {};
        s.tag = QString::fromUtf8(av_fourcc_make_string(tag, par->codec_tag));
        s.width = par->width;
        s.height = par->height;
        s.fps = av_q2d(st->avg_frame_rate);
        s.bitrate = par->bit_rate;
        s.frames = st->nb_frames;
        s.sampleRate = par->sample_rate;
        s.channels = par->ch_layout.nb_channels;
        s.primaries = par->color_primaries;
        s.transfer = par->color_trc;
        s.matrix = par->color_space;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO)
            info.video = s;
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            info.audio = s;
            ++info.audioStreams;
        }
    }
    return info;
}

struct Result {
    bool ok = false;
    QString error;
    QList<double> progress;
};

Result runExport(const Project &p, const ExportSettings &s, const QString &path)
{
    Exporter exporter;
    Result result;
    QObject::connect(&exporter, &Exporter::progress, [&](double f) { result.progress.append(f); });
    QSignalSpy finished(&exporter, &Exporter::finished);
    exporter.start(p, s, path);
    // Pela condição: numa máquina ocupada (whisper, suítes pesadas) a exportação demora bem mais.
    if (!finished.wait(600'000))
        qFatal("exportação não terminou em 10 min");
    result.ok = finished.first().at(0).toBool();
    result.error = finished.first().at(1).toString();
    return result;
}

/// Valor eficaz do canal esquerdo do áudio do arquivo, lido de volta pela mixagem.
double fileRms(const QString &path, Micros from, Micros to)
{
    AudioClip c;
    c.path = path;
    c.source = {0, to};
    c.targetDuration = to;
    AudioMixer mixer({c});
    mixer.seek(from);
    std::vector<float> s(size_t(AudioMix::sampleAt(to - from)) * 2);
    mixer.read(s.data(), int(s.size() / 2));
    double sum = 0;
    for (size_t i = 0; i < s.size(); i += 2)
        sum += double(s[i]) * s[i];
    return std::sqrt(sum / double(s.size() / 2));
}

/// Faixas verticais de cor do vídeo de "amostras".
const QList<QColor> patches{{200, 40, 40},   {40, 180, 60},   {50, 70, 210}, {128, 128, 128},
                            {225, 180, 150}, {240, 240, 240}, {20, 20, 20}};

QString makePatchVideo()
{
    const QString path = test::tempPath(QStringLiteral("mp4"));
    MediaWriter::Video v;
    v.size = QSize(448, 252);
    v.preset = QStringLiteral("veryfast");
    QString error;
    auto writer = MediaWriter::create(path, v, std::nullopt, &error);
    if (!writer)
        qFatal("%s", qPrintable(error));
    QImage frame(v.size, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&frame);
        const int w = v.size.width() / int(patches.size());
        for (qsizetype i = 0; i < patches.size(); ++i)
            p.fillRect(int(i) * w, 0, w, v.size.height(), patches[i]);
    }
    for (int f = 0; f < 30; ++f)
        writer->writeVideo(frame, f);
    if (!writer->finish(&error))
        qFatal("%s", qPrintable(error));
    return path;
}

QList<QColor> patchColors(const QImage &image)
{
    QList<QColor> out;
    for (qsizetype i = 0; i < patches.size(); ++i)
        out.append(test::pixel(image, (double(i) + 0.5) / double(patches.size()), 0.5));
    return out;
}

} // namespace

class ExportTests : public QObject
{
    Q_OBJECT

    Material video;
    Material photo;
    Material tone;

    /// Como o richProject do Swift: dois vídeos (um acelerado), uma foto,
    /// transição, filtro, efeito e texto.
    Project richProject() const
    {
        Project p;
        const QUuid a = TimelineEditor::addMedia(video, p);
        const QUuid b = TimelineEditor::addMedia(video, p);
        TimelineEditor::addMedia(photo, p);
        TimelineEditor::update(p, a, [](Segment &s) {
            s.transition = Transition{TransitionKind::SlideLeft, 400'000};
            s.filter = FilterSetting{FilterPreset::Cinematic, 1};
            s.fadeIn = 300'000;
        });
        TimelineEditor::setSpeed(p, b, 2);
        TimelineEditor::update(p, b, [](Segment &s) {
            VideoEffect split;
            split.kind = VideoEffectKind::RgbSplit;
            s.effects = {split};
        });
        TimelineEditor::addText(test::text(QStringLiteral("OpenFrame")), p, 500'000, 2'000'000);
        return p;
    }

private slots:
    void initTestCase()
    {
        QString error;
        const auto v = MediaProbe::probe(test::makeVideo({.seconds = 2}), &error);
        QVERIFY2(v, qPrintable(error));
        video = *v;
        const auto ph = MediaProbe::probe(test::makeImage(QStringLiteral("png"), 400, 300, QColor(51, 204, 77),
                                                          QColor(51, 204, 77)),
                                          &error);
        QVERIFY2(ph, qPrintable(error));
        photo = *ph;
        const auto t = MediaProbe::probe(test::makeAudio(2), &error);
        QVERIFY2(t, qPrintable(error));
        tone = *t;
    }

    void cleanupTestCase()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_KEEP_MEDIA"))
            QDir(test::mediaDirectory()).removeRecursively();
    }

    void fullExport()
    {
        const Project p = richProject();
        ExportSettings s;
        s.resolution = ResolutionPreset::P720;
        s.frameRate = FrameRate::Fps60;
        s.codec = VideoCodec::Hevc;
        s.bitrateMode = BitrateMode::Custom;
        s.customMbps = 3;
        s.audioKbps = 128;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));

        const FileInfo info = inspect(path);
        QVERIFY2(std::abs(info.seconds - Time::toSeconds(p.duration())) < 0.1, qPrintable(QString::number(info.seconds)));
        QVERIFY(info.video);
        QCOMPARE(QSize(info.video->width, info.video->height), QSize(1280, 720));
        QVERIFY2(std::abs(info.video->fps - 60) < 0.5, qPrintable(QString::number(info.video->fps)));
        QCOMPARE(info.video->codec, QStringLiteral("hevc"));
        QCOMPARE(info.video->tag, QStringLiteral("hvc1"));
        QVERIFY2(info.video->bitrate > 0 && info.video->bitrate < 3'000'000 * 1.5,
                 qPrintable(QString::number(info.video->bitrate)));
        QVERIFY(info.audio);
        QCOMPARE(info.audio->codec, QStringLiteral("aac"));
        QCOMPARE(info.audio->sampleRate, 48000);
        QCOMPARE(info.audio->channels, 2);

        QVERIFY(!r.progress.isEmpty());
        QCOMPARE(r.progress.last(), 1.0);
        QVERIFY(std::is_sorted(r.progress.cbegin(), r.progress.cend()));
    }

    void verticalExport()
    {
        Project p = richProject();
        p.canvas.aspect = AspectRatio::R9x16;
        p.canvas.background = CanvasBackground::blur(0.5);
        ExportSettings s;
        s.resolution = ResolutionPreset::P720;
        s.frameRate = FrameRate::Fps24;
        s.bitrateMode = BitrateMode::Lower;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const FileInfo info = inspect(path);
        QVERIFY(info.video);
        QCOMPARE(QSize(info.video->width, info.video->height), QSize(720, 1280));
        QVERIFY(std::abs(info.video->fps - 24) < 0.5);
        QCOMPARE(info.video->codec, QStringLiteral("h264"));
    }

    void proResExport()
    {
        const Project p = richProject();
        ExportSettings s;
        s.resolution = ResolutionPreset::P480;
        s.frameRate = FrameRate::Fps29_97;
        s.codec = VideoCodec::ProRes422;
        s.container = ContainerFormat::Mov;
        s.audioEnabled = false;
        const QString path = test::tempPath(QStringLiteral("mov"));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const FileInfo info = inspect(path);
        QVERIFY(info.video);
        QVERIFY2(std::abs(info.video->fps - 29.97) < 0.05, qPrintable(QString::number(info.video->fps)));
        QCOMPARE(info.video->codec, QStringLiteral("prores"));
        QCOMPARE(info.audioStreams, 0);
    }

    void audioOnly()
    {
        const Project p = richProject();
        ExportSettings s;
        s.container = ContainerFormat::M4a;
        const QString path = test::tempPath(QStringLiteral("m4a"));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const FileInfo info = inspect(path);
        QVERIFY(!info.video);
        QCOMPARE(info.audioStreams, 1);
        QCOMPARE(r.progress.last(), 1.0);
    }

    void audioOnlyNeedsSound()
    {
        Project p;
        TimelineEditor::addMedia(photo, p);
        ExportSettings s;
        s.container = ContainerFormat::M4a;
        const Result r = runExport(p, s, test::tempPath(QStringLiteral("m4a")));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("O projeto não tem áudio para exportar."));
    }

    void exportedSoundMatchesTheMix_data()
    {
        QTest::addColumn<int>("sampleRate");
        QTest::newRow("48 kHz") << 48000;
        QTest::newRow("44,1 kHz") << 44100;
    }

    void exportedSoundMatchesTheMix()
    {
        QFETCH(int, sampleRate);
        // Foto de 3 s com uma música de 2 s a meio volume a partir de 0,5 s.
        Project p;
        TimelineEditor::addMedia(photo, p);
        const QUuid id = TimelineEditor::addMedia(tone, p, 500'000);
        TimelineEditor::update(p, id, [](Segment &s) { s.volume = 0.5; });
        ExportSettings s;
        s.resolution = ResolutionPreset::P480;
        s.audioSampleRate = sampleRate;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const FileInfo info = inspect(path);
        QVERIFY(info.audio);
        QCOMPARE(info.audio->sampleRate, sampleRate);
        const double expected = 12000.0 / 32768 / std::sqrt(2.0) / 2;
        QVERIFY(fileRms(path, 0, Time::seconds(0.45)) < 0.002);
        const double middle = fileRms(path, Time::seconds(0.7), Time::seconds(2.3));
        QVERIFY2(std::abs(middle - expected) < 0.01, qPrintable(QString::number(middle)));
        QVERIFY(fileRms(path, Time::seconds(2.6), Time::seconds(2.95)) < 0.002);
    }

    void frameCountAndDuration()
    {
        // 1 s a 30 quadros/s = 30 quadros, o último até o fim.
        Project p;
        p.materials = {video};
        p.tracks[0].segments = {Segment(video.id, {0, Time::seconds(1)}, 0)};
        ExportSettings s;
        s.resolution = ResolutionPreset::P480;
        s.audioEnabled = false;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        QVERIFY(runExport(p, s, path).ok);
        const FileInfo info = inspect(path);
        QVERIFY(info.video);
        QCOMPARE(info.video->frames, qint64(30));
        QVERIFY2(std::abs(info.seconds - 1) < 0.02, qPrintable(QString::number(info.seconds)));
    }

    void renderedContent()
    {
        Project p;
        const QString red = test::makeImage(QStringLiteral("png"), 320, 180, Qt::red, Qt::red);
        TimelineEditor::addMedia(*MediaProbe::probe(red), p);
        TextContent text = test::text(QStringLiteral("TESTE"));
        text.style.y = 0.6;
        text.style.backgroundEnabled = true;
        text.style.backgroundColor = RGBA{0, 0, 1, 1};
        TimelineEditor::addText(text, p, 0);
        ExportSettings s;
        s.resolution = ResolutionPreset::P480;
        s.audioEnabled = false;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        QVERIFY(runExport(p, s, path).ok);

        auto decoder = VideoDecoder::open(path);
        QVERIFY(decoder);
        const QImage frame = decoder->frameAt(Time::seconds(1));
        QCOMPARE(frame.size(), QSize(854, 480));
        const QColor center = test::pixel(frame, 0.5, 0.7);
        QVERIFY2(center.red() > 200 && center.green() < 50 && center.blue() < 50, qPrintable(test::describe(center)));
        // A caixa azul do texto aparece na faixa de cima (y = 0,6 para cima = 0,2 da borda).
        bool box = false;
        for (int i = 0; i < 20 && !box; ++i) {
            const QColor c = test::pixel(frame, 0.3 + i * 0.02, 0.2);
            box = c.blue() > 150 && c.red() < 100;
        }
        QVERIFY(box);
    }

    void colorsRoundTrip_data()
    {
        QTest::addColumn<VideoCodec>("codec");
        QTest::addColumn<ContainerFormat>("container");
        QTest::newRow("h264") << VideoCodec::H264 << ContainerFormat::Mp4;
        QTest::newRow("hevc") << VideoCodec::Hevc << ContainerFormat::Mp4;
        QTest::newRow("prores") << VideoCodec::ProRes422 << ContainerFormat::Mov;
    }

    void colorsRoundTrip()
    {
        QFETCH(VideoCodec, codec);
        QFETCH(ContainerFormat, container);
        const QString source = makePatchVideo();
        Project p;
        TimelineEditor::addMedia(*MediaProbe::probe(source), p);
        ExportSettings s;
        s.resolution = ResolutionPreset::P480;
        s.codec = codec;
        s.container = container;
        s.audioEnabled = false;
        s.bitrateMode = BitrateMode::Custom;
        s.customMbps = 20;
        const QString path = test::tempPath(fileExtension(container));
        const Result r = runExport(p, s, path);
        QVERIFY2(r.ok, qPrintable(r.error));

        const QList<QColor> original = patchColors(VideoDecoder::open(source)->frameAt(Time::seconds(0.5)));
        const QList<QColor> exported = patchColors(VideoDecoder::open(path)->frameAt(Time::seconds(0.5)));
        for (qsizetype i = 0; i < original.size(); ++i)
            QVERIFY2(test::near(original[i], exported[i], 4),
                     qPrintable(QStringLiteral("faixa %1: original %2, exportado %3")
                                    .arg(i)
                                    .arg(test::describe(original[i]), test::describe(exported[i]))));

        // Marcas de cor BT.709.
        const FileInfo info = inspect(path);
        QVERIFY(info.video);
        QCOMPARE(info.video->primaries, AVCOL_PRI_BT709);
        QCOMPARE(info.video->transfer, AVCOL_TRC_BT709);
        QCOMPARE(info.video->matrix, AVCOL_SPC_BT709);
    }

    void cancelRemovesThePartialFile()
    {
        Project p;
        for (int i = 0; i < 10; ++i)
            TimelineEditor::addMedia(video, p);
        ExportSettings s;
        s.resolution = ResolutionPreset::P1080;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        Exporter exporter;
        QSignalSpy finished(&exporter, &Exporter::finished);
        QSignalSpy progress(&exporter, &Exporter::progress);
        exporter.start(p, s, path);
        QVERIFY(exporter.isRunning());
        // Cancela no meio, já gravando (o primeiro progresso), qualquer que seja a velocidade da máquina.
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() || !finished.isEmpty(), 120'000);
        QVERIFY2(finished.isEmpty(), "terminou antes de cancelar");
        exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60'000);
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(finished.first().at(1).toString(), QString());
        QVERIFY(!exporter.isRunning());
        QVERIFY(!QFile::exists(path));
    }

    void rejectsInvalid()
    {
        ExportSettings s;
        s.resolution = ResolutionPreset::P4320;
        const QString path = test::tempPath(QStringLiteral("mp4"));
        const Result r = runExport(richProject(), s, path);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(!QFile::exists(path));
    }

    void destroyingCancels()
    {
        Project p;
        for (int i = 0; i < 10; ++i)
            TimelineEditor::addMedia(video, p);
        const QString path = test::tempPath(QStringLiteral("mp4"));
        {
            Exporter exporter;
            exporter.start(p, ExportSettings{}, path);
            QTest::qWait(100);
        }
        QVERIFY(!QFile::exists(path));
    }

    void projectThumbnail()
    {
        const Project p = richProject();
        const QImage image = ProjectThumbnail::image(p);
        QCOMPARE(std::min(image.width(), image.height()), 360);
        QCOMPARE(image.size(), p.canvasSize(360));
        const QByteArray jpeg = ProjectThumbnail::jpeg(p);
        QVERIFY(jpeg.startsWith("\xFF\xD8"));
        QVERIFY(ProjectThumbnail::image(Project{}).isNull());
        QVERIFY(ProjectThumbnail::jpeg(Project{}).isEmpty());
    }
};

QTEST_MAIN(ExportTests)
#include "ExportTests.moc"
