// Playability of the shooting drills (audit spec firsttouch-shooting, cross-cutting fix 10): the setups are checked with
// the core ball physics (pr::stepBall / simulatePath of the prediction module) and the repo's car reach model
// (findIntercept), and the module is driven by small simulated worlds with a scripted player who reacts 0.4 s after
// the release. The assertions are on success, not only on the presence of a result.
//
// The shooting test binary does not link the prediction module: this file compiles its sources itself (namespace
// rlt::pr, no clash). No other test file of the binary may include them.

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "core/common/Units.h"
#include "core/modules/prediction/BallPath.cpp"
#include "core/modules/prediction/BallPhysics.cpp"
#include "core/modules/prediction/Intercept.cpp"
#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

constexpr float kReact = 0.4f;  // scripted player reaction (fix 10: 0.3 to 0.5 s)

// Ground acceleration of the throttle alone (RocketSim curve): a speed is held without boost while it is positive.
float throttleAccel(float v) {
    if (v < 1400.f) return 1600.f - 1440.f * (v / 1400.f);
    if (v < 1410.f) return 160.f * (1410.f - v) / 10.f;
    return 0.f;
}
bool throttleCanHold(float v) { return throttleAccel(v) > 100.f; }

pr::PredictedPath simulate(const Vec3& p, const Vec3& v, const Vec3& w = {}, float seconds = 6.f) {
    pr::SimBall b;
    b.pos = p;
    b.vel = v;
    b.angVel = w;
    pr::PredictedPath path;
    pr::simulatePath(b, seconds, pr::SimParams{}, pr::ArenaMode::Soccar, path);
    pr::analyzePath(path, 93.f, 300.f);
    return path;
}

pr::InterceptResult intercept(const pr::PredictedPath& path, const CarPlacement& car, const Vec3& carVel, bool boost,
                              float minTime = 0.15f) {
    static const pr::CarReachModel model;
    pr::InterceptCar c;
    c.pos = car.pos;
    c.vel = carVel;
    c.forward = basisFromRot(car.rot).fwd;
    c.boost = boost ? 100.f : 0.f;
    pr::InterceptParams prm;
    prm.useBoost = boost;
    prm.minTime = minTime;
    return pr::findIntercept(path, c, prm, model);
}

float levelOf(int niveau) { return static_cast<float>(niveau - 1) / 9.f; }

void loadLevel(Harness& h, const std::string& key, float level) {
    Store st;
    st.setFloat(key + ".level", level);
    st.setInt(key + ".attempts", 1);
    h.mod.load(st);
}

void tickWorld(Harness& h, World& w, int n = 1) {
    for (int i = 0; i < n; ++i) {
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
}

void pressToLive(Harness& h, World& w) {
    for (int i = 0; i < 900 && h.mod.phase() != ShootPhase::Live; ++i) {
        w.in.throttle = i < 2 ? 0.f : 1.f;
        tickWorld(h, w);
    }
    w.in.throttle = 0.f;
}

std::vector<std::string> panelMain(Harness& h, const World& w) {
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.carValid = true;
    v.carVisualPos = w.car.pos;
    v.carVisualBasis = w.car.basis;
    v.ballValid = true;
    v.ballVisualPos = w.ball.pos;
    const DrawList d = h.draw(v);
    std::vector<std::string> out;
    for (const auto& p : d.panels)
        for (const auto& l : p.lines)
            if (!l.detail) out.push_back(l.text);
    return out;
}

bool anyContains(const std::vector<std::string>& lines, const std::string& sub) {
    for (const auto& l : lines)
        if (l.find(sub) != std::string::npos) return true;
    return false;
}

} // namespace

// ==== Flick, balle sur le toit ===================================================================================
TEST(shooting_flick_power_bar_matches_distance) {
    CHECK(flickMinPower(0.f) <= 1000.f);
    const GoalFrame goal{1.f};
    RoofParams roof;
    for (int li = 0; li <= 10; ++li) {
        const float level = static_cast<float>(li) / 10.f;
        Rng rng(50 + static_cast<uint64_t>(li));
        float worst = 0.f;
        for (int k = 0; k < 50; ++k) {
            const Spawn sp = flickSpawn(level, false, -1.f, goal, roof, rng);
            const Vec3 start = sp.ball.pos;
            const Vec3 toGoal = normalize(flat(Vec3{0.f, kGoalLineY, 0.f} - start), Vec3{0.f, 1.f, 0.f});
            float best = 1e9f;
            for (float e = 5.f; e <= 40.f; e += 2.5f) {
                const Vec3 dir = toGoal * std::cos(e * kDegToRad) + Vec3{0.f, 0.f, std::sin(e * kDegToRad)};
                for (float v = 300.f; v < best; v += 25.f) {
                    pr::SimBall b;
                    b.pos = start;
                    b.vel = dir * v;
                    bool scored = false;
                    for (int i = 0; i < 600 && !scored; ++i) {
                        const Vec3 before = b.pos;
                        pr::stepBall(b);
                        if (before.y < kGoalLineY && b.pos.y >= kGoalLineY) {
                            scored = std::fabs(b.pos.x) < kOnTargetHalfU && b.pos.z < kOnTargetTopZ;
                            break;
                        }
                    }
                    if (scored) {
                        best = v;
                        break;
                    }
                }
            }
            worst = std::max(worst, best);
        }
        CHECK_MSG(worst <= flickMinPower(level), std::to_string(level) + ": " + std::to_string(worst));
        if (li == 0) CHECK(worst < 700.f);  // niveau 1 asks about 1.7 x what the 2000 uu need
    }
}

