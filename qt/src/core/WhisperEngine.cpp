#include "WhisperEngine.h"

#include "AudioMixdown.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef OPENFRAME_HAS_WHISPER
#include <ggml-backend.h>
#include <whisper.h>
#endif

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif

namespace of {

namespace {

/// Tempo acumulado de todas as CPUs da máquina, ocupado e total, na unidade do sistema.
struct CpuTimes {
    quint64 busy = 0;
    quint64 total = 0;
};

std::optional<CpuTimes> cpuTimes()
{
#if defined(Q_OS_WIN)
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return std::nullopt;
    const auto value = [](const FILETIME &f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    // O tempo do kernel inclui o ocioso.
    const quint64 total = value(kernel) + value(user);
    return CpuTimes{total - value(idle), total};
#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    const mach_port_t host = mach_host_self();
    const kern_return_t status = host_statistics(host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count);
    mach_port_deallocate(mach_task_self(), host);
    if (status != KERN_SUCCESS)
        return std::nullopt;
    const quint64 busy = quint64(info.cpu_ticks[CPU_STATE_USER]) + info.cpu_ticks[CPU_STATE_SYSTEM] +
                         info.cpu_ticks[CPU_STATE_NICE];
    return CpuTimes{busy, busy + info.cpu_ticks[CPU_STATE_IDLE]};
#elif defined(Q_OS_LINUX)
    // "cpu user nice system idle iowait irq softirq steal guest guest_nice"; guest já está somado em user.
    QFile stat(QStringLiteral("/proc/stat"));
    if (!stat.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    const QList<QByteArray> fields = stat.readLine().simplified().split(' ');
    if (fields.size() < 5 || fields[0] != "cpu")
        return std::nullopt;
    CpuTimes times;
    for (qsizetype i = 1; i < std::min<qsizetype>(fields.size(), 9); ++i) {
        const quint64 v = fields[i].toULongLong();
        times.total += v;
        if (i != 4 && i != 5)
            times.busy += v;
    }
    return times;
#else
    return std::nullopt;
#endif
}

} // namespace

std::optional<double> WhisperEngine::busyCores(std::chrono::milliseconds window)
{
    const auto before = cpuTimes();
    if (!before)
        return std::nullopt;
    QThread::sleep(window);
    // O Mac atualiza os contadores em lotes: às vezes eles ficam ~1 s parados e depois alcançam de uma vez
    // (medido no M5, livre ou cheio). Sem esperar, uma em cada poucas medidas saía vazia (e usava o máximo).
    std::optional<CpuTimes> after = cpuTimes();
    for (int extra = 0; after && after->total == before->total && extra < 30; ++extra) {
        QThread::sleep(std::chrono::milliseconds(50));
        after = cpuTimes();
    }
    // Contadores que voltaram (os de 32 bits do Mac dão a volta depois de semanas ligado): sem medida.
    if (!after || after->total <= before->total || after->busy < before->busy)
        return std::nullopt;
    const double fraction = double(after->busy - before->busy) / double(after->total - before->total);
    return std::clamp(fraction, 0.0, 1.0) * QThread::idealThreadCount();
}

QString WhisperEngine::executableDirectory()
{
    if (!QCoreApplication::instance())
        return {};
#ifdef Q_OS_LINUX
    const QString self = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    if (!self.isEmpty())
        return QFileInfo(self).absolutePath();
#endif
    // `canonicalPath()` só resolveria a pasta: o link é o próprio arquivo.
    const QString real = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    return real.isEmpty() ? QCoreApplication::applicationDirPath() : QFileInfo(real).absolutePath();
}

QStringList WhisperEngine::backendSearchPath(const QString &appDir, const QString &buildDir, const QString &buildRoot,
                                             const QString &appImageDir)
{
    QStringList dirs;
    if (appDir.isEmpty())
        return dirs;
    dirs << appDir << QDir::cleanPath(appDir + QStringLiteral("/../lib/openframe"));
    if (!appImageDir.isEmpty()) {
        const QString packaged = QDir::cleanPath(appImageDir + QStringLiteral("/usr/lib/openframe"));
        if (!dirs.contains(packaged))
            dirs << packaged;
    }
    if (buildDir.isEmpty() || buildRoot.isEmpty())
        return dirs;
    // Pelos caminhos reais (links resolvidos), com a barra no fim: /x/build2 não está dentro de /x/build.
    const auto real = [](const QString &path) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? path : canonical) + u'/';
    };
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (real(appDir).startsWith(real(buildRoot), cs))
        dirs << buildDir;
    return dirs;
}

int WhisperEngine::threadsFor(std::optional<double> busy, int logical)
{
    const int most = std::clamp(logical - 2, 1, 8);
    if (!busy)
        return most;
    return std::clamp(int(std::lround(logical - *busy)), std::min(4, most), most);
}

QList<EngineWord> WhisperEngine::joinTokens(const QList<QList<Token>> &segments)
{
    QList<EngineWord> result;
    for (const QList<Token> &tokens : segments) {
        std::optional<Token> current;
        QList<double> probabilities;
        const auto flush = [&] {
            if (!current)
                return;
            const QString text = QString::fromUtf8(current->bytes).trimmed();
            if (!text.isEmpty()) {
                double sum = 0;
                for (double p : probabilities)
                    sum += p;
                result.append({text, current->start, std::max(current->end, current->start),
                               sum / std::max<qsizetype>(probabilities.size(), 1)});
            }
            current.reset();
            probabilities.clear();
        };
        for (const Token &piece : tokens) {
            if (piece.bytes.startsWith(' ') || !current) {
                flush();
                current = piece;
                probabilities = {piece.probability};
            } else {
                current->bytes += piece.bytes;
                current->end = std::max(current->end, piece.end);
                probabilities.append(piece.probability);
            }
        }
        flush();
    }
    return result;
}

#ifdef OPENFRAME_HAS_WHISPER

namespace {

/// Modelo parado há mais que isso é liberado (o small ocupa ~500 MB).
constexpr qint64 idleUnloadMs = 180'000;

struct Engine {
    QMutex lock;
    whisper_context *context = nullptr;
    QString contextKey;
    qint64 lastUse = 0;
    QMutex backendLock;
    bool triedDefault = false;

