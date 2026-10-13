// Intercept advisor of the prediction module: car reach model, jump timing, earliest reachable point.
#include <cmath>
#include <vector>

#include "tests/TestFramework.h"

#include "core/modules/prediction/BallPath.h"
#include "core/modules/prediction/Intercept.h"

using namespace rlt;
using namespace rlt::pr;

namespace {

const float kR = phys::kBallArenaRadius;

// Synthetic path: the ball stays at `pos` for `seconds` (lets the reach model be tested in isolation).
PredictedPath hoveringPath(const Vec3& pos, float seconds) {
    PredictedPath p;
    p.valid = true;
    p.horizon = seconds;
    const int n = static_cast<int>(std::lround(seconds * kTickRate));
    for (int i = 0; i < n; ++i) {
        PathSample s;
        s.t = static_cast<float>(i + 1) * kTickDt;
        s.pos = pos;
        p.samples.push_back(s);
    }
    return p;
}

PredictedPath simulated(const Vec3& pos, const Vec3& vel, float seconds = 4.f) {
    PredictedPath p;
    SimBall b;
    b.pos = pos;
    b.vel = vel;
    simulatePath(b, seconds, SimParams{}, ArenaMode::Soccar, p);
    analyzePath(p, 120.f, 300.f);
    return p;
}

InterceptCar carAt(const Vec3& pos, const Vec3& vel = {}, const Vec3& fwd = {0.f, 1.f, 0.f}, float boost = 0.f) {
    InterceptCar c;
    c.pos = pos;
    c.vel = vel;
    c.forward = fwd;
    c.boost = boost;
    return c;
}

// Reachability of one sample alone: a one-sample path keeps the logic of findIntercept as a black box.
bool reachableAt(const PredictedPath& p, int i, const InterceptCar& c, const InterceptParams& prm,
                 const CarReachModel& m) {
    PredictedPath one;
    one.valid = true;
    one.arena = p.arena;
    one.samples.push_back(p.samples[static_cast<size_t>(i)]);
    return findIntercept(one, c, prm, m).found;
}

} // namespace

// ---- Reach model ---------------------------------------------------------------------------------------
TEST(pr_reach_throttle_curve) {
    CHECK_NEAR(CarReachModel::throttleAccel(0.f), 1600.f, 1e-3f);
    CHECK_NEAR(CarReachModel::throttleAccel(700.f), 880.f, 1e-2f);
    CHECK_NEAR(CarReachModel::throttleAccel(1400.f), 160.f, 1e-3f);
    CHECK_NEAR(CarReachModel::throttleAccel(1405.f), 80.f, 1e-3f);
    CHECK_NEAR(CarReachModel::throttleAccel(1410.f), 0.f, 1e-3f);
    CHECK_NEAR(CarReachModel::throttleAccel(2000.f), 0.f, 1e-3f);
    CHECK_NEAR(CarReachModel::throttleAccel(-50.f), 1600.f, 1e-3f);
}

TEST(pr_reach_terminal_speeds) {
    const CarReachModel m;
    float v = 0.f;
    const float d = m.distance(0.f, 5.f, 0.f, &v);
    CHECK_NEAR(v, 1410.f, 3.f);
    CHECK(d < 1410.f * 5.f && d > 1410.f * 3.f);
    m.distance(0.f, 5.f, 10.f, &v);
    CHECK_NEAR(v, 2300.f, 0.5f);
    // Boost from rest: ~0.8 s to 1400 (throttle + boost), then 991.7 uu/s^2: supersonic (2200) at ~1.6 s.
    float v15 = 0.f, v20 = 0.f;
    m.distance(0.f, 1.5f, 10.f, &v15);
    m.distance(0.f, 2.f, 10.f, &v20);
    CHECK(v15 > 2000.f && v15 < 2200.f);
    CHECK_NEAR(v20, 2300.f, 0.5f);
}

TEST(pr_reach_cruise_above_throttle_speed) {
    const CarReachModel m;
    float v = 0.f;
    CHECK_NEAR(m.distance(2000.f, 1.5f, 0.f, &v), 3000.f, 0.5f);
    CHECK_NEAR(v, 2000.f, 1e-3f);
    CHECK_NEAR(m.distance(2300.f, 1.f, 5.f, &v), 2300.f, 0.5f);
    CHECK_NEAR(m.distance(800.f, 0.f, 5.f, &v), 0.f, 1e-6f);
    CHECK_NEAR(v, 800.f, 1e-6f);
    CHECK_NEAR(m.distance(800.f, -1.f, 5.f, &v), 0.f, 1e-6f);
}