// ==== Départ en dribble (flick comme en match) ===================================================================
namespace {

// Ball carried on the roof of a car that holds its speed (the player keeps the throttle just right): the core physics
// moves the ball (gravity, drag), the roof is a plane under it with Coulomb friction 0.35 (the ball-vs-arena value of
// the core physics; car-ball contacts are not in it). Returns the seconds the ball stayed over the roof, up to
// `seconds`, and the car's last distance to the goal line.
struct Carry {
    float time = 0.f;
    float goalDist = 0.f;
};
Carry carryOnRoof(const Spawn& sp, const RoofParams& roof, float seconds) {
    pr::SimBall b;
    b.pos = sp.ball.pos;
    b.vel = sp.ballReleaseVel;
    const Basis B = basisFromRot(sp.car.rot);
    Vec3 car = sp.car.pos;
    const Vec3 carVel = sp.carReleaseVel;
    const float rest = roof.hb.roofZ() + roof.ballRadius;
    Carry c;
    for (float t = 0.f; t < seconds; t += kTickDt) {
        pr::stepBall(b);
        car += carVel * kTickDt;
        Vec3 l = B.toLocal(b.pos - car);
        const bool over = std::fabs(l.x - roof.hb.offset.x) <= roof.hb.extent.x * 0.5f &&
                          std::fabs(l.y - roof.hb.offset.y) <= roof.hb.extent.y * 0.5f;
        if (over && l.z < rest) {
            b.pos.z += rest - l.z;
            if (b.vel.z < 0.f) {
                const float jn = -b.vel.z;
                b.vel.z = 0.f;
                Vec3 rel = flat(b.vel - carVel);
                const float r = length(rel);
                if (r > 1e-4f) b.vel -= rel * (std::min(r, 0.35f * jn) / r);
            }
            l = B.toLocal(b.pos - car);
        }
        if (!ballOnRoof(b.pos, car, B, roof.hb, roof.ballRadius)) break;
        c.time = t + kTickDt;
    }
    c.goalDist = sp.goal.distanceToPlane(car);
    return c;
}

// Lowest ball speed (25 uu/s steps) that scores from `start` toward the goal centre, best elevation (core physics).
float speedToScore(const Vec3& start) {
    const Vec3 toGoal = normalize(flat(Vec3{0.f, kGoalLineY, 0.f} - start), Vec3{0.f, 1.f, 0.f});
    float best = 1e9f;
    for (float e = 0.f; e <= 40.f; e += 2.5f) {
        const Vec3 dir = toGoal * std::cos(e * kDegToRad) + Vec3{0.f, 0.f, std::sin(e * kDegToRad)};
        for (float v = 300.f; v < best; v += 25.f) {
            pr::SimBall b;
            b.pos = start;
            b.vel = dir * v;
            bool scored = false;
            for (int i = 0; i < 480; ++i) {
                const Vec3 before = b.pos;
                pr::stepBall(b);
                if (before.y < kGoalLineY && b.pos.y >= kGoalLineY) {
                    scored = std::fabs(b.pos.x) < kOnTargetHalfU && b.pos.z < kOnTargetTopZ;
                    break;
                }
            }
            if (scored) {
                best = v;
                break;
            }
        }
    }
    return best;
}

} // namespace

TEST(shooting_dribble_start_is_playable_with_the_core_physics) {
    RoofParams roof;
    roof.gap = 2.f;
    for (int niveau = 1; niveau <= 10; ++niveau) {
        const float level = levelOf(niveau);
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            Rng rng(seed * 7 + static_cast<uint64_t>(niveau));
            const GoalFrame goal{seed % 2 ? 1.f : -1.f};
            const Spawn sp = flickSpawn(level, true, -1.f, goal, roof, rng);
            const float speed = length(sp.carReleaseVel);
            const float dist = goal.distanceToPlane(sp.car.pos);
            // From midfield at a dribble speed the throttle alone holds, ball on the roof moving with the car.
            CHECK_MSG(dist >= 4499.f && dist <= 5501.f, std::to_string(dist));
            CHECK(speed >= 899.f && speed <= 1301.f);
            CHECK_NEAR(speed, dribbleSpeed(level), 1e-2f);
            CHECK(throttleCanHold(speed));
            CHECK(sp.ballOnRoof && distance(sp.ballReleaseVel, sp.carReleaseVel) < 1e-3f);
            CHECK(ballOnRoof(sp.ball.pos, sp.car.pos, basisFromRot(sp.car.rot), roof.hb, roof.ballRadius));
            CHECK(dot(normalize(flat(sp.carReleaseVel)), Vec3{0.f, goal.sign, 0.f}) > 0.9f);
            // Several seconds before the 12 m line at the start speed, and the ball stays on the roof all along.
            const float available = (dist - kFlickLateDist) / speed;
            CHECK_MSG(available >= (niveau == 1 ? 3.6f : 2.9f), std::to_string(available));
            const Carry c = carryOnRoof(sp, roof, available);
            CHECK_MSG(c.time >= available - 0.02f, std::to_string(niveau) + ": " + std::to_string(c.time));
            CHECK(c.goalDist >= kFlickLateDist - 20.f);
        }
        // The shot asked is a real shot, and enough: at the bar speed, a flick toward the goal centre scores from the
        // 12 m line and from 25 m (core physics, best elevation).
        const float bar = flickMinPower(level, true);
        CHECK(bar >= 1499.f && bar <= 2401.f);
        for (float d : {kFlickLateDist, 2500.f}) {
            const float need = speedToScore(Vec3{0.f, kGoalLineY - d, 160.f});
            CHECK_MSG(need <= bar, std::to_string(d) + ": " + std::to_string(need));
        }
    }
    // The stopped-car start is still there to learn the motion (and still near the goal).
    Rng rng(9);
    const Spawn still = flickSpawn(0.f, false, -1.f, GoalFrame{1.f}, roof, rng);
    CHECK(length(still.carReleaseVel) < 1e-3f);
    CHECK_NEAR(GoalFrame{1.f}.distanceToPlane(still.car.pos), 2000.f, 1.f);
}

TEST(shooting_flick_niveau_1_and_2_accept_any_flick_on_target) {
    // A backflip on target when a front flick is asked: a success at niveaux 1 and 2, the requested type counts from 3.
    for (int niveau : {1, 3}) {
        Harness h;
        loadLevel(h, "shooting.stair.flick", levelOf(niveau));
        World w;
        h.tick(w.snap());
        h.command("flick", {"front"});
        pressToLive(h, w);
        w.ballRides = true;
        tickWorld(h, w, 50);
        // Balle stable shows once the ball rested 0.3 s on the roof.
        CHECK(anyContains(panelMain(h, w), "Balle stable"));
        FlickScript s;
        s.start = w.car.pos;
        s.yawDeg = static_cast<float>(w.car.rot.yaw) * 360.f / 65536.f;
        s.startFrame = w.frame;
        s.dodgeDir = Vec3{-1.f, 0.f, 0.f};  // backflip
        s.launch = {1600.f, 0.f, 500.f};
        const ScriptRun run = buildFlick(s);
        for (size_t i = 0; i < run.snaps.size(); ++i) {
            for (const auto& t : run.touches)
                if (t.first == i) h.touch(t.second);
            h.tick(run.snaps[i]);
        }
        CHECK(h.results.size() == 1);
        if (h.results.empty()) continue;
        CHECK_MSG(h.results[0].success == (niveau == 1), h.results[0].title);
    }
}

