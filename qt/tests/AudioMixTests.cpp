#include "AudioMix.h"
#include "MediaProbe.h"
#include "TestMedia.h"
#include "TestSupport.h"

#include <QTest>

using namespace of;

namespace {

/// Amplitude e valor eficaz do seno dos arquivos de teste.
constexpr double amplitude = 12000.0 / 32768;
const double sineRms = amplitude / std::sqrt(2.0);

/// `frames` quadros estéreo da mixagem a partir de `start`.
std::vector<float> mix(const QList<AudioClip> &clips, Micros start, Micros duration)
{
    AudioMixer mixer(clips);
    mixer.seek(start);
    std::vector<float> out(size_t(AudioMix::sampleAt(duration)) * 2);
    // Em blocos, como o player e a exportação leem.
    for (size_t done = 0; done < out.size() / 2;) {
        const int n = int(std::min<size_t>(1024, out.size() / 2 - done));
        mixer.read(out.data() + done * 2, n);
        done += size_t(n);
    }
    return out;
}

/// Valor eficaz do canal esquerdo entre `from` e `to` (segundos, relativos ao bloco).
double rms(const std::vector<float> &s, double from, double to)
{
    const qint64 a = qint64(from * 48000), b = std::min<qint64>(qint64(to * 48000), qint64(s.size() / 2));
    double sum = 0;
    for (qint64 i = a; i < b; ++i)
        sum += double(s[size_t(i * 2)]) * s[size_t(i * 2)];
    return b > a ? std::sqrt(sum / double(b - a)) : 0;
}

/// Frequência pelo número de cruzamentos por zero (subindo) entre `from` e `to`.
double frequency(const std::vector<float> &s, double from, double to)
{
    const qint64 a = qint64(from * 48000), b = qint64(to * 48000);
    int crossings = 0;
    for (qint64 i = a + 1; i < b; ++i)
        crossings += s[size_t((i - 1) * 2)] < 0 && s[size_t(i * 2)] >= 0;
    return crossings / (to - from);
}

/// Maior diferença entre a mixagem e `gain × seno(fonte)`, com a amostra de
/// origem `sourceStart + (n − targetStart)`, nos dois canais.
double maxError(const std::vector<float> &s, qint64 first, qint64 targetStart, qint64 sourceStart, qint64 from,
                qint64 to, const std::function<double(qint64)> &gain)
{
    double worst = 0;
    for (qint64 n = from; n < to; ++n) {
        const double expected =
            gain(n) * std::sin(2 * M_PI * 440 * double(sourceStart + n - targetStart) / 48000) * amplitude;
        for (int c = 0; c < 2; ++c)
            worst = std::max(worst, std::abs(double(s[size_t((n - first) * 2 + c)]) - expected));
    }
    return worst;
}

AudioClip clip(const QString &path, Micros start, Micros duration, Micros sourceStart = 0, double speed = 1)
{
    AudioClip c;
    c.segment = QUuid::createUuid();
    c.path = path;
    c.source = {sourceStart, Time::round(double(duration) * speed)};
    c.targetStart = start;
    c.targetDuration = duration;
    c.speed = speed;
    return c;
}

} // namespace

class AudioMixTests : public QObject
{
    Q_OBJECT

    QString tone;

private slots:
    void initTestCase() { tone = test::makeAudio(3); }

