// Geometry of the shooting module: roof spawn, ball on roof, goal frame (both teams), zones, aim scoring,
// ball prediction to the goal plane (ballistic, rolling, bouncing, walls).

#include <cmath>

#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

// ---- Roof spawn -------------------------------------------------------------------------------------------
TEST(shooting_roof_spawn_default_octane_at_rest) {
    const CarState car = carAt({0.f, 0.f, 17.f});
    const Hitbox hb = hitboxOf(car);
    const Vec3 p = roofSpawn(car.pos, car.basis, hb, 91.25f, 2.f);
    CHECK_NEAR(p.x, 13.88f, 1e-3f);
    CHECK_NEAR(p.y, 0.f, 1e-3f);
    // 17 (origin) + 20.75 (hitbox z offset) + 18.08 (half height) + 91.25 (radius) + 2 (gap)
    CHECK_NEAR(p.z, 17.f + 20.75f + 18.08f + 91.25f + 2.f, 1e-3f);
    CHECK_NEAR(hb.roofZ(), 38.83f, 1e-3f);
    const Vec3 rc = roofCenter(car.pos, car.basis, hb);
    CHECK_NEAR(p.z - rc.z, 91.25f + 2.f, 1e-3f); // ball centre exactly one radius + gap above the roof
}

TEST(shooting_roof_spawn_rotated_car_stays_above_roof) {
    const Rot rots[] = {rotDeg(30.f, 120.f, -45.f), rotDeg(-80.f, -30.f, 170.f), rotDeg(0.f, 90.f, 90.f),
                        rotDeg(10.f, 200.f, 0.f)};
    for (const Rot& r : rots) {
        CarState car = carAt({1200.f, -800.f, 400.f}, r);
        car.hitboxExtent = {130.f, 85.f, 40.f}; // another body (Dominus-like)
        car.hitboxOffset = {9.f, 0.f, 15.75f};
        const Hitbox hb = hitboxOf(car);
        const Vec3 p = roofSpawn(car.pos, car.basis, hb, 91.25f, 2.f);
        const Vec3 local = car.basis.toLocal(p - car.pos);
        CHECK_NEAR(local.x, 9.f, 1e-2f);
        CHECK_NEAR(local.y, 0.f, 1e-2f);
        CHECK_NEAR(local.z, 15.75f + 20.f + 91.25f + 2.f, 1e-2f);
        const Vec3 off = roofOffset(p, car.pos, car.basis, hb, 91.25f);
        CHECK_NEAR(off.x, 0.f, 1e-2f);
        CHECK_NEAR(off.y, 0.f, 1e-2f);
        CHECK_NEAR(off.z, 2.f, 1e-2f);
        CHECK(ballOnRoof(p, car.pos, car.basis, hb, 91.25f));
        // Raised by 100 uu along the car up axis, or pushed sideways off the roof: not on the roof.
        CHECK(!ballOnRoof(p + car.basis.up * 100.f, car.pos, car.basis, hb, 91.25f));
        CHECK(!ballOnRoof(p + car.basis.right * 120.f, car.pos, car.basis, hb, 91.25f));
        // Slightly off-centre but still over the roof.
        CHECK(ballOnRoof(p + car.basis.fwd * 40.f, car.pos, car.basis, hb, 91.25f));
    }
}

TEST(shooting_hitbox_fallback_on_invalid_sizes) {
    CarState car = carAt({0.f, 0.f, 17.f});
    car.hitboxExtent = {0.f, 0.f, 0.f};
    const Hitbox hb = hitboxOf(car);
    CHECK_NEAR(hb.extent.x, 118.01f, 1e-3f);
    CHECK_NEAR(hb.offset.z, 20.75f, 1e-3f);
}