// ==== Balle sur le toit, départ en dribble =========================================================================
namespace {

// Harness on Targets / roof at `level`, seeded until the attempt starts moving. The stationary flick is at niveau 3.
bool startMovingRoof(Harness& h, World& w, float level) {
    loadLevel(h, "shooting.stair.flick", levelOf(3));
    loadLevel(h, "shooting.stair.roof", level);
    for (uint64_t seed = 1; seed < 60; ++seed) {
        h.mod.setSeed(seed);
        h.tick(w.snap());
        h.command("targets", {"center", "roof"});
        tickWorld(h, w, 2);  // setup
        if (h.mod.movingStart()) return true;
        h.command("stop");
    }
    return false;
}

// Coasting car (525 uu/s^2) with the ball sliding forward on the roof at 375 uu/s^2 relative, unless the module
// put it back: returns the frames (since the release) where the ball was re-placed. From `dropAt` the ball rolls off
// the front of the roof (500 uu/s faster than the car) and falls to the floor, without any jump in position.
std::vector<int> coastWithDrift(Harness& h, World& w, int ticks, int dropAt = -1) {
    std::vector<int> replaced;
    float drift = 0.f, driftVel = 0.f;
    const Vec3 fwd = w.car.basis.fwd;
    bool dropping = false;
    for (int i = 0; i < ticks; ++i) {
        const float sp = std::max(0.f, length2D(w.car.vel) - 525.f * kTickDt);
        w.car.vel = fwd * sp;
        driftVel += 375.f * kTickDt;
        drift += driftVel * kTickDt;
        const Vec3 rest = roofSpawn(w.car.pos, w.car.basis, hitboxOf(w.car), 91.25f, 0.f);
        if (dropAt >= 0 && i >= dropAt) {
            if (!dropping) {
                dropping = true;
                w.ball.vel = fwd * (sp + 500.f);
            }
            w.ball.vel.z -= 650.f * kTickDt;
            w.ball.pos += w.ball.vel * kTickDt;
            if (w.ball.pos.z < 93.15f) {
                w.ball.pos.z = 93.15f;
                w.ball.vel.z = 0.f;
            }
        } else {
            w.ball.pos = rest + fwd * drift;
            w.ball.vel = fwd * (sp + driftVel);
        }
        const Actions a = h.tick(w.snap());
        if (a.setBall && !a.setCar) {
            replaced.push_back(i);
            drift = 0.f;
            driftVel = 0.f;
        }
        ++w.frame;
        w.car.pos += w.car.vel * kTickDt;
        if (h.mod.phase() != ShootPhase::Live) break;
    }
    return replaced;
}

} // namespace

TEST(shooting_moving_roof_start_is_announced_and_assisted) {
    Harness h;
    World w;
    CHECK(startMovingRoof(h, w, 0.5f));
    // The countdown announces the dribble start and the ball cam hint.
    CHECK(anyContains(panelMain(h, w), "dribble"));
    CHECK(anyContains(panelMain(h, w), "ball cam"));
    CHECK(h.mod.status().movingStart);
    pressToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    w.ballRides = false;
    const int attempts = h.mod.targetStaircase(TargetSetup::Roof).attempts();
    // No input: the ball would slide off; during 0.6 s it is put back on the roof every tick.
    const std::vector<int> replaced = coastWithDrift(h, w, 90);
    CHECK(!replaced.empty());
    bool at05 = false;
    for (int f : replaced) at05 = at05 || (f >= 58 && f <= 62);
    CHECK(at05);
    for (int f : replaced) CHECK(f <= static_cast<int>(kRoofAssistSeconds * kTickRate) + 1);
    // The ball falls off at 0.8 s (on the floor about 0.4 s later): the attempt is replayed, not counted.
    Harness g;
    World wg;
    CHECK(startMovingRoof(g, wg, 0.5f));
    pressToLive(g, wg);
    coastWithDrift(g, wg, 240, 96);
    CHECK(g.mod.phase() != ShootPhase::Live);
    CHECK(g.mod.status().message.find("rejoué") != std::string::npos);
    CHECK(g.mod.targetStaircase(TargetSetup::Roof).attempts() == attempts);
    CHECK(g.results.empty());
    // The roof setup is always the dribble start, like a match: 900 uu/s at niveau 1 up to 1300 at niveau 10, the ball
    // riding with the car.
    Rng rng(3);
    for (int k = 0; k < 200; ++k) {
        const Spawn lo = targetSpawn(TargetSetup::Roof, 0.f, GoalFrame{1.f}, RoofParams{}, rng);
        CHECK_NEAR(length(lo.carReleaseVel), 900.f, 1e-2f);
        CHECK(lo.ballOnRoof && distance(lo.carReleaseVel, lo.ballReleaseVel) < 1e-3f);
        CHECK_NEAR(length(targetSpawn(TargetSetup::Roof, 1.f, GoalFrame{1.f}, RoofParams{}, rng).carReleaseVel), 1300.f,
                   1e-2f);
    }
    CHECK_NEAR(dribbleSpeed(0.5f), 1100.f, 1e-3f);
}

TEST(shooting_flick_mode_moving_loss_in_first_second_is_replayed) {
    Harness h;
    h.set("rlt_sh_flick_start", "1");  // moving start
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    pressToLive(h, w);
    CHECK(h.mod.movingStart());
    const int before = h.mod.flickStaircase().attempts();
    coastWithDrift(h, w, 240, 80);  // falls off at 0.67 s
    CHECK(h.mod.status().message.find("rejoué") != std::string::npos);
    CHECK(h.mod.flickStaircase().attempts() == before);
    CHECK(h.results.empty());
    // A loss after the first second is a real failure.
    pressToLive(h, w);
    w.ballRides = true;
    tickWorld(h, w, 140);
    w.ballRides = false;
    w.ball.vel = {900.f, 0.f, 0.f};
    tickWorld(h, w, 150);
    CHECK(h.mod.flickStaircase().attempts() == before + 1);
    CHECK(h.results.size() == 1);
}

