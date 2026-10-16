import Foundation

/// Operações de edição da timeline. Todas são funções puras sobre `Project`,
/// o que permite testá-las sem interface e implementar desfazer por snapshot.
public enum TimelineEditor {
    public static let minDuration: Micros = 100_000
    public static let defaultPhotoDuration: Micros = 3 * Time.second
    public static let defaultTextDuration: Micros = 3 * Time.second

    // MARK: - Layout

    /// Reposiciona a faixa principal: clipes encostados, com sobreposição
    /// apenas onde há transição.
    public static func relayoutMain(_ p: inout Project) {
        guard let mi = p.mainTrackIndex else { return }
        var t: Micros = 0
        let count = p.tracks[mi].segments.count
        for i in 0..<count {
            p.tracks[mi].segments[i].targetStart = t
            let seg = p.tracks[mi].segments[i]
            var overlap: Micros = 0
            if i + 1 < count, let tr = seg.transition {
                let next = p.tracks[mi].segments[i + 1]
                overlap = max(0, min(tr.duration, seg.targetDuration / 2, next.targetDuration / 2))
            }
            t = seg.targetEnd - overlap
        }
    }

    public static func removeEmptyTracks(_ p: inout Project) {
        p.tracks.removeAll { !$0.isMain && $0.segments.isEmpty }
    }

    private static func normalize(_ p: inout Project) {
        for t in p.tracks.indices where !p.tracks[t].isMain {
            p.tracks[t].segments.sort { $0.targetStart < $1.targetStart }
        }
        relayoutMain(&p)
        removeEmptyTracks(&p)
    }

    // MARK: - Adição

    public static func sourceRange(for m: Material) -> TimeRange {
        switch m.kind {
        case .photo: TimeRange(start: 0, duration: defaultPhotoDuration)
        case .video, .audio: TimeRange(start: 0, duration: max(m.duration, minDuration))
        }
    }

    /// Adiciona o material: visual vai para o fim da faixa principal; áudio
    /// vai para uma faixa de áudio livre na posição `time`.
    @discardableResult
    public static func addMedia(_ m: Material, to p: inout Project, at time: Micros = 0) -> UUID {
        if !p.materials.contains(where: { $0.id == m.id }) { p.materials.append(m) }
        if m.kind == .audio {
            let seg = Segment(materialId: m.id, source: sourceRange(for: m), targetStart: max(0, time))
            place(seg, kind: .audio, in: &p)
            return seg.id
        }
        if p.mainTrackIndex == nil {
            p.tracks.insert(Track(kind: .video, isMain: true), at: 0)
        }
        let mi = p.mainTrackIndex!
        let end = p.tracks[mi].segments.last?.targetEnd ?? 0
        let seg = Segment(materialId: m.id, source: sourceRange(for: m), targetStart: end)
        p.tracks[mi].segments.append(seg)
        relayoutMain(&p)
        return seg.id
    }

    /// Adiciona como sobreposição (picture-in-picture) a partir de `time`.
    @discardableResult
    public static func addOverlay(_ m: Material, to p: inout Project, at time: Micros) -> UUID {
        if !p.materials.contains(where: { $0.id == m.id }) { p.materials.append(m) }
        var seg = Segment(materialId: m.id, source: sourceRange(for: m), targetStart: max(0, time))
        seg.transform.scale = 0.5
        place(seg, kind: .video, in: &p)
        return seg.id
    }

    /// Insere o material solto na timeline em `time`, sobre a faixa `track`
    /// (índice em `p.tracks`; nil fora das faixas). Visual sobre uma sobreposição
    /// ou um texto vira sobreposição; nos demais casos entra na faixa principal,
    /// na junção mais próxima de `time`. Áudio fica na faixa indicada se ela for
    /// de áudio e estiver livre; senão, numa faixa de áudio livre.
    @discardableResult
    public static func insertMedia(_ m: Material, into p: inout Project, at time: Micros, track: Int?) -> UUID {
        if !p.materials.contains(where: { $0.id == m.id }) { p.materials.append(m) }
        let target = track.flatMap { p.tracks.indices.contains($0) ? $0 : nil }
        var seg = Segment(materialId: m.id, source: sourceRange(for: m), targetStart: max(0, time))
        if m.kind == .audio {
            if let t = target, p.tracks[t].kind == .audio, isFree(p.tracks[t], range: seg.targetRange) {
                p.tracks[t].segments.append(seg)
                p.tracks[t].segments.sort { $0.targetStart < $1.targetStart }
            } else {
                place(seg, kind: .audio, in: &p)
            }
            return seg.id
        }
        if let t = target, !p.tracks[t].isMain, p.tracks[t].kind != .audio {
            seg.transform.scale = 0.5
            if p.tracks[t].kind == .video, isFree(p.tracks[t], range: seg.targetRange) {
                p.tracks[t].segments.append(seg)
                p.tracks[t].segments.sort { $0.targetStart < $1.targetStart }
            } else {
                place(seg, kind: .video, in: &p)
            }
            return seg.id
        }
        if p.mainTrackIndex == nil {
            p.tracks.insert(Track(kind: .video, isMain: true), at: 0)
        }
        let mi = p.mainTrackIndex!
        let index = p.tracks[mi].segments.filter { ($0.targetStart + $0.targetEnd) / 2 < time }.count
        p.tracks[mi].segments.insert(seg, at: index)
        relayoutMain(&p)
        return seg.id
    }