// ---- Goal frame / zones -------------------------------------------------------------------------------------
TEST(shooting_goal_frame_both_teams) {
    const GoalFrame blue = GoalFrame::forTeam(0), orange = GoalFrame::forTeam(1);
    CHECK_NEAR(blue.planeY(), 5120.f, 1e-3f);
    CHECK_NEAR(orange.planeY(), -5120.f, 1e-3f);
    // Attacker's right: facing +Y the right is -X; facing -Y the right is +X (matches basisFromRot).
    CHECK_NEAR(blue.right().x, basisFromRot(rotDeg(0.f, 90.f, 0.f)).right.x, 1e-4f);
    CHECK_NEAR(orange.right().x, basisFromRot(rotDeg(0.f, -90.f, 0.f)).right.x, 1e-4f);
    for (const GoalFrame& g : {blue, orange}) {
        const Vec3 w = g.toWorld(-300.f, 450.f);
        const Vec2 back = g.toPlane(w);
        CHECK_NEAR(back.x, -300.f, 1e-3f);
        CHECK_NEAR(back.y, 450.f, 1e-3f);
        CHECK_NEAR(std::fabs(w.y), 5120.f, 1e-3f);
        CHECK_NEAR(g.distanceToPlane(Vec3{0.f, g.sign * 5000.f, 0.f}), 120.f, 1e-3f);
    }
    // The top left zone is on +X for blue and -X for orange.
    const ZoneRect tl = zoneRect(Zone::TopLeft, 0.5f);
    CHECK(blue.toWorld(tl.u, tl.z).x > 0.f);
    CHECK(orange.toWorld(tl.u, tl.z).x < 0.f);
}

TEST(shooting_zones_symmetric_inside_goal_and_shrinking) {
    for (float level : {0.f, 0.5f, 1.f}) {
        const ZoneRect tl = zoneRect(Zone::TopLeft, level), tr = zoneRect(Zone::TopRight, level);
        const ZoneRect bl = zoneRect(Zone::LowLeft, level), br = zoneRect(Zone::LowRight, level);
        const ZoneRect c = zoneRect(Zone::Center, level);
        CHECK_NEAR(tl.u, -tr.u, 1e-3f);
        CHECK_NEAR(bl.u, -br.u, 1e-3f);
        CHECK(tl.u < 0.f && bl.u < 0.f); // left = attacker's left
        CHECK_NEAR(tl.z, tr.z, 1e-3f);
        for (const ZoneRect& r : {tl, tr, bl, br, c}) {
            CHECK(std::fabs(r.u) + r.hw <= kOnTargetHalfU + 1e-2f); // inside the goal mouth
            CHECK(r.z + r.hh <= kOnTargetTopZ + 1e-2f);
            CHECK(r.z - r.hh >= kZoneFloorZ - 40.f);
        }
        // Corner zones touch their corner whatever the size.
        CHECK_NEAR(tr.u + tr.hw, kOnTargetHalfU, 1e-2f);
        CHECK_NEAR(tr.z + tr.hh, kOnTargetTopZ, 1e-2f);
        CHECK_NEAR(br.z - br.hh, kZoneFloorZ, 1e-2f);
        // A rolling ball (centre at 93.15) can hit the low zones.
        CHECK(bl.contains({bl.u, kBallRestZ}));
    }
    CHECK(zoneRect(Zone::TopLeft, 1.f).hw < zoneRect(Zone::TopLeft, 0.f).hw);
    CHECK(zoneRect(Zone::Center, 1.f).hh < zoneRect(Zone::Center, 0.f).hh);
    // At the easiest level the centre zone reaches a rolling ball.
    CHECK(zoneRect(Zone::Center, 0.f).contains({0.f, kBallRestZ}));
}

TEST(shooting_zone_parsing_and_labels) {
    int z = -1;
    CHECK(parseZone("tl", z) && z == 0);
    CHECK(parseZone("TR", z) && z == 1);
    CHECK(parseZone("bl", z) && z == 2);
    CHECK(parseZone("br", z) && z == 3);
    CHECK(parseZone("center", z) && z == 4);
    CHECK(parseZone("centre", z) && z == 4);
    CHECK(parseZone("random", z) && z == kZoneRandom);
    CHECK(parseZone("aleatoire", z) && z == kZoneRandom);
    CHECK(parseZone("lucarne-gauche", z) && z == 0);
    CHECK(!parseZone("nowhere", z));
    CHECK(std::string(zoneLabel(Zone::TopLeft)) == "lucarne gauche");
    CHECK(std::string(zoneLabel(Zone::LowRight)) == "poteau bas droit");
    CHECK(std::string(zoneId(Zone::Center)) == "center");
}

