#include "ProjectLibrary.h"

#include "ProjectStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimeZone>
#include <algorithm>

namespace of {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

std::optional<QDateTime> modificationDate(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return std::nullopt;
    return info.lastModified();
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

bool copyFolder(const QString &from, const QString &to, QString *error)
{
    if (QFileInfo::exists(to) || !QDir().mkpath(to)) {
        setError(error, QStringLiteral("Não foi possível criar %1").arg(to));
        return false;
    }
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir source(from);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = to + u'/' + source.relativeFilePath(info.filePath());
        if (info.isDir() ? !QDir().mkpath(target) : !QFile::copy(info.filePath(), target)) {
            setError(error, QStringLiteral("Não foi possível copiar %1").arg(info.filePath()));
            return false;
        }
    }
    return true;
}

/// O `Int(_:)` do Swift: dígitos com sinal opcional, sem espaços.
std::optional<int> strictInt(QStringView s)
{
    if (s.isEmpty() || s.front().isSpace() || s.back().isSpace())
        return std::nullopt;
    bool ok = false;
    const int n = s.toInt(&ok);
    return ok ? std::optional(n) : std::nullopt;
}

} // namespace

QString ProjectEntry::projectPath() const { return folder + u'/' + ProjectLibrary::projectFile; }

QString ProjectEntry::thumbnailPath() const { return folder + u'/' + ProjectLibrary::thumbnailFile; }

QString ProjectLibrary::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/OpenFrame/Projetos");
}

ProjectLibrary::ProjectLibrary(QString root, bool usesTrash) : m_root(std::move(root)), m_usesTrash(usesTrash) {}

QString ProjectLibrary::folder(const QUuid &id) const { return m_root + u'/' + ProjectStore::uuidString(id); }

QList<ProjectEntry> ProjectLibrary::list() const
{
    QList<ProjectEntry> entries;
    const QStringList names = QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        // Só pastas com nome de UUID, como no Swift (`UUID(uuidString:)`).
        const QUuid id = QUuid::fromString(name);
        if (id.isNull() || name.size() != 36)
            continue;
        if (auto e = entry(id))
            entries.append(*e);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ProjectEntry &a, const ProjectEntry &b) { return a.modified > b.modified; });
    return entries;
}

std::optional<ProjectEntry> ProjectLibrary::entry(const QUuid &id) const
{
    const QString dir = folder(id);
    const QString path = dir + u'/' + projectFile;
    const auto project = ProjectStore::load(path);
    if (!project)
        return std::nullopt;
    ProjectEntry e;
    e.id = id;
    e.name = project->name;
    e.modified = modificationDate(path).value_or(QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC));
    e.duration = project->duration();
    e.aspect = project->canvas.aspect;
    e.aspectValue = project->aspectValue();
    e.thumbnailModified = modificationDate(dir + u'/' + thumbnailFile);
    e.folder = dir;
    return e;
}

std::optional<ProjectLibrary::Created> ProjectLibrary::create(AspectRatio aspect, QString *error) const
{
    Project project;
    project.name = nextName();
    project.canvas.aspect = aspect;
    const QUuid id = QUuid::createUuid();
    if (!save(project, id, error))
        return std::nullopt;
    const auto e = entry(id);
    if (!e) {
        setError(error, QStringLiteral("Não foi possível ler o projeto criado."));
        return std::nullopt;
    }
    return Created{*e, project};
}

bool ProjectLibrary::save(const Project &project, const QUuid &id, QString *error) const
{
    if (!QDir().mkpath(folder(id))) {
        setError(error, QStringLiteral("Não foi possível criar %1").arg(folder(id)));
        return false;
    }
    return ProjectStore::save(project, folder(id) + u'/' + projectFile, error);
}

std::optional<Project> ProjectLibrary::load(const QUuid &id, QString *error) const
{
    return ProjectStore::load(folder(id) + u'/' + projectFile, error);
}

bool ProjectLibrary::rename(const QUuid &id, const QString &name, QString *error) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return true;
    auto project = load(id, error);
    if (!project)
        return false;
    project->name = trimmed;
    return save(*project, id, error);
}

