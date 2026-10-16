// Comparação do render do motion e das legendas com a referência medida no núcleo Swift
// (fixtures/motion.json, gerado por tools/gerar-motion.sh): as 17 animações, os 4 loops (em texto e em foto), as
// revelações, o destaque, a ênfase, o fitLines e as 10 predefinições, em alguns instantes de cada.
#include "ProjectStore.h"
#include "Renderer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace of;

namespace {

struct Measure {
    int ink = 0;
    std::optional<QRect> box; // x0, y0, x1, y1 inclusivos em QRect(QPoint, QPoint)
    std::optional<QPointF> centroid;
    double mean = 0;
    QList<std::array<double, 3>> cells;
};

struct Reference {
    QSize size;
    int columns = 8, rows = 4;
    QJsonArray cases;
};

const Reference &reference()
{
    static const Reference r = [] {
        QFile file(QStringLiteral(OPENFRAME_FIXTURES "/motion.json"));
        if (!file.open(QIODevice::ReadOnly))
            qFatal("motion.json ausente");
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        Reference r;
        r.size = QSize(o["size"][0].toInt(), o["size"][1].toInt());
        r.columns = o["grid"][0].toInt();
        r.rows = o["grid"][1].toInt();
        r.cases = o["cases"].toArray();
        return r;
    }();
    return r;
}

Measure fromJson(const QJsonObject &o)
{
    Measure m;
    m.ink = o["ink"].toInt();
    if (o.contains("box")) {
        const QJsonArray b = o["box"].toArray();
        m.box = QRect(QPoint(b[0].toInt(), b[1].toInt()), QPoint(b[2].toInt(), b[3].toInt()));
    }
    if (o.contains("centroid"))
        m.centroid = QPointF(o["centroid"][0].toDouble(), o["centroid"][1].toDouble());
    m.mean = o["mean"].toDouble();
    for (const auto &c : o["cells"].toArray())
        m.cells.append({c[0].toDouble(), c[1].toDouble(), c[2].toDouble()});
    return m;
}

/// As mesmas medidas do gerador: v = max(r, g, b) em sRGB 8 bits; tinta = v > 64.
Measure measure(const QImage &frame, int columns, int rows)
{
    const QImage img = frame.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int w = img.width(), h = img.height();
    Measure m;
    int minX = w, minY = h, maxX = -1, maxY = -1;
    double sum = 0, sx = 0, sy = 0;
    m.cells.fill({0, 0, 0}, columns * rows);
    for (int y = 0; y < h; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int r = line[x * 4], g = line[x * 4 + 1], b = line[x * 4 + 2];
            const int v = std::max({r, g, b});
            if (v > 64) {
                ++m.ink;
                minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
            }
            sum += v, sx += v * (x + 0.5), sy += v * (y + 0.5);
            auto &cell = m.cells[(y * rows / h) * columns + x * columns / w];
            cell[0] += r, cell[1] += g, cell[2] += b;
        }
    }
    const double cellArea = double(w * h / (columns * rows));
    for (auto &c : m.cells)
        for (double &v : c)
            v /= cellArea;
    m.mean = sum / (w * h);
    if (m.ink > 0)
        m.box = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    if (sum > 0)
        m.centroid = QPointF(sx / sum, sy / sum);
    return m;
}

/// Foto do gerador: 200×200, metade de cima laranja e de baixo azul.
QImage testPhoto()
{
    QImage img(200, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
            img.setPixel(x, y, y < 100 ? qRgb(230, 120, 40) : qRgb(40, 90, 200));
    return img;
}

struct Errors {
    int box = 0;
    double centroid = 0;
    double mean = 0;
    double ink = 0; // relativa
    double cell = 0;
    QString worst;
};

/// Tolerâncias, em pixels de um quadro 480×270 e em níveis de 8 bits. Medido no Mac (15/10/2026): caixa ≤2 px,
/// centroide ≤1,3 px, brilho médio ≤0,42, célula ≤7,6 e tinta ≤20%. A tinta é a medida mais sensível à
/// rasterização das letras: o Core Text engrossa um pouco o traço ao suavizar, e em letras pequenas (legendas) ou
/// quase sumindo (giro) 1 px de borda muda a contagem em ~15%. A folga é para o FreeType e o DirectWrite da CI.
struct Tolerance {
    int box = 3;
    double centroid = 2;
    double mean = 1.5;
    double ink = 0.25;
    double cell = 12;
};

} // namespace

class MotionReferenceTests : public QObject
{
    Q_OBJECT

private slots:
    void matchesSwift_data()
    {
        QTest::addColumn<int>("index");
        const QJsonArray &cases = reference().cases;
        for (int i = 0; i < cases.size(); ++i)
            QTest::newRow(qPrintable(cases[i].toObject()["name"].toString())) << i;
    }

