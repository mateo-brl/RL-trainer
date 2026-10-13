// Regression tests pinning the fixes of the module review: air dribble with a flip finish (was graded as a delayed
// flick), flick attempt released when the flip expires, drill fail grace cancelled when the situation recovers,
// rolling-ball goal projection, near-miss wheel count, game flip reset events outside analysed modes, wrong mechanic
// tip in drills, double precision score accumulator.
#include "core/common/Store.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Stats.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace rlt::bm;
using namespace bmtest;

namespace {
std::vector<SimTick> continueFrom(const Harness& h, std::vector<SimTick> ticks) {
    if (ticks.empty()) return ticks;
    const int64_t offset = h.latest.frame + 1 - ticks.front().s.frame;
    for (auto& t : ticks) {
        t.s.frame += offset;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    return ticks;
}

void warmUp(Harness& h, int n = 10) {
    Sim sim;
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.ball.pos = {0, 1000, 93};
    sim.step(n);
    h.run(sim.out);
}

void skipReady(Harness& h) { h.idle(static_cast<int>(h.settings.ballmech.drillReady * kTickRate) + 2); }

// Index of the first airborne tick (the jump of a carry scenario).
size_t jumpIndex(const std::vector<SimTick>& ticks) {
    for (size_t i = 0; i < ticks.size(); ++i)
        if (ticks[i].s.car.wheelContacts == 0) return i;
    return ticks.size();
}

// Carry on the roof for 0.4 s, jump, then hover with the ball on the roof (no dodge). `flipUntil`: ticks after the
// jump during which the game reports the flip as available (-1 = always, 0 = never).
std::vector<SimTick> carryJumpHover(int flipUntil, int airTicks = 240) {
    Sim sim;
    setGround(sim.s.car);
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.car.pos = {0, -3000, arena::kCarRestZ};
    sim.s.car.vel = {0, 800, 0};
    sim.attached = true;
    sim.attachOffset = {0, 20, 125};
    sim.s.ball.pos = sim.s.car.pos + sim.attachOffset;
    sim.s.ball.vel = sim.s.car.vel;
    sim.step(48);
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.jumped = true;
        x.in.jump = true;
        x.car.vel.z = 120;
    });
    sim.step(airTicks, [&](Snapshot& x, int i) {
        x.in.jump = false;
        x.car.hasFlip = flipUntil < 0 || i < flipUntil;
    });
    return sim.out;
}

// Runs the flick detector and returns, per tick, whether it is engaged.
std::vector<bool> flickEngaged(const std::vector<SimTick>& ticks) {
    FlickDetector fd;
    const auto frames = buildFrames(ticks);
    std::vector<bool> on(frames.size(), false);
    for (size_t i = 1; i < frames.size(); ++i) {
        (void)fd.update(frames[i - 1], frames[i], refParams());
        float since = 0.f;
        on[i] = fd.engaged(since);
    }
    return on;
}
} // namespace

// ---- Air dribble ending with a flip: not a delayed flick ------------------------------------------------------------
TEST(bm_fix_airdribble_flip_finish_detectors) {
    const auto seq = airDribbleFlipFinish();
    FlickDetector fd;
    CHECK(run(fd, seq).empty());  // the ball was bumped 8 times after the takeoff: not a carried flick
    AirDribbleDetector ad;
    auto g = run(ad, seq);
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) {
        CHECK(g[0].success);
        CHECK(!g[0].weak);
        CHECK(g[0].variant.rfind("ground_to_air", 0) == 0);
        CHECK(g[0].value("touches") >= 9.f);  // 8 bumps + the flip touch (+ the pop)
        CHECK(g[0].value("duration") >= 0.8f);
    }
}

TEST(bm_fix_airdribble_flip_finish_free_play) {
    Harness h;
    h.run(airDribbleFlipFinish());
    h.idle(60);
    CHECK_EQ(h.countSkill("ball.flick"), 0);
    CHECK_EQ(h.countSkill("ball.airdribble"), 1);
    CHECK_EQ(h.results.size(), 1u);  // the flip touch (aerial) is absorbed by the air dribble
    if (!h.results.empty()) {
        CHECK(h.results[0].title.rfind("Air dribble depuis le sol", 0) == 0);
        CHECK(h.results[0].success);
    }
    CHECK(h.mod.uiState().bestAirDribble >= 0.8f);  // the record is updated
}

