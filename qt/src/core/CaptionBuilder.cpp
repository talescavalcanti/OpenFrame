#include "CaptionBuilder.h"

#include "TextRenderer.h"
#include "TimelineEditor.h"

#include <algorithm>
#include <cmath>

namespace of::CaptionBuilder {

namespace {

const QSet<QString> &functionWords()
{
    static const QSet<QString> words{
        // português
        "a", "o", "as", "os", "um", "uma", "uns", "umas", "de", "do", "da", "dos", "das", "em", "no", "na", "nos",
        "nas", "por", "pelo", "pela", "pelos", "pelas", "para", "pra", "pro", "com", "sem", "sob", "sobre", "entre",
        "até", "e", "ou", "mas", "que", "se", "ao", "aos", "à", "às", "num", "numa", "meu", "minha", "seu", "sua",
        "nosso", "nossa", "este", "esta", "esse", "essa", "aquele", "aquela", "eu", "tu", "ele", "ela", "nós",
        // inglês
        "the", "an", "of", "to", "in", "on", "at", "for", "with", "by", "from", "and", "or", "but", "as", "into",
        "my", "your", "his", "her", "our", "their", "this", "that", "is", "i",
        // espanhol, francês, italiano e alemão
        "el", "la", "los", "las", "una", "del", "con", "y", "al", "le", "les", "une", "des", "du", "au", "aux",
        "et", "pour", "avec", "sur", "il", "lo", "gli", "di", "per", "der", "die", "das", "ein", "eine", "und",
        "mit", "von", "zu",
    };
    return words;
}

/// Palavras de 5 letras ou mais sem peso (advérbios e pronomes comuns).
const QSet<QString> &weakWords()
{
    static const QSet<QString> words{
        "porque", "quando", "também", "então", "muito", "muita", "muitos", "muitas", "sempre", "agora", "depois",
        "antes", "assim", "ainda", "apenas", "mesmo", "mesma", "outro", "outra", "outros", "outras", "todos",
        "todas", "vocês", "aquilo", "aquele", "aquela", "nossos", "nossas", "dessa", "desse", "deste", "desta",
        "nesse", "nessa", "neste", "nesta", "sobre", "entre", "estou", "estava", "seria", "tinha", "vamos",
        "there", "where", "which", "about", "because", "their", "these", "those", "would", "could", "should",
        "really", "after", "before", "other", "every", "being", "going",
        "cuando", "también", "entonces", "siempre", "ahora", "después", "parce", "quand", "toujours",
    };
    return words;
}

int letterCount(const QString &word)
{
    return int(std::count_if(word.cbegin(), word.cend(), [](QChar c) { return c.isLetter(); }));
}

bool isPunctOrSymbol(QChar c) { return c.isPunct() || c.isSymbol(); }

bool isOpening(const QString &s)
{
    static const QString opening = QStringLiteral("¿¡([{«“\"'");
    return std::all_of(s.cbegin(), s.cend(), [](QChar c) { return opening.contains(c); });
}

/// Partes de `text` separadas por espaço (como o `split(whereSeparator: \.isWhitespace)` do Swift).
QStringList pieces(const QString &text) { return TextContent::wordsOf(text); }

QList<Segment> makeSegments(const QList<CaptionCue> &cues, CaptionPreset preset, const TextStyle &style,
                            const std::optional<RGBA> &accent)
{
    const bool usesKeywords = preset == CaptionPreset::Editorial || preset == CaptionPreset::Stacked;
    QList<Segment> out;
    for (const CaptionCue &cue : cues) {
        TextContent content(cue.text, style);
        QList<CaptionWord> words;
        for (const TranscribedWord &w : cue.words)
            words.append({w.text, w.start - cue.start, w.end - cue.start});
        content.words = words;
        content.highlight = highlight(preset, accent);
        if (usesKeywords) {
            auto e = emphasis(preset, style, accent);
            if (e)
                e->words = keywords(cue);
            content.emphasis = e;
        }
        const Micros duration = std::max(TimelineEditor::minDuration, cue.end - cue.start);
        Segment seg = Segment::makeText(content, std::max<Micros>(0, cue.start), duration);
        animate(seg, preset, duration);
        out.append(seg);
    }
    return out;
}

QUuid insert(const QList<Segment> &segments, const QList<CaptionCue> &cues, Project &project, bool replacing,
             const std::optional<TimeRange> &range)
{
    if (replacing) {
        const auto it = std::find_if(project.tracks.begin(), project.tracks.end(),
                                     [](const Track &t) { return t.isAutoCaptions; });
        if (it != project.tracks.end()) {
            const QUuid keep = it->id;
            std::optional<TimeRange> span = range;
            if (!span && !cues.isEmpty())
                span = TimeRange{cues.first().start, cues.last().end - cues.first().start};
            if (span)
                for (Track &t : project.tracks)
                    if (t.isAutoCaptions)
                        t.segments.removeIf([&](const Segment &s) {
                            return s.targetStart < span->end() && span->start < s.targetEnd();
                        });
            Track &target = *std::find_if(project.tracks.begin(), project.tracks.end(),
                                          [&](const Track &t) { return t.id == keep; });
            target.segments += segments;
            std::stable_sort(target.segments.begin(), target.segments.end(),
                             [](const Segment &a, const Segment &b) { return a.targetStart < b.targetStart; });
            // Faixas automáticas que ficaram vazias somem.
            project.tracks.removeIf([&](const Track &t) { return t.isAutoCaptions && t.id != keep && t.segments.isEmpty(); });
            return keep;
        }
    }
    Track track(TrackKind::Text);
    track.isAutoCaptions = true;
    track.segments = segments;
    project.tracks.append(track);
    return track.id;
}

} // namespace

Limits limits(CaptionLayout layout)
{
    switch (layout) {
    case CaptionLayout::Phrases: return {2, 42, 5'000'000, std::numeric_limits<int>::max(), std::nullopt};
    case CaptionLayout::Highlight: return {1, 32, 4'000'000, std::numeric_limits<int>::max(), std::nullopt};
    case CaptionLayout::Punch: return {1, 20, 2'500'000, 4, 3};
    case CaptionLayout::Stacked: return {3, 12, 2'500'000, 5, 4};
    }
    return {};
}

Limits limits(CaptionPreset preset)
{
    Limits lim = limits(layout(preset));
    // Cinema: frases curtas.
    if (preset == CaptionPreset::Cinema)
        lim.maxDuration = 3'500'000;
    return lim;
}

// MARK: - Medidas

Measure::Measure(const QList<TranscribedWord> &words, const TextStyle &style, double aspect,
                 const std::optional<WordEmphasis> &emphasis)
{
    const double px = 100;
    TextRenderer &r = TextRenderer::shared();
    for (const TranscribedWord &w : words)
        widths.append(r.lineWidth(w.text, style, px) / px);
    if (emphasis) {
        TextStyle es = style;
        es.fontName = emphasis->fontName;
        es.fontFace = emphasis->fontFace;
        es.bold = false;
        es.italic = false;
        const double size = px * emphasis->scale;
        for (int k = 0; k < words.size(); ++k)
            extras.append(canBeKeyword(words[k].text) ? std::max(0.0, r.lineWidth(words[k].text, es, size) / px - widths[k])
                                                      : 0.0);
    } else {
        extras = QList<double>(words.size(), 0.0);
    }
    space = (r.lineWidth(QStringLiteral("a a"), style, px) - 2 * r.lineWidth(QStringLiteral("a"), style, px)) / px;
    // Folga de 3% para a diferença entre as palavras medidas soltas e a linha inteira.
    line = style.fitLines ? INFINITY : style.maxWidth * aspect / std::max(style.fontSize, 0.001) * 0.97;
}

double Measure::reserve(int from, int to) const
{
    QList<double> top(extras.begin() + from, extras.begin() + to);
    std::sort(top.begin(), top.end(), std::greater<>());
    double sum = 0;
    for (int k = 0; k < std::min<int>(int(top.size()), to - from > 6 ? 2 : 1); ++k)
        sum += top[k];
    return sum;
}

int Measure::lineCount(int from, int to, const std::function<bool(int)> &glued) const
{
    const double width = line - reserve(from, to);
    int lines = 1;
    double current = -1;
    for (int k = from; k < to; ++k) {
        double chunk = widths[k];
        while (k + 1 < to && glued && glued(k)) {
            ++k;
            chunk += space + widths[k];
        }
        if (current < 0) {
            current = chunk;
        } else if (current + space + chunk <= width) {
            current += space + chunk;
        } else {
            ++lines;
            current = chunk;
        }
    }
    return lines;
}

double Measure::length(int from, int to) const
{
    double sum = 0;
    for (int k = from; k < to; ++k)
        sum += widths[k];
    return sum + space * std::max(0, to - from - 1);
}

// MARK: - Quebra

QList<CaptionCue> cues(const QList<TranscribedWord> &input, CaptionPreset preset, double aspect)
{
    const QList<TranscribedWord> words = normalized(input);
    if (words.isEmpty())
        return {};
    const Limits lim = limits(preset);
    const TextStyle style = defaultStyle(preset, aspect);
    const Measure measure(words, style, aspect,
                          preset == CaptionPreset::Editorial ? emphasis(preset, style, std::nullopt) : std::nullopt);
    const int n = int(words.size());

    // Programação dinâmica: best[j] = menor custo para quebrar as palavras 0..<j.
    QList<double> best(n + 1, INFINITY);
    QList<int> from(n + 1, 0);
    best[0] = 0;
    for (int j = 1; j <= n; ++j) {
        for (int i = j - 1; i >= 0; --i) {
            const int count = j - i;
            // Uma palavra sozinha sempre cabe (mesmo longa demais).
            if (count > 1) {
                if (count > lim.maxWords)
                    break;
                if (words[j - 1].end - words[i].start > lim.maxDuration)
                    break;
                if (words[i + 1].start - words[i].end >= forcedBreak)
                    break;
                if (measure.lineCount(i, j) > lim.lines)
                    break;
            }
            const double cost = best[i] + cueCost(words, i, j, lim, measure);
            if (cost < best[j]) {
                best[j] = cost;
                from[j] = i;
            }
        }
    }

    QList<std::pair<int, int>> ranges;
    for (int j = n; j > 0; j = from[j])
        ranges.prepend({from[j], j});

    QList<CaptionCue> out;
    for (const auto &[a, b] : ranges) {
        CaptionCue cue;
        cue.words = words.mid(a, b - a);
        cue.start = cue.words.first().start;
        cue.end = std::max(cue.words.last().end, cue.start + 1);
        if (layout(preset) == CaptionLayout::Stacked) {
            QStringList texts;
            for (const auto &w : cue.words)
                texts.append(w.text);
            const QList<int> keys = keywords(cue);
            QStringList lines;
            for (const auto &[x, y] : stackLines(texts, QSet<int>(keys.begin(), keys.end())))
                lines.append(texts.mid(x, y - x).join(u' '));
            cue.text = lines.join(u'\n');
        } else {
            cue.text = text(a, b, words, measure, lim);
        }
        out.append(cue);
    }
    retime(out);
    return out;
}

QList<CaptionCue> cues(const QList<TranscribedWord> &words, CaptionLayout layout, double aspect)
{
    return cues(words, presetFor(layout), aspect);
}

double cueCost(const QList<TranscribedWord> &w, int i, int j, const Limits &limits, const Measure &measure)
{
    double cost = 1;
    if (j < w.size())
        cost += breakCost(w[j - 1], w[j]);
    const int count = j - i;
    if (limits.idealWords) {
        cost += 0.25 * std::abs(count - *limits.idealWords);
        if (count == 1 && j < w.size() && i > 0)
            cost += 0.8;
    } else {
        // Legendas curtas demais piscam: só valem com pausa dos dois lados.
        const double fill = measure.length(i, j) / (limits.lines * measure.line);
        if (fill < 0.5)
            cost += (0.5 - fill) * 2.5;
    }
    // Fim de frase seguido de pausa no meio da legenda: melhor quebrar ali.
    if (count > 1)
        for (int k = i; k < j - 1; ++k) {
            const Micros gap = w[k + 1].start - w[k].end;
            if (endsSentence(w[k].text))
                cost += gap >= pause ? 2.5 : 0.3;
            else if (gap >= 2 * pause)
                cost += 0.6;
        }
    return cost;
}

double breakCost(const TranscribedWord &word, const TranscribedWord &next)
{
    const Micros gap = next.start - word.end;
    if (endsSentence(word.text))
        return 0;
    if (gap >= pause)
        return endsClause(word.text) ? 0 : 0.2;
    if (endsClause(word.text))
        return 0.4;
    return isFunctionWord(word.text) ? 4 : 1.5;
}

QString text(int from, int to, const QList<TranscribedWord> &words, const Measure &measure, const Limits &limits)
{
    QSet<int> glued;
    double chunk = 0;
    for (int k = from; k < to; ++k) {
        chunk = glued.contains(k - 1) ? chunk + measure.space + measure.widths[k] : measure.widths[k];
        if (k + 1 >= to || !isFunctionWord(words[k].text))
            continue;
        if (chunk + measure.space + measure.widths[k + 1] <= measure.line)
            glued.insert(k);
    }
    if (measure.lineCount(from, to, [&](int k) { return glued.contains(k); }) > limits.lines)
        glued.clear();
    QString out;
    for (int k = from; k < to; ++k) {
        out += words[k].text;
        if (k + 1 < to)
            out += glued.contains(k) ? glue : u' ';
    }
    return out;
}

QList<std::pair<int, int>> stackLines(const QStringList &words, const QSet<int> &keywords)
{
    using Parts = QList<std::pair<int, int>>;
    const int n = int(words.size());
    if (n <= 1)
        return {{0, n}};
    const int maxPart = std::max(2, (n + 2) / 3);
    const auto cost = [&](const Parts &parts) {
        double c = 0;
        QList<double> lengths;
        for (const auto &[a, b] : parts) {
            if (b - a > 1) {
                for (int k = a; k < b; ++k)
                    if (keywords.contains(k))
                        c += 6;
                if (isFunctionWord(words[b - 1]))
                    c += 2.5;
                // Fim de frase no meio da linha junta duas ideias.
                for (int k = a; k < b - 1; ++k)
                    if (endsSentence(words[k]) || endsClause(words[k])) {
                        c += 1.5;
                        break;
                    }
            } else if (isFunctionWord(words[a])) {
                c += 5;
            }
            int length = b - a - 1;
            for (int k = a; k < b; ++k)
                length += int(words[k].size());
            lengths.append(length);
        }
        double mean = 0;
        for (const double l : lengths)
            mean += l;
        mean /= lengths.size();
        for (const double l : lengths)
            c += 1.5 * std::abs(std::log(std::max(l, 1.0) / std::max(mean, 1.0)));
        return c;
    };
    std::optional<std::pair<double, Parts>> best;
    std::function<void(int, const Parts &)> visit = [&](int start, const Parts &parts) {
        if (start == n) {
            if (parts.size() < 2)
                return;
            const double c = cost(parts);
            if (!best || c < best->first)
                best = std::pair{c, parts};
            return;
        }
        if (parts.size() >= 3)
            return;
        for (int size = 1; size <= std::min(maxPart, n - start); ++size)
            visit(start + size, parts + Parts{{start, start + size}});
    };
    visit(0, {});
    return best ? best->second : Parts{{0, n}};
}

// MARK: - Palavra-chave

QList<int> keywords(const CaptionCue &cue)
{
    QStringList words;
    for (const auto &w : cue.words)
        words.append(w.text);
    if (words.isEmpty())
        return {};
    struct Scored {
        double score;
        int index;
    };
    QList<Scored> scored;
    for (int i = 0; i < words.size(); ++i) {
        const QString &word = words[i];
        if (!canBeKeyword(word))
            continue;
        double score = std::min(letterCount(word), 12);
        // Sem a classe gramatical do macOS: os advérbios em "-mente" (pt, es, it) perdem peso.
        QString core = word;
        while (!core.isEmpty() && isPunctOrSymbol(core.back()))
            core.chop(1);
        if (core.toLower().endsWith(QStringLiteral("mente")))
            score -= 3;
        if (endsSentence(word) || endsClause(word) || i == words.size() - 1)
            score += 2;
        scored.append({score, i});
    }
    // No empate, a mais adiante: sem a classe gramatical, o fim da frase costuma levar o sentido (no Swift, o
    // etiquetador desempata quase sempre pelo substantivo).
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        return a.score != b.score ? a.score > b.score : a.index > b.index;
    });
    if (scored.isEmpty())
        return {};
    const int first = scored.first().index;
    QList<int> picked{first};
    if (words.size() > 6)
        for (qsizetype k = 1; k < scored.size(); ++k)
            if (std::abs(scored[k].index - first) > 1) {
                picked.append(scored[k].index);
                break;
            }
    std::sort(picked.begin(), picked.end());
    return picked;
}

