// Pure rules of the overlay: speed differential modes and signs, deadband, units, zones, chat report, touch zone
// behaviour, tilt, smoothing and circle geometry.

#include <cmath>
#include <string>
#include <vector>

#include "core/modules/overlay/OverlayMath.h"
#include "tests/TestFramework.h"

using namespace rlt;
using namespace rlt::ovl;

namespace {
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
float angleFromDown(const Vec3& d) { return angleBetweenDeg(d, {0.f, 0.f, -1.f}); }
} // namespace

// ---- Speed differential ----------------------------------------------------------------------------------------
TEST(ov_math_xy_mode_ignores_vertical_speed) {
    const SpeedDiff d = computeSpeedDiff(SpeedMode::XY, {}, {1000.f, 0.f, 900.f}, {0.f, 300.f, 500.f},
                                         {600.f, 0.f, -2000.f});
    CHECK_NEAR(d.value, 400.f, 1e-3f);
    CHECK(!d.hasZ);
}

TEST(ov_math_xy_mode_sign_negative_when_ball_faster) {
    const SpeedDiff d = computeSpeedDiff(SpeedMode::XY, {}, {300.f, 400.f, 0.f}, {}, {0.f, 800.f, 0.f});
    CHECK_NEAR(d.value, -300.f, 1e-3f);
}

TEST(ov_math_xyz_mode_uses_3d_speeds) {
    const SpeedDiff d = computeSpeedDiff(SpeedMode::XYZ, {}, {300.f, 0.f, 400.f}, {}, {0.f, 0.f, 200.f});
    CHECK_NEAR(d.value, 300.f, 1e-3f);
    const SpeedDiff n = computeSpeedDiff(SpeedMode::XYZ, {}, {0.f, 0.f, 100.f}, {}, {0.f, 0.f, -700.f});
    CHECK_NEAR(n.value, -600.f, 1e-3f);
    CHECK(!d.hasZ);
}

TEST(ov_math_xy_plus_z_mode_adds_vertical_line) {
    const SpeedDiff d = computeSpeedDiff(SpeedMode::XYPlusZ, {}, {1000.f, 0.f, 500.f}, {}, {600.f, 0.f, -300.f});
    CHECK(d.hasZ);
    CHECK_NEAR(d.value, 400.f, 1e-3f);       // main = XY
    CHECK_NEAR(d.z, 200.f, 1e-3f);           // |500| - |-300|
    const SpeedDiff n = computeSpeedDiff(SpeedMode::XYPlusZ, {}, {0.f, 0.f, -100.f}, {}, {0.f, 0.f, 600.f});
    CHECK_NEAR(n.z, -500.f, 1e-3f);
}

TEST(ov_math_closing_speed_signs) {
    const Vec3 car{0.f, 0.f, 17.f}, ball{1000.f, 0.f, 17.f};
    // Chasing a ball that runs away slower: closing in at 500.
    CHECK_NEAR(computeSpeedDiff(SpeedMode::Closing, car, {1500.f, 0.f, 0.f}, ball, {1000.f, 0.f, 0.f}).value, 500.f,
               1e-2f);
    // Ball coming toward the car: closing speeds add up.
    CHECK_NEAR(computeSpeedDiff(SpeedMode::Closing, car, {1500.f, 0.f, 0.f}, ball, {-500.f, 0.f, 0.f}).value, 2000.f,
               1e-2f);
    // Ball running away faster: negative.
    CHECK_NEAR(computeSpeedDiff(SpeedMode::Closing, car, {500.f, 0.f, 0.f}, ball, {1200.f, 0.f, 0.f}).value, -700.f,
               1e-2f);
    // Moving sideways only: no closing speed (deadband).
    CHECK_NEAR(computeSpeedDiff(SpeedMode::Closing, car, {0.f, 1400.f, 0.f}, ball, {0.f, 1400.f, 0.f}).value, 0.f,
               1e-6f);
    // Vertical approach counts (ball above the car).
    CHECK_NEAR(closingSpeed({0.f, 0.f, 0.f}, {0.f, 0.f, 800.f}, {0.f, 0.f, 300.f}, {0.f, 0.f, 200.f}), 600.f, 1e-2f);
}