TEST(bm_fix_airdribble_flip_finish_drill) {
    Harness h;
    warmUp(h);
    h.command("level", {"airdribble", "0"});
    h.command("drill", {"airdribble"});
    skipReady(h);
    h.run(continueFrom(h, airDribbleFlipFinish()));
    h.idle(60);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.airdribble");
        CHECK(h.results[0].success);
        CHECK(!hasTip(h.results[0], "bm.ad_short"));
    }
    const auto u = h.mod.uiState();
    CHECK_EQ(u.drill.successes, 1);
    CHECK(u.drill.lastVerdict.find("Raté") == std::string::npos);
    CHECK(u.bestAirDribble >= 0.8f);
}

TEST(bm_fix_flick_tolerates_one_recontact) {
    // A real delayed flick: the ball leaves the roof once at the end of the jump and lands back on it.
    auto seq = flickScenario(FK::FrontDelayed);
    const size_t j = jumpIndex(seq);
    CHECK(j + 40 < seq.size());
    if (j + 40 >= seq.size()) return;
    seq[j + 36].touched = true;
    seq[j + 36].newContact = true;
    FlickDetector one;
    auto g = run(one, seq);
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) CHECK(g[0].variant == "front_delayed");
    // A second separate contact: the ball is being bumped, the dodge is no longer a flick.
    seq[j + 60].touched = true;
    seq[j + 60].newContact = true;
    FlickDetector two;
    CHECK(run(two, seq).empty());
    // Continuous contact (no new contact) never gives up.
    auto cont = flickScenario(FK::FrontDelayed);
    for (size_t i = j + 13; i < j + 80 && i < cont.size(); ++i) {
        cont[i].touched = true;
        cont[i].newContact = false;
    }
    FlickDetector c;
    auto gc = run(c, cont);
    CHECK_EQ(gc.size(), 1u);
    if (!gc.empty()) CHECK(gc[0].variant == "front_delayed");
}

TEST(bm_fix_flick_releases_when_flip_expires) {
    // Flip reported until 1.3 s after the jump: the attempt ends a few ticks later (it held other grades for 4 s).
    {
        const auto seq = carryJumpHover(156);
        const auto on = flickEngaged(seq);
        const size_t j = jumpIndex(seq);
        CHECK(on[j + 2]);
        CHECK(on[j + 150]);
        CHECK(!on[j + 156 + 3]);
        CHECK(!on.back());
    }
    // Flip still reported: the 1.5 s cap ends it.
    {
        const auto seq = carryJumpHover(-1);
        const auto on = flickEngaged(seq);
        const size_t j = jumpIndex(seq);
        CHECK(on[j + 170]);
        CHECK(!on[j + 182]);
    }
    // The game never reports the flip (unreliable flag): only the time cap applies, the attempt is not cut early.
    {
        const auto seq = carryJumpHover(0);
        const auto on = flickEngaged(seq);
        const size_t j = jumpIndex(seq);
        CHECK(on[j + 120]);
        CHECK(!on[j + 182]);
    }
}

// ---- Drill fail grace ----------------------------------------------------------------------------------------------
TEST(bm_fix_drill_landing_cancelled_by_takeoff) {
    Harness h;
    warmUp(h);
    h.command("drill", {"airdribble"});
    skipReady(h);
    Sim sim;
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.car.pos = {0, -2000, arena::kCarRestZ};
    sim.s.ball.pos = {0, 1000, 93};
    sim.step(5, [](Snapshot& x, int) { x.in.throttle = 1.f; });
    // 30-tick hop.
    sim.step(30, [](Snapshot& x, int i) {
        setAir(x.car);
        x.car.pos.z = arena::kCarRestZ + 100.f * std::sin(PI * static_cast<float>(i + 1) / 31.f);
    });
    // 10 ticks on the ground, then airborne for 1 s.
    sim.step(10, [](Snapshot& x, int) {
        setGround(x.car);
        x.car.pos.z = arena::kCarRestZ;
    });
    sim.step(120, [](Snapshot& x, int i) {
        setAir(x.car);
        x.car.pos.z = arena::kCarRestZ + 6.f * static_cast<float>(i + 1);
    });
    h.run(continueFrom(h, sim.out));
    CHECK(h.results.empty());  // not failed in flight
    CHECK(h.mod.uiState().drill.phase == "À toi !");
    // Landing for good still fails the attempt after the grace delay.
    Sim land;
    land.s = h.latest;
    land.step(100, [](Snapshot& x, int) {
        setGround(x.car);
        x.car.pos.z = arena::kCarRestZ;
        x.car.vel = {};
    });
    h.run(continueFrom(h, land.out));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.drill_landed"));
}

