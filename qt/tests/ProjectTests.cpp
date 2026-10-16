// Porte da suíte "Projeto" (ModelTests.swift) e compatibilidade do formato
// .openframe com os projetos gravados pela versão Swift (fixtures/).
#include "ProjectStore.h"
#include "TestSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <set>
#include <QTemporaryDir>
#include <functional>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

QByteArray readFixture(const QString &name)
{
    QFile file(QStringLiteral(OPENFRAME_FIXTURES "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fixture ausente: %s", qPrintable(name));
    return file.readAll();
}

/// Primeira diferença entre dois JSON (vazio = iguais). Números comparam pelo
/// valor: o Swift grava `1` e `1.0` do mesmo jeito.
QString jsonDiff(const QJsonValue &a, const QJsonValue &b, const QString &path = QStringLiteral("$"))
{
    if (a.isDouble() && b.isDouble())
        return a.toDouble() == b.toDouble() ? QString() : QStringLiteral("%1: %2 ≠ %3").arg(path).arg(a.toDouble()).arg(b.toDouble());
    if (a.type() != b.type())
        return QStringLiteral("%1: tipos diferentes").arg(path);
    if (a.isObject()) {
        const QJsonObject oa = a.toObject(), ob = b.toObject();
        if (oa.keys() != ob.keys())
            return QStringLiteral("%1: chaves %2 ≠ %3").arg(path, oa.keys().join(u','), ob.keys().join(u','));
        for (auto it = oa.begin(); it != oa.end(); ++it)
            if (const QString d = jsonDiff(it.value(), ob.value(it.key()), path + u'.' + it.key()); !d.isEmpty())
                return d;
        return {};
    }
    if (a.isArray()) {
        const QJsonArray aa = a.toArray(), ab = b.toArray();
        if (aa.size() != ab.size())
            return QStringLiteral("%1: tamanhos %2 ≠ %3").arg(path).arg(aa.size()).arg(ab.size());
        for (qsizetype i = 0; i < aa.size(); ++i)
            if (const QString d = jsonDiff(aa[i], ab[i], QStringLiteral("%1[%2]").arg(path).arg(i)); !d.isEmpty())
                return d;
        return {};
    }
    return a == b ? QString() : QStringLiteral("%1: %2 ≠ %3").arg(path, a.toVariant().toString(), b.toVariant().toString());
}

QJsonObject parse(const QByteArray &data) { return QJsonDocument::fromJson(data).object(); }

} // namespace

class ProjectTests : public QObject
{
    Q_OBJECT

private slots:
    // Salvar e abrir preserva tudo
    void roundTrip()
    {
        Project p;
        p.canvas.aspect = AspectRatio::R9x16;
        p.canvas.background = CanvasBackground::blur(0.4);
        const QUuid id = TE::addMedia(video(5, QStringLiteral("a.mov")), p);
        TE::update(p, id, [](Segment &s) {
            s.adjust.brightness = 0.3;
            s.filter = FilterSetting{FilterPreset::Cinematic, 0.7};
            s.effects = {VideoEffect{QUuid::createUuid(), VideoEffectKind::RgbSplit}};
            s.animationIn = SegmentAnimation{AnimationKind::ZoomIn};
        });
        TE::addText(text(QStringLiteral("Olá")), p, 0);
        QString error;
        const auto back = ProjectStore::decode(ProjectStore::encode(p), &error);
        QVERIFY2(back, qPrintable(error));
        QVERIFY(*back == p);

        // Gravar e abrir pelo disco também.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.openframe"));
        QVERIFY(ProjectStore::save(p, path, &error));
        QVERIFY(*ProjectStore::load(path) == p);
    }

    // Canvas: proporção original vem do primeiro clipe
    void canvasSize()
    {
        Project p;
        QCOMPARE(p.canvasSize(1080), QSize(1920, 1080));
        Material portrait = video(1, QStringLiteral("p.mov"));
        portrait.width = 1080;
        portrait.height = 1920;
        portrait.hasAudio = false;
        TE::addMedia(portrait, p);
        QCOMPARE(p.canvasSize(720), QSize(720, 1280));
        p.canvas.aspect = AspectRatio::R1x1;
        QCOMPARE(p.canvasSize(1080), QSize(1080, 1080));
        p.canvas.aspect = AspectRatio::R4x3;
        QCOMPARE(p.canvasSize(480), QSize(640, 480));
        p.canvas.aspect = AspectRatio::R21x9;
        QCOMPARE(p.canvasSize(1080), QSize(2520, 1080));
    }