    void matchesSwift()
    {
        QFETCH(int, index);
        const Reference &ref = reference();
        const QJsonObject c = ref.cases[index].toObject();
        QString error;
        const auto project = ProjectStore::fromJson(c["project"].toObject(), &error);
        QVERIFY2(project, qPrintable(error));
        RenderOptions options;
        if (c["photo"].toBool())
            for (const Material &m : project->materials)
                if (m.kind == MaterialKind::Photo)
                    options.stills.insert(m.id, testPhoto());
        const Tolerance tol;
        Errors e;
        QStringList failures;
        for (const auto &f : c["frames"].toArray()) {
            const QJsonObject frame = f.toObject();
            const Micros t = Micros(frame["t"].toDouble());
            const Measure want = fromJson(frame);
            const QImage image = Renderer::shared().render(*project, t, ref.size, options);
            // OPENFRAME_MOTION_DUMP=<pasta>: grava o quadro (<caso>-<ms>.png), como o gerador.
            if (const QString dump = qEnvironmentVariable("OPENFRAME_MOTION_DUMP"); !dump.isEmpty())
                image.save(QStringLiteral("%1/%2-%3.png").arg(dump, c["name"].toString()).arg(t / 1000));
            const Measure got = measure(image, ref.columns, ref.rows);
            const QString at = QStringLiteral("t=%1").arg(t / 1000);
            // Tinta: com pouca tinta (quase invisível), só a contagem vale.
            const bool small = want.ink < 150 && got.ink < 150;
            if (!small) {
                if (want.box.has_value() != got.box.has_value()) {
                    failures << at + QStringLiteral(": caixa ausente de um lado");
                } else if (want.box) {
                    const int d = std::max({std::abs(want.box->left() - got.box->left()),
                                            std::abs(want.box->top() - got.box->top()),
                                            std::abs(want.box->right() - got.box->right()),
                                            std::abs(want.box->bottom() - got.box->bottom())});
                    e.box = std::max(e.box, d);
                    if (d > tol.box)
                        failures << at + QStringLiteral(": caixa (%1,%2,%3,%4) × Swift (%5,%6,%7,%8)")
                                             .arg(got.box->left()).arg(got.box->top()).arg(got.box->right()).arg(got.box->bottom())
                                             .arg(want.box->left()).arg(want.box->top()).arg(want.box->right()).arg(want.box->bottom());
                }
                const double inkError = std::abs(got.ink - want.ink) / double(std::max(want.ink, 1));
                e.ink = std::max(e.ink, inkError);
                if (inkError > tol.ink)
                    failures << at + QStringLiteral(": tinta %1 × Swift %2").arg(got.ink).arg(want.ink);
            } else if (std::abs(got.ink - want.ink) > 60) {
                failures << at + QStringLiteral(": tinta %1 × Swift %2").arg(got.ink).arg(want.ink);
            }
            if (want.centroid && got.centroid && want.mean > 1) {
                const QPointF d = *want.centroid - *got.centroid;
                const double dist = std::hypot(d.x(), d.y());
                e.centroid = std::max(e.centroid, dist);
                if (dist > tol.centroid)
                    failures << at + QStringLiteral(": centroide (%1, %2) × Swift (%3, %4)")
                                         .arg(got.centroid->x(), 0, 'f', 1).arg(got.centroid->y(), 0, 'f', 1)
                                         .arg(want.centroid->x(), 0, 'f', 1).arg(want.centroid->y(), 0, 'f', 1);
            }
            const double meanError = std::abs(got.mean - want.mean);
            e.mean = std::max(e.mean, meanError);
            if (meanError > tol.mean)
                failures << at + QStringLiteral(": brilho médio %1 × Swift %2").arg(got.mean, 0, 'f', 2).arg(want.mean, 0, 'f', 2);
            for (int i = 0; i < want.cells.size(); ++i)
                for (int k = 0; k < 3; ++k) {
                    const double d = std::abs(got.cells[i][k] - want.cells[i][k]);
                    if (d > e.cell) {
                        e.cell = d;
                        e.worst = at + QStringLiteral(" célula %1 canal %2: %3 × Swift %4")
                                           .arg(i).arg(k).arg(got.cells[i][k], 0, 'f', 1).arg(want.cells[i][k], 0, 'f', 1);
                    }
                }
        }
        if (e.cell > tol.cell)
            failures << e.worst;
        if (qEnvironmentVariableIsSet("OPENFRAME_MOTION_REPORT"))
            qInfo().noquote() << QStringLiteral("%1: caixa %2 px, centroide %3 px, brilho %4, tinta %5%, célula %6 (%7)")
                                     .arg(c["name"].toString(), -28).arg(e.box).arg(e.centroid, 0, 'f', 1)
                                     .arg(e.mean, 0, 'f', 2).arg(e.ink * 100, 0, 'f', 1).arg(e.cell, 0, 'f', 1).arg(e.worst);
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
    }
};

QTEST_MAIN(MotionReferenceTests)
#include "MotionReferenceTests.moc"
