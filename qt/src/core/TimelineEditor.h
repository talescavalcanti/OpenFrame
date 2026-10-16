#pragma once

#include "Project.h"

#include <QPointF>
#include <functional>

namespace of {

/// Operações de edição da timeline. Todas são funções puras sobre `Project`,
/// o que permite testá-las sem interface e implementar desfazer por cópia.
namespace TimelineEditor {
inline constexpr Micros minDuration = 100'000;
inline constexpr Micros defaultPhotoDuration = 3 * Time::second;
inline constexpr Micros defaultTextDuration = 3 * Time::second;

// Layout

/// Reposiciona a faixa principal: clipes encostados, com sobreposição
/// apenas onde há transição.
void relayoutMain(Project &p);
void removeEmptyTracks(Project &p);

// Adição

TimeRange sourceRange(const Material &m);

/// Adiciona o material: visual vai para o fim da faixa principal; áudio
/// vai para uma faixa de áudio livre na posição `time`.
QUuid addMedia(const Material &m, Project &p, Micros time = 0);

/// Adiciona como sobreposição (picture-in-picture) a partir de `time`.
QUuid addOverlay(const Material &m, Project &p, Micros time);

/// Insere o material solto na timeline em `time`, sobre a faixa `track`
/// (índice em `p.tracks`; vazio fora das faixas). Visual sobre uma sobreposição
/// ou um texto vira sobreposição; nos demais casos entra na faixa principal,
/// na junção mais próxima de `time`. Áudio fica na faixa indicada se ela for
/// de áudio e estiver livre; senão, numa faixa de áudio livre.
QUuid insertMedia(const Material &m, Project &p, Micros time, std::optional<int> track);

QUuid addText(const TextContent &content, Project &p, Micros time, Micros duration = defaultTextDuration);

// Áudio

/// Se o segmento é um vídeo com som que ainda não foi extraído.
bool canExtractAudio(const Project &p, const QUuid &id);

/// Extrai o som do clipe para uma faixa de áudio, sem gravar nada em disco: um material de áudio que aponta
/// para o mesmo arquivo ("Áudio de <nome>", reaproveitado se outro trecho do mesmo vídeo já foi extraído) e um
/// segmento no mesmo trecho, com a velocidade, o volume e os fades do clipe, na primeira faixa de áudio livre no
/// trecho (ou numa nova). O cruzamento de uma transição da faixa principal vira fade no segmento novo, para o som
/// não mudar. O clipe fica com `audioDetached` (não soa mais). Devolve o id do segmento de áudio novo.
std::optional<QUuid> extractAudio(Project &p, const QUuid &id);

/// Põe uma gravação (o material entra no projeto se ainda não estiver) na timeline a partir de `time`, numa
/// faixa de áudio livre no trecho (ou numa nova). Devolve o id do segmento.
QUuid addRecording(const Material &m, Project &p, Micros time);

// Faixas

/// Muda a altura de uma faixa (limitada a `Track::heightMin...heightMax`; vazio volta à padrão).
void setTrackHeight(Project &p, const QUuid &track, std::optional<double> height);
/// Muda a altura de todas as faixas de uma vez (o controle global da barra da timeline).
void setAllTrackHeights(Project &p, std::optional<double> height);

/// Primeira faixa não principal do tipo sem nada no intervalo (ignorando `excluding`).
std::optional<int> freeTrack(TrackKind kind, TimeRange range, const Project &p, std::optional<QUuid> excluding);

// Edição

/// Divide o segmento em `time`. Retorna o id da parte direita.
std::optional<QUuid> split(Project &p, const QUuid &id, Micros time);
void remove(Project &p, const QUuid &id);
std::optional<QUuid> duplicate(Project &p, const QUuid &id);

/// Move horizontalmente um segmento de faixa não principal (sem resolver
/// sobreposições: chame `resolveOverlap` ao soltar).
void move(Project &p, const QUuid &id, Micros start);

/// Reordena um segmento da faixa principal pela posição do centro. A regra
/// é a mesma nos dois sentidos: o clipe passa um vizinho quando o centro
/// dele passa do centro do vizinho; no empate, nada troca.
void reorderMain(Project &p, const QUuid &id, Micros center);

/// Se o segmento ficou sobreposto a outro na mesma faixa, manda-o para
/// uma faixa livre do mesmo tipo (criando uma se preciso).
void resolveOverlap(Project &p, const QUuid &id);

/// Se a faixa `target` (diferente da atual) aceita o tipo de mídia do segmento.
bool canMove(const Project &p, const QUuid &id, int target);
/// Move o segmento para outra faixa do mesmo tipo de mídia.
void moveToTrack(Project &p, const QUuid &id, int target);

/// Apara o início do segmento até `newStart` (tempo da timeline).
void trimStart(Project &p, const QUuid &id, Micros newStart);
/// Apara o fim do segmento até `newEnd` (tempo da timeline).
void trimEnd(Project &p, const QUuid &id, Micros newEnd);

void setSpeed(Project &p, const QUuid &id, double speed);

/// Altera um segmento e reaplica o layout da faixa principal.
void update(Project &p, const QUuid &id, const std::function<void(Segment &)> &change);

/// Grava posição/escala/rotação no lugar certo: `text.style` no texto, `transform` no visual.
/// `center` normalizado (-1...1, y para cima); escala mínima 0,01.
void setPlacement(Project &p, const QUuid &id, QPointF center, double scale, double rotation);

/// Muda a largura da caixa de um texto (`style.maxWidth`, limitada a `TextStyle::maxWidthMin...Max`); o texto
/// se reorganiza. Numa faixa de legendas automáticas, vale para todos os textos da faixa. Ignora NaN/infinito.
void setTextWidth(Project &p, const QUuid &id, double maxWidth);

// Ímã (snap)

QList<Micros> snapPoints(const Project &p, std::optional<QUuid> excluding, const QList<Micros> &extra = {});
Micros snap(Micros t, const QList<Micros> &points, Micros threshold);

/// Ímã ao mover um clipe inteiro: a borda (início ou fim) mais perto de um
/// ponto vence; no empate, a que vai à frente no sentido do arraste. Assim
/// arrastar para a direita e para a esquerda se comportam como espelhos.
Micros snapMove(Micros start, Micros duration, const QList<Micros> &points, Micros threshold, bool forward);
} // namespace TimelineEditor

} // namespace of