    // Os projetos gravados pelo Swift são lidos e gravados de volta com o mesmo conteúdo
    void swiftFixturesRoundTrip_data()
    {
        QTest::addColumn<QString>("file");
        for (const QString &name : QDir(QStringLiteral(OPENFRAME_FIXTURES)).entryList({QStringLiteral("*.openframe")}))
            QTest::newRow(qPrintable(name)) << name;
    }

    void swiftFixturesRoundTrip()
    {
        QFETCH(QString, file);
        const QByteArray data = readFixture(file);
        QString error;
        const auto p = ProjectStore::decode(data, &error);
        QVERIFY2(p, qPrintable(error));
        const QByteArray again = ProjectStore::encode(*p);
        const QString diff = jsonDiff(parse(data), parse(again));
        QVERIFY2(diff.isEmpty(), qPrintable(diff));
        // Ler o que gravamos dá o mesmo projeto.
        QVERIFY(*ProjectStore::decode(again) == *p);
    }

    // O projeto completo do Swift chega com os valores certos
    void swiftFixtureValues()
    {
        auto p = ProjectStore::decode(readFixture(QStringLiteral("completo.openframe")));
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("Projeto completo"));
        QCOMPARE(p->fps, 29.97);
        QCOMPARE(p->canvas.aspect, AspectRatio::R9x16);
        QVERIFY(p->canvas.background.colorValue());
        QCOMPARE(*p->canvas.background.colorValue(), (RGBA{0.1, 0.2, 0.3, 1}));
        QCOMPARE(p->materials.size(), 3);
        QCOMPARE(p->materials[2].name, QStringLiteral("música.m4a"));
        QCOMPARE(p->materials[0].duration, 12'345'678);
        QCOMPARE(p->tracks.size(), 4);

        const Segment &a = p->tracks[0].segments[0];
        QCOMPARE(a.speed, 2.0);
        QCOMPARE(a.source, (TimeRange{1'000'000, 4'000'000}));
        QCOMPARE(a.filter->preset, FilterPreset::Cinematic);
        QCOMPARE(a.effects.size(), 2);
        QCOMPARE(a.effects[1].kind, VideoEffectKind::Shake);
        QCOMPARE(a.animationOut->kind, AnimationKind::SlideLeft);
        QCOMPARE(a.transition->kind, TransitionKind::Dissolve);
        QCOMPARE(a.adjust.grain, 0.35);
        QVERIFY(!a.text);
        // Mesmo layout magnético do Swift: 2 s na timeline menos 0,5 s de transição.
        QCOMPARE(p->tracks[0].segments[1].targetStart, 1'500'000);
        TE::relayoutMain(*p);
        QCOMPARE(p->tracks[0].segments[1].targetStart, 1'500'000);

        QVERIFY(p->tracks[2].muted);
        const Track &titles = p->tracks[3];
        QVERIFY(titles.hidden);
        QCOMPARE(titles.kind, TrackKind::Text);
        const Segment &t = titles.segments[0];
        QVERIFY(!t.materialId);
        QCOMPARE(t.text->text, QStringLiteral("Olá, mundo!\nSegunda linha 🎬"));
        QCOMPARE(t.text->style.fontFace, QStringLiteral("Semibold"));
        QCOMPARE(t.text->style.textCase, TextCase::Upper);
        QCOMPARE(t.text->style.alignment, TextAlignment::Left);
        QCOMPARE(t.text->style.shadowAngle, -45.0);
        QCOMPARE(t.animationIn->kind, AnimationKind::Typewriter);