    Engine()
    {
        // Os logs do whisper e do ggml vão para o stderr; só com OPENFRAME_WHISPER_LOG=1.
        if (qEnvironmentVariable("OPENFRAME_WHISPER_LOG") != QStringLiteral("1"))
            whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
    }
    ~Engine()
    {
        if (context)
            whisper_free(context);
    }

    static Engine &shared()
    {
        static Engine engine;
        return engine;
    }

    static bool hasCpu() { return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU) != nullptr; }

    static bool logging() { return qEnvironmentVariable("OPENFRAME_WHISPER_LOG") == QStringLiteral("1"); }

    /// Com OPENFRAME_WHISPER_LOG=1: carrega cada variante de `dir` como o ggml (todos os símbolos já) e mostra o
    /// erro do sistema ou a nota. O ggml, num build com otimização, falha calado (`silent` com NDEBUG).
    static void diagnose(const QString &dir)
    {
#ifdef OPENFRAME_GGML_DL
        const QStringList files = QDir(dir).entryList({QStringLiteral("*ggml-cpu*")}, QDir::Files, QDir::Name);
        fprintf(stderr, "[openframe] variantes em %s: %lld\n", qPrintable(QDir::toNativeSeparators(dir)),
                qint64(files.size()));
        for (const QString &file : files) {
            QLibrary library(QDir(dir).filePath(file));
            library.setLoadHints(QLibrary::ResolveAllSymbolsHint);
            if (!library.load()) {
                fprintf(stderr, "[openframe]   %s: falhou: %s\n", qPrintable(file), qPrintable(library.errorString()));
                continue;
            }
            using Score = int (*)();
            const auto score = reinterpret_cast<Score>(library.resolve("ggml_backend_score"));
            fprintf(stderr, "[openframe]   %s: abriu, nota %d\n", qPrintable(file), score ? score() : -1);
            library.unload();
        }
#else
        Q_UNUSED(dir)
#endif
    }

    bool loadFrom(const QString &dir)
    {
#ifdef OPENFRAME_GGML_DL
        if (!hasCpu() && !dir.isEmpty() && QDir(dir).exists()) {
            if (logging())
                diagnose(dir);
            ggml_backend_load_all_from_path(QDir::toNativeSeparators(dir).toUtf8().constData());
        } else if (!hasCpu() && logging()) {
            fprintf(stderr, "[openframe] variantes: %s não existe\n", qPrintable(QDir::toNativeSeparators(dir)));
        }
#else
        Q_UNUSED(dir)
#endif
        return hasCpu();
    }

