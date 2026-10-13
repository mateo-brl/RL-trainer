#include "core/common/DrawList.h"

#include <cmath>

namespace rlt {

void DrawList::polyline3D(const std::vector<Vec3>& pts, Color c, float width, bool closed, Occluder occ) {
    if (pts.size() < 2) return;
    for (size_t i = 0; i + 1 < pts.size(); ++i) line3D(pts[i], pts[i + 1], c, width, occ);
    if (closed && pts.size() > 2) line3D(pts.back(), pts.front(), c, width, occ);
}

void DrawList::circle3D(const Vec3& center, const Vec3& normal, float radius, Color c, float width, int segments,
                        Occluder occ) {
    if (segments < 3) segments = 3;
    Vec3 u, w;
    perpendicularFrame(normal, u, w);
    Vec3 prev = center + u * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 p = center + (u * std::cos(a) + w * std::sin(a)) * radius;
        line3D(prev, p, c, width, occ);
        prev = p;
    }
}

void DrawList::sphere3D(const Vec3& center, float radius, Color c, float width, int segments, Occluder occ) {
    circle3D(center, {1.f, 0.f, 0.f}, radius, c, width, segments, occ);
    circle3D(center, {0.f, 1.f, 0.f}, radius, c, width, segments, occ);
    circle3D(center, {0.f, 0.f, 1.f}, radius, c, width, segments, occ);
}

void DrawList::marker3D(const Vec3& p, float size, Color c, float width) {
    line3D(p - Vec3{size, 0.f, 0.f}, p + Vec3{size, 0.f, 0.f}, c, width);
    line3D(p - Vec3{0.f, size, 0.f}, p + Vec3{0.f, size, 0.f}, c, width);
    line3D(p - Vec3{0.f, 0.f, size}, p + Vec3{0.f, 0.f, size}, c, width);
}

void DrawList::box3D(const Vec3& center, const Basis& b, const Vec3& h, Color c, float width) {
    Vec3 corners[8];
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                corners[k++] = center + b.fwd * (h.x * static_cast<float>(sx)) + b.right * (h.y * static_cast<float>(sy)) +
                               b.up * (h.z * static_cast<float>(sz));
    // Index = (sx>0)*4 + (sy>0)*2 + (sz>0)
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) line3D(corners[e[0]], corners[e[1]], c, width);
}

} // namespace rlt