    private static func isFree(_ track: Track, range: TimeRange) -> Bool {
        !track.segments.contains { $0.targetStart < range.end && range.start < $0.targetEnd }
    }

    @discardableResult
    public static func addText(_ content: TextContent, to p: inout Project, at time: Micros, duration: Micros = defaultTextDuration) -> UUID {
        let seg = Segment.text(content, at: max(0, time), duration: max(duration, minDuration))
        place(seg, kind: .text, in: &p)
        return seg.id
    }

    // MARK: - Áudio

    /// Se o segmento é um vídeo com som que ainda não foi extraído.
    public static func canExtractAudio(_ p: Project, segment id: UUID) -> Bool {
        guard let seg = p.segment(id), seg.text == nil, !seg.audioDetached,
              let m = p.material(seg.materialId) else { return false }
        return m.kind == .video && m.hasAudio
    }

    /// Extrai o som do clipe para uma faixa de áudio, sem gravar nada em disco: um material de áudio que aponta
    /// para o mesmo arquivo (reaproveitado se outro trecho do mesmo vídeo já foi extraído) e um segmento no mesmo
    /// trecho, com a velocidade, o volume e os fades do clipe. O cruzamento de uma transição da faixa principal
    /// vira fade no segmento novo, para o som não mudar. O clipe fica com `audioDetached` (não soa mais).
    /// Devolve o id do segmento de áudio novo.
    @discardableResult
    public static func extractAudio(_ p: inout Project, segment id: UUID) -> UUID? {
        guard canExtractAudio(p, segment: id), let (t, i) = p.locate(id),
              let video = p.material(p.tracks[t].segments[i].materialId) else { return nil }
        let seg = p.tracks[t].segments[i]
        let name = "Áudio de \(video.name)"
        let audio = p.materials.first { $0.kind == .audio && $0.path == video.path && $0.name == name }
            ?? Material(kind: .audio, path: video.path, name: name, duration: video.duration,
                        width: 0, height: 0, hasAudio: video.hasAudio)
        if !p.materials.contains(where: { $0.id == audio.id }) { p.materials.append(audio) }

        var crossIn: Micros = 0, crossOut: Micros = 0
        if p.tracks[t].isMain {
            let segs = p.tracks[t].segments
            if i > 0 { crossIn = max(0, segs[i - 1].targetEnd - seg.targetStart) }
            if i + 1 < segs.count { crossOut = max(0, seg.targetEnd - segs[i + 1].targetStart) }
        }
        var sound = Segment(materialId: audio.id, source: seg.source, targetStart: seg.targetStart)
        sound.speed = seg.speed
        sound.volume = seg.volume
        sound.fadeIn = max(seg.fadeIn, crossIn)
        sound.fadeOut = max(seg.fadeOut, crossOut)
        p.tracks[t].segments[i].audioDetached = true
        place(sound, kind: .audio, in: &p)
        return sound.id
    }

    /// Põe uma gravação (o material entra no projeto se ainda não estiver) na timeline a partir de `time`, numa
    /// faixa de áudio livre no trecho (ou numa nova). Devolve o id do segmento.
    @discardableResult
    public static func addRecording(_ m: Material, to p: inout Project, at time: Micros) -> UUID {
        if !p.materials.contains(where: { $0.id == m.id }) { p.materials.append(m) }
        let seg = Segment(materialId: m.id, source: sourceRange(for: m), targetStart: max(0, time))
        place(seg, kind: .audio, in: &p)
        return seg.id
    }