    void cleanupTestCase()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_KEEP_MEDIA"))
            QDir(test::mediaDirectory()).removeRecursively();
    }

    void clipsFollowTheSwiftRules()
    {
        Project p;
        const Material withAudio = test::video(5, QStringLiteral("a.mov"));
        Material silent = test::video(5, QStringLiteral("b.mov"));
        silent.hasAudio = false;
        const Material photo = test::photo();
        const Material song = test::audio(10);
        p.materials = {withAudio, silent, photo, song};

        TimelineEditor::addMedia(withAudio, p);
        TimelineEditor::addMedia(silent, p);
        TimelineEditor::addMedia(photo, p);
        // Sobreposição na faixa principal (transição): o som cruza.
        const auto main = [&]() -> QList<Segment> & { return p.tracks[*p.mainTrackIndex()].segments; };
        main()[1].targetStart = Time::seconds(4);

        Track music(TrackKind::Audio);
        Segment loud(song.id, {0, Time::seconds(4)}, 0);
        loud.volume = 1.5;
        loud.fadeIn = Time::seconds(3); // limitado à metade (2 s)
        Segment mutedSeg(song.id, {0, Time::seconds(2)}, Time::seconds(5));
        mutedSeg.volume = 0;
        music.segments = {loud, mutedSeg};
        p.tracks.append(music);

        Track mutedTrack(TrackKind::Audio);
        mutedTrack.muted = true;
        mutedTrack.segments = {Segment(song.id, {0, Time::seconds(1)}, 0)};
        p.tracks.append(mutedTrack);

        const QList<AudioClip> clips = AudioMix::clips(p);
        QCOMPARE(clips.size(), 2);
        const AudioClip &video = clips[0].segment == main()[0].id ? clips[0] : clips[1];
        const AudioClip &music0 = clips[0].segment == main()[0].id ? clips[1] : clips[0];
        QCOMPARE(video.segment, main()[0].id);
        QCOMPARE(video.path, withAudio.path);
        QCOMPARE(video.fadeOut, Time::seconds(1)); // sobreposição de 1 s
        QCOMPARE(video.fadeIn, Micros(0));
        QCOMPARE(music0.segment, loud.id);
        QCOMPARE(music0.volume, 1.0f);
        QCOMPARE(music0.fadeIn, Time::seconds(2));

        // O vídeo de entrada também ganha o fade quando tem áudio.
        main()[1] = Segment(withAudio.id, {0, Time::seconds(5)}, Time::seconds(4));
        const QList<AudioClip> again = AudioMix::clips(p);
        QCOMPARE(again.size(), 3);
        for (const AudioClip &c : again)
            if (c.segment == main()[1].id)
                QCOMPARE(c.fadeIn, Time::seconds(1));

        // Faixa de vídeo muda: nenhum som dela.
        p.tracks[*p.mainTrackIndex()].muted = true;
        QCOMPARE(AudioMix::clips(p).size(), 1);
    }

    void speedChangesTargetDuration()
    {
        Project p;
        const Material m = test::video(4);
        p.materials = {m};
        TimelineEditor::addMedia(m, p);
        const QUuid id = p.tracks[0].segments[0].id;
        TimelineEditor::setSpeed(p, id, 2);
        const QList<AudioClip> clips = AudioMix::clips(p);
        QCOMPARE(clips.size(), 1);
        QCOMPARE(clips[0].speed, 2.0);
        QCOMPARE(clips[0].targetDuration, Time::seconds(2));
        QCOMPARE(clips[0].source.duration, Time::seconds(4));
    }

    void gainRamps()
    {
        AudioClip c = clip(QString(), Time::seconds(1), Time::seconds(4));
        c.volume = 0.8f;
        c.fadeIn = Time::seconds(1);
        c.fadeOut = Time::seconds(2);
        QCOMPARE(AudioMix::gain(c, Time::seconds(0.5)), 0.0f);
        QCOMPARE(AudioMix::gain(c, Time::seconds(1)), 0.0f);
        QVERIFY(qFuzzyCompare(AudioMix::gain(c, Time::seconds(1.5)), 0.4f));
        QCOMPARE(AudioMix::gain(c, Time::seconds(2.5)), 0.8f);
        QVERIFY(qFuzzyCompare(AudioMix::gain(c, Time::seconds(4)), 0.4f));
        QCOMPARE(AudioMix::gain(c, Time::seconds(5)), 0.0f);
    }

    void playsInPlaceAndInSync()
    {
        // Trecho de 1,5 s do arquivo a partir de 0,5 s, colocado em 1 s.
        const AudioClip c = clip(tone, Time::seconds(1), Time::seconds(1.5), Time::seconds(0.5));
        const auto s = mix({c}, 0, Time::seconds(3));
        QCOMPARE(rms(s, 0, 0.99), 0.0);
        QCOMPARE(rms(s, 2.51, 3), 0.0);
        QVERIFY2(std::abs(rms(s, 1.1, 2.4) - sineRms) < 0.01, qPrintable(QString::number(rms(s, 1.1, 2.4))));
        // Amostra a amostra: o AAC deixa um resíduo de ~0,02, e uma amostra de
        // atraso para qualquer lado aumenta o erro.
        const auto error = [&](int lag) {
            return maxError(s, 0, 48000, 24000 + lag, 48000 + 2400, 48000 * 5 / 2 - 2400, [](qint64) { return 1.0; });
        };
        QVERIFY2(error(0) < 0.03, qPrintable(QString::number(error(0))));
        QVERIFY(error(0) < error(-1));
        QVERIFY(error(0) < error(1));
    }

    void monoGoesToBothSidesAtFullLevel()
    {
        const auto s = mix({clip(tone, 0, Time::seconds(1))}, 0, Time::seconds(1));
        double left = 0, right = 0;
        for (size_t i = 4800; i < 43200; ++i) {
            left += double(s[i * 2]) * s[i * 2];
            right += double(s[i * 2 + 1]) * s[i * 2 + 1];
        }
        QCOMPARE(left, right);
        QVERIFY(std::abs(std::sqrt(left / 38400) - sineRms) < 0.01);
    }

    void volumeAndFades()
    {
        AudioClip c = clip(tone, 0, Time::seconds(2));
        c.volume = 0.5f;
        c.fadeIn = Time::seconds(0.5);
        c.fadeOut = Time::seconds(0.5);
        const auto s = mix({c}, 0, Time::seconds(2));
        const auto gain = [&](qint64 n) { return double(AudioMix::gain(c, Time::round(double(n) * 1e6 / 48000))); };
        const double error = maxError(s, 0, 0, 0, 2400, 96000 - 10, gain);
        QVERIFY2(error < 0.03, qPrintable(QString::number(error)));
        QVERIFY(std::abs(rms(s, 0.6, 1.4) - sineRms / 2) < 0.006);
        QVERIFY(rms(s, 0, 0.1) < rms(s, 0.4, 0.5));
    }

    void seekMatchesSequentialRead()
    {
        const AudioClip c = clip(tone, Time::seconds(0.25), Time::seconds(2.5));
        const auto whole = mix({c}, 0, Time::seconds(2));
        const auto part = mix({c}, Time::seconds(1.3), Time::seconds(0.5));
        const size_t offset = size_t(AudioMix::sampleAt(Time::seconds(1.3))) * 2;
        double worst = 0;
        for (size_t i = 0; i < part.size(); ++i)
            worst = std::max(worst, double(std::abs(part[i] - whole[offset + i])));
        QVERIFY2(worst < 0.01, qPrintable(QString::number(worst)));
    }

    void clockedMixerCatchesUpWithTheVideo()
    {
        // O som do preview só sai depois que o dispositivo parte (no Windows, perto de
        // 1 s numa VM). O vídeo anda pelo relógio desde o play; o som tem que entrar no
        // instante que o vídeo mostra, não no do play.
        const AudioClip c = clip(tone, 0, Time::seconds(3));
        const Micros base = Time::seconds(0.5);
        QElapsedTimer clock;
        clock.start();
        ClockedMixer mixer({c}, base, clock);
        QTest::qSleep(350); // a partida lenta do dispositivo
        std::vector<float> out(1024 * 2);
        const Micros before = clock.nsecsElapsed() / 1000;
        mixer.read(out.data(), 1024);
        const Micros after = clock.nsecsElapsed() / 1000;
        QVERIFY2(mixer.skipped() >= before && mixer.skipped() <= after,
                 qPrintable(QStringLiteral("pulou %1 µs, relógio entre %2 e %3").arg(mixer.skipped()).arg(before).arg(after)));
        const qint64 start = AudioMix::sampleAt(base + mixer.skipped());
        QCOMPARE(mixer.position(), start + 1024);
        // O conteúdo é o do instante alcançado (igual a uma leitura comum de lá).
        const auto reference = mix({c}, base + mixer.skipped(), Time::seconds(0.05));
        double worst = 0;
        for (size_t i = 0; i < 1024 * 2; ++i)
            worst = std::max(worst, double(std::abs(out[i] - reference[i])));
        QVERIFY2(worst < 0.01, qPrintable(QString::number(worst)));
        // Depois da primeira leitura, segue em sequência enquanto estiver à frente do relógio (o
        // dispositivo lê adiantado): 10 blocos de 21 ms de uma vez, sem pular.
        for (int i = 0; i < 10; ++i)
            mixer.read(out.data(), 1024);
        QCOMPARE(mixer.position(), start + 11 * 1024);
        QCOMPARE(mixer.resyncs(), 0);
    }

    void clockedMixerResyncsAfterAStall()
    {
        // A máquina engasga mais que o buffer: o som ficaria atrasado até o próximo play.
        // A leitura seguinte alcança o relógio de novo.
        const AudioClip c = clip(tone, 0, Time::seconds(3));
        const Micros base = Time::seconds(0.25);
        QElapsedTimer clock;
        clock.start();
        ClockedMixer mixer({c}, base, clock);
        std::vector<float> out(1024 * 2);
        mixer.read(out.data(), 1024);
        // Um atraso pequeno (dentro de `maxLag`) não pula: é a folga normal do dispositivo.
        QTest::qSleep(20);
        mixer.read(out.data(), 1024);
        QCOMPARE(mixer.resyncs(), 0);
        QTest::qSleep(300);
        const Micros before = clock.nsecsElapsed() / 1000;
        mixer.read(out.data(), 1024);
        const Micros after = clock.nsecsElapsed() / 1000;
        QCOMPARE(mixer.resyncs(), 1);
        const qint64 pos = mixer.position() - 1024;
        QVERIFY2(pos >= AudioMix::sampleAt(base + before) && pos <= AudioMix::sampleAt(base + after),
                 qPrintable(QStringLiteral("posição %1, relógio entre %2 e %3")
                                .arg(pos).arg(AudioMix::sampleAt(base + before)).arg(AudioMix::sampleAt(base + after))));
        // O conteúdo é o do instante alcançado.
        const auto reference = mix({c}, Micros(pos * 1'000'000 / AudioMix::sampleRate), Time::seconds(0.05));
        double worst = 0;
        for (size_t i = 0; i < 1024 * 2; ++i)
            worst = std::max(worst, double(std::abs(out[i] - reference[i])));
        QVERIFY2(worst < 0.01, qPrintable(QString::number(worst)));
    }

    void clockedMixerWithoutClockStartsAtBase()
    {
        const AudioClip c = clip(tone, 0, Time::seconds(3));
        ClockedMixer mixer({c}, Time::seconds(1), QElapsedTimer());
        std::vector<float> out(512 * 2);
        mixer.read(out.data(), 512);
        QCOMPARE(mixer.skipped(), Micros(0));
        QCOMPARE(mixer.position(), AudioMix::sampleAt(Time::seconds(1)) + 512);
    }

    void speedKeepsPitch_data()
    {
        QTest::addColumn<double>("speed");
        QTest::newRow("2x") << 2.0;
        QTest::newRow("0.5x") << 0.5;
        QTest::newRow("0.25x") << 0.25;
        QTest::newRow("3x") << 3.0;
    }

    void speedKeepsPitch()
    {
        QFETCH(double, speed);
        // 1 s da timeline, qualquer que seja a velocidade.
        const AudioClip c = clip(tone, 0, Time::seconds(1), 0, speed);
        const auto s = mix({c}, 0, Time::seconds(1.5));
        const double f = frequency(s, 0.1, 0.9);
        QVERIFY2(std::abs(f - 440) < 15, qPrintable(QString::number(f)));
        QVERIFY2(std::abs(rms(s, 0.1, 0.9) - sineRms) < 0.03, qPrintable(QString::number(rms(s, 0.1, 0.9))));
        QCOMPARE(rms(s, 1.0, 1.5), 0.0);
        // Busca no meio de um trecho acelerado continua soando.
        const auto mid = mix({c}, Time::seconds(0.5), Time::seconds(0.4));
        QVERIFY(std::abs(rms(mid, 0.05, 0.35) - sineRms) < 0.03);
    }

    void sumIsClamped()
    {
        QList<AudioClip> clips;
        for (int i = 0; i < 4; ++i)
            clips.append(clip(tone, 0, Time::seconds(1)));
        const auto s = mix(clips, 0, Time::seconds(1));
        float top = 0;
        for (float v : s)
            top = std::max(top, std::abs(v));
        QCOMPARE(top, 1.0f);
    }

    void missingFileIsSilent()
    {
        const auto s = mix({clip(QStringLiteral("/nao/existe.m4a"), 0, Time::seconds(1))}, 0, Time::seconds(1));
        QCOMPARE(rms(s, 0, 1), 0.0);
    }

    void videoSoundtrack()
    {
        // O som do vídeo de teste (seno de 440 Hz) toca pela mixagem do projeto.
        Project p;
        QString error;
        const auto m = MediaProbe::probe(test::makeVideo({.seconds = 1}), &error);
        QVERIFY2(m, qPrintable(error));
        p.materials = {*m};
        TimelineEditor::addMedia(*m, p);
        const auto s = mix(AudioMix::clips(p), 0, Time::seconds(1));
        QVERIFY(std::abs(rms(s, 0.1, 0.9) - sineRms) < 0.01);
    }
};

QTEST_GUILESS_MAIN(AudioMixTests)
#include "AudioMixTests.moc"
