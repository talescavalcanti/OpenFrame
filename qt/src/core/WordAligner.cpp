#include "WordAligner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace of {

std::vector<double> WordAligner::energy(const std::vector<float> &samples, double sampleRate)
{
    const int hopN = int(hop * sampleRate);
    const int winN = int(window * sampleRate);
    if (hopN <= 0 || int(samples.size()) < winN)
        return {};
    const int frames = (int(samples.size()) - winN) / hopN + 1;
    std::vector<double> out(size_t(frames), -100);
    for (int f = 0; f < frames; ++f) {
        float sum = 0;
        const size_t base = size_t(f) * size_t(hopN);
        for (int i = 0; i < winN; ++i)
            sum += samples[base + size_t(i)] * samples[base + size_t(i)];
        out[size_t(f)] = 10 * std::log10(double(sum) / winN + 1e-10);
    }
    return out;
}

double WordAligner::percentile(std::vector<double> xs, double p)
{
    if (xs.empty())
        return 0;
    std::sort(xs.begin(), xs.end());
    const int i = std::min(int(xs.size()) - 1, std::max(0, int(double(xs.size() - 1) * p)));
    return xs[size_t(i)];
}

namespace {

/// Uma opção de fronteira: a palavra anterior termina em `end` e a seguinte começa em `start`.
struct Candidate {
    double end;
    double start;
    double cost;
};

} // namespace

