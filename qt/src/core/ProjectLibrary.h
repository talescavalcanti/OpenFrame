#pragma once

#include "Project.h"

#include <QDateTime>

namespace of {

/// Um projeto da biblioteca, como aparece na tela inicial.
struct ProjectEntry {
    QUuid id;
    QString name;
    /// Última gravação do projeto.
    QDateTime modified;
    Micros duration = 0;
    AspectRatio aspect = AspectRatio::Original;
    /// Proporção efetiva do canvas (em "Original", a do primeiro clipe).
    double aspectValue = 16.0 / 9;
    /// Data da miniatura, para recarregá-la quando muda (vazio = ainda sem miniatura).
    std::optional<QDateTime> thumbnailModified;
    QString folder;

    QString projectPath() const;
    QString thumbnailPath() const;
    bool operator==(const ProjectEntry &) const = default;
};

/// Biblioteca de projetos, no espírito dos rascunhos do CapCut: cada projeto é
/// uma pasta (`<id>/project.openframe` + `thumbnail.jpg`) dentro de `root`,
/// gravada automaticamente pelo editor. O layout é o mesmo da versão Swift.
class ProjectLibrary
{
public:
    static inline const QString projectFile = QStringLiteral("project.openframe");
    static inline const QString thumbnailFile = QStringLiteral("thumbnail.jpg");
    /// Caminho do arquivo `.openframe` de origem, para não importar o mesmo arquivo duas vezes.
    static inline const QString originFile = QStringLiteral("origin.txt");

    /// `<Vídeos>/OpenFrame/Projetos` (`~/Movies` no Mac, `~/Videos` no Windows e no Linux).
    static QString defaultRoot();

    /// `usesTrash`: apagar manda para a Lixeira (no app) ou remove de vez (nos testes).
    explicit ProjectLibrary(QString root = defaultRoot(), bool usesTrash = true);

    const QString &root() const { return m_root; }
    bool usesTrash() const { return m_usesTrash; }

    QString folder(const QUuid &id) const;

    /// Projetos da biblioteca, do editado mais recentemente ao mais antigo.
    /// Pastas sem projeto legível são ignoradas.
    QList<ProjectEntry> list() const;
    std::optional<ProjectEntry> entry(const QUuid &id) const;

    struct Created {
        ProjectEntry entry;
        Project project;
    };
    /// Cria um projeto vazio com o próximo nome livre ("Projeto N").
    std::optional<Created> create(AspectRatio aspect = AspectRatio::Original, QString *error = nullptr) const;

    bool save(const Project &, const QUuid &id, QString *error = nullptr) const;
    std::optional<Project> load(const QUuid &id, QString *error = nullptr) const;
    /// Nome vazio (só espaços) não muda nada.
    bool rename(const QUuid &id, const QString &name, QString *error = nullptr) const;
    /// Cópia completa (projeto e miniatura) com o nome "<nome> (cópia)".
    std::optional<ProjectEntry> duplicate(const QUuid &id, QString *error = nullptr) const;
    /// Manda o projeto para a Lixeira (ou apaga, se a Lixeira não estiver disponível).
    bool remove(const QUuid &id, QString *error = nullptr) const;
    /// Traz um `.openframe` de fora para a biblioteca. O mesmo arquivo importado
    /// de novo abre o projeto que já existe, em vez de criar outra cópia.
    std::optional<ProjectEntry> importProject(const QString &path, QString *error = nullptr) const;
    bool writeThumbnail(const QByteArray &data, const QUuid &id, QString *error = nullptr) const;

    /// "Projeto N", com N um a mais que o maior já usado.
    QString nextName() const;

    /// Pasta das gravações de áudio: a do projeto `id` da biblioteca (`<pasta do projeto>/Gravações`) ou, num
    /// projeto aberto fora dela (vazio), `Gravações` ao lado da pasta dos projetos (`Vídeos/OpenFrame/Gravações`).
    QString recordingsFolder(const std::optional<QUuid> &id) const;
    /// Onde gravar o próximo áudio do projeto `id` (vazio = projeto fora da biblioteca).
    QString recordingPath(const std::optional<QUuid> &id) const;
    /// Arquivo da próxima gravação na pasta: "Gravação N.m4a", com N depois do maior que já existe.
    static QString nextRecordingPath(const QString &folder);
    static inline const QString recordingsFolderName = QStringLiteral("Gravações");

private:
    QString copyName(const QString &name) const;

    QString m_root;
    bool m_usesTrash;
};

} // namespace of
