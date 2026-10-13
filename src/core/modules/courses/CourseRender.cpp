#include "core/modules/courses/CourseRender.h"

#include <algorithm>
#include <cmath>

namespace rlt::courses {

float projectedRadiusPx(float radius, float depth, float hfovDeg, float screenW) {
    if (depth <= 1e-3f) return 1e9f;
    const float half = std::clamp(hfovDeg, 10.f, 170.f) * 0.5f * kDegToRad;
    return radius * (screenW * 0.5f) / std::tan(half) / depth;
}

int circleSegments(float radiusPx, float maxErrPx, int nMin, int nMax) {
    const float e = std::max(0.05f, maxErrPx);
    const float n = kPi * std::sqrt(std::max(0.f, std::min(radiusPx, 1e6f)) / (2.f * e));
    return std::clamp(static_cast<int>(std::ceil(n)), nMin, nMax);
}

float heightAboveSurface(const Ring& r, const Vec3& p) {
    switch (r.surface) {
    case Surface::Floor: return p.z;
    case Surface::WallPosX: return static_cast<float>(phys::kArenaX) - p.x;
    case Surface::WallNegX: return p.x + static_cast<float>(phys::kArenaX);
    default: return 1e9f;
    }
}

namespace {

int clippedLine(const Ring& r, Vec3 a, Vec3 b, Color c, float width, const Occluder& occ, DrawList& out) {
    const float ha = heightAboveSurface(r, a), hb = heightAboveSurface(r, b);
    if (ha < 0.f && hb < 0.f) return 0;
    if (ha < 0.f) a = a + (b - a) * (ha / (ha - hb));
    else if (hb < 0.f) b = b + (a - b) * (hb / (hb - ha));
    out.line3D(a, b, c, width, occ);
    return 1;
}

} // namespace

int appendRingLines(const Ring& r, int segments, Color c, float width, const Occluder& occ, DrawList& out) {
    const Vec3 center = toF(r.center);
    const Vec3 up = toF(r.up);
    const Vec3 normal = toF(r.normal);
    if (r.shape == RingShape::Gate) {
        const Vec3 side = cross(up, normal);
        const float hw = static_cast<float>(r.radius), hh = static_cast<float>(r.halfHeight);
        const Vec3 p0 = center - side * hw - up * hh, p1 = center - side * hw + up * hh;
        const Vec3 p2 = center + side * hw + up * hh, p3 = center + side * hw - up * hh;
        out.line3D(p0, p1, c, width, occ);
        out.line3D(p1, p2, c, width, occ);
        out.line3D(p2, p3, c, width, occ);
        out.line3D(p3, p0, c, width * 0.6f, occ);
        return 4;
    }
    segments = std::clamp(segments, 3, 128);
    const Vec3 w = up;
    const Vec3 u = cross(w, normal);
    const float radius = static_cast<float>(r.radius);
    int added = 0;
    Vec3 prev = center + u * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 p = center + (u * std::cos(a) + w * std::sin(a)) * radius;
        added += clippedLine(r, prev, p, c, width, occ, out);
        prev = p;
    }
    return added;
}

int appendDashedLine(const Vec3& a, const Vec3& b, int pieces, Color c, float width, DrawList& out) {
    pieces = std::clamp(pieces, 1, 40);
    int added = 0;
    for (int i = 0; i < pieces; i += 2) {
        const float t0 = static_cast<float>(i) / static_cast<float>(pieces);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(pieces);
        out.line3D(lerp(a, b, t0), lerp(a, b, t1), c, width);
        ++added;
    }
    return added;
}

} // namespace rlt::courses
