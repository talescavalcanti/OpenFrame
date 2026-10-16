#include "LinearImage.h"

#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <array>
#include <cmath>

namespace of {

namespace {

float srgbToLinearExact(float v)
{
    const float a = std::abs(v);
    const float r = a <= 0.04045f ? a / 12.92f : std::pow((a + 0.055f) / 1.055f, 2.4f);
    return std::copysign(r, v);
}

float linearToSrgbExact(float v)
{
    const float a = std::abs(v);
    const float r = a <= 0.0031308f ? a * 12.92f : 1.055f * std::pow(a, 1 / 2.4f) - 0.055f;
    return std::copysign(r, v);
}

constexpr int tableSize = 4096;

struct Tables {
    std::array<float, 256> decode8{};
    std::array<float, tableSize + 1> toLinear{};
    std::array<float, tableSize + 1> toSrgb{};
    std::vector<uint8_t> encode16 = std::vector<uint8_t>(65536);

    Tables()
    {
        for (int i = 0; i < 256; ++i)
            decode8[size_t(i)] = srgbToLinearExact(float(i) / 255);
        for (int i = 0; i <= tableSize; ++i) {
            toLinear[size_t(i)] = srgbToLinearExact(float(i) / tableSize);
            toSrgb[size_t(i)] = linearToSrgbExact(float(i) / tableSize);
        }
        for (int i = 0; i < 65536; ++i)
            encode16[size_t(i)] = uint8_t(std::lround(linearToSrgbExact(float(i) / 65535) * 255));
    }
};

const Tables &tables()
{
    static const Tables t;
    return t;
}

float lookup(const std::array<float, tableSize + 1> &table, float v)
{
    const float x = v * tableSize;
    const int i = std::min(int(x), tableSize - 1);
    const float f = x - float(i);
    return table[size_t(i)] + (table[size_t(i) + 1] - table[size_t(i)]) * f;
}

inline uint8_t encode8(float linear)
{
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return tables().encode16[size_t(c * 65535 + 0.5f)];
}

} // namespace

float toLinear(float srgb)
{
    if (srgb >= 0 && srgb <= 1)
        return lookup(tables().toLinear, srgb);
    return srgbToLinearExact(srgb);
}

float toSrgb(float linear)
{
    if (linear >= 0 && linear <= 1)
        return lookup(tables().toSrgb, linear);
    return linearToSrgbExact(linear);
}

void parallelFor(int count, const std::function<void(int, int)> &fn)
{
    if (count <= 0)
        return;
    const int threads = std::max(1, QThreadPool::globalInstance()->maxThreadCount());
    const int chunks = std::min(count, threads * 4);
    if (chunks <= 1 || count < 64) {
        fn(0, count);
        return;
    }
    std::vector<std::pair<int, int>> ranges;
    for (int c = 0; c < chunks; ++c)
        ranges.emplace_back(int(qint64(count) * c / chunks), int(qint64(count) * (c + 1) / chunks));
    QtConcurrent::blockingMap(ranges, [&](const std::pair<int, int> &r) { fn(r.first, r.second); });
}

LinearImage::LinearImage(int width, int height, Pixel fill)
    : m_width(std::max(0, width)), m_height(std::max(0, height)),
      m_pixels(size_t(m_width) * size_t(m_height), fill)
{
}

LinearImage LinearImage::fromImage(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    LinearImage out(image.width(), image.height());
    const auto &decode = tables().decode8;
    parallelFor(image.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const auto *src = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            Pixel *dst = out.row(y);
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = src[x];
                const int a = qAlpha(p);
                if (a == 255) {
                    dst[x] = {decode[size_t(qRed(p))], decode[size_t(qGreen(p))], decode[size_t(qBlue(p))], 1};
                } else if (a == 0) {
                    dst[x] = {};
                } else {
                    // Desfaz a pré-multiplicação em sRGB, lineariza e pré-multiplica de novo.
                    const float fa = float(a) / 255;
                    const auto lin = [&](int c) { return srgbToLinearExact(std::min(1.0f, float(c) / float(a))) * fa; };
                    dst[x] = {lin(qRed(p)), lin(qGreen(p)), lin(qBlue(p)), fa};
                }
            }
        }
    });
    return out;
}

