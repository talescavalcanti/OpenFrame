#include "AudioMix.h"

#include "FFmpeg.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include <QHash>
#include <algorithm>
#include <cmath>

namespace of {

namespace {

/// Ganho no instante `t` (µs, com fração: cada amostra cai entre microssegundos).
float gainAt(const AudioClip &c, double t)
{
    const double start = double(c.targetStart), end = double(c.targetEnd());
    if (t < start || t >= end)
        return 0;
    if (c.fadeIn > 0 && t < start + double(c.fadeIn))
        return c.volume * float((t - start) / double(c.fadeIn));
    if (c.fadeOut > 0 && t >= end - double(c.fadeOut))
        return c.volume * float((end - t) / double(c.fadeOut));
    return c.volume;
}

} // namespace

namespace AudioMix {

qint64 sampleAt(Micros t) { return Time::round(double(t) * sampleRate / 1e6); }

float gain(const AudioClip &clip, Micros t) { return gainAt(clip, double(t)); }

QList<AudioClip> clips(const Project &project)
{
    // Fades extras para cruzar o áudio nas transições da faixa principal.
    QHash<QUuid, Micros> crossIn, crossOut;
    if (const auto main = project.mainTrackIndex()) {
        const QList<Segment> &segs = project.tracks[*main].segments;
        for (qsizetype i = 0; i + 1 < segs.size(); ++i)
            if (segs[i + 1].targetStart < segs[i].targetEnd()) {
                const Micros overlap = segs[i].targetEnd() - segs[i + 1].targetStart;
                crossOut[segs[i].id] = overlap;
                crossIn[segs[i + 1].id] = overlap;
            }
    }

    QList<AudioClip> out;
    const auto add = [&](const Segment &seg, const Material &m) {
        AudioClip c;
        c.segment = seg.id;
        c.path = m.path;
        c.source = seg.source;
        c.targetStart = seg.targetStart;
        c.targetDuration = seg.targetDuration();
        c.speed = std::max(seg.speed, 0.01);
        c.volume = float(std::clamp(seg.volume, 0.0, 1.0));
        c.fadeIn = std::min(std::max(seg.fadeIn, crossIn.value(seg.id)), c.targetDuration / 2);
        c.fadeOut = std::min(std::max(seg.fadeOut, crossOut.value(seg.id)), c.targetDuration / 2);
        out.append(c);
    };
    for (const Track &track : project.tracks) {
        if (track.muted || track.kind == TrackKind::Text)
            continue;
        for (const Segment &seg : track.segments) {
            const Material *m = project.material(seg.materialId);
            if (!m || seg.volume <= 0)
                continue;
            // Com o som extraído para uma faixa de áudio, o clipe de vídeo não soa (como volume 0). O áudio
            // extraído é um material de áudio que aponta para o vídeo; um vídeo com som numa faixa de áudio também toca.
            const bool video = m->kind == MaterialKind::Video && m->hasAudio;
            const bool sounds = track.kind == TrackKind::Video ? video && !seg.audioDetached
                                                               : m->kind == MaterialKind::Audio || video;
            if (sounds)
                add(seg, *m);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const AudioClip &a, const AudioClip &b) { return a.targetStart < b.targetStart; });
    return out;
}

} // namespace AudioMix

namespace {

using AudioMix::channels;
using AudioMix::sampleAt;
using AudioMix::sampleRate;

/// Fila de quadros estéreo intercalados.
struct Fifo {
    std::vector<float> data;
    size_t head = 0;