TEST(shooting_flick_early_losses_replayed_twice_then_counted) {
    // A start the player keeps losing is not replayed forever (dribble start by default now): two replays in a row at
    // most, like Mécas balle, then the loss is graded ("Balle perdue", level lowered, tip shown).
    Harness h;
    loadLevel(h, "shooting.stair.flick", levelOf(4));
    h.set("rlt_sh_flick_start", "1");
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    const int before = h.mod.flickStaircase().attempts();
    const float level = h.mod.flickStaircase().level();
    for (int k = 0; k < 2; ++k) {
        pressToLive(h, w);
        CHECK(h.mod.movingStart());
        coastWithDrift(h, w, 240, 80);  // falls off at 0.67 s
        CHECK_MSG(h.mod.status().message.find("rejoué") != std::string::npos, h.mod.status().message);
        CHECK(h.mod.flickStaircase().attempts() == before && h.results.empty());
    }
    pressToLive(h, w);
    coastWithDrift(h, w, 240, 80);
    CHECK(h.mod.flickStaircase().attempts() == before + 1);
    CHECK(h.mod.flickStaircase().level() < level);
    CHECK_EQ(h.results.size(), size_t(1));
    if (!h.results.empty()) CHECK(!h.results[0].success && !h.results[0].tips.empty());
    CHECK(h.mod.status().lastFlick == "Balle perdue");
    // The graded attempt ends the run: the next early loss is replayed again.
    pressToLive(h, w);
    coastWithDrift(h, w, 240, 80);
    CHECK(h.mod.status().message.find("rejoué") != std::string::npos);
    CHECK(h.mod.flickStaircase().attempts() == before + 1 && h.results.size() == 1);
}

// ==== Cibles, balle roulante =====================================================================================
TEST(shooting_targets_beginner_zones_and_redirect) {
    Rng rng(8);
    Zone prev = Zone::Center;
    for (int niveau = 1; niveau <= 3; ++niveau)
        for (int k = 0; k < 1000; ++k) {
            prev = pickTargetZone(kZoneRandom, levelOf(niveau), prev, rng);
            CHECK(prev != Zone::TopLeft && prev != Zone::TopRight);
        }
    bool top = false;
    for (int k = 0; k < 200; ++k) top = top || pickTargetZone(kZoneRandom, levelOf(4), prev, rng) == Zone::TopLeft;
    CHECK(top);
    CHECK(pickTargetZone(0, 0.f, prev, rng) == Zone::TopLeft);  // the player's own choice is kept
    const GoalFrame goal{1.f};
    for (float level : {0.f, 0.5f, 1.f}) {
        Rng r(21);
        for (int k = 0; k < 300; ++k) {
            const Spawn sp = targetSpawn(TargetSetup::Rolling, level, goal, RoofParams{}, r);
            // Untouched, it misses the goal mouth by at least 400 uu.
            const auto c = predictToPlaneY(sp.ball.pos, sp.ballReleaseVel, sp.ballReleaseAngVel, goal.planeY(), 12.f);
            CHECK(!c || std::fabs(goal.toPlane(c->pos).x) > kOnTargetHalfU + 400.f);
            // The car released behind it reaches it early, far from the goal.
            const pr::PredictedPath path = simulate(sp.ball.pos, sp.ballReleaseVel, sp.ballReleaseAngVel);
            const pr::InterceptResult it = intercept(path, sp.car, sp.carReleaseVel, true);
            CHECK(it.found);
            if (!it.found) continue;
            CHECK(it.t + kReact <= 2.4f);
            CHECK(goal.distanceToPlane(it.ballPos) >= 1500.f);
        }
    }
}

// ==== Cibles, balle qui rebondit =================================================================================
TEST(shooting_bouncing_niveau1_meets_the_second_bounce) {
    const GoalFrame goal{1.f};
    static const pr::CarReachModel model;
    for (float level : {0.f, 0.5f, 1.f}) {
        Rng rng(31);
        for (int k = 0; k < 300; ++k) {
            const Spawn sp = targetSpawn(TargetSetup::Bouncing, level, goal, RoofParams{}, rng);
            if (level == 0.f) CHECK_NEAR(sp.ball.pos.z, 400.f, 1e-3f);
            const pr::PredictedPath path = simulate(sp.ball.pos, sp.ballReleaseVel);
            int n = 0;
            float t2 = -1.f;
            Vec3 p2;
            for (int i = 0; i < path.bounceCount; ++i) {
                const auto& b = path.bounces[static_cast<size_t>(i)];
                if ((b.flags & pr::kStepFloor) && ++n == 2) {
                    t2 = path.samples[static_cast<size_t>(b.index)].t;
                    p2 = b.point;
                    break;
                }
            }
            CHECK(t2 > 0.f);
            const float dist = std::max(0.f, length2D(p2 - sp.car.pos) - 150.f);
            float lo = 0.f, hi = 6.f;
            for (int it = 0; it < 40; ++it) {
                const float m = 0.5f * (lo + hi);
                if (model.distance(0.f, m, 0.f) >= dist) hi = m;
                else lo = m;
            }
            CHECK(hi + kReact <= t2 - 0.2f + 0.4f);  // the reach time from rest leaves the 0.2 s (reaction included)
            CHECK(hi <= t2 - 0.2f);
            // The light predictor of the circles agrees with the core physics.
            const auto marks = predictFloorBounces(sp.ball.pos, sp.ballReleaseVel, Vec3{}, 2);
            CHECK(marks.size() == 2);
            if (marks.size() == 2) {
                CHECK(length2D(marks[1].pos - p2) < 60.f);
                CHECK(std::fabs(marks[1].time - t2) < 0.1f);
            }
        }
    }
    // Module: the two next bounces are drawn from the countdown on with "Cercles au sol" (a prediction: off by default);
    // "tire quand elle remonte" after a bounce.
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("targets", {"bouncing"});
    tickWorld(h, w, 3);
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    auto floorLinesOf = [&]() {
        int n = 0;
        for (const auto& l : h.draw(v).lines3D)
            if (std::fabs(l.a.z - 6.f) < 1e-3f) ++n;
        return n;
    };
    CHECK_EQ(floorLinesOf(), 0);
    h.set("rlt_sh_floor_marks", "1");
    CHECK(floorLinesOf() >= 2 * 28);
    pressToLive(h, w);
    bool cue = false;
    for (int i = 0; i < 300 && !cue; ++i) {
        tickWorld(h, w);
        cue = anyContains(panelMain(h, w), "Tire quand elle remonte");
    }
    CHECK(cue);
}