bool canBeKeyword(const QString &word)
{
    qsizetype a = 0, b = word.size();
    while (a < b && isPunctOrSymbol(word[a]))
        ++a;
    while (b > a && isPunctOrSymbol(word[b - 1]))
        --b;
    const QString core = word.mid(a, b - a);
    return letterCount(core) >= 5 && !isFunctionWord(core) && !weakWords().contains(core.toLower());
}

bool endsSentence(const QString &word)
{
    static const QString closing = QStringLiteral("\"'”’»)]");
    qsizetype a = 0, b = word.size();
    while (a < b && closing.contains(word[a]))
        ++a;
    while (b > a && closing.contains(word[b - 1]))
        --b;
    return b > a && QStringLiteral(".?!…").contains(word[b - 1]);
}

bool endsClause(const QString &word) { return !word.isEmpty() && QStringLiteral(",;:—–").contains(word.back()); }

bool isFunctionWord(const QString &word) { return functionWords().contains(word.toLower()); }

QList<TranscribedWord> normalized(const QList<TranscribedWord> &words)
{
    QList<TranscribedWord> sorted = words;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const TranscribedWord &a, const TranscribedWord &b) { return a.start < b.start; });
    QList<TranscribedWord> out;
    for (TranscribedWord w : sorted) {
        const QStringList parts = pieces(w.text);
        if (parts.isEmpty())
            continue;
        w.text = parts.join(QString());
        w.end = std::max(w.end, w.start);
        const bool loose = std::all_of(w.text.cbegin(), w.text.cend(), isPunctOrSymbol);
        if (loose && !out.isEmpty() && !isOpening(w.text)) {
            out.last().text += w.text;
            out.last().end = std::max(out.last().end, w.end);
            continue;
        }
        out.append(w);
    }
    return out;
}

