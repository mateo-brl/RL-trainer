// Ball physics and path analysis of the prediction module (ported from the reference predictor tests).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "tests/TestFramework.h"

#include "core/modules/prediction/BallPath.h"
#include "core/modules/prediction/BallPhysics.h"

using namespace rlt;
using namespace rlt::pr;

namespace {

const double kDragK = -std::log(1.0 - static_cast<double>(phys::kBallDrag));  // 0.030459 1/s
const float kR = phys::kBallArenaRadius;

// Analytic free fall with linear drag dv/dt = g - k v (g < 0). Height of the centre at time t.
double zAnalytic(double z0, double t) {
    const double g = -static_cast<double>(phys::kGravityZ), k = kDragK;
    return z0 - (g / k) * t + (g / (k * k)) * (1.0 - std::exp(-k * t));
}
double vzAnalytic(double t) {
    const double g = -static_cast<double>(phys::kGravityZ), k = kDragK;
    return -(g / k) * (1.0 - std::exp(-k * t));
}
double fallTimeAnalytic(double z0, double zEnd) {
    double lo = 0, hi = 10;
    for (int i = 0; i < 200; ++i) {
        const double m = 0.5 * (lo + hi);
        (zAnalytic(z0, m) > zEnd ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

SimBall ball(Vec3 pos, Vec3 vel = {}, Vec3 angVel = {}) {
    SimBall b;
    b.pos = pos;
    b.vel = vel;
    b.angVel = angVel;
    return b;
}

} // namespace

// ---- Physics -------------------------------------------------------------------------------------------
TEST(pr_physics_free_fall_time_matches_analytic) {
    for (float z0 : {300.f, 1000.f, 1900.f}) {
        SimBall b = ball({0.f, 0.f, z0});
        float tHit = -1.f, vzHit = 0.f;
        for (int i = 1; i <= 600; ++i) {
            const float vzPrev = b.vel.z;
            const StepResult r = stepBall(b);
            if (r.flags & kStepFloor) {
                tHit = static_cast<float>(i) * kTickDt;
                vzHit = vzPrev;
                break;
            }
        }
        const double tA = fallTimeAnalytic(z0, kR);
        CHECK_NEAR(tHit, tA, 1.5 * kTickDt);
        CHECK(std::fabs(vzHit - vzAnalytic(tA)) / std::fabs(vzAnalytic(tA)) < 0.02);
    }
}

TEST(pr_physics_free_fall_1000_reference_value) {
    // physics.md: from h = 1000 the analytic contact time with drag is 1.6847 s (1.6704 s without drag).
    CHECK_NEAR(fallTimeAnalytic(1000.0, kR), 1.6847, 0.001);
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 1000.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(p.landingIndex >= 0);
    if (p.landingIndex >= 0) CHECK_NEAR(p.samples[static_cast<size_t>(p.landingIndex)].t, 1.6847f, 1.5f * kTickDt);
}

TEST(pr_physics_bounce_apex_decay) {
    SimBall b = ball({0.f, 0.f, 1000.f});
    std::vector<float> apex{1000.f - kR};
    float zmax = 0.f;
    bool rising = false;
    for (int i = 0; i < 120 * 12; ++i) {
        const StepResult r = stepBall(b);
        if (r.flags & kStepBounce) {
            rising = true;
            zmax = b.pos.z;
        }
        if (rising) zmax = std::max(zmax, b.pos.z);
        if (rising && (r.flags & kStepApex)) {
            apex.push_back(zmax - kR);
            rising = false;
        }
    }
    CHECK(apex.size() >= 4);
    for (size_t i = 1; i < apex.size() && i < 3; ++i) {  // big bounces: ratio ~ e^2 minus drag
        const float r = apex[i] / apex[i - 1];
        CHECK(r > 0.33f && r < 0.365f);
    }
    for (size_t i = 3; i < apex.size() && i < 7; ++i) CHECK(std::fabs(apex[i] - 0.36f * apex[i - 1]) < 2.5f);
    // Continuous-time reference: exact fall/rise with linear drag, instant bounce with e = 0.6.
    double h = 1000.0 - kR;
    for (size_t i = 1; i < apex.size() && i < 5; ++i) {
        const double t = fallTimeAnalytic(kR + h, kR), v = -vzAnalytic(t) * phys::kBallRestitution;
        const double g = -static_cast<double>(phys::kGravityZ), k = kDragK;
        h = v / k - (g / (k * k)) * std::log(1.0 + k * v / g);
        CHECK(std::fabs(apex[i] - h) < std::max(3.0, 0.035 * h));
    }
    // At rest after 12 s, exactly on the floor.
    CHECK_NEAR(b.pos.z, kR, 0.5f);
    CHECK(std::fabs(b.vel.z) < 1.f);
}

TEST(pr_physics_side_wall_bounce_restitution_and_spin) {
    SimBall b = ball({3000.f, 0.f, 1000.f}, {1500.f, 0.f, 0.f});
    float xmax = 0.f;
    bool hit = false;
    for (int i = 0; i < 240; ++i) {
        const Vec3 v0 = b.vel;
        const StepResult r = stepBall(b);
        xmax = std::max(xmax, b.pos.x);
        if (r.flags & kStepWall) {
            hit = true;
            const float vxBefore = v0.x * std::pow(1.f - phys::kBallDrag, kTickDt);
            const float vzBefore = v0.z + phys::kGravityZ * kTickDt;
            CHECK_NEAR(-b.vel.x / vxBefore, 0.6f, 0.01f);
            CHECK_NEAR(b.vel.z, vzBefore * 5.f / 7.f, 1.f);  // the wall kills the slip: rolls on the wall
            CHECK(b.angVel.y < -1.f);                       // the slip became spin
            CHECK_NEAR(r.normal.x, -1.f, 1e-3f);
            CHECK(r.flags & kStepBounce);
            break;
        }
    }
    CHECK(hit);
    CHECK(xmax <= phys::kArenaX - kR + 0.01f);
    CHECK(std::fabs(b.pos.y) < 1e-3f);
}

TEST(pr_physics_ceiling_bounce) {
    SimBall c = ball({0.f, 0.f, 1500.f}, {0.f, 0.f, 1500.f});
    bool hit = false;
    for (int i = 0; i < 120; ++i) {
        const Vec3 v0 = c.vel;
        const StepResult r = stepBall(c);
        if (r.flags & kStepCeiling) {
            hit = true;
            const float vin = v0.z * std::pow(1.f - phys::kBallDrag, kTickDt);
            CHECK_NEAR(-c.vel.z / vin, 0.6f, 0.01f);
            CHECK(c.pos.z <= phys::kArenaZ - kR + 0.01f);
            break;
        }
    }
    CHECK(hit);
}

TEST(pr_physics_corner_bounce_symmetric) {
    SimBall b = ball({3000.f, 3000.f, 1000.f}, {1000.f, 1000.f, 0.f});
    bool hit = false;
    for (int i = 0; i < 240; ++i) {
        const StepResult r = stepBall(b);
        if (r.flags & kStepWall) {
            hit = true;
            CHECK(b.vel.x < 0.f && b.vel.y < 0.f);
            CHECK(std::fabs(b.vel.x - b.vel.y) < 1e-2f);
            CHECK_NEAR(-b.vel.x / 1000.f, 0.6f, 0.02f);
            break;
        }
    }
    CHECK(hit);
}

TEST(pr_physics_goal_straight_shot) {
    PredictedPath p;
    simulatePath(ball({0.f, 4000.f, kR}, {0.f, 2000.f, 0.f}, {-6.f, 0.f, 0.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(p.valid);
    CHECK(!p.samples.empty() && (p.samples.back().flags & kStepGoal));
    CHECK(std::fabs(p.samples.back().t - 0.617f) < 0.05f);
    CHECK_EQ(p.goalIndex, p.size() - 1);  // the path stops at the goal
    CHECK_EQ(p.goalSide, 1);
    // Same shot towards the blue goal.
    simulatePath(ball({0.f, -4000.f, kR}, {0.f, -2000.f, 0.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK_EQ(p.goalSide, -1);
    CHECK(isGoal({0.f, 5300.f, 100.f}));
    CHECK(!isGoal({0.f, 5100.f, 100.f}));
}

TEST(pr_physics_back_wall_shot_is_no_goal) {
    PredictedPath p;
    simulatePath(ball({2000.f, 4000.f, 500.f}, {0.f, 2000.f, 0.f}), 2.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    float ymax = 0.f;
    bool anyGoal = false;
    for (const auto& s : p.samples) {
        anyGoal = anyGoal || (s.flags & kStepGoal);
        ymax = std::max(ymax, s.pos.y);
    }
    CHECK(!anyGoal);
    CHECK_EQ(p.goalIndex, -1);
    CHECK(ymax <= phys::kArenaY - kR + 0.01f);
    CHECK(p.bounceCount >= 1);  // the back wall bounce is recorded
}

TEST(pr_physics_post_deflects) {
    PredictedPath p;
    simulatePath(ball({850.f, 4500.f, 300.f}, {0.f, 2000.f, 0.f}), 1.f, SimParams{}, ArenaMode::Soccar, p);
    bool hit = false;
    for (const auto& q : p.samples)
        if (q.flags & kStepWall) {
            hit = true;
            CHECK(q.vel.x < -100.f);
            break;
        }
    CHECK(hit);
}

TEST(pr_physics_speed_and_spin_caps) {
    SimBall b = ball({0.f, 0.f, 1000.f}, {7000.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
    stepBall(b);
    CHECK(length(b.vel) <= 6000.01f);
    CHECK(length(b.angVel) <= 6.0001f);
}

TEST(pr_physics_ramp_climb_stays_inside) {
    SimBall r = ball({3000.f, 0.f, kR}, {1500.f, 0.f, 0.f}, {0.f, 6.f, 0.f});
    float zmax = 0.f, xmax = 0.f;
    for (int i = 0; i < 300; ++i) {
        stepBall(r);
        zmax = std::max(zmax, r.pos.z);
        xmax = std::max(xmax, r.pos.x);
    }
    CHECK(zmax > 300.f);
    CHECK(xmax <= phys::kArenaX - kR + 0.01f);
}

TEST(pr_physics_rolling_shares_drag_with_spin) {
    SimBall q = ball({0.f, -2000.f, kR}, {0.f, 500.f, 0.f}, {-500.f / phys::kBallRadius, 0.f, 0.f});
    for (int i = 0; i < 240; ++i) stepBall(q);
    const float expect = 500.f * static_cast<float>(std::exp(-kDragK * 2.0 / 1.4));
    CHECK_NEAR(q.vel.y, expect, 2.f);
    CHECK_NEAR(q.pos.z, kR, 0.01f);
}

TEST(pr_physics_resting_ball_stays_still) {
    SimBall b = ball({100.f, 200.f, kR});
    for (int i = 0; i < 240; ++i) {
        const StepResult r = stepBall(b);
        CHECK(!(r.flags & kStepBounce));
    }
    CHECK_NEAR(b.pos.z, kR, 0.01f);
    CHECK(length(b.vel) < 0.5f);
}

TEST(pr_physics_random_states_stay_in_arena_and_lose_energy) {
    uint32_t seed = 12345;
    auto rnd = [&](float a, float b) {
        seed = seed * 1664525u + 1013904223u;
        return a + (b - a) * (static_cast<float>(seed >> 8) * (1.f / 16777216.f));
    };
    int escapes = 0;
    double maxGain = 0.0;
    for (int n = 0; n < 120; ++n) {
        SimBall b;
        b.pos = {rnd(-3500, 3500), rnd(-4500, 4500), rnd(150, 1800)};
        if (std::fabs(b.pos.x) + std::fabs(b.pos.y) > 7500) {
            b.pos.x *= 0.7f;
            b.pos.y *= 0.7f;
        }
        b.vel = normalize(Vec3{rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)}) * rnd(0, 6000);
        b.angVel = {rnd(-4, 4), rnd(-4, 4), rnd(-4, 4)};
        auto energy = [&](const SimBall& s) {
            return 0.5 * dot(s.vel, s.vel) +
                   0.5 * 0.4 * phys::kBallRadius * phys::kBallRadius * dot(s.angVel, s.angVel) -
                   static_cast<double>(phys::kGravityZ) * s.pos.z;
        };
        for (int i = 0; i < 720; ++i) {
            const double e0 = energy(b);
            const StepResult r = stepBall(b);
            if (r.flags & kStepGoal) break;
            maxGain = std::max(maxGain, energy(b) - e0);
            const float ax = std::fabs(b.pos.x), ay = std::fabs(b.pos.y);
            const bool inGoal = ay > phys::kArenaY;
            const bool ok =
                b.pos.z >= kR - 0.5f && b.pos.z <= phys::kArenaZ - kR + 0.5f && ax <= phys::kArenaX - kR + 0.5f &&
                (ax + ay) <= phys::kCornerSum - kR * std::sqrt(2.f) + 0.5f &&
                (inGoal ? (ax <= phys::kGoalHalfWidth - kR + 0.5f && b.pos.z <= phys::kGoalHeight - kR + 0.5f &&
                           ay <= phys::kGoalBackY - kR + 0.5f)
                        : (ay <= phys::kArenaY - kR + 0.5f || ax < phys::kGoalHalfWidth));
            if (!ok) {
                ++escapes;
                break;
            }
        }
    }
    CHECK_EQ(escapes, 0);
    CHECK(maxGain < 200.0);
}

TEST(pr_physics_floor_only_mode_has_no_walls) {
    SimBall b = ball({3000.f, 0.f, 500.f}, {1500.f, 0.f, 0.f});
    bool floor = false;
    for (int i = 0; i < 240; ++i) {
        const StepResult r = stepBall(b, SimParams{}, ArenaMode::FloorOnly);
        CHECK(!(r.flags & (kStepWall | kStepGoal | kStepCeiling)));
        floor = floor || (r.flags & kStepFloor);
    }
    CHECK(b.pos.x > phys::kArenaX);  // went through where the wall would be
    CHECK(floor);
    CHECK(b.pos.z >= kR - 0.5f);
}

TEST(pr_physics_none_mode_is_free_flight) {
    SimBall b = ball({0.f, 0.f, 300.f});
    for (int i = 0; i < 240; ++i) {
        const StepResult r = stepBall(b, SimParams{}, ArenaMode::None);
        CHECK(!(r.flags & kStepContact));
    }
    CHECK(b.pos.z < 0.f);  // fell through the (absent) floor
    CHECK(arenaContacts({0.f, 0.f, 10.f}, 100.f, ArenaMode::None).count == 0);
}

TEST(pr_physics_contacts_and_params) {
    CHECK_EQ(arenaContacts({0.f, 0.f, 1000.f}, kR).count, 0);
    const ContactList near = arenaContacts({0.f, 0.f, 50.f}, kR);
    CHECK_EQ(near.count, 1);
    CHECK(near.c[0].kind & kContactFloor);
    CHECK_NEAR(near.c[0].dist, 50.f, 1e-4f);
    const SimParams d = SimParams::forRadius(-5.f);
    CHECK_NEAR(d.radius, phys::kBallRadius, 1e-6f);
    const SimParams big = SimParams::forRadius(100.2565f);
    CHECK_NEAR(big.arenaRadius, 100.2565f + phys::kArenaMargin, 1e-4f);
    CHECK_NEAR(dragFactor(SimParams{}), std::pow(0.97f, kTickDt), 1e-7f);
}

// ---- Path ----------------------------------------------------------------------------------------------
TEST(pr_path_sample_count_times_and_clamp) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 500.f}, {300.f, 0.f, 0.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK(p.valid);
    CHECK_EQ(p.size(), 360);
    CHECK_NEAR(p.samples[0].t, kTickDt, 1e-6f);
    CHECK_NEAR(p.samples[359].t, 3.f, 1e-4f);
    CHECK(p.posAt(0) == p.start.pos);
    CHECK(p.posAt(1) == p.samples[0].pos);
    CHECK(p.posAt(100000) == p.samples.back().pos);
    simulatePath(p.start, 50.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK_EQ(p.size(), static_cast<int>(kMaxHorizon * kTickRate));
    simulatePath(p.start, 0.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK_EQ(p.size(), static_cast<int>(std::lround(kMinHorizon * kTickRate)));
}

TEST(pr_path_reuses_capacity) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 500.f}), 6.f, SimParams{}, ArenaMode::Soccar, p);
    const auto* data = p.samples.data();
    const size_t cap = p.samples.capacity();
    for (int i = 0; i < 5; ++i) simulatePath(ball({0.f, 100.f * i, 500.f}), 6.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK(p.samples.data() == data);
    CHECK_EQ(p.samples.capacity(), cap);
}

TEST(pr_path_rejects_non_finite_and_clamps_spin) {
    PredictedPath p;
    simulatePath(ball({std::nanf(""), 0.f, 500.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK(!p.valid);
    CHECK(p.samples.empty());
    simulatePath(ball({0.f, 0.f, 500.f}, {}, {0.f, 0.f, 600.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK(p.valid);
    CHECK(length(p.start.angVel) <= phys::kBallMaxAngSpeed + 1e-4f);
    analyzePath(p, 120.f, 300.f);
}

TEST(pr_path_apex_landing_and_zone_for_a_lob) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 500.f}, {400.f, 0.f, 600.f}), 6.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(p.startAirborne);
    CHECK(p.apexIndex > 0);
    const float apexT = p.samples[static_cast<size_t>(p.apexIndex)].t;
    CHECK_NEAR(apexT, 600.f / 650.f, 0.03f);  // vz / g (slightly less with drag)
    for (int i = 0; i < p.apexIndex; ++i) CHECK(p.samples[static_cast<size_t>(i)].vel.z > 0.f);
    CHECK(p.landingIndex > p.apexIndex);
    CHECK(p.samples[static_cast<size_t>(p.landingIndex)].flags & kStepFloor);
    for (int i = 0; i < p.landingIndex; ++i) CHECK(!(p.samples[static_cast<size_t>(i)].flags & kStepContact));
    // Zone: after the apex, first descending sample inside 120..300, until it leaves the band.
    CHECK(p.zoneStart > p.apexIndex);
    CHECK(p.zoneEnd >= p.zoneStart);
    CHECK(p.zoneEnd < p.landingIndex);
    for (int i = p.zoneStart; i <= p.zoneEnd; ++i) {
        const auto& s = p.samples[static_cast<size_t>(i)];
        CHECK(s.pos.z >= 120.f && s.pos.z <= 300.f && s.vel.z <= 0.f);
    }
    CHECK(p.samples[static_cast<size_t>(p.zoneStart - 1)].pos.z > 300.f);
    CHECK(p.samples[static_cast<size_t>(p.zoneEnd + 1)].pos.z < 120.f);
    // The first bounce is the landing.
    CHECK(p.bounceCount >= 1);
    CHECK_EQ(p.bounces[0].index, p.landingIndex);
    CHECK_NEAR(p.bounces[0].point.z, 0.f, 1.f);
    CHECK_NEAR(p.bounces[0].normal.z, 1.f, 1e-3f);
}

TEST(pr_path_descending_ball_apex_is_now) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 250.f}, {0.f, 0.f, -100.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK_EQ(p.apexIndex, -1);
    CHECK_EQ(p.zoneStart, 0);  // already inside the band
    CHECK(p.landingIndex > 0);
}

TEST(pr_path_rolling_ball_has_no_landing_and_no_zone) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, kR}, {800.f, 0.f, 0.f}, {0.f, 800.f / phys::kBallRadius, 0.f}), 2.f, SimParams{},
                 ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(!p.startAirborne);
    CHECK_EQ(p.landingIndex, -1);
    CHECK_EQ(p.zoneStart, -1);
    CHECK_EQ(p.bounceCount, 0);
    CHECK(!ballAirborne(p.start.pos, p.start.vel, kR));
}

TEST(pr_path_zone_none_when_apex_below_band_and_swapped_bounds) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 105.f}, {0.f, 0.f, 50.f}), 2.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK_EQ(p.zoneStart, -1);
    simulatePath(ball({0.f, 0.f, 800.f}), 3.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 300.f, 120.f);  // swapped
    CHECK(p.zoneStart >= 0);
    if (p.zoneStart >= 0) CHECK(p.samples[static_cast<size_t>(p.zoneStart)].pos.z <= 300.f);
}

TEST(pr_path_bounce_markers_and_wall_normal) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 1000.f}), 6.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(p.bounceCount >= 3);
    for (int i = 1; i < p.bounceCount; ++i) {
        CHECK(p.bounces[static_cast<size_t>(i)].impactSpeed < p.bounces[static_cast<size_t>(i - 1)].impactSpeed);
        CHECK(p.bounces[static_cast<size_t>(i)].impactSpeed >= kMinBounceMarkerSpeed);
    }
    simulatePath(ball({3000.f, 0.f, 1000.f}, {1500.f, 0.f, 0.f}), 1.f, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    CHECK(p.bounceCount >= 1);
    CHECK_NEAR(p.bounces[0].normal.x, -1.f, 1e-3f);
    CHECK_NEAR(p.bounces[0].point.x, phys::kArenaX, 1.f);
    CHECK(p.bounces[0].flags & kStepWall);
}

TEST(pr_path_closest_sample) {
    PredictedPath p;
    simulatePath(ball({0.f, 0.f, 500.f}, {1000.f, 0.f, 0.f}), 2.f, SimParams{}, ArenaMode::Soccar, p);
    CHECK_EQ(closestSample(p, p.start.pos, 90), -1);
    CHECK_EQ(closestSample(p, p.samples[10].pos, 90), 10);
    CHECK_EQ(closestSample(p, p.samples[200].pos, 90), 89);  // search bounded
    CHECK_EQ(closestSample(p, p.samples[10].pos, 0), -1);
}

TEST(pr_path_goal_bound_ball_in_floor_only_mode_never_scores) {
    PredictedPath p;
    simulatePath(ball({0.f, 4000.f, kR}, {0.f, 2000.f, 0.f}), 3.f, SimParams{}, ArenaMode::FloorOnly, p);
    analyzePath(p, 120.f, 300.f);
    CHECK_EQ(p.goalIndex, -1);
    CHECK_EQ(p.size(), 360);
}