TEST(shooting_zone_distances) {
    const ZoneRect r{100.f, 300.f, 50.f, 40.f};
    CHECK_NEAR(r.normDistance({100.f, 300.f}), 0.f, 1e-5f);
    CHECK_NEAR(r.normDistance({150.f, 300.f}), 1.f, 1e-5f);
    CHECK_NEAR(r.edgeDistance({130.f, 320.f}), 0.f, 1e-5f);
    CHECK_NEAR(r.edgeDistance({180.f, 300.f}), 30.f, 1e-4f);
    CHECK_NEAR(r.edgeDistance({180.f, 380.f}), 50.f, 1e-3f); // 30-40-50 triangle
    CHECK_NEAR(r.centreDistance({130.f, 340.f}), 50.f, 1e-3f);
}

TEST(shooting_score_aim_bands) {
    const ZoneRect zone = zoneRect(Zone::TopRight, 0.5f);
    const AimResult centre = scoreAim(zone, true, {zone.u, zone.z});
    CHECK(centre.inZone && centre.onGoal);
    CHECK_NEAR(centre.score01, 1.f, 1e-4f);
    const AimResult edge = scoreAim(zone, true, {zone.u - zone.hw, zone.z});
    CHECK(edge.inZone);
    CHECK_NEAR(edge.score01, 0.7f, 1e-3f);
    const AimResult goalNotZone = scoreAim(zone, true, {-300.f, 200.f});
    CHECK(!goalNotZone.inZone && goalNotZone.onGoal);
    CHECK(goalNotZone.score01 >= 0.45f && goalNotZone.score01 <= 0.65f);
    const AimResult wide = scoreAim(zone, true, {1300.f, 300.f});
    CHECK(!wide.onGoal && !wide.inZone);
    CHECK(wide.score01 < 0.4f);
    CHECK(wide.du > 0.f); // to the right of the zone centre
    const AimResult over = scoreAim(zone, true, {zone.u, 800.f});
    CHECK(!over.onGoal);
    CHECK(over.dz > 0.f);
    const AimResult none = scoreAim(zone, false, {});
    CHECK(!none.reached && none.score01 == 0.f);
    // Monotonic: closer is better.
    CHECK(scoreAim(zone, true, {zone.u + 20.f, zone.z}).score01 >
          scoreAim(zone, true, {zone.u + 60.f, zone.z}).score01);
    // The goal mouth rectangle covers every zone.
    const ZoneRect mouth = goalMouthRect();
    CHECK(mouth.contains({zone.u, zone.z}));
    CHECK(mouth.contains({0.f, kBallRestZ}));
    CHECK(!mouth.contains({0.f, 700.f}));
}

// ---- Prediction -------------------------------------------------------------------------------------------
TEST(shooting_predict_ballistic_matches_analytic_drag) {
    const Vec3 p0{0.f, 2000.f, 300.f}, v0{0.f, 3000.f, 600.f};
    const auto c = predictToPlaneY(p0, v0, Vec3{}, 5120.f);
    CHECK(c.has_value());
    if (!c) return;
    // Continuous solution with linear drag k = 0.03: y(t) = v0/k (1 - e^-kt),
    // z(t) = z0 + (vz0 + g/k)(1 - e^-kt)/k - g t / k.
    const double k = 0.03, g = 650.0;
    const double t = -std::log(1.0 - k * 3120.0 / 3000.0) / k;
    const double e = 1.0 - std::exp(-k * t);
    const double z = 300.0 + (600.0 + g / k) * e / k - g * t / k;
    CHECK_NEAR(c->time, t, 0.01);
    CHECK_NEAR(c->pos.z, z, 6.0);
    CHECK_NEAR(c->pos.x, 0.f, 1e-3f);
    CHECK_NEAR(c->pos.y, 5120.f, 1e-3f);
    CHECK_EQ(c->bounces, 0);
}