std::optional<ProjectEntry> ProjectLibrary::duplicate(const QUuid &id, QString *error) const
{
    const QUuid newId = QUuid::createUuid();
    if (!copyFolder(folder(id), folder(newId), error))
        return std::nullopt;
    QFile::remove(folder(newId) + u'/' + originFile);
    auto project = load(newId, error);
    if (!project)
        return std::nullopt;
    project->name = copyName(project->name);
    if (!save(*project, newId, error))
        return std::nullopt;
    const auto e = entry(newId);
    if (!e)
        setError(error, QStringLiteral("Não foi possível ler a cópia."));
    return e;
}

bool ProjectLibrary::remove(const QUuid &id, QString *error) const
{
    const QString dir = folder(id);
    if (m_usesTrash && QFile::moveToTrash(dir))
        return true;
    if (!QDir(dir).removeRecursively()) {
        setError(error, QStringLiteral("Não foi possível apagar %1").arg(dir));
        return false;
    }
    return true;
}

std::optional<ProjectEntry> ProjectLibrary::importProject(const QString &path, QString *error) const
{
    const QString origin = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (const auto &existing : list()) {
        QFile file(existing.folder + u'/' + originFile);
        if (file.open(QIODevice::ReadOnly) && QString::fromUtf8(file.readAll()) == origin)
            return existing;
    }
    auto project = ProjectStore::load(path, error);
    if (!project)
        return std::nullopt;
    if (project->name == Project().name)
        project->name = QFileInfo(path).completeBaseName();
    const QUuid id = QUuid::createUuid();
    if (!save(*project, id, error) || !writeFile(folder(id) + u'/' + originFile, origin.toUtf8(), error))
        return std::nullopt;
    const auto e = entry(id);
    if (!e)
        setError(error, QStringLiteral("Não foi possível ler o projeto importado."));
    return e;
}

bool ProjectLibrary::writeThumbnail(const QByteArray &data, const QUuid &id, QString *error) const
{
    return writeFile(folder(id) + u'/' + thumbnailFile, data, error);
}

QString ProjectLibrary::nextName() const
{
    const QString prefix = QStringLiteral("Projeto ");
    std::optional<int> highest;
    for (const auto &e : list())
        if (e.name.startsWith(prefix))
            if (const auto n = strictInt(QStringView(e.name).mid(prefix.size())))
                highest = std::max(highest.value_or(*n), *n);
    return prefix + QString::number(highest.value_or(0) + 1);
}

QString ProjectLibrary::recordingsFolder(const std::optional<QUuid> &id) const
{
    const QString base = id ? folder(*id) : QFileInfo(m_root).absolutePath();
    return QDir(base).filePath(recordingsFolderName);
}

QString ProjectLibrary::recordingPath(const std::optional<QUuid> &id) const
{
    return nextRecordingPath(recordingsFolder(id));
}

QString ProjectLibrary::nextRecordingPath(const QString &folder)
{
    const QString prefix = QStringLiteral("Gravação ");
    std::optional<int> highest;
    for (const QFileInfo &f : QDir(folder).entryInfoList(QDir::Files)) {
        // Os nomes vindos do sistema de arquivos podem estar decompostos (o "ç" do macOS).
        const QString base = f.completeBaseName().normalized(QString::NormalizationForm_C);
        if (base.startsWith(prefix))
            if (const auto n = strictInt(QStringView(base).mid(prefix.size())))
                highest = std::max(highest.value_or(*n), *n);
    }
    return QDir(folder).filePath(prefix + QString::number(highest.value_or(0) + 1) + QStringLiteral(".m4a"));
}

QString ProjectLibrary::copyName(const QString &name) const
{
    QSet<QString> taken;
    for (const auto &e : list())
        taken.insert(e.name);
    QString candidate = name + QStringLiteral(" (cópia)");
    for (int n = 2; taken.contains(candidate); ++n)
        candidate = name + QStringLiteral(" (cópia %1)").arg(n);
    return candidate;
}

} // namespace of
