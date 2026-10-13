// PredictionModule regression tests for the second review: a velocity change landing on the regular recompute
// tick is still classed (forced recompute, outside disturbance), and the camera-view cache never survives from one
// view period to the next.
#include <cmath>
#include <string>

#include "tests/TestFramework.h"
#include "tests/prediction/PredictionHarness.h"

using namespace rlt;
using namespace rlt_test_pr;

namespace {
const float kR = pr::phys::kBallArenaRadius;

// Draws frames without physics ticks (local car demolished: the ball moves on) until the module switches to the
// camera view. Returns the draw of that first view frame, or an empty list after `maxFrames`.
DrawList drawUntilView(Harness& h, int maxFrames, int& framesUsed) {
    for (framesUsed = 1; framesUsed <= maxFrames; ++framesUsed) {
        h.advance();
        DrawList d = h.draw();
        if (h.mod.status().fromView) return d;
    }
    return DrawList{};
}
} // namespace

// ---- Velocity change on the regular recompute tick ----------------------------------------------------------
TEST(pr_push_on_any_tick_of_the_period_is_classed_the_same) {
    // A bot taps the ball (150 uu/s, far from the local car) 1, 2, 3 or 4 ticks after the last prediction. The 4th
    // is the regular recompute tick: it used to skip the comparison and the push ended up as 'model error'.
    for (int phase = 0; phase < 4; ++phase) {
        Harness h;
        h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
        h.tick();
        CHECK_EQ(h.mod.status().recomputes, 1);
        h.run(phase);
        CHECK_EQ(h.mod.status().recomputes, 1);
        h.advance();
        h.snap.ball.vel.x += 150.f;
        h.tick();
        CHECK_EQ(h.mod.status().recomputes, 2);
        CHECK_EQ(h.mod.status().forcedRecomputes, 1);
        CHECK(h.mod.path().start.vel == h.snap.ball.vel);
        h.run(PredictionModule::kProbeTicks);
        const PredictionAccuracy acc = h.mod.accuracy();
        CHECK_EQ(acc.count, 0);
        CHECK_EQ(acc.disturbed, 1);
    }
}

TEST(pr_push_with_a_one_tick_period_is_still_forced) {
    // With the period at its minimum every tick is a regular recompute: a push must still be seen as one.
    Harness h;
    CHECK(h.set("rlt_pr_recompute_ticks", "1"));
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(5);
    CHECK_EQ(h.mod.status().recomputes, 6);
    CHECK_EQ(h.mod.status().forcedRecomputes, 0);
    h.advance();
    h.snap.ball.vel.y -= 200.f;
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 7);
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    h.run(PredictionModule::kProbeTicks);
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 1);
}

TEST(pr_small_change_on_the_regular_tick_is_a_plain_recompute) {
    // Below the threshold on the regular tick: the period recomputes, nothing is forced, the probe keeps measuring.
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(3);
    h.advance();
    h.snap.ball.vel.x += 30.f;
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 2);
    CHECK_EQ(h.mod.status().forcedRecomputes, 0);
    CHECK(h.mod.path().start.vel == h.snap.ball.vel);
    h.run(PredictionModule::kProbeTicks);
    const PredictionAccuracy acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 1);
    CHECK_EQ(acc.disturbed, 0);
    CHECK(acc.lastError > 10.f && acc.lastError < 40.f);
}

TEST(pr_teleport_on_the_regular_tick_drops_the_probe) {
    // A reset landing on the regular tick is still a teleport: the probe is dropped, nothing is counted.
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(3);
    h.advance();
    h.snap.ball.pos.x += 1000.f;
    h.tick();
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    CHECK(h.mod.path().start.pos == h.snap.ball.pos);
    h.run(PredictionModule::kProbeTicks - 10);
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 0);
}

TEST(pr_free_ball_period_unchanged_by_the_comparison) {
    // The comparison on every tick must not add recomputes for a ball that follows its prediction.
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {300.f, 200.f, 400.f});
    h.tick();
    h.run(400);  // bounces included: the synthetic ball uses the same physics
    CHECK_EQ(h.mod.status().recomputes, 101);
    CHECK_EQ(h.mod.status().forcedRecomputes, 0);
}