TEST(shooting_predict_rolling_ball) {
    // Rolling without slipping (spin matches the speed, below the 6 rad/s cap): only drag slows it down.
    const Vec3 w{-500.f / kBallRadius, 0.f, 0.f};
    const auto c = predictToPlaneY({300.f, 4000.f, kBallRestZ}, {0.f, 500.f, 0.f}, w, 5120.f);
    CHECK(c.has_value());
    if (!c) return;
    // Drag acts on the linear velocity only; through friction the spin shares the loss, so a rolling ball
    // (inertia 7/5 m) decelerates with an effective drag of 0.03 * 5/7.
    const double k = 0.03 * 5.0 / 7.0;
    const double t = -std::log(1.0 - k * 1120.0 / 500.0) / k;
    CHECK_NEAR(c->time, t, 0.01);
    CHECK_NEAR(c->pos.z, kBallRestZ, 0.5f);
    CHECK_NEAR(c->pos.x, 300.f, 1e-2f);
    CHECK_EQ(c->bounces, 0);
    // The same ball sliding without spin loses speed to friction until it rolls: it arrives later.
    const auto sliding = predictToPlaneY({300.f, 4000.f, kBallRestZ}, {0.f, 500.f, 0.f}, Vec3{}, 5120.f);
    CHECK(sliding.has_value());
    if (sliding) CHECK(sliding->time > c->time + 0.05f);
}

TEST(shooting_predict_bouncing_ball_and_orange_goal) {
    // Dropped from 600 uu while moving towards the orange-side plane (y = -5120).
    const auto c = predictToPlaneY({-200.f, -2500.f, 600.f}, {0.f, -1400.f, 0.f}, Vec3{}, -5120.f);
    CHECK(c.has_value());
    if (!c) return;
    CHECK(c->bounces >= 1);
    CHECK(c->pos.z >= kBallRestZ - 1e-3f);
    CHECK(c->pos.z < 600.f); // the bounces lose energy
    CHECK_NEAR(c->pos.y, -5120.f, 1e-3f);
    CHECK(c->vel.y < 0.f);
}

TEST(shooting_predict_spin_and_friction_on_bounce) {
    // A ball landing with backspin keeps less forward speed than one landing with topspin (moving +Y, rolling
    // forward means w.x < 0: the contact point slip is v.y + R * w.x).
    const Vec3 p{0.f, 0.f, 300.f}, v{0.f, 1200.f, -800.f};
    const auto back = predictToPlaneY(p, v, Vec3{6.f, 0.f, 0.f}, 2000.f, 6.f);
    const auto top = predictToPlaneY(p, v, Vec3{-6.f, 0.f, 0.f}, 2000.f, 6.f);
    CHECK(back.has_value() && top.has_value());
    if (back && top) CHECK(back->time > top->time);
}

TEST(shooting_predict_never_reaching) {
    CHECK(!predictToPlaneY({0.f, 0.f, kBallRestZ}, {0.f, -800.f, 0.f}, Vec3{}, 5120.f).has_value()); // moving away
    CHECK(!predictToPlaneY({0.f, 0.f, kBallRestZ}, {0.f, 100.f, 0.f}, Vec3{}, 5120.f).has_value()); // too slow (drag)
    CHECK(!predictToPlaneY({0.f, 5200.f, 300.f}, {0.f, 500.f, 0.f}, Vec3{}, 5120.f).has_value()); // already beyond
    CHECK(!predictToPlaneY({0.f, 4000.f, 300.f}, {0.f, 3000.f, 0.f}, Vec3{}, 5120.f, 0.1f).has_value()); // time limit
}

TEST(shooting_predict_side_wall_bounce) {
    const auto c = predictToPlaneY({3800.f, 3000.f, 500.f}, {1500.f, 1500.f, 0.f}, Vec3{}, 5120.f);
    CHECK(c.has_value());
    if (!c) return;
    CHECK(c->bounces >= 1);
    CHECK(c->pos.x < 4096.f - 93.f);
    CHECK(c->vel.x < 0.f); // reflected
}

TEST(shooting_clamp_to_field) {
    const Vec3 a = shoot::clampToField({5000.f, 0.f, 17.f}, 300.f);
    CHECK_NEAR(a.x, 4096.f - 300.f, 1e-3f);
    const Vec3 b = shoot::clampToField({4000.f, 5000.f, 17.f}, 100.f);
    CHECK(std::fabs(b.x) + std::fabs(b.y) <= 8064.f - 100.f + 1e-2f);
    const Vec3 c = shoot::clampToField({100.f, -200.f, 17.f}, 300.f);
    CHECK_NEAR(c.x, 100.f, 1e-4f);
    CHECK_NEAR(c.y, -200.f, 1e-4f);
}