    bool ensureBackend()
    {
        QMutexLocker locker(&backendLock);
        if (hasCpu())
            return true;
        if (triedDefault)
            return false;
        triedDefault = true;
        // OPENFRAME_GGML_DIR, quando definida, é a única pasta procurada.
        if (const QString env = qEnvironmentVariable("OPENFRAME_GGML_DIR"); !env.isEmpty())
            return loadFrom(env);
        const QStringList dirs =
            WhisperEngine::backendSearchPath(WhisperEngine::executableDirectory(),
                                             QStringLiteral(OPENFRAME_GGML_BUILD_DIR), QStringLiteral(OPENFRAME_BUILD_ROOT),
                                             qEnvironmentVariable("APPDIR"));
        for (const QString &dir : dirs)
            if (loadFrom(dir))
                return true;
        if (logging())
            fprintf(stderr, "[openframe] nenhum backend de CPU em: %s\n",
                    qPrintable(QDir::toNativeSeparators(dirs.join(QStringLiteral(" | ")))));
        return false;
    }

    void scheduleIdleUnload()
    {
        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(app, [] {
            QTimer::singleShot(idleUnloadMs + 1000, QCoreApplication::instance(), [] {
                Engine &e = shared();
                if (!e.lock.tryLock())
                    return;
                if (e.context && QDateTime::currentMSecsSinceEpoch() - e.lastUse >= idleUnloadMs) {
                    whisper_free(e.context);
                    e.context = nullptr;
                    e.contextKey.clear();
                }
                e.lock.unlock();
            });
        });
    }
};

whisper_alignment_heads_preset alignmentHeads(std::optional<WhisperModel> model)
{
    if (!model)
        return WHISPER_AHEADS_N_TOP_MOST;
    switch (*model) {
    case WhisperModel::Base: return WHISPER_AHEADS_BASE;
    case WhisperModel::Small: return WHISPER_AHEADS_SMALL;
    case WhisperModel::Turbo: return WHISPER_AHEADS_LARGE_V3_TURBO;
    }
    return WHISPER_AHEADS_N_TOP_MOST;
}

struct Callbacks {
    const std::function<void(double)> *progress;
    const std::atomic<bool> *cancelled;
};

} // namespace

bool WhisperEngine::isAvailable() { return Engine::shared().ensureBackend(); }

bool WhisperEngine::loadBackends(const QString &directory)
{
    Engine &e = Engine::shared();
    QMutexLocker locker(&e.backendLock);
    return e.loadFrom(directory);
}

QString WhisperEngine::backendDescription()
{
    ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    if (!dev)
        return {};
    ggml_backend_reg_t reg = ggml_backend_dev_backend_reg(dev);
    // Recursos da CPU que a variante carregada usa (AVX2, DOTPROD…): mostra qual foi escolhida.
    QStringList features;
    using GetFeatures = ggml_backend_feature *(*)(ggml_backend_reg_t);
    if (reg)
        if (auto get = reinterpret_cast<GetFeatures>(ggml_backend_reg_get_proc_address(reg, "ggml_backend_get_features")))
            for (ggml_backend_feature *f = get(reg); f && f->name; ++f)
                features << QString::fromUtf8(f->name);
    return QStringLiteral("%1 (%2)%3")
        .arg(QString::fromUtf8(reg ? ggml_backend_reg_name(reg) : "CPU"),
             QString::fromUtf8(ggml_backend_dev_description(dev)).trimmed(),
             features.isEmpty() ? QString() : QStringLiteral(": ") + features.join(u' '));
}

void WhisperEngine::unload()
{
    Engine &e = Engine::shared();
    QMutexLocker locker(&e.lock);
    if (e.context)
        whisper_free(e.context);
    e.context = nullptr;
    e.contextKey.clear();
}

