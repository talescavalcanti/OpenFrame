#include "TextMotion.h"

#include "TextRenderer.h"
#include "TextShapes.h"

#include <QCache>
#include <QHash>
#include <QMutex>
#include <QPainter>
#include <cmath>
#include <numeric>

namespace of {

using namespace textshapes;

namespace {

// MARK: - Sprites

/// Desenha as formas na caixa `box` (coordenadas da caixa de texto) e devolve o sprite já na posição da imagem
/// do texto (`pad` = as margens).
TextSprite sprite(const Shapes &shapes, const QRect &box, QPoint pad, const TextStyle &style, const Layout &l,
                  const Metrics &m, bool stroke)
{
    if (box.isEmpty())
        return {};
    QImage img(box.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.translate(-box.x(), -box.y());
        drawShapes(p, shapes, style, l, m, stroke, !stroke);
    }
    return {LinearImage::fromImageEncoded(img), box.topLeft() + pad};
}

QRect outsetBox(const QRectF &bounds, double outset)
{
    const QRectF r = bounds.adjusted(-outset, -outset, outset, outset);
    return QRect(QPoint(int(std::floor(r.left())), int(std::floor(r.top()))),
                 QPoint(int(std::ceil(r.right())) - 1, int(std::ceil(r.bottom())) - 1));
}

std::shared_ptr<TextUnits> buildUnits(const QString &text, const TextContent &content, QSize canvas,
                                      TextGranularity granularity)
{
    const TextStyle &style = content.style;
    const Layout l = buildLayout(text, style, canvas, content.emphasis);
    const Metrics m = metrics(l, style);
    const auto [glyphs, lines] = glyphInfo(l, text, style);

    // Caractere (grafema) e palavra de cada posição UTF-16.
    const QList<int> ends = graphemeEnds(text);
    QList<int> characterAt(text.size() + 1, 0);
    QList<int> wordOf;
    int word = -1;
    bool previousBlank = true;
    for (int c = 0, start = 0; c < ends.size(); start = ends[c], ++c) {
        for (int u = start; u < ends[c]; ++u)
            characterAt[u] = c;
        const bool blank = text[start].isSpace();
        if (!blank && previousBlank)
            ++word;
        wordOf.append(blank ? -1 : word);
        previousBlank = blank;
    }
    const auto unitKey = [&](const Glyph &g) {
        switch (granularity) {
        case TextGranularity::Letter: return characterAt[g.utf16];
        case TextGranularity::Word: return wordOf[characterAt[g.utf16]];
        case TextGranularity::Line: return g.line;
        }
        return 0;
    };

    // Glifos de cada unidade, em ordem de texto.
    QMap<int, QList<const Glyph *>> members;
    for (const Glyph &g : glyphs)
        if (!g.blank)
            members[unitKey(g)].append(&g);

    // Extensão de cada linha e sublinhado: cada unidade leva o trecho até a próxima da mesma linha.
    QList<double> lineMin(lines.size(), INFINITY), lineMax(lines.size(), -INFINITY);
    for (const Glyph &g : glyphs)
        if (!g.blank) {
            lineMin[g.line] = std::min(lineMin[g.line], g.position.x());
            lineMax[g.line] = std::max(lineMax[g.line], g.position.x() + g.advance);
        }
    QHash<int, QList<QRectF>> underlines;
    if (style.underline)
        for (int line = 0; line < lines.size(); ++line) {
            if (!(lineMax[line] > lineMin[line]))
                continue;
            QList<std::pair<int, double>> starts;
            for (auto it = members.cbegin(); it != members.cend(); ++it) {
                double x = INFINITY;
                for (const Glyph *g : *it)
                    if (g->line == line)
                        x = std::min(x, g->position.x());
                if (std::isfinite(x))
                    starts.append({it.key(), x});
            }
            std::sort(starts.begin(), starts.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
            // Emendas em pixel inteiro (a margem é inteira): sem fresta de antisserrilhado entre os trechos.
            for (int j = 0; j < starts.size(); ++j) {
                const double x = j == 0 ? starts[j].second : std::round(starts[j].second);
                const double end = j + 1 < starts.size() ? std::round(starts[j + 1].second) : lineMax[line];
                if (end > x)
                    underlines[starts[j].first].append(underlineRect(lines[line], x, end, l.fontPx));
            }
        }

    auto set = std::make_shared<TextUnits>();
    const QPoint pad(int(m.padX), int(m.padY));
    const double outset = m.strokeOut + (l.face.syntheticBold ? syntheticBold * l.fontPx : 0) + 2;
    for (auto it = members.cbegin(); it != members.cend(); ++it) {
        const QList<const Glyph *> &group = *it;
        const Shapes shapes = makeShapes(group, underlines.value(it.key()), l);
        const QRectF bounds = shapes.bounds();
        if (bounds.isEmpty() && shapes.underlines.isEmpty())
            continue;
        const QRect box = outsetBox(bounds, outset);
        TextUnit unit;
        unit.fill = sprite(shapes, box, pad, style, l, m, false);
        if (unit.fill.isNull())
            continue;
        if (m.strokeOut > 0)
            unit.stroke = sprite(shapes, box, pad, style, l, m, true);
        const Glyph &first = *group.first();
        unit.bounds = QRectF(box.translated(pad));
        unit.character = characterAt[first.utf16];
        unit.word = std::max(0, wordOf[characterAt[first.utf16]]);
        unit.line = first.line;
        unit.lineCenterX = (lineMin[first.line] + lineMax[first.line]) / 2 + pad.x();
        unit.cellCenterX = first.position.x() + first.advance / 2 + pad.x();
        unit.cellWidth = first.advance;
        unit.baseline = lines[first.line].baseline + pad.y();
        set->units.append(unit);
    }

    const double below = 0.17 * l.fontPx;
    for (const LineInfo &line : lines) {
        const double top = line.baseline - line.ascent - outset + pad.y();
        const double bottom = line.baseline + std::max(line.descent, below) + outset + pad.y();
        set->lineBands.append(QRectF(0, top, m.width, bottom - top));
    }
    if (style.backgroundEnabled) {
        QImage bg(m.width, m.height, QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::transparent);
        QPainter p(&bg);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(backgroundPath(l, m, style).translated(m.padX, m.padY), qcolor(style.backgroundColor));
        p.end();
        set->background = {LinearImage::fromImageEncoded(bg), QPoint(0, 0)};
    }
    set->size = QSize(m.width, m.height);
    set->style = style;
    set->fontPx = l.fontPx;
    set->shadowOffset = m.shadowOffset;
    set->shadowBlur = m.shadowBlur;
    set->characterCount = int(ends.size());
    set->textBox = QRectF(QPointF(m.padX, m.padY), l.textSize);
    for (int k = 0; k < lines.size(); ++k)
        set->lineSpans.append(lineMax[k] > lineMin[k] ? std::pair{lineMin[k] + m.padX, lineMax[k] + m.padX}
                                                      : std::pair{m.padX, m.padX});
    set->backgroundPadding = QSizeF(m.bgX, m.bgY);
    set->backgroundRadius = std::max(0.0, style.backgroundRadius) * l.fontPx;
    return set;
}

QMutex cacheLock;
QCache<QString, std::shared_ptr<TextUnits>> unitCache(32);
QCache<QString, std::shared_ptr<QList<ScrambleGlyph>>> scrambleCache(8);

// MARK: - Cor

/// Sprite de cor única no lugar das letras (como a sombra: os valores sRGB entram como estão).
LinearImage solid(const LinearImage &img, const RGBA &c)
{
    LinearImage out(img.width(), img.height());
    const float r = float(c.r), g = float(c.g), b = float(c.b), a = float(std::clamp(c.a, 0.0, 1.0));
    for (int y = 0; y < img.height(); ++y) {
        const Pixel *s = img.row(y);
        Pixel *d = out.row(y);
        for (int x = 0; x < img.width(); ++x) {
            const float alpha = s[x].a * a;
            d[x] = {r * alpha, g * alpha, b * alpha, alpha};
        }
    }
    return out;
}

/// O preenchimento misturado com a cor do destaque (`amount` 0...1).
LinearImage tinted(const LinearImage &img, const RGBA &c, double amount)
{
    const LinearImage target = solid(img, c);
    if (amount >= 0.999)
        return target;
    LinearImage out(img.width(), img.height());
    const float t = float(amount), u = 1 - t;
    for (int y = 0; y < img.height(); ++y) {
        const Pixel *s = img.row(y), *q = target.row(y);
        Pixel *d = out.row(y);
        for (int x = 0; x < img.width(); ++x)
            d[x] = {s[x].r * u + q[x].r * t, s[x].g * u + q[x].g * t, s[x].b * u + q[x].b * t, s[x].a * u + q[x].a * t};
    }
    return out;
}

double smoothstep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

/// O preenchimento com a cor até a fração `progress` da palavra `rect` (borda suave de 0,06 em).
LinearImage filled(const TextSprite &fill, const RGBA &c, double progress, const QRectF &rect, double em)
{
    if (progress <= 0.001)
        return fill.image;
    const LinearImage target = solid(fill.image, c);
    if (progress >= 0.999)
        return target;
    const double soft = std::max(1.0, 0.06 * em);
    const double x0 = rect.left() + progress * rect.width() - soft / 2;
    LinearImage out(fill.image.width(), fill.image.height());
    for (int x = 0; x < out.width(); ++x) {
        const float mask = float(1 - smoothstep((fill.origin.x() + x + 0.5 - x0) / soft));
        for (int y = 0; y < out.height(); ++y) {
            const Pixel &s = fill.image.at(x, y), &q = target.at(x, y);
            out.at(x, y) = {q.r * mask + s.r * (1 - mask), q.g * mask + s.g * (1 - mask), q.b * mask + s.b * (1 - mask),
                            q.a * mask + s.a * (1 - mask)};
        }
    }
    return out;
}

/// Desenha uma forma com antisserrilhado sobre `dst` (valores sRGB), com a opacidade `alpha`.
void paintPath(LinearImage &dst, const QPainterPath &path, const RGBA &color, double alpha)
{
    if (alpha <= 0.001 || path.isEmpty())
        return;
    const QRect box = path.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1).intersected(QRect(QPoint(), dst.size()));
    if (box.isEmpty())
        return;
    QImage img(box.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-box.x(), -box.y());
    p.fillPath(path, qcolor(color));
    p.end();
    dst.drawAt(LinearImage::fromImageEncoded(img), box.x(), box.y(), float(alpha));
}

/// Multiplica a imagem toda (cor e alfa) por `a`.
void scaleAlpha(LinearImage &img, double a)
{
    if (a >= 0.999)
        return;
    const float k = float(std::max(0.0, a));
    for (int y = 0; y < img.height(); ++y) {
        Pixel *p = img.row(y);
        for (int x = 0; x < img.width(); ++x)
            p[x] = {p[x].r * k, p[x].g * k, p[x].b * k, p[x].a * k};
    }
}

// MARK: - Poses

struct Pose {
    double dx = 0;
    /// Para cima, como no Swift.
    double dy = 0;
    double scale = 1;
    double alpha = 1;
    double blur = 0;
    std::optional<QRectF> clip;
    bool scramble = false;
};

/// Fração do tempo da animação em que cada unidade anima (com uma unidade só, o tempo todo).
double window(AnimationKind kind)
{
    switch (kind) {
    case AnimationKind::Cascade: return 0.45;
    case AnimationKind::Elastic: return 0.5;
    case AnimationKind::Wave: return 0.35;
    case AnimationKind::WordByWord: return 0.55;
    case AnimationKind::MaskReveal: return 0.7;
    case AnimationKind::Tracking: return 1;
    case AnimationKind::Decode: return 0.5;
    default: return 1;
    }
}

/// Progresso da unidade `i` de `n`: a janela de largura `w` começa em `(1-w)·i/(n-1)`.
double unitProgress(double p, int i, int n, double w)
{
    if (n <= 1 || w >= 1)
        return Animations::clamp(p);
    const double start = (1 - w) * double(i) / double(n - 1);
    return Animations::clamp((p - start) / w);
}

Pose pose(AnimationKind kind, double q, bool exiting, const TextUnit &unit, const std::optional<QRectF> &band, double em)
{
    Pose pose;
    // Entrando, as unidades vêm de baixo; saindo, continuam subindo.
    const double up = exiting ? 1 : -1;
    switch (kind) {
    case AnimationKind::Cascade: {
        const double e = Animations::expoOut(q);
        pose.dy = up * (1 - e) * 0.6 * em;
        pose.blur = (1 - e) * 0.25 * em;
        pose.alpha = e;
        break;
    }
    case AnimationKind::Elastic:
        pose.scale = 0.3 + 0.7 * Animations::spring(q);
        pose.dy = up * (1 - Animations::quartOut(q)) * 0.25 * em;
        pose.alpha = Animations::quartOut(std::min(1.0, q * 3));
        break;
    case AnimationKind::Wave: {
        const double hop = std::sin(q * M_PI);
        pose.dy = hop * 0.35 * em;
        pose.scale = 1 + hop * 0.12;
        pose.alpha = Animations::quartOut(std::min(1.0, q * 2.5));
        break;
    }
    case AnimationKind::WordByWord: {
        const double e = Animations::expoOut(q);
        pose.dy = up * (1 - e) * 0.45 * em;
        pose.blur = (1 - e) * 0.12 * em;
        pose.alpha = e;
        break;
    }
    case AnimationKind::MaskReveal: {
        const double e = Animations::quintOut(q);
        const double travel = (band ? band->height() : em) * 1.1;
        pose.dy = up * (1 - e) * travel;
        pose.clip = band;
        break;
    }
    case AnimationKind::Tracking: {
        const double e = Animations::quartOut(q);
        pose.dx = (unit.bounds.center().x() - unit.lineCenterX) * 0.6 * (1 - e);
        pose.alpha = e;
        break;
    }
    case AnimationKind::Decode:
        pose.alpha = Animations::quartOut(std::min(1.0, q * 3));
        pose.scramble = q < 1;
        break;
    default: break;
    }
    return pose;
}

QTransform translation(double x, double y) { return QTransform::fromTranslate(x, y); }

/// Escala `s` em torno de `c` (aplicada depois das transformações à esquerda).
QTransform scaling(double s, QPointF c)
{
    return translation(-c.x(), -c.y()) * QTransform::fromScale(s, s) * translation(c.x(), c.y());
}

/// Desenha o sprite com a transformação (do sprite para a imagem), o desfoque em pixels da imagem, o recorte
/// e a opacidade.
void drawSprite(LinearImage &dst, const LinearImage &image, QPoint origin, const QTransform &place, double blur,
                const QRect &clip, double alpha)
{
    if (image.isNull() || alpha <= 0.001)
        return;
    QTransform t = translation(origin.x(), origin.y()) * place;
    const LinearImage *src = &image;
    LinearImage blurred;
    const double scale = std::sqrt(std::abs(t.determinant()));
    if (blur > 0.3 && scale > 0) {
        // O desfoque é em pixels da imagem final: no sprite, dividido pela escala; com folga transparente.
        const double sigma = blur / scale;
        const int padding = int(std::ceil(3 * sigma)) + 1;
        blurred = LinearImage(image.width() + 2 * padding, image.height() + 2 * padding);
        blurred.drawAt(image, padding, padding);
        blurred.blur(sigma);
        src = &blurred;
        t = translation(-padding, -padding) * t;
    }
    const bool integer = t.type() <= QTransform::TxTranslate && std::abs(t.dx() - std::round(t.dx())) < 1e-6 &&
                         std::abs(t.dy() - std::round(t.dy())) < 1e-6;
    if (integer)
        dst.drawAt(*src, int(std::round(t.dx())), int(std::round(t.dy())), float(alpha), clip);
    else
        dst.draw(*src, t, float(alpha), clip);
}

// MARK: - Destaque e revelações

/// Rampa suave (0...1) de `highlightRamp`, centrada em `start`.
double rampOn(Micros start, Micros local)
{
    return smoothstep(double(local - start + TextMotion::highlightRamp / 2) / double(TextMotion::highlightRamp));
}

/// Quanto da palavra já foi preenchido (0...1), ao longo da duração dela.
double fillProgress(const CaptionWord &word, Micros local)
{
    if (word.end <= word.start)
        return local >= word.start ? 1 : 0;
    return Animations::clamp(double(local - word.start) / double(word.end - word.start));
}

/// Deslocamento horizontal de cada palavra para abrir espaço à que cresce: as da linha se afastam metade do
/// aumento para cada lado, e a linha continua centrada.
QHash<int, double> wordShifts(const WordHighlight &style, const TextMotion::HighlightAmounts &amounts,
                              const QHash<int, QRectF> &rects, const QHash<int, int> &lines)
{
    QHash<int, double> shifts;
    for (auto v = rects.cbegin(); v != rects.cend(); ++v) {
        if (v.key() < 0 || v.key() >= amounts.emphasis.size() || amounts.emphasis[v.key()] <= 0.001)
            continue;
        const double extra = (style.scale - 1) * amounts.emphasis[v.key()] * v->width() / 2;
        if (extra == 0)
            continue;
        for (auto w = rects.cbegin(); w != rects.cend(); ++w)
            if (w.key() != v.key() && lines.value(w.key()) == lines.value(v.key()))
                shifts[w.key()] += w->center().x() < v->center().x() ? -extra : extra;
    }
    return shifts;
}

/// Retângulo arredondado atrás da palavra: a largura da palavra com folga e a altura da linha.
QRectF pillRect(const QRectF &word, const QRectF &band, double em)
{
    return QRectF(word.left() - 0.1 * em, band.top(), word.width() + 0.2 * em, band.height());
}

QPainterPath roundedRect(const QRectF &rect, double radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

/// Cursor fino depois da última letra visível (antes da primeira, enquanto nada foi digitado). Pisca quando
/// para e some `cursorHold` depois da última letra.
std::optional<QRectF> typingCursor(const QList<Micros> &times, const TextUnits &set, const QHash<int, double> &shifts,
                                   Micros local, double em)
{
    if (set.units.isEmpty() || times.size() != set.units.size())
        return std::nullopt;
    const Micros finish = *std::max_element(times.begin(), times.end());
    if (local >= finish + TextMotion::cursorHold)
        return std::nullopt;
    std::optional<int> last;
    for (int i = 0; i < set.units.size(); ++i)
        if (times[i] <= local && (!last || set.units[i].character > set.units[*last].character))
            last = i;
    const Micros since = local - (last ? times[*last] : 0);
    if (since > TextMotion::cursorIdle && (since - TextMotion::cursorIdle) % 1'000'000 >= 500'000)
        return std::nullopt;
    int first = 0;
    for (int i = 1; i < set.units.size(); ++i)
        if (set.units[i].character < set.units[first].character)
            first = i;
    const TextUnit &unit = set.units[last.value_or(first)];
    const double gap = 0.05 * em;
    const double width = std::max(1.5, 0.07 * em);
    const double x = last ? unit.cellCenterX + unit.cellWidth / 2 + gap : unit.cellCenterX - unit.cellWidth / 2 - gap - width;
    // 0,12 em abaixo da linha de base, 0,9 em de altura.
    return QRectF(x + shifts.value(unit.word), unit.baseline + 0.12 * em - 0.9 * em, width, 0.9 * em);
}

/// Fundo da digitação no instante: a caixa das letras já digitadas (até o cursor), com a folga e o raio do fundo.
/// Cada letra empurra as bordas da caixa anterior até a sua, com smoothstep em `typingGrow`, começando
/// `typingLead` antes de ela aparecer; os empurrões se somam. Vazio quando já chegou à caixa inteira.
std::optional<QPainterPath> typingBackground(const QList<Micros> &times, const TextUnits &set,
                                             const QHash<int, double> &shifts, Micros local, double em)
{
    if (set.units.isEmpty() || times.size() != set.units.size() || set.textBox.isEmpty() ||
        set.lineSpans.size() != set.lineBands.size())
        return std::nullopt;
    QList<int> ordered(set.units.size());
    std::iota(ordered.begin(), ordered.end(), 0);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](int a, int b) { return set.units[a].character < set.units[b].character; });
    if (local >= times[ordered.last()] - TextMotion::typingLead + TextMotion::typingGrow)
        return std::nullopt;
    const QRectF box = set.textBox;
    const double gap = 0.05 * em;
    const double width = std::max(1.5, 0.07 * em);
    // Base da faixa da linha `k`: entre ela e a seguinte; a última vai até a base da caixa.
    const auto bottom = [&](int k) {
        if (k + 1 >= set.lineBands.size())
            return box.bottom();
        return std::clamp((set.lineBands[k].bottom() + set.lineBands[k + 1].top()) / 2, box.top(), box.bottom());
    };
    struct Edges {
        double left, bottom, right;
    };
    // Bordas da caixa com as letras até a posição `p` de `ordered` (-1: só o cursor; a última: a caixa inteira).
    const auto reach = [&](int p) -> Edges {
        if (p >= ordered.size() - 1)
            return {box.left(), box.bottom(), box.right()};
        if (p < 0) {
            const TextUnit &u = set.units[ordered[0]];
            const double x = u.cellCenterX - u.cellWidth / 2 - gap - width + shifts.value(u.word);
            return {x, bottom(u.line), x + width};
        }
        const TextUnit &u = set.units[ordered[p]];
        const int k = std::min<int>(u.line, int(set.lineSpans.size()) - 1);
        const double cursor = std::min(box.right(), u.cellCenterX + u.cellWidth / 2 + gap + width + shifts.value(u.word));
        double left = INFINITY, right = cursor;
        for (int j = 0; j <= k; ++j)
            left = std::min(left, set.lineSpans[j].first);
        for (int j = 0; j < k; ++j)
            right = std::max(right, set.lineSpans[j].second);
        return {left, bottom(k), right};
    };
    Edges edges = reach(-1), previous = edges;
    for (int p = 0; p < ordered.size(); ++p) {
        const Edges next = reach(p);
        const double x = Animations::clamp(double(local - times[ordered[p]] + TextMotion::typingLead) /
                                           double(TextMotion::typingGrow));
        if (x > 0) {
            const double e = smoothstep(x);
            edges.left += (next.left - previous.left) * e;
            edges.bottom += (next.bottom - previous.bottom) * e;
            edges.right += (next.right - previous.right) * e;
        }
        previous = next;
    }
    const QRectF rect = QRectF(edges.left, box.top(), std::max(0.0, edges.right - edges.left), edges.bottom - box.top())
                            .adjusted(-set.backgroundPadding.width(), -set.backgroundPadding.height(),
                                      set.backgroundPadding.width(), set.backgroundPadding.height());
    return roundedRect(rect, std::min(set.backgroundRadius, std::min(rect.width(), rect.height()) / 2));
}

} // namespace

// MARK: - API

std::shared_ptr<const TextUnits> TextMotion::units(const TextContent &content, QSize canvas, TextGranularity granularity)
{
    const QString text = TextRenderer::cased(content.text, content.style.textCase);
    if (text.isEmpty() || canvas.height() <= 0)
        return nullptr;
    const QString key = QStringLiteral("%1\x1f%2x%3\x1f%4")
                            .arg(contentKey(text, content))
                            .arg(canvas.width())
                            .arg(canvas.height())
                            .arg(int(granularity));
    {
        QMutexLocker locker(&cacheLock);
        if (auto *cached = unitCache.object(key))
            return *cached;
    }
    TextRenderer::shared(); // registra as fontes distribuídas
    auto built = buildUnits(text, content, canvas, granularity);
    QMutexLocker locker(&cacheLock);
    unitCache.insert(key, new std::shared_ptr<TextUnits>(built));
    return built;
}

std::shared_ptr<const QList<ScrambleGlyph>> TextMotion::scrambleGlyphs(const TextStyle &style, QSize canvas)
{
    const QString key = QStringLiteral("%1\x1f%2x%3").arg(styleKey(style)).arg(canvas.width()).arg(canvas.height());
    {
        QMutexLocker locker(&cacheLock);
        if (auto *cached = scrambleCache.object(key))
            return *cached;
    }
    TextRenderer::shared();
    const Layout l = buildLayout(QStringLiteral("A"), style, canvas, std::nullopt);
    const Metrics m = metrics(l, style);
    const double outset = m.strokeOut + (l.face.syntheticBold ? syntheticBold * l.fontPx : 0) + 2;
    const QRawFont raw = QRawFont::fromFont(l.face.font);
    auto result = std::make_shared<QList<ScrambleGlyph>>();
    for (const QChar ch : scrambleCharacters) {
        const QList<quint32> indexes = raw.glyphIndexesForString(QString(ch));
        if (indexes.isEmpty() || indexes[0] == 0)
            continue;
        Glyph g;
        g.raw = raw;
        g.index = indexes[0];
        QTransform t;
        t.scale(l.s, l.s);
        if (l.face.syntheticItalic)
            t.shear(-syntheticSlant, 0);
        g.outline = t.map(raw.pathForGlyph(indexes[0]));
        if (g.outline.isEmpty())
            continue;
        g.advance = raw.advancesForGlyphIndexes(indexes)[0].x() * l.s;
        const Shapes shapes = makeShapes({&g}, {}, l);
        const QRect box = outsetBox(shapes.bounds(), outset);
        ScrambleGlyph glyph{sprite(shapes, box, QPoint(), style, l, m, false), {}, g.advance};
        if (m.strokeOut > 0)
            glyph.stroke = sprite(shapes, box, QPoint(), style, l, m, true);
        result->append(glyph);
    }
    QMutexLocker locker(&cacheLock);
    scrambleCache.insert(key, new std::shared_ptr<QList<ScrambleGlyph>>(result));
    return result;
}

QList<int> TextMotion::scrambleCandidates(const QList<ScrambleGlyph> &glyphs, double cellWidth)
{
    QList<int> fitting;
    for (int i = 0; i < glyphs.size(); ++i)
        if (glyphs[i].advance <= cellWidth * 1.3)
            fitting.append(i);
    if (fitting.size() >= 4)
        return fitting;
    QList<int> nearest(glyphs.size());
    std::iota(nearest.begin(), nearest.end(), 0);
    std::sort(nearest.begin(), nearest.end(), [&](int a, int b) {
        const double da = std::abs(glyphs[a].advance - cellWidth), db = std::abs(glyphs[b].advance - cellWidth);
        return da != db ? da < db : a < b;
    });
    nearest = nearest.mid(0, 4);
    std::sort(nearest.begin(), nearest.end());
    return nearest;
}

int TextMotion::scrambleIndex(int index, int step, int count)
{
    quint64 z = quint64(qint64(index)) * 0x9E3779B97F4A7C15ULL + quint64(qint64(step)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return int(z % quint64(std::max(count, 1)));
}

QList<Micros> TextMotion::typingTimes(const QList<CaptionWord> &words, const QList<TextUnit> &units)
{
    QList<Micros> times(units.size(), 0);
    QMap<int, QList<int>> byWord;
    for (int i = 0; i < units.size(); ++i)
        byWord[units[i].word].append(i);
    for (auto it = byWord.begin(); it != byWord.end(); ++it) {
        const int w = it.key();
        if (w < 0 || w >= words.size())
            continue;
        const Micros span = std::max<Micros>(0, words[w].end - words[w].start);
        QList<int> ordered = *it;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [&](int a, int b) { return units[a].character < units[b].character; });
        for (int j = 0; j < ordered.size(); ++j)
            times[ordered[j]] = words[w].start + span * Micros(j) / Micros(ordered.size());
    }
    return times;
}

TextMotion::HighlightAmounts TextMotion::highlightAmounts(const QList<CaptionWord> &words, HighlightMode mode, Micros local)
{
    HighlightAmounts a{QList<double>(words.size(), 0), QList<double>(words.size(), 0)};
    for (int i = 0; i < words.size(); ++i) {
        const double reached = rampOn(words[i].start, local);
        const double next = i + 1 < words.size() ? rampOn(words[i + 1].start, local) : 0;
        a.emphasis[i] = reached * (1 - next);
        a.color[i] = mode == HighlightMode::Progressive ? reached : a.emphasis[i];
    }
    return a;
}

void TextMotion::applyGlow(LinearImage &image, double amount, double sigma)
{
    if (amount <= 0.001 || image.isNull())
        return;
    LinearImage halo = image;
    halo.blur(std::max(sigma, 0.5));
    const float k = float(1.8 * amount);
    for (int y = 0; y < halo.height(); ++y) {
        Pixel *p = halo.row(y);
        for (int x = 0; x < halo.width(); ++x) {
            Pixel q{p[x].r * k, p[x].g * k, p[x].b * k, p[x].a * k};
            if (q.a > 1) {
                const float f = 1 / q.a;
                q = {q.r * f, q.g * f, q.b * f, 1};
            }
            p[x] = q;
        }
    }
    halo.drawAt(image, 0, 0);
    image = std::move(halo);
}

void TextMotion::applyShine(LinearImage &image, double position, double strength, bool linear, const QRectF &area)
{
    const int W = image.width(), H = image.height();
    if (strength <= 0.001 || W <= 0 || H <= 0)
        return;
    const QRectF rect = area.isEmpty() ? QRectF(0, 0, W, H) : area;
    const double angle = 20 * M_PI / 180;
    const double cx = std::cos(angle), cy = std::sin(angle);
    // Projeções dos cantos na direção da faixa (y para cima, como no Swift).
    const double l = rect.left(), r = rect.right(), b = H - rect.bottom(), t = H - rect.top();
    const double corners[4] = {l * cx + b * cy, r * cx + b * cy, l * cx + t * cy, r * cx + t * cy};
    const double shortSide = std::min(rect.width(), rect.height());
    const double band = 0.55 * shortSide + 0.08 * std::max(rect.width(), rect.height());
    // Começa e termina com a faixa ainda tocando a caixa: a passagem aparece em mais quadros.
    const double from = *std::min_element(corners, corners + 4) - band * 0.3;
    const double to = *std::max_element(corners, corners + 4) + band * 0.3;
    const double c = from + (to - from) * position;
    // Tom levemente quente: em texto branco, a faixa aparece pela cor.
    const float wr = 1, wg = linear ? toLinear(0.93f) : 0.93f, wb = linear ? toLinear(0.8f) : 0.8f;
    // Brilho que vaza: só onde a camada desfocada tem alfa.
    LinearImage spread = image;
    spread.blur(0.06 * shortSide + 2);
    const double inside = 0.85 * strength, outside = std::min(1.0, 1.1 * strength);
    parallelFor(H, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            Pixel *row = image.row(y);
            const Pixel *halo = spread.row(y);
            const double up = H - (y + 0.5);
            for (int x = 0; x < W; ++x) {
                const double t = (x + 0.5) * cx + up * cy;
                // Rampas suaves (smoothstep) dos dois lados do centro: o mínimo das duas é a faixa.
                const double light = std::min(smoothstep((t - (c - band / 2)) / (band / 2)), 1 - smoothstep((t - c) / (band / 2)));
                if (light <= 0)
                    continue;
                const Pixel d = row[x];
                const float la = float(light * inside);
                // Dentro: a luz em source-atop (só onde a camada tem alfa).
                Pixel in{wr * la * d.a + d.r * (1 - la), wg * la * d.a + d.g * (1 - la), wb * la * d.a + d.b * (1 - la), d.a};
                const float lb = float(light * outside) * halo[x].a;
                const float k = 1 - in.a;
                row[x] = {in.r + wr * lb * k, in.g + wg * lb * k, in.b + wb * lb * k, in.a + lb * k};
            }
        }
    });
}