void retime(QList<CaptionCue> &cues)
{
    for (int k = 0; k < cues.size(); ++k) {
        const Micros next = k + 1 < cues.size() ? cues[k + 1].start : std::numeric_limits<Micros>::max();
        Micros end = cues[k].end;
        if (end - cues[k].start < minDuration)
            end = cues[k].start + minDuration;
        if (next - end < closeGap)
            end = next;
        cues[k].end = std::max(cues[k].start + 1, std::min(end, next));
    }
}

// MARK: - Projeto

QUuid apply(const QList<CaptionCue> &cues, CaptionPreset preset, const std::optional<RGBA> &accent, Project &project,
            bool replacing, const std::optional<TimeRange> &range)
{
    const TextStyle style = defaultStyle(preset, project.aspectValue());
    return insert(makeSegments(cues, preset, style, accent), cues, project, replacing, range);
}

QUuid apply(const QList<CaptionCue> &cues, CaptionLayout layout, const std::optional<TextStyle> &style,
            Project &project, bool replacing, const std::optional<TimeRange> &range)
{
    const CaptionPreset preset = presetFor(layout);
    const TextStyle s = style.value_or(defaultStyle(preset, project.aspectValue()));
    return insert(makeSegments(cues, preset, s, std::nullopt), cues, project, replacing, range);
}