TEST(bm_fix_drill_landing_bounce_does_not_cancel) {
    // A landing bounce (wheels off for a tick, car still low) keeps the pending fail.
    Harness h;
    warmUp(h);
    h.command("drill", {"airroll"});
    skipReady(h);
    Sim sim;
    setAir(sim.s.car);
    sim.s.car.pos = {0, 1300, 500};
    sim.s.ball.pos = {0, 3000, 1000};
    sim.step(40);
    sim.step(150, [](Snapshot& x, int i) {
        if (i % 20 == 5) setAir(x.car);
        else setGround(x.car);
        x.car.pos.z = arena::kCarRestZ + (i % 20 == 5 ? 6.f : 0.f);
        x.car.vel = {};
    });
    h.run(continueFrom(h, sim.out));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.drill_landed"));
}

TEST(bm_fix_drill_ball_lost_cancelled_when_ball_returns) {
    Harness h;
    warmUp(h);
    h.command("drill", {"flick"});
    skipReady(h);
    Sim sim;
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.car.pos = {0, 0, arena::kCarRestZ};
    sim.s.ball.pos = {0, 420, 93};
    sim.step(60, [](Snapshot& x, int) { x.in.throttle = 0.2f; });  // 0.5 s with the ball out of reach
    sim.step(1, [](Snapshot& x, int) { x.ball.vel = {0, -1500, 0}; });
    sim.step(12, [](Snapshot& x, int) { x.in.throttle = 0.2f; });    // the ball rolls back within reach
    sim.step(1, [](Snapshot& x, int) { x.ball.vel = {}; });
    sim.step(120, [](Snapshot& x, int) { x.in.throttle = 0.2f; });
    h.run(continueFrom(h, sim.out));
    CHECK(distance(h.latest.car.pos, h.latest.ball.pos) <= 300.f);
    CHECK(h.results.empty());
    // Out of reach again for good: the attempt fails.
    Sim away;
    away.s = h.latest;
    away.s.ball.vel = {0, 1200, 0};
    away.step(140);
    h.run(continueFrom(h, away.out));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.drill_ball_lost"));
}

