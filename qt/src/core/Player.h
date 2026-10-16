#pragma once

#include "Project.h"

#include <QImage>
#include <QObject>
#include <memory>

namespace of {

/// Reprodução do preview, como o AVPlayer do editor Swift. Vive na thread
/// principal; o render roda numa thread de trabalho (um quadro por vez) e o
/// áudio numa thread própria.
///
/// - `setProject` a cada edição: se só a imagem mudou, redesenha; se o som
///   mudou (trechos, volume, fades, velocidade), refaz a mixagem.
/// - `seek` é "perseguido": com um quadro em andamento, só a busca mais recente
///   é desenhada em seguida.
/// - Tocando, os quadros saem no ritmo do relógio; se o render não acompanha,
///   os intermediários são pulados e a resolução cai (até 1/3), voltando quando
///   sobra tempo. Pausado, o quadro sai na resolução cheia.
/// - Sem dispositivo de áudio (ou com `setAudioEnabled(false)`), toca mudo.
class Player : public QObject
{
    Q_OBJECT

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    void setProject(const Project &project);
    const Project &project() const;

    /// Lado menor do quadro do preview (padrão 720).
    void setPreviewShortSide(int shortSide = 720);
    int previewShortSide() const;

    /// Liga ou desliga a saída de som (testes e CI tocam mudos).
    void setAudioEnabled(bool enabled);
    bool isAudioEnabled() const;

    void play();
    void pause();
    void seek(Micros t);

    Micros time() const;
    Micros duration() const;
    bool isPlaying() const;
    /// Há um quadro sendo desenhado ou esperando a vez (o `frameReady` dele ainda não saiu).
    bool isRendering() const;

signals:
    /// Quadro novo (sRGB, `Format_ARGB32_Premultiplied`).
    void frameReady(const QImage &frame);
    /// Posição durante a reprodução (~30 vezes por segundo), em cada `seek`
    /// e ao pausar.
    void timeChanged(of::Micros time);
    void playingChanged(bool playing);
    /// Chegou ao fim tocando (já pausado, com o tempo na duração).
    void finished();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace of
