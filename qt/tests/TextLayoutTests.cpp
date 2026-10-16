// A caixa do texto (`TextRenderer::boxSize`) e as linhas batem com o núcleo Swift: a seleção, as alças e as
// guias do preview dependem dela. A referência (fixtures/caixas.json) é medida no Mac por
// tools/gerar-caixas.sh, com as mesmas fontes Inter do app; aqui elas são registradas a partir de src/app/fonts.
#include "ProjectStore.h"
#include "TestSupport.h"
#include "TextRenderer.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>

using namespace of;
namespace TE = of::TimelineEditor;

namespace {

/// Lê o conteúdo de texto do JSON do Swift pelo próprio ProjectStore (num projeto com um texto).
std::optional<TextContent> decodeContent(const QJsonObject &content)
{
    Project p;
    TE::addText(TextContent(QStringLiteral("x")), p, 0);
    QJsonObject o = ProjectStore::toJson(p);
    QJsonArray tracks = o["tracks"].toArray();
    QJsonObject track = tracks[1].toObject();
    QJsonArray segments = track["segments"].toArray();
    QJsonObject seg = segments[0].toObject();
    seg["text"] = content;
    segments[0] = seg;
    track["segments"] = segments;
    tracks[1] = track;
    o["tracks"] = tracks;
    const auto back = ProjectStore::fromJson(o);
    if (!back)
        return std::nullopt;
    return back->tracks[1].segments[0].text;
}

} // namespace

class TextLayoutTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QDir fonts(QStringLiteral(OPENFRAME_FIXTURES "/../../src/app/fonts"));
        for (const QString &file : fonts.entryList({QStringLiteral("*.ttf")}))
            QVERIFY(QFontDatabase::addApplicationFont(fonts.filePath(file)) >= 0);
        QVERIFY(QFontDatabase::hasFamily(QStringLiteral("Inter")));
    }

    void matchesSwift_data()
    {
        QTest::addColumn<QJsonObject>("item");
        QFile file(QStringLiteral(OPENFRAME_FIXTURES "/caixas.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (const QJsonValue v : QJsonDocument::fromJson(file.readAll())["cases"].toArray())
            QTest::newRow(qPrintable(v["name"].toString())) << v.toObject();
    }

    // Caixa a até 3 px (num quadro de 1080) e as mesmas linhas
    void matchesSwift()
    {
        QFETCH(QJsonObject, item);
        const auto content = decodeContent(item["content"].toObject());
        QVERIFY(content);
        const QJsonArray c = item["canvas"].toArray();
        const QSize canvas(c[0].toInt(), c[1].toInt());
        const QSizeF box = *TextRenderer::shared().boxSize(*content, canvas);
        const QJsonArray expected = item["box"].toArray();
        const double dw = box.width() - expected[0].toDouble(), dh = box.height() - expected[1].toDouble();
        QStringList lines;
        for (const QJsonValue l : item["lines"].toArray())
            lines.append(l.toString());
        QCOMPARE(TextRenderer::shared().lineTexts(*content, canvas), lines);
        QVERIFY2(std::abs(dw) <= 3 && std::abs(dh) <= 3,
                 qPrintable(QStringLiteral("Qt %1×%2, Swift %3×%4")
                                .arg(box.width()).arg(box.height())
                                .arg(expected[0].toDouble()).arg(expected[1].toDouble())));
    }
};

QTEST_MAIN(TextLayoutTests)
#include "TextLayoutTests.moc"
