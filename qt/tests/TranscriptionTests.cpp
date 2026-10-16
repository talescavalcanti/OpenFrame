// Porte da suíte "Transcrição (whisper.cpp)" (TranscriptionTests.swift), com o whisper na CPU e as falas do
// `say` versionadas em fixtures/fala (tools/gerar-fala.sh). Precisa do modelo base: sem ele, os testes são
// pulados, a menos que OPENFRAME_REQUIRE_WHISPER_MODEL=1 (a CI), que transforma o pulo em falha.
// Com OPENFRAME_BENCH=1, mede 1 min de fala com cada modelo instalado.
#include "MediaProbe.h"
#include "TestMedia.h"
#include "TimelineEditor.h"
#include "Transcriber.h"
#include "WhisperEngine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QThread>
#include <QtEndian>
#include <algorithm>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

const QString sentence = QStringLiteral("Olá, pessoal. Hoje vamos testar as legendas automáticas do editor, direto no computador.");

QString fala(const QString &name) { return QStringLiteral(OPENFRAME_FIXTURES "/fala/") + name; }

/// Arquivo do modelo dos testes: OPENFRAME_TEST_MODELS_DIR, OPENFRAME_MODELS_DIR (a CI), o cache dos testes
/// do Mac (scripts/baixar-modelo-teste.sh) ou a pasta do app. Os testes nunca baixam.
QString modelPath(WhisperModel m = WhisperModel::Base)
{
    QStringList dirs;
    // OPENFRAME_TEST_MODELS_DIR troca a pasta (ex.: para conferir que os testes são pulados).
    if (const QString dir = qEnvironmentVariable("OPENFRAME_TEST_MODELS_DIR"); !dir.isEmpty())
        dirs << dir;
    else if (const QString models = qEnvironmentVariable("OPENFRAME_MODELS_DIR"); !models.isEmpty())
        dirs << models;
    if (dirs.isEmpty())
        dirs << QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/OpenFrame-test-models")
         << QDir::homePath() + QStringLiteral("/Library/Caches/OpenFrame-test-models") << ModelStore::defaultDirectory();
    for (const QString &dir : dirs) {
        const QFileInfo info(QDir(dir).filePath(fileName(m)));
        if (info.isFile() && info.size() == byteSize(m))
            return info.absoluteFilePath();
    }
    return {};
}

#define REQUIRE_MODEL()                                                                                              \
    do {                                                                                                             \
        if (modelPath().isEmpty()) {                                                                                 \
            if (qEnvironmentVariable("OPENFRAME_REQUIRE_WHISPER_MODEL") == QStringLiteral("1"))                      \
                QFAIL("modelo base ausente e OPENFRAME_REQUIRE_WHISPER_MODEL=1");                                    \
            QSKIP("Modelo base ausente: rode scripts/baixar-modelo-teste.sh ou defina OPENFRAME_TEST_MODELS_DIR");   \
        }                                                                                                            \
        if (!WhisperEngine::isAvailable())                                                                           \
            QFAIL("whisper sem backend de CPU");                                                                     \
    } while (false)

/// Amostras de um WAV PCM 16 bits mono (os das falas).
std::vector<float> readWav(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("WAV ausente: %s", qPrintable(path));
    const QByteArray data = f.readAll();
    qsizetype pos = 12;
    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 size = qFromLittleEndian<quint32>(data.constData() + pos + 4);
        if (id == "data") {
            std::vector<float> out(size / 2);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = qFromLittleEndian<qint16>(data.constData() + pos + 8 + qsizetype(i) * 2) / 32768.0f;
            return out;
        }
        pos += 8 + size + (size & 1);
    }
    qFatal("WAV sem dados: %s", qPrintable(path));
}

