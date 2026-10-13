// Pass detection, gates, Hermite arcs, arena bounds, render helpers (ported from the research reference tests).

#include "tests/TestFramework.h"

#include "core/common/DrawList.h"
#include "core/common/Rng.h"
#include "core/modules/courses/CourseGen.h"
#include "core/modules/courses/CourseGeom.h"
#include "core/modules/courses/CourseRender.h"

using namespace rlt;
using namespace rlt::courses;

namespace {
bool nearV(const DVec3& a, const DVec3& b, double eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
} // namespace

TEST(courses_rng_matches_pcg32_reference_vector) {
    // rlt::Rng is the same PCG32 as the reference (seed 42, stream 54).
    Rng r(42u, 54u);
    const uint32_t expect[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expect) CHECK_EQ(r.next(), e);
    CHECK_EQ(splitmix64(0), mix64(0));
}

TEST(courses_segment_through_centre_is_clean) {
    const Ring ring = makeDisk({100, 200, 800}, {0, 1, 0}, 300);
    const PassResult r = testSegment(ring, {100, 192, 800}, {100, 208.6667, 800}, 50);
    CHECK(r.kind == PassKind::Clean);
    CHECK_NEAR(r.t, 8.0 / 16.6667, 1e-4);
    CHECK_NEAR(r.radial, 0.0, 1e-9);
}

TEST(courses_segment_misses_and_none) {
    const Ring ring = makeDisk({100, 200, 800}, {0, 1, 0}, 300);
    const PassResult miss = testSegment(ring, {500, 150, 800}, {500, 250, 800}, 50);  // 400 off centre
    CHECK(miss.kind == PassKind::Outside);
    CHECK(!passCounts(miss.kind));
    CHECK(isNearMiss(ring, miss));  // 400 < 2.5 R: a real miss
    const PassResult far = testSegment(ring, {2000, 150, 800}, {2000, 250, 800}, 50);
    CHECK(far.kind == PassKind::Outside);
    CHECK(!isNearMiss(ring, far));  // crossing of the infinite plane far away: ignored
    CHECK(testSegment(ring, {100, 100, 800}, {100, 190, 800}, 50).kind == PassKind::None);  // stops before
    CHECK(testSegment(ring, {0, 200, 800}, {200, 200, 800}, 50).kind == PassKind::None);    // in the plane
    const double o = 301.0 / std::sqrt(2.0);
    CHECK(testSegment(ring, {100 + o, 190, 800 + o}, {100 + o, 210, 800 + o}, 50).kind == PassKind::Outside);
}

TEST(courses_segment_grazed_band_and_oblique) {
    const Ring ring = makeDisk({100, 200, 800}, {0, 1, 0}, 300);
    CHECK(testSegment(ring, {380, 190, 800}, {380, 210, 800}, 50).kind == PassKind::Grazed);  // rho 280
    CHECK(testSegment(ring, {349, 190, 800}, {349, 210, 800}, 50).kind == PassKind::Clean);   // rho 249
    auto through = [&](double thetaDeg, double rho) {
        const double th = degToRad(thetaDeg);
        const DVec3 dir{std::sin(th), std::cos(th), 0};
        const DVec3 x{100 + rho, 200, 800};
        return testSegment(ring, x - dir * 10, x + dir * 10, 50);
    };
    const double c60 = rimClearance(ring, {100, 200, 800}, normalize(DVec3{std::sin(degToRad(60)), std::cos(degToRad(60)), 0}));
    CHECK_NEAR(c60, 150.0, 1e-6);  // R cos 60
    CHECK(through(80, 0).kind == PassKind::Clean);    // 300 cos80 = 52.1 >= 50
    CHECK(through(81, 0).kind == PassKind::Grazed);   // 46.9 < 50
    CHECK(through(0, 240).kind == PassKind::Clean);
    CHECK(through(45, 240).kind == PassKind::Grazed);
    CHECK_NEAR(rimClearance(ring, {340, 200, 800}, {0, 1, 0}), 60.0, 1e-6);
    // 45 deg in the YZ plane through a point 100 off centre.
    const DVec3 hit{100, 200, 900};
    const DVec3 dir = normalize(DVec3{0, 1, 1});
    const PassResult r = testSegment(ring, hit - dir * 10, hit + dir * 30, 50);
    CHECK(r.kind == PassKind::Clean);
    CHECK_NEAR(r.t, 0.25, 1e-9);
    CHECK(nearV(r.point, hit, 1e-9));
    CHECK_NEAR(r.radial, 100.0, 1e-9);
}

TEST(courses_segment_backward_and_half_open_rule) {
    const Ring ring = makeDisk({100, 200, 800}, {0, 1, 0}, 300);
    const PassResult back = testSegment(ring, {100, 210, 800}, {100, 190, 800}, 50);
    CHECK(back.kind == PassKind::Backward);
    CHECK(!passCounts(back.kind));
    // A sample exactly on the plane is counted by exactly one of the two segments.
    const DVec3 a{100, 190, 800}, b{100, 200, 800}, c{100, 210, 800};
    CHECK(passCounts(testSegment(ring, a, b, 50).kind));
    CHECK(testSegment(ring, b, c, 50).kind == PassKind::None);
}

TEST(courses_tilted_ring) {
    const Ring t = makeDisk({0, 0, 1000}, normalize(DVec3{1, 1, 0.5}), 250);
    const DVec3 n = t.normal;
    CHECK(testSegment(t, t.center - n * 5, t.center + n * 5, 50).kind == PassKind::Clean);
    DVec3 u, w;
    perpBasis(n, u, w);
    CHECK(testSegment(t, t.center + u * 260 - n * 5, t.center + u * 260 + n * 5, 50).kind == PassKind::Outside);
    CHECK(testSegment(t, t.center + w * 230 - n * 5, t.center + w * 230 + n * 5, 50).kind == PassKind::Grazed);
}

TEST(courses_gate_pass_detection) {
    const Ring g = makeGate({0, 0, 230}, {0, 1, 0}, 300, 230);
    CHECK(testSegment(g, {0, -5, 150}, {0, 5, 150}, phys::kBallRadius).kind == PassKind::Clean);
    CHECK(testSegment(g, {250, -5, 150}, {250, 5, 150}, phys::kBallRadius).kind == PassKind::Grazed);
    CHECK(testSegment(g, {320, -5, 150}, {320, 5, 150}, phys::kBallRadius).kind == PassKind::Outside);
    CHECK(testSegment(g, {0, -5, 500}, {0, 5, 500}, phys::kBallRadius).kind == PassKind::Outside);  // popped too high
    CHECK(testSegment(g, {0, 5, 150}, {0, -5, 150}, phys::kBallRadius).kind == PassKind::Backward);
    const DVec3 d60{std::sin(degToRad(60)), std::cos(degToRad(60)), 0};
    CHECK(testSegment(g, DVec3{100, 0, 150} - d60 * 5, DVec3{100, 0, 150} + d60 * 5, phys::kBallRadius).kind == PassKind::Clean);
    CHECK(testSegment(g, DVec3{150, 0, 150} - d60 * 5, DVec3{150, 0, 150} + d60 * 5, phys::kBallRadius).kind == PassKind::Grazed);
    // Gates stand vertically even when built from a tilted direction.
    const Ring tilted = makeGate({0, 0, 230}, {0, 1, 1}, 300, 230);
    CHECK_NEAR(tilted.normal.z, 0.0, 1e-12);
    // Near-miss window of gates.
    const PassResult wide = testSegment(g, {700, -5, 150}, {700, 5, 150}, phys::kBallRadius);
    CHECK(wide.kind == PassKind::Outside && isNearMiss(g, wide));
    const PassResult wider = testSegment(g, {1200, -5, 150}, {1200, 5, 150}, phys::kBallRadius);
    CHECK(wider.kind == PassKind::Outside && !isNearMiss(g, wider));
}

TEST(courses_hermite_long_gap) {
    // Ring centred on the TRUE crossing point of a ballistic arc; one 1 s gap between samples.
    const Ring ring = makeDisk({0, 1000, 618.75}, {0, 1, 0}, 50);
    const DVec3 p0{0, 0, 200}, v0{0, 2000, 1000};
    const double dt = 1.0;
    const DVec3 p1 = p0 + v0 * dt + DVec3{0, 0, -0.5 * phys::kGravity * dt * dt};
    const DVec3 v1 = v0 + DVec3{0, 0, -phys::kGravity * dt};
    const PassResult chord = testSegment(ring, p0, p1, 0.0);
    const PassResult arc = testHermite(ring, p0, v0, p1, v1, dt, 0.0);
    CHECK(chord.kind == PassKind::Outside);  // a straight chord would report a miss
    CHECK_NEAR(chord.radial, 81.25, 1e-9);
    CHECK(arc.kind == PassKind::Clean);
    CHECK_NEAR(arc.t, 0.5, 1e-9);
    CHECK_NEAR(arc.radial, 0.0, 1e-6);  // cubic Hermite reproduces a parabola
    // Short gaps fall back to the plain segment test.
    CHECK(testHermite(ring, {0, 990, 618.75}, {0, 1200, 0}, {0, 1010, 618.75}, {0, 1200, 0}, 1.0 / 120.0, 0.0).kind ==
          PassKind::Clean);
}

TEST(courses_arena_bounds) {
    CHECK(ringInsideArena(makeDisk({0, 0, 1000}, {0, 1, 0}, 300), 150));
    CHECK(!ringInsideArena(makeDisk({3800, 0, 1000}, {0, 1, 0}, 300), 150));   // through the side wall
    CHECK(!ringInsideArena(makeDisk({0, 4900, 1000}, {1, 0, 0}, 300), 150));   // through the back wall
    CHECK(!ringInsideArena(makeDisk({0, 0, 1900}, {0, 1, 0}, 300), 150));      // through the ceiling
    CHECK(!ringInsideArena(makeDisk({0, 0, 200}, {0, 1, 0}, 300), 150));       // through the floor
    CHECK(!ringInsideArena(makeDisk({3500, 4300, 800}, {1, -1, 0}, 300), 150)); // corner chamfer
    CHECK(ringInsideArena(makeDisk({3000, 3900, 800}, {1, 1, 0}, 300), 150));   // in the corner, clear of it
    // A floor arch may sink into the floor (its own surface) but not into anything else.
    CHECK(ringInsideArena(makeDisk({0, 0, 165}, {0, 1, 0}, 300, Surface::Floor), 120));
    // Ring lying flat (normal up): extent along z is 0.
    CHECK_NEAR(ringExtent(makeDisk({0, 0, 1000}, {0, 0, 1}, 300), {0, 0, 1}), 0.0, 1e-9);
}

TEST(courses_render_helpers) {
    const int nFar = circleSegments(projectedRadiusPx(300, 8000, 110, 1920));
    const int nMid = circleSegments(projectedRadiusPx(300, 1500, 110, 1920));
    const int nNear = circleSegments(projectedRadiusPx(300, 150, 110, 1920));
    CHECK(nFar >= 8 && nMid > nFar && nNear == 64);
    CHECK(circleSegments(projectedRadiusPx(300, 60000, 110, 1920)) == 8);
    CHECK(circleSegments(projectedRadiusPx(300, 0, 110, 1920)) == 64);  // depth 0: max detail, no blow-up
    DrawList dl;
    const Ring air = makeDisk({0, 0, 500}, {0, 1, 0}, 300);
    CHECK_EQ(appendRingLines(air, 32, colors::White, 2.f, {}, dl), 32);
    bool onRim = true;
    for (const auto& l : dl.lines3D) {
        const float d = distance(l.a, Vec3{0.f, 0.f, 500.f});
        onRim = onRim && std::fabs(d - 300.f) < 0.5f && std::fabs(l.a.y) < 1e-3f;
    }
    CHECK(onRim);
    // Floor arch: the buried part is clipped at z = 0.
    DrawList arch;
    Ring a = makeDisk({0, 0, 150}, {0, 1, 0}, 300, Surface::Floor);
    a.up = {0, 0, 1};
    const int n = appendRingLines(a, 64, colors::White, 2.f, {}, arch);
    CHECK(n > 0 && n < 64);
    bool above = true;
    for (const auto& l : arch.lines3D) above = above && l.a.z > -1e-3f && l.b.z > -1e-3f;
    CHECK(above);
    // Wall arch on +X: nothing beyond x = 4096.
    DrawList wall;
    Ring w = makeDisk({4096 - 150, 0, 900}, {0, 1, 0}, 300, Surface::WallPosX);
    w.up = {-1, 0, 0};
    appendRingLines(w, 64, colors::White, 2.f, {}, wall);
    bool inside = !wall.lines3D.empty();
    for (const auto& l : wall.lines3D) inside = inside && l.a.x <= 4096.01f && l.b.x <= 4096.01f;
    CHECK(inside);
    // Gate: 4 lines.
    DrawList gate;
    CHECK_EQ(appendRingLines(makeGate({0, 0, 230}, {0, 1, 0}, 300, 230), 64, colors::White, 2.f, {}, gate), 4);
    DrawList dash;
    CHECK_EQ(appendDashedLine({0, 0, 0}, {1000, 0, 0}, 10, colors::White, 1.f, dash), 5);
}