// ==== Centre en travers ==========================================================================================
TEST(shooting_pass_car_arrives_just_before_the_cross) {
    const GoalFrame goal{1.f};
    for (float level : {0.f, 0.5f, 1.f}) {
        Rng rng(7);
        for (int k = 0; k < 300; ++k) {
            const Spawn sp = targetSpawn(TargetSetup::Pass, level, goal, RoofParams{}, rng);
            CHECK(length(sp.carReleaseVel) < 1.f);  // the car starts at rest
            const pr::PredictedPath path = simulate(sp.ball.pos, sp.ballReleaseVel, sp.ballReleaseAngVel);
            float tBall = -1.f;
            for (int i = 0; i < path.size(); ++i)
                if (std::fabs(path.samples[static_cast<size_t>(i)].pos.x) < 100.f) {
                    tBall = path.samples[static_cast<size_t>(i)].t;
                    break;
                }
            const pr::InterceptResult it = intercept(path, sp.car, {}, false);
            CHECK(tBall > 0.f && it.found);
            const float d = tBall - it.t;
            CHECK_MSG(d >= 0.2f && d <= 1.0f, std::to_string(level) + ": " + std::to_string(d));
        }
    }
    // Niveau 1: from 1500 uu on the side at 550 uu/s, rolling.
    Rng rng(1);
    const Spawn s0 = targetSpawn(TargetSetup::Pass, 0.f, goal, RoofParams{}, rng);
    CHECK_NEAR(std::fabs(s0.ball.pos.x), 1500.f, 1e-3f);
    CHECK_NEAR(std::fabs(s0.ballReleaseVel.x), 550.f, 1e-3f);
    CHECK(length(s0.ballReleaseAngVel) > 1.f);
}

// ==== Puissance ==================================================================================================
TEST(shooting_power_is_reachable_with_speed) {
    const GoalFrame goal{1.f};
    Rng rng(9);
    for (int k = 0; k < 200; ++k) {
        const Spawn sp = powerSpawn(25.f / kUusToKph, goal, rng);
        const pr::PredictedPath path = simulate(sp.ball.pos, sp.ballReleaseVel, sp.ballReleaseAngVel);
        const pr::InterceptResult it = intercept(path, sp.car, {}, true);
        CHECK(it.found);
        if (!it.found) continue;
        CHECK(it.t <= 2.5f);
        CHECK(it.carSpeed >= 1800.f);
        CHECK(goal.sign * (it.ballPos.y - sp.car.pos.y) > 0.f);  // met in front of the car's start
    }
    // Adaptive bar: 70 km/h at niveau 1 (72 passes, 65 fails), 110 at niveau 10; 3 successes = about +5 km/h.
    CHECK_NEAR(powerGoalKph(0.f), 70.f, 1e-4f);
    CHECK_NEAR(powerGoalKph(1.f), 110.f, 1e-4f);
    PowerShot shot;
    shot.speed = 72.f / kUusToKph;
    CHECK(gradePower(shot, powerGoalKph(0.f) / kUusToKph, 0.f).success);
    shot.speed = 65.f / kUusToKph;
    CHECK(!gradePower(shot, powerGoalKph(0.f) / kUusToKph, 0.f).success);
    CHECK_NEAR(powerGoalKph(3.f * kPowerStep) - powerGoalKph(0.f), 5.f, 0.01f);
    // Module: a new player is asked 70 km/h; the fixed goal returns when the adaptive goal is off.
    Harness h;
    CHECK_NEAR(h.mod.powerGoal() * kUusToKph, 70.f, 0.01f);
    h.set("rlt_sh_power_adaptive", "0");
    CHECK_NEAR(h.mod.powerGoal() * kUusToKph, 90.f, 0.01f);
}

TEST(shooting_power_drill_moves_its_staircase_and_draws_the_run_up) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("power");
    tickWorld(h, w, 3);
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    auto circleLines = [&]() {
        int n = 0;
        for (const auto& l : h.draw(v).lines3D)
            if (std::fabs(l.a.z - 6.f) < 1e-3f) ++n;
        return n;
    };
    CHECK_EQ(circleLines(), 0);  // a prediction: off by default
    h.set("rlt_sh_floor_marks", "1");
    CHECK(circleLines() >= 28);  // the run-up circle
    pressToLive(h, w);
    w.car.dodging = true;
    tickWorld(h, w, 6);
    const Snapshot pre = w.snap();
    w.ball.vel = {0.f, 80.f / kUusToKph, 100.f};  // 80 km/h: over the niveau 1 goal
    const Snapshot post = w.snap();
    h.touch(touchOf(pre, post, post));
    tickWorld(h, w, 40);
    CHECK(h.results.size() == 1);
    if (!h.results.empty()) CHECK(h.results[0].success);
    CHECK(h.mod.powerStaircase().attempts() == 1);
    CHECK(h.mod.powerStaircase().level() > 0.f);
    h.command("reset_level", {"power"});
    CHECK(h.mod.powerStaircase().attempts() == 0);
}

// ==== Parcours complets ==========================================================================================
namespace {

// Physics world for the parcours: the ball follows the core physics, the car a simple kinematic model, a contact is a
// scripted shot (or a soft push) and is delivered to the module as a touch event.
struct ShootWorld {
    Harness& h;
    CarState car;
    pr::SimBall ball;
    int64_t frame = 3000;
    float yawDeg = 90.f;
    float speed = 0.f;
    bool frozen = false;
    bool contact = false;
    int touches = 0;
    Inputs in;
    bool rides = false;  // the ball sits on the roof (scripted pickup)