    // MARK: - Faixas

    /// Muda a altura de uma faixa (limitada a `Track.heightRange`; nil volta à padrão).
    public static func setTrackHeight(_ p: inout Project, track id: UUID, height: Double?) {
        guard let t = p.tracks.firstIndex(where: { $0.id == id }) else { return }
        p.tracks[t].height = Track.clampedHeight(height)
    }

    /// Muda a altura de todas as faixas de uma vez (o controle global da barra da timeline).
    public static func setAllTrackHeights(_ p: inout Project, height: Double?) {
        let h = Track.clampedHeight(height)
        for t in p.tracks.indices { p.tracks[t].height = h }
    }

    /// Coloca o segmento na primeira faixa não principal do tipo com espaço
    /// livre; cria uma nova faixa se nenhuma servir.
    private static func place(_ seg: Segment, kind: TrackKind, in p: inout Project) {
        if let t = freeTrack(kind: kind, range: seg.targetRange, in: p, excluding: seg.id) {
            p.tracks[t].segments.append(seg)
            p.tracks[t].segments.sort { $0.targetStart < $1.targetStart }
        } else {
            var track = Track(kind: kind)
            track.segments = [seg]
            p.tracks.append(track)
        }
    }

    public static func freeTrack(kind: TrackKind, range: TimeRange, in p: Project, excluding id: UUID?) -> Int? {
        p.tracks.indices.first { t in
            let track = p.tracks[t]
            // A faixa das legendas automáticas é só delas (substituir não pode levar texto do usuário).
            guard track.kind == kind, !track.isMain, !track.isAutoCaptions else { return false }
            return !track.segments.contains { other in
                other.id != id && other.targetStart < range.end && range.start < other.targetEnd
            }
        }
    }

    // MARK: - Edição

    /// Divide o segmento em `time`. Retorna o id da parte direita.
    @discardableResult
    public static func split(_ p: inout Project, segment id: UUID, at time: Micros) -> UUID? {
        guard let (t, i) = p.locate(id) else { return nil }
        let seg = p.tracks[t].segments[i]
        guard time - seg.targetStart >= minDuration, seg.targetEnd - time >= minDuration else { return nil }

        let offsetSrc = Micros((Double(time - seg.targetStart) * seg.speed).rounded())
        var left = seg
        var right = seg
        right.id = UUID()
        left.source.duration = offsetSrc
        right.source.duration = seg.source.duration - offsetSrc
        if seg.text == nil, p.material(seg.materialId)?.kind != .photo {
            right.source.start = seg.source.start + offsetSrc
        }
        right.targetStart = time
        shiftWords(&right, by: seg.targetStart - time)
        left.transition = nil
        left.animationOut = nil
        left.fadeOut = 0
        right.animationIn = nil
        right.fadeIn = 0
        p.tracks[t].segments.replaceSubrange(i...i, with: [left, right])
        normalize(&p)
        return right.id
    }

    public static func delete(_ p: inout Project, segment id: UUID) {
        guard let (t, i) = p.locate(id) else { return }
        p.tracks[t].segments.remove(at: i)
        normalize(&p)
    }

    @discardableResult
    public static func duplicate(_ p: inout Project, segment id: UUID) -> UUID? {
        guard let (t, i) = p.locate(id) else { return nil }
        var copy = p.tracks[t].segments[i]
        copy.id = UUID()
        if p.tracks[t].isMain {
            p.tracks[t].segments.insert(copy, at: i + 1)
        } else {
            copy.targetStart = p.tracks[t].segments[i].targetEnd
            place(copy, kind: p.tracks[t].kind, in: &p)
        }
        normalize(&p)
        return copy.id
    }

    /// Move horizontalmente um segmento de faixa não principal (sem resolver
    /// sobreposições — chame `resolveOverlap` ao soltar).
    public static func move(_ p: inout Project, segment id: UUID, toStart start: Micros) {
        guard let (t, i) = p.locate(id), !p.tracks[t].isMain else { return }
        p.tracks[t].segments[i].targetStart = max(0, start)
    }

