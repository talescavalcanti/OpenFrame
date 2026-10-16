#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <deque>
#include <functional>

namespace of {

/// Quadros das miniaturas animadas, gerados em segundo plano um de cada vez e
/// guardados. Pedidos urgentes (hover, selecionado) passam à frente da fila
/// (o `FrameStripStore` do Mac).
class AnimationStrips : public QObject
{
    Q_OBJECT

public:
    static AnimationStrips *instance();

    using Maker = std::function<QList<QImage>()>;
    /// Quadros prontos, ou nulo.
    const QList<QImage> *strip(const QString &key) const;
    void request(const QString &key, bool urgent, Maker make);

signals:
    void stripReady(const QString &key);

private:
    using QObject::QObject;
    void work();

    QHash<QString, QList<QImage>> m_strips;
    std::deque<std::pair<QString, Maker>> m_queue;
    bool m_working = false;
};

/// Miniatura de uma animação do segmento: o repouso quando parada e o ciclo
/// quando toca. Enquanto os quadros são gerados (`hasFrames` falso), o QML mostra
/// o ícone, como no Mac.
class AnimatedThumb : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString segment MEMBER m_segment NOTIFY sourceChanged)
    /// "entry:<tipo>", "exit:<tipo>", "loop:<tipo>" ou "" (só o repouso).
    Q_PROPERTY(QString part MEMBER m_part NOTIFY sourceChanged)
    Q_PROPERTY(bool playing MEMBER m_playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasFrames READ hasFrames NOTIFY framesChanged)

public:
    explicit AnimatedThumb(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    bool hasFrames() const;

    /// Pixels das miniaturas (exibidas em 2x, cabem no cartão de ~90×50).
    static constexpr QSize pixelSize{180, 100};

signals:
    void sourceChanged();
    void playingChanged();
    void framesChanged();

private:
    void request(bool urgent);
    QString restKey() const;
    QString partKey() const;
    /// Segundos de um ciclo: entrada/saída 1,2 s; loop, dois períodos padrão.
    double cycle() const;

    QString m_segment;
    QString m_part;
    bool m_playing = false;
    QString m_subject;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

/// Prévia animada de um cartão da janela das legendas: o quadro parado no meio da
/// frase e o ciclo de 3,6 s quando toca (hover ou selecionado). Na proporção do
/// projeto; a cor do destaque só entra quando a predefinição a usa. Enquanto os
/// quadros são gerados (`hasFrames` falso), o QML mostra a amostra.
class CaptionPreviewStrip : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString preset MEMBER m_preset NOTIFY sourceChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY sourceChanged)
    Q_PROPERTY(bool usesAccent MEMBER m_usesAccent NOTIFY sourceChanged)
    Q_PROPERTY(bool playing MEMBER m_playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasFrames READ hasFrames NOTIFY framesChanged)

public:
    explicit CaptionPreviewStrip(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    bool hasFrames() const;

signals:
    void sourceChanged();
    void playingChanged();
    void framesChanged();

private:
    QString key() const;
    void request();

    QString m_preset;
    QColor m_accent;
    bool m_usesAccent = false;
    bool m_playing = false;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

} // namespace of