    qint64 frames() const { return qint64(data.size() - head) / channels; }
    void push(const float *samples, qint64 count) { data.insert(data.end(), samples, samples + count * channels); }
    void pushSilence(qint64 count) { data.insert(data.end(), size_t(count * channels), 0.0f); }
    /// Tira até `count` quadros; devolve quantos tirou.
    qint64 pop(float *out, qint64 count)
    {
        const qint64 n = std::min(count, frames());
        std::copy_n(data.begin() + qsizetype(head), n * channels, out);
        head += size_t(n * channels);
        if (head > 1 << 16 && head * 2 > data.size()) {
            data.erase(data.begin(), data.begin() + qsizetype(head));
            head = 0;
        }
        return n;
    }
    void clear()
    {
        data.clear();
        head = 0;
    }
};

struct GraphDeleter {
    void operator()(AVFilterGraph *g) const { avfilter_graph_free(&g); }
};

/// Fatores do atempo dentro de 0,5…2 cuja multiplicação dá `speed`.
QString tempoChain(double speed)
{
    QStringList parts;
    while (speed < 0.5) {
        parts << QStringLiteral("atempo=0.5");
        speed /= 0.5;
    }
    while (speed > 2) {
        parts << QStringLiteral("atempo=2");
        speed /= 2;
    }
    parts << QStringLiteral("atempo=%1").arg(speed, 0, 'g', 17);
    return parts.join(u',');
}

/// Som de um trecho, em quadros da timeline a partir do início do trecho:
/// decodifica, reamostra para 48 kHz estéreo e, com velocidade ≠ 1, passa
/// pelo atempo.
class ClipReader
{
public:
    static std::unique_ptr<ClipReader> open(const AudioClip &clip)
    {
        auto r = std::unique_ptr<ClipReader>(new ClipReader(clip));
        r->input = ff::openInput(clip.path);
        if (!r->input)
            return nullptr;
        const int index = av_find_best_stream(r->input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (index < 0)
            return nullptr;
        r->stream = r->input->streams[index];
        for (unsigned i = 0; i < r->input->nb_streams; ++i)
            if (int(i) != index)
                r->input->streams[i]->discard = AVDISCARD_ALL;
        r->codec = ff::openDecoder(r->stream);
        if (!r->codec)
            return nullptr;
        if (r->stream->start_time != AV_NOPTS_VALUE)
            r->streamStart = std::max<Micros>(0, ff::toMicros(r->stream->start_time, r->stream->time_base));
        // Mono vai para os dois lados sem atenuar (o swresample usaria −3 dB).
        r->mono = r->codec->ch_layout.nb_channels == 1;
        r->packet = ff::makePacket();
        r->frame = ff::makeFrame();
        if (!r->resetConverters())
            return nullptr;
        r->seek(0);
        return r;
    }

    qint64 position() const { return pos; }

    void seek(qint64 offset)
    {
        pos = offset;
        out.clear();
        source.clear();
        sourceDone = graphFlushed = false;
        drained = false;
        const qint64 sourceFirst = sampleAt(clip.source.start);
        want = sourceFirst + Time::round(double(offset) * clip.speed);
        sourceEnd = sampleAt(clip.source.start + clip.source.duration);
        synced = false;
        // Um pouco antes, para o decodificador (AAC) assentar.
        const Micros t = std::max<Micros>(0, Micros(double(want) * 1e6 / sampleRate) - 100'000);
        av_seek_frame(input.get(), stream->index, ff::fromMicros(t + streamStart, stream->time_base),
                      AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec.get());
        resetConverters();
    }

    /// `frames` quadros estéreo; silêncio depois do fim do arquivo.
    void read(float *dst, int frames)
    {
        while (out.frames() < frames && produce()) {
        }
        const qint64 got = out.pop(dst, frames);
        std::fill(dst + got * channels, dst + qint64(frames) * channels, 0.0f);
        pos += frames;
    }

private:
    explicit ClipReader(const AudioClip &c) : clip(c), tempo(std::abs(c.speed - 1) > 0.0001) {}

    bool resetConverters()
    {
        SwrContext *raw = nullptr;
        AVChannelLayout layout;
        if (mono)
            layout = AV_CHANNEL_LAYOUT_MONO;
        else
            layout = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&raw, &layout, AV_SAMPLE_FMT_FLT, sampleRate, &codec->ch_layout, codec->sample_fmt,
                                codec->sample_rate, 0, nullptr) < 0)
            return false;
        swr.reset(raw);
        if (swr_init(swr.get()) < 0)
            return false;
        return !tempo || buildGraph();
    }

