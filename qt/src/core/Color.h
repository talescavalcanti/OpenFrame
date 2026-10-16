#pragma once

#include <QColor>

namespace of {

struct RGBA {
    double r = 0, g = 0, b = 0, a = 1;

    static RGBA white() { return {1, 1, 1, 1}; }
    static RGBA black() { return {0, 0, 0, 1}; }
    static RGBA yellow() { return {1, 0.85, 0.1, 1}; }
    QColor color() const { return QColor::fromRgbF(float(r), float(g), float(b), float(a)); }
    bool operator==(const RGBA &) const = default;
};

} // namespace of