std::optional<QList<EngineWord>> WhisperEngine::transcribe(const std::vector<float> &samples, const QString &modelPath,
                                                           std::optional<WhisperModel> model, const Options &options,
                                                           const std::atomic<bool> &cancelled,
                                                           const std::function<void(double)> &progress, QString *error)
{
    const auto fail = [&](const QString &message) -> std::optional<QList<EngineWord>> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    Engine &e = Engine::shared();
    if (!e.ensureBackend())
        return fail(QStringLiteral("nenhum backend de CPU do ggml foi encontrado"));
    QMutexLocker locker(&e.lock);
    const auto done = qScopeGuard([&] {
        e.lastUse = QDateTime::currentMSecsSinceEpoch();
        e.scheduleIdleUnload();
    });

    const QString key = QStringLiteral("%1|%2").arg(modelPath).arg(int(options.timing));
    if (!e.context || e.contextKey != key) {
        if (e.context)
            whisper_free(e.context);
        e.context = nullptr;
        e.contextKey.clear();
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false;
        if (options.timing == Timing::Dtw) {
            // O DTW precisa das matrizes de atenção: sem flash attention.
            cparams.flash_attn = false;
            cparams.dtw_token_timestamps = true;
            cparams.dtw_aheads_preset = alignmentHeads(model);
            if (cparams.dtw_aheads_preset == WHISPER_AHEADS_N_TOP_MOST)
                cparams.dtw_n_top = 2;
        } else {
            cparams.flash_attn = true;
        }
        e.context = whisper_init_from_file_with_params(QFile::encodeName(modelPath).constData(), cparams);
        if (!e.context)
            return fail(QStringLiteral("não foi possível abrir o modelo %1").arg(QFileInfo(modelPath).fileName()));
        e.contextKey = key;
    }
    if (cancelled)
        return std::nullopt;

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.beam_search.beam_size = 5;
    // Medido depois de carregar o modelo: a janela só vê os outros processos.
    params.n_threads = options.threads > 0
                           ? options.threads
                           : threadsFor(busyCores(std::chrono::milliseconds(200)), QThread::idealThreadCount());
    if (cancelled)
        return std::nullopt;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;
    params.translate = false;
    // Sem o texto anterior como contexto: com ele, fala repetitiva fazia o turbo pular trechos inteiros.
    params.no_context = true;
    params.suppress_nst = true;
    params.token_timestamps = options.timing == Timing::Tokens;
    Callbacks callbacks{&progress, &cancelled};
    params.progress_callback = [](whisper_context *, whisper_state *, int percent, void *user) {
        const auto *c = static_cast<Callbacks *>(user);
        if (*c->progress)
            (*c->progress)(percent / 100.0);
    };
    params.progress_callback_user_data = &callbacks;
    params.abort_callback = [](void *user) { return static_cast<Callbacks *>(user)->cancelled->load(); };
    params.abort_callback_user_data = &callbacks;
    params.encoder_begin_callback = [](whisper_context *, whisper_state *, void *user) {
        return !static_cast<Callbacks *>(user)->cancelled->load();
    };
    params.encoder_begin_callback_user_data = &callbacks;
    const QByteArray language = options.language.isEmpty() ? QByteArrayLiteral("auto") : options.language.toUtf8();
    params.language = language.constData();
    params.detect_language = false;

    const int status = whisper_full(e.context, params, samples.data(), int(samples.size()));
    if (cancelled)
        return std::nullopt;
    if (status != 0)
        return fail(QStringLiteral("o whisper.cpp devolveu o código %1").arg(status));
    if (progress)
        progress(1);

    whisper_context *ctx = e.context;
    const whisper_token eot = whisper_token_eot(ctx);
    QList<QList<Token>> segments;
    for (int s = 0; s < whisper_full_n_segments(ctx); ++s) {
        const double segEnd = whisper_full_get_segment_t1(ctx, s) / 100.0;
        QList<Token> tokens;
        for (int t = 0; t < whisper_full_n_tokens(ctx, s); ++t) {
            const whisper_token_data data = whisper_full_get_token_data(ctx, s, t);
            const char *text = whisper_full_get_token_text(ctx, s, t);
            if (data.id >= eot || !text || !*text)
                continue;
            Token token{QByteArray(text), 0, 0, double(data.p)};
            if (options.timing == Timing::Tokens) {
                token.start = data.t0 / 100.0;
                token.end = data.t1 / 100.0;
            } else {
                token.start = token.end = data.t_dtw / 100.0;
            }
            tokens.append(token);
        }
        // No DTW, o fim de cada token é o início do próximo (o último vai até o fim do segmento).
        if (options.timing == Timing::Dtw)
            for (int i = 0; i < tokens.size(); ++i)
                tokens[i].end = i + 1 < tokens.size() ? std::max(tokens[i].start, tokens[i + 1].start)
                                                      : std::max(tokens[i].start, segEnd);
        segments.append(tokens);
    }
    const QList<EngineWord> words = joinTokens(segments);
    return options.refine ? WordAligner::refine(words, samples, AudioMixdown::sampleRate) : words;
}

#else

bool WhisperEngine::isAvailable() { return false; }
bool WhisperEngine::loadBackends(const QString &) { return false; }
QString WhisperEngine::backendDescription() { return {}; }
void WhisperEngine::unload() {}
std::optional<QList<EngineWord>> WhisperEngine::transcribe(const std::vector<float> &, const QString &,
                                                           std::optional<WhisperModel>, const Options &,
                                                           const std::atomic<bool> &,
                                                           const std::function<void(double)> &, QString *error)
{
    if (error)
        error->clear();
    return std::nullopt;
}

#endif

} // namespace of