std::optional<WordHighlight> highlight(CaptionPreset preset, const std::optional<RGBA> &accent)
{
    WordHighlight h;
    h.color = accent.value_or(RGBA::yellow());
    switch (preset) {
    case CaptionPreset::Highlight: break;
    case CaptionPreset::Rise:
        h.reveal = WordReveal::Rise;
        h.scale = 1;
        break;
    case CaptionPreset::Guided:
        h.reveal = WordReveal::Dim;
        h.dimOpacity = 0.35;
        h.scale = 1;
        break;
    case CaptionPreset::Fill:
        h.reveal = WordReveal::Fill;
        h.mode = HighlightMode::Progressive;
        h.scale = 1.05;
        break;
    case CaptionPreset::Typing:
        h.reveal = WordReveal::Type;
        h.scale = 1;
        break;
    case CaptionPreset::Phrases:
    case CaptionPreset::Punch:
    case CaptionPreset::Editorial:
    case CaptionPreset::Cinema:
    case CaptionPreset::Stacked: return std::nullopt;
    }
    return h;
}

std::optional<WordEmphasis> emphasis(CaptionPreset preset, const TextStyle &style, const std::optional<RGBA> &accent)
{
    WordEmphasis e;
    e.color = accent.value_or(RGBA::yellow());
    switch (preset) {
    case CaptionPreset::Editorial: break;
    case CaptionPreset::Stacked:
        // A mesma fonte do bloco: só a cor muda (e a linha própria).
        e.fontName = style.fontName;
        e.fontFace = style.fontFace;
        e.scale = 1;
        break;
    default: return std::nullopt;
    }
    return e;
}