    bool buildGraph()
    {
        graph.reset(avfilter_graph_alloc());
        const QByteArray args = QStringLiteral("sample_rate=%1:sample_fmt=flt:channel_layout=stereo:time_base=1/%1")
                                    .arg(sampleRate)
                                    .toUtf8();
        if (avfilter_graph_create_filter(&src, avfilter_get_by_name("abuffer"), "in", args.constData(), nullptr,
                                         graph.get()) < 0 ||
            avfilter_graph_create_filter(&sink, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr,
                                         graph.get()) < 0)
            return false;
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        const QByteArray chain =
            (tempoChain(clip.speed) + QStringLiteral(",aformat=sample_fmts=flt:channel_layouts=stereo")).toUtf8();
        int r = avfilter_graph_parse_ptr(graph.get(), chain.constData(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (r >= 0)
            r = avfilter_graph_config(graph.get(), nullptr);
        tempoPts = 0;
        return r >= 0;
    }

    /// Acrescenta amostras convertidas em `source`, descartando o que vem antes
    /// de `want` e cortando no fim do trecho.
    void append(const uint8_t **data, int count)
    {
        const int capacity = swr_get_out_samples(swr.get(), count);
        if (capacity <= 0)
            return;
        converted.resize(size_t(capacity) * (mono ? 1 : 2));
        uint8_t *dst = reinterpret_cast<uint8_t *>(converted.data());
        const int got = swr_convert(swr.get(), &dst, capacity, data, count);
        for (int i = 0; i < got && !sourceDone; ++i) {
            const qint64 sample = cursor + i;
            if (sample < want)
                continue;
            if (sample >= sourceEnd) {
                sourceDone = true;
                break;
            }
            const float l = mono ? converted[size_t(i)] : converted[size_t(i) * 2];
            const float r = mono ? l : converted[size_t(i) * 2 + 1];
            const float pair[2] = {l, r};
            source.push(pair, 1);
        }
        cursor += std::max(got, 0);
    }

    /// Decodifica mais um pedaço do arquivo para `source`; falso no fim.
    bool decodeMore()
    {
        if (sourceDone)
            return false;
        for (;;) {
            const int r = avcodec_receive_frame(codec.get(), frame.get());
            if (r == 0) {
                if (!synced) {
                    const int64_t ts =
                        frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
                    const Micros t = ts == AV_NOPTS_VALUE ? 0 : ff::toMicros(ts, stream->time_base) - streamStart;
                    cursor = sampleAt(t);
                    synced = true;
                    // O áudio começa depois do pedido (arquivo com atraso): silêncio até lá.
                    if (cursor > want) {
                        source.pushSilence(std::min(cursor, sourceEnd) - want);
                        want = cursor;
                    }
                }
                append(const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
                av_frame_unref(frame.get());
                return true;
            }
            if (r != AVERROR(EAGAIN) || drained) {
                append(nullptr, 0); // o que restou no reamostrador
                sourceDone = true;
                return false;
            }
            for (;;) {
                if (av_read_frame(input.get(), packet.get()) < 0) {
                    avcodec_send_packet(codec.get(), nullptr);
                    drained = true;
                    break;
                }
                const bool ours = packet->stream_index == stream->index;
                if (ours)
                    avcodec_send_packet(codec.get(), packet.get());
                av_packet_unref(packet.get());
                if (ours)
                    break;
            }
        }
    }

    /// Enche `out`; falso quando não há mais nada a produzir.
    bool produce()
    {
        if (!tempo) {
            const bool more = decodeMore();
            moveAll(source, out);
            return more || out.frames() > 0;
        }
        // Com atempo: entrega o que o filtro tiver; senão, alimenta mais.
        if (pullTempo())
            return true;
        if (graphFlushed)
            return false;
        const bool more = decodeMore();
        if (source.frames() > 0)
            pushTempo();
        if (!more) {
            graphFlushed = true;
            if (av_buffersrc_add_frame(src, nullptr) < 0)
                return false;
        }
        pullTempo();
        return true;
    }

    static void moveAll(Fifo &from, Fifo &to)
    {
        const qint64 n = from.frames();
        if (n == 0)
            return;
        to.push(from.data.data() + from.head, n);
        from.clear();
    }

    void pushTempo()
    {
        const qint64 n = source.frames();
        ff::FramePtr f = ff::makeFrame();
        f->format = AV_SAMPLE_FMT_FLT;
        f->sample_rate = sampleRate;
        av_channel_layout_default(&f->ch_layout, channels);
        f->nb_samples = int(n);
        f->pts = tempoPts;
        tempoPts += n;
        if (av_frame_get_buffer(f.get(), 0) < 0)
            return;
        source.pop(reinterpret_cast<float *>(f->data[0]), n);
        if (av_buffersrc_add_frame(src, f.get()) < 0)
            sourceDone = true;
    }

    bool pullTempo()
    {
        bool any = false;
        while (av_buffersink_get_frame(sink, frame.get()) >= 0) {
            out.push(reinterpret_cast<const float *>(frame->data[0]), frame->nb_samples);
            av_frame_unref(frame.get());
            any = true;
        }
        return any;
    }

    AudioClip clip;
    bool tempo;
    ff::InputPtr input;
    AVStream *stream = nullptr;
    ff::CodecPtr codec;
    ff::SwrPtr swr;
    ff::PacketPtr packet;
    ff::FramePtr frame;
    std::unique_ptr<AVFilterGraph, GraphDeleter> graph;
    AVFilterContext *src = nullptr;
    AVFilterContext *sink = nullptr;
    int64_t tempoPts = 0;
    Micros streamStart = 0;
    bool mono = false;
    std::vector<float> converted;
    /// Amostras do arquivo (48 kHz) a partir de `want` e, depois do atempo, as da timeline.
    Fifo source, out;
    qint64 want = 0, sourceEnd = 0, cursor = 0, pos = 0;
    bool synced = false, drained = false, sourceDone = false, graphFlushed = false;
};

} // namespace

struct AudioMixer::Impl {
    QList<AudioClip> clips;
    struct Slot {
        std::unique_ptr<ClipReader> reader;
        bool failed = false;
        qint64 start = 0, end = 0;
    };
    std::vector<Slot> lanes;
    qint64 pos = 0;
    std::vector<float> buffer;
};

AudioMixer::AudioMixer(QList<AudioClip> clips) : d(std::make_unique<Impl>())
{
    d->clips = std::move(clips);
    for (const AudioClip &c : d->clips) {
        Impl::Slot slot;
        slot.start = sampleAt(c.targetStart);
        slot.end = sampleAt(c.targetEnd());
        d->lanes.push_back(std::move(slot));
    }
}

AudioMixer::~AudioMixer() = default;

void AudioMixer::seek(Micros t) { d->pos = sampleAt(std::max<Micros>(0, t)); }

qint64 AudioMixer::position() const { return d->pos; }

const QList<AudioClip> &AudioMixer::clips() const { return d->clips; }

void AudioMixer::read(float *out, int frames)
{
    std::fill(out, out + qint64(frames) * channels, 0.0f);
    const qint64 a0 = d->pos, b0 = d->pos + frames;
    // Abre os decodificadores até meio segundo antes, para não atrasar a hora de tocar.
    const qint64 preopen = sampleRate / 2;
    for (qsizetype i = 0; i < d->clips.size(); ++i) {
        Impl::Slot &slot = d->lanes[size_t(i)];
        if (slot.end <= a0) {
            slot.reader.reset();
            continue;
        }
        if (slot.start >= b0 + preopen)
            continue;
        if (!slot.reader && !slot.failed) {
            slot.reader = ClipReader::open(d->clips[i]);
            slot.failed = !slot.reader;
        }
        const qint64 a = std::max(a0, slot.start), b = std::min(b0, slot.end);
        if (!slot.reader || a >= b)
            continue;
        if (slot.reader->position() != a - slot.start)
            slot.reader->seek(a - slot.start);
        d->buffer.resize(size_t((b - a) * channels));
        slot.reader->read(d->buffer.data(), int(b - a));
        const AudioClip &clip = d->clips[i];
        for (qint64 j = 0; j < b - a; ++j) {
            const float g = gainAt(clip, double(a + j) * 1e6 / sampleRate);
            float *o = out + (a - a0 + j) * channels;
            o[0] += d->buffer[size_t(j * channels)] * g;
            o[1] += d->buffer[size_t(j * channels + 1)] * g;
        }
    }
    for (qint64 i = 0; i < qint64(frames) * channels; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    d->pos = b0;
}

ClockedMixer::ClockedMixer(QList<AudioClip> clips, Micros base, const QElapsedTimer &clock)
    : m_mixer(std::move(clips)), m_base(base), m_clock(clock)
{
    // Abre os decodificadores que tocam logo depois da partida (o `read` abre os
    // que começam até meio segundo adiante); o pulo da primeira leitura cai perto.
    m_mixer.seek(base);
    float scratch[channels];
    m_mixer.read(scratch, 1);
    m_mixer.seek(base);
}

void ClockedMixer::read(float *out, int frames)
{
    if (m_clock.isValid()) {
        const Micros now = m_base + m_clock.nsecsElapsed() / 1000;
        if (!m_aligned) {
            m_skipped = std::max<Micros>(0, now - m_base);
            m_mixer.seek(now);
        } else if (now - m_mixer.position() * 1'000'000 / sampleRate > maxLag) {
            ++m_resyncs;
            m_mixer.seek(now);
        }
    }
    m_aligned = true;
    m_mixer.read(out, frames);
}

} // namespace of
