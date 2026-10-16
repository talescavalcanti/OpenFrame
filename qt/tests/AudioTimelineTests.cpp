// Porte da suíte "Áudio na timeline" (Tests/OpenFrameCoreTests/AudioTimelineTests.swift): extrair o áudio de um
// clipe, gravação na timeline e altura das faixas.
#include "AudioMix.h"
#include "MediaProbe.h"
#include "ProjectStore.h"
#include "TestMedia.h"
#include "TestSupport.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace of;
using namespace of::test;
namespace TE = of::TimelineEditor;

namespace {

QList<Track> audioTracks(const Project &p)
{
    QList<Track> out;
    for (const Track &t : p.tracks)
        if (t.kind == TrackKind::Audio)
            out.append(t);
    return out;
}

Material recording(const QString &name, double seconds)
{
    Material m;
    m.kind = MaterialKind::Audio;
    m.path = QStringLiteral("/tmp/") + name;
    m.name = name;
    m.duration = Time::seconds(seconds);
    m.hasAudio = true;
    return m;
}

/// Mixagem estéreo do projeto inteiro, em blocos (como o player e a exportação leem).
std::vector<float> mixAll(const Project &p)
{
    AudioMixer mixer(AudioMix::clips(p));
    std::vector<float> out(size_t(AudioMix::sampleAt(p.duration())) * AudioMix::channels);
    for (size_t done = 0; done < out.size() / 2;) {
        const int n = int(std::min<size_t>(1024, out.size() / 2 - done));
        mixer.read(out.data() + done * 2, n);
        done += size_t(n);
    }
    return out;
}

QList<std::optional<double>> heights(const Project &p)
{
    QList<std::optional<double>> out;
    for (const Track &t : p.tracks)
        out.append(t.height);
    return out;
}

} // namespace

