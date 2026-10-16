import Foundation

/// Um projeto da biblioteca, como aparece na tela inicial.
public struct ProjectEntry: Identifiable, Hashable, Sendable {
    public let id: UUID
    public var name: String
    /// Última gravação do projeto.
    public var modified: Date
    public var duration: Micros
    public var aspect: AspectRatio
    /// Proporção efetiva do canvas (em "Original", a do primeiro clipe).
    public var aspectValue: Double
    /// Data da miniatura, para recarregá-la quando muda (nil = ainda sem miniatura).
    public var thumbnailModified: Date?
    public let folder: URL

    public var projectURL: URL { folder.appendingPathComponent(ProjectLibrary.projectFile) }
    public var thumbnailURL: URL { folder.appendingPathComponent(ProjectLibrary.thumbnailFile) }
}

/// Biblioteca de projetos, no espírito dos rascunhos do CapCut: cada projeto é
/// uma pasta (`<id>/project.openframe` + `thumbnail.jpg`) dentro de `root`,
/// gravada automaticamente pelo editor.
public struct ProjectLibrary: Sendable {
    public static let projectFile = "project.\(ProjectStore.fileExtension)"
    public static let thumbnailFile = "thumbnail.jpg"
    /// Caminho do arquivo `.openframe` de origem, para não importar o mesmo arquivo duas vezes.
    static let originFile = "origin.txt"

