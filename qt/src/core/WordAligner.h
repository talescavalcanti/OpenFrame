#pragma once

#include <QList>
#include <QString>
#include <vector>

namespace of {

/// Palavra do whisper, com tempos em segundos desde a amostra 0 do áudio entregue.
struct EngineWord {
    QString text;
    double start = 0;
    double end = 0;
    double probability = 0;
    bool operator==(const EngineWord &) const = default;
};

/// Ajusta os tempos das palavras pela energia do áudio. Os tempos do Whisper (DTW) acertam a ordem e a região,
/// mas chegam atrasados (~0,3 s no `say`) e não enxergam as pausas. Cada fronteira entre palavras escolhe um
/// candidato perto da estimativa: uma pausa (a anterior termina onde ela começa e a seguinte começa onde ela
/// acaba) ou um vale de energia. A escolha é feita em conjunto (programação dinâmica), pesando a distância à
/// estimativa e a duração de cada palavra contra o número de letras dela. Porte do `WordAligner.swift`.
namespace WordAligner {
inline constexpr double hop = 0.01;
inline constexpr double window = 0.02;
/// Atraso típico do DTW e a dispersão aceita em torno dele.
inline constexpr double bias = 0.3;
inline constexpr double spread = 0.4;
/// Janela de busca de cada fronteira em torno da estimativa.
inline constexpr double before = 0.9;
inline constexpr double after = 0.3;
inline constexpr double minWord = 0.05;

/// Energia (dB) por quadro de 10 ms.
std::vector<double> energy(const std::vector<float> &samples, double sampleRate);
double percentile(std::vector<double> xs, double p);
QList<EngineWord> refine(const QList<EngineWord> &words, const std::vector<float> &samples, double sampleRate);
} // namespace WordAligner

} // namespace of