TEST(pr_reach_monotonic_in_time_speed_and_boost) {
    const CarReachModel m;
    for (float v0 : {0.f, 500.f, 1200.f, 1500.f, 2100.f}) {
        float prev = -1.f;
        for (float t = 0.f; t <= 6.f; t += 0.05f) {
            const float d = m.distance(v0, t, 0.f);
            CHECK(d >= prev - 1e-3f);
            prev = d;
        }
    }
    for (float t : {0.3f, 1.f, 2.5f}) {
        float prev = -1.f;
        for (float v0 = 0.f; v0 <= 2300.f; v0 += 100.f) {
            const float d = m.distance(v0, t, 0.f);
            CHECK(d >= prev - 1e-3f);
            prev = d;
        }
        float prevB = -1.f;
        for (float b = 0.f; b <= 3.f; b += 0.25f) {
            const float d = m.distance(300.f, t, b);
            CHECK(d >= prevB - 0.05f);  // table interpolation noise once the boost is no longer useful
            prevB = d;
        }
    }
}

TEST(pr_reach_time_shift_consistency) {
    const CarReachModel m;
    for (float a : {0.2f, 0.7f, 1.3f}) {
        for (float b : {0.1f, 0.5f, 2.f}) {
            float va = 0.f;
            const float da = m.distance(0.f, a, 0.f, &va);
            const float db = m.distance(va, b, 0.f);
            CHECK_NEAR(m.distance(0.f, a + b, 0.f), da + db, 1.f);
            float vab = 0.f;
            const float dab = m.distance(0.f, a, 10.f, &vab);
            CHECK_NEAR(m.distance(0.f, a + b, 10.f), dab + m.distance(vab, b, 10.f), 1.5f);
        }
    }
    // Boost running out: boost part then throttle part.
    float v1 = 0.f;
    const float d1 = m.distance(0.f, 1.f, 1.f, &v1);
    CHECK_NEAR(m.distance(0.f, 3.f, 1.f), d1 + m.distance(v1, 2.f, 0.f), 1.5f);
}

TEST(pr_jump_time_kinds_and_limits) {
    float t = 0.f;
    JumpKind k = JumpKind::None;
    CHECK(CarReachModel::jumpTime(-20.f, false, t, k));
    CHECK(k == JumpKind::None && t == 0.f);
    CHECK(CarReachModel::jumpTime(50.f, false, t, k));
    CHECK(k == JumpKind::Jump && t > 0.f && t < 0.2f);
    CHECK(CarReachModel::jumpTime(200.f, false, t, k));
    CHECK(k == JumpKind::Jump && t > 0.2f && t < 0.9f);
    CHECK(CarReachModel::jumpTime(300.f, false, t, k));
    CHECK(k == JumpKind::DoubleJump);
    CHECK(CarReachModel::jumpTime(800.f, true, t, k));
    CHECK(k == JumpKind::Aerial && t > 0.5f && t < 2.f);
    CHECK(!CarReachModel::jumpTime(800.f, false, t, k));
    CHECK(!CarReachModel::jumpTime(2500.f, true, t, k));
    CHECK(!CarReachModel::jumpTime(std::nanf(""), true, t, k));
}

TEST(pr_jump_time_matches_kinematics_and_is_monotonic) {
    // Single jump: rise(t) with 0.2 s of jump hold, then gravity only.
    const float J = 875.f / 3.f, a = 4375.f / 3.f - 650.f;
    auto rise = [&](float t) {
        if (t <= 0.2f) return J * t + 0.5f * a * t * t;
        const float h0 = J * 0.2f + 0.5f * a * 0.04f, v1 = J + a * 0.2f, u = t - 0.2f;
        return h0 + v1 * u - 325.f * u * u;
    };
    for (float h : {20.f, 74.f, 120.f, 200.f, 224.f}) {
        float t = 0.f;
        JumpKind k = JumpKind::None;
        CHECK(CarReachModel::jumpTime(h, false, t, k));
        CHECK_NEAR(rise(t), h, 0.5f);
    }
    float prev = 0.f;
    for (float h = 1.f; h < 1800.f; h += 7.f) {
        float t = 0.f;
        JumpKind k = JumpKind::None;
        if (!CarReachModel::jumpTime(h, true, t, k)) continue;
        // Within one technique the time grows with the height (technique changes may be faster).
        if (h > 1.f) CHECK(t >= prev - 1e-4f || k != JumpKind::Jump);
        prev = t;
    }
}

