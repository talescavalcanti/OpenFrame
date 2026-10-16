#pragma once

#include "Project.h"

#include <functional>
#include <optional>
#include <vector>

namespace of {

/// Áudio do projeto como a exportação ouve (o mesmo `AudioMixer`: volume, mudo, velocidade com o tom
/// preservado, fades e faixas de áudio), em mono float a 16 kHz para o Whisper. Como o `AudioMixdown.swift`.
namespace AudioMixdown {
inline constexpr int sampleRate = 16000;

/// Amostras do trecho `range` da timeline; a amostra 0 é `range.start`. Trechos sem áudio ficam em silêncio.
/// Vazio quando não há nenhum trecho com som no projeto. `progress` (0…1) devolve falso para interromper (a
/// função devolve vazio).
std::optional<std::vector<float>> samples(const Project &project, TimeRange range,
                                          const std::function<bool(double)> &progress = {});

/// 48 kHz → 16 kHz (passa-baixa com janela de Blackman em 7,2 kHz e dizimação por 3). `count` amostras de saída.
std::vector<float> downsample48to16(const std::vector<float> &mono48, size_t count);
} // namespace AudioMixdown

} // namespace of