    explicit ShootWorld(Harness& harness) : h(harness) {
        car = carAt({0.f, -2500.f, 17.f}, rotDeg(0.f, 90.f, 0.f));
        ball.pos = {0.f, 0.f, 93.15f};
    }
    Snapshot snap() const {
        Snapshot s = snapOf(frame, car, ballAt(ball.pos, ball.vel), in);
        s.ball.angVel = ball.angVel;
        return s;
    }
    void apply(const Actions& a) {
        if (a.setCar) {
            car.pos = a.setCar->pos;
            car.rot = a.setCar->rot;
            car.basis = basisFromRot(car.rot);
            yawDeg = static_cast<float>(car.rot.yaw) * 360.f / 65536.f;
            speed = dot(a.setCar->vel, car.basis.fwd);
        }
        if (a.setBall) {
            ball.pos = a.setBall->pos;
            ball.vel = a.setBall->vel;
            ball.angVel = a.setBall->angVel;
        }
        frozen = a.freezeCar;
    }
    // One tick. `steerTo` / `wantSpeed` drive the car; on a new contact the ball gets `shotVel` (or a soft push when
    // it is zero).
    Actions tick(const Vec3* steerTo, float wantSpeed, bool boost, const Vec3& shotVel = {}) {
        const float dt = kTickDt;
        in = Inputs{};
        if (steerTo) {
            in.throttle = wantSpeed > speed + 20.f ? 1.f : (wantSpeed < speed - 20.f ? -1.f : 0.f);
            in.boost = boost;
        }
        if (!frozen && steerTo) {
            const Vec3 d = flat(*steerTo - car.pos);
            if (length(d) > 1.f) {
                float err = std::atan2(d.y, d.x) * kRadToDeg - yawDeg;
                while (err > 180.f) err -= 360.f;
                while (err < -180.f) err += 360.f;
                const float v = std::max(std::fabs(speed), 300.f);
                const float rate = v * 0.004f * kRadToDeg;
                yawDeg += std::clamp(err, -rate * dt, rate * dt);
            }
            const float acc = wantSpeed > speed ? (speed < 1400.f ? 1600.f - 1440.f * std::fabs(speed) / 1400.f : 0.f) +
                                                      (boost ? 991.67f : 0.f)
                                                : -3500.f;
            speed += acc * dt;
            if ((acc > 0.f && speed > wantSpeed) || (acc < 0.f && speed < wantSpeed)) speed = wantSpeed;
            speed = std::clamp(speed, -1410.f, 2300.f);
        } else if (!frozen) {
            speed -= std::copysign(std::min(std::fabs(speed), 525.f * dt), speed);
        }
        if (!frozen) {
            car.rot = rotFromYawDeg(yawDeg);
            car.basis = basisFromRot(car.rot);
            car.pos += car.basis.fwd * (speed * dt);
        }
        car.vel = car.basis.fwd * speed;
        const Snapshot pre = snap();
        if (rides) {
            ball.pos = roofSpawn(car.pos, car.basis, hitboxOf(car), 91.25f, 0.f);
            ball.vel = car.vel;
            ball.angVel = {};
        } else {
            pr::stepBall(ball);
        }
        ++frame;
        const Vec3 local = car.basis.toLocal(ball.pos - car.pos) - Vec3{13.88f, 0.f, 20.75f};
        const Vec3 half{59.f, 42.1f, 18.08f};
        const Vec3 q{std::clamp(local.x, -half.x, half.x), std::clamp(local.y, -half.y, half.y),
                     std::clamp(local.z, -half.z, half.z)};
        const bool touching = !rides && length(local - q) <= 91.25f;
        Actions out;
        if (touching && !contact) {
            ++touches;
            const Vec3 n = normalize(car.basis.toWorld(local - q), Vec3{0.f, 0.f, 1.f});
            if (length(shotVel) > 1.f) {
                ball.vel = shotVel;
            } else {
                const Vec3 rel = ball.vel - car.vel;
                const float vn = dot(rel, n);
                if (vn < 0.f) ball.vel -= n * vn;
                ball.vel += n * 150.f;  // soft push
            }
            ball.pos = car.pos + car.basis.toWorld(q + Vec3{13.88f, 0.f, 20.75f}) + n * 92.f;
            const Snapshot post = snap();
            h.touch(touchOf(pre, post, post));
        }
        contact = touching;
        out = h.tick(snap());
        apply(out);
        return out;
    }
    void toLive() {
        for (int i = 0; i < 900 && h.mod.phase() != ShootPhase::Live; ++i) {
            const Vec3 here = car.pos + car.basis.fwd * 100.f;
            tick(&here, i < 2 ? 0.f : 2000.f, false);  // released once, then the throttle (the car is held)
        }
    }
};

// Velocity that sends the ball from `from` to `target` (on the goal plane) in `t` s (drag ignored, gravity kept).
Vec3 shotTo(const Vec3& from, const Vec3& target, float t) {
    Vec3 v = (target - from) / t;
    v.z += 0.5f * 650.f * t;
    return v;
}

} // namespace

TEST(shooting_full_flick_pickup_at_speed_carry_flick) {
    for (int niveau : {1, 10}) {
        Harness h;
        if (niveau > 1) loadLevel(h, "shooting.stair.pickup", levelOf(niveau));
        ShootWorld w(h);
        h.tick(w.snap());
        h.command("flick", {"full"});
        CHECK(h.mod.fullMode());
        w.toLive();
        CHECK(h.mod.phase() == ShootPhase::Live);
        CHECK(h.mod.fullSetup().pickup && h.mod.fullSetup().chase && !h.mod.fullSetup().rolling);
        // Like a match: at GO the car already drives toward the goal from 55 m, the ball rolls ahead of it, slower.
        const float ballSpeed = length2D(w.ball.vel);
        CHECK(w.speed >= 699.f && w.speed <= 921.f);
        CHECK(ballSpeed >= 440.f && ballSpeed < w.speed - 200.f);
        CHECK(dot(w.ball.vel, w.car.basis.fwd) > 0.f);
        CHECK(h.mod.goal().distanceToPlane(w.car.pos) > 5000.f);
        // Pickup at speed (scripted climb: the ball is put on the roof when the nose reaches it): keep 250 uu/s more
        // than the ball until the nose is under it, then on the roof 0.5 s and settled 0.3 s while driving on.
        int ticks = 0;
        for (; ticks < 1500 && !w.rides; ++ticks) {
            const float d = length2D(w.ball.pos - hitboxCenter(w.car));
            w.tick(&w.ball.pos, length2D(w.ball.vel) + 250.f, false);
            if (d < 170.f) w.rides = true;
        }
        CHECK(w.rides);
        CHECK_MSG(ticks < static_cast<int>(2.5f * kTickRate), std::to_string(ticks));  // a short pickup
        const Vec3 goal{0.f, 5120.f, 0.f};
        const float keep = std::max(700.f, w.speed - 100.f);
        for (int i = 0; i < 110; ++i) w.tick(&goal, keep, false);  // R1 0.5 s then settle 0.3 s
        CHECK(h.mod.fullSteps().done(pickup::kStepRoof) && h.mod.fullSteps().done(pickup::kStepSettle));
        for (int i = 0; i < 130; ++i) w.tick(&goal, keep, false);  // carry
        CHECK(h.mod.fullSteps().done("carry"));
        // Room left to choose the moment of the flick before the 12 m line.
        CHECK_MSG(h.mod.goal().distanceToPlane(w.car.pos) > kFlickLateDist + 800.f,
                  std::to_string(h.mod.goal().distanceToPlane(w.car.pos)));
        // The flick (scripted like the drill tests).
        FlickScript s;
        s.start = w.car.pos;
        s.yawDeg = w.yawDeg;
        s.startFrame = w.frame;
        s.carSpeed = w.speed;
        s.launch = {1600.f, 0.f, 350.f};
        const ScriptRun run = buildFlick(s);
        for (size_t i = 0; i < run.snaps.size(); ++i) {
            for (const auto& t : run.touches)
                if (t.first == i) h.touch(t.second);
            h.tick(run.snaps[i]);
        }
        CHECK(h.results.size() == 1);
        if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.results[0].title);
        CHECK(h.mod.fullSteps().isComplete());
        CHECK(h.mod.fullStaircase(ShootDrill::Flick).attempts() == 1);
        CHECK(h.mod.fullStaircase(ShootDrill::Flick).successes() == 1);
        CHECK(h.mod.pickupStaircase().attempts() == (niveau > 1 ? 2 : 1));
        CHECK(h.mod.flickStaircase().attempts() == 0);  // the short drill's level is not touched
    }
}