/// Grava um WAV PCM 16 bits mono de 16 kHz.
QString writeWav(const std::vector<float> &samples)
{
    const QString path = tempPath(QStringLiteral("wav"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("não foi possível gravar %s", qPrintable(path));
    const quint32 bytes = quint32(samples.size() * 2);
    const auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    const auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    f.write("RIFF");
    u32(36 + bytes);
    f.write("WAVEfmt ");
    u32(16), u16(1), u16(1), u32(16000), u32(32000), u16(2), u16(16);
    f.write("data");
    u32(bytes);
    for (float s : samples)
        u16(quint16(qint16(std::clamp(std::lround(s * 32767.0f), -32768L, 32767L))));
    return path;
}

std::pair<Project, QUuid> project(const QString &wav, Micros at = 0, double speed = 1)
{
    const auto m = MediaProbe::probe(wav);
    if (!m)
        qFatal("probe falhou: %s", qPrintable(wav));
    Project p;
    const QUuid id = TE::addMedia(*m, p, at);
    if (speed != 1)
        TE::setSpeed(p, id, speed);
    return {p, id};
}

QStringList normalize(const QString &s)
{
    QString folded = s.normalized(QString::NormalizationForm_D).toLower();
    folded.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
    QStringList out;
    for (const QString &w : folded.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
        out << w;
    return out;
}

/// Taxa de erro de palavras (distância de edição / palavras da referência).
double wer(const QString &reference, const QString &hypothesis)
{
    const QStringList r = normalize(reference), h = normalize(hypothesis);
    QList<int> d(h.size() + 1);
    for (int j = 0; j <= h.size(); ++j)
        d[j] = j;
    for (int i = 1; i <= r.size(); ++i) {
        int prev = d[0];
        d[0] = i;
        for (int j = 1; j <= h.size(); ++j) {
            const int cur = d[j];
            d[j] = std::min({d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] == h[j - 1] ? 0 : 1)});
            prev = cur;
        }
    }
    return double(d[h.size()]) / std::max<qsizetype>(r.size(), 1);
}

QString joined(const QList<TranscribedWord> &words)
{
    QStringList t;
    for (const auto &w : words)
        t << w.text;
    return t.join(u' ');
}

struct Truth {
    QString text;
    double start = 0, end = 0;
};

QList<Truth> wordTruth()
{
    QFile f(fala(QStringLiteral("palavras.json")));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("palavras.json ausente");
    QList<Truth> out;
    for (const auto &v : QJsonDocument::fromJson(f.readAll()).object()["words"].toArray()) {
        const QJsonObject o = v.toObject();
        out.append({o["text"].toString(), o["start"].toDouble(), o["end"].toDouble()});
    }
    return out;
}

struct TimingError {
    double start = 0, end = 0, maxStart = 0;
    int matched = 0;
};

/// Erro médio e máximo (s) do início e do fim das palavras que casam com a referência, na ordem.
TimingError timingError(const QList<TranscribedWord> &words, const QList<Truth> &truth, double offset = 0, double speed = 1)
{
    QList<double> starts, ends;
    int j = 0;
    for (const Truth &t : truth) {
        const QStringList key = normalize(t.text);
        int k = j;
        while (k < words.size() && normalize(words[k].text) != key)
            ++k;
        if (k >= words.size())
            continue;
        starts << std::abs(Time::toSeconds(words[k].start) - (offset + t.start / speed));
        ends << std::abs(Time::toSeconds(words[k].end) - (offset + t.end / speed));
        j = k + 1;
    }
    const auto mean = [](const QList<double> &xs) {
        double s = 0;
        for (double x : xs)
            s += x;
        return xs.isEmpty() ? INFINITY : s / xs.size();
    };
    return {mean(starts), mean(ends), starts.isEmpty() ? INFINITY : *std::max_element(starts.begin(), starts.end()),
            int(starts.size())};
}

struct Run {
    std::optional<QList<TranscribedWord>> words;
    QString error;
    QList<TranscriptionProgress> reports;
};

/// Espera sem prazo próprio: o limite é o TIMEOUT do ctest (um travamento), nunca a velocidade da máquina.
constexpr int noDeadline = OPENFRAME_TEST_TIMEOUT_MS;

Run transcribe(const Project &p, const TranscriptionSource &source = TranscriptionSource::timeline(),
               const QString &language = QStringLiteral("pt"), WhisperEngine::Timing timing = WhisperEngine::Timing::Dtw,
               bool refine = true, WhisperModel model = WhisperModel::Base, const std::atomic<bool> *cancel = nullptr,
               const std::function<void(const TranscriptionProgress &)> &onProgress = {},
               int threads = 0)
{
    WhisperEngine::Options options;
    options.threads = threads;
    options.language = language;
    options.timing = timing;
    options.refine = refine;
    static const std::atomic<bool> never{false};
    Run run;
    run.words = Transcriber::run(p, source, modelPath(model), model, options, cancel ? *cancel : never,
                                 [&](const TranscriptionProgress &pr) {
                                     run.reports.append(pr);
                                     if (onProgress)
                                         onProgress(pr);
                                 },
                                 &run.error);
    return run;
}

} // namespace

class TranscriptionTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // O Transcriber procura o modelo pelo ModelStore: a pasta do modelo dos testes (a CI já define).
        if (!modelPath().isEmpty() && qEnvironmentVariableIsEmpty("OPENFRAME_MODELS_DIR"))
            qputenv("OPENFRAME_MODELS_DIR", QFile::encodeName(QFileInfo(modelPath()).absolutePath()));
        WhisperEngine::isAvailable();
        qInfo().noquote() << "backend:" << WhisperEngine::backendDescription() << "| modelo:" << modelPath();
    }
    void cleanupTestCase() { QDir(mediaDirectory()).removeRecursively(); }

    /// Frase natural: texto parecido, tempos coerentes e progresso nas duas fases.
    void natural()
    {
        REQUIRE_MODEL();
        const auto [p, id] = project(fala(QStringLiteral("frase.wav")), 2'000'000);
        const Run run = transcribe(p);
        QVERIFY2(run.words, qPrintable(run.error));
        const QList<TranscribedWord> &words = *run.words;
        const QString text = joined(words);
        const double w = wer(sentence, text);
        qInfo().noquote() << "Transcrição:" << text << "| WER" << w;
        QVERIFY2(w <= 0.25, qPrintable(text));
        QVERIFY(words.first().start >= 2'000'000);
        QVERIFY(words.last().end <= p.duration());
        for (int i = 1; i < words.size(); ++i)
            QVERIFY(words[i - 1].start <= words[i].start);
        for (const auto &word : words)
            QVERIFY(word.end >= word.start && word.probability >= 0 && word.probability <= 1);
        QList<TranscriptionProgress::Phase> phases;
        QList<double> overall;
        for (const auto &r : run.reports) {
            if (phases.isEmpty() || phases.last() != r.phase)
                phases << r.phase;
            overall << r.overall;
        }
        QCOMPARE(phases, (QList{TranscriptionProgress::Phase::Mixing, TranscriptionProgress::Phase::Transcribing}));
        QVERIFY(std::is_sorted(overall.begin(), overall.end()));
        QCOMPARE(overall.last(), 1.0);
    }

    /// Tempos por palavra contra a referência (tokens e DTW, com e sem o ajuste pela energia).
    void wordTiming()
    {
        REQUIRE_MODEL();
        const QList<Truth> truth = wordTruth();
        const auto [p, id] = project(fala(QStringLiteral("palavras.wav")));
        using T = WhisperEngine::Timing;
        for (const auto &[timing, refine] : {std::pair{T::Tokens, false}, {T::Dtw, false}, {T::Tokens, true}, {T::Dtw, true}}) {
            const Run run = transcribe(p, TranscriptionSource::timeline(), QStringLiteral("pt"), timing, refine);
            QVERIFY2(run.words, qPrintable(run.error));
            const TimingError e = timingError(*run.words, truth);
            qInfo().noquote() << QStringLiteral("Tempo %1%2: início %3 s, fim %4 s, pior início %5 s, %6/%7 palavras")
                                     .arg(timing == T::Tokens ? "tokens" : "dtw", refine ? " + energia" : "")
                                     .arg(e.start, 0, 'f', 3).arg(e.end, 0, 'f', 3).arg(e.maxStart, 0, 'f', 3)
                                     .arg(e.matched).arg(truth.size());
            QVERIFY(e.matched >= truth.size() * 3 / 4);
            if (timing == T::Dtw && refine)
                QVERIFY2(e.start < 0.2 && e.end < 0.3, qPrintable(QStringLiteral("%1 %2").arg(e.start).arg(e.end)));
        }
    }

    /// Com velocidade 2×, os tempos saem na timeline.
    void doubleSpeed()
    {
        REQUIRE_MODEL();
        const QList<Truth> truth = wordTruth();
        const auto samples = readWav(fala(QStringLiteral("palavras.wav")));
        const auto [p, id] = project(fala(QStringLiteral("palavras.wav")), 1'000'000, 2);
        QVERIFY(p.segment(id)->targetDuration() < Micros(double(samples.size()) / 16000 * 1e6 / 1.9));
        const Run run = transcribe(p);
        QVERIFY2(run.words, qPrintable(run.error));
        const TimingError e = timingError(*run.words, truth, 1, 2);
        qInfo().noquote() << QStringLiteral("2×: início %1 s, %2/%3 palavras").arg(e.start, 0, 'f', 3).arg(e.matched).arg(truth.size());
        QVERIFY(e.matched >= truth.size() * 2 / 3);
        QVERIFY(e.start < 0.2);
    }

    /// Fonte = clipe: só o áudio daquele segmento, com tempos da timeline.
    void clipSource()
    {
        REQUIRE_MODEL();
        auto [p, first] = project(fala(QStringLiteral("primeiro.wav")));
        const Material other = *MediaProbe::probe(fala(QStringLiteral("banana.wav")));
        // Numa faixa de áudio própria, muda: o clipe pedido é ouvido mesmo assim.
        Track track;
        track.kind = TrackKind::Audio;
        track.muted = true;
        p.materials.append(other);
        Segment seg(other.id, TimeRange{0, other.duration}, 6'000'000);
        seg.volume = 0;
        track.segments = {seg};
        p.tracks.append(track);
        const Run clip = transcribe(p, TranscriptionSource::of(seg.id));
        QVERIFY2(clip.words, qPrintable(clip.error));
        const QString text = joined(*clip.words).toLower();
        qInfo().noquote() << "Clipe:" << text;
        QVERIFY(text.contains(QStringLiteral("banana")));
        QVERIFY(!text.contains(QStringLiteral("primeiro")));
        for (const auto &w : *clip.words)
            QVERIFY(w.start >= 6'000'000);
        // A timeline inteira não ouve a faixa muda.
        const Run all = transcribe(p);
        QVERIFY2(all.words, qPrintable(all.error));
        const QString everything = joined(*all.words).toLower();
        QVERIFY2(everything.contains(QStringLiteral("primeiro")) && !everything.contains(QStringLiteral("banana")),
                 qPrintable(everything));
    }

    /// Detecta o idioma.
    void detectLanguage()
    {
        REQUIRE_MODEL();
        const auto [p, id] = project(fala(QStringLiteral("bomdia.wav")));
        const Run run = transcribe(p, TranscriptionSource::timeline(), QString());
        QVERIFY2(run.words, qPrintable(run.error));
        const QString text = joined(*run.words);
        QVERIFY2(wer(QStringLiteral("Bom dia. Este teste roda sem a placa de vídeo."), text) <= 0.35, qPrintable(text));
    }

    /// Silêncio dá "sem áudio"; o cancelamento para o whisper de verdade.
    void silenceAndCancel()
    {
        REQUIRE_MODEL();
        const auto [silent, sid] = project(writeWav(std::vector<float>(32000, 0)));
        const Run none = transcribe(silent);
        QVERIFY(!none.words);
        QCOMPARE(none.error, message(TranscriptionError::NoAudio));

        // ~66 s de fala (3 janelas do whisper). O whisper só confere o cancelamento entre passos do encoder e do
        // decoder, que ficam mais lentos com a máquina ocupada: em vez de um prazo fixo, a comparação com a
        // transcrição inteira do mesmo áudio, nas mesmas condições. Cancelado logo no começo, tem de parar bem
        // antes de onde a inteira termina.
        const auto one = readWav(fala(QStringLiteral("frase.wav")));
        std::vector<float> longer;
        for (int i = 0; i < 12; ++i)
            longer.insert(longer.end(), one.begin(), one.end());
        const auto [p, id] = project(writeWav(longer));
        QElapsedTimer clock;
        double fullStart = -1;
        clock.start();
        const Run full = transcribe(p, TranscriptionSource::timeline(), QStringLiteral("pt"), WhisperEngine::Timing::Dtw,
                                    true, WhisperModel::Base, nullptr, [&](const TranscriptionProgress &pr) {
                                        if (pr.phase == TranscriptionProgress::Phase::Transcribing && fullStart < 0)
                                            fullStart = clock.nsecsElapsed() / 1e6;
                                    });
        const double fullMs = clock.nsecsElapsed() / 1e6 - fullStart;
        QVERIFY2(full.words, qPrintable(full.error));

        std::atomic<bool> cancel{false}, started{false};
        Run result;
        QThread *worker = QThread::create([&] {
            result = transcribe(p, TranscriptionSource::timeline(), QStringLiteral("pt"), WhisperEngine::Timing::Dtw,
                                true, WhisperModel::Base, &cancel, [&](const TranscriptionProgress &pr) {
                                    if (pr.phase == TranscriptionProgress::Phase::Transcribing)
                                        started = true;
                                });
        });
        worker->start();
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), noDeadline);
        QThread::msleep(300);
        clock.restart();
        cancel = true;
        QVERIFY(worker->wait(noDeadline));
        const qint64 waited = clock.elapsed();
        delete worker;
        qInfo().noquote() << QStringLiteral("Cancelamento em %1 ms (a transcrição inteira levou %2 ms)").arg(waited).arg(fullMs, 0, 'f', 0);
        QVERIFY(!result.words);
        QVERIFY(result.error.isEmpty());
        QVERIFY2(waited < 0.6 * fullMs, qPrintable(QStringLiteral("%1 ms × %2 ms").arg(waited).arg(fullMs)));
    }

    /// O Transcriber (QObject) de ponta a ponta, com o modelo da pasta do ModelStore.
    void transcriberObject()
    {
        REQUIRE_MODEL();
        if (QFileInfo(modelPath()).absolutePath() != QFileInfo(ModelStore::shared().path(WhisperModel::Base)).absolutePath())
            QSKIP("o ModelStore aponta para outra pasta (defina OPENFRAME_MODELS_DIR com a pasta do modelo)");
        const auto [p, id] = project(fala(QStringLiteral("bomdia.wav")));
        Transcriber transcriber;
        QSignalSpy progress(&transcriber, &Transcriber::progress);
        QSignalSpy finished(&transcriber, &Transcriber::finished);
        transcriber.start(p, TranscriptionSource::timeline(), WhisperModel::Base, QStringLiteral("pt"));
        QVERIFY(transcriber.isRunning());
        QVERIFY(finished.wait(noDeadline));
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][2].toString()));
        const auto words = finished[0][1].value<QList<TranscribedWord>>();
        QVERIFY(wer(QStringLiteral("Bom dia. Este teste roda sem a placa de vídeo."), joined(words)) <= 0.35);
        QVERIFY(progress.size() > 2);
        QVERIFY(!transcriber.isRunning());
    }

    /// Medição (só com OPENFRAME_BENCH=1): 1 min de fala por modelo, na CPU. `OPENFRAME_BENCH_THREADS=8,4` mede
    /// cada número de threads (padrão: 0, a escolha automática do app).
    void oneMinutePerModel()
    {
        if (qEnvironmentVariable("OPENFRAME_BENCH") != QStringLiteral("1"))
            QSKIP("medição: OPENFRAME_BENCH=1");
        REQUIRE_MODEL();
        const auto one = readWav(fala(QStringLiteral("frase.wav")));
        std::vector<float> samples;
        QString reference;
        while (samples.size() < 60 * 16000) {
            samples.insert(samples.end(), one.begin(), one.end());
            reference += sentence + u' ';
        }
        const auto [p, id] = project(writeWav(samples));
        QList<int> threadCounts;
        for (const QString &n : qEnvironmentVariable("OPENFRAME_BENCH_THREADS").split(u',', Qt::SkipEmptyParts))
            threadCounts.append(n.toInt());
        if (threadCounts.isEmpty())
            threadCounts = {0};
        qInfo().noquote() << QStringLiteral("Áudio: %1 s, %2")
                                 .arg(samples.size() / 16000.0, 0, 'f', 1)
                                 .arg(WhisperEngine::backendDescription());
        const QString only = qEnvironmentVariable("OPENFRAME_TEST_MODEL");
        for (WhisperModel model : allCases<WhisperModel>()) {
            if (modelPath(model).isEmpty() || (!only.isEmpty() && only != enumKey(model)))
                continue;
            for (int threads : threadCounts) {
                WhisperEngine::unload();
                QElapsedTimer clock;
                qint64 first = 0, second = 0;
                Run run;
                for (int i = 0; i < 2; ++i) {
                    clock.start();
                    run = transcribe(p, TranscriptionSource::timeline(), QStringLiteral("pt"),
                                     WhisperEngine::Timing::Dtw, true, model, nullptr, {}, threads);
                    (i == 0 ? first : second) = clock.elapsed();
                    QVERIFY2(run.words, qPrintable(run.error));
                }
                qInfo().noquote() << QStringLiteral("MEDIÇÃO %1 CPU, %2 threads: 1ª %3 s (com o carregamento), 2ª %4 s, WER %5")
                                         .arg(enumKey(model))
                                         .arg(threads > 0 ? QString::number(threads) : QStringLiteral("auto"))
                                         .arg(first / 1000.0, 0, 'f', 1)
                                         .arg(second / 1000.0, 0, 'f', 1)
                                         .arg(wer(reference, joined(*run.words)), 0, 'f', 3);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TranscriptionTests)
#include "TranscriptionTests.moc"