TEST(ov_math_closing_speed_degenerate_positions) {
    // Same centres: fallback to 3D speed difference, never NaN.
    const float v = closingSpeed({5.f, 5.f, 5.f}, {300.f, 0.f, 400.f}, {5.f, 5.f, 5.f}, {100.f, 0.f, 0.f});
    CHECK(std::isfinite(v));
    CHECK_NEAR(v, 400.f, 1e-3f);
}

TEST(ov_math_deadband) {
    CHECK_EQ(applyDeadband(0.9f), 0.f);
    CHECK_EQ(applyDeadband(1.f), 0.f);
    CHECK_EQ(applyDeadband(-1.f), 0.f);
    CHECK_NEAR(applyDeadband(1.01f), 1.01f, 1e-6f);
    CHECK_NEAR(applyDeadband(-1.5f), -1.5f, 1e-6f);
    CHECK_EQ(applyDeadband(std::nanf("")), 0.f);
    // Applied by every mode.
    CHECK_EQ(computeSpeedDiff(SpeedMode::XY, {}, {1000.5f, 0.f, 0.f}, {}, {1000.f, 0.f, 0.f}).value, 0.f);
    CHECK_EQ(computeSpeedDiff(SpeedMode::XYPlusZ, {}, {0.f, 0.f, 500.8f}, {}, {0.f, 0.f, 500.f}).z, 0.f);
}

TEST(ov_math_mode_and_behavior_indices) {
    CHECK(speedModeFromIndex(0) == SpeedMode::XY);
    CHECK(speedModeFromIndex(1) == SpeedMode::XYZ);
    CHECK(speedModeFromIndex(2) == SpeedMode::XYPlusZ);
    CHECK(speedModeFromIndex(3) == SpeedMode::Closing);
    CHECK(speedModeFromIndex(42) == SpeedMode::XY);
    CHECK(speedModeFromIndex(-1) == SpeedMode::XY);
    CHECK(behaviorFromIndex(0) == ZoneBehavior::Static);
    CHECK(behaviorFromIndex(1) == ZoneBehavior::VelocityDriven);
    CHECK(behaviorFromIndex(2) == ZoneBehavior::KeepItUp);
    CHECK(behaviorFromIndex(3) == ZoneBehavior::Auto);
    CHECK(behaviorFromIndex(9) == ZoneBehavior::Static);
    CHECK(std::string(behaviorLabel(ZoneBehavior::KeepItUp)) == "Garder en l'air");
    CHECK(std::string(speedModeLabel(SpeedMode::Closing)) == "approche");
}

// ---- Units and text ------------------------------------------------------------------------------------------
TEST(ov_math_speed_text_units) {
    CHECK_EQ(speedText(1000.f, SpeedUnit::Kph), std::string("+36.00 KPH"));
    CHECK_EQ(speedText(-250.f, SpeedUnit::Kph), std::string("-9.00 KPH"));
    CHECK_EQ(speedText(447.04f, SpeedUnit::Mph), std::string("+10.00 MPH"));
    CHECK_EQ(speedText(1234.5f, SpeedUnit::Uus), std::string("+1234.50 uu/s"));
    CHECK_EQ(speedText(0.8f, SpeedUnit::Kph), std::string("0.00 KPH"));
    CHECK_EQ(speedText(-1.f, SpeedUnit::Mph), std::string("0.00 MPH"));
    CHECK_EQ(zText(200.f, SpeedUnit::Kph), std::string("Z : +7.20 KPH"));
    // Auto resolves through the game setting.
    CHECK(resolveUnit(SpeedUnit::Auto, true) == SpeedUnit::Kph);
    CHECK(resolveUnit(SpeedUnit::Auto, false) == SpeedUnit::Mph);
    CHECK(resolveUnit(SpeedUnit::Uus, false) == SpeedUnit::Uus);
}

