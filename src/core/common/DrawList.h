#pragma once
// Render commands produced by modules (pure data) and executed by the plugin renderer on the game canvas.
// 2D coordinates are screen pixels (origin top-left). 3D coordinates are world uu; the renderer projects them,
// clips segments behind the camera and can hide parts occluded by a sphere (the ball).

#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/common/Math.h"

namespace rlt {

struct Vec2 {
    float x = 0.f, y = 0.f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
};

enum class Align : int { Left = 0, Center = 1, Right = 2 };

// Optional sphere that hides 3D lines behind it (from the camera point of view).
struct Occluder {
    bool enabled = false;
    Vec3 center;
    float radius = 0.f;
};

struct DrawText2D {
    Vec2 pos;              // anchor in pixels
    std::string text;      // UTF-8; the renderer folds accents unless the HUD accent option is on
    Color color;
    float scale = 1.f;     // canvas DrawString scale (1 = ~ 12 px font)
    bool shadow = true;
    Align align = Align::Left;
};

struct DrawRect2D {
    Vec2 pos;   // top-left
    Vec2 size;
    Color color;
    bool filled = true;
};

struct DrawLine2D {
    Vec2 a, b;
    Color color;
    float width = 1.f;
};

struct DrawTriangle2D {
    Vec2 a, b, c;
    Color color;
};

struct DrawLine3D {
    Vec3 a, b;
    Color color;
    float width = 1.f;
    Occluder occluder;
};

struct DrawText3D {
    Vec3 pos;          // world anchor; skipped when behind the camera
    Vec2 offset;       // pixel offset after projection
    std::string text;
    Color color;
    float scale = 1.f;
    bool shadow = true;
    Align align = Align::Center;
};

// A HUD panel laid out by the renderer (which can measure text): background box, optional title,
// then lines of text, optional progress bars. All modules use it so the HUD looks consistent.
struct PanelLine {
    std::string text;
    Color color = colors::White;
    float bar = -1.f;           // 0..1 draws a progress bar under/after the text; < 0 = no bar
    Color barColor = colors::Green;
    float scale = 1.f;          // relative to the panel scale
    bool detail = false;        // secondary line (stats, history, help): hidden when the compact HUD is on
};

struct DrawPanel {
    Vec2 pos;                   // anchor point in pixels
    Align anchorX = Align::Left;  // which side of the panel `pos.x` refers to
    bool anchorBottom = false;  // true: `pos.y` is the bottom edge
    std::string title;
    Color titleColor{255, 200, 60, 255};
    Color background{0, 0, 0, 150};
    std::vector<PanelLine> lines;
    float scale = 1.f;          // text scale
    float minWidth = 0.f;       // pixels
    float padding = 6.f;        // pixels (multiplied by scale)
};

class DrawList {
public:
    void panel(DrawPanel p) { panels.push_back(std::move(p)); }

    // 2D
    void text(Vec2 pos, std::string s, Color c, float scale = 1.f, bool shadow = true, Align align = Align::Left) {
        texts2D.push_back({pos, std::move(s), c, scale, shadow, align});
    }
    void rect(Vec2 pos, Vec2 size, Color c, bool filled = true) { rects2D.push_back({pos, size, c, filled}); }
    void line(Vec2 a, Vec2 b, Color c, float width = 1.f) { lines2D.push_back({a, b, c, width}); }
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color col) { triangles2D.push_back({a, b, c, col}); }

    // 3D
    void line3D(const Vec3& a, const Vec3& b, Color c, float width = 1.f, Occluder occ = {}) {
        lines3D.push_back({a, b, c, width, occ});
    }
    void polyline3D(const std::vector<Vec3>& pts, Color c, float width = 1.f, bool closed = false, Occluder occ = {});
    // Circle of `radius` around `center`, in the plane whose normal is `normal`.
    void circle3D(const Vec3& center, const Vec3& normal, float radius, Color c, float width = 1.f, int segments = 32,
                  Occluder occ = {});
    // Three orthogonal great circles: a cheap wire sphere.
    void sphere3D(const Vec3& center, float radius, Color c, float width = 1.f, int segments = 24, Occluder occ = {});
    // Small 3D cross marker.
    void marker3D(const Vec3& p, float size, Color c, float width = 1.f);
    // Oriented box wireframe (centre, basis, half extents).
    void box3D(const Vec3& center, const Basis& basis, const Vec3& halfExtent, Color c, float width = 1.f);
    void text3D(const Vec3& pos, std::string s, Color c, float scale = 1.f, Vec2 offset = {}, Align align = Align::Center) {
        texts3D.push_back({pos, offset, std::move(s), c, scale, true, align});
    }

    void clear() {
        texts2D.clear(); rects2D.clear(); lines2D.clear(); triangles2D.clear(); lines3D.clear(); texts3D.clear();
        panels.clear();
    }
    bool empty() const {
        return texts2D.empty() && rects2D.empty() && lines2D.empty() && triangles2D.empty() && lines3D.empty() &&
               texts3D.empty() && panels.empty();
    }

    // Draw order for the renderer: 3D lines, 3D texts, then 2D rects, triangles, lines, texts, panels.
    std::vector<DrawPanel> panels;
    std::vector<DrawText2D> texts2D;
    std::vector<DrawRect2D> rects2D;
    std::vector<DrawLine2D> lines2D;
    std::vector<DrawTriangle2D> triangles2D;
    std::vector<DrawLine3D> lines3D;
    std::vector<DrawText3D> texts3D;
};

} // namespace rlt
