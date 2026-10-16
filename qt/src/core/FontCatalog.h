#pragma once

#include <QStringList>

namespace of {

/// Famílias e estilos de fonte instalados, para os menus do inspetor.
/// Usa o `QFontDatabase`: precisa de um `QGuiApplication` criado.
namespace FontCatalog {
/// Famílias visíveis (sem as privadas do sistema e as que começam com "."),
/// em ordem alfabética natural ("Font 2" antes de "Font 10").
QStringList families();
/// Estilos de uma família ("Light", "Regular", "Bold Italic"…), do mais fino ao
/// mais grosso, os itálicos depois.
QStringList faces(const QString &family);
/// Ordem natural dos nomes, como o `localizedStandardCompare` do Swift: sem
/// diferenciar maiúsculas e com os números pelo valor ("Font 2" < "Font 10").
/// Negativo, zero ou positivo; é uma ordem fraca estrita em qualquer plataforma
/// (o QCollator do Windows pode falhar e responder "menor" nos dois sentidos).
int naturalCompare(const QString &a, const QString &b);

/// Registra as fontes livres (OFL) que o núcleo distribui (recurso `:/openframe/fonts`): Inter, JetBrains Mono,
/// Playfair Display Italic e Barlow Condensed. Pode ser chamada várias vezes; o `TextRenderer` e `families()`
/// chamam sozinhos.
void registerBundled();

/// Família e estilo usados para uma fonte pedida. Se a família existir, ela mesma. Senão (projeto vindo do Mac),
/// a equivalente distribuída: Helvetica Neue, Avenir Next, Futura, Gill Sans e SF Pro → Inter (o estilo
/// "Condensed Black"/"Condensed Bold" → Barlow Condensed Black/Bold); Menlo, Monaco, SF Mono, Courier, Courier
/// New e American Typewriter → JetBrains Mono; Didot, Bodoni 72 e Baskerville → Playfair Display. Qualquer outra
/// ausente → Inter (ou a fonte do sistema, se nem ela existir). Com `OPENFRAME_BUNDLED_FONTS_ONLY=1` (testes), só as
/// distribuídas contam como existentes: o Mac usa as mesmas fontes do Windows e do Linux.
struct ResolvedFont {
    QString family;
    QString face;
};
ResolvedFont resolve(const QString &family, const QString &face);
} // namespace FontCatalog

} // namespace of