// ---- Camera-view cache across view periods ------------------------------------------------------------------
TEST(pr_second_demolition_does_not_draw_the_old_view_path) {
    Harness h;
    h.game.mode = GameMode::OfflineMatch;
    h.setBall({0.f, -3000.f, 900.f}, {0.f, 1500.f, 300.f});
    h.tick();
    // First demolition: the view takes over after a few frames, then ticks come back.
    int used = 0;
    DrawList d = drawUntilView(h, 40, used);
    CHECK(h.mod.status().fromView);
    CHECK(!d.lines3D.empty());
    for (int f = 0; f < 4; ++f) {  // a few view frames: the cache ages but stays below the 6 frame period
        h.advance();
        h.draw();
    }
    CHECK(h.mod.status().fromView);
    h.run(1);
    h.draw();
    CHECK(!h.mod.status().fromView);
    // Two seconds of normal play: the ball is far from where the old view path was computed.
    for (int i = 0; i < 240; ++i) {
        h.run(1);
        h.draw();
    }
    const Vec3 oldStart = h.mod.viewPath().start.pos;
    CHECK(distance(oldStart, h.snap.ball.pos) > 1000.f);
    // Second demolition: the very first view frame predicts from the ball seen now.
    const long long before = h.mod.status().viewRecomputes;
    d = drawUntilView(h, 40, used);
    const PredictionStatus st = h.mod.status();
    CHECK(st.fromView);
    CHECK_EQ(st.viewRecomputes, before + 1);
    CHECK(h.mod.viewPath().valid);
    CHECK(distance(h.mod.viewPath().start.pos, h.snap.ball.pos) < 1.f);
    CHECK(!d.lines3D.empty());
    if (!d.lines3D.empty()) CHECK(distance(d.lines3D[0].a, h.snap.ball.pos) < 2.f * kR);
    CHECK(std::fabs(st.ballHeight - h.snap.ball.pos.z) < 30.f);
}

TEST(pr_replay_hidden_then_shown_predicts_from_the_current_ball) {
    Harness h;
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    h.snap.car.valid = false;
    h.setBall({0.f, -3000.f, 900.f}, {0.f, 1500.f, 300.f});
    for (int f = 0; f < 3; ++f) {
        h.advance();
        h.draw();
    }
    CHECK_EQ(h.mod.status().viewRecomputes, 1);
    h.command("hide");
    for (int f = 0; f < 120; ++f) {  // the replay plays on while hidden
        h.advance();
        CHECK(h.draw().lines3D.empty());
    }
    h.command("show");
    const DrawList d = h.draw();
    CHECK_EQ(h.mod.status().viewRecomputes, 2);
    CHECK(distance(h.mod.viewPath().start.pos, h.snap.ball.pos) < 1.f);
    CHECK(!d.lines3D.empty());
    if (!d.lines3D.empty()) CHECK(distance(d.lines3D[0].a, h.snap.ball.pos) < 2.f * kR);
    // Inside one view period the 6 frame throttling still holds: frames 6 and 12 after the fresh one.
    for (int f = 0; f < 11; ++f) {
        h.advance();
        h.draw();
    }
    CHECK_EQ(h.mod.status().viewRecomputes, 3);
    h.advance();
    h.draw();
    CHECK_EQ(h.mod.status().viewRecomputes, 4);
}

// ---- First-touch zone over the path start -------------------------------------------------------------------
TEST(pr_ball_in_the_band_draws_the_zone_over_the_path) {
    // A ball at 300 uu going down is already in the default 120..300 band: the zone (amber, thicker) covers the
    // part of the path inside the band, the goal colour the rest. Documents what the HUD shows in that case.
    Harness h;
    h.snap.car.team = 1;
    h.setBall({0.f, 3000.f, 300.f}, {0.f, 2000.f, 0.f});
    h.tick();
    DrawList d = h.draw();
    const PredictionConfig& c = h.mod.config();
    int zone = 0, goal = 0;
    for (const auto& l : d.lines3D) {
        if (std::fabs(l.width - (c.lineWidth + 1.5f)) < 1e-5f && sameRgb(l.color, c.zoneColor)) {
            ++zone;
            CHECK(l.b.z >= c.zoneMin - 1.f && l.b.z <= c.zoneMax + 1.f);
        } else if (std::fabs(l.width - c.lineWidth) < 1e-5f && sameRgb(l.color, c.ownGoalColor)) {
            ++goal;
        }
    }
    CHECK(zone > 0);
    CHECK(goal > 0);
    CHECK(anyText(d, "Contre ton camp dans"));
    CHECK(anyText(d, "Zone de touche"));
    // Same ball seen from the camera a few frames later (local car demolished): the ball is inside the band, so the
    // drawn path starts with the zone. This is why the goal colour test hides the zone.
    h.game.mode = GameMode::OfflineMatch;
    for (int f = 0; f < 15; ++f) {
        h.advance();
        d = h.draw();
    }
    CHECK(h.mod.status().fromView);
    CHECK(!d.lines3D.empty());
    if (d.lines3D.empty()) return;
    CHECK(sameRgb(d.lines3D[0].color, c.zoneColor));
    CHECK_NEAR(d.lines3D[0].width, c.lineWidth + 1.5f, 1e-5f);
}
