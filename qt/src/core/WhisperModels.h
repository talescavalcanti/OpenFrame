#pragma once

#include "Enums.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

namespace of {

/// Modelos do Whisper oferecidos para as legendas automáticas (formato ggml do whisper.cpp).
enum class WhisperModel {
    Base,
    Small,
    /// large-v3-turbo quantizado em q5_0.
    Turbo
};
OF_ENUM_KEYS(WhisperModel, "base", "small", "turbo")

/// "Rápido", "Equilibrado", "Máxima precisão".
QString displayName(WhisperModel);
/// Nome do arquivo no repositório e na pasta local.
QString fileName(WhisperModel);
qint64 byteSize(WhisperModel);
QUrl downloadUrl(WhisperModel);
/// sha256 em hexadecimal minúsculo.
QString sha256(WhisperModel);
/// Modelo indicado na interface. Nesta versão o whisper roda só na CPU, onde o turbo é lento (65 s para 65 s de
/// fala no M5 com 8 threads, contra 12 s do small e 4,4 s do base, todos com WER 0 nessa fala): o recomendado
/// é o small ("Equilibrado").
WhisperModel recommendedModel();

/// Modelos baixados em `QStandardPaths::AppLocalDataLocation/Models` (a variável de ambiente
/// `OPENFRAME_MODELS_DIR` troca a pasta). O download (`QNetworkAccessManager`) é o único acesso à rede do app,
/// só quando o usuário pede. Use da thread do objeto (a da interface, no `shared()`).
class ModelStore : public QObject
{
    Q_OBJECT

public:
    explicit ModelStore(const QString &directory, QObject *parent = nullptr);
    ~ModelStore() override;
    /// Pasta padrão (ou a de `OPENFRAME_MODELS_DIR`).
    static ModelStore &shared();
    static QString defaultDirectory();

    QString directory() const { return m_directory; }
    QString path(WhisperModel m) const;
    /// Instalado = o arquivo final existe com o tamanho certo (o sha256 é conferido ao baixar).
    bool isInstalled(WhisperModel m) const;
    /// Baixa para um `.partial`, confere tamanho e sha256 e só então move para o lugar. Retoma o que já veio
    /// quando o servidor permite (`Range`). Termina com `downloadFinished`. Ignorado se já estiver baixando.
    void download(WhisperModel m);
    /// O mesmo, com a origem, o tamanho e o sha256 à escolha (testes; `file://` também serve, sem retomada).
    void download(WhisperModel m, const QUrl &source, qint64 size, const QString &sha256);
    /// Interrompe o download (o parcial fica para retomar); termina com `downloadFinished(m, false, "")`.
    void cancel(WhisperModel m);
    bool isDownloading(WhisperModel m) const;
    /// Apaga o modelo e o parcial (antes, solta o modelo se ele estiver carregado).
    bool remove(WhisperModel m, QString *error = nullptr);
    /// Caminho do download em andamento ou interrompido.
    QString partialPath(WhisperModel m) const { return path(m) + QStringLiteral(".partial"); }
    /// sha256 (hexadecimal minúsculo) de um arquivo; vazio se não abrir.
    static QString sha256Of(const QString &file);

signals:
    /// 0…1.
    void downloadProgress(of::WhisperModel model, double fraction);
    /// Cancelado = `ok` falso com `error` vazio.
    void downloadFinished(of::WhisperModel model, bool ok, const QString &error);

private:
    struct Impl;
    void finish(WhisperModel m, bool ok, const QString &error);
    QString m_directory;
    std::unique_ptr<Impl> m_jobs;
};

} // namespace of