// ---- Zones ---------------------------------------------------------------------------------------------------
TEST(ov_math_zone_thresholds_default) {
    CHECK(classifyZone(0.f, 150.f, 300.f) == SpeedZone::Good);
    CHECK(classifyZone(150.f, 150.f, 300.f) == SpeedZone::Good);
    CHECK(classifyZone(-150.f, 150.f, 300.f) == SpeedZone::Good);
    CHECK(classifyZone(150.5f, 150.f, 300.f) == SpeedZone::Alright);
    CHECK(classifyZone(-299.f, 150.f, 300.f) == SpeedZone::Alright);
    CHECK(classifyZone(300.f, 150.f, 300.f) == SpeedZone::Alright);
    CHECK(classifyZone(300.5f, 150.f, 300.f) == SpeedZone::Bad);
    CHECK(classifyZone(-2000.f, 150.f, 300.f) == SpeedZone::Bad);
}

TEST(ov_math_zone_thresholds_custom_and_misconfigured) {
    CHECK(classifyZone(400.f, 500.f, 800.f) == SpeedZone::Good);
    CHECK(classifyZone(600.f, 500.f, 800.f) == SpeedZone::Alright);
    // alright < good: behaves like alright == good (no alright band).
    CHECK(classifyZone(200.f, 250.f, 100.f) == SpeedZone::Good);
    CHECK(classifyZone(260.f, 250.f, 100.f) == SpeedZone::Bad);
    // Negative thresholds are treated as 0.
    CHECK(classifyZone(0.f, -5.f, -5.f) == SpeedZone::Good);
    CHECK(classifyZone(2.f, -5.f, -5.f) == SpeedZone::Bad);
}

TEST(ov_math_zone_colors_from_config) {
    OverlayConfig cfg;
    cfg.goodColor = {1.f, 2.f, 3.f, 4.f};
    cfg.alrightColor = {5.f, 6.f, 7.f, 8.f};
    cfg.badColor = {9.f, 10.f, 11.f, 12.f};
    CHECK(zoneColor(SpeedZone::Good, cfg) == cfg.goodColor);
    CHECK(zoneColor(SpeedZone::Alright, cfg) == cfg.alrightColor);
    CHECK(zoneColor(SpeedZone::Bad, cfg) == cfg.badColor);
    const OverlayConfig def;
    CHECK(zoneColor(SpeedZone::Good, def) == colors::Green);
    CHECK(zoneColor(SpeedZone::Alright, def) == colors::Yellow);
    CHECK(zoneColor(SpeedZone::Bad, def) == colors::Red);
}

TEST(ov_math_speed_report_text) {
    SpeedDiff good;
    good.value = 50.f;
    const std::string g = speedReport(SpeedMode::XY, good, SpeedZone::Good, SpeedUnit::Kph, "RT");
    CHECK_EQ(g, std::string("Écart de vitesse (XY) : +1.80 KPH, Bon."));

    SpeedDiff fast;
    fast.value = 900.f;
    const std::string f = speedReport(SpeedMode::XYZ, fast, SpeedZone::Bad, SpeedUnit::Kph, "RT");
    CHECK(contains(f, "(XYZ)"));
    CHECK(contains(f, "Trop d'écart"));
    CHECK(contains(f, "plus vite que la balle"));
    CHECK(contains(f, "relâche l'accélération (RT)"));

    SpeedDiff slow;
    slow.value = -900.f;
    const std::string s = speedReport(SpeedMode::XY, slow, SpeedZone::Bad, SpeedUnit::Mph, "");
    CHECK(contains(s, "MPH"));
    CHECK(contains(s, "accélère."));
    CHECK(!contains(s, "()"));

    SpeedDiff z;
    z.value = 200.f;
    z.z = -100.f;
    z.hasZ = true;
    const std::string zs = speedReport(SpeedMode::XYPlusZ, z, SpeedZone::Alright, SpeedUnit::Kph, "RT");
    CHECK(contains(zs, "Z : -3.60 KPH"));
    CHECK(contains(zs, "Correct"));
    CHECK(!contains(zs, "relâche"));  // hints only for the bad zone

    SpeedDiff closing;
    closing.value = 800.f;
    closing.carApproach = 800.f;   // all of it from the car
    CHECK(contains(speedReport(SpeedMode::Closing, closing, SpeedZone::Bad, SpeedUnit::Kph, ""), "Tu fonces"));
    closing.value = -800.f;
    closing.carApproach = 0.f;     // the ball runs away from a car that stands still
    CHECK(contains(speedReport(SpeedMode::Closing, closing, SpeedZone::Bad, SpeedUnit::Kph, ""), "s'éloigne"));
}