TEST(bm_fix_drill_wrong_mechanic_tip) {
    // A flick drill ended by a power shot: the main tip says it was not the requested mechanic.
    Harness h;
    warmUp(h);
    h.command("drill", {"flick", "any"});
    skipReady(h);
    h.run(continueFrom(h, powerShotScenario(PS::FlipShot)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        const AttemptResult& r = h.results[0];
        CHECK(r.skill == "ball.flick");
        CHECK(!r.success);
        CHECK(!r.tips.empty() && r.tips[0].code == "bm.drill_wrong_mech");
        CHECK(hasTip(r, "bm.fl_no_contact"));  // the drill's own advice comes second
        CHECK(r.tips.size() >= 2 && r.tips[1].code == "bm.fl_no_contact");
    }
    CHECK(h.mod.uiState().drill.lastVerdict.find("Frappe en flip") != std::string::npos);
    CHECK_EQ(h.countSkill("ball.powershot"), 0);
}

// ---- Goal projection of a rolling ball --------------------------------------------------------------------------------
TEST(bm_fix_projection_rolling_ball) {
    const float r = 91.25f;
    GoalProjection a = projectToGoal({0, 0, r}, {0, 1500, 0}, 0, r, true);
    CHECK(a.reaches && a.onTarget);
    CHECK(a.t > 3.2f && a.t < 3.9f);
    GoalProjection b = projectToGoal({0, 2000, r}, {0, 1000, 0}, 0, r, true);
    CHECK(b.reaches && b.onTarget);
    GoalProjection c = projectToGoal({0, 1000, r}, {0, 1600, 0}, 0, r, true);
    CHECK(c.reaches);
    CHECK_NEAR(c.t, 2.6f, 0.15f);  // was 3.6 s with the micro-bounce friction
    // Orange team, rolling toward -Y.
    GoalProjection o = projectToGoal({0, -1000, r}, {0, -1600, 0}, 1, r, true);
    CHECK(o.reaches && o.onTarget);
    // A ball resting on the floor with a tiny vertical speed is rolling too.
    CHECK(projectToGoal({0, 1000, r + 1.f}, {0, 1600, -20.f}, 0, r, true).reaches);
    // A dropped ball bounces, then keeps its rolling speed (friction only until the spin matches).
    GoalProjection d = projectToGoal({0, 1000, 600}, {0, 1500, 0}, 0, r, true);
    CHECK(d.reaches && d.onTarget);
    // The drawn path reaches the goal line.
    CHECK(a.pointCount >= 2);
    if (a.pointCount >= 2) CHECK(a.points[static_cast<size_t>(a.pointCount - 1)].y >= arena::kBackWallY - r - 1.f);
    // Too slow to get there within the horizon.
    CHECK(!projectToGoal({0, 0, r}, {0, 600, 0}, 0, r, true).reaches);
}

// ---- Flip reset near miss: wheels counted at the contact ---------------------------------------------------------------
TEST(bm_fix_nearmiss_wheels_on_ball) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::NoReset));
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) {
        CHECK(g[0].variant == "no_reset");
        CHECK_NEAR(g[0].value("wheels_on_ball", -1.f), 1.f, 1e-6f);  // the scenario touches with one wheel
    }
}

// ---- Game flip reset events: counted only where detection runs ------------------------------------------------------
TEST(bm_fix_sdk_reset_gating) {
    auto sdk = [](Harness& h) {
        warmUp(h);
        h.event(GameEventKind::FlipReset, "sdk", h.latest.frame);
    };
    Harness on;
    sdk(on);
    CHECK_EQ(on.mod.sessionResets(), 1);
    CHECK_EQ(on.mod.totalResets(), 1);

    Harness detOff;
    detOff.set("rlt_bm_det_flipreset", "0");
    sdk(detOff);
    CHECK_EQ(detOff.mod.sessionResets(), 0);
    CHECK_EQ(detOff.mod.totalResets(), 0);

    Harness match;
    match.game.mode = GameMode::OfflineMatch;
    match.game.worldControlAllowed = false;
    match.set("rlt_bm_in_matches", "0");
    sdk(match);
    CHECK_EQ(match.mod.totalResets(), 0);
    Harness matchOn;  // analysed offline match: counted
    matchOn.game.mode = GameMode::OfflineMatch;
    matchOn.game.worldControlAllowed = false;
    sdk(matchOn);
    CHECK_EQ(matchOn.mod.totalResets(), 1);

    Harness replay;
    replay.game.mode = GameMode::Replay;
    replay.game.worldControlAllowed = false;
    sdk(replay);
    CHECK_EQ(replay.mod.sessionResets(), 0);
    CHECK_EQ(replay.mod.totalResets(), 0);

    Harness online;
    online.game.mode = GameMode::Online;
    online.game.worldControlAllowed = false;
    sdk(online);
    CHECK_EQ(online.mod.totalResets(), 0);

    // Detection off, but the flip reset drill runs: the drill counts its resets.
    Harness drill;
    drill.set("rlt_bm_det_flipreset", "0");
    warmUp(drill);
    drill.command("drill", {"flipreset"});
    CHECK(drill.mod.drillActive());
    drill.event(GameEventKind::FlipReset, "sdk", drill.latest.frame);
    CHECK_EQ(drill.mod.sessionResets(), 1);
}

