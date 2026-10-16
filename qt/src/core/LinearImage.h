#pragma once

#include <QImage>
#include <QTransform>
#include <functional>
#include <vector>

namespace of {

/// Cor RGBA em ponto flutuante, em luz linear (primárias sRGB), pré-multiplicada.
struct Pixel {
    float r = 0, g = 0, b = 0, a = 0;
};

/// sRGB ↔ linear (curva sRGB). Valores fora de 0…1 seguem a curva espelhada,
/// como o espaço estendido do Core Image.
float toLinear(float srgb);
float toSrgb(float linear);

/// Executa `fn(begin, end)` em fatias de [0, count) no pool global do Qt.
void parallelFor(int count, const std::function<void(int, int)> &fn);

/// Imagem de trabalho do render: o Core Image (versão Swift) filtra e compõe
/// em luz linear, então fazemos o mesmo para ter os mesmos números.
class LinearImage
{
public:
    LinearImage() = default;
    LinearImage(int width, int height, Pixel fill = {});

    /// De uma imagem sRGB 8 bits (qualquer formato; vira pré-multiplicada).
    static LinearImage fromImage(const QImage &image);
    /// Para sRGB 8 bits, `Format_ARGB32_Premultiplied`.
    QImage toImage() const;
    /// De uma imagem sRGB 8 bits sem converter para luz linear: os valores sRGB ficam como estão (pré-multiplicados),
    /// como os sprites sem gerência de cor do texto no Swift. Volta para linear com `encodedToLinear()`.
    static LinearImage fromImageEncoded(const QImage &image);
    /// Converte valores sRGB pré-multiplicados (de `fromImageEncoded`) para luz linear pré-multiplicada.
    void encodedToLinear();

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool isNull() const { return m_width <= 0 || m_height <= 0; }
    QSize size() const { return {m_width, m_height}; }

    Pixel *row(int y) { return m_pixels.data() + size_t(y) * size_t(m_width); }
    const Pixel *row(int y) const { return m_pixels.data() + size_t(y) * size_t(m_width); }
    Pixel &at(int x, int y) { return m_pixels[size_t(y) * size_t(m_width) + size_t(x)]; }
    const Pixel &at(int x, int y) const { return m_pixels[size_t(y) * size_t(m_width) + size_t(x)]; }

    void fill(Pixel p);
    /// Cor no ponto (x, y) em pixels (centro do pixel em +0,5), bilinear.
    /// `clamp`: fora da imagem repete a borda; senão é transparente.
    Pixel sample(float x, float y, bool clamp) const;

    /// Desenha `src` por cima (source-over), com `transform` levando as
    /// coordenadas de `src` para as desta imagem, bilinear e com `opacity`.
    /// `clip` (se não vazio) limita o desenho a esse retângulo destas coordenadas.
    void draw(const LinearImage &src, const QTransform &transform, float opacity = 1, const QRect &clip = {});
    /// Desenho sem transformação na posição (x, y).
    void drawAt(const LinearImage &src, int x, int y, float opacity = 1, const QRect &clip = {});

    /// Desfoque gaussiano (três passadas de caixa), bordas repetidas como o
    /// `clampedToExtent` do Core Image.
    void blur(double sigma);
    /// Cópia reduzida (média por área) para caber em `size`.
    LinearImage scaled(QSize size) const;
    LinearImage copy() const { return *this; }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<Pixel> m_pixels;
};

} // namespace of