// ---- Touch zone behaviour ------------------------------------------------------------------------------------
TEST(ov_math_auto_behavior_resolution) {
    CHECK(resolveBehavior(ZoneBehavior::Auto, 900.f, MapCategory::Standard, 800.f) == ZoneBehavior::VelocityDriven);
    CHECK(resolveBehavior(ZoneBehavior::Auto, 800.f, MapCategory::Standard, 800.f) == ZoneBehavior::Static);
    CHECK(resolveBehavior(ZoneBehavior::Auto, 100.f, MapCategory::Dribble, 800.f) == ZoneBehavior::Static);
    CHECK(resolveBehavior(ZoneBehavior::Auto, 0.f, MapCategory::AirDribble, 800.f) == ZoneBehavior::VelocityDriven);
    // Explicit choices are never changed, even on air dribble maps.
    CHECK(resolveBehavior(ZoneBehavior::KeepItUp, 0.f, MapCategory::AirDribble, 800.f) == ZoneBehavior::KeepItUp);
    CHECK(resolveBehavior(ZoneBehavior::Static, 3000.f, MapCategory::AirDribble, 800.f) == ZoneBehavior::Static);
}

TEST(ov_math_tilt_ramp_and_clamp) {
    CHECK_NEAR(tiltAngleDeg(0.f, 800.f, 2100.f, 35.f), 0.f, 1e-6f);
    CHECK_NEAR(tiltAngleDeg(800.f, 800.f, 2100.f, 35.f), 0.f, 1e-6f);
    CHECK_NEAR(tiltAngleDeg(1450.f, 800.f, 2100.f, 35.f), 17.5f, 1e-3f);
    CHECK_NEAR(tiltAngleDeg(2100.f, 800.f, 2100.f, 35.f), 35.f, 1e-3f);
    CHECK_NEAR(tiltAngleDeg(6000.f, 800.f, 2100.f, 35.f), 35.f, 1e-3f);     // clamped at max speed
    CHECK_NEAR(tiltAngleDeg(6000.f, 800.f, 2100.f, 500.f), 80.f, 1e-3f);    // max tilt clamped to 80
    CHECK_NEAR(tiltAngleDeg(6000.f, 800.f, 2100.f, -10.f), 0.f, 1e-6f);
    CHECK_NEAR(tiltAngleDeg(900.f, 800.f, 800.f, 30.f), 30.f, 1e-3f);       // degenerate ramp: step
    CHECK_NEAR(tiltAngleDeg(std::nanf(""), 800.f, 2100.f, 35.f), 0.f, 1e-6f);
}

TEST(ov_math_direction_static_is_straight_down) {
    const Vec3 d = zoneTargetDirection(ZoneBehavior::Static, {3000.f, 0.f, 0.f}, 800.f, 2100.f, 35.f);
    CHECK_NEAR(d.x, 0.f, 1e-6f);
    CHECK_NEAR(d.y, 0.f, 1e-6f);
    CHECK_NEAR(d.z, -1.f, 1e-6f);
    // Auto must be resolved before: treated as static.
    CHECK_NEAR(zoneTargetDirection(ZoneBehavior::Auto, {3000.f, 0.f, 0.f}, 800.f, 2100.f, 35.f).z, -1.f, 1e-6f);
}

TEST(ov_math_direction_velocity_driven_opposes_travel) {
    const Vec3 d = zoneTargetDirection(ZoneBehavior::VelocityDriven, {2100.f, 0.f, 300.f}, 800.f, 2100.f, 35.f);
    CHECK_NEAR(length(d), 1.f, 1e-5f);
    CHECK(d.x < 0.f);   // behind the ball
    CHECK_NEAR(d.y, 0.f, 1e-5f);
    CHECK(d.z < 0.f);
    CHECK_NEAR(angleFromDown(d), 35.f, 0.05f);
    // Any heading: horizontal part is anti-parallel to v_xy.
    const Vec3 v{-900.f, -1500.f, -400.f};
    const Vec3 e = zoneTargetDirection(ZoneBehavior::VelocityDriven, v, 800.f, 2100.f, 35.f);
    CHECK(dot(flat(e), flat(v)) < 0.f);
    CHECK_NEAR(length(cross(flat(e), flat(v))), 0.f, 1e-2f);
}