void animate(Segment &seg, CaptionPreset preset, Micros duration)
{
    switch (preset) {
    case CaptionPreset::Punch:
        seg.animationIn = SegmentAnimation{AnimationKind::Elastic, std::min<Micros>(350'000, duration / 2)};
        break;
    case CaptionPreset::Rise:
        seg.animationOut = SegmentAnimation{AnimationKind::Fade, std::min<Micros>(150'000, duration / 4)};
        break;
    case CaptionPreset::Editorial:
        seg.animationIn = SegmentAnimation{AnimationKind::Focus, std::min<Micros>(300'000, duration / 3)};
        break;
    case CaptionPreset::Cinema:
        seg.animationIn = SegmentAnimation{AnimationKind::Focus, std::min<Micros>(400'000, duration / 3)};
        seg.animationOut = SegmentAnimation{AnimationKind::Fade, std::min<Micros>(300'000, duration / 4)};
        break;
    case CaptionPreset::Stacked:
        seg.animationIn = SegmentAnimation{AnimationKind::Cascade, std::min<Micros>(400'000, duration / 2)};
        break;
    case CaptionPreset::Phrases:
    case CaptionPreset::Highlight:
    case CaptionPreset::Guided:
    case CaptionPreset::Fill:
    case CaptionPreset::Typing: break;
    }
}

// MARK: - Estilos

TextStyle defaultStyle(CaptionPreset preset, double aspect)
{
    TextStyle s = defaultStyle(preset);
    if (!(aspect > 0) || !std::isfinite(aspect))
        return s;
    if (s.fitLines) {
        // Empilhado: a largura em pixels fica em `stackedBlock` × a altura do quadro (no máximo 90% da largura),
        // e a fonte é proporcional a ela: o bloco mais alto fica em ~45% da altura em qualquer proporção.
        const double block = std::min(stackedBlock, widestBox * aspect);
        s.maxWidth = block / aspect;
        s.fontSize = stackedFont * block;
        return s;
    }
    const double font = std::min(1.0, aspect);
    s.fontSize *= font;
    s.maxWidth = std::min(std::max(s.maxWidth, widestBox), s.maxWidth * ((16.0 / 9) / aspect) * font);
    return s;
}

TextStyle defaultStyle(CaptionPreset preset)
{
    TextStyle s;
    s.fontName = QStringLiteral("Helvetica Neue");
    s.bold = true;
    s.color = RGBA::white();
    s.balanceLines = true;
    switch (preset) {
    case CaptionPreset::Phrases:
        // Legenda clássica: branca, contorno fino e sombra, embaixo.
        s.fontSize = 0.055;
        s.strokeWidth = 8;
        s.strokeColor = RGBA::black();
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.6};
        s.y = -0.78;
        s.maxWidth = 0.66;
        break;
    case CaptionPreset::Highlight:
        s.fontSize = 0.07;
        s.strokeWidth = 12;
        s.strokeColor = RGBA::black();
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.5};
        s.y = -0.55;
        s.maxWidth = 0.64;
        break;
    case CaptionPreset::Punch:
        s.fontSize = 0.11;
        s.textCase = TextCase::Upper;
        s.strokeWidth = 14;
        s.strokeColor = RGBA::black();
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.55};
        s.shadowDistance = 0.06;
        s.y = 0;
        s.maxWidth = 0.76;
        break;
    case CaptionPreset::Rise:
        s.fontSize = 0.07;
        s.strokeWidth = 5;
        s.strokeColor = {0, 0, 0, 0.85};
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.55};
        s.shadowBlur = 0.25;
        s.y = -0.55;
        s.maxWidth = 0.64;
        break;
    case CaptionPreset::Guided:
        s.fontSize = 0.055;
        s.strokeWidth = 0;
        s.shadow = false;
        s.backgroundEnabled = true;
        s.backgroundColor = {0, 0, 0, 0.55};
        s.backgroundRadius = 0.35;
        s.backgroundPaddingX = 0.45;
        s.backgroundPaddingY = 0.25;
        s.y = -0.72;
        s.maxWidth = 0.66;
        break;
    case CaptionPreset::Fill:
        s.fontSize = 0.075;
        s.strokeWidth = 18;
        s.strokeColor = RGBA::black();
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.5};
        s.y = -0.5;
        s.maxWidth = 0.68;
        break;
    case CaptionPreset::Typing:
        s.fontName = QStringLiteral("Menlo");
        s.fontSize = 0.06;
        s.strokeWidth = 0;
        s.shadow = false;
        s.backgroundEnabled = true;
        s.backgroundColor = {0.05, 0.05, 0.07, 0.8};
        s.backgroundRadius = 0.2;
        s.backgroundPaddingX = 0.5;
        s.backgroundPaddingY = 0.3;
        s.y = -0.6;
        s.maxWidth = 0.66;
        break;
    case CaptionPreset::Editorial:
        s.fontFace = QStringLiteral("Light");
        s.bold = false;
        s.fontSize = 0.058;
        s.strokeWidth = 0;
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.45};
        s.shadowDistance = 0.03;
        s.shadowBlur = 0.3;
        s.y = -0.68;
        s.maxWidth = 0.64;
        break;
    case CaptionPreset::Cinema:
        s.fontFace = QStringLiteral("Light");
        s.bold = false;
        s.fontSize = 0.042;
        s.letterSpacing = 0.15;
        s.strokeWidth = 0;
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.5};
        s.shadowDistance = 0.02;
        s.shadowBlur = 0.25;
        s.y = -0.82;
        s.maxWidth = 0.5;
        break;
    case CaptionPreset::Stacked:
        s.fontFace = QStringLiteral("Condensed Black");
        // Fonte grande em relação à caixa: as linhas curtas chegam à largura antes do limite de 2,5×.
        s.fontSize = stackedFont * stackedBlock;
        s.textCase = TextCase::Upper;
        s.fitLines = true;
        s.balanceLines = false;
        s.lineSpacing = -0.15;
        s.strokeWidth = 0;
        s.shadow = true;
        s.shadowColor = {0, 0, 0, 0.5};
        s.shadowBlur = 0.2;
        s.y = 0;
        s.maxWidth = stackedBlock / (16.0 / 9);
        break;
    }
    return s;
}

} // namespace of::CaptionBuilder
