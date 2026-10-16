#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace of {

/// Estado do painel "Gravar áudio" (o `AudioRecording` do Mac): o microfone
/// escolhido, o nível ao vivo e a fase da gravação. Quem grava é o `EditorModel`
/// (EditorRecording.cpp), que escreve aqui e avisa; o QML só lê.
class RecordingSession : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(bool showPanel READ showPanel NOTIFY changed)
    /// ready (o microfone só dá o nível), countdown, recording, finishing ou failed.
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    /// Contagem, gravação ou fechamento: o painel não fecha e as teclas são do gravador.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int countdown READ countdown NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// Microfones: [{id, name}].
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    /// Microfone escolhido (vazio = o padrão do sistema).
    Q_PROPERTY(QString input READ input NOTIFY changed)
    Q_PROPERTY(QString inputName READ inputName NOTIFY changed)
    /// Pergunta antes de jogar fora uma gravação de mais de 3 s.
    Q_PROPERTY(bool confirmDiscard READ confirmDiscard NOTIFY changed)
    /// Onde a gravação começa na timeline, em segundos.
    Q_PROPERTY(qreal start READ start NOTIFY changed)
    /// Nível do microfone (0…1), com uma queda suave para o medidor.
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(qreal duration READ duration NOTIFY levelChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY levelChanged)
    /// Linha (na ordem de exibição) onde a gravação vai cair; o número de faixas = uma faixa nova embaixo.
    Q_PROPERTY(int row READ row NOTIFY levelChanged)

public:
    /// Id dos picos ao vivo no `MediaCache` (o `WaveformView` desenha com eles).
    static inline const QString peaksId = QStringLiteral("gravacao-ao-vivo");

    explicit RecordingSession(QObject *parent = nullptr) : QObject(parent) {}

    bool showPanel() const { return m_showPanel; }
    QString phase() const { return m_phase; }
    bool active() const { return m_phase == "countdown" || m_phase == "recording" || m_phase == "finishing"; }
    int countdown() const { return m_countdown; }
    QString error() const { return m_error; }
    QVariantList inputs() const { return m_inputs; }
    QString input() const { return m_input; }
    QString inputName() const;
    bool confirmDiscard() const { return m_confirmDiscard; }
    qreal start() const { return m_start; }
    qreal level() const { return m_level; }
    qreal duration() const { return m_duration; }
    QString durationText() const;
    int row() const { return m_row; }

    // Escritos pelo EditorModel, que chama `changed`/`levelChanged` depois.
    bool m_showPanel = false;
    QString m_phase = QStringLiteral("ready");
    int m_countdown = 0;
    QString m_error;
    QVariantList m_inputs;
    QString m_input;
    QString m_defaultInput;
    bool m_confirmDiscard = false;
    qreal m_start = 0;
    qreal m_level = 0;
    qreal m_duration = 0;
    int m_row = 0;

signals:
    void changed();
    void levelChanged();
};

} // namespace of