TEST(ov_math_direction_keep_it_up_follows_travel) {
    const Vec3 d = zoneTargetDirection(ZoneBehavior::KeepItUp, {0.f, -2100.f, 0.f}, 800.f, 2100.f, 35.f);
    CHECK(d.y < 0.f);   // in front of the ball (toward its travel)
    CHECK_NEAR(d.x, 0.f, 1e-5f);
    CHECK_NEAR(angleFromDown(d), 35.f, 0.05f);
    const Vec3 half = zoneTargetDirection(ZoneBehavior::KeepItUp, {1450.f, 0.f, 0.f}, 800.f, 2100.f, 35.f);
    CHECK(half.x > 0.f);
    CHECK_NEAR(angleFromDown(half), 17.5f, 0.05f);
}

TEST(ov_math_direction_slow_or_vertical_ball_stays_down) {
    CHECK_NEAR(zoneTargetDirection(ZoneBehavior::VelocityDriven, {700.f, 0.f, 0.f}, 800.f, 2100.f, 35.f).z, -1.f,
               1e-6f);
    CHECK_NEAR(zoneTargetDirection(ZoneBehavior::KeepItUp, {0.f, 0.f, 3000.f}, 800.f, 2100.f, 35.f).z, -1.f, 1e-6f);
    const Vec3 nanDir = zoneTargetDirection(ZoneBehavior::KeepItUp, {std::nanf(""), 0.f, 0.f}, 800.f, 2100.f, 35.f);
    CHECK_NEAR(nanDir.z, -1.f, 1e-6f);
}

TEST(ov_math_direction_tilt_is_clamped) {
    const Vec3 d = zoneTargetDirection(ZoneBehavior::VelocityDriven, {5800.f, 0.f, 0.f}, 800.f, 2100.f, 35.f);
    CHECK_NEAR(angleFromDown(d), 35.f, 0.05f);
    const Vec3 e = zoneTargetDirection(ZoneBehavior::VelocityDriven, {5800.f, 0.f, 0.f}, 800.f, 2100.f, 400.f);
    CHECK_NEAR(angleFromDown(e), 80.f, 0.05f);
    CHECK(e.z < 0.f);   // never above the ball equator
}

// ---- Smoothing -----------------------------------------------------------------------------------------------
TEST(ov_math_smoothing_converges) {
    const Vec3 down{0.f, 0.f, -1.f};
    const Vec3 target = zoneTargetDirection(ZoneBehavior::VelocityDriven, {2100.f, 0.f, 0.f}, 800.f, 2100.f, 35.f);
    Vec3 d = down;
    // One time constant: about 63 % of the way (small-angle blend).
    d = smoothDirection(d, target, 0.1f, 0.1f);
    const float a1 = angleFromDown(d);
    CHECK(a1 > 18.f && a1 < 26.f);
    CHECK_NEAR(length(d), 1.f, 1e-5f);
    // 120 Hz for one second: converged.
    Vec3 e = down;
    for (int i = 0; i < 120; ++i) e = smoothDirection(e, target, 1.f / 120.f, 0.1f);
    CHECK_NEAR(angleBetweenDeg(e, target), 0.f, 0.05f);
    // Monotonic approach.
    Vec3 f = down;
    float prev = angleBetweenDeg(f, target);
    for (int i = 0; i < 30; ++i) {
        f = smoothDirection(f, target, 1.f / 120.f, 0.1f);
        const float now = angleBetweenDeg(f, target);
        CHECK(now <= prev + 1e-4f);
        prev = now;
    }
}