    /// Reordena um segmento da faixa principal pela posição do centro. A regra
    /// é a mesma nos dois sentidos: o clipe passa um vizinho quando o centro
    /// dele passa do centro do vizinho; no empate, nada troca.
    public static func reorderMain(_ p: inout Project, segment id: UUID, toCenter center: Micros) {
        guard let (t, i) = p.locate(id), p.tracks[t].isMain else { return }
        let segments = p.tracks[t].segments
        let index = segments.indices.filter { j in
            let c = (segments[j].targetStart + segments[j].targetEnd) / 2
            return j < i ? c <= center : (j > i && c < center)
        }.count
        let seg = p.tracks[t].segments.remove(at: i)
        p.tracks[t].segments.insert(seg, at: index)
        relayoutMain(&p)
    }

    /// Se o segmento ficou sobreposto a outro na mesma faixa, manda-o para
    /// uma faixa livre do mesmo tipo (criando uma se preciso).
    public static func resolveOverlap(_ p: inout Project, segment id: UUID) {
        guard let (t, i) = p.locate(id), !p.tracks[t].isMain else { return }
        let seg = p.tracks[t].segments[i]
        let overlaps = p.tracks[t].segments.contains {
            $0.id != id && $0.targetStart < seg.targetEnd && seg.targetStart < $0.targetEnd
        }
        if overlaps {
            p.tracks[t].segments.remove(at: i)
            place(seg, kind: p.tracks[t].kind, in: &p)
        }
        normalize(&p)
    }

    /// Se a faixa `target` (diferente da atual) aceita o tipo de mídia do segmento.
    public static func canMove(_ p: Project, segment id: UUID, toTrack target: Int) -> Bool {
        guard let (t, i) = p.locate(id), target != t, p.tracks.indices.contains(target) else { return false }
        let seg = p.tracks[t].segments[i]
        let isText = seg.text != nil
        let isAudio = p.material(seg.materialId)?.kind == .audio
        return switch p.tracks[target].kind {
        case .text: isText
        case .audio: isAudio
        case .video: !isText && !isAudio
        }
    }

    /// Move o segmento para outra faixa do mesmo tipo de mídia.
    public static func moveToTrack(_ p: inout Project, segment id: UUID, track target: Int) {
        guard canMove(p, segment: id, toTrack: target), let (t, i) = p.locate(id) else { return }
        let seg = p.tracks[t].segments[i]
        let dest = p.tracks[target]
        p.tracks[t].segments.remove(at: i)
        if dest.isMain {
            let center = (seg.targetStart + seg.targetEnd) / 2
            let index = p.tracks[target].segments.filter { ($0.targetStart + $0.targetEnd) / 2 < center }.count
            p.tracks[target].segments.insert(seg, at: index)
        } else {
            p.tracks[target].segments.append(seg)
        }
        normalize(&p)
        resolveOverlap(&p, segment: id)
    }

    /// Apara o início do segmento até `newStart` (tempo da timeline).
    public static func trimStart(_ p: inout Project, segment id: UUID, to newStart: Micros) {
        guard let (t, i) = p.locate(id) else { return }
        var s = p.tracks[t].segments[i]
        let timed = isTimed(s, in: p)
        var deltaSrc = Micros((Double(newStart - s.targetStart) * s.speed).rounded())
        if timed { deltaSrc = max(deltaSrc, -s.source.start) }
        if !p.tracks[t].isMain { deltaSrc = max(deltaSrc, -Micros(Double(s.targetStart) * s.speed)) }
        deltaSrc = min(deltaSrc, s.source.duration - Micros(Double(minDuration) * s.speed))
        if timed { s.source.start += deltaSrc }
        s.source.duration -= deltaSrc
        let oldStart = s.targetStart
        s.targetStart += Micros((Double(deltaSrc) / s.speed).rounded())
        shiftWords(&s, by: oldStart - s.targetStart)
        p.tracks[t].segments[i] = s
        normalize(&p)
    }

    /// Os tempos das palavras são relativos ao início do segmento: quando o início muda
    /// (aparar, dividir), elas continuam no mesmo instante da timeline.
    private static func shiftWords(_ s: inout Segment, by delta: Micros) {
        guard delta != 0, let words = s.text?.words else { return }
        s.text!.words = words.map { CaptionWord(text: $0.text, start: $0.start + delta, end: $0.end + delta) }
    }

