#include "FontCatalog.h"

#include <QDir>
#include <QFontDatabase>
#include <QSet>
#include <algorithm>
#include <mutex>

namespace of::FontCatalog {

void registerBundled()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const QDir dir(QStringLiteral(":/openframe/fonts"));
        for (const QString &file : dir.entryList({QStringLiteral("*.ttf")}, QDir::Files, QDir::Name))
            QFontDatabase::addApplicationFont(dir.filePath(file));
    });
}

ResolvedFont resolve(const QString &family, const QString &face)
{
    registerBundled();
    // Testes: o Mac age como o Windows e o Linux, onde as fontes do Mac não existem.
    static const bool bundledOnly = qEnvironmentVariableIntValue("OPENFRAME_BUNDLED_FONTS_ONLY") == 1;
    static const QSet<QString> bundled{"inter", "jetbrains mono", "playfair display", "barlow condensed"};
    if (QFontDatabase::hasFamily(family) && (!bundledOnly || bundled.contains(family.toLower())))
        return {family, face};
    const QString inter = QStringLiteral("Inter");
    const QString name = family.toLower();
    const QString style = face.toLower();
    static const QSet<QString> sans{"helvetica neue", "helvetica", "avenir next", "avenir", "futura", "gill sans",
                                    "sf pro", "sf pro display", "sf pro text", "arial rounded mt bold"};
    static const QSet<QString> mono{"menlo", "monaco", "sf mono", "courier", "courier new", "american typewriter"};
    static const QSet<QString> serif{"didot", "bodoni 72", "bodoni 72 oldstyle", "baskerville"};
    if (sans.contains(name) && style.startsWith(QStringLiteral("condensed")))
        return {QStringLiteral("Barlow Condensed"), style.contains(QStringLiteral("black")) ? QStringLiteral("Black")
                                                                                              : QStringLiteral("Bold")};
    if (mono.contains(name) && QFontDatabase::hasFamily(QStringLiteral("JetBrains Mono")))
        return {QStringLiteral("JetBrains Mono"), face};
    if (serif.contains(name) && QFontDatabase::hasFamily(QStringLiteral("Playfair Display")))
        return {QStringLiteral("Playfair Display"), face};
    if (QFontDatabase::hasFamily(inter))
        return {inter, face};
    return {QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(), face};
}

QStringList families()
{
    registerBundled();
    QStringList names;
    for (const QString &family : QFontDatabase::families())
        if (!family.startsWith(u'.') && !QFontDatabase::isPrivateFamily(family))
            names.append(family);
    // Nomes iguais sem diferenciar maiúsculas ficam na ordem do QFontDatabase.
    std::stable_sort(names.begin(), names.end(),
                     [](const QString &a, const QString &b) { return naturalCompare(a, b) < 0; });
    names.removeDuplicates();
    return names;
}

int naturalCompare(const QString &a, const QString &b)
{
    // Os nomes viram sequências de "peças": um número (dígitos ASCII seguidos,
    // pelo valor) ou um caractere sem maiúscula. Um número fica onde ficaria o
    // "0"; como nenhum caractere avulso é dígito, a ordem das peças é total, e a
    // comparação peça a peça é uma ordem fraca estrita.
    const auto isDigit = [](QChar c) { return c >= u'0' && c <= u'9'; };
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = isDigit(a[i]), db = isDigit(b[j]);
        if (da && db) {
            const qsizetype si = i, sj = j;
            while (i < a.size() && isDigit(a[i]))
                ++i;
            while (j < b.size() && isDigit(b[j]))
                ++j;
            // Valor: sem os zeros à esquerda, mais dígitos = maior; senão, dígito a dígito.
            QStringView na = QStringView(a).sliced(si, i - si), nb = QStringView(b).sliced(sj, j - sj);
            while (na.size() > 1 && na.front() == u'0')
                na = na.sliced(1);
            while (nb.size() > 1 && nb.front() == u'0')
                nb = nb.sliced(1);
            if (na.size() != nb.size())
                return na.size() < nb.size() ? -1 : 1;
            if (const int c = na.compare(nb); c != 0)
                return c < 0 ? -1 : 1;
            continue;
        }
        const char16_t ca = da ? u'0' : a[i].toCaseFolded().unicode();
        const char16_t cb = db ? u'0' : b[j].toCaseFolded().unicode();
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    const bool endA = i >= a.size(), endB = j >= b.size();
    return endA && endB ? 0 : endA ? -1 : 1;
}

QStringList faces(const QString &family)
{
    struct Face {
        QString name;
        int weight;
        bool italic;
    };
    QList<Face> faces;
    QSet<QString> seen;
    for (const QString &style : QFontDatabase::styles(family)) {
        if (seen.contains(style))
            continue;
        seen.insert(style);
        faces.append({style, QFontDatabase::weight(family, style), QFontDatabase::italic(family, style)});
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) {
        return std::pair(a.italic ? 1 : 0, a.weight) < std::pair(b.italic ? 1 : 0, b.weight);
    });
    QStringList names;
    for (const auto &f : faces)
        names.append(f.name);
    return names;
}

} // namespace of::FontCatalog