QList<EngineWord> WordAligner::refine(const QList<EngineWord> &words, const std::vector<float> &samples,
                                      double sampleRate)
{
    constexpr double inf = std::numeric_limits<double>::infinity();
    if (words.isEmpty())
        return words;
    const std::vector<double> e = energy(samples, sampleRate);
    if (e.size() <= 10)
        return words;
    const double floor = percentile(e, 0.1);
    const double level = percentile(e, 0.9);
    // Sem contraste entre fala e fundo (música alta, ruído), os vales não querem dizer nada.
    if (level - floor <= 12)
        return words;
    const double silence = floor + 0.3 * (level - floor);
    const int lastFrame = int(e.size()) - 1;
    const auto frame = [&](double t) { return std::min(lastFrame, std::max(0, int(std::lround(t / hop)))); };
    // Centro do quadro.
    const auto time = [](int f) { return f * hop + window / 2; };
    std::vector<bool> quiet(e.size());
    for (size_t i = 0; i < e.size(); ++i)
        quiet[i] = e[i] < silence;

    // Pausas: trechos contínuos de silêncio.
    struct Pause {
        int a, b;
    };
    QList<Pause> pauses;
    for (int f = 0; f <= lastFrame;) {
        if (quiet[size_t(f)]) {
            int g = f;
            while (g < lastFrame && quiet[size_t(g + 1)])
                ++g;
            pauses.append({f, g});
            f = g + 1;
        } else {
            ++f;
        }
    }

    // Ritmo da fala: segundos com voz por letra, no trecho das palavras.
    QList<int> letters;
    int totalLetters = 0;
    for (const EngineWord &w : words) {
        int n = 0;
        for (const QChar c : w.text)
            n += c.isLetter();
        letters.append(std::max(1, n));
        totalLetters += letters.last();
    }
    const int n = int(words.size());
    int voicedFrames = 0;
    for (int f = frame(std::max(0.0, words[0].start - 0.5)); f <= frame(words[n - 1].end + 0.5); ++f)
        voicedFrames += !quiet[size_t(f)];
    const double perLetter = std::min(0.2, std::max(0.04, voicedFrames * hop / totalLetters));
    const auto durationCost = [&](double d, int word) {
        if (d < minWord)
            return inf;
        const double expected = perLetter * letters[word] + 0.05;
        const double x = std::log(d / expected) / 0.6;
        return x * x;
    };

    // Início da primeira: a primeira voz depois de um silêncio, perto da estimativa.
    const double firstStart = [&] {
        const int lo = frame(words[0].start - before), hi = frame(words[0].start + after);
        if (lo >= hi)
            return words[0].start;
        for (int f = lo; f <= hi; ++f)
            if (!quiet[size_t(f)])
                return time(f);
        return words[0].start;
    }();
    // Fim da última: a última voz antes do fim estimado (com folga).
    const double lastEnd = [&] {
        const int lo = frame(words[n - 1].start), hi = frame(words[n - 1].end + 0.4);
        if (lo >= hi)
            return words[n - 1].end;
        for (int f = hi; f >= lo; --f)
            if (!quiet[size_t(f)])
                return time(f);
        return words[n - 1].end;
    }();

    // Candidatos de cada fronteira i (início da palavra i, i ≥ 1).
    const auto position = [](double t, double rough) {
        const double x = (t - (rough - bias)) / spread;
        return x * x;
    };
    QList<QList<Candidate>> candidates{{Candidate{firstStart, firstStart, 0}}};
    for (int i = 1; i < n; ++i) {
        const double rough = words[i].start;
        const int lo = frame(rough - before), hi = frame(rough + after);
        QList<Candidate> list{{rough, rough, 2.5}};
        for (const Pause &p : pauses)
            if (p.b >= lo && p.a <= hi && p.a > 0)
                list.append({time(p.a), time(p.b), position(time(p.b), rough)});
        // Vales de energia sem silêncio (fala emendada).
        if (lo + 2 < hi)
            for (int g = lo + 1; g < hi; ++g) {
                if (quiet[size_t(g)] || !(e[size_t(g)] < e[size_t(g - 1)]) || !(e[size_t(g)] <= e[size_t(g + 1)]))
                    continue;
                const double left = *std::max_element(e.begin() + std::max(lo, g - 8), e.begin() + g + 1);
                const double right = *std::max_element(e.begin() + g, e.begin() + std::min(hi, g + 8) + 1);
                const double depth = (std::min(left, right) - e[size_t(g)]) / 6;
                if (depth <= 0.3)
                    continue;
                list.append({time(g), time(g), 1 + 1 / depth + position(time(g), rough)});
            }
        candidates.append(list);
    }

    // Programação dinâmica: custo da fronteira + duração da palavra entre duas fronteiras.
    const int count = int(candidates.size());
    QList<QList<double>> best(count);
    QList<QList<int>> back(count);
    best[0] = {0};
    back[0] = {0};
    for (int i = 1; i < count; ++i) {
        best[i] = QList<double>(candidates[i].size(), inf);
        back[i] = QList<int>(candidates[i].size(), 0);
        for (int c = 0; c < candidates[i].size(); ++c) {
            const Candidate &cand = candidates[i][c];
            for (int k = 0; k < candidates[i - 1].size(); ++k) {
                if (!(best[i - 1][k] < inf))
                    continue;
                const double total = best[i - 1][k] + cand.cost + durationCost(cand.end - candidates[i - 1][k].start, i - 1);
                if (total < best[i][c]) {
                    best[i][c] = total;
                    back[i][c] = k;
                }
            }
        }
    }
    const int lastIndex = count - 1;
    int pick = 0;
    double pickCost = inf;
    for (int c = 0; c < candidates[lastIndex].size(); ++c) {
        const double cost = best[lastIndex][c] + durationCost(lastEnd - candidates[lastIndex][c].start, lastIndex);
        if (c == 0 || cost < pickCost)
            pick = c, pickCost = cost;
    }
    if (!(best[lastIndex][pick] < inf))
        return words;
    QList<Candidate> chosen(count, candidates[0][0]);
    for (int i = lastIndex; i >= 0; --i) {
        chosen[i] = candidates[i][pick];
        pick = back[i][pick];
    }

    QList<EngineWord> out = words;
    for (int i = 0; i < out.size(); ++i) {
        out[i].start = chosen[i].start;
        out[i].end = i + 1 < out.size() ? chosen[i + 1].end : std::max(lastEnd, chosen[i].start + minWord);
        out[i].end = std::max(out[i].end, out[i].start);
    }
    return out;
}

} // namespace of
