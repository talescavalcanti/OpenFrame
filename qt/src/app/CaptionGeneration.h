#pragma once

#include "CaptionsCore.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace of {

/// Estado da janela "Legendas automáticas" (o `CaptionGeneration` do Mac): as
/// escolhas, os modelos instalados e a fase da geração. Fica no `EditorModel`
/// para as escolhas valerem entre uma abertura e outra e para as capturas
/// montarem cada estado.
class CaptionGeneration : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY optionsChanged)
    /// Código do idioma; vazio = detectar automaticamente.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY optionsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY optionsChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY optionsChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY optionsChanged)
    Q_PROPERTY(bool replacePrevious READ replacePrevious WRITE setReplacePrevious NOTIFY optionsChanged)
    Q_PROPERTY(bool usesAccent READ usesAccent NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QVariantList presetGroups READ presetGroups CONSTANT)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString modelError READ modelError NOTIFY modelsChanged)
    /// Nota do rodapé: o modelo que vai ser baixado, ou que tudo roda no computador.
    Q_PROPERTY(QString footerNote READ footerNote NOTIFY modelsChanged)
    Q_PROPERTY(bool modelPending READ modelPending NOTIFY modelsChanged)

    /// configuring, downloading, mixing, transcribing, finished, failed.
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool running READ running NOTIFY phaseChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY phaseChanged)
    Q_PROPERTY(bool includesDownload READ includesDownload NOTIFY phaseChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY phaseChanged)
    Q_PROPERTY(QString remaining READ remaining NOTIFY phaseChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY phaseChanged)
    Q_PROPERTY(QString error READ error NOTIFY phaseChanged)

public:
    explicit CaptionGeneration(QObject *parent = nullptr);

    CaptionsCore::Engine *engine() const { return m_engine; }

    QString source() const { return m_source; }
    void setSource(const QString &v);
    QString language() const { return m_language; }
    void setLanguage(const QString &v);
    QString model() const { return m_model; }
    void setModel(const QString &v);
    QString preset() const { return m_preset; }
    void setPreset(const QString &v);
    QColor highlightColor() const { return m_highlightColor; }
    void setHighlightColor(const QColor &v);
    bool replacePrevious() const { return m_replacePrevious; }
    void setReplacePrevious(bool v);
    bool usesAccent() const;
    QVariantList languages() const;
    QVariantList presetGroups() const;
    QVariantList models() const;
    QString modelError() const { return m_modelError; }
    QString footerNote() const;
    bool modelPending() const { return !m_engine->isInstalled(m_model); }

    QString phase() const { return m_phase; }
    bool running() const;
    qreal progress() const { return m_progress; }
    bool includesDownload() const { return m_includesDownload; }
    QString detail() const;
    QString remaining() const;
    int resultCount() const { return m_resultCount; }
    QString error() const { return m_error; }

    /// Muda a fase; o relógio da estimativa recomeça só quando a etapa muda.
    void setPhase(const QString &phase, qreal progress = 0);
    void setIncludesDownload(bool v);
    void setResult(int count, const QString &error);
    /// O idioma já foi preenchido com o do sistema (só na primeira abertura).
    bool languageChosen = false;

    /// Predefinições de um grupo: [{id, name, detail, layout, usesAccent}].
    Q_INVOKABLE QVariantList presetsIn(const QString &group) const;
    Q_INVOKABLE void downloadModel(const QString &id);
    Q_INVOKABLE void cancelDownload(const QString &id);
    Q_INVOKABLE void deleteModel(const QString &id);
    Q_INVOKABLE void backToOptions() { setPhase(QStringLiteral("configuring")); }

    /// Idioma do sistema, se estiver entre os oferecidos; senão, vazio (detectar).
    static QString systemLanguage();
    static QString megabytes(qint64 bytes);

signals:
    void optionsChanged();
    void modelsChanged();
    void phaseChanged();

private:
    CaptionsCore::Engine *m_engine;
    QString m_source = QStringLiteral("timeline");
    QString m_language;
    QString m_model = CaptionsCore::recommendedModel();
    QString m_preset = QStringLiteral("phrases");
    QColor m_highlightColor = CaptionsCore::defaultHighlight();
    bool m_replacePrevious = true;
    /// Downloads pedidos na lista de precisão, fora da geração: modelo → fração.
    QHash<QString, double> m_downloads;
    QString m_modelError;

    QString m_phase = QStringLiteral("configuring");
    qreal m_progress = 0;
    bool m_includesDownload = false;
    int m_resultCount = 0;
    QString m_error;
    QElapsedTimer m_phaseClock;
};

} // namespace of
