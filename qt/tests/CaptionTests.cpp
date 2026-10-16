// Porte das suítes de legendas do Swift (CaptionTests.swift e CaptionStyleTests.swift, sem o render):
// modelo e JSON, quebra, largura da caixa e equilíbrio, predefinições, palavra-chave, Empilhado, e os
// cabeçalhos da transcrição (catálogo de modelos, palavras falsas).
#include "CaptionBuilder.h"
#include "ProjectStore.h"
#include "TestSupport.h"
#include "TextRenderer.h"
#include "Transcriber.h"

#include <QSignalSpy>
#include <QTemporaryDir>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;
namespace CB = of::CaptionBuilder;

namespace {

/// Palavras com tempos sintéticos: ~0,3 s cada, 0,05 s entre elas e 0,5 s depois de . ? !
/// `|` no texto força uma pausa de 0,6 s.
QList<TranscribedWord> spokenWords(const QString &text, Micros start = 0)
{
    Micros t = start;
    QList<TranscribedWord> words;
    for (const QString &token : text.split(u' ', Qt::SkipEmptyParts)) {
        if (token == u"|") {
            t += 600'000;
            continue;
        }
        const Micros end = t + 200'000 + Micros(token.size()) * 20'000;
        words.append({token, t, end, 0.9});
        t = end + (CB::endsSentence(token) ? 500'000 : 50'000);
    }
    return words;
}

QStringList texts(const QList<TranscribedWord> &words)
{
    QStringList out;
    for (const auto &w : words)
        out.append(w.text);
    return out;
}

QStringList cueTexts(const QList<CaptionCue> &cues)
{
    QStringList out;
    for (const auto &c : cues)
        out.append(c.text);
    return out;
}

const QString speech = QStringLiteral(
    "Olá, pessoal! Hoje eu vou mostrar para vocês como gerar legendas automáticas no editor, sem internet e sem "
    "complicação. | Primeiro escolha o idioma da fala; depois escolha a precisão do modelo e o estilo que combina "
    "com o seu vídeo. Pronto: as legendas aparecem na timeline, uma faixa só delas, e você pode editar cada uma "
    "quando quiser.");

const QString presetSpeech = QStringLiteral(
    "Hoje eu vou mostrar para vocês como gerar legendas automáticas no editor, sem internet e sem complicação. "
    "Primeiro escolha o idioma da fala; depois escolha a precisão do modelo e o estilo que combina com o vídeo.");

const QSize canvas1080(1920, 1080);

const Track *trackWithId(const Project &p, const QUuid &id)
{
    for (const Track &t : p.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

QStringList renderedLines(const CaptionCue &cue, CaptionPreset preset, QSize canvas = canvas1080)
{
    const TextStyle style = CB::defaultStyle(preset, double(canvas.width()) / canvas.height());
    return TextRenderer::shared().lineTexts(TextContent(cue.text, style), canvas);
}

} // namespace

class CaptionTests : public QObject
{
    Q_OBJECT

    /// Cada legenda: todas as palavras na ordem, linhas do visual, duração e vãos.
    static void check(const QList<CaptionCue> &cues, CaptionPreset preset, const QList<TranscribedWord> &words,
                      QSize canvas = canvas1080)
    {
        const CB::Limits lim = CB::limits(preset);
        QStringList all;
        for (const auto &c : cues)
            all += texts(c.words);
        QCOMPARE(all, texts(words));
        for (int k = 0; k < cues.size(); ++k) {
            const CaptionCue &cue = cues[k];
            if (layout(preset) == CaptionLayout::Stacked) {
                const QStringList lines = cue.text.split(u'\n');
                QVERIFY2(cue.words.size() == 1 || (lines.size() >= 2 && lines.size() <= 3), qPrintable(cue.text));
                for (const QString &line : lines) {
                    const auto n = line.split(u' ').size();
                    QVERIFY2(n >= 1 && n <= 2, qPrintable(cue.text));
                }
            } else {
                // Quebra suave: sem `\n`; quem quebra é a caixa, dentro das linhas do visual.
                QVERIFY(!cue.text.contains(u'\n'));
                const QStringList lines = renderedLines(cue, preset, canvas);
                if (cue.words.size() > 1)
                    QVERIFY2(lines.size() <= lim.lines,
                             qPrintable(displayName(preset) + u' ' + lines.join(QStringLiteral(" | "))));
            }
            QCOMPARE(TextContent::wordsOf(cue.text).size(), cue.words.size());
            QVERIFY(cue.words.last().end - cue.words.first().start <= lim.maxDuration);
            QVERIFY(cue.end > cue.start);
            if (k + 1 < cues.size()) {
                const CaptionCue &next = cues[k + 1];
                QVERIFY2(cue.end <= next.start, qPrintable(QStringLiteral("sobreposição em %1").arg(k)));
                QVERIFY(cue.end - cue.start >= CB::minDuration || cue.end == next.start);
                QVERIFY(next.start - cue.end == 0 || next.start - cue.end >= CB::closeGap);
            }
        }
    }

    static void presetRows()
    {
        QTest::addColumn<CaptionPreset>("preset");
        for (const CaptionPreset p : allCases<CaptionPreset>())
            QTest::newRow(qPrintable(enumKey(p))) << p;
    }

private slots:
    // MARK: - Modelo e JSON

    // Sem palavras, as chaves novas não são gravadas e projetos antigos abrem
    void optionalKeys()
    {
        Project p;
        TE::addText(text(QStringLiteral("Oi")), p, 0);
        const QByteArray json = ProjectStore::encode(p);
        // ("highlights" é do Ajustar.)
        QVERIFY(!json.contains("\"words\"") && !json.contains("\"highlight\"") && !json.contains("isAutoCaptions") &&
                !json.contains("\"emphasis\"") && !json.contains("animationLoop"));
        QCOMPARE(*ProjectStore::decode(json), p);
    }

    // Visuais e palavras vão e voltam no JSON
    void roundTrip_data() { presetRows(); }
    void roundTrip()
    {
        QFETCH(CaptionPreset, preset);
        Project p;
        const auto cues = CB::cues(spokenWords(QStringLiteral("Hoje vamos construir legendas incríveis. Tudo bem?")), preset);
        const QUuid id = CB::apply(cues, preset, RGBA{0.2, 0.8, 1, 1}, p, false);
        QString error;
        const auto back = ProjectStore::decode(ProjectStore::encode(p), &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(*back, p);
        QVERIFY(trackWithId(*back, id)->isAutoCaptions);
    }

    // O destaque só vale quando a contagem de palavras bate com o texto
    void activeHighlight()
    {
        TextContent c(QStringLiteral("um dois\ntrês"));
        c.words = QList<CaptionWord>{{"um", 0, 1}, {"dois", 1, 2}, {"três", 2, 3}};
        QVERIFY(!c.activeHighlight()); // sem `highlight`
        c.highlight = WordHighlight();
        QCOMPARE(c.activeHighlight()->words.size(), 3);
        c.text = QStringLiteral("um dois três quatro");
        QVERIFY(!c.activeHighlight());
        // O espaço sem quebra também separa palavras.
        QCOMPARE(TextContent::wordsOf(QStringLiteral("de manhã  cedo\n")), (QStringList{"de", "manhã", "cedo"}));
    }

    // Texto novo não entra na faixa das legendas automáticas
    void autoTrackIsReserved()
    {
        Project p;
        const QUuid autoId = CB::apply(CB::cues(spokenWords(QStringLiteral("uma frase curta")), CaptionLayout::Phrases),
                                       CaptionLayout::Phrases, std::nullopt, p, false);
        const QUuid id = TE::addText(text(QStringLiteral("Título")), p, 10'000'000);
        QVERIFY(p.tracks[p.locate(id)->track].id != autoId);
    }

    // Aparar e dividir mantêm as palavras no mesmo instante da timeline
    void editsKeepWordTimes()
    {
        Project p;
        const QString spoken = QStringLiteral("primeira segunda terceira quarta");
        const auto words = spokenWords(spoken, 1'000'000);
        // Uma legenda só, montada aqui: pela segmentação, caber numa linha depende da fonte (no Mac a Helvetica
        // Neue usa 99% da linha; a Inter, fora dele, 101,5%, e viram duas).
        const QList<CaptionCue> cues{{spoken, words.first().start, words.last().end, words}};
        CB::apply(cues, CaptionLayout::Highlight, std::nullopt, p, false);
        const QUuid id = p.tracks.last().segments[0].id;
        const auto absolute = [](const Segment &seg) {
            QList<Micros> out;
            for (const auto &w : *seg.text->words)
                out.append(seg.targetStart + w.start);
            return out;
        };
        const QList<Micros> original = absolute(*p.segment(id));
        QList<Micros> starts;
        for (const auto &w : words)
            starts.append(w.start);
        QCOMPARE(original, starts);

        TE::trimStart(p, id, 1'200'000);
        QCOMPARE(absolute(*p.segment(id)), original);
        const auto right = TE::split(p, id, words[2].start);
        QVERIFY(right);
        QCOMPARE(absolute(*p.segment(*right)), original);
        QCOMPARE(absolute(*p.segment(id)), original);
    }

    // MARK: - Quebra

    // Cada visual respeita as linhas, a duração e a ordem das palavras
    void limitsRespected_data() { presetRows(); }
    void limitsRespected()
    {
        QFETCH(CaptionPreset, preset);
        const auto words = spokenWords(speech);
        const auto cues = CB::cues(words, preset);
        QVERIFY(cues.size() > 2);
        check(cues, preset, words);
    }

    // No 9:16 cabe menos numa linha: a segmentação acompanha
    void portrait_data()
    {
        QTest::addColumn<CaptionPreset>("preset");
        for (const CaptionPreset p : {CaptionPreset::Phrases, CaptionPreset::Highlight, CaptionPreset::Rise})
            QTest::newRow(qPrintable(enumKey(p))) << p;
    }
    void portrait()
    {
        QFETCH(CaptionPreset, preset);
        const auto words = spokenWords(speech);
        const auto tallCues = CB::cues(words, preset, 9.0 / 16);
        check(tallCues, preset, words, QSize(1080, 1920));
        QVERIFY(tallCues.size() >= CB::cues(words, preset).size());
        const CB::Measure tall({}, CB::defaultStyle(preset, 9.0 / 16), 9.0 / 16);
        const CB::Measure wide({}, CB::defaultStyle(preset), 16.0 / 9);
        QVERIFY(tall.line < wide.line);
        QCOMPARE(CB::defaultStyle(preset, 9.0 / 16).maxWidth, 0.9);
        const TextStyle base = CB::defaultStyle(preset);
        QVERIFY(std::abs(CB::defaultStyle(preset, 9.0 / 16).fontSize * 1920 - base.fontSize * 1080) < 1e-9);
        QVERIFY(std::abs(CB::defaultStyle(preset, 4.0 / 5).fontSize * 1350 - base.fontSize * 1080) < 1e-9);
        QCOMPARE(CB::defaultStyle(preset, 1).fontSize, base.fontSize);
        QCOMPARE(CB::defaultStyle(preset, 21.0 / 9).fontSize, base.fontSize);
    }

    // Quebra nas pausas e na pontuação
    void breaksAtPunctuation()
    {
        const auto cues = CB::cues(spokenWords(speech), CaptionLayout::Phrases);
        QStringList all;
        for (const auto &c : cues)
            all.append(QString(c.text).replace(CB::glue, u' '));
        QVERIFY(std::any_of(all.begin(), all.end(), [](const QString &t) { return t.endsWith(u"complicação."); }));
        QVERIFY(std::any_of(all.begin(), all.end(), [](const QString &t) { return t.startsWith(u"Primeiro"); }));
        int clean = 0;
        for (int k = 0; k + 1 < cues.size(); ++k) {
            const QString last = cues[k].words.last().text;
            clean += CB::endsSentence(last) || CB::endsClause(last) ? 1 : 0;
        }
        QVERIFY2(clean * 2 >= cues.size() - 1, qPrintable(all.join(QStringLiteral(" | "))));
    }

    // Nenhuma linha termina em artigo ou preposição quando há outra opção
    void noDanglingFunctionWords_data()
    {
        QTest::addColumn<double>("factor");
        QTest::newRow("1") << 1.0;
        QTest::newRow("0.6") << 0.6;
        QTest::newRow("0.4") << 0.4;
    }
    void noDanglingFunctionWords()
    {
        QFETCH(double, factor);
        const auto cues = CB::cues(spokenWords(speech), CaptionLayout::Phrases);
        TextStyle style = CB::defaultStyle(CaptionPreset::Phrases);
        style.maxWidth *= factor;
        for (const auto &cue : cues) {
            const QStringList lines = TextRenderer::shared().lineTexts(TextContent(cue.text, style), canvas1080);
            for (int k = 0; k + 1 < lines.size(); ++k)
                QVERIFY2(!CB::isFunctionWord(TextContent::wordsOf(lines[k]).last()),
                         qPrintable(lines.join(QStringLiteral(" | "))));
        }
        for (int k = 0; k + 1 < cues.size(); ++k)
            QVERIFY(!CB::isFunctionWord(cues[k].words.last().text));
    }

    // Frases no 16:9: duas linhas equilibradas; caixa larga junta numa linha só
    void balancedLines()
    {
        const auto words = spokenWords(QStringLiteral("este é um exemplo de legenda longa que precisa de duas linhas na tela"));
        const auto cues = CB::cues(words, CaptionLayout::Phrases);
        QCOMPARE(cues.size(), 1);
        QVERIFY(!cues[0].text.contains(u'\n'));
        Project p;
        p.canvas.aspect = AspectRatio::R16x9;
        const QUuid track = CB::apply(cues, CaptionPreset::Phrases, std::nullopt, p, false);
        const auto lines = [&] {
            return TextRenderer::shared().lineTexts(*trackWithId(p, track)->segments[0].text, canvas1080);
        };
        const QStringList two = lines();
        QCOMPARE(two.size(), 2);
        const TextStyle style = trackWithId(p, track)->segments[0].text->style;
        const double a = TextRenderer::shared().lineWidth(two[0], style, 100);
        const double b = TextRenderer::shared().lineWidth(two[1], style, 100);
        QVERIFY2(std::abs(a - b) <= 0.25 * std::max(a, b), qPrintable(two.join(QStringLiteral(" | "))));

        // Alargar a caixa junta as linhas; estreitar quebra mais.
        const QUuid seg = trackWithId(p, track)->segments[0].id;
        TE::setTextWidth(p, seg, 3);
        QCOMPARE(lines().size(), 1);
        TE::setTextWidth(p, seg, 0.3);
        QVERIFY(lines().size() > 2);
    }

    // Impacto: de 1 a 4 palavras, quase sempre de 2 a 4
    void punchWordCounts()
    {
        const auto cues = CB::cues(spokenWords(speech), CaptionLayout::Punch);
        int singles = 0;
        for (const auto &c : cues) {
            QVERIFY(c.words.size() >= 1 && c.words.size() <= 4);
            singles += c.words.size() == 1 ? 1 : 0;
        }
        QVERIFY2(singles * 5 <= cues.size(), qPrintable(cueTexts(cues).join(QStringLiteral(" | "))));
    }

    // Silêncio longo separa as legendas; legenda curta dura pelo menos 0,7 s sem invadir a próxima
    void timing()
    {
        auto words = spokenWords(QStringLiteral("Oi."));
        words += spokenWords(QStringLiteral("Tudo bem?"), 3'000'000);
        const auto cues = CB::cues(words, CaptionLayout::Phrases);
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues[0].end - cues[0].start, CB::minDuration);

        const QList<TranscribedWord> close{{"Sim.", 0, 200'000}, {"Não.", 2'000'000, 2'300'000},
                                           {"Talvez", 2'400'000, 2'600'000}};
        const auto c2 = CB::cues(close, CaptionLayout::Punch);
        QCOMPARE(c2.first().end, 700'000);
        for (int k = 0; k + 1 < c2.size(); ++k)
            QVERIFY(c2[k].end <= c2[k + 1].start);
    }

    // Pontuação solta vai para a palavra anterior; espaços somem
    void normalizes()
    {
        const QList<TranscribedWord> words{{" Olá", 0, 300'000},
                                           {",", 300'000, 310'000},
                                           {" mundo", 400'000, 700'000},
                                           {"!", 700'000, 720'000},
                                           {"  ", 800'000, 900'000}};
        const auto cues = CB::cues(words, CaptionLayout::Phrases);
        QCOMPARE(cueTexts(cues), QStringList{QStringLiteral("Olá, mundo!")});
        QCOMPARE(texts(cues[0].words), (QStringList{"Olá,", "mundo!"}));
        QCOMPARE(cues[0].words[1].end, 720'000);
        QVERIFY(CB::cues(QList<TranscribedWord>{}, CaptionLayout::Phrases).isEmpty());
    }

    // Aplicar cria a faixa automática; substituir troca as legendas sem mexer no resto
    void applyAndReplace()
    {
        Project p;
        const QUuid user = TE::addText(text(QStringLiteral("Meu título")), p, 0);
        const auto cues = CB::cues(spokenWords(speech), CaptionLayout::Punch);
        const QUuid first = CB::apply(cues, CaptionLayout::Punch, std::nullopt, p, false);
        const Track *track = trackWithId(p, first);
        QVERIFY(track && track->kind == TrackKind::Text && track->isAutoCaptions);
        QCOMPARE(track->segments.size(), cues.size());
        const Segment seg = track->segments[1];
        QCOMPARE(seg.targetStart, cues[1].start);
        QCOMPARE(seg.text->words->first().start, cues[1].words[0].start - cues[1].start);
        QVERIFY(!seg.text->highlight);
        QCOMPARE(seg.text->style, CB::defaultStyle(CaptionPreset::Punch));
        QCOMPARE(seg.animationIn->kind, AnimationKind::Elastic);

        // Substituir: mesma faixa, legendas novas; o texto do usuário continua.
        const auto again = CB::cues(spokenWords(QStringLiteral("outra fala")), CaptionLayout::Highlight);
        TextStyle style = CB::defaultStyle(CaptionPreset::Highlight);
        style.color = {0, 1, 0, 1};
        const QUuid second = CB::apply(again, CaptionLayout::Highlight, style, p, true, TimeRange{0, p.duration()});
        QCOMPARE(second, first);
        QCOMPARE(std::count_if(p.tracks.begin(), p.tracks.end(), [](const Track &t) { return t.isAutoCaptions; }), 1);
        const auto segTexts = [&] {
            QStringList out;
            for (const auto &s : trackWithId(p, second)->segments)
                out.append(s.text->text);
            return out;
        };
        QCOMPARE(segTexts(), cueTexts(again));
        QCOMPARE(*trackWithId(p, second)->segments[0].text->highlight, WordHighlight());
        QCOMPARE(trackWithId(p, second)->segments[0].text->style.color, (RGBA{0, 1, 0, 1}));
        QVERIFY(p.segment(user));

        // Substituir só o trecho transcrito: as legendas de fora ficam.
        const auto late = CB::cues(spokenWords(QStringLiteral("mais tarde"), 60'000'000), CaptionLayout::Highlight);
        CB::apply(late, CaptionLayout::Highlight, std::nullopt, p, true, TimeRange{59'000'000, 5'000'000});
        QCOMPARE(segTexts(), cueTexts(again) + cueTexts(late));
        const auto lateAgain = CB::cues(spokenWords(QStringLiteral("de novo"), 60'000'000), CaptionLayout::Highlight);
        CB::apply(lateAgain, CaptionLayout::Highlight, std::nullopt, p, true, TimeRange{59'000'000, 5'000'000});
        QCOMPARE(segTexts(), cueTexts(again) + cueTexts(lateAgain));

        // Sem substituir: faixa nova.
        const QUuid third = CB::apply(again, CaptionLayout::Phrases, std::nullopt, p, false);
        QVERIFY(third != first);
        QCOMPARE(std::count_if(p.tracks.begin(), p.tracks.end(), [](const Track &t) { return t.isAutoCaptions; }), 2);
    }

    // Layouts: nomes e estilos
    void layouts()
    {
        QStringList names;
        for (const auto l : allCases<CaptionLayout>())
            names.append(displayName(l));
        QCOMPARE(names, (QStringList{"Frases", "Destaque", "Impacto", "Empilhado"}));
        QVERIFY(CB::defaultStyle(CaptionPreset::Punch).fontSize > CB::defaultStyle(CaptionPreset::Phrases).fontSize);
        QCOMPARE(CB::defaultStyle(CaptionPreset::Punch).y, 0.0);
        QVERIFY(CB::defaultStyle(CaptionPreset::Phrases).y < -0.5);
    }

    // MARK: - Largura da caixa

    // setTextWidth limita à faixa e ignora valores inválidos
    void clamps()
    {
        Project p;
        const QUuid id = TE::addText(text(QStringLiteral("Oi")), p, 0);
        const auto width = [&] { return p.segment(id)->text->style.maxWidth; };
        TE::setTextWidth(p, id, 0.01);
        QCOMPARE(width(), TextStyle::maxWidthMin);
        TE::setTextWidth(p, id, 10);
        QCOMPARE(width(), TextStyle::maxWidthMax);
        TE::setTextWidth(p, id, 1.4);
        QCOMPARE(width(), 1.4);
        TE::setTextWidth(p, id, std::nan(""));
        TE::setTextWidth(p, id, INFINITY);
        QCOMPARE(width(), 1.4);
        const Project before = p;
        TE::setTextWidth(p, QUuid::createUuid(), 0.5);
        QCOMPARE(p, before);
    }

    // Numa faixa automática, a largura vale para todos os textos dela e só dela
    void wholeTrack()
    {
        Project p;
        const QUuid user = TE::addText(text(QStringLiteral("Título")), p, 0);
        const auto cues = CB::cues(spokenWords(QStringLiteral("Olá mundo. Tudo bem com você? Vamos lá.")), CaptionLayout::Highlight);
        const QUuid first = CB::apply(cues, CaptionPreset::Highlight, std::nullopt, p, false);
        const QUuid second = CB::apply(cues, CaptionPreset::Phrases, std::nullopt, p, false);
        const auto segs = trackWithId(p, first)->segments;
        QVERIFY(segs.size() > 1);
        TE::setTextWidth(p, segs[1].id, 1.2);
        for (const auto &s : trackWithId(p, first)->segments)
            QCOMPARE(s.text->style.maxWidth, 1.2);
        for (const auto &s : trackWithId(p, second)->segments)
            QVERIFY(s.text->style.maxWidth != 1.2);
        QCOMPARE(p.segment(user)->text->style.maxWidth, TextStyle().maxWidth);
        TE::setTextWidth(p, user, 0.5);
        QCOMPARE(p.segment(user)->text->style.maxWidth, 0.5);
        for (const auto &s : trackWithId(p, first)->segments)
            QCOMPARE(s.text->style.maxWidth, 1.2);
    }

    // Equilibrar: o mesmo número de linhas, com a mais longa menor ou igual
    void balance_data()
    {
        QTest::addColumn<double>("width");
        for (const double w : {0.3, 0.45, 0.6, 0.9})
            QTest::newRow(qPrintable(QString::number(w))) << w;
    }
    void balance()
    {
        QFETCH(double, width);
        const QString t = QStringLiteral("O rato roeu a roupa do rei de Roma e a rainha, com raiva, resolveu remendar tudo sozinha");
        TextStyle style;
        style.fontSize = 0.06;
        style.maxWidth = width;
        TextStyle balanced = style;
        balanced.balanceLines = true;
        TextRenderer &r = TextRenderer::shared();
        const QStringList plain = r.lineTexts(TextContent(t, style), canvas1080);
        const QStringList even = r.lineTexts(TextContent(t, balanced), canvas1080);
        QCOMPARE(even.size(), plain.size());
        QCOMPARE(even.join(u' '), plain.join(u' '));
        const auto widest = [&](const QStringList &lines) {
            double w = 0;
            for (const auto &l : lines)
                w = std::max(w, r.lineWidth(l, style, 100));
            return w;
        };
        QVERIFY(widest(even) <= widest(plain) + 0.01);
        if (plain.size() > 1) {
            // A caixa encolhe junto (é ela que o preview mostra).
            const QSizeF a = *r.boxSize(TextContent(t, style), canvas1080);
            const QSizeF b = *r.boxSize(TextContent(t, balanced), canvas1080);
            QVERIFY(b.width() <= a.width() && b.height() == a.height());
        }
        QCOMPARE(r.lineTexts(TextContent(QStringLiteral("curto"), balanced), canvas1080), QStringList{QStringLiteral("curto")});
    }

    // A caixa passa da largura do quadro: texto longo numa linha só
    void wideBox()
    {
        TextStyle style;
        style.fontSize = 0.07;
        style.balanceLines = true;
        const auto content = [&](double w) {
            style.maxWidth = w;
            return TextContent(QStringLiteral("uma legenda bem comprida que normalmente quebraria em duas linhas ou mais"), style);
        };
        QVERIFY(TextRenderer::shared().lineTexts(content(0.9), canvas1080).size() > 1);
        QCOMPARE(TextRenderer::shared().lineTexts(content(3), canvas1080).size(), 1);
        QVERIFY(TextRenderer::shared().boxSize(content(3), canvas1080)->width() > canvas1080.width());
    }

    // MARK: - Predefinições

    // Nomes, grupos e segmentação
    void catalog()
    {
        QStringList names;
        QList<CaptionPreset> reveal, noAccent;
        for (const CaptionPreset p : allCases<CaptionPreset>()) {
            names.append(displayName(p));
            QVERIFY(!detail(p).isEmpty());
            if (group(p) == CaptionPresetGroup::Reveal)
                reveal.append(p);
            if (!usesAccent(p))
                noAccent.append(p);
        }
        QCOMPARE(names, (QStringList{"Frases", "Destaque", "Impacto", "Surgir", "Leitura guiada", "Preenchimento",
                                     "Digitação", "Editorial", "Cinema", "Empilhado"}));
        QStringList groups;
        for (const auto g : allCases<CaptionPresetGroup>())
            groups.append(displayName(g));
        QCOMPARE(groups, (QStringList{"Clássicos", "Revelação", "Editorial"}));
        QCOMPARE(reveal, (QList<CaptionPreset>{CaptionPreset::Rise, CaptionPreset::Guided, CaptionPreset::Fill,
                                               CaptionPreset::Typing}));
        QCOMPARE(noAccent, (QList<CaptionPreset>{CaptionPreset::Phrases, CaptionPreset::Punch, CaptionPreset::Cinema}));
        QCOMPARE(layout(CaptionPreset::Stacked), CaptionLayout::Stacked);
        QStringList reveals;
        for (const auto r : allCases<WordReveal>())
            reveals.append(displayName(r));
        QCOMPARE(reveals, (QStringList{"Nenhuma", "Surgir", "Leitura guiada", "Preenchimento", "Digitação"}));
        for (const auto l : allCases<CaptionLayout>())
            QCOMPARE(layout(presetFor(l)), l);
    }

    // Os clássicos continuam iguais pela API antiga e pela nova
    void classicsMatch()
    {
        const auto words = spokenWords(presetSpeech);
        for (const CaptionLayout l : {CaptionLayout::Phrases, CaptionLayout::Highlight, CaptionLayout::Punch}) {
            Project a, b;
            CB::apply(CB::cues(words, l), l, std::nullopt, a, false);
            CB::apply(CB::cues(words, presetFor(l)), presetFor(l), std::nullopt, b, false);
            const auto &sa = a.tracks.last().segments;
            const auto &sb = b.tracks.last().segments;
            QCOMPARE(sa.size(), sb.size());
            for (int k = 0; k < sa.size(); ++k) {
                QCOMPARE(sa[k].text, sb[k].text);
                QCOMPARE(sa[k].animationIn, sb[k].animationIn);
                QCOMPARE(sa[k].targetStart, sb[k].targetStart);
            }
        }
    }

    // Cada visual leva o destaque, a palavra-chave e a cor nos lugares certos
    void applies_data() { presetRows(); }
    void applies()
    {
        QFETCH(CaptionPreset, preset);
        Project p;
        const RGBA accent{0.1, 0.9, 0.4, 1};
        const auto cues = CB::cues(spokenWords(presetSpeech), preset);
        const QUuid id = CB::apply(cues, preset, accent, p, false);
        const auto segs = trackWithId(p, id)->segments;
        QCOMPARE(segs.size(), cues.size());
        const bool highlighted = preset == CaptionPreset::Highlight || group(preset) == CaptionPresetGroup::Reveal;
        const bool keyworded = preset == CaptionPreset::Editorial || preset == CaptionPreset::Stacked;
        bool anyKeyword = false;
        for (const Segment &seg : segs) {
            const TextContent &t = *seg.text;
            QCOMPARE(t.style, CB::defaultStyle(preset));
            QCOMPARE(t.words->size(), TextContent::wordsOf(t.text).size());
            if (highlighted)
                QCOMPARE(t.highlight->color, accent);
            else
                QVERIFY(!t.highlight);
            if (keyworded) {
                QCOMPARE(*t.emphasis->color, accent);
                QVERIFY(t.emphasis->words.size() <= (t.words->size() > 6 ? 2 : 1));
                anyKeyword |= !t.emphasis->words.isEmpty();
            } else {
                QVERIFY(!t.emphasis);
            }
        }
        QCOMPARE(anyKeyword, keyworded);
        const std::map<CaptionPreset, WordReveal> reveal{{CaptionPreset::Rise, WordReveal::Rise},
                                                         {CaptionPreset::Guided, WordReveal::Dim},
                                                         {CaptionPreset::Fill, WordReveal::Fill},
                                                         {CaptionPreset::Typing, WordReveal::Type}};
        if (reveal.count(preset))
            QCOMPARE(segs[0].text->highlight->reveal, reveal.at(preset));
    }

    // Empilhado: 2 ou 3 linhas de 1 ou 2 palavras, a palavra-chave sozinha na linha
    void stacked()
    {
        const auto cues = CB::cues(spokenWords(presetSpeech), CaptionPreset::Stacked);
        for (const auto &cue : cues) {
            QVERIFY(cue.words.size() >= 1 && cue.words.size() <= 5);
            if (cue.words.size() <= 1)
                continue;
            const QStringList lines = cue.text.split(u'\n');
            QVERIFY2(lines.size() >= 2 && lines.size() <= 3, qPrintable(cue.text));
            QStringList flat;
            for (const auto &l : lines)
                flat += l.split(u' ');
            QCOMPARE(flat, texts(cue.words));
            const QList<int> keys = CB::keywords(cue);
            int index = 0;
            for (const auto &l : lines) {
                const int n = int(l.split(u' ').size());
                if (n > 1)
                    for (const int k : keys)
                        QVERIFY2(k < index || k >= index + n, qPrintable(cue.text));
                index += n;
            }
        }
        const TextStyle style = CB::defaultStyle(CaptionPreset::Stacked);
        QVERIFY(style.fitLines && !style.balanceLines && style.textCase == TextCase::Upper);
    }

    // Empilhado: o bloco mais alto fica em ~45% da altura do quadro em qualquer proporção
    void stackedHeight_data()
    {
        QTest::addColumn<double>("aspect");
        QTest::addColumn<QSize>("canvas");
        QTest::newRow("16:9") << 16.0 / 9 << QSize(1920, 1080);
        QTest::newRow("9:16") << 9.0 / 16 << QSize(1080, 1920);
        QTest::newRow("1:1") << 1.0 << QSize(1080, 1080);
    }
    void stackedHeight()
    {
        QFETCH(double, aspect);
        QFETCH(QSize, canvas);
        const TextStyle style = CB::defaultStyle(CaptionPreset::Stacked, aspect);
        const auto cues = CB::cues(
            spokenWords(presetSpeech + QStringLiteral(" Pronto: as legendas aparecem na timeline, uma faixa só delas.")),
            CaptionPreset::Stacked, aspect);
        double highest = 0;
        for (const auto &cue : cues) {
            TextContent content(cue.text, style);
            WordEmphasis e;
            e.words = CB::keywords(cue);
            content.emphasis = e;
            highest = std::max(highest, TextRenderer::shared().boxSize(content, canvas)->height() / canvas.height());
        }
        // No Mac, 45%. Aqui a fonte condensada do Mac pode faltar (a de reserva é mais larga e encolhe a linha),
        // então o limite de baixo é mais folgado.
        QVERIFY2(highest <= 0.47, qPrintable(QString::number(highest)));
        QVERIFY2(highest >= 0.2, qPrintable(QString::number(highest)));
        if (aspect < 1) {
            QVERIFY(std::abs(style.maxWidth - 0.64) < 0.001);
            QVERIFY(std::abs(style.fontSize * canvas.height() - 0.13 * canvas.width()) < 0.1);
        }
        QCOMPARE(CB::defaultStyle(CaptionPreset::Stacked, 16.0 / 9), CB::defaultStyle(CaptionPreset::Stacked));
    }

    // Palavra-chave: nunca uma palavra funcional, no máximo uma (duas com mais de 6 palavras); determinística
    void keywords()
    {
        const QStringList samples{
            "o problema de verdade é a velocidade",
            "eu acho que vocês vão adorar essa novidade",
            "de para com sem sobre entre",
            "The quick brown fox jumps over the lazy dog because everything matters",
            "mas porque então também",
            "a",
        };
        for (const QString &t : samples) {
            const CaptionCue cue{t, 0, 1, spokenWords(t)};
            const QList<int> picked = CB::keywords(cue);
            QVERIFY(picked.size() <= (cue.words.size() > 6 ? 2 : 1));
            for (const int i : picked) {
                const QString word = cue.words[i].text;
                QVERIFY(!CB::isFunctionWord(word));
                QVERIFY2(std::count_if(word.begin(), word.end(), [](QChar c) { return c.isLetter(); }) >= 5, qPrintable(word));
            }
            QCOMPARE(CB::keywords(cue), picked);
        }
        QVERIFY(CB::keywords({QString(), 0, 1, spokenWords(QStringLiteral("de para com sem sobre entre"))}).isEmpty());
        const CaptionCue speed{QString(), 0, 1, spokenWords(QStringLiteral("o problema real é a velocidade"))};
        QCOMPARE(CB::keywords(speed), QList<int>{5});
        // Sem o etiquetador do macOS, os advérbios em "-mente" perdem para a palavra de peso.
        const CaptionCue adverb{QString(), 0, 1, spokenWords(QStringLiteral("rapidamente surgiu uma solução"))};
        QCOMPARE(CB::keywords(adverb), QList<int>{3});
        // Duas, não vizinhas, com mais de 6 palavras.
        const CaptionCue two{QString(), 0, 1,
                             spokenWords(QStringLiteral("hoje a montanha gigante cobriu toda cidade pequena"))};
        const QList<int> both = CB::keywords(two);
        QCOMPARE(both.size(), 2);
        QVERIFY(std::abs(both[0] - both[1]) > 1);
    }

    // MARK: - Transcrição (cabeçalhos da etapa 1)

    // Catálogo dos modelos: nomes, arquivos, tamanhos e o recomendado (small, só CPU)
    void models()
    {
        QStringList names;
        for (const auto m : allCases<WhisperModel>())
            names.append(displayName(m));
        QCOMPARE(names, (QStringList{"Rápido", "Equilibrado", "Máxima precisão"}));
        QCOMPARE(recommendedModel(), WhisperModel::Small);
        QCOMPARE(fileName(WhisperModel::Turbo), QStringLiteral("ggml-large-v3-turbo-q5_0.bin"));
        QCOMPARE(byteSize(WhisperModel::Base), 147'951'465);
        QCOMPARE(downloadUrl(WhisperModel::Small).toString(),
                 QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"));
        for (const auto m : allCases<WhisperModel>())
            QCOMPARE(sha256(m).size(), 64);

        // Instalado = arquivo com o tamanho certo; apagar leva o parcial junto.
        QTemporaryDir dir;
        ModelStore store(dir.path());
        QVERIFY(!store.isInstalled(WhisperModel::Base));
        QFile partial(store.path(WhisperModel::Base) + QStringLiteral(".partial"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("x");
        partial.close();
        QFile model(store.path(WhisperModel::Base));
        QVERIFY(model.open(QIODevice::WriteOnly));
        QVERIFY(model.resize(byteSize(WhisperModel::Base)));
        model.close();
        QVERIFY(store.isInstalled(WhisperModel::Base));
        QVERIFY(store.remove(WhisperModel::Base));
        QVERIFY(!QFile::exists(store.path(WhisperModel::Base)) && !partial.exists());
        // Sem a variável, a pasta padrão termina em "Models"; com ela, é a dela.
        qputenv("OPENFRAME_MODELS_DIR", dir.path().toUtf8());
        QCOMPARE(ModelStore::defaultDirectory(), dir.path());
        qunsetenv("OPENFRAME_MODELS_DIR");
        QVERIFY(ModelStore::defaultDirectory().endsWith(QStringLiteral("/Models")));
    }

    // Progresso: a mistura pesa 10% do total
    void progress()
    {
        using P = TranscriptionProgress::Phase;
        QCOMPARE(TranscriptionProgress::make(P::Mixing, 0.5).overall, 0.05);
        QCOMPARE(TranscriptionProgress::make(P::Transcribing, 0).overall, 0.1);
        QCOMPARE(TranscriptionProgress::make(P::Transcribing, 1).overall, 1.0);
        QCOMPARE(TranscriptionProgress::make(P::Mixing, 2).fraction, 1.0);
    }

    // Trecho a transcrever e o projeto que a mistura ouve
    void sourceAndMix()
    {
        Project p;
        QVERIFY(!Transcriber::sourceRange(p, TranscriptionSource::timeline()));
        const QUuid a = TE::addMedia(video(4, QStringLiteral("a.mov")), p);
        const QUuid b = TE::addMedia(video(3, QStringLiteral("b.mov")), p);
        const QUuid still = TE::addMedia(photo(), p);
        QCOMPARE(*Transcriber::sourceRange(p, TranscriptionSource::timeline()), (TimeRange{0, p.duration()}));
        QCOMPARE(*Transcriber::sourceRange(p, TranscriptionSource::of(b)), p.segment(b)->targetRange());
        QVERIFY(!Transcriber::sourceRange(p, TranscriptionSource::of(still)));
        TE::update(p, b, [](Segment &s) { s.volume = 0; });
        p.tracks[0].muted = true;
        const Project mix = Transcriber::mixProject(p, TranscriptionSource::of(b));
        QCOMPARE(mix.tracks[0].segments.size(), 1);
        QCOMPARE(mix.tracks[0].segments[0].id, b);
        QVERIFY(!mix.tracks[0].muted);
        QCOMPARE(mix.tracks[0].segments[0].volume, 1.0);
        QCOMPARE(Transcriber::mixProject(p, TranscriptionSource::timeline()), p);
        Q_UNUSED(a)
        QCOMPARE(Transcriber::languages().first().code, QStringLiteral("pt"));
        QCOMPARE(Transcriber::languages().size(), 20);
    }

    // OPENFRAME_FAKE_TRANSCRIPTION=1: palavras falsas pelo trecho, com progresso; sem ela, erro claro
    void fakeTranscription()
    {
        Project p;
        TE::addMedia(video(12), p);
        const auto words = Transcriber::fakeWords({1'000'000, 10'000'000});
        QVERIFY(words.size() > 15);
        QVERIFY(words.first().start >= 1'000'000 && words.last().end <= 11'000'000);
        const auto cues = CB::cues(words, CaptionPreset::Highlight);
        QVERIFY(cues.size() > 3);

        qputenv("OPENFRAME_FAKE_TRANSCRIPTION", "1");
        Transcriber t;
        QSignalSpy progress(&t, &Transcriber::progress);
        QSignalSpy finished(&t, &Transcriber::finished);
        t.start(p, TranscriptionSource::timeline(), WhisperModel::Small);
        QVERIFY(t.isRunning());
        QVERIFY(finished.wait(5000));
        QVERIFY(!t.isRunning());
        QCOMPARE(finished.first()[0].toBool(), true);
        QVERIFY(!finished.first()[1].value<QList<TranscribedWord>>().isEmpty());
        QCOMPARE(progress.size(), 13);
        QCOMPARE(progress.last()[0].value<TranscriptionProgress>().overall, 1.0);

        // Cancelado: ok falso, sem erro.
        finished.clear();
        t.start(p, TranscriptionSource::timeline(), WhisperModel::Small);
        t.cancel();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first()[0].toBool(), false);
        QVERIFY(finished.first()[2].toString().isEmpty());
        qunsetenv("OPENFRAME_FAKE_TRANSCRIPTION");

        // Sem a variável e sem o modelo: a mensagem do modelo não instalado.
        QTemporaryDir dir;
        qputenv("OPENFRAME_MODELS_DIR", dir.path().toUtf8());
        finished.clear();
        t.start(p, TranscriptionSource::timeline(), WhisperModel::Base);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first()[0].toBool(), false);
        // `ModelStore::shared()` já pode ter sido criado com outra pasta: vale qualquer um dos dois erros.
        const QString error = finished.first()[2].toString();
        QVERIFY2(error.contains(QStringLiteral("não está instalado")) || error.contains(QStringLiteral("não está disponível")),
                 qPrintable(error));
        qunsetenv("OPENFRAME_MODELS_DIR");
    }
};

QTEST_MAIN(CaptionTests)
#include "CaptionTests.moc"