// The pickup at speed with the core physics: the ball rolls ahead (pr::stepBall, drag and rolling), the car follows at a
// steady speed a little faster: the nose meets it soon, at a gentle relative speed, far from the goal and the walls.
TEST(shooting_pickup_at_speed_is_reachable_with_the_core_physics) {
    for (int niveau = 1; niveau <= 10; ++niveau) {
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Rng rng(seed);
            const GoalFrame goal{seed % 2 ? 1.f : -1.f};
            const FullSetup su = makeFullSetup(ShootDrill::Flick, 0.f, niveau, goal, RoofParams{}, rng);
            CHECK(su.chase && su.pickup);
            const Vec3 fwd = normalize(flat(su.car.vel));
            const float carSpeed = length(su.car.vel);
            CHECK(throttleCanHold(carSpeed));
            pr::SimBall b;
            b.pos = su.ball.pos;
            b.vel = su.ball.vel;
            b.angVel = su.ball.angVel;
            Vec3 car = su.car.pos;
            float t = 0.f, rel = 0.f;
            bool met = false;
            for (; t < 4.f && !met; t += kTickDt) {
                pr::stepBall(b);
                car += fwd * (carSpeed * kTickDt);
                const float gap = dot(b.pos - car, fwd);
                if (gap <= 13.88f + 59.f + 91.25f) {
                    met = true;
                    rel = carSpeed - dot(b.vel, fwd);
                }
                CHECK(b.pos.z < 95.f);  // it keeps rolling on the floor
            }
            CHECK_MSG(met && t <= 2.f, std::to_string(niveau) + ": " + std::to_string(t));
            CHECK_MSG(rel >= 200.f && rel <= 450.f, std::to_string(rel));  // the gentle catch of the pickup helper
            CHECK(goal.distanceToPlane(car) > 3500.f);
            CHECK(std::fabs(b.pos.x) < kArenaHalfX - 600.f);
        }
    }
}

TEST(shooting_full_flick_failed_pickup_feeds_only_the_pickup_staircase) {
    Harness h;
    ShootWorld w(h);
    h.tick(w.snap());
    h.command("flick", {"full"});
    w.toLive();
    // On the roof for 0.5 s, then the ball drops before it is stable: the pickup fails with its tip.
    w.rides = true;
    const Vec3 goal{0.f, 5120.f, 0.f};
    for (int i = 0; i < 62; ++i) w.tick(&goal, 0.f, false);
    CHECK(h.mod.fullSteps().done(pickup::kStepRoof));
    w.rides = false;
    w.ball.pos = w.car.pos + w.car.basis.fwd * 400.f;
    w.ball.pos.z = 93.15f;
    w.ball.vel = {};
    for (int i = 0; i < 20 && h.mod.phase() == ShootPhase::Live; ++i) w.tick(&goal, 0.f, false);
    CHECK(h.mod.phase() == ShootPhase::Result);
    CHECK(h.mod.status().lastResult.tips.size() == 1);
    CHECK(h.mod.status().message.find("Il manquait : ") == 0);
    CHECK(h.mod.pickupStaircase().attempts() == 1 && h.mod.pickupStaircase().successes() == 0);
    CHECK(h.mod.fullStaircase(ShootDrill::Flick).attempts() == 0);
    CHECK(h.mod.flickStaircase().attempts() == 0);
    CHECK(h.results.empty());  // nothing sent to the coach: no flick was made
}

TEST(shooting_full_rolling_conduite_et_tir) {
    Harness h;
    ShootWorld w(h);
    h.tick(w.snap());
    h.command("targets", {"center", "rolling", "full"});
    w.toLive();
    const float go = static_cast<float>(w.frame) / kTickRate;
    bool shot = false;
    for (int i = 0; i < 16 * 120 && h.mod.phase() == ShootPhase::Live; ++i) {
        const float t = static_cast<float>(w.frame) / kTickRate - go;
        if (t < kReact) {
            w.tick(nullptr, 0.f, false);
            continue;
        }
        const Vec3 goalC{0.f, 5120.f, 300.f};
        const float distGoal = 5120.f - w.ball.pos.y;
        const auto& steps = h.mod.fullSteps();
        Vec3 shotVel;
        float want = 900.f;
        if (steps.done("push") && distGoal < 3200.f && !shot) {
            shotVel = shotTo(w.ball.pos, goalC, 1.6f);  // the shot
            want = 1400.f;
        } else if (steps.done(kStepFirst)) {
            want = length2D(w.ball.vel) + 250.f;  // pushes it along
        } else {
            // Goes to the ball and slows down before it: the first touch is soft.
            want = length2D(w.ball.pos - w.car.pos) > 700.f ? 1200.f : 450.f;
        }
        const Vec3 aim = w.ball.pos - Vec3{0.f, 120.f, 0.f};
        w.tick(&aim, want, false, shotVel);
        if (length(shotVel) > 1.f && w.touches > 0 && steps.done(kStepShot)) shot = true;
    }
    for (int i = 0; i < 600 && h.mod.phase() == ShootPhase::Live; ++i) w.tick(nullptr, 0.f, false);
    CHECK(h.results.size() == 1);
    if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.mod.status().message + " / " + h.results[0].title);
    CHECK(h.mod.fullStaircase(ShootDrill::Rolling).successes() == 1);
}

TEST(shooting_full_bouncing_rebond_devant_le_but) {
    Harness h;
    ShootWorld w(h);
    h.tick(w.snap());
    h.command("targets", {"center", "bouncing", "full"});
    w.toLive();
    const FullSetup su = h.mod.fullSetup();
    CHECK(su.bounces.size() >= 2);
    const float go = static_cast<float>(w.frame) / kTickRate;
    for (int i = 0; i < 14 * 120 && h.mod.phase() == ShootPhase::Live; ++i) {
        const float t = static_cast<float>(w.frame) / kTickRate - go;
        if (t < kReact) {
            w.tick(nullptr, 0.f, false);
            continue;
        }
        // Arrive at the second bounce just as the ball comes down there, then hit it as it rises.
        const Vec3 spot = su.target - w.car.basis.fwd * 150.f;
        const float d = length2D(spot - hitboxCenter(w.car));
        const float left = su.target.y > 0.f ? su.targetTime - t : 0.f;
        float want = left > 0.f ? std::min(1400.f, d / std::max(0.2f, left - 0.05f)) : 900.f;
        if (d < 80.f && left > 0.2f) want = 0.f;
        const Vec3 goalC{0.f, 5120.f, 250.f};
        const Vec3 shot = shotTo(w.ball.pos, goalC, 1.f);
        w.tick(&su.target, want, false, h.mod.fullSteps().done("arrive") ? shot : Vec3{});
    }
    for (int i = 0; i < 600 && h.mod.phase() == ShootPhase::Live; ++i) w.tick(nullptr, 0.f, false);
    CHECK(h.results.size() == 1);
    if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.mod.status().message);
}

