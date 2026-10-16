#include "MediaProbe.h"
#include "Player.h"
#include "TestMedia.h"
#include "TestSupport.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace of;

namespace {

/// Centro da faixa branca do vídeo de teste, em fração da largura (−1 sem faixa).
double barPosition(const QImage &image)
{
    const int y = image.height() / 4;
    int sum = 0, count = 0;
    for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).green() > 160) {
            sum += x;
            ++count;
        }
    return count ? double(sum) / count / image.width() : -1;
}

/// Espera o Player terminar os quadros pedidos (o `frameReady` do último já saiu). Pela condição, não por um
/// prazo: com a máquina ocupada (whisper, suítes pesadas em paralelo), um quadro pode levar segundos.
bool settle(const Player &player)
{
    QElapsedTimer clock;
    clock.start();
    while (player.isRendering()) {
        if (clock.elapsed() > 120'000)
            return false;
        QTest::qWait(5);
    }
    return true;
}

/// Posições do `timeChanged` com o relógio real do teste no instante de cada uma (ms desde `clock`).
struct Timeline {
    QList<std::pair<Micros, double>> samples;
    Timeline(Player &player, const QElapsedTimer &clock)
    {
        QObject::connect(&player, &Player::timeChanged, &player,
                         [this, &clock](Micros t) { samples.append({t, clock.nsecsElapsed() / 1e6}); });
    }
};

QImage lastFrame(const QSignalSpy &frames) { return frames.last().at(0).value<QImage>(); }

} // namespace

class PlayerTests : public QObject
{
    Q_OBJECT

    Material clip;

    /// Projeto com o vídeo de teste (2 s) inteiro ou só o começo.
    Project project(Micros length = Time::seconds(2)) const
    {
        Project p;
        p.materials = {clip};
        p.tracks[0].segments = {Segment(clip.id, {0, length}, 0)};
        return p;
    }

private slots:
    void initTestCase()
    {
        QString error;
        const auto m = MediaProbe::probe(test::makeVideo({.seconds = 2}), &error);
        QVERIFY2(m, qPrintable(error));
        clip = *m;
    }

