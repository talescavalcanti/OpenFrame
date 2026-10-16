#pragma once

#include "Effects.h"
#include "Enums.h"
#include "Timing.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QUuid>
#include <optional>
#include <variant>

// O modelo segue a mesma divisão do draft do CapCut: uma lista de materiais
// (arquivos importados) e uma lista de faixas, cada uma com segmentos que
// apontam para um material e guardam um intervalo de origem e uma posição na
// timeline.

namespace of {

enum class MaterialKind { Video, Photo, Audio };
OF_ENUM_KEYS(MaterialKind, "video", "photo", "audio")

struct Material {
    QUuid id = QUuid::createUuid();
    MaterialKind kind = MaterialKind::Video;
    QString path;
    QString name;
    /// Duração do arquivo (0 para fotos).
    Micros duration = 0;
    /// Dimensões de exibição, já considerando a rotação do vídeo.
    int width = 0;
    int height = 0;
    bool hasAudio = false;

    bool isVisual() const { return kind != MaterialKind::Audio; }
    bool operator==(const Material &) const = default;
};

enum class TrackKind { Video, Audio, Text };
OF_ENUM_KEYS(TrackKind, "video", "audio", "text")

struct Segment {
    QUuid id = QUuid::createUuid();
    std::optional<QUuid> materialId;
    /// Trecho usado do material (para texto, `start` = 0 e `duration` = duração na timeline).
    TimeRange source;
    Micros targetStart = 0;
    double speed = 1;
    double volume = 1;
    Micros fadeIn = 0;
    Micros fadeOut = 0;
    Transform transform;
    double opacity = 1;
    Adjust adjust;
    std::optional<FilterSetting> filter;
    QList<VideoEffect> effects;
    std::optional<SegmentAnimation> animationIn;
    std::optional<SegmentAnimation> animationOut;
    /// Sem loop, a chave não é gravada (projetos e fixtures antigos ficam iguais).
    std::optional<LoopAnimation> animationLoop;
    std::optional<Transition> transition;
    std::optional<TextContent> text;
    /// O som deste clipe foi extraído para uma faixa de áudio: o clipe continua com a imagem, mas não soa mais.
    /// Só é gravado quando ligado; ausente na leitura = desligado.
    bool audioDetached = false;

    Segment() = default;
    Segment(std::optional<QUuid> materialId, TimeRange source, Micros targetStart)
        : materialId(materialId), source(source), targetStart(targetStart)
    {
    }

    static Segment makeText(const TextContent &content, Micros start, Micros duration);

    Micros targetDuration() const { return Time::round(double(source.duration) / std::max(speed, 0.01)); }
    Micros targetEnd() const { return targetStart + targetDuration(); }
    TimeRange targetRange() const { return {targetStart, targetDuration()}; }

    bool operator==(const Segment &) const = default;
};

struct Track {
    QUuid id = QUuid::createUuid();
    TrackKind kind = TrackKind::Video;
    /// A faixa principal é magnética: os clipes ficam encostados, sem buracos.
    bool isMain = false;
    bool muted = false;
    bool hidden = false;
    /// Faixa das legendas automáticas ("substituir anteriores" e o painel Legendas).
    /// Só é gravada quando ligada; ausente na leitura = desligada.
    bool isAutoCaptions = false;
    /// Altura da faixa na timeline, em pontos (vazio = a padrão do app, `defaultHeight`). Preferência de
    /// visualização gravada no projeto, para a faixa voltar com a mesma altura. Só é gravada quando não é vazia;
    /// na leitura, limitada a `heightMin...heightMax`.
    std::optional<double> height;
    QList<Segment> segments;

    /// `Track.heightRange` do Swift.
    static constexpr double heightMin = 32;
    static constexpr double heightMax = 200;
    static constexpr double defaultHeight = 50;

    Track() = default;
    explicit Track(TrackKind kind, bool isMain = false) : kind(kind), isMain(isMain) {}

    /// Altura limitada a `heightMin...heightMax` (vazio e valores não finitos voltam à padrão).
    static std::optional<double> clampedHeight(std::optional<double> height);

    bool operator==(const Track &) const = default;
};

enum class AspectRatio { Original, R16x9, R9x16, R1x1, R4x3, R3x4, R21x9, R4x5 };
OF_ENUM_KEYS(AspectRatio, "original", "r16x9", "r9x16", "r1x1", "r4x3", "r3x4", "r21x9", "r4x5")
QString label(AspectRatio);
/// Largura/altura; vazio em "Original".
std::optional<double> ratio(AspectRatio);

/// Fundo do canvas. `Blur` é o fundo desfocado com o próprio vídeo
/// (intensidade 0...1), como o "canvas_blur" do CapCut.
struct CanvasBackground {
    struct Black {
        bool operator==(const Black &) const = default;
    };
    struct Color {
        RGBA color;
        bool operator==(const Color &) const = default;
    };
    struct Blur {
        double intensity = 0;
        bool operator==(const Blur &) const = default;
    };
    std::variant<Black, Color, Blur> value;

    static CanvasBackground black() { return {Black{}}; }
    static CanvasBackground color(RGBA c) { return {Color{c}}; }
    static CanvasBackground blur(double intensity) { return {Blur{intensity}}; }

    bool isBlack() const { return std::holds_alternative<Black>(value); }
    const RGBA *colorValue() const;
    std::optional<double> blurIntensity() const;

    bool operator==(const CanvasBackground &) const = default;
};

struct Canvas {
    AspectRatio aspect = AspectRatio::Original;
    CanvasBackground background = CanvasBackground::black();
    bool operator==(const Canvas &) const = default;
};

struct Project {
    int formatVersion = 1;
    QString name = QStringLiteral("Sem título");
    Canvas canvas;
    double fps = 30;
    QList<Material> materials;
    QList<Track> tracks;

    Project() { tracks.append(Track(TrackKind::Video, true)); }

    struct Location {
        int track = -1;
        int index = -1;
        bool operator==(const Location &) const = default;
    };

    Micros duration() const;
    const Material *material(const std::optional<QUuid> &id) const;
    std::optional<int> mainTrackIndex() const;
    std::optional<Location> locate(const QUuid &segmentId) const;
    std::optional<Segment> segment(const QUuid &id) const;

    /// Proporção largura/altura do canvas. Em "Original", usa o primeiro
    /// clipe visual da faixa principal, como o CapCut faz.
    double aspectValue() const;
    /// Tamanho do canvas com o lado menor igual a `shortSide`, em pixels pares.
    QSize canvasSize(int shortSide) const;

    bool operator==(const Project &) const = default;
};

} // namespace of
