#pragma once

#include "WhisperModels.h"
#include "WordAligner.h"

#include <QStringList>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <vector>

namespace of {

/// Ponte para o whisper.cpp na CPU (interno: o app usa o `Transcriber`). Um contexto (modelo) fica carregado
/// entre as transcrições e é liberado depois de 3 min parado; as transcrições são uma de cada vez. Mesmos
/// parâmetros do `WhisperEngine.swift`.
///
/// Com as variantes de CPU (`OPENFRAME_GGML_DL`), o backend é carregado na primeira transcrição (nunca ao abrir
/// o app), da primeira pasta que tiver um: a pasta do executável, `../lib/openframe` a partir dela (AppImage) e a
/// pasta do build; `OPENFRAME_GGML_DIR`, quando definida, é a única procurada. O ggml escolhe a variante de maior
/// nota que a CPU suporta.
namespace WhisperEngine {

/// Como os tempos das palavras são calculados.
enum class Timing {
    /// `token_timestamps`: t0/t1 de cada token pelas probabilidades dos tokens de tempo.
    Tokens,
    /// DTW sobre as cabeças de alinhamento do modelo: o início de cada token.
    Dtw
};

struct Options {
    /// Vazio = detectar.
    QString language;
    Timing timing = Timing::Dtw;
    /// Ajusta as fronteiras pela energia do áudio (`WordAligner`).
    bool refine = true;
    /// 0 = automático: `threadsFor` com a CPU medida logo antes de transcrever. Senão, o número de threads.
    int threads = 0;
};

/// Núcleos ocupados na máquina inteira (o uso somado de todas as CPUs, em núcleos), medidos ao longo de `window`
/// (bloqueia esse tempo; até 1,5 s a mais se os contadores do sistema estiverem parados). Vazio se o sistema não
/// informa.
std::optional<double> busyCores(std::chrono::milliseconds window);
/// Threads para transcrever com `busy` dos `logical` núcleos ocupados: os livres, entre min(4, máximo) e o
/// máximo (os núcleos menos 2, até 8); o máximo quando `busy` é vazio.
///
/// A barreira entre as operações do ggml gira sem ceder a CPU: com mais threads que núcleos livres, uma thread
/// sem vez segura as outras, e a transcrição fica de 2 a 25 vezes mais lenta (medido no API.md). Ceder a vez na
/// barreira foi pior: quem cede perde a CPU para quem continua girando. Abaixo de 4, mesmo numa CPU cheia, fica
/// mais lento: o sistema divide o tempo entre todos, e mais threads ganham mais fatias.
int threadsFor(std::optional<double> busy, int logical);

/// O whisper foi compilado e há um backend de CPU (carrega das pastas padrão na primeira chamada).
bool isAvailable();
/// Carrega o backend de CPU só de `directory` (testes). Verdadeiro se, depois disso, há um backend de CPU.
/// Sem efeito se um já estiver carregado.
bool loadBackends(const QString &directory);
/// Backend de CPU carregado, a descrição da CPU e os recursos que a variante usa; vazio sem backend.
QString backendDescription();
/// Pasta real do executável, com os links resolvidos (vazio sem QCoreApplication). O AppImage abre pelo
/// `$APPDIR/AppRun`, um link para `usr/bin/OpenFrame`, e no Linux o `applicationDirPath()` do Qt dá a pasta do link
/// (a raiz do AppDir): no Linux vem do `/proc/self/exe`.
QString executableDirectory();
/// Pastas onde procurar as variantes, em ordem: a do executável, `../lib/openframe` a partir dela,
/// `<appImageDir>/usr/lib/openframe` (o `$APPDIR` do AppImage) e, só se o executável estiver dentro de `buildRoot` (o
/// build, nunca um pacote), `buildDir`. Vazios e repetidos ficam de fora.
QStringList backendSearchPath(const QString &appDir, const QString &buildDir, const QString &buildRoot,
                              const QString &appImageDir = {});

/// Transcreve áudio mono de 16 kHz. Bloqueia a thread. Vazio com `cancelled` (sem erro) ou com `error`.
std::optional<QList<EngineWord>> transcribe(const std::vector<float> &samples, const QString &modelPath,
                                            std::optional<WhisperModel> model, const Options &options,
                                            const std::atomic<bool> &cancelled,
                                            const std::function<void(double)> &progress, QString *error);

/// Libera o modelo carregado (ex.: antes de apagá-lo).
void unload();

/// Junta tokens em palavras: um token que começa com espaço abre uma palavra nova; os outros (sub-palavras,
/// pontuação grudada) continuam a anterior. Os bytes são juntados antes de virar texto (um caractere acentuado
/// pode vir partido em dois tokens). Exposto para os testes.
struct Token {
    QByteArray bytes;
    double start = 0;
    double end = 0;
    double probability = 0;
};
QList<EngineWord> joinTokens(const QList<QList<Token>> &segments);
} // namespace WhisperEngine

} // namespace of