    public static var defaultRoot: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Movies/OpenFrame/Projetos", isDirectory: true)
    }

    public let root: URL
    /// Apagar manda para o Lixo (no app) ou remove de vez (nos testes).
    public let usesTrash: Bool

    public init(root: URL = ProjectLibrary.defaultRoot, usesTrash: Bool = true) {
        self.root = root
        self.usesTrash = usesTrash
    }

    private var fm: FileManager { .default }

    public func folder(for id: UUID) -> URL {
        root.appendingPathComponent(id.uuidString, isDirectory: true)
    }

    /// Projetos da biblioteca, do editado mais recentemente ao mais antigo.
    /// Pastas sem projeto legível são ignoradas.
    public func list() -> [ProjectEntry] {
        guard let folders = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil) else { return [] }
        return folders.compactMap { folder in
            guard let id = UUID(uuidString: folder.lastPathComponent) else { return nil }
            return entry(id)
        }
        .sorted { $0.modified > $1.modified }
    }

    public func entry(_ id: UUID) -> ProjectEntry? {
        let folder = folder(for: id)
        let url = folder.appendingPathComponent(Self.projectFile)
        guard let project = try? ProjectStore.load(from: url) else { return nil }
        return ProjectEntry(id: id, name: project.name, modified: modificationDate(url) ?? .distantPast,
                            duration: project.duration, aspect: project.canvas.aspect,
                            aspectValue: project.aspectValue,
                            thumbnailModified: modificationDate(folder.appendingPathComponent(Self.thumbnailFile)),
                            folder: folder)
    }

    /// Cria um projeto vazio com o próximo nome livre ("Projeto N").
    @discardableResult
    public func create(aspect: AspectRatio = .original) throws -> (entry: ProjectEntry, project: Project) {
        var project = Project()
        project.name = nextName()
        project.canvas.aspect = aspect
        let id = UUID()
        try save(project, id: id)
        guard let entry = entry(id) else { throw CocoaError(.fileWriteUnknown) }
        return (entry, project)
    }

    public func save(_ project: Project, id: UUID) throws {
        try fm.createDirectory(at: folder(for: id), withIntermediateDirectories: true)
        try ProjectStore.save(project, to: folder(for: id).appendingPathComponent(Self.projectFile))
    }

    public func load(_ id: UUID) throws -> Project {
        try ProjectStore.load(from: folder(for: id).appendingPathComponent(Self.projectFile))
    }

    public func rename(_ id: UUID, to name: String) throws {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        var project = try load(id)
        project.name = trimmed
        try save(project, id: id)
    }

    /// Cópia completa (projeto e miniatura) com o nome "<nome> (cópia)".
    @discardableResult
    public func duplicate(_ id: UUID) throws -> ProjectEntry {
        let newID = UUID()
        try fm.copyItem(at: folder(for: id), to: folder(for: newID))
        try? fm.removeItem(at: folder(for: newID).appendingPathComponent(Self.originFile))
        var project = try load(newID)
        project.name = copyName(project.name)
        try save(project, id: newID)
        guard let entry = entry(newID) else { throw CocoaError(.fileReadCorruptFile) }
        return entry
    }

    /// Manda o projeto para o Lixo (ou apaga, se o Lixo não estiver disponível).
    public func delete(_ id: UUID) throws {
        let folder = folder(for: id)
        if usesTrash, (try? fm.trashItem(at: folder, resultingItemURL: nil)) != nil { return }
        try fm.removeItem(at: folder)
    }

    /// Traz um `.openframe` de fora para a biblioteca. O mesmo arquivo importado
    /// de novo abre o projeto que já existe, em vez de criar outra cópia.
    @discardableResult
    public func importProject(from url: URL) throws -> ProjectEntry {
        let origin = url.standardizedFileURL.path
        for existing in list() {
            let saved = try? String(contentsOf: existing.folder.appendingPathComponent(Self.originFile), encoding: .utf8)
            if saved == origin { return existing }
        }
        var project = try ProjectStore.load(from: url)
        if project.name == Project().name { project.name = url.deletingPathExtension().lastPathComponent }
        let id = UUID()
        try save(project, id: id)
        try origin.write(to: folder(for: id).appendingPathComponent(Self.originFile), atomically: true, encoding: .utf8)
        guard let entry = entry(id) else { throw CocoaError(.fileReadCorruptFile) }
        return entry
    }

    public func writeThumbnail(_ data: Data, id: UUID) throws {
        try data.write(to: folder(for: id).appendingPathComponent(Self.thumbnailFile), options: .atomic)
    }

    /// "Projeto N", com N um a mais que o maior já usado.
    /// Pasta das gravações de áudio: a do projeto `id` da biblioteca (`<pasta do projeto>/Gravações`) ou, num
    /// projeto aberto fora dela (nil), `Gravações` ao lado da pasta dos projetos (`Filmes/OpenFrame/Gravações`).
    public func recordingsFolder(for id: UUID?) -> URL {
        let base = id.map { folder(for: $0) } ?? root.deletingLastPathComponent()
        return base.appendingPathComponent(Self.recordingsFolderName, isDirectory: true)
    }

    public static let recordingsFolderName = "Gravações"

    /// Arquivo da próxima gravação na pasta: "Gravação N.m4a", com N depois do maior que já existe.
    public static func nextRecordingURL(in folder: URL) -> URL {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        let numbers = names.compactMap { name -> Int? in
            let base = (name as NSString).deletingPathExtension
            guard base.hasPrefix("Gravação ") else { return nil }
            return Int(base.dropFirst("Gravação ".count))
        }
        return folder.appendingPathComponent("Gravação \((numbers.max() ?? 0) + 1).m4a")
    }

    /// Onde gravar o próximo áudio do projeto `id` (nil = projeto fora da biblioteca).
    public func recordingURL(for id: UUID?) -> URL {
        Self.nextRecordingURL(in: recordingsFolder(for: id))
    }

    public func nextName() -> String {
        let numbers = list().compactMap { entry -> Int? in
            guard entry.name.hasPrefix("Projeto ") else { return nil }
            return Int(entry.name.dropFirst("Projeto ".count))
        }
        return "Projeto \((numbers.max() ?? 0) + 1)"
    }

    private func copyName(_ name: String) -> String {
        let taken = Set(list().map(\.name))
        var candidate = "\(name) (cópia)"
        var n = 2
        while taken.contains(candidate) {
            candidate = "\(name) (cópia \(n))"
            n += 1
        }
        return candidate
    }

    private func modificationDate(_ url: URL) -> Date? {
        (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate
    }
}