QImage LinearImage::toImage() const
{
    QImage image(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    parallelFor(m_height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
            const Pixel *src = row(y);
            for (int x = 0; x < m_width; ++x) {
                const Pixel &p = src[x];
                const float a = std::clamp(p.a, 0.0f, 1.0f);
                if (a >= 0.9999f) {
                    dst[x] = qRgba(encode8(p.r), encode8(p.g), encode8(p.b), 255);
                } else if (a <= 0.0001f) {
                    dst[x] = 0;
                } else {
                    const int a8 = int(std::lround(a * 255));
                    const auto enc = [&](float c) { return std::min(a8, int(std::lround(float(encode8(c / a)) * a))); };
                    dst[x] = qRgba(enc(p.r), enc(p.g), enc(p.b), a8);
                }
            }
        }
    });
    return image;
}

LinearImage LinearImage::fromImageEncoded(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    LinearImage out(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        Pixel *dst = out.row(y);
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = src[x];
            dst[x] = {qRed(p) / 255.0f, qGreen(p) / 255.0f, qBlue(p) / 255.0f, qAlpha(p) / 255.0f};
        }
    }
    return out;
}

void LinearImage::encodedToLinear()
{
    parallelFor(m_height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            Pixel *p = row(y);
            for (int x = 0; x < m_width; ++x) {
                const float a = p[x].a;
                if (a <= 0) {
                    p[x] = {};
                    continue;
                }
                const float inv = 1 / a;
                p[x] = {toLinear(p[x].r * inv) * a, toLinear(p[x].g * inv) * a, toLinear(p[x].b * inv) * a, a};
            }
        }
    });
}

void LinearImage::fill(Pixel p) { std::fill(m_pixels.begin(), m_pixels.end(), p); }

Pixel LinearImage::sample(float x, float y, bool clamp) const
{
    // Centros dos pixels em +0,5.
    const float fx = x - 0.5f, fy = y - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = fx - float(x0), ty = fy - float(y0);
    const auto get = [&](int px, int py) -> Pixel {
        if (clamp) {
            px = std::clamp(px, 0, m_width - 1);
            py = std::clamp(py, 0, m_height - 1);
        } else if (px < 0 || py < 0 || px >= m_width || py >= m_height) {
            return {};
        }
        return at(px, py);
    };
    const Pixel a = get(x0, y0), b = get(x0 + 1, y0), c = get(x0, y0 + 1), d = get(x0 + 1, y0 + 1);
    const float wa = (1 - tx) * (1 - ty), wb = tx * (1 - ty), wc = (1 - tx) * ty, wd = tx * ty;
    return {a.r * wa + b.r * wb + c.r * wc + d.r * wd, a.g * wa + b.g * wb + c.g * wc + d.g * wd,
            a.b * wa + b.b * wb + c.b * wc + d.b * wd, a.a * wa + b.a * wb + c.a * wc + d.a * wd};
}

void LinearImage::draw(const LinearImage &src, const QTransform &transform, float opacity, const QRect &clip)
{
    if (src.isNull() || isNull() || opacity <= 0)
        return;
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return;
    const QRect bounds = transform.mapRect(QRectF(0, 0, src.width(), src.height()))
                             .toAlignedRect()
                             .adjusted(-1, -1, 1, 1)
                             .intersected(clip.isEmpty() ? QRect(0, 0, m_width, m_height) : clip)
                             .intersected(QRect(0, 0, m_width, m_height));
    if (bounds.isEmpty())
        return;
    // Bordas suaves: a amostra fora da origem é transparente (bilinear até meio pixel).
    parallelFor(bounds.height(), [&](int r0, int r1) {
        for (int y = bounds.top() + r0; y < bounds.top() + r1; ++y) {
            Pixel *dst = row(y);
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                const QPointF p = inverse.map(QPointF(x + 0.5, y + 0.5));
                if (p.x() < -0.5 || p.y() < -0.5 || p.x() > src.width() + 0.5 || p.y() > src.height() + 0.5)
                    continue;
                Pixel s = src.sample(float(p.x()), float(p.y()), false);
                if (s.a <= 0)
                    continue;
                s.r *= opacity, s.g *= opacity, s.b *= opacity, s.a *= opacity;
                Pixel &d = dst[x];
                const float k = 1 - s.a;
                d = {s.r + d.r * k, s.g + d.g * k, s.b + d.b * k, s.a + d.a * k};
            }
        }
    });
}

