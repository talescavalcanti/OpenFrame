// Partes da transcrição que não precisam do modelo: junção dos tokens, ajuste pela energia (WordAligner),
// mistura em 16 kHz (AudioMixdown) e o download dos modelos (ModelStore) contra um servidor HTTP local, com
// retomada, cancelamento, sha256 e tamanho errados.
#include "AudioMixdown.h"
#include "MediaProbe.h"
#include "TestMedia.h"
#include "TimelineEditor.h"
#include "WhisperEngine.h"
#include "WhisperModels.h"
#include "WordAligner.h"

#include <QElapsedTimer>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <atomic>
#include <cmath>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

/// Servidor HTTP mínimo: serve `payload` com `Range`, e pode fechar a conexão no meio (`dropAfter`), parar de
/// mandar sem fechar (`stallAfter`) ou responder com outro código (`status`). Os bytes do meio sempre chegam
/// inteiros ao cliente, em pedaços de qualquer tamanho.
class TestServer : public QTcpServer
{
public:
    QByteArray payload;
    qint64 dropAfter = -1;
    qint64 stallAfter = -1;
    int status = 0;
    QList<QByteArray> ranges;

    TestServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                connect(s, &QTcpSocket::readyRead, s, [this, s, buffer] {
                    buffer->append(s->readAll());
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    respond(s, *buffer);
                    buffer->clear();
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/modelo.bin").arg(serverPort())); }

private:
    void respond(QTcpSocket *s, const QByteArray &request)
    {
        QByteArray range;
        for (const QByteArray &line : request.split('\n'))
            if (line.toLower().startsWith("range:"))
                range = line.mid(6).trimmed();
        ranges.append(range);
        if (status) {
            s->write("HTTP/1.1 " + QByteArray::number(status) + " Erro\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }
        qint64 from = 0;
        if (range.startsWith("bytes="))
            from = range.mid(6, range.indexOf('-') - 6).toLongLong();
        const QByteArray body = payload.mid(from);
        QByteArray head = from > 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n";
        if (from > 0)
            head += "Content-Range: bytes " + QByteArray::number(from) + '-' + QByteArray::number(payload.size() - 1) + '/' +
                    QByteArray::number(payload.size()) + "\r\n";
        s->write(head + "\r\n");
        if (stallAfter >= 0) {
            s->write(body.left(stallAfter));
            return; // fica aberto, sem mandar o resto
        }
        if (dropAfter >= 0) {
            // Fecha depois de mandar (FIN), antes do Content-Length. Com `abort()` (RST), o Windows pode jogar fora
            // o que o cliente ainda não leu, e o parcial dependeria do ritmo.
            s->write(body.left(dropAfter));
            s->disconnectFromHost();
            return;
        }
        s->write(body);
        s->disconnectFromHost();
    }
};

/// Espera o `n`-ésimo fim de download, por até 60 s (é tudo local: só pega um travamento). Vazio se chegou; senão,
/// quanto esperou e quantos pedidos o servidor recebeu (0 = a conexão nem chegou: proxy ou firewall). Acima de 2 s,
/// avisa mesmo passando.
QString waitDownloads(QSignalSpy &finished, qsizetype n, const TestServer *server)
{
    QElapsedTimer clock;
    clock.start();
    while (finished.size() < n && clock.elapsed() < 60'000)
        finished.wait(int(std::max<qint64>(1, 60'000 - clock.elapsed())));
    const QString where = server ? QStringLiteral("; pedidos no servidor: %1").arg(server->ranges.size()) : QString();
    if (clock.elapsed() > 2'000)
        qWarning().noquote() << QStringLiteral("download do teste levou %1 ms%2").arg(clock.elapsed()).arg(where);
    if (finished.size() >= n)
        return {};
    return QStringLiteral("o download não terminou em %1 ms%2").arg(clock.elapsed()).arg(where);
}

#define QVERIFY_DOWNLOADS(spy, n, server)                                                                              \
    do {                                                                                                               \
        const QString why = waitDownloads(spy, n, server);                                                             \
        QVERIFY2(why.isEmpty(), qPrintable(why));                                                                      \
    } while (false)

/// Espera o progresso chegar a `bytes` de `server.payload` (por até 60 s); na falha, o último valor, o parcial e os
/// pedidos que chegaram ao servidor.
#define QVERIFY_PROGRESS(spy, bytes, server)                                                                           \
    do {                                                                                                               \
        const double target = double(bytes) / double((server).payload.size());                                         \
        QTRY_VERIFY2_WITH_TIMEOUT(!(spy).isEmpty() && (spy).last()[1].toDouble() == target,                          \
                                  qPrintable(QStringLiteral("progresso %1 (esperado %2); pedidos no servidor: %3")    \
                                                 .arg((spy).isEmpty() ? -1.0 : (spy).last()[1].toDouble())            \
                                                 .arg(target)                                                          \
                                                 .arg((server).ranges.size())),                                        \
                                  60'000);                                                                             \
    } while (false)

QByteArray payload(int size)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 31) & 0xff);
    return data;
}

QString hexSha(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

/// Grava um WAV PCM 16 bits mono.
QString writeWav(const std::vector<float> &samples, int rate)
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
    u32(16), u16(1), u16(1), u32(quint32(rate)), u32(quint32(rate * 2)), u16(2), u16(16);
    f.write("data");
    u32(bytes);
    for (float s : samples)
        u16(quint16(qint16(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f))));
    return path;
}

double rms(const std::vector<float> &s, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += double(s[i]) * s[i];
    return std::sqrt(sum / double(std::max<size_t>(1, to - from)));
}

/// Cruzamentos de zero (subindo) por segundo: a frequência de um seno.
double frequency(const std::vector<float> &s, size_t from, size_t to, int rate)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        crossings += s[i - 1] < 0 && s[i] >= 0;
    return crossings * double(rate) / double(to - from);
}

} // namespace

class WhisperUnitTests : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase() { QDir(mediaDirectory()).removeRecursively(); }

    // MARK: - Palavras

    /// Tokens viram palavras; um "ç" partido em dois tokens volta inteiro.
    void joinTokens()
    {
        using WhisperEngine::Token;
        const QByteArray c = QByteArrayLiteral(" Ol\xc3\xa1"); // " Olá"
        const QList<QList<Token>> segments = {
            {{c, 0.1, 0.3, 0.9}, {",", 0.3, 0.35, 0.7}, {" cora", 0.5, 0.7, 0.8}, {"\xc3", 0.7, 0.75, 0.6},
             {"\xa7\xc3\xa3o", 0.75, 0.9, 0.4}},
            {{" fim.", 1.2, 1.1, 1.0}, {"   ", 1.5, 1.6, 0.5}},
        };
        const QList<EngineWord> words = WhisperEngine::joinTokens(segments);
        QCOMPARE(words.size(), 3);
        QCOMPARE(words[0].text, QStringLiteral("Olá,"));
        QCOMPARE(words[0].start, 0.1);
        QCOMPARE(words[0].end, 0.35);
        QCOMPARE(words[0].probability, 0.8);
        QCOMPARE(words[1].text, QStringLiteral("coração"));
        QVERIFY(std::abs(words[1].probability - 0.6) < 1e-9);
        QCOMPARE(words[2].text, QStringLiteral("fim."));
        QCOMPARE(words[2].end, 1.2); // fim antes do início: fica no início
    }

    // MARK: - Threads

    /// Os núcleos livres, entre min(4, máximo) e o máximo (os núcleos menos 2, até 8).
    void threadsFor()
    {
        using WhisperEngine::threadsFor;
        QCOMPARE(threadsFor(std::nullopt, 10), 8);
        QCOMPARE(threadsFor(0.3, 10), 8);
        QCOMPARE(threadsFor(2.0, 10), 8);
        QCOMPARE(threadsFor(4.2, 10), 6);
        QCOMPARE(threadsFor(8.0, 10), 4);
        QCOMPARE(threadsFor(10.0, 10), 4);
        QCOMPARE(threadsFor(10.0, 16), 6);
        QCOMPARE(threadsFor(0.0, 32), 8);
        QCOMPARE(threadsFor(1.0, 8), 6);
        QCOMPARE(threadsFor(5.0, 8), 4);
        QCOMPARE(threadsFor(0.0, 4), 2);
        QCOMPARE(threadsFor(4.0, 4), 2);
        QCOMPARE(threadsFor(0.0, 2), 1);
        QCOMPARE(threadsFor(std::nullopt, 1), 1);
    }

    /// A pasta do build só entra para um executável dentro dela: num pacote, nunca.
    void backendSearchPath()
    {
        using WhisperEngine::backendSearchPath;
        QTemporaryDir tmp;
        const QString root = tmp.filePath(QStringLiteral("build")), other = tmp.filePath(QStringLiteral("build2"));
        for (const QString &d : {root + QStringLiteral("/src/app"), root + QStringLiteral("/ggml"),
                                 other + QStringLiteral("/bin"), tmp.filePath(QStringLiteral("pkg/usr/bin"))})
            QVERIFY(QDir().mkpath(d));
        const QString ggml = root + QStringLiteral("/ggml");
        const QString inside = root + QStringLiteral("/src/app");
        QCOMPARE(backendSearchPath(inside, ggml, root),
                 (QStringList{inside, root + QStringLiteral("/src/lib/openframe"), ggml}));
        const QString package = tmp.filePath(QStringLiteral("pkg/usr/bin"));
        QCOMPARE(backendSearchPath(package, ggml, root),
                 (QStringList{package, tmp.filePath(QStringLiteral("pkg/usr/lib/openframe"))}));
        // Mesmo começo de nome não é estar dentro.
        QCOMPARE(backendSearchPath(other + QStringLiteral("/bin"), ggml, root).size(), 2);
        QCOMPARE(backendSearchPath(root, ggml, root).last(), ggml);
        QVERIFY(backendSearchPath(QString(), ggml, root).isEmpty());
        // AppImage: $APPDIR/usr/lib/openframe depois das da pasta do executável, sem repetir.
        const QString appImage = tmp.filePath(QStringLiteral("pkg"));
        QCOMPARE(backendSearchPath(appImage, ggml, root, appImage),
                 (QStringList{appImage, tmp.filePath(QStringLiteral("lib/openframe")),
                              appImage + QStringLiteral("/usr/lib/openframe")}));
        QCOMPARE(backendSearchPath(package, ggml, root, appImage).size(), 2);
        QCOMPARE(backendSearchPath(inside, QString(), root).size(), 2);
#ifndef Q_OS_WIN
        // Um link para dentro do build conta como dentro (caminhos reais).
        const QString link = tmp.filePath(QStringLiteral("atalho"));
        QVERIFY(QFile::link(inside, link));
        QCOMPARE(backendSearchPath(link, ggml, root).last(), ggml);
#else
        QCOMPARE(backendSearchPath(inside.toUpper(), ggml, root).last(), ggml);
#endif
    }

    /// A medida da CPU existe nos três sistemas e vê duas threads girando (com folga: a máquina pode estar cheia,
    /// e aí elas ganham menos que um núcleo cada, mas o total fica alto do mesmo jeito).
    void busyCores()
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
        std::atomic<bool> stop{false};
        const auto spin = [&] {
            while (!stop.load(std::memory_order_relaxed)) {
            }
        };
        QThread *a = QThread::create(spin), *b = QThread::create(spin);
        a->start();
        b->start();
        const std::optional<double> busy = WhisperEngine::busyCores(std::chrono::milliseconds(300));
        stop = true;
        a->wait();
        b->wait();
        delete a;
        delete b;
        QVERIFY(busy);
        qInfo().noquote() << QStringLiteral("%1 de %2 núcleos ocupados").arg(*busy, 0, 'f', 2).arg(QThread::idealThreadCount());
        QVERIFY2(*busy >= std::min(1.0, QThread::idealThreadCount() / 2.0), qPrintable(QString::number(*busy)));
        QVERIFY(*busy <= QThread::idealThreadCount() + 1e-9);
#else
        QSKIP("sem medida da CPU neste sistema");
#endif
    }

    /// O ajuste pela energia leva as fronteiras para as pausas reais.
    void alignerSnapsToPauses()
    {
        // Três "palavras" de ruído (0,5–1,0 s, 1,4–1,9 s, 2,3–2,8 s) separadas por silêncio.
        const int rate = 16000;
        std::vector<float> s(size_t(3.2 * rate), 0);
        uint32_t seed = 1;
        for (const auto &[a, b] : {std::pair{0.5, 1.0}, {1.4, 1.9}, {2.3, 2.8}})
            for (size_t i = size_t(a * rate); i < size_t(b * rate); ++i) {
                seed = seed * 1664525u + 1013904223u;
                s[i] = (float(seed >> 8) / float(1 << 24) - 0.5f) * 0.6f;
            }
        // Estimativas atrasadas ~0,25 s, como o DTW.
        const QList<EngineWord> rough = {{"um", 0.75, 1.65, 0.9}, {"dois", 1.65, 2.55, 0.9}, {"três", 2.55, 3.05, 0.9}};
        const QList<EngineWord> refined = WordAligner::refine(rough, s, rate);
        QCOMPARE(refined.size(), 3);
        const double starts[] = {0.5, 1.4, 2.3}, ends[] = {1.0, 1.9, 2.8};
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(std::abs(refined[i].start - starts[i]) < 0.05, qPrintable(QString::number(refined[i].start)));
            QVERIFY2(std::abs(refined[i].end - ends[i]) < 0.05, qPrintable(QString::number(refined[i].end)));
        }
        // Sem contraste (ruído constante), nada muda.
        std::vector<float> noise(size_t(3.2 * rate));
        for (float &x : noise) {
            seed = seed * 1664525u + 1013904223u;
            x = (float(seed >> 8) / float(1 << 24) - 0.5f) * 0.6f;
        }
        QCOMPARE(WordAligner::refine(rough, noise, rate), rough);
    }

    // MARK: - Mistura

    /// 48 → 16 kHz: um seno de 1 kHz passa inteiro; um de 12 kHz (acima de 8 kHz) some.
    void downsample()
    {
        const auto tone = [](double hz) {
            std::vector<float> s(48000);
            for (size_t i = 0; i < s.size(); ++i)
                s[i] = float(0.5 * std::sin(2 * M_PI * hz * double(i) / 48000));
            return s;
        };
        const auto low = AudioMixdown::downsample48to16(tone(1000), 16000);
        QVERIFY(std::abs(rms(low, 1000, 15000) - 0.5 / std::sqrt(2.0)) < 0.01);
        QVERIFY(std::abs(frequency(low, 1000, 15000, 16000) - 1000) < 5);
        const auto high = AudioMixdown::downsample48to16(tone(12000), 16000);
        QVERIFY(rms(high, 1000, 15000) < 0.005);
    }

    /// O trecho da timeline em 16 kHz: silêncio antes do clipe, o som no lugar e a velocidade sem mudar o tom.
    void mixdownFollowsTimeline()
    {
        std::vector<float> tone(size_t(2 * 16000));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = float(0.5 * std::sin(2 * M_PI * 440 * double(i) / 16000));
        const auto m = MediaProbe::probe(writeWav(tone, 16000));
        QVERIFY(m);
        Project p;
        const QUuid id = TE::addMedia(*m, p, 1'000'000);
        const auto s = AudioMixdown::samples(p, TimeRange{0, 3'000'000});
        QVERIFY(s);
        QCOMPARE(s->size(), size_t(48000));
        QVERIFY(rms(*s, 0, 15000) < 0.001);
        QVERIFY2(std::abs(rms(*s, 18000, 46000) - 0.5 / std::sqrt(2.0)) < 0.03, qPrintable(QString::number(rms(*s, 18000, 46000))));
        QVERIFY(std::abs(frequency(*s, 18000, 46000, 16000) - 440) < 5);

        // 2×: dura a metade e o tom continua em 440 Hz.
        TE::setSpeed(p, id, 2);
        const auto fast = AudioMixdown::samples(p, TimeRange{0, 3'000'000});
        QVERIFY(fast);
        QVERIFY(rms(*fast, 18000, 31000) > 0.3);
        QVERIFY(rms(*fast, 34000, 48000) < 0.01);
        QVERIFY2(std::abs(frequency(*fast, 18000, 31000, 16000) - 440) < 10,
                 qPrintable(QString::number(frequency(*fast, 18000, 31000, 16000))));

        // Cancelado no meio: vazio.
        QVERIFY(!AudioMixdown::samples(p, TimeRange{0, 3'000'000}, [](double) { return false; }));
        // Sem nenhum som no projeto: vazio.
        QVERIFY(!AudioMixdown::samples(Project{}, TimeRange{0, 1'000'000}));
    }

    // MARK: - Modelos

    /// Baixa, confere o sha256 e só então instala; sha256 e tamanho errados não instalam nada.
    void downloadVerifies()
    {
        QTemporaryDir dir;
        ModelStore store(dir.filePath(QStringLiteral("Models")));
        const QByteArray data = payload(3'000'000);
        const QString source = dir.filePath(QStringLiteral("origem.bin"));
        QFile f(source);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
        f.close();
        const QUrl url = QUrl::fromLocalFile(source);
        QSignalSpy finished(&store, &ModelStore::downloadFinished);
        QSignalSpy progress(&store, &ModelStore::downloadProgress);

        store.download(WhisperModel::Base, url, data.size(), QString(64, u'0'));
        QVERIFY_DOWNLOADS(finished, 1, nullptr);
        QCOMPARE(finished[0][1].toBool(), false);
        QVERIFY(finished[0][2].toString().contains(QStringLiteral("sha256")));
        QVERIFY(!QFile::exists(store.path(WhisperModel::Base)));
        QVERIFY(!QFile::exists(store.partialPath(WhisperModel::Base)));

        store.download(WhisperModel::Base, url, data.size(), hexSha(data));
        QVERIFY(store.isDownloading(WhisperModel::Base));
        QVERIFY_DOWNLOADS(finished, 2, nullptr);
        QVERIFY2(finished[1][1].toBool(), qPrintable(finished[1][2].toString()));
        QFile installed(store.path(WhisperModel::Base));
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(), data);
        QCOMPARE(progress.last()[1].toDouble(), 1.0);
        QVERIFY(!QFile::exists(store.partialPath(WhisperModel::Base)));
        QVERIFY(!store.isDownloading(WhisperModel::Base));

        // Tamanho diferente do anunciado.
        store.download(WhisperModel::Small, url, 10, hexSha(data));
        QVERIFY_DOWNLOADS(finished, 3, nullptr);
        QCOMPARE(finished[2][1].toBool(), false);
        QVERIFY(finished[2][2].toString().contains(QStringLiteral("incompleto")));
        QVERIFY(!QFile::exists(store.path(WhisperModel::Small)));
    }

    /// Download interrompido: o parcial fica e o próximo continua com `Range`.
    void downloadResumes()
    {
        TestServer server;
        server.payload = payload(2'000'000);
        server.dropAfter = 700'000;
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QSignalSpy finished(&store, &ModelStore::downloadFinished);
        store.download(WhisperModel::Base, server.url(), server.payload.size(), hexSha(server.payload));
        QVERIFY_DOWNLOADS(finished, 1, &server);
        QCOMPARE(finished[0][1].toBool(), false);
        QVERIFY(!finished[0][2].toString().isEmpty());
        const qint64 kept = QFileInfo(store.partialPath(WhisperModel::Base)).size();
        QCOMPARE(kept, 700'000);

        server.dropAfter = -1;
        store.download(WhisperModel::Base, server.url(), server.payload.size(), hexSha(server.payload));
        QVERIFY_DOWNLOADS(finished, 2, &server);
        QVERIFY2(finished[1][1].toBool(), qPrintable(finished[1][2].toString()));
        QCOMPARE(server.ranges.last(), QByteArray("bytes=") + QByteArray::number(kept) + '-');
        QFile installed(store.path(WhisperModel::Base));
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(), server.payload);
    }

    /// Rajada e depois pausa: o progresso chega ao que veio, mesmo que os últimos pedaços caiam dentro dos 100 ms
    /// entre dois avisos; nunca volta e não passa de um aviso a cada ~100 ms.
    void progressAfterBurst()
    {
        TestServer server;
        server.payload = payload(2'000'000);
        server.stallAfter = 300'000;
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QSignalSpy progress(&store, &ModelStore::downloadProgress);
        QElapsedTimer clock;
        clock.start();
        store.download(WhisperModel::Base, server.url(), server.payload.size(), hexSha(server.payload));
        QVERIFY_PROGRESS(progress, 300'000, server);
        const qint64 elapsed = clock.elapsed();
        // Parado: nada de aviso repetido.
        QTest::qWait(300);
        QCOMPARE(progress.last()[1].toDouble(), 300'000.0 / 2'000'000);
        for (qsizetype k = 1; k < progress.size(); ++k)
            QVERIFY(progress[k][1].toDouble() > progress[k - 1][1].toDouble());
        QVERIFY2(progress.size() <= 2 + elapsed / 100 + 1, qPrintable(QStringLiteral("%1 avisos em %2 ms")
                                                                         .arg(progress.size()).arg(elapsed)));
        store.cancel(WhisperModel::Base);
    }

    /// Cancelar termina sem erro e guarda o parcial; erro HTTP vira mensagem.
    void downloadCancelAndHttpError()
    {
        TestServer server;
        server.payload = payload(2'000'000);
        server.stallAfter = 300'000;
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QSignalSpy finished(&store, &ModelStore::downloadFinished);
        QSignalSpy progress(&store, &ModelStore::downloadProgress);
        store.download(WhisperModel::Base, server.url(), server.payload.size(), hexSha(server.payload));
        QVERIFY_PROGRESS(progress, 300'000, server);
        store.cancel(WhisperModel::Base);
        // O `abort()` pode terminar na hora, dentro do `cancel`.
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5'000);
        QCOMPARE(finished[0][1].toBool(), false);
        QCOMPARE(finished[0][2].toString(), QString());
        QVERIFY(QFileInfo(store.partialPath(WhisperModel::Base)).size() > 0);
        QVERIFY(store.remove(WhisperModel::Base));
        QVERIFY(!QFile::exists(store.partialPath(WhisperModel::Base)));

        server.stallAfter = -1;
        server.status = 404;
        store.download(WhisperModel::Base, server.url(), server.payload.size(), hexSha(server.payload));
        QVERIFY_DOWNLOADS(finished, 2, &server);
        QCOMPARE(finished[1][1].toBool(), false);
        QVERIFY2(finished[1][2].toString().contains(QStringLiteral("404")), qPrintable(finished[1][2].toString()));
    }

    /// Fora do computador, o proxy do sistema é consultado fora da thread da interface: cancelar durante a consulta
    /// termina na hora; sem cancelar, um endereço que não existe termina em erro (sem travar).
    void downloadQueriesProxyAside()
    {
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QSignalSpy finished(&store, &ModelStore::downloadFinished);
        const QUrl nowhere(QStringLiteral("http://openframe-teste.invalid/modelo.bin"));
        store.download(WhisperModel::Base, nowhere, 1000, QString(64, u'0'));
        QVERIFY(store.isDownloading(WhisperModel::Base));
        store.cancel(WhisperModel::Base);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][1].toBool(), false);
        QCOMPARE(finished[0][2].toString(), QString());
        QVERIFY(!store.isDownloading(WhisperModel::Base));

        store.download(WhisperModel::Base, nowhere, 1000, QString(64, u'0'));
        QVERIFY_DOWNLOADS(finished, 2, nullptr);
        QCOMPARE(finished.size(), 2); // a consulta cancelada não termina de novo
        QCOMPARE(finished[1][1].toBool(), false);
        QVERIFY2(!finished[1][2].toString().isEmpty(), "erro sem mensagem");
    }

    /// Instalado = tamanho certo; apagar tira o modelo e o parcial.
    void installedAndRemove()
    {
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QVERIFY(!store.isInstalled(WhisperModel::Base));
        QFile f(store.path(WhisperModel::Base));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(10, 0));
        f.close();
        QVERIFY(!store.isInstalled(WhisperModel::Base));
        QVERIFY(store.remove(WhisperModel::Base));
        QVERIFY(!QFile::exists(store.path(WhisperModel::Base)));
        QVERIFY(store.path(WhisperModel::Turbo).endsWith(QStringLiteral("ggml-large-v3-turbo-q5_0.bin")));
        QCOMPARE(ModelStore::sha256Of(dir.filePath(QStringLiteral("nada"))), QString());
    }
};

QTEST_GUILESS_MAIN(WhisperUnitTests)
#include "WhisperUnitTests.moc"
