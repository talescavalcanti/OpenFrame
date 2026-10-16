// Variantes do backend CPU do ggml (GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS), num processo só para isto
// (o registro de backends do ggml é global): sem nenhuma variante, a transcrição termina com "motor
// indisponível" sem travar; com só a variante mais básica (x64 no x86, apple_m1 no Mac, armv8.0_1 no Linux
// ARM), a transcrição funciona, o que prova que um PC sem AVX2 consegue transcrever.
#include "MediaProbe.h"
#include "TimelineEditor.h"
#include "Transcriber.h"
#include "WhisperEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

using namespace of;

namespace {

QString modelPath()
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
        const QFileInfo info(QDir(dir).filePath(fileName(WhisperModel::Base)));
        if (info.isFile() && info.size() == byteSize(WhisperModel::Base))
            return info.absoluteFilePath();
    }
    return {};
}

/// Arquivo da variante mais básica na pasta do build.
QString baseVariant()
{
#ifdef _WIN32
    const QString prefix = QStringLiteral("ggml-cpu-"), suffix = QStringLiteral(".dll");
#else
    const QString prefix = QStringLiteral("libggml-cpu-"), suffix = QStringLiteral(".so");
#endif
    const QDir dir(QStringLiteral(OPENFRAME_GGML_BUILD_DIR));
    for (const char *name : {"x64", "apple_m1", "armv8.0_1"}) {
        const QString file = dir.filePath(prefix + QLatin1StringView(name) + suffix);
        if (QFile::exists(file))
            return file;
    }
    return {};
}

} // namespace

class WhisperVariantTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifndef OPENFRAME_GGML_DL
        QSKIP("compilado sem as variantes de CPU (OPENFRAME_WHISPER_VARIANTS=OFF)");
#endif
        // As pastas padrão (a do executável, a do build) carregariam a melhor variante: aqui, só a pasta do teste.
        qputenv("OPENFRAME_GGML_DIR", QByteArray("/nenhuma/pasta"));
    }

    /// Aberto por um link em outra pasta (como o AppRun do AppImage), a pasta é a do executável de verdade. Roda
    /// este mesmo teste pelo link, num processo filho, que só imprime a pasta.
    void executableDirectoryFollowsLinks()
    {
        if (qEnvironmentVariableIsSet("OPENFRAME_PRINT_EXE_DIR")) {
            printf("PASTA=%s\n", qPrintable(WhisperEngine::executableDirectory()));
            fflush(stdout);
            return;
        }
        const QString real = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        QCOMPARE(WhisperEngine::executableDirectory(), QFileInfo(real).absolutePath());
#ifdef Q_OS_WIN
        QSKIP("no Windows o QFile::link cria um atalho .lnk, não um link");
#else
        QTemporaryDir dir;
        const QString link = dir.filePath(QStringLiteral("AppRun"));
        QVERIFY(QFile::link(real, link));
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OPENFRAME_PRINT_EXE_DIR"), QStringLiteral("1"));
        child.setProcessEnvironment(env);
        child.start(link, {QStringLiteral("executableDirectoryFollowsLinks")});
        QVERIFY(child.waitForFinished(120'000));
        const QString out = QString::fromUtf8(child.readAllStandardOutput());
        const qsizetype at = out.indexOf(QStringLiteral("PASTA="));
        QVERIFY2(at >= 0, qPrintable(out));
        const QString printed = out.mid(at + 6).section(u'\n', 0, 0).trimmed();
        QCOMPARE(printed, QFileInfo(real).absolutePath());
        QVERIFY(printed != QFileInfo(dir.path()).canonicalFilePath());
#endif
    }

    /// O executável do teste está no build: a pasta do build entra na busca (num pacote, não entraria).
    void buildTreeIncludesBuildDir()
    {
        const QStringList dirs = WhisperEngine::backendSearchPath(WhisperEngine::executableDirectory(),
                                                                  QStringLiteral(OPENFRAME_GGML_BUILD_DIR),
                                                                  QStringLiteral(OPENFRAME_BUILD_ROOT));
        QCOMPARE(dirs.size(), 3);
        QCOMPARE(dirs.last(), QStringLiteral(OPENFRAME_GGML_BUILD_DIR));
    }

    /// Pasta sem variantes: nenhum backend, e a transcrição diz "motor indisponível".
    void noVariantMeansUnavailable()
    {
        QTemporaryDir empty;
        QVERIFY(!WhisperEngine::loadBackends(empty.path()));
        QVERIFY(WhisperEngine::backendDescription().isEmpty());
        Project p;
        const auto m = MediaProbe::probe(QStringLiteral(OPENFRAME_FIXTURES "/fala/bomdia.wav"));
        QVERIFY(m);
        TimelineEditor::addMedia(*m, p);
        QString error;
        const std::atomic<bool> never{false};
        const auto words = Transcriber::run(p, TranscriptionSource::timeline(), QStringLiteral("/nao/existe.bin"),
                                            WhisperModel::Base, {}, never, {}, &error);
        QVERIFY(!words);
        QCOMPARE(error, message(TranscriptionError::EngineUnavailable));
    }

    /// Só a variante mais básica: carrega e transcreve.
    void baseVariantTranscribes()
    {
        const QString variant = baseVariant();
        QVERIFY2(!variant.isEmpty(), OPENFRAME_GGML_BUILD_DIR);
        QTemporaryDir dir;
        QVERIFY(QFile::copy(variant, dir.filePath(QFileInfo(variant).fileName())));
        QVERIFY(WhisperEngine::loadBackends(dir.path()));
        QVERIFY(WhisperEngine::isAvailable());
        qInfo().noquote() << "variante:" << QFileInfo(variant).fileName() << "|" << WhisperEngine::backendDescription();

        if (modelPath().isEmpty()) {
            if (qEnvironmentVariable("OPENFRAME_REQUIRE_WHISPER_MODEL") == QStringLiteral("1"))
                QFAIL("modelo base ausente e OPENFRAME_REQUIRE_WHISPER_MODEL=1");
            QSKIP("Modelo base ausente: rode scripts/baixar-modelo-teste.sh ou defina OPENFRAME_TEST_MODELS_DIR");
        }
        Project p;
        const auto m = MediaProbe::probe(QStringLiteral(OPENFRAME_FIXTURES "/fala/bomdia.wav"));
        QVERIFY(m);
        TimelineEditor::addMedia(*m, p);
        QString error;
        const std::atomic<bool> never{false};
        WhisperEngine::Options options;
        options.language = QStringLiteral("pt");
        const auto words = Transcriber::run(p, TranscriptionSource::timeline(), modelPath(), WhisperModel::Base, options,
                                            never, {}, &error);
        QVERIFY2(words, qPrintable(error));
        QStringList text;
        for (const auto &w : *words)
            text << w.text.toLower();
        qInfo().noquote() << "Transcrição:" << text.join(u' ');
        QVERIFY2(text.join(u' ').contains(QStringLiteral("bom dia")), qPrintable(text.join(u' ')));
    }
};

QTEST_GUILESS_MAIN(WhisperVariantTests)
#include "WhisperVariantTests.moc"
