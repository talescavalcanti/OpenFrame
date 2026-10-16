#pragma once

#include "LinearImage.h"
#include "Project.h"

#include <optional>

namespace of {

/// Unidade das animações de texto por partes.
enum class TextGranularity { Letter, Word, Line };

/// Animação por unidade (letra, palavra ou linha) em andamento.
struct UnitAnimation {
    AnimationKind kind = AnimationKind::Cascade;
    /// Progresso linear no tempo: 0 = início da entrada (ou fim da saída), 1 = repouso.
    double progress = 1;
    bool exiting = false;
    bool operator==(const UnitAnimation &) const = default;
};

/// Estado de uma animação de entrada/saída (e do loop) num instante.
struct AnimationState {
    double scale = 1;
    double dx = 0;
    /// Normalizado, y para cima.
    double dy = 0;
    double rotation = 0;
    double alpha = 1;
    /// Fração do texto visível (animação "máquina de escrever").
    double reveal = 1;
    /// Desfoque da camada inteira, como fração da altura do canvas (animação "foco").
    double blur = 0;
    /// Animação por unidade ativa no instante (só texto).
    std::optional<UnitAnimation> unit;
    /// Opacidade do fundo do texto durante uma animação por unidade.
    double backgroundAlpha = 1;
    std::optional<LoopAnimation> loop;
    /// Tempo desde o início do clipe.
    Micros local = 0;

    /// Brilho do loop no instante (0 quando não há).
    double glow() const;
    struct Shine {
        /// 0...1 atravessando a caixa.
        double position;
        double strength;
    };
    /// Faixa de luz do reflexo, ou vazio fora da passagem.
    std::optional<Shine> shine() const;
};

namespace Animations {
AnimationState state(const Segment &seg, Micros local);
/// Desaceleração cúbica (1 - (1 - p)³), com p limitado a 0…1.
double ease(double p);
double clamp(double x);
// Curvas (0 → 1, sem pulo no fim).
double expoOut(double x);
double quartOut(double x);
double quintOut(double x);
/// Mola amortecida, com ~18% de exagero; vale 0 em 0 e 1 em 1.
double spring(double x);
/// Fase do loop no instante, em ciclos.
double loopPhase(const LoopAnimation &loop, Micros local);
/// Maior escala que as animações do segmento atingem (entrada, saída, loop e destaque da palavra), para o texto
/// ser rasterizado grande o bastante.
double peakScale(const Segment &seg);
/// Granularidade das animações desenhadas por unidade (vazio nas outras).
std::optional<TextGranularity> granularity(AnimationKind kind);
bool isPerUnit(AnimationKind kind);
/// O texto usa o caminho por unidade no clipe inteiro (destaque da palavra, entrada/saída por unidade, brilho
/// ou reflexo), para não haver pulo entre os dois desenhos.
bool usesUnits(const Segment &seg);
/// Granularidade no instante: a da animação ativa; em repouso, a da entrada (ou da saída). Com o destaque,
/// no máximo palavra (na digitação, letra).
TextGranularity granularityFor(const Segment &seg, const AnimationState &state);
} // namespace Animations

namespace Transitions {
/// Mistura duas camadas já posicionadas no canvas (do tamanho do canvas).
/// `k` = altura do canvas / 1080.
LinearImage apply(TransitionKind kind, const LinearImage &from, const LinearImage &to, double progress, double k);
} // namespace Transitions

} // namespace of
