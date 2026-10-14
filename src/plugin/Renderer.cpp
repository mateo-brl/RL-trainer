#include "plugin/Renderer.h"

#include <algorithm>
#include <cmath>

#include "core/common/HudLayout.h"
#include "core/common/Text.h"

namespace rlt {

namespace {
constexpr float kNearPlane = 10.f;  // uu in front of the camera

LinearColor toLinear(const Color& c) {
    LinearColor l;
    l.R = c.r;
    l.G = c.g;
    l.B = c.b;
    l.A = c.a;
    return l;
}

Vector2F toV2(const Vec2& v) { return Vector2F{v.x, v.y}; }

// Part of segment [a,b] hidden by the sphere (seen from the camera) is removed by sampling: the segment is
// split into pieces and a piece is dropped when its midpoint is behind the sphere.
bool pointOccluded(const Vec3& p, const Vec3& cam, const Occluder& o) {
    const Vec3 d = p - cam;
    const float len = length(d);
    if (len < 1e-3f) return false;
    const Vec3 dir = d / len;
    const Vec3 oc = o.center - cam;
    const float t = dot(oc, dir);           // closest approach along the ray
    if (t <= 0.f) return false;             // sphere behind the camera
    const float d2 = lengthSq(oc) - t * t;  // squared distance ray-centre
    const float r2 = o.radius * o.radius;
    if (d2 > r2) return false;              // ray misses the sphere
    const float tEnter = t - std::sqrt(r2 - d2);
    return tEnter < len - 1.f;              // the ray enters the sphere before reaching the point
}
} // namespace

void Renderer::drawLineClipped(CanvasWrapper& canvas, Vec3 a, Vec3 b, const ViewContext& view, float width) {
    const Vec3& fwd = view.camBasis.fwd;
    const float da = dot(a - view.camPos, fwd) - kNearPlane;
    const float db = dot(b - view.camPos, fwd) - kNearPlane;
    if (da < 0.f && db < 0.f) return;
    if (da < 0.f) a = a + (b - a) * (da / (da - db));
    else if (db < 0.f) b = b + (a - b) * (db / (db - da));
    const Vector2F pa = canvas.ProjectF(sdk::toVector(a));
    const Vector2F pb = canvas.ProjectF(sdk::toVector(b));
    // Skip absurd projections (numerical blow-ups near the plane).
    const float lim = 20000.f;
    if (std::fabs(pa.X) > lim || std::fabs(pa.Y) > lim || std::fabs(pb.X) > lim || std::fabs(pb.Y) > lim) return;
    canvas.DrawLine(pa, pb, width);
}

void Renderer::line3D(CanvasWrapper& canvas, const DrawLine3D& l, const ViewContext& view) {
    canvas.SetColor(toLinear(l.color));
    if (!l.occluder.enabled) {
        drawLineClipped(canvas, l.a, l.b, view, l.width);
        return;
    }
    // Split into pieces of ~8 uu and keep only the visible ones (merged into runs).
    const float len = distance(l.a, l.b);
    const int pieces = std::clamp(static_cast<int>(len / 8.f), 1, 16);
    int runStart = -1;
    for (int i = 0; i <= pieces; ++i) {
        bool visible = false;
        if (i < pieces) {
            const float tm = (static_cast<float>(i) + 0.5f) / static_cast<float>(pieces);
            visible = !pointOccluded(lerp(l.a, l.b, tm), view.camPos, l.occluder);
        }
        if (visible && runStart < 0) runStart = i;
        if (!visible && runStart >= 0) {
            const Vec3 a = lerp(l.a, l.b, static_cast<float>(runStart) / static_cast<float>(pieces));
            const Vec3 b = lerp(l.a, l.b, static_cast<float>(i) / static_cast<float>(pieces));
            drawLineClipped(canvas, a, b, view, l.width);
            runStart = -1;
        }
    }
}

void Renderer::text(CanvasWrapper& canvas, Vec2 pos, const std::string& s, const Color& c, float scale, bool shadow,
                    Align align, bool hudAccents) {
    const std::string t = hudText(s, hudAccents);
    if (align != Align::Left) {
        const Vector2F size = canvas.GetStringSize(t, scale, scale);
        pos.x -= align == Align::Center ? size.X * 0.5f : size.X;
    }
    canvas.SetColor(toLinear(c));
    canvas.SetPosition(toV2(pos));
    canvas.DrawString(t, scale, scale, shadow);
}

Renderer::PanelMetrics Renderer::measurePanel(CanvasWrapper& canvas, const DrawPanel& p, bool hudAccents) const {
    PanelMetrics m;
    m.scale = std::max(0.3f, p.scale);
    const float s = m.scale;
    m.pad = p.padding * s;
    m.barH = 5.f * s;
    float width = p.minWidth;
    float height = m.pad;
    m.title = hudText(p.title, hudAccents);
    if (!m.title.empty()) {
        const Vector2F ts = canvas.GetStringSize(m.title, s * 1.1f, s * 1.1f);
        width = std::max(width, ts.X);
        m.titleH = ts.Y + 2.f * s;
        height += m.titleH;
    }
    for (const auto& l : p.lines) {
        m.lines.push_back(hudText(l.text, hudAccents));
        const float ls = s * l.scale;
        const Vector2F ts = canvas.GetStringSize(m.lines.back().empty() ? " " : m.lines.back(), ls, ls);
        width = std::max(width, ts.X);
        float h = ts.Y + 1.f * s;
        if (l.bar >= 0.f) h += m.barH + 2.f * s;
        m.lineH.push_back(h);
        height += h;
    }
    m.width = width + 2.f * m.pad;
    m.height = height + m.pad;
    return m;
}

void Renderer::drawPanel(CanvasWrapper& canvas, const DrawPanel& p, const PanelMetrics& m, float x, float y) const {
    const float s = m.scale;
    if (p.background.a > 0.f) {
        canvas.SetColor(toLinear(p.background));
        canvas.SetPosition(Vector2F{x, y});
        canvas.FillBox(Vector2F{m.width, m.height});
    }
    float cy = y + m.pad;
    if (!m.title.empty()) {
        canvas.SetColor(toLinear(p.titleColor));
        canvas.SetPosition(Vector2F{x + m.pad, cy});
        canvas.DrawString(m.title, s * 1.1f, s * 1.1f, true);
        cy += m.titleH;
    }
    for (size_t i = 0; i < p.lines.size(); ++i) {
        const PanelLine& l = p.lines[i];
        const float ls = s * l.scale;
        canvas.SetColor(toLinear(l.color));
        canvas.SetPosition(Vector2F{x + m.pad, cy});
        canvas.DrawString(m.lines[i], ls, ls, true);
        if (l.bar >= 0.f) {
            const float by = cy + m.lineH[i] - m.barH - 1.f * s;
            const float bw = m.width - 2.f * m.pad;
            canvas.SetColor(LinearColor{60.f, 60.f, 60.f, 200.f});
            canvas.SetPosition(Vector2F{x + m.pad, by});
            canvas.FillBox(Vector2F{bw, m.barH});
            canvas.SetColor(toLinear(l.barColor));
            canvas.SetPosition(Vector2F{x + m.pad, by});
            canvas.FillBox(Vector2F{bw * std::clamp(l.bar, 0.f, 1.f), m.barH});
        }
        cy += m.lineH[i];
    }
}

void Renderer::panels(CanvasWrapper& canvas, const std::vector<DrawPanel>& list, const ViewContext& view,
                      bool hudAccents) const {
    if (list.empty()) return;
    // Measure everything first, then let the shared layout stack the panels and keep them off the boost gauge.
    std::vector<PanelMetrics> metrics;
    std::vector<HudRect> rects;
    metrics.reserve(list.size());
    rects.reserve(list.size());
    for (const auto& p : list) {
        metrics.push_back(measurePanel(canvas, p, hudAccents));
        const PanelMetrics& m = metrics.back();
        HudRect r;
        r.w = m.width;
        r.h = m.height;
        r.x = p.pos.x;
        if (p.anchorX == Align::Center) r.x -= m.width * 0.5f;
        else if (p.anchorX == Align::Right) r.x -= m.width;
        r.y = p.anchorBottom ? p.pos.y - m.height : p.pos.y;
        rects.push_back(r);
    }
    layoutHudPanels(rects, view.screenW, view.screenH);
    for (size_t i = 0; i < list.size(); ++i) drawPanel(canvas, list[i], metrics[i], rects[i].x, rects[i].y);
}

void Renderer::render(CanvasWrapper& canvas, const DrawList& list, const ViewContext& view, bool hudAccents) {
    if (view.valid) {
        for (const auto& l : list.lines3D) line3D(canvas, l, view);
        for (const auto& t : list.texts3D) {
            if (dot(t.pos - view.camPos, view.camBasis.fwd) < kNearPlane) continue;
            const Vector2F p = canvas.ProjectF(sdk::toVector(t.pos));
            text(canvas, Vec2{p.X + t.offset.x, p.Y + t.offset.y}, t.text, t.color, t.scale, t.shadow, t.align, hudAccents);
        }
    }
    for (const auto& r : list.rects2D) {
        canvas.SetColor(toLinear(r.color));
        canvas.SetPosition(toV2(r.pos));
        if (r.filled) canvas.FillBox(toV2(r.size));
        else canvas.DrawBox(toV2(r.size));
    }
    for (const auto& t : list.triangles2D) canvas.FillTriangle(toV2(t.a), toV2(t.b), toV2(t.c), toLinear(t.color));
    for (const auto& l : list.lines2D) {
        canvas.SetColor(toLinear(l.color));
        canvas.DrawLine(toV2(l.a), toV2(l.b), l.width);
    }
    for (const auto& t : list.texts2D) text(canvas, t.pos, t.text, t.color, t.scale, t.shadow, t.align, hudAccents);
    panels(canvas, list.panels, view, hudAccents);
}

} // namespace rlt
