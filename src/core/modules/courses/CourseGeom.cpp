#include "core/modules/courses/CourseGeom.h"

#include <algorithm>

namespace rlt::courses {

Ring makeDisk(const DVec3& c, const DVec3& n, double r, Surface s) {
    Ring g;
    g.center = c;
    g.normal = normalize(n);
    DVec3 u, w;
    perpBasis(g.normal, u, w);
    g.up = w;
    g.radius = r;
    g.shape = RingShape::Disk;
    g.surface = s;
    return g;
}

Ring makeGate(const DVec3& c, const DVec3& n, double halfWidth, double halfHeight) {
    Ring g;
    g.center = c;
    g.normal = normalize(DVec3{n.x, n.y, 0});  // gates stand vertically
    g.up = {0, 0, 1};
    g.radius = halfWidth;
    g.halfHeight = halfHeight;
    g.shape = RingShape::Gate;
    g.surface = Surface::Floor;
    return g;
}

double rimClearance(const Ring& r, const DVec3& x, const DVec3& dir) {
    const DVec3 w = r.up, u = cross(r.up, r.normal);
    auto d2 = [&](double phi) {
        const DVec3 q = r.center + u * (std::cos(phi) * r.radius) + w * (std::sin(phi) * r.radius) - x;
        const double a = dot(q, dir);
        return dot(q, q) - a * a;
    };
    constexpr int kN = 64;
    const double step = 2.0 * phys::kPi / kN;
    int best = 0;
    double bestV = d2(0.0);
    for (int i = 1; i < kN; ++i) {
        const double v = d2(i * step);
        if (v < bestV) {
            bestV = v;
            best = i;
        }
    }
    double lo = (best - 1) * step, hi = (best + 1) * step;
    for (int it = 0; it < 40; ++it) {
        const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
        if (d2(m1) < d2(m2)) hi = m2;
        else lo = m1;
    }
    bestV = std::min(bestV, d2(0.5 * (lo + hi)));
    return std::sqrt(std::max(0.0, bestV));
}

PassResult testSegment(const Ring& ring, const DVec3& p0, const DVec3& p1, double objRadius) {
    PassResult res;
    const double d0 = dot(p0 - ring.center, ring.normal);
    const double d1 = dot(p1 - ring.center, ring.normal);
    const bool fwd = d0 < 0.0 && d1 >= 0.0;
    const bool bwd = d1 < 0.0 && d0 >= 0.0;
    if (!fwd && !bwd) return res;
    const double t = d0 / (d0 - d1);  // signs differ, so the denominator is not 0
    const DVec3 x = p0 + (p1 - p0) * t;
    const DVec3 rel = x - ring.center;
    const DVec3 dir = normalize(p1 - p0);
    res.t = t;
    res.point = x;
    bool inside = false, clean = false;
    if (ring.shape == RingShape::Disk) {
        const DVec3 inPlane = rel - ring.normal * dot(rel, ring.normal);
        res.radial = length(inPlane);
        inside = res.radial <= ring.radius;
        clean = inside && (objRadius <= 0.0 || rimClearance(ring, x, dir) >= objRadius);
    } else {
        const DVec3 side = cross(ring.up, ring.normal);
        const double u = dot(rel, side), v = dot(rel, ring.up);
        res.radial = std::fabs(u);
        res.vertical = v;
        inside = std::fabs(v) <= ring.halfHeight && res.radial <= ring.radius;
        const DVec3 dh = normalize(DVec3{dir.x, dir.y, 0.0});
        const double cosH = std::fabs(dot(dh, ring.normal));
        clean = inside && (ring.radius - res.radial) * cosH >= objRadius;
    }
    if (bwd) {
        res.kind = inside ? PassKind::Backward : PassKind::None;
        return res;
    }
    res.kind = clean ? PassKind::Clean : (inside ? PassKind::Grazed : PassKind::Outside);
    return res;
}

DVec3 hermite(const DVec3& p0, const DVec3& v0, const DVec3& p1, const DVec3& v1, double dt, double s) {
    const double s2 = s * s, s3 = s2 * s;
    return p0 * (2 * s3 - 3 * s2 + 1) + v0 * (dt * (s3 - 2 * s2 + s)) + p1 * (-2 * s3 + 3 * s2) + v1 * (dt * (s3 - s2));
}

PassResult testHermite(const Ring& ring, const DVec3& p0, const DVec3& v0, const DVec3& p1, const DVec3& v1,
                       double dt, double objRadius, int maxSub) {
    const int n = std::clamp(static_cast<int>(std::ceil(dt * phys::kTickRate - 1e-9)), 1, std::max(1, maxSub));
    if (n == 1) return testSegment(ring, p0, p1, objRadius);
    DVec3 a = p0;
    for (int i = 1; i <= n; ++i) {
        const double s = static_cast<double>(i) / n;
        const DVec3 b = (i == n) ? p1 : hermite(p0, v0, p1, v1, dt, s);
        PassResult r = testSegment(ring, a, b, objRadius);
        if (r.kind != PassKind::None) {
            r.t = (static_cast<double>(i - 1) + r.t) / n;
            return r;
        }
        a = b;
    }
    return {};
}

bool isNearMiss(const Ring& ring, const PassResult& pr) {
    if (pr.kind != PassKind::Outside) return false;
    if (ring.shape == RingShape::Gate)
        return pr.radial <= ring.radius + 500.0 && std::fabs(pr.vertical) <= ring.halfHeight + 500.0;
    return pr.radial <= std::max(ring.radius * 2.5, ring.radius + 500.0);
}

double ringExtent(const Ring& r, const DVec3& axis) {
    if (r.shape == RingShape::Gate) {
        const DVec3 side = cross(r.up, r.normal);
        return r.radius * std::fabs(dot(axis, side)) + r.halfHeight * std::fabs(dot(axis, r.up));
    }
    const double an = dot(axis, r.normal);
    return r.radius * std::sqrt(std::max(0.0, 1.0 - an * an));
}

bool ringInsideArena(const Ring& r, double clearance) {
    const DVec3& c = r.center;
    const double ex = ringExtent(r, {1, 0, 0}), ey = ringExtent(r, {0, 1, 0}), ez = ringExtent(r, {0, 0, 1});
    const bool skipFloor = r.surface == Surface::Floor;
    const bool skipPosX = r.surface == Surface::WallPosX, skipNegX = r.surface == Surface::WallNegX;
    if (!skipPosX && c.x + ex > phys::kArenaX - clearance) return false;
    if (!skipNegX && c.x - ex < -phys::kArenaX + clearance) return false;
    if (std::fabs(c.x) > phys::kArenaX) return false;  // the centre itself is never outside
    if (std::fabs(c.y) + ey > phys::kArenaY - clearance) return false;
    if (!skipFloor && c.z - ez < clearance) return false;
    if (c.z < 0.0) return false;
    if (c.z + ez > phys::kCeilingZ - clearance) return false;
    // Corner chamfer: plane (|x| + |y|) / sqrt2 = 8064 / sqrt2.
    const double s2 = std::sqrt(2.0);
    const DVec3 a{(c.x >= 0 ? 1.0 : -1.0) / s2, (c.y >= 0 ? 1.0 : -1.0) / s2, 0.0};
    if ((std::fabs(c.x) + std::fabs(c.y)) / s2 + ringExtent(r, a) > phys::kCornerSum / s2 - clearance) return false;
    return true;
}

} // namespace rlt::courses