        const auto black = ProjectStore::decode(readFixture(QStringLiteral("fundo-preto.openframe")));
        QVERIFY(black && black->canvas.background.isBlack());
        const auto blur = ProjectStore::decode(readFixture(QStringLiteral("fundo-desfoque.openframe")));
        QVERIFY(blur);
        QCOMPARE(blur->canvas.background.blurIntensity(), std::optional(0.6));
    }

    // Formato: UUID em maiúsculas, opcionais nulos omitidos, todos os outros campos gravados
    void encodingRules()
    {
        Project p;
        TE::addMedia(video(2), p);
        const QByteArray data = ProjectStore::encode(p);
        const QJsonObject o = parse(data);
        const QString id = o["tracks"][0]["id"].toString();
        QCOMPARE(id, id.toUpper());
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}$"))
                    .match(id)
                    .hasMatch());
        const QJsonObject seg = o["tracks"][0]["segments"][0].toObject();
        for (const char *absent : {"filter", "animationIn", "animationOut", "transition", "text"})
            QVERIFY2(!seg.contains(QLatin1StringView(absent)), absent);
        QCOMPARE(seg.keys(), (QStringList{"adjust", "effects", "fadeIn", "fadeOut", "id", "materialId", "opacity",
                                          "source", "speed", "targetStart", "transform", "volume"}));
        QCOMPARE(o["canvas"]["background"].toObject(), (QJsonObject{{"black", QJsonObject{}}}));
        QCOMPARE(o["canvas"]["aspect"].toString(), QStringLiteral("original"));
        QCOMPARE(o.keys(), (QStringList{"canvas", "formatVersion", "fps", "materials", "name", "tracks"}));
        // Inteiros continuam inteiros no texto.
        QVERIFY(data.contains("\"duration\": 2000000"));
    }

    // Leitura exige os campos, como o Codable do Swift (menos o estilo do texto)
    void decodingIsStrict()
    {
        QJsonObject o = parse(readFixture(QStringLiteral("completo.openframe")));
        QString error;

        QJsonObject noFps = o;
        noFps.remove(QStringLiteral("fps"));
        QVERIFY(!ProjectStore::fromJson(noFps, &error));
        QVERIFY2(error.contains(QStringLiteral("fps")), qPrintable(error));

        QJsonObject badEnum = o;
        QJsonObject canvas = badEnum["canvas"].toObject();
        canvas["aspect"] = QStringLiteral("r2x1");
        badEnum["canvas"] = canvas;
        QVERIFY(!ProjectStore::fromJson(badEnum, &error));
        QVERIFY2(error.contains(QStringLiteral("r2x1")), qPrintable(error));

        QJsonObject badInt = o;
        QJsonArray materials = badInt["materials"].toArray();
        QJsonObject m0 = materials[0].toObject();
        m0["duration"] = 1.5;
        materials[0] = m0;
        badInt["materials"] = materials;
        QVERIFY(!ProjectStore::fromJson(badInt, &error));

        QVERIFY(!ProjectStore::decode("{ nada", &error));
        QVERIFY(!ProjectStore::decode("[]", &error));
    }

    /// O primeiro texto do projeto, com `change` aplicado ao JSON do segmento e da faixa.
    static QJsonObject editedText(const std::function<void(QJsonObject &track, QJsonObject &seg)> &change)
    {
        Project p;
        TE::addText(text(QStringLiteral("um dois")), p, 0);
        QJsonObject o = ProjectStore::toJson(p);
        QJsonArray tracks = o["tracks"].toArray();
        QJsonObject track = tracks[1].toObject();
        QJsonArray segments = track["segments"].toArray();
        QJsonObject seg = segments[0].toObject();
        change(track, seg);
        segments[0] = seg;
        track["segments"] = segments;
        tracks[1] = track;
        o["tracks"] = tracks;
        return o;
    }

    // Legendas automáticas: `words`/`highlight` no texto e `isAutoCaptions` na faixa são lidos e ficam ao
    // regravar; no destaque, campos ausentes ficam com o padrão
    void captionFieldsRoundTrip()
    {
        const QJsonObject o = editedText([](QJsonObject &track, QJsonObject &seg) {
            track["isAutoCaptions"] = true;
            QJsonObject content = seg["text"].toObject();
            content["words"] = QJsonArray{QJsonObject{{"text", "um"}, {"start", 0}, {"end", 300000}},
                                          QJsonObject{{"text", "dois"}, {"start", 350000}, {"end", 700000}}};
            content["highlight"] = QJsonObject{{"color", QJsonObject{{"r", 1}, {"g", 0.85}, {"b", 0.1}, {"a", 1}}},
                                               {"scale", 1.12}, {"backgroundEnabled", true}, {"mode", "progressive"}};
            seg["text"] = content;
        });
        QString error;
        const auto back = ProjectStore::fromJson(o, &error);
        QVERIFY2(back, qPrintable(error));
        const Track &track = back->tracks[1];
        QVERIFY(track.isAutoCaptions);
        const TextContent &t = *track.segments[0].text;
        QCOMPARE(t.words->size(), 2);
        QCOMPARE(t.words->at(1), (CaptionWord{QStringLiteral("dois"), 350000, 700000}));
        QVERIFY(t.highlight->backgroundEnabled);
        QCOMPARE(t.highlight->mode, HighlightMode::Progressive);
        QCOMPARE(t.highlight->reveal, WordReveal::None);
        QCOMPARE(t.highlight->dimOpacity, 0.35);
        QCOMPARE(t.highlight->backgroundColor, (RGBA{0.1, 0.45, 1, 1}));
        QVERIFY(t.activeHighlight());

        const QJsonObject again = ProjectStore::toJson(*back)["tracks"][1].toObject();
        QCOMPARE(again["isAutoCaptions"].toBool(), true);
        const QJsonObject againText = again["segments"][0]["text"].toObject();
        QCOMPARE(againText["words"].toArray().size(), 2);
        // O destaque sai com todos os campos, como o Codable do Swift grava.
        QCOMPARE(againText["highlight"]["reveal"].toString(), QStringLiteral("none"));
        QCOMPARE(againText["highlight"]["dimOpacity"].toDouble(), 0.35);
        QVERIFY(!againText.contains(QStringLiteral("emphasis")));
        // Sem legendas, as chaves não são gravadas.
        const QJsonObject plain = ProjectStore::toJson(Project())["tracks"][0].toObject();
        QVERIFY(!plain.contains(QStringLiteral("isAutoCaptions")));
    }

    // Estilos de legenda: `balanceLines`/`fitLines`, `maxWidth` acima de 1, `reveal`/`dimOpacity` e `emphasis`
    // ficam ao regravar; `mode`/`reveal` desconhecidos viram o padrão; a ênfase sem cor não grava a cor
    void captionStyleFieldsRoundTrip()
    {
        const QJsonObject o = editedText([](QJsonObject &track, QJsonObject &seg) {
            track["isAutoCaptions"] = true;
            QJsonObject content = seg["text"].toObject();
            content["text"] = QStringLiteral("um dois\ntrês");
            QJsonObject style = content["style"].toObject();
            style["balanceLines"] = true;
            style["fitLines"] = true;
            style["maxWidth"] = 2.5;
            content["style"] = style;
            content["words"] = QJsonArray{QJsonObject{{"text", "um"}, {"start", 0}, {"end", 300000}},
                                          QJsonObject{{"text", "dois"}, {"start", 350000}, {"end", 700000}},
                                          QJsonObject{{"text", "três"}, {"start", 750000}, {"end", 900000}}};
            content["highlight"] = QJsonObject{{"scale", 1.05}, {"mode", "progressive"}, {"reveal", "fill"},
                                               {"dimOpacity", 0.35}};
            content["emphasis"] = QJsonObject{{"words", QJsonArray{1}}, {"fontName", "Didot"}, {"fontFace", "Italic"},
                                              {"scale", 1.3},
                                              {"color", QJsonObject{{"r", 1}, {"g", 0.8}, {"b", 0.1}, {"a", 1}}}};
            seg["text"] = content;
        });
        QString error;
        const auto back = ProjectStore::fromJson(o, &error);
        QVERIFY2(back, qPrintable(error));
        const TextContent &t = *back->tracks[1].segments[0].text;
        QCOMPARE(t.text, QStringLiteral("um dois\ntrês"));
        QCOMPARE(t.style.maxWidth, 2.5);
        QVERIFY(t.style.balanceLines && t.style.fitLines);
        QCOMPARE(t.highlight->reveal, WordReveal::Fill);
        QCOMPARE(t.emphasis->words, QList<int>{1});
        QCOMPARE(*t.emphasis->color, (RGBA{1, 0.8, 0.1, 1}));
        const QJsonObject again = ProjectStore::toJson(*back);
        QString diff = jsonDiff(o["tracks"][1]["segments"][0]["text"]["emphasis"],
                                again["tracks"][1]["segments"][0]["text"]["emphasis"]);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));
        QCOMPARE(again["tracks"][1]["segments"][0]["text"]["style"]["fitLines"].toBool(), true);

        // Valores de uma versão futura: o modo e a revelação desconhecidos viram o padrão; a ênfase
        // sem campos fica com o padrão (Didot itálico, 1,3, sem cor).
        const QJsonObject future = editedText([](QJsonObject &, QJsonObject &seg) {
            QJsonObject content = seg["text"].toObject();
            content["highlight"] = QJsonObject{{"mode", "rainbow"}, {"reveal", "explode"}};
            content["emphasis"] = QJsonObject{};
            seg["text"] = content;
        });
        const auto read = ProjectStore::fromJson(future, &error);
        QVERIFY2(read, qPrintable(error));
        const TextContent &f = *read->tracks[1].segments[0].text;
        QCOMPARE(f.highlight->mode, HighlightMode::Current);
        QCOMPARE(f.highlight->reveal, WordReveal::None);
        QCOMPARE(*f.emphasis, WordEmphasis{});
        QVERIFY(!ProjectStore::toJson(*read)["tracks"][1]["segments"][0]["text"]["emphasis"].toObject().contains(
            QStringLiteral("color")));
        // Um tipo errado ainda é erro, com o caminho.
        const QJsonObject bad = editedText([](QJsonObject &, QJsonObject &seg) {
            QJsonObject content = seg["text"].toObject();
            content["highlight"] = QJsonObject{{"reveal", 3}};
            seg["text"] = content;
        });
        QVERIFY(!ProjectStore::fromJson(bad, &error));
        QVERIFY2(error.contains(QStringLiteral("highlight.reveal")), qPrintable(error));
    }

    // As 17 animações e os 4 loops são lidos e gravados; tipo desconhecido vira esmaecer e loop
    // desconhecido é descartado
    void animationsRoundTrip()
    {
        const QJsonObject o = editedText([](QJsonObject &, QJsonObject &seg) {
            seg["animationIn"] = QJsonObject{{"kind", "cascade"}, {"duration", 700000}};
            seg["animationOut"] = QJsonObject{{"kind", "decode"}, {"duration", 400000}};
            seg["animationLoop"] = QJsonObject{{"kind", "shine"}, {"period", 2000000}, {"intensity", 0.5}};
        });
        QString error;
        const auto back = ProjectStore::fromJson(o, &error);
        QVERIFY2(back, qPrintable(error));
        const Segment &s = back->tracks[1].segments[0];
        QCOMPARE(s.animationIn->kind, AnimationKind::Cascade);
        QCOMPARE(s.animationOut->kind, AnimationKind::Decode);
        QCOMPARE(*s.animationLoop, (LoopAnimation{LoopKind::Shine, 2000000, 0.5}));
        const QString diff = jsonDiff(o, ProjectStore::toJson(*back));
        QVERIFY2(diff.isEmpty(), qPrintable(diff));
        QCOMPARE(allCases<AnimationKind>().size(), 17);
        QCOMPARE(allCases<LoopKind>().size(), 4);

        const QJsonObject future = editedText([](QJsonObject &, QJsonObject &seg) {
            seg["animationIn"] = QJsonObject{{"kind", "teleport"}, {"duration", 700000}};
            seg["animationLoop"] = QJsonObject{{"kind", "rainbow"}, {"period", 2000000}, {"intensity", 0.5}};
        });
        const auto read = ProjectStore::fromJson(future, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->tracks[1].segments[0].animationIn->kind, AnimationKind::Fade);
        QVERIFY(!read->tracks[1].segments[0].animationLoop);

        // O tipo ainda precisa ser texto, e a duração continua obrigatória.
        const QJsonObject bad = editedText([](QJsonObject &, QJsonObject &seg) {
            seg["animationIn"] = QJsonObject{{"kind", 3}, {"duration", 700000}};
        });
        QVERIFY(!ProjectStore::fromJson(bad, &error));
        QVERIFY2(error.contains(QStringLiteral("animationIn.kind")), qPrintable(error));
        const QJsonObject badLoop = editedText([](QJsonObject &, QJsonObject &seg) {
            seg["animationLoop"] = QJsonObject{{"kind", "float"}, {"intensity", 0.5}};
        });
        QVERIFY(!ProjectStore::fromJson(badLoop, &error));
        QVERIFY2(error.contains(QStringLiteral("animationLoop.period")), qPrintable(error));
    }

    // Os fixtures de motion e legendas do Swift chegam com os valores certos
    void swiftMotionAndCaptionFixtures()
    {
        const auto motion = ProjectStore::decode(readFixture(QStringLiteral("motion.openframe")));
        QVERIFY(motion);
        const Track &texts = motion->tracks[1];
        QCOMPARE(texts.segments.size(), 17);
        for (int k = 0; k < 17; ++k)
            QCOMPARE(texts.segments[k].animationIn->kind, AnimationKind(k));
        QCOMPARE(texts.segments[3].animationLoop->kind, LoopKind::Shine);
        QCOMPARE(motion->tracks[0].segments[0].animationLoop->intensity, 0.8);

        const auto captions = ProjectStore::decode(readFixture(QStringLiteral("legendas.openframe")));
        QVERIFY(captions);
        const Track &auto_ = captions->tracks[1];
        QVERIFY(auto_.isAutoCaptions);
        QVERIFY(!captions->tracks[2].isAutoCaptions);
        int withWords = 0, fit = 0, emphasized = 0;
        std::set<WordReveal> reveals;
        for (const Segment &s : auto_.segments) {
            QVERIFY(s.text->words);
            withWords += s.text->activeHighlight() ? 1 : 0;
            fit += s.text->style.fitLines ? 1 : 0;
            emphasized += s.text->emphasis ? 1 : 0;
            if (s.text->highlight)
                reveals.insert(s.text->highlight->reveal);
        }
        QVERIFY(withWords > 20);
        QVERIFY(fit > 0 && emphasized > fit);
        QCOMPARE(reveals.size(), 5);
        const TextContent &manual = *captions->tracks[2].segments[0].text;
        QVERIFY(manual.text.contains(QChar(0x00A0)));
        QCOMPARE(manual.style.maxWidth, 2.5);
        QCOMPARE(TextContent::wordsOf(manual.text).size(), 6);
        QVERIFY(manual.activeHighlight());
        QVERIFY(!manual.emphasis->color);
    }

    // Estilo de texto sem campos (versões anteriores) fica com o padrão
    void textStyleDefaults()
    {
        Project p;
        TE::addText(text(QStringLiteral("A")), p, 0);
        QJsonObject o = ProjectStore::toJson(p);
        QJsonArray tracks = o["tracks"].toArray();
        QJsonObject track = tracks[1].toObject();
        QJsonArray segments = track["segments"].toArray();
        QJsonObject seg = segments[0].toObject();
        seg["text"] = QJsonObject{{"text", "A"}, {"style", QJsonObject{{"fontSize", 0.1}, {"textCase", "title"}}}};
        segments[0] = seg;
        track["segments"] = segments;
        tracks[1] = track;
        o["tracks"] = tracks;

        QString error;
        const auto back = ProjectStore::fromJson(o, &error);
        QVERIFY2(back, qPrintable(error));
        const TextStyle &style = back->tracks[1].segments[0].text->style;
        TextStyle expected;
        expected.fontSize = 0.1;
        expected.textCase = TextCase::Title;
        QVERIFY(style == expected);
        QCOMPARE(style.fontName, QStringLiteral("Helvetica Neue"));
    }

    // Projeto novo: nome, 30 fps e só a faixa principal
    void defaults()
    {
        const Project p;
        QCOMPARE(p.name, QStringLiteral("Sem título"));
        QCOMPARE(p.fps, 30.0);
        QCOMPARE(p.formatVersion, 1);
        QCOMPARE(p.tracks.size(), 1);
        QVERIFY(p.tracks[0].isMain);
        QCOMPARE(p.mainTrackIndex(), std::optional(0));
        QCOMPARE(p.duration(), 0);
        QCOMPARE(p.aspectValue(), 16.0 / 9);
        QVERIFY(!p.locate(QUuid::createUuid()));
        QCOMPARE(label(AspectRatio::R9x16), QStringLiteral("9:16"));
    }
};

QTEST_GUILESS_MAIN(ProjectTests)
#include "ProjectTests.moc"