class AudioTimelineTests : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase()
    {
        if (qEnvironmentVariableIsEmpty("OPENFRAME_KEEP_MEDIA"))
            QDir(mediaDirectory()).removeRecursively();
    }

    // Extrair cria um material de áudio no mesmo arquivo e um segmento no mesmo trecho
    void extract()
    {
        Project p;
        TE::addMedia(video(2, QStringLiteral("a.mov")), p);
        const QUuid id = TE::addMedia(video(10), p);
        TE::trimStart(p, id, Time::seconds(3));
        TE::setSpeed(p, id, 1.5);
        TE::update(p, id, [](Segment &s) {
            s.volume = 0.6;
            s.fadeIn = 200'000;
            s.fadeOut = 700'000;
        });
        const Segment clip = *p.segment(id);
        QVERIFY(TE::canExtractAudio(p, id));

        const auto audioId = TE::extractAudio(p, id);
        QVERIFY(audioId);
        QCOMPARE(p.materials.size(), 3);
        const Material m = p.materials.last();
        QCOMPARE(m.kind, MaterialKind::Audio);
        QCOMPARE(m.path, QStringLiteral("/tmp/clip.mov"));
        QCOMPARE(m.name, QStringLiteral("Áudio de clip.mov"));
        QCOMPARE(m.duration, Time::seconds(10));
        QCOMPARE(m.width, 0);
        QCOMPARE(m.height, 0);
        QVERIFY(m.hasAudio);

        const QList<Track> tracks = audioTracks(p);
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(ids(tracks[0].segments), QList<QUuid>{*audioId});
        const Segment sound = *p.segment(*audioId);
        QCOMPARE(sound.materialId, std::optional<QUuid>(m.id));
        QCOMPARE(sound.targetStart, clip.targetStart);
        QCOMPARE(sound.source, clip.source);
        QCOMPARE(sound.speed, 1.5);
        QCOMPARE(sound.volume, 0.6);
        QCOMPARE(sound.fadeIn, Micros(200'000));
        QCOMPARE(sound.fadeOut, Micros(700'000));
        QCOMPARE(sound.targetRange(), clip.targetRange());
        QVERIFY(!sound.audioDetached);

        const Segment after = *p.segment(id);
        QVERIFY(after.audioDetached);
        QCOMPARE(after.source, clip.source);
        QCOMPARE(after.targetStart, clip.targetStart);
        QVERIFY(!TE::canExtractAudio(p, id));
        QVERIFY(!TE::extractAudio(p, id));
        QCOMPARE(p.materials.size(), 3);
    }

    // Só vídeo com som pode ter o áudio extraído
    void canExtractOnlyVideoWithSound()
    {
        Project p;
        Material silent = video(3, QStringLiteral("mudo.mov"));
        silent.hasAudio = false;
        const QList<QUuid> segments{TE::addMedia(photo(), p), TE::addMedia(silent, p), TE::addMedia(audio(5), p),
                                    TE::addText(text(QStringLiteral("Olá")), p, 0), QUuid::createUuid()};
        for (const QUuid &id : segments) {
            QVERIFY(!TE::canExtractAudio(p, id));
            QVERIFY(!TE::extractAudio(p, id));
        }
        QCOMPARE(p.materials.size(), 3);
    }

    // O áudio extraído vai para uma faixa de áudio livre no trecho; se não houver, para uma nova
    void extractToFreeTrack()
    {
        Project p;
        const QUuid a = TE::addMedia(video(4, QStringLiteral("a.mov")), p);
        const QUuid b = TE::addMedia(video(4, QStringLiteral("b.mov")), p);
        // Música ocupando só o trecho do primeiro clipe.
        TE::addMedia(audio(3), p, Time::seconds(1));
        QCOMPARE(audioTracks(p).size(), 1);

        const auto soundA = TE::extractAudio(p, a);
        QVERIFY(soundA);
        QCOMPARE(audioTracks(p).size(), 2);
        QCOMPARE(p.locate(*soundA)->track, p.tracks.size() - 1);

        // O segundo clipe (4–8 s) cabe na primeira faixa de áudio, que só está ocupada até 4 s.
        const auto soundB = TE::extractAudio(p, b);
        QVERIFY(soundB);
        QCOMPARE(audioTracks(p).size(), 2);
        QVERIFY(ids(audioTracks(p)[0].segments).contains(*soundB));
    }

    // Extrair as duas metades de um vídeo reaproveita o material de áudio
    void extractReusesMaterial()
    {
        Project p;
        const QUuid id = TE::addMedia(video(6), p);
        const auto right = TE::split(p, id, Time::seconds(2));
        QVERIFY(right);
        const auto left = TE::extractAudio(p, id);
        const auto rightSound = TE::extractAudio(p, *right);
        QVERIFY(left && rightSound);
        QCOMPARE(p.materials.size(), 2);
        QCOMPARE(p.segment(*left)->materialId, p.segment(*rightSound)->materialId);
        QCOMPARE(p.segment(*rightSound)->source, (TimeRange{Time::seconds(2), Time::seconds(4)}));
    }

    // O cruzamento de uma transição vira fade no áudio extraído
    void extractKeepsTransitionCrossfade()
    {
        Project p;
        const QUuid a = TE::addMedia(video(4, QStringLiteral("a.mov")), p);
        const QUuid b = TE::addMedia(video(4, QStringLiteral("b.mov")), p);
        TE::update(p, a, [](Segment &s) {
            s.transition = Transition{TransitionKind::Dissolve, 600'000};
            s.fadeOut = 100'000;
        });
        TE::update(p, b, [](Segment &s) { s.fadeIn = 900'000; });
        const auto soundA = TE::extractAudio(p, a);
        const auto soundB = TE::extractAudio(p, b);
        QVERIFY(soundA && soundB);
        QCOMPARE(p.segment(*soundA)->fadeOut, Micros(600'000));
        QCOMPARE(p.segment(*soundA)->fadeIn, Micros(0));
        QCOMPARE(p.segment(*soundB)->fadeIn, Micros(900'000));
        QCOMPARE(p.segment(*soundB)->targetStart, p.segment(b)->targetStart);
    }

    // Dividir, duplicar, aparar e mover mantêm o som extraído; apagar o áudio não devolve o som
    void editsKeepDetached()
    {
        Project p;
        const QUuid id = TE::addMedia(video(8), p);
        const auto sound = TE::extractAudio(p, id);
        QVERIFY(sound);
        const auto right = TE::split(p, id, Time::seconds(3));
        QVERIFY(right);
        const auto copy = TE::duplicate(p, *right);
        QVERIFY(copy);
        TE::trimStart(p, id, Time::seconds(1));
        TE::trimEnd(p, *copy, p.segment(*copy)->targetEnd() - Time::seconds(1));
        for (const Segment &seg : mainSegments(p))
            QVERIFY(seg.audioDetached);

        const QUuid overlay = TE::addOverlay(video(2, QStringLiteral("pip.mov")), p, 0);
        QVERIFY(TE::extractAudio(p, overlay));
        TE::move(p, overlay, Time::seconds(4));
        TE::moveToTrack(p, overlay, *p.mainTrackIndex());
        QCOMPARE(p.locate(overlay)->track, *p.mainTrackIndex());
        QVERIFY(p.segment(overlay)->audioDetached);

        // O áudio extraído é um clipe de áudio comum.
        TE::trimEnd(p, *sound, Time::seconds(2));
        QCOMPARE(p.segment(*sound)->targetEnd(), Time::seconds(2));
        TE::remove(p, *sound);
        QVERIFY(p.segment(id)->audioDetached);
        QVERIFY(!TE::canExtractAudio(p, id));
    }

    // Gravação entra numa faixa de áudio livre no trecho
    void addRecording()
    {
        Project p;
        TE::addMedia(video(10), p);
        const Material take = recording(QStringLiteral("Gravação 1.m4a"), 3);
        const QUuid first = TE::addRecording(take, p, Time::seconds(2));
        QCOMPARE(p.materials.size(), 2);
        QCOMPARE(p.segment(first)->targetRange(), (TimeRange{Time::seconds(2), Time::seconds(3)}));
        QCOMPARE(audioTracks(p).size(), 1);

        // Sobre a primeira: faixa nova. Depois dela: a primeira serve.
        const Material second = recording(QStringLiteral("Gravação 2.m4a"), 2);
        const QUuid over = TE::addRecording(second, p, Time::seconds(4));
        QCOMPARE(audioTracks(p).size(), 2);
        const QUuid later = TE::addRecording(second, p, Time::seconds(6));
        QCOMPARE(audioTracks(p).size(), 2);
        QCOMPARE(p.locate(later)->track, p.locate(first)->track);
        QVERIFY(p.locate(over)->track != p.locate(first)->track);
        QCOMPARE(p.materials.size(), 3);
        // Tempo negativo vai para o início.
        const QUuid start = TE::addRecording(take, p, -5);
        QCOMPARE(p.segment(start)->targetStart, Micros(0));
    }

    // Altura das faixas: limitada e vazio volta à padrão
    void trackHeight()
    {
        Project p;
        TE::addText(text(QStringLiteral("Olá")), p, 0);
        const QUuid main = p.tracks[0].id;
        QCOMPARE(p.tracks[0].height, std::nullopt);
        TE::setTrackHeight(p, main, 90);
        QCOMPARE(p.tracks[0].height, std::optional<double>(90));
        TE::setTrackHeight(p, main, 10);
        QCOMPARE(p.tracks[0].height, std::optional<double>(Track::heightMin));
        TE::setTrackHeight(p, main, 1000);
        QCOMPARE(p.tracks[0].height, std::optional<double>(Track::heightMax));
        TE::setTrackHeight(p, main, std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(p.tracks[0].height, std::nullopt);
        TE::setTrackHeight(p, main, 140);
        TE::setTrackHeight(p, main, std::nullopt);
        QCOMPARE(p.tracks[0].height, std::nullopt);
        QCOMPARE(p.tracks[1].height, std::nullopt);
        TE::setTrackHeight(p, QUuid::createUuid(), 80);

        using H = QList<std::optional<double>>;
        TE::setAllTrackHeights(p, 36);
        QCOMPARE(heights(p), (H{36.0, 36.0}));
        TE::setAllTrackHeights(p, 500);
        QCOMPARE(heights(p), (H{200.0, 200.0}));
        TE::setAllTrackHeights(p, std::nullopt);
        QCOMPARE(heights(p), (H{std::nullopt, std::nullopt}));
        QCOMPARE(Track::defaultHeight, 50.0);
    }

    // JSON: projeto antigo abre igual e os campos novos só são gravados fora do padrão
    void json()
    {
        Project p;
        const QUuid id = TE::addMedia(video(4), p);
        const QByteArray plain = ProjectStore::encode(p);
        QVERIFY(!plain.contains("audioDetached"));
        for (const QJsonValue &t : QJsonDocument::fromJson(plain).object()["tracks"].toArray())
            QVERIFY(!t.toObject().contains(QStringLiteral("height")));
        QCOMPARE(*ProjectStore::decode(plain), p);

        TE::extractAudio(p, id);
        TE::setTrackHeight(p, p.tracks[0].id, 120);
        const QByteArray data = ProjectStore::encode(p);
        const auto back = ProjectStore::decode(data);
        QVERIFY(back);
        QCOMPARE(*back, p);
        QVERIFY(back->segment(id)->audioDetached);
        QCOMPARE(back->tracks[0].height, std::optional<double>(120));
        QCOMPARE(back->tracks[1].height, std::nullopt);

        // Altura fora da faixa (outra versão, arquivo editado à mão) é limitada na leitura; null = ausente.
        QJsonObject object = QJsonDocument::fromJson(data).object();
        QJsonArray list = object["tracks"].toArray();
        QJsonObject t0 = list[0].toObject(), t1 = list[1].toObject();
        t0["height"] = 5;
        t1["height"] = 900.5;
        list[0] = t0;
        list[1] = t1;
        object["tracks"] = list;
        const auto edited = ProjectStore::decode(QJsonDocument(object).toJson());
        QVERIFY(edited);
        QCOMPARE(heights(*edited), (QList<std::optional<double>>{32.0, 200.0}));
        t0["height"] = QJsonValue::Null;
        list[0] = t0;
        object["tracks"] = list;
        QCOMPARE(ProjectStore::decode(QJsonDocument(object).toJson())->tracks[0].height, std::nullopt);
        // O campo novo continua com o tipo certo: texto no lugar do número é erro, como no Swift.
        t0["height"] = QStringLiteral("alta");
        list[0] = t0;
        object["tracks"] = list;
        QString error;
        QVERIFY(!ProjectStore::decode(QJsonDocument(object).toJson(), &error));
        QVERIFY2(error.contains(QStringLiteral("height")), qPrintable(error));
    }

    // O fixture do Swift chega com os valores certos (a ida e volta está no ProjectTests)
    void swiftFixture()
    {
        QFile file(QStringLiteral(OPENFRAME_FIXTURES "/audio.openframe"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const auto p = ProjectStore::decode(file.readAll(), &error);
        QVERIFY2(p, qPrintable(error));
        QCOMPARE(heights(*p), (QList<std::optional<double>>{90.0, 36.0, std::nullopt}));
        const QList<Segment> &main = mainSegments(*p);
        QCOMPARE(main.size(), 2);
        QVERIFY(main[0].audioDetached);
        QVERIFY(!main[1].audioDetached);
        const Segment &sound = p->tracks[1].segments[0];
        const Material *m = p->material(sound.materialId);
        QVERIFY(m);
        QCOMPARE(m->kind, MaterialKind::Audio);
        QCOMPARE(m->name, QStringLiteral("Áudio de clipe.mov"));
        QCOMPARE(m->path, p->material(main[0].materialId)->path);
        QCOMPARE(sound.targetRange(), main[0].targetRange());
        QCOMPARE(sound.speed, 1.5);
        QCOMPARE(sound.volume, 0.8);
        QCOMPARE(sound.fadeIn, Micros(200'000));
        QCOMPARE(sound.fadeOut, Micros(400'000)); // o cruzamento da transição
        QCOMPARE(p->material(p->tracks[2].segments[0].materialId)->name, QStringLiteral("Gravação 1.m4a"));

        // O que soa: o áudio extraído e a gravação, não o vídeo destacado; o outro vídeo, sim.
        QList<QUuid> sounding;
        for (const AudioClip &c : AudioMix::clips(*p))
            sounding.append(c.segment);
        QCOMPARE(sounding.size(), 3);
        QVERIFY(!sounding.contains(main[0].id));
        QVERIFY(sounding.contains(main[1].id));
        QVERIFY(sounding.contains(sound.id));
    }

    // O som não muda ao extrair (velocidade, volume, fades e transição)
    void soundUnchanged()
    {
        VideoOptions o;
        o.seconds = 3;
        QString error;
        const auto clip = MediaProbe::probe(makeVideo(o), &error);
        QVERIFY2(clip, qPrintable(error));
        Project p;
        const QUuid a = TE::addMedia(*clip, p);
        const QUuid b = TE::addMedia(*clip, p);
        TE::update(p, a, [](Segment &s) {
            s.fadeIn = 300'000;
            s.volume = 0.7;
            s.transition = Transition{TransitionKind::Dissolve, 500'000};
        });
        TE::setSpeed(p, b, 2);
        TE::update(p, b, [](Segment &s) { s.fadeOut = 400'000; });

        const std::vector<float> before = mixAll(p);
        Project q = p;
        const auto soundA = TE::extractAudio(q, a);
        const auto soundB = TE::extractAudio(q, b);
        QVERIFY(soundA && soundB);
        QCOMPARE(q.duration(), p.duration());
        const std::vector<float> after = mixAll(q);
        QCOMPARE(after.size(), before.size());
        double worst = 0, energy = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            worst = std::max(worst, double(std::abs(before[i] - after[i])));
            energy += double(before[i]) * before[i];
        }
        QVERIFY(std::sqrt(energy / double(before.size())) > 0.05);
        QVERIFY2(worst < 1e-5, qPrintable(QString::number(worst)));

        // Sem o áudio extraído, o vídeo destacado não soa.
        TE::remove(q, *soundA);
        TE::remove(q, *soundB);
        QVERIFY(AudioMix::clips(q).isEmpty());
    }
};

QTEST_GUILESS_MAIN(AudioTimelineTests)
#include "AudioTimelineTests.moc"