TEST(ov_math_smoothing_edge_cases) {
    const Vec3 down{0.f, 0.f, -1.f};
    const Vec3 target = normalize(Vec3{0.5f, 0.f, -1.f});
    // tau 0 = snap.
    CHECK_NEAR(angleBetweenDeg(smoothDirection(down, target, 0.01f, 0.f), target), 0.f, 1e-3f);
    // dt 0 = unchanged.
    CHECK_NEAR(angleBetweenDeg(smoothDirection(down, target, 0.f, 0.1f), down), 0.f, 1e-3f);
    // Degenerate current = snap.
    CHECK_NEAR(angleBetweenDeg(smoothDirection({}, target, 0.01f, 0.1f), target), 0.f, 1e-3f);
    // Opposite direction = snap (no zero crossing).
    const Vec3 r = smoothDirection({0.f, 0.f, 1.f}, down, 0.01f, 0.1f);
    CHECK_NEAR(r.z, -1.f, 1e-5f);
    // Non-unit input is normalized.
    CHECK_NEAR(length(smoothDirection(Vec3{0.f, 0.f, -5.f}, target, 0.02f, 0.1f)), 1.f, 1e-5f);
}

// ---- Circle geometry -----------------------------------------------------------------------------------------
TEST(ov_math_circle_points_lie_on_the_ball) {
    const Vec3 ball{100.f, -200.f, 600.f};
    const float R = 91.25f;
    const std::vector<Vec3> dirs = {{0.f, 0.f, -1.f}, normalize(Vec3{1.f, 0.f, -1.f}),
                                    normalize(Vec3{-0.3f, 0.7f, -1.f}), normalize(Vec3{0.f, 1.f, -0.2f}),
                                    {1.f, 0.f, 0.f}};
    std::vector<Vec3> pts;
    for (const Vec3& d : dirs) {
        for (float r : {30.f, 47.f, 60.f}) {
            const ZoneCircle c = zoneCircle(ball, R, d, r);
            CHECK_NEAR(c.radius, r, 1e-4f);
            CHECK_NEAR(distance(c.center, ball), std::sqrt(R * R - r * r), 1e-2f);
            CHECK_NEAR(dot(c.normal, d), 1.f, 1e-5f);
            zoneCirclePoints(c, 32, pts);
            CHECK_EQ(pts.size(), static_cast<size_t>(33));
            for (const Vec3& p : pts) {
                CHECK_NEAR(distance(p, ball), R, 0.5f);
                CHECK_NEAR(distance(p, c.center), r, 0.05f);
                CHECK_NEAR(dot(p - c.center, c.normal), 0.f, 0.05f);  // in the circle plane
            }
        }
    }
}

TEST(ov_math_circle_default_depth_matches_critique) {
    // C-4: rho = 47 on a 91.25 ball sits sqrt(91.25^2 - 47^2) = 78.2 uu below the centre.
    const ZoneCircle c = zoneCircle({0.f, 0.f, 500.f}, 91.25f, {0.f, 0.f, -1.f}, 47.f);
    CHECK_NEAR(c.center.z, 500.f - 78.2f, 0.05f);
    CHECK_NEAR(c.center.x, 0.f, 1e-5f);
}

TEST(ov_math_circle_radius_clamped_inside_ball) {
    const Vec3 ball{0.f, 0.f, 500.f};
    const ZoneCircle big = zoneCircle(ball, 91.25f, {0.f, 0.f, -1.f}, 150.f);
    CHECK_NEAR(big.radius, 90.75f, 1e-3f);
    std::vector<Vec3> pts;
    zoneCirclePoints(big, 16, pts);
    for (const Vec3& p : pts) CHECK_NEAR(distance(p, ball), 91.25f, 0.5f);
    // Bad ball radius: default radius; zero direction: straight down.
    const ZoneCircle odd = zoneCircle(ball, 0.f, {}, 47.f);
    CHECK_NEAR(distance(odd.center, ball), std::sqrt(91.25f * 91.25f - 47.f * 47.f), 1e-2f);
    CHECK(odd.center.z < ball.z);
    zoneCirclePoints(odd, 2, pts);  // segments clamped to 3
    CHECK_EQ(pts.size(), static_cast<size_t>(4));
}