    void cleanupTestCase()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_KEEP_MEDIA"))
            QDir(test::mediaDirectory()).removeRecursively();
    }

    void seekDrawsTheFrame()
    {
        Player player;
        player.setAudioEnabled(false);
        QSignalSpy frames(&player, &Player::frameReady);
        QSignalSpy times(&player, &Player::timeChanged);
        player.setProject(project());
        QVERIFY(settle(player));
        QCOMPARE(frames.count(), 1);
        QCOMPARE(lastFrame(frames).size(), QSize(1280, 720));
        QVERIFY(barPosition(lastFrame(frames)) >= 0 && barPosition(lastFrame(frames)) < 0.02);

        player.seek(Time::seconds(1));
        QVERIFY(settle(player));
        QCOMPARE(frames.count(), 2);
        QCOMPARE(times.last().at(0).value<Micros>(), Time::seconds(1));
        QCOMPARE(player.time(), Time::seconds(1));
        QVERIFY2(std::abs(barPosition(lastFrame(frames)) - 0.5) < 0.02,
                 qPrintable(QString::number(barPosition(lastFrame(frames)))));
    }

    void seekIsChased()
    {
        Player player;
        player.setAudioEnabled(false);
        QSignalSpy frames(&player, &Player::frameReady);
        player.setProject(project());
        QVERIFY(settle(player));
        frames.clear();
        // Várias buscas seguidas: a primeira desenha, as do meio são
        // descartadas e só a última é desenhada depois.
        for (double s : {0.1, 0.3, 0.5, 0.7, 0.9, 1.1, 1.3, 1.5})
            player.seek(Time::seconds(s));
        QVERIFY(settle(player));
        QCOMPARE(frames.count(), 2);
        QVERIFY(std::abs(barPosition(frames.first().at(0).value<QImage>()) - 0.05) < 0.02);
        QVERIFY(std::abs(barPosition(lastFrame(frames)) - 0.75) < 0.02);
    }

    void previewShortSide()
    {
        Player player;
        player.setAudioEnabled(false);
        QSignalSpy frames(&player, &Player::frameReady);
        player.setProject(project());
        player.setPreviewShortSide(360);
        QVERIFY(settle(player));
        QCOMPARE(lastFrame(frames).size(), QSize(640, 360));
        QCOMPARE(player.previewShortSide(), 360);
    }

    void editRedrawsWhilePaused()
    {
        Player player;
        player.setAudioEnabled(false);
        QSignalSpy frames(&player, &Player::frameReady);
        Project p = project();
        player.setProject(p);
        player.seek(Time::seconds(0.5));
        QVERIFY(settle(player));
        const QColor before = test::pixel(lastFrame(frames), 0.8, 0.75);
        p.tracks[0].segments[0].opacity = 0.5;
        player.setProject(p);
        QVERIFY(settle(player));
        const QColor after = test::pixel(lastFrame(frames), 0.8, 0.75);
        QVERIFY2(after.blue() < before.blue() - 20, qPrintable(test::describe(before) + test::describe(after)));
        QCOMPARE(player.time(), Time::seconds(0.5));
    }

    void playRunsInRealTimeAndFinishes()
    {
        Player player;
        player.setAudioEnabled(false);
        player.setProject(project(Time::seconds(0.6)));
        QSignalSpy frames(&player, &Player::frameReady);
        QSignalSpy times(&player, &Player::timeChanged);
        QSignalSpy finished(&player, &Player::finished);
        QSignalSpy playing(&player, &Player::playingChanged);
        QVERIFY(settle(player));
        frames.clear();

        QElapsedTimer clock;
        clock.start();
        Timeline real(player, clock);
        player.play();
        QVERIFY(player.isPlaying());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60'000);
        const qint64 elapsed = clock.elapsed();
        // Nunca termina antes da duração em tempo real (o atraso de uma máquina ocupada só pode somar).
        QVERIFY2(elapsed >= 595, qPrintable(QString::number(elapsed)));
        QVERIFY(!player.isPlaying());
        QCOMPARE(player.time(), Time::seconds(0.6));
        QCOMPARE(playing.count(), 2);
        QCOMPARE(playing.last().at(0).toBool(), false);

        // Tempo real: cada posição é o relógio real desde o play (até a duração), mesmo com a máquina ocupada
        // atrasando os avisos; sempre avançando e terminando na duração.
        QVERIFY2(times.count() >= 5, qPrintable(QString::number(times.count())));
        for (qsizetype i = 1; i < times.count(); ++i)
            QVERIFY(times[i].at(0).value<Micros>() >= times[i - 1].at(0).value<Micros>());
        QCOMPARE(times.last().at(0).value<Micros>(), Time::seconds(0.6));
        for (const auto &[t, ms] : std::as_const(real.samples)) {
            const double expected = std::min(ms, 600.0);
            QVERIFY2(std::abs(t / 1000.0 - expected) < 50 || (t == Time::seconds(0.6) && ms >= 595),
                     qPrintable(QStringLiteral("posição %1 ms no relógio %2 ms").arg(t / 1000.0).arg(ms)));
        }
        QVERIFY(settle(player));
        // Desenhou durante a reprodução (quantos, depende da máquina) e o último é o do fim.
        QVERIFY2(frames.count() >= 2, qPrintable(QString::number(frames.count())));
        // O último quadro é o do fim do trecho (faixa perto de 0,6/2 = 0,3).
        QVERIFY(std::abs(barPosition(lastFrame(frames)) - 0.3) < 0.03);
    }

    void pauseKeepsThePosition()
    {
        Player player;
        player.setAudioEnabled(false);
        player.setProject(project());
        QSignalSpy frames(&player, &Player::frameReady);
        QElapsedTimer clock;
        clock.start();
        player.play();
        QTest::qWait(400);
        const double before = clock.nsecsElapsed() / 1e6;
        player.pause();
        const double after = clock.nsecsElapsed() / 1e6;
        const Micros t = player.time();
        // A posição da pausa é o tempo real tocado (a espera pode ter passado de 400 ms numa máquina ocupada).
        const double expected = std::min(before, 2000.0);
        QVERIFY2(t / 1000.0 >= expected - 5 && t / 1000.0 <= std::min(after, 2000.0) + 5,
                 qPrintable(QStringLiteral("%1 ms entre %2 e %3").arg(t / 1000.0).arg(before).arg(after)));
        QVERIFY(t >= Time::seconds(0.4));
        QTest::qWait(200);
        QCOMPARE(player.time(), t);
        QVERIFY(settle(player));
        // Pausado, o quadro sai na resolução cheia e na posição da pausa.
        QCOMPARE(lastFrame(frames).size(), QSize(1280, 720));
        QVERIFY(std::abs(barPosition(lastFrame(frames)) - double(t) / Time::seconds(2)) < 0.03);
    }

    void playAtTheEndRestarts()
    {
        Player player;
        player.setAudioEnabled(false);
        player.setProject(project(Time::seconds(0.3)));
        player.seek(player.duration());
        QSignalSpy times(&player, &Player::timeChanged);
        player.play();
        QCOMPARE(times.first().at(0).value<Micros>(), Micros(0));
        QVERIFY(player.isPlaying());
        QTRY_VERIFY_WITH_TIMEOUT(!player.isPlaying(), 60'000);
    }

    void seekWhilePlaying()
    {
        Player player;
        player.setAudioEnabled(false);
        player.setProject(project());
        player.play();
        QTest::qWait(100);
        QElapsedTimer clock;
        clock.start();
        player.seek(Time::seconds(0.5));
        const double seeked = clock.nsecsElapsed() / 1e6;
        QTest::qWait(200);
        const double before = clock.nsecsElapsed() / 1e6;
        const Micros t = player.time();
        const double after = clock.nsecsElapsed() / 1e6;
        // Continua tocando da posição nova, em tempo real (até o fim do clipe, em 2 s): o relógio do player
        // recomeçou dentro do `seek`, entre 0 e `seeked`.
        const double lo = std::min(500 + before - seeked, 2000.0) - 2, hi = std::min(500 + after, 2000.0) + 2;
        QVERIFY2(t / 1000.0 >= lo && t / 1000.0 <= hi,
                 qPrintable(QStringLiteral("%1 ms, esperado entre %2 e %3").arg(t / 1000.0).arg(lo).arg(hi)));
        QVERIFY(t >= Time::seconds(0.7));
        player.pause();
    }

    void resolutionDropsWhenRenderIsSlow()
    {
        // Foto com desfoque forte e fundo desfocado em 4K: nenhum computador
        // desenha isso a 30 quadros/s na CPU, então a resolução tem de cair.
        Project p;
        QString error;
        const auto still = MediaProbe::probe(
            test::makeImage(QStringLiteral("png"), 1920, 1080, QColor(200, 80, 40), QColor(40, 80, 200)), &error);
        QVERIFY2(still, qPrintable(error));
        p.materials = {*still};
        // Longo: numa máquina ocupada, os 8 quadros que decidem a queda podem levar bem mais que o normal.
        Segment seg(still->id, {0, Time::seconds(600)}, 0);
        VideoEffect blur;
        blur.kind = VideoEffectKind::Blur;
        blur.intensity = 1;
        seg.effects = {blur};
        seg.transform.scale = 0.8;
        p.tracks[0].segments = {seg};
        p.canvas.background = CanvasBackground::blur(1);

        Player player;
        player.setAudioEnabled(false);
        player.setPreviewShortSide(2160);
        player.setProject(p);
        QSignalSpy frames(&player, &Player::frameReady);
        // Quadros de 4K demoram, ainda mais com outros testes rodando: espera pela condição.
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() && lastFrame(frames).height() == 2160, 120'000);
        QVERIFY(settle(player));

        frames.clear();
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() && lastFrame(frames).height() < 2160, 120'000);
        player.pause();
        for (const auto &args : std::as_const(frames))
            QVERIFY(args.at(0).value<QImage>().height() >= 2160 / 3 - 2);
        // Pausado, volta à resolução cheia.
        QTRY_COMPARE_WITH_TIMEOUT(lastFrame(frames).height(), 2160, 120'000);
    }

    void emptyProject()
    {
        Player player;
        player.setAudioEnabled(false);
        QSignalSpy frames(&player, &Player::frameReady);
        player.setProject(Project{});
        player.play();
        QVERIFY(!player.isPlaying());
        QVERIFY(settle(player));
        QCOMPARE(frames.count(), 1);
        QCOMPARE(test::pixel(lastFrame(frames), 0.5, 0.5), QColor(Qt::black));
    }

    // Com som de verdade (OPENFRAME_TEST_AUDIO=1): só confere que tocar com o
    // dispositivo de áudio não trava nem atrasa o relógio.
    void playsWithSound()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_TEST_AUDIO"))
            QSKIP("defina OPENFRAME_TEST_AUDIO=1 para tocar no dispositivo de som");
        Player player;
        Project p = project(Time::seconds(0.8));
        p.tracks[0].segments[0].volume = 0.05;
        player.setProject(p);
        QSignalSpy finished(&player, &Player::finished);
        QElapsedTimer clock;
        clock.start();
        Timeline real(player, clock);
        player.play();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60'000);
        QVERIFY(clock.elapsed() >= 795);
        // O som não atrasa o relógio: as posições seguem o tempo real.
        for (const auto &[t, ms] : std::as_const(real.samples))
            QVERIFY(std::abs(t / 1000.0 - std::min(ms, 800.0)) < 50 || t == Time::seconds(0.8));
    }
};

QTEST_MAIN(PlayerTests)
#include "PlayerTests.moc"
