#include "AudioMixdown.h"

#include "AudioMix.h"

#include <QtMath> // M_PI também no MSVC
#include <cmath>

namespace of {

namespace {

/// Filtro passa-baixa (sinc com janela de Blackman), normalizado para ganho 1.
std::vector<float> lowPass(int taps, double cutoff, double rate)
{
    std::vector<float> h(static_cast<size_t>(taps));
    const double fc = cutoff / rate;
    const int mid = taps / 2;
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double x = i - mid;
        const double sinc = x == 0 ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x);
        const double w = 0.42 - 0.5 * std::cos(2 * M_PI * i / (taps - 1)) + 0.08 * std::cos(4 * M_PI * i / (taps - 1));
        h[size_t(i)] = float(sinc * w);
        sum += sinc * w;
    }
    for (float &v : h)
        v = float(v / sum);
    return h;
}

} // namespace

std::vector<float> AudioMixdown::downsample48to16(const std::vector<float> &mono48, size_t count)
{
    static const std::vector<float> h = lowPass(95, 7200, AudioMix::sampleRate);
    const int mid = int(h.size()) / 2;
    std::vector<float> out(count, 0);
    const qint64 n = qint64(mono48.size());
    for (size_t i = 0; i < count; ++i) {
        const qint64 center = qint64(i) * 3;
        double acc = 0;
        for (int k = 0; k < int(h.size()); ++k) {
            const qint64 j = center + k - mid;
            if (j >= 0 && j < n)
                acc += double(h[size_t(k)]) * mono48[size_t(j)];
        }
        out[i] = float(acc);
    }
    return out;
}

std::optional<std::vector<float>> AudioMixdown::samples(const Project &project, TimeRange range,
                                                        const std::function<bool(double)> &progress)
{
    const QList<AudioClip> clips = AudioMix::clips(project);
    if (clips.isEmpty() || range.duration <= 0)
        return std::nullopt;
    const size_t count = size_t(std::llround(double(range.duration) / 1e6 * sampleRate));
    const size_t frames48 = count * 3;
    AudioMixer mixer(clips);
    mixer.seek(range.start);
    std::vector<float> mono(frames48, 0);
    // Em blocos de 1 s: progresso e cancelamento.
    constexpr size_t block = AudioMix::sampleRate;
    std::vector<float> stereo(block * AudioMix::channels);
    for (size_t done = 0; done < frames48;) {
        const size_t n = std::min(block, frames48 - done);
        mixer.read(stereo.data(), int(n));
        for (size_t i = 0; i < n; ++i)
            mono[done + i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
        done += n;
        if (progress && !progress(double(done) / double(frames48)))
            return std::nullopt;
    }
    return downsample48to16(mono, count);
}

} // namespace of