    /// Apara o fim do segmento até `newEnd` (tempo da timeline).
    public static func trimEnd(_ p: inout Project, segment id: UUID, to newEnd: Micros) {
        guard let (t, i) = p.locate(id) else { return }
        var s = p.tracks[t].segments[i]
        let targetDur = max(minDuration, newEnd - s.targetStart)
        var srcDur = Micros((Double(targetDur) * s.speed).rounded())
        if isTimed(s, in: p), let m = p.material(s.materialId) {
            srcDur = min(srcDur, m.duration - s.source.start)
        }
        s.source.duration = max(srcDur, Micros(Double(minDuration) * s.speed))
        p.tracks[t].segments[i] = s
        normalize(&p)
    }

    public static func setSpeed(_ p: inout Project, segment id: UUID, speed: Double) {
        update(&p, segment: id) { $0.speed = min(max(speed, 0.1), 10) }
    }

    /// Altera um segmento e reaplica o layout da faixa principal.
    public static func update(_ p: inout Project, segment id: UUID, _ change: (inout Segment) -> Void) {
        guard let (t, i) = p.locate(id) else { return }
        change(&p.tracks[t].segments[i])
        relayoutMain(&p)
    }

    /// Grava posição/escala/rotação no lugar certo: `text.style` no texto, `transform` no visual.
    public static func setPlacement(_ p: inout Project, segment id: UUID, center: CGPoint, scale: Double, rotation: Double) {
        let scale = max(scale, 0.01)
        update(&p, segment: id) { s in
            if s.text != nil {
                s.text!.style.x = center.x
                s.text!.style.y = center.y
                s.text!.style.scale = scale
                s.text!.style.rotation = rotation
            } else {
                s.transform.x = center.x
                s.transform.y = center.y
                s.transform.scale = scale
                s.transform.rotation = rotation
            }
        }
    }

    /// Muda a largura da caixa de um texto (`style.maxWidth`, limitada a `TextStyle.maxWidthRange`); o texto
    /// se reorganiza. Numa faixa de legendas automáticas, vale para todos os textos da faixa.
    public static func setTextWidth(_ p: inout Project, segment id: UUID, maxWidth: Double) {
        guard maxWidth.isFinite, let (t, i) = p.locate(id), p.tracks[t].segments[i].text != nil else { return }
        let range = TextStyle.maxWidthRange
        let width = min(max(maxWidth, range.lowerBound), range.upperBound)
        if p.tracks[t].isAutoCaptions {
            for k in p.tracks[t].segments.indices where p.tracks[t].segments[k].text != nil {
                p.tracks[t].segments[k].text!.style.maxWidth = width
            }
        } else {
            p.tracks[t].segments[i].text!.style.maxWidth = width
        }
    }

    /// Vídeo e áudio têm duração limitada pelo arquivo; foto e texto não.
    private static func isTimed(_ s: Segment, in p: Project) -> Bool {
        guard let m = p.material(s.materialId) else { return false }
        return m.kind != .photo
    }

    // MARK: - Ímã (snap)

    public static func snapPoints(_ p: Project, excluding id: UUID?, extra: [Micros] = []) -> [Micros] {
        var points: [Micros] = [0] + extra
        for track in p.tracks {
            for s in track.segments where s.id != id {
                points.append(s.targetStart)
                points.append(s.targetEnd)
            }
        }
        return points
    }

    public static func snap(_ t: Micros, to points: [Micros], threshold: Micros) -> Micros {
        nearestPoint(t, in: points, threshold: threshold) ?? t
    }

    /// Ímã ao mover um clipe inteiro: a borda (início ou fim) mais perto de um
    /// ponto vence; no empate, a que vai à frente no sentido do arraste. Assim
    /// arrastar para a direita e para a esquerda se comportam como espelhos.
    public static func snapMove(start: Micros, duration: Micros, to points: [Micros],
                                threshold: Micros, forward: Bool) -> Micros {
        let end = start + duration
        let byStart = nearestPoint(start, in: points, threshold: threshold).map { $0 - start }
        let byEnd = nearestPoint(end, in: points, threshold: threshold).map { $0 - end }
        switch (byStart, byEnd) {
        case let (s?, e?):
            if abs(s) == abs(e) { return start + (forward ? e : s) }
            return start + (abs(s) < abs(e) ? s : e)
        case let (s?, nil): return start + s
        case let (nil, e?): return start + e
        case (nil, nil): return start
        }
    }

    private static func nearestPoint(_ t: Micros, in points: [Micros], threshold: Micros) -> Micros? {
        var best: Micros?
        var bestDistance = threshold + 1
        for point in points {
            let d = abs(point - t)
            if d <= threshold, d < bestDistance {
                best = point
                bestDistance = d
            }
        }
        return best
    }
}