void LinearImage::drawAt(const LinearImage &src, int ox, int oy, float opacity, const QRect &clip)
{
    QRect bounds = QRect(ox, oy, src.width(), src.height()).intersected(QRect(0, 0, m_width, m_height));
    if (!clip.isEmpty())
        bounds = bounds.intersected(clip);
    if (bounds.isEmpty() || opacity <= 0)
        return;
    parallelFor(bounds.height(), [&](int r0, int r1) {
        for (int y = bounds.top() + r0; y < bounds.top() + r1; ++y) {
            Pixel *dst = row(y);
            const Pixel *s = src.row(y - oy);
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                Pixel p = s[x - ox];
                p.r *= opacity, p.g *= opacity, p.b *= opacity, p.a *= opacity;
                const float k = 1 - p.a;
                Pixel &d = dst[x];
                d = {p.r + d.r * k, p.g + d.g * k, p.b + d.b * k, p.a + d.a * k};
            }
        }
    });
}

namespace {

/// Uma passada de média móvel de raio `r` numa linha, com bordas repetidas.
void boxLine(const Pixel *in, Pixel *out, int n, int stride, int r)
{
    if (n <= 0)
        return;
    const float norm = 1.0f / float(2 * r + 1);
    Pixel acc{};
    const auto get = [&](int i) -> const Pixel & { return in[size_t(std::clamp(i, 0, n - 1)) * size_t(stride)]; };
    for (int i = -r; i <= r; ++i) {
        const Pixel &p = get(i);
        acc.r += p.r, acc.g += p.g, acc.b += p.b, acc.a += p.a;
    }
    for (int i = 0; i < n; ++i) {
        out[size_t(i) * size_t(stride)] = {acc.r * norm, acc.g * norm, acc.b * norm, acc.a * norm};
        const Pixel &add = get(i + r + 1), &sub = get(i - r);
        acc.r += add.r - sub.r, acc.g += add.g - sub.g, acc.b += add.b - sub.b, acc.a += add.a - sub.a;
    }
}

/// Raios de três caixas que aproximam a gaussiana de desvio `sigma`.
std::array<int, 3> boxRadii(double sigma)
{
    const double ideal = std::sqrt(12 * sigma * sigma / 3 + 1);
    int lower = int(std::floor(ideal));
    if (lower % 2 == 0)
        --lower;
    const int upper = lower + 2;
    const double m = (12 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9) / (-4.0 * lower - 4);
    const int count = int(std::lround(m));
    std::array<int, 3> radii{};
    for (int i = 0; i < 3; ++i)
        radii[size_t(i)] = ((i < count ? lower : upper) - 1) / 2;
    return radii;
}

} // namespace

void LinearImage::blur(double sigma)
{
    if (isNull() || sigma < 0.3)
        return;
    const auto radii = boxRadii(sigma);
    std::vector<Pixel> temp(m_pixels.size());
    for (const int r : radii) {
        if (r <= 0)
            continue;
        parallelFor(m_height, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                boxLine(row(y), temp.data() + size_t(y) * size_t(m_width), m_width, 1, r);
        });
        parallelFor(m_width, [&](int x0, int x1) {
            for (int x = x0; x < x1; ++x)
                boxLine(temp.data() + x, m_pixels.data() + x, m_height, m_width, r);
        });
    }
}

LinearImage LinearImage::scaled(QSize size) const
{
    const int w = std::max(1, size.width()), h = std::max(1, size.height());
    if (w == m_width && h == m_height)
        return *this;
    LinearImage out(w, h);
    const double sx = double(m_width) / w, sy = double(m_height) / h;
    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                if (sx <= 1 && sy <= 1) {
                    out.at(x, y) = sample(float((x + 0.5) * sx), float((y + 0.5) * sy), true);
                    continue;
                }
                // Média por área (redução).
                const int ax = int(x * sx), bx = std::max(ax + 1, int(std::ceil((x + 1) * sx)));
                const int ay = int(y * sy), by = std::max(ay + 1, int(std::ceil((y + 1) * sy)));
                Pixel acc{};
                int n = 0;
                for (int yy = ay; yy < std::min(by, m_height); ++yy)
                    for (int xx = ax; xx < std::min(bx, m_width); ++xx) {
                        const Pixel &p = at(xx, yy);
                        acc.r += p.r, acc.g += p.g, acc.b += p.b, acc.a += p.a;
                        ++n;
                    }
                const float inv = n ? 1.0f / float(n) : 0;
                out.at(x, y) = {acc.r * inv, acc.g * inv, acc.b * inv, acc.a * inv};
            }
        }
    });
    return out;
}

} // namespace of