// ---- Stats: double precision accumulator ---------------------------------------------------------------------------
TEST(bm_fix_stats_score_sum_double) {
    Stats a;
    MechStats& m = a.at(Mech::Aerial);
    double expected = 0.0;
    for (int i = 0; i < 30000; ++i) {
        const float score = 40.5f + static_cast<float>(i % 7);
        m.record(score, i % 3 == 0);
        expected += static_cast<double>(score);
    }
    const double sum = m.scoreSum;
    CHECK(sum == expected);
    CHECK(sum > 1e6);  // beyond the 6 significant digits of Store::setFloat
    Stats b = a;
    for (int cycle = 0; cycle < 5; ++cycle) {  // repeated save / load cycles do not drift
        Store st;
        b.save(st);
        Stats c;
        c.load(st);
        b = c;
    }
    CHECK(b.at(Mech::Aerial).scoreSum == sum);
    CHECK_EQ(b.at(Mech::Aerial).attempts, 30000);
    CHECK_NEAR(b.at(Mech::Aerial).average(), m.average(), 1e-6f);
    // Older saves (written with setFloat) still load.
    Store legacy;
    legacy.setInt("ballmech.stats.flick.attempts", 4);
    legacy.setFloat("ballmech.stats.flick.score_sum", 250.5f);
    Stats l;
    l.load(legacy);
    CHECK_NEAR(l.at(Mech::Flick).average(), 62.625f, 1e-4f);
    // Out of range sums are clamped to the attempts.
    Store bad;
    bad.setInt("ballmech.stats.flick.attempts", 2);
    bad.setDouble("ballmech.stats.flick.score_sum", 1e12);
    Stats x;
    x.load(bad);
    CHECK(x.at(Mech::Flick).average() <= 100.f);
}

// ---- Double tap: a shot over the bar is not a failed double tap when the car did not follow ---------------------------
namespace {
// Aerial shot that goes over the bar onto the backboard. `follow`: the car keeps flying toward the rebound; otherwise
// it drops back to the floor before the ball reaches the wall.
std::vector<SimTick> overTheBar(bool follow) {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    orient(s.car, PI / 2, 0.2f, 0);
    s.car.pos = {0, 3500, 600};
    s.car.vel = {0, 1000, 200};
    s.ball.pos = s.car.pos + s.car.basis.fwd * 140.f;
    sim.step(30, [&](Snapshot& x, int) { x.ball.pos = x.car.pos + x.car.basis.fwd * 140.f; });
    sim.hit({0, 1800, 900}, [&](Snapshot& x) { x.ball.pos = x.car.pos + x.car.basis.fwd * 140.f; });
    sim.ballGravity = true;
    sim.step(1, [&](Snapshot& x, int) { x.car.vel = follow ? Vec3{0, 900, 300} : Vec3{0, 800, -1300}; });
    for (int i = 0; i < 420; ++i) {
        sim.step(1, [&](Snapshot& x, int) {
            if (x.ball.pos.y >= arena::kBackWallY - x.ball.radius && x.ball.vel.y > 0.f) x.ball.vel.y = -0.6f * x.ball.vel.y;
            if (follow && x.car.pos.y >= 4300.f) x.car.vel = {};  // waits for the rebound (missed)
            if (!follow && x.car.pos.z <= arena::kCarRestZ) {
                x.car.pos.z = arena::kCarRestZ;
                x.car.vel = {};
                orient(x.car, PI / 2, 0, 0);
                setGround(x.car);
            }
        });
    }
    return sim.out;
}
} // namespace

TEST(bm_fix_doubletap_over_the_bar_is_an_aerial) {
    // The car is back on the floor when the ball hits the backboard: the aerial shot (over the bar) is reported.
    Harness h;
    h.run(overTheBar(false));
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.doubletap"), 0);
    CHECK_EQ(h.countSkill("ball.aerial"), 1);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.ae_off_target"));
    // The detector still grades it, as a weak drill-only claim.
    DoubleTapDetector dt;
    auto g = run(dt, overTheBar(false));
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) {
        CHECK(g[0].weak && g[0].drillOnly && !g[0].success);
        CHECK(g[0].value("car_dist_at_bounce") < 1500.f);  // close enough, but on the ground
    }
    // Following the ball in the air without the second touch: a real double tap attempt, reported as failed.
    Harness f;
    f.run(overTheBar(true));
    f.idle(30);
    CHECK_EQ(f.countSkill("ball.doubletap"), 1);
    CHECK_EQ(f.results.size(), 1u);
    if (!f.results.empty()) CHECK(!f.results[0].success);
}