// ---- findIntercept -------------------------------------------------------------------------------------
TEST(pr_intercept_ball_rolling_towards_car) {
    const CarReachModel m;
    const PredictedPath p = simulated({0.f, 2000.f, kR}, {0.f, -800.f, 0.f});
    const InterceptResult r = findIntercept(p, carAt({0.f, 0.f, 17.f}), InterceptParams{}, m);
    CHECK(r.found);
    CHECK(r.t > 0.3f && r.t < 2.5f);
    CHECK(r.jump == JumpKind::None);
    CHECK_NEAR(r.ballSpeed2D, length2D(r.ballVel), 1e-3f);
    // A ball launched without spin slides: friction (~227 uu/s^2) and drag slow it down.
    CHECK(r.ballSpeed2D > 300.f && r.ballSpeed2D < 800.f);
    CHECK(r.ballSpeed2D < length2D(p.start.vel));
}

TEST(pr_intercept_is_the_earliest_reachable_sample) {
    const CarReachModel m;
    struct Case {
        Vec3 ballPos, ballVel, carPos, carVel, fwd;
        float boost;
    };
    const Case cases[] = {
        {{0.f, 2000.f, 600.f}, {300.f, 0.f, 500.f}, {0.f, 0.f, 17.f}, {}, {0.f, 1.f, 0.f}, 0.f},
        {{1500.f, -1000.f, 1200.f}, {-400.f, 300.f, 200.f}, {-2000.f, 0.f, 17.f}, {800.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, 50.f},
        {{0.f, 0.f, 1500.f}, {0.f, 0.f, 0.f}, {0.f, -3000.f, 17.f}, {0.f, 1400.f, 0.f}, {0.f, 1.f, 0.f}, 100.f},
        {{-3000.f, 3000.f, 300.f}, {600.f, -900.f, 800.f}, {1000.f, 0.f, 17.f}, {0.f, -500.f, 0.f}, {0.f, -1.f, 0.f}, 30.f},
    };
    InterceptParams prm;
    int found = 0;
    for (const Case& c : cases) {
        const PredictedPath p = simulated(c.ballPos, c.ballVel, 5.f);
        const InterceptCar car = carAt(c.carPos, c.carVel, c.fwd, c.boost);
        const InterceptResult r = findIntercept(p, car, prm, m);
        int expected = -1;
        for (int i = 0; i < p.size() && expected < 0; ++i)
            if (reachableAt(p, i, car, prm, m)) expected = i;
        CHECK_EQ(r.index, expected);
        CHECK_EQ(r.found, expected >= 0);
        if (r.found) {
            ++found;
            CHECK_NEAR(r.t, p.samples[static_cast<size_t>(r.index)].t, 1e-6f);
            CHECK(r.ballPos == p.samples[static_cast<size_t>(r.index)].pos);
            CHECK(r.jumpTime <= r.t + 1e-5f);
        }
    }
    CHECK(found >= 3);
}

TEST(pr_intercept_closer_car_is_never_later) {
    const CarReachModel m;
    const PredictedPath p = simulated({0.f, 3000.f, 900.f}, {0.f, -300.f, 400.f}, 6.f);
    float prev = 1e9f;
    for (float y = -3000.f; y <= 2500.f; y += 250.f) {
        const InterceptResult r = findIntercept(p, carAt({0.f, y, 17.f}), InterceptParams{}, m);
        CHECK(r.found);
        if (r.found) {
            CHECK(r.t <= prev + 1e-5f);
            prev = r.t;
        }
    }
}

TEST(pr_intercept_faster_car_and_boost_are_never_later) {
    const CarReachModel m;
    const PredictedPath p = simulated({500.f, 2500.f, 700.f}, {200.f, 300.f, 300.f}, 6.f);
    float prev = 1e9f;
    for (float v = 0.f; v <= 2300.f; v += 230.f) {
        const InterceptResult r = findIntercept(p, carAt({0.f, -2000.f, 17.f}, {0.f, v, 0.f}), InterceptParams{}, m);
        CHECK(r.found);
        if (r.found) {
            CHECK(r.t <= prev + 1e-5f);
            prev = r.t;
        }
    }
    const InterceptResult noBoost = findIntercept(p, carAt({0.f, -2000.f, 17.f}, {}, {0.f, 1.f, 0.f}, 0.f), InterceptParams{}, m);
    const InterceptResult boost = findIntercept(p, carAt({0.f, -2000.f, 17.f}, {}, {0.f, 1.f, 0.f}, 100.f), InterceptParams{}, m);
    CHECK(noBoost.found && boost.found);
    CHECK(boost.t <= noBoost.t);
    InterceptParams off;
    off.useBoost = false;
    const InterceptResult ignored = findIntercept(p, carAt({0.f, -2000.f, 17.f}, {}, {0.f, 1.f, 0.f}, 100.f), off, m);
    CHECK_NEAR(ignored.t, noBoost.t, 1e-6f);
}

TEST(pr_intercept_turning_around_costs_time) {
    const CarReachModel m;
    const PredictedPath p = hoveringPath({0.f, 2000.f, kR}, 4.f);
    const InterceptResult facing = findIntercept(p, carAt({0.f, 0.f, 17.f}, {}, {0.f, 1.f, 0.f}), InterceptParams{}, m);
    const InterceptResult away = findIntercept(p, carAt({0.f, 0.f, 17.f}, {}, {0.f, -1.f, 0.f}), InterceptParams{}, m);
    CHECK(facing.found && away.found);
    CHECK_NEAR(away.t - facing.t, 0.6f, 0.03f);  // default penalty for a half turn
    CHECK_NEAR(away.turnPenalty, 0.6f, 1e-3f);
    // Driving away at speed: no speed towards the target, plus the turn.
    const InterceptResult fleeing = findIntercept(p, carAt({0.f, 0.f, 17.f}, {0.f, -1400.f, 0.f}, {0.f, -1.f, 0.f}), InterceptParams{}, m);
    CHECK(fleeing.found && fleeing.t >= away.t - 1e-5f);
}

TEST(pr_intercept_height_picks_the_jump) {
    const CarReachModel m;
    const InterceptParams prm;  // ground reach 170
    struct Case {
        float z;
        float boost;
        JumpKind kind;
        bool found;
    };
    const Case cases[] = {{150.f, 0.f, JumpKind::None, true},       {300.f, 0.f, JumpKind::Jump, true},
                          {550.f, 0.f, JumpKind::DoubleJump, true}, {1200.f, 50.f, JumpKind::Aerial, true},
                          {1200.f, 0.f, JumpKind::None, false},     {1200.f, 10.f, JumpKind::None, false}};
    for (const Case& c : cases) {
        const PredictedPath p = hoveringPath({0.f, 1500.f, c.z}, 4.f);
        const InterceptResult r = findIntercept(p, carAt({0.f, 0.f, 17.f}, {}, {0.f, 1.f, 0.f}, c.boost), prm, m);
        CHECK_EQ(r.found, c.found);
        if (r.found) {
            CHECK(r.jump == c.kind);
            CHECK(r.t >= r.jumpTime);
        }
    }
}

TEST(pr_intercept_already_in_reach_and_unreachable) {
    const CarReachModel m;
    const PredictedPath close = hoveringPath({50.f, 60.f, kR}, 2.f);
    const InterceptResult r = findIntercept(close, carAt({0.f, 0.f, 17.f}), InterceptParams{}, m);
    CHECK(r.found);
    CHECK_EQ(r.index, 0);
    CHECK_NEAR(r.distance, 0.f, 1e-6f);
    const PredictedPath far = hoveringPath({4000.f, 5000.f, kR}, 1.f);
    CHECK(!findIntercept(far, carAt({-4000.f, -5000.f, 17.f}), InterceptParams{}, m).found);
    PredictedPath invalid;
    CHECK(!findIntercept(invalid, carAt({}), InterceptParams{}, m).found);
}

TEST(pr_intercept_ignores_the_ball_past_the_goal_line) {
    const CarReachModel m;
    const PredictedPath p = simulated({0.f, 4000.f, kR}, {0.f, 2000.f, 0.f}, 3.f);
    CHECK(p.goalIndex >= 0);
    // A car parked in the net would only reach the ball after the goal: no advice.
    const InterceptResult r = findIntercept(p, carAt({0.f, 5600.f, 17.f}, {}, {0.f, -1.f, 0.f}), InterceptParams{}, m);
    if (r.found) CHECK(r.index < p.goalIndex && std::fabs(r.ballPos.y) <= phys::kArenaY);
}

TEST(pr_intercept_min_time_skips_early_samples) {
    const CarReachModel m;
    const PredictedPath p = hoveringPath({0.f, 100.f, kR}, 2.f);
    InterceptParams prm;
    prm.minTime = 0.5f;
    const InterceptResult r = findIntercept(p, carAt({0.f, 0.f, 17.f}), prm, m);
    CHECK(r.found);
    CHECK(r.t >= 0.5f - 1e-6f);
}