namespace {

/// Um sprite posado, ainda não desenhado: a primeira passada só mede até onde as unidades vão.
struct SpriteDraw {
    const LinearImage *image = nullptr;
    /// Cópia alterada (tingida ou preenchida), quando há; `image` aponta para ela.
    std::shared_ptr<const LinearImage> owned;
    QPoint origin;
    QTransform place;
    double blur = 0;
    QRect clip;
    double alpha = 1;

    /// Retângulo que o sprite ocupa na imagem do texto (com o desfoque e o recorte).
    QRectF extent() const
    {
        QRectF r = (translation(origin.x(), origin.y()) * place).mapRect(QRectF(0, 0, image->width(), image->height()));
        if (blur > 0.3)
            r.adjust(-3 * blur - 1, -3 * blur - 1, 3 * blur + 1, 3 * blur + 1);
        return clip.isEmpty() ? r : r.intersected(QRectF(clip));
    }
};

} // namespace

std::optional<TextMotion::Image> TextMotion::image(const TextContent &content, QSize canvas, const AnimationState &state,
                                                   TextGranularity granularity)
{
    const auto setPtr = units(content, canvas, granularity);
    if (!setPtr)
        return std::nullopt;
    const TextUnits &set = *setPtr;
    const double em = set.fontPx;
    const int visible = state.reveal >= 1 ? std::numeric_limits<int>::max()
                                          : int(std::ceil(double(set.characterCount) * state.reveal));
    const int step = int(state.local / 50'000);
    std::shared_ptr<const QList<ScrambleGlyph>> scramble;
    if (state.unit && state.unit->kind == AnimationKind::Decode)
        scramble = scrambleGlyphs(content.style, canvas);

    // Destaque da palavra falada: cor (mistura), escala em torno do centro da palavra e pílula.
    const auto highlight = content.activeHighlight();
    std::optional<HighlightAmounts> amounts;
    if (highlight)
        amounts = highlightAmounts(highlight->words, highlight->style.mode, state.local);
    QHash<int, QRectF> wordRects;
    QHash<int, int> wordLines;
    if (highlight)
        for (const TextUnit &unit : set.units) {
            wordRects[unit.word] = wordRects.contains(unit.word) ? wordRects[unit.word].united(unit.bounds) : unit.bounds;
            if (!wordLines.contains(unit.word))
                wordLines[unit.word] = unit.line;
        }
    const QHash<int, double> shifts =
        highlight ? wordShifts(highlight->style, *amounts, wordRects, wordLines) : QHash<int, double>();
    QHash<int, QList<Pose>> wordPoses;
    // Revelação pela fala (só com o destaque ativo: texto editado aparece inteiro).
    const WordReveal reveal = highlight ? highlight->style.reveal : WordReveal::None;
    const QList<Micros> letterTimes =
        reveal == WordReveal::Type ? typingTimes(highlight->words, set.units) : QList<Micros>();

    QList<SpriteDraw> fillDraws, strokeDraws;
    const int n = int(set.units.size());
    // Com o destaque, a "revelar por máscara" vem em sprites de palavra: o passo continua sendo a linha.
    const bool byLine = state.unit && Animations::granularity(state.unit->kind) == TextGranularity::Line &&
                        granularity != TextGranularity::Line;
    for (int i = 0; i < n; ++i) {
        const TextUnit &unit = set.units[i];
        if (unit.character >= visible)
            continue;
        Pose p;
        if (state.unit) {
            const int index = byLine ? unit.line : i;
            const int count = byLine ? int(set.lineBands.size()) : n;
            const double q = unitProgress(state.unit->progress, index, count, window(state.unit->kind));
            const std::optional<QRectF> band =
                unit.line < set.lineBands.size() ? std::optional(set.lineBands[unit.line]) : std::nullopt;
            p = pose(state.unit->kind, q, state.unit->exiting, unit, band, em);
        }
        if (highlight && reveal != WordReveal::None && unit.word < highlight->words.size()) {
            const CaptionWord &word = highlight->words[unit.word];
            switch (reveal) {
            case WordReveal::Rise: {
                // Sobe um pouco, de desfocada e transparente a nítida e opaca; antes de ser dita, não aparece.
                const double e = Animations::expoOut(double(state.local - word.start) / double(riseDuration));
                p.alpha *= e;
                p.dy -= (1 - e) * 0.3 * em;
                p.blur = std::max(p.blur, (1 - e) * 0.12 * em);
                break;
            }
            case WordReveal::Dim: {
                const double dim = Animations::clamp(highlight->style.dimOpacity);
                p.alpha *= dim + (1 - dim) * rampOn(word.start, state.local);
                break;
            }
            case WordReveal::Type:
                if (i < letterTimes.size() && state.local < letterTimes[i])
                    p.alpha = 0;
                break;
            case WordReveal::Fill:
            case WordReveal::None: break;
            }
        }
        if (highlight)
            wordPoses[unit.word].append(p);
        if (p.alpha <= 0.001)
            continue;

        const LinearImage *fill = &unit.fill.image;
        std::shared_ptr<const LinearImage> owned;
        const LinearImage *stroke = unit.stroke.isNull() ? nullptr : &unit.stroke.image;
        QPointF fillOrigin = unit.fill.origin, strokeOrigin = unit.stroke.origin;
        if (p.scramble && scramble && !scramble->isEmpty()) {
            // Mesmo tamanho de fonte e mesma linha de base da letra, centralizado na célula. O sorteio só usa
            // glifos de largura parecida, para não encavalar nas vizinhas.
            const QList<int> pool = scrambleCandidates(*scramble, unit.cellWidth);
            const ScrambleGlyph &g = (*scramble)[pool[scrambleIndex(i, step, int(pool.size()))]];
            const QPointF move(unit.cellCenterX - g.advance / 2, unit.baseline);
            fill = &g.fill.image;
            fillOrigin = QPointF(g.fill.origin) + move;
            stroke = g.stroke.isNull() ? nullptr : &g.stroke.image;
            strokeOrigin = QPointF(g.stroke.origin) + move;
        }
        double grow = 1;
        if (highlight && unit.word < amounts->color.size()) {
            switch (reveal) {
            case WordReveal::Fill:
                // A cor enche a palavra da esquerda para a direita ao longo da duração dela.
                owned = std::make_shared<const LinearImage>(
                    filled({*fill, fillOrigin.toPoint()}, highlight->style.color,
                           fillProgress(highlight->words[unit.word], state.local), wordRects.value(unit.word), em));
                fill = owned.get();
                break;
            case WordReveal::Type: break; // a cor vai para o cursor
            default:
                if (amounts->color[unit.word] > 0.001) {
                    owned = std::make_shared<const LinearImage>(tinted(*fill, highlight->style.color, amounts->color[unit.word]));
                    fill = owned.get();
                }
            }
            grow = 1 + (highlight->style.scale - 1) * amounts->emphasis[unit.word];
        }
        const double shift = shifts.value(unit.word);
        // Do canto do sprite para a imagem: desloca, aplica a pose em torno do centro da unidade e faz a
        // palavra crescer inteira, em torno do próprio centro.
        QTransform place = translation(shift, 0);
        if (p.scale != 1 || p.dx != 0 || p.dy != 0) {
            const QPointF c = unit.bounds.center();
            const double s = std::max(p.scale, 0.001);
            place = place * translation(-c.x(), -c.y()) * QTransform::fromScale(s, s) * translation(c.x() + p.dx, c.y() - p.dy);
        }
        if (std::abs(grow - 1) > 0.0001 && wordRects.contains(unit.word)) {
            const QRectF rect = wordRects[unit.word];
            place = place * scaling(grow, QPointF(rect.center().x() + shift + p.dx, rect.center().y() - p.dy));
        }
        const QRect clip = p.clip ? p.clip->toAlignedRect() : QRect();
        const auto origin = [](QPointF o) { return QTransform::fromTranslate(o.x() - std::floor(o.x()), o.y() - std::floor(o.y())); };
        const SpriteDraw f{fill, owned, QPoint(int(std::floor(fillOrigin.x())), int(std::floor(fillOrigin.y()))),
                           origin(fillOrigin) * place, p.blur, clip, p.alpha};
        if (!fill->isNull())
            fillDraws.append(f);
        if (stroke && !stroke->isNull())
            strokeDraws.append({stroke, nullptr, QPoint(int(std::floor(strokeOrigin.x())), int(std::floor(strokeOrigin.y()))),
                                origin(strokeOrigin) * place, p.blur, clip, p.alpha});
    }
    std::optional<QRectF> cursor;
    if (reveal == WordReveal::Type)
        cursor = typingCursor(letterTimes, set, shifts, state.local, em);
    const TextStyle &style = set.style;
    std::optional<QPainterPath> typed;
    if (!set.background.isNull() && reveal == WordReveal::Type)
        typed = typingBackground(letterTimes, set, shifts, state.local, em);
    // Pílulas do destaque (forma já posada e opacidade).
    QList<std::pair<QPainterPath, double>> pills;
    if (highlight && highlight->style.backgroundEnabled)
        for (auto it = wordPoses.cbegin(); it != wordPoses.cend(); ++it) {
            const int word = it.key();
            const QList<Pose> &poses = *it;
            if (word >= amounts->emphasis.size() || amounts->emphasis[word] <= 0.001 || poses.isEmpty() ||
                !wordRects.contains(word) || wordLines.value(word) >= set.lineBands.size())
                continue;
            // Pose média das unidades da palavra: com letras separadas, elas andam quase juntas.
            double dx = 0, dy = 0, a = 0;
            for (const Pose &q : poses)
                dx += q.dx, dy += q.dy, a += q.alpha;
            dx = dx / poses.size() + shifts.value(word);
            dy /= poses.size();
            a /= poses.size();
            const double e = amounts->emphasis[word];
            const double g = 1 + (highlight->style.scale - 1) * e;
            const QRectF box = wordRects[word];
            const QRectF rect = pillRect(box, set.lineBands[wordLines[word]], em);
            const QTransform t = translation(dx, -dy) * scaling(g, QPointF(box.center().x() + dx, box.center().y() - dy));
            pills.append({t.map(roundedRect(rect, std::min(0.3 * em, rect.height() / 2))), a * e});
        }

    // Margem simétrica (o centro não muda) para o que as unidades posadas, a sombra e os loops levam para fora
    // da imagem estática: no Swift, a imagem do Core Image não tem borda.
    const QRectF nominal(QPointF(0, 0), QSizeF(set.size));
    QRectF glyphExtent;
    for (const auto *draws : {&fillDraws, &strokeDraws})
        for (const SpriteDraw &d : *draws)
            glyphExtent = glyphExtent.united(d.extent());
    if (cursor)
        glyphExtent = glyphExtent.united(*cursor);
    QRectF extent = nominal.united(glyphExtent);
    if (!glyphExtent.isEmpty()) {
        if (style.shadow && style.shadowColor.a > 0) {
            const double spread = 1.5 * set.shadowBlur + 1;
            extent = extent.united(glyphExtent.translated(set.shadowOffset).adjusted(-spread, -spread, spread, spread));
        }
        if (state.glow() > 0) {
            const double spread = 3 * 0.012 * canvas.height() + 1;
            extent = extent.united(glyphExtent.adjusted(-spread, -spread, spread, spread));
        }
        if (state.shine()) {
            const double spread = 3 * (0.06 * std::min(nominal.width(), nominal.height()) + 2) + 1;
            extent = extent.united(glyphExtent.adjusted(-spread, -spread, spread, spread));
        }
    }
    for (const auto &[path, alpha] : pills)
        extent = extent.united(path.boundingRect());
    const int mx = int(std::ceil(std::max({0.0, -extent.left(), extent.right() - nominal.width()})));
    const int my = int(std::ceil(std::max({0.0, -extent.top(), extent.bottom() - nominal.height()})));
    const QSize full(set.size.width() + 2 * mx, set.size.height() + 2 * my);
    const QTransform offset = translation(mx, my);

    LinearImage strokes(full.width(), full.height());
    LinearImage fills(full.width(), full.height());
    const auto paint = [&](LinearImage &dst, const QList<SpriteDraw> &draws) {
        for (const SpriteDraw &d : draws)
            drawSprite(dst, *d.image, d.origin, d.place * offset, d.blur, d.clip.isEmpty() ? QRect() : d.clip.translated(mx, my),
                       d.alpha);
    };
    paint(fills, fillDraws);
    paint(strokes, strokeDraws);
    if (cursor) {
        QPainterPath path;
        path.addRect(cursor->translated(mx, my));
        paintPath(fills, path, highlight->style.color, 1);
    }
    // Todos os contornos por baixo de todos os preenchimentos, como no estático.
    LinearImage glyphs = std::move(strokes);
    glyphs.drawAt(fills, 0, 0);

    // Loops que dependem das letras separadas da sombra (a sombra sai só das letras). A faixa do reflexo
    // atravessa a caixa estática, não a margem.
    LinearImage out = glyphs;
    if (const auto shine = state.shine())
        applyShine(out, shine->position, shine->strength, false,
                   mx || my ? nominal.translated(mx, my) : QRectF());
    if (state.glow() > 0)
        applyGlow(out, state.glow(), 0.012 * canvas.height());

    LinearImage result(full.width(), full.height());
    // Fundo (na digitação, o que cresce com o texto), sombra, pílulas e letras.
    if (!set.background.isNull()) {
        if (typed)
            paintPath(result, offset.map(*typed), style.backgroundColor, state.backgroundAlpha);
        else
            result.drawAt(set.background.image, set.background.origin.x() + mx, set.background.origin.y() + my,
                          float(state.backgroundAlpha));
    }
    if (style.shadow && style.shadowColor.a > 0) {
        LinearImage shadow = solid(glyphs, style.shadowColor);
        // O `blur` da sombra do Core Graphics equivale a um desvio-padrão de ~metade dele.
        if (set.shadowBlur > 0.5)
            shadow.blur(set.shadowBlur / 2);
        result.draw(shadow, translation(set.shadowOffset.x(), set.shadowOffset.y()));
    }
    // Pílulas do destaque: por cima da sombra, por baixo das letras.
    for (const auto &[path, alpha] : pills)
        paintPath(result, offset.map(path), highlight->style.backgroundColor, alpha);
    result.drawAt(out, 0, 0);
    result.encodedToLinear();
    scaleAlpha(result, std::clamp(style.opacity, 0.0, 1.0));
    return Image{std::move(result), set.size};
}

} // namespace of
