#pragma once

#include "Project.h"
#include "TimelineEditor.h"

#include <QTest>

// Materiais falsos para os testes de modelo (os arquivos não precisam existir).
namespace of::test {

inline Material video(double seconds, const QString &name = QStringLiteral("clip.mov"))
{
    Material m;
    m.kind = MaterialKind::Video;
    m.path = QStringLiteral("/tmp/") + name;
    m.name = name;
    m.duration = Time::seconds(seconds);
    m.width = 1920;
    m.height = 1080;
    m.hasAudio = true;
    return m;
}

inline Material audio(double seconds)
{
    Material m;
    m.kind = MaterialKind::Audio;
    m.path = QStringLiteral("/tmp/song.m4a");
    m.name = QStringLiteral("song.m4a");
    m.duration = Time::seconds(seconds);
    m.hasAudio = true;
    return m;
}

inline Material photo(int width = 800, int height = 600, const QString &name = QStringLiteral("a.png"))
{
    Material m;
    m.kind = MaterialKind::Photo;
    m.path = QStringLiteral("/tmp/") + name;
    m.name = name;
    m.width = width;
    m.height = height;
    return m;
}

inline TextContent text(const QString &s) { return TextContent{s, {}}; }

/// Limite da mediana por quadro nos testes de desempenho, em ms. Com otimização, 150 (o Swift usa 100; a folga é
/// para os runners da CI e os testes em paralelo); sem otimização, a medida não diz nada e só pega o patológico.
constexpr double frameBudgetMs()
{
#ifdef NDEBUG
    return 150;
#else
    return 2000;
#endif
}

inline const QList<Segment> &mainSegments(const Project &p) { return p.tracks[*p.mainTrackIndex()].segments; }

inline QList<QUuid> ids(const QList<Segment> &segments)
{
    QList<QUuid> out;
    for (const auto &s : segments)
        out.append(s.id);
    return out;
}

} // namespace of::test