TEST(shooting_full_pass_appel_de_balle) {
    Harness h;
    ShootWorld w(h);
    h.tick(w.snap());
    h.command("targets", {"center", "pass", "full"});
    w.toLive();
    const FullSetup su = h.mod.fullSetup();
    CHECK(su.targetTime > 2.f);
    const float go = static_cast<float>(w.frame) / kTickRate;
    for (int i = 0; i < 14 * 120 && h.mod.phase() == ShootPhase::Live; ++i) {
        const float t = static_cast<float>(w.frame) / kTickRate - go;
        if (t < kReact) {
            w.tick(nullptr, 0.f, false);
            continue;
        }
        // Times the run: arrives at the meeting point about 0.3 s before the ball, then shoots first time.
        const float d = length2D(su.target - hitboxCenter(w.car));
        const float left = su.targetTime - 0.3f - t;
        const float want = left > 0.05f ? std::min(1400.f, d / left) : 1000.f;
        const Vec3 goalC{0.f, 5120.f, 250.f};
        w.tick(&su.target, want, false, shotTo(w.ball.pos, goalC, 0.9f));
    }
    for (int i = 0; i < 600 && h.mod.phase() == ShootPhase::Live; ++i) w.tick(nullptr, 0.f, false);
    CHECK(h.results.size() == 1);
    if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.mod.status().message);
    // Waiting at the meeting point from the start: "Démarre au bon moment" is the missing step.
    Harness g;
    ShootWorld wg(g);
    g.tick(wg.snap());
    g.command("targets", {"center", "pass", "full"});
    wg.toLive();
    for (int i = 0; i < 8 * 120 && g.mod.phase() == ShootPhase::Live; ++i) wg.tick(nullptr, 0.f, false);
    CHECK(g.mod.phase() == ShootPhase::Result);
    CHECK(g.mod.status().message.find("Démarre") != std::string::npos);
    CHECK(g.mod.fullStaircase(ShootDrill::Pass).attempts() == 1);
}

TEST(shooting_full_power_frappe_de_degagement) {
    Harness h;
    ShootWorld w(h);
    h.tick(w.snap());
    h.command("power", {"full"});
    w.toLive();
    const float go = static_cast<float>(w.frame) / kTickRate;
    for (int i = 0; i < 10 * 120 && h.mod.phase() == ShootPhase::Live; ++i) {
        const float t = static_cast<float>(w.frame) / kTickRate - go;
        if (t < kReact) {
            w.tick(nullptr, 0.f, false);
            continue;
        }
        const float d = distance(hitboxCenter(w.car), w.ball.pos);
        w.car.dodging = d < 260.f;  // front flip into the ball just before the contact
        w.car.dodgeDir = Vec3{1.f, 0.f, 0.f};
        const Vec3 goalC{0.f, 5120.f, 300.f};
        w.tick(&w.ball.pos, 2300.f, true, normalize(goalC - w.ball.pos, Vec3{0.f, 1.f, 0.f}) * (95.f / kUusToKph));
    }
    for (int i = 0; i < 100 && h.mod.phase() == ShootPhase::Live; ++i) w.tick(nullptr, 0.f, false);
    CHECK(h.results.size() == 1);
    if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.mod.status().message + " / " + h.results[0].title);
    CHECK(h.mod.fullStaircase(ShootDrill::Power).successes() == 1);
}

TEST(shooting_texts_lexicon_tutorials_and_levels) {
    for (int i = 0; i < kShootDrillCount; ++i) {
        const auto d = static_cast<ShootDrill>(i);
        ShootDrill back = ShootDrill::Power;
        CHECK(parseShootDrill(shootDrillId(d), back) && back == d);
        const ShootTutorial& t = shootTutorial(d);
        CHECK(t.steps.size() >= 3 && t.steps.size() <= 4);
        CHECK(shootPanelCount(d) >= 3);
        auto clean = [](const std::string& s) {
            return s.find("\xE2\x80\x94") == std::string::npos && s.find("\xE2\x80\x93") == std::string::npos;
        };
        auto noDigits = [](const std::string& s) { return s.find_first_of("0123456789") == std::string::npos; };
        CHECK(clean(t.intro) && clean(t.level1) && noDigits(t.level1) && noDigits(t.intro));
        for (const auto& st : t.steps) {
            CHECK(clean(st.title) && clean(st.why) && clean(st.ok) && clean(st.todo));
            CHECK(noDigits(st.title) && noDigits(st.why) && noDigits(st.ok) && noDigits(st.todo));
        }
        for (int k = 0; k < shootPanelCount(d); ++k) CHECK(noDigits(shootPanelLine(d, k).substr(4)));
        for (const char* x : t.tech) CHECK(clean(x));
        for (const auto& l : fullLabels(d, 1)) CHECK(clean(l));
        CHECK(std::string(t.fullTitle).find("Parcours complet : ") == 0);
    }
    CHECK(std::string(shootLexicon()).find("Supersonique") != std::string::npos);
    CHECK(std::string(shootProgressRule()).find("3 essais sur 4") != std::string::npos);
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}};
    CHECK(shootFillButtons("Appuie sur {jump}.", p, KeyStyle::Auto) == "Appuie sur saut (A).");
    // "Revenir au niveau 1" resets one drill and its parcours complet.
    Harness h;
    loadLevel(h, "shooting.stair.bouncing", 0.6f);
    loadLevel(h, "shooting.full.bouncing", 0.4f);
    CHECK_NEAR(h.mod.fullStaircase(ShootDrill::Bouncing).level(), 0.4f, 1e-6f);
    h.command("reset_level", {"bouncing"});
    CHECK_NEAR(h.mod.targetLevel(TargetSetup::Bouncing), 0.f, 1e-6f);
    CHECK_NEAR(h.mod.fullStaircase(ShootDrill::Bouncing).level(), 0.f, 1e-6f);
    h.command("reset_level", {"nope"});
    CHECK(!h.toasts.empty() && h.toasts.back().kind == ToastKind::Warning);
}
