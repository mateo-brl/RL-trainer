#pragma once
// RGBA colour in the 0..255 range, the same convention as BakkesMod's LinearColor for the canvas and cvars.

#include <cstdint>
#include <string>

namespace rlt {

struct Color {
    float r = 255.f, g = 255.f, b = 255.f, a = 255.f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 255.f) : r(r_), g(g_), b(b_), a(a_) {}

    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

    constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
};

namespace colors {
constexpr Color White{255, 255, 255, 255};
constexpr Color Black{0, 0, 0, 255};
constexpr Color Green{0, 255, 0, 255};
constexpr Color Yellow{255, 255, 0, 255};
constexpr Color Red{255, 0, 0, 255};
constexpr Color Orange{255, 140, 0, 255};
constexpr Color Blue{60, 140, 255, 255};
constexpr Color Cyan{0, 220, 255, 255};
constexpr Color Magenta{255, 0, 255, 255};
constexpr Color Grey{160, 160, 160, 255};
} // namespace colors

// Linear blend between two colours (t in 0..1).
Color mix(const Color& a, const Color& b, float t);

// Parses "#RRGGBB", "#RRGGBBAA", "(R, G, B, A)", "(R, G, B)" or "R G B A" (0..255 components).
// Returns false and leaves `out` untouched when the text cannot be parsed.
bool parseColor(const std::string& text, Color& out);

// Formats as "#RRGGBBAA" (the format written into BakkesMod cvars).
std::string toHex(const Color& c);

} // namespace rlt
