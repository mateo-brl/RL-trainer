// PredictionModule regression tests pinned by the review: hanging training-pack ball, button style of the jump
// hint, outside pushes kept out of the accuracy probe, HUD and tab agreeing on the goal, goal wording, and the
// first-touch zone colour.
#include <cmath>
#include <string>

#include "tests/TestFramework.h"
#include "tests/prediction/PredictionHarness.h"

using namespace rlt;
using namespace rlt_test_pr;

namespace {
const float kR = pr::phys::kBallArenaRadius;

// One tick where the frame advances but the ball stays where it is (the game holds it).
void holdTick(Harness& h) {
    ++h.snap.frame;
    h.snap.time = static_cast<float>(h.snap.frame) / kTickRate;
    h.tick();
}

int countText(const DrawList& d) { return static_cast<int>(d.texts3D.size() + d.texts2D.size() + d.panels.size()); }

float rgbDistance(const Color& a, const Color& b) {
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}
} // namespace

// ---- Hanging ball (training pack before the player's first move) --------------------------------------------
TEST(pr_frozen_training_ball_shows_nothing_until_it_moves) {
    Harness h;
    h.game.mode = GameMode::CustomTraining;
    h.set("rlt_pr_panel", "1");
    h.setBall({0.f, 2000.f, 600.f});
    holdTick(h);  // first sight: predicted like any ball
    CHECK_EQ(h.mod.status().recomputes, 1);
    CHECK(!h.mod.status().frozen);
    holdTick(h);  // one still tick could be an apex: not frozen yet
    CHECK(!h.mod.status().frozen);
    CHECK(h.mod.path().valid);
    holdTick(h);  // two still ticks in a row: held by the game
    CHECK(h.mod.status().frozen);
    CHECK(!h.mod.path().valid);
    for (int i = 0; i < 240; ++i) holdTick(h);
    PredictionStatus st = h.mod.status();
    CHECK(st.frozen);
    CHECK(!st.active);
    CHECK(!st.hasLanding);
    CHECK(!st.hasIntercept);
    CHECK_EQ(st.recomputes, 1);  // no prediction while it hangs
    CHECK_EQ(st.forcedRecomputes, 0);
    const DrawList d = h.draw();
    CHECK(d.lines3D.empty());
    CHECK_EQ(countText(d), 0);
    CHECK(!anyText(d, "Atterrit"));
    CHECK(!anyText(d, "Interception"));
    // Nothing measured, nothing counted as disturbed.
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 0);

    // The player moves: the shot starts and the prediction comes back on the very next tick.
    h.snap.ball.vel = {0.f, 900.f, 400.f};
    h.run(1);
    st = h.mod.status();
    CHECK(!st.frozen);
    CHECK(st.active);
    CHECK_EQ(st.recomputes, 2);
    CHECK(anyText(h.draw(), "Atterrit dans"));
    // The shot is measured like any free ball.
    h.run(PredictionModule::kProbeTicks + 4);
    CHECK_EQ(h.mod.accuracy().count, 1);
    CHECK_EQ(h.mod.accuracy().disturbed, 0);
}

TEST(pr_frozen_ball_released_without_velocity_falls_normally) {
    Harness h;
    h.setBall({500.f, 0.f, 800.f});
    for (int i = 0; i < 10; ++i) holdTick(h);
    CHECK(h.mod.status().frozen);
    // Released with no velocity: gravity moves it at once.
    h.run(1);
    CHECK(!h.mod.status().frozen);
    CHECK(h.mod.status().hasLanding);
    CHECK_NEAR(h.mod.status().landingIn, std::sqrt(2.f * (800.f - kR) / 650.f), 0.05f);
}

TEST(pr_frozen_detection_ignores_apex_resting_ball_and_repeated_frames) {
    // A ball thrown straight up passes its apex with |v| ~ 0: never frozen, always predicted.
    Harness up;
    up.setBall({0.f, 0.f, 300.f}, {0.f, 0.f, 800.f});
    up.tick();
    for (int i = 0; i < 300; ++i) {
        up.run(1);
        CHECK(!up.mod.status().frozen);
        CHECK(up.mod.path().valid);
    }
    // A ball resting on the floor is still a valid (rolling) prediction, not a frozen one.
    Harness rest;
    rest.setBall({0.f, 0.f, kR});
    for (int i = 0; i < 20; ++i) holdTick(rest);
    CHECK(!rest.mod.status().frozen);
    CHECK(rest.mod.path().valid);
    // The same frame delivered again does not count as a still tick.
    Harness same;
    same.setBall({0.f, 0.f, 700.f});
    for (int i = 0; i < 10; ++i) same.tick();
    CHECK(!same.mod.status().frozen);
    // A reset to another hanging spot (teleport) is frozen again after two still ticks.
    Harness tp;
    tp.setBall({0.f, 0.f, 700.f});
    for (int i = 0; i < 5; ++i) holdTick(tp);
    CHECK(tp.mod.status().frozen);
    tp.snap.ball.pos = {1000.f, 1000.f, 500.f};
    holdTick(tp);
    CHECK(!tp.mod.status().frozen);
    CHECK(tp.mod.path().valid);
    holdTick(tp);
    holdTick(tp);
    CHECK(tp.mod.status().frozen);
    // Map change forgets it.
    tp.event(GameEventKind::ContextChanged);
    CHECK(!tp.mod.status().frozen);
}

// ---- Jump hint with the player's button style -------------------------------------------------------------
TEST(pr_jump_hint_follows_the_button_style) {
    PlayerProfile pad;
    pad.valid = true;
    pad.usingGamepad = true;
    pad.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}};
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Jump, pad, KeyStyle::Auto) == "Saut (A)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Jump, pad, KeyStyle::PlayStation) == "Saut (Croix)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::DoubleJump, pad, KeyStyle::PlayStation) == "Double saut (Croix)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Aerial, pad, KeyStyle::PlayStation) == "Aérien (Croix + Rond)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Aerial, pad, KeyStyle::Xbox) == "Aérien (A + B)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::None, pad, KeyStyle::PlayStation).empty());
    // Unknown bindings: plain words.
    PlayerProfile none;
    CHECK(PredictionModule::jumpHint(pr::JumpKind::DoubleJump, none, KeyStyle::PlayStation) == "Double saut");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Aerial, none, KeyStyle::Auto) == "Aérien (saut + boost)");
    CHECK(PredictionModule::jumpHint(pr::JumpKind::Jump, none, KeyStyle::Auto).rfind("Saut", 0) == 0);
}

// ---- Accuracy probe: outside pushes are not model error -------------------------------------------------
TEST(pr_probe_outside_push_in_the_air_is_a_disturbance) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(60);
    h.advance();
    h.snap.ball.vel.x += 150.f;  // a bot taps the ball: 3x the recompute threshold, far from the local car
    h.tick();
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    h.run(PredictionModule::kProbeTicks);
    PredictionAccuracy acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 0);
    CHECK_EQ(acc.disturbed, 1);
    // After a short calm period a fresh probe measures the model again (zero error on this synthetic ball).
    h.run(PredictionModule::kProbeCooldownTicks + 8);
    acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 1);
    CHECK(acc.lastError < 1.f);
    CHECK_EQ(acc.disturbed, 1);
}

TEST(pr_probe_continuous_pushes_count_once_per_calm_period) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(10);
    // A bot pushes the ball for 0.5 s (60 uu/s every 2 ticks).
    for (int i = 0; i < 60; ++i) {
        h.advance();
        if (i % 2 == 0) h.snap.ball.vel.y += 60.f;
        h.tick();
    }
    CHECK(h.mod.status().forcedRecomputes >= 20);
    CHECK_EQ(h.mod.accuracy().disturbed, 1);
    CHECK_EQ(h.mod.accuracy().count, 0);
}

TEST(pr_probe_outside_push_on_a_rolling_ball_is_a_disturbance) {
    Harness h;
    h.setBall({0.f, -2000.f, kR}, {0.f, 800.f, 0.f});
    h.tick();
    h.run(40);
    CHECK(!pr::ballAirborne(h.snap.ball.pos, h.snap.ball.vel, kR));
    h.advance();
    h.snap.ball.vel.x += 200.f;
    h.tick();
    h.run(PredictionModule::kProbeTicks);
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 1);
}

TEST(pr_probe_bounce_mismatch_is_still_model_error) {
    Harness h;
    h.setBall({0.f, -2000.f, 300.f}, {300.f, 400.f, 0.f});
    h.tick();
    bool bounced = false;
    for (int i = 0; i < PredictionModule::kProbeTicks; ++i) {
        const float vzBefore = h.snap.ball.vel.z;
        h.advance();
        if (!bounced && vzBefore < 0.f && h.snap.ball.vel.z > 0.f) {
            h.snap.ball.vel.z *= 0.8f;  // the real floor gives back less than the model said
            bounced = true;
        }
        h.tick();
    }
    CHECK(bounced);
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    const PredictionAccuracy acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 1);
    CHECK_EQ(acc.disturbed, 0);
    CHECK(acc.lastError > 2.f && acc.lastError < 100.f);
}

TEST(pr_probe_local_car_push_is_neither_scored_nor_disturbed) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(30);
    h.advance();
    h.snap.car.pos = h.snap.ball.pos - Vec3{0.f, 150.f, 60.f};  // the local car is on the ball
    h.snap.ball.vel.y += 400.f;
    h.tick();
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    // The engine reports the touch two ticks later.
    TouchEvent t;
    t.pre = h.history.back(1);
    t.frame = t.pre.frame;
    h.advance();
    h.tick();
    Actions a;
    h.mod.onTouch(t, h.tc(), a);
    h.snap.car.pos = {0.f, -4000.f, 17.f};
    h.run(PredictionModule::kProbeTicks);
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 0);
    // No cool-down after the player's own touch: the next probe completes.
    h.run(8);
    CHECK_EQ(h.mod.accuracy().count, 1);
}

TEST(pr_path_expects_impulse_only_near_bounces_and_walls) {
    pr::PredictedPath p;
    pr::SimBall b;
    // Free flight: nothing expected.
    b.pos = {0.f, 0.f, 1000.f};
    b.vel = {300.f, 0.f, 500.f};
    pr::simulatePath(b, 1.f, pr::SimParams{}, pr::ArenaMode::Soccar, p);
    CHECK(!PredictionModule::pathExpectsImpulse(p, 1));
    CHECK(!PredictionModule::pathExpectsImpulse(p, 60));
    // Rolling on the flat floor: contacts, but no impulse expected.
    b.pos = {0.f, 0.f, kR};
    b.vel = {500.f, 0.f, 0.f};
    pr::simulatePath(b, 1.f, pr::SimParams{}, pr::ArenaMode::Soccar, p);
    CHECK(p.samples[10].flags & pr::kStepContact);
    CHECK(!PredictionModule::pathExpectsImpulse(p, 10));
    // A floor bounce: expected around its tick, not long before.
    b.pos = {0.f, 0.f, 400.f};
    b.vel = {0.f, 0.f, 0.f};
    pr::simulatePath(b, 2.f, pr::SimParams{}, pr::ArenaMode::Soccar, p);
    int bounce = -1;
    for (int i = 0; i < p.size() && bounce < 0; ++i)
        if (p.samples[static_cast<size_t>(i)].flags & pr::kStepBounce) bounce = i;
    CHECK(bounce > 10);
    CHECK(PredictionModule::pathExpectsImpulse(p, bounce + 1));
    CHECK(PredictionModule::pathExpectsImpulse(p, bounce - 1));
    CHECK(PredictionModule::pathExpectsImpulse(p, bounce + 3));
    CHECK(!PredictionModule::pathExpectsImpulse(p, bounce - 10));
    // A side wall hit.
    b.pos = {3800.f, 0.f, 800.f};
    b.vel = {1500.f, 0.f, 0.f};
    pr::simulatePath(b, 1.f, pr::SimParams{}, pr::ArenaMode::Soccar, p);
    int wall = -1;
    for (int i = 0; i < p.size() && wall < 0; ++i)
        if (p.samples[static_cast<size_t>(i)].flags & pr::kStepWall) wall = i;
    CHECK(wall >= 0);
    CHECK(PredictionModule::pathExpectsImpulse(p, wall + 1));
    // Out of range indices are safe.
    CHECK(!PredictionModule::pathExpectsImpulse(pr::PredictedPath{}, 5));
    CHECK(!PredictionModule::pathExpectsImpulse(p, 100000));
    CHECK(!PredictionModule::pathExpectsImpulse(p, -50));
}

// ---- HUD and tab agree on the goal ----------------------------------------------------------------------
TEST(pr_stale_view_goal_relation_matches_the_hud) {
    Harness h;
    h.game.mode = GameMode::OfflineMatch;
    h.snap.car.team = 1;
    h.setBall({0.f, 3000.f, 300.f}, {0.f, 2000.f, 0.f});
    h.tick();
    DrawList d;
    for (int f = 0; f < 15; ++f) {  // the local car is demolished: no ticks, the ball moves on
        h.advance();
        d = h.draw();
    }
    PredictionStatus st = h.mod.status();
    CHECK(st.fromView);
    CHECK(!st.replay);
    CHECK_EQ(st.goalSide, 1);
    CHECK(anyText(d, "Contre ton camp dans"));
    CHECK_EQ(st.goalForPlayer, -1);
    CHECK(sameRgb(d.lines3D[0].color, h.mod.config().ownGoalColor));
    // In a replay the team is unknown for both.
    Harness r;
    r.game.mode = GameMode::Replay;
    r.hasSnapshot = false;
    r.snap.car.team = 1;
    r.setBall({0.f, 3000.f, 300.f}, {0.f, 2000.f, 0.f});
    for (int f = 0; f < 3; ++f) {
        r.advance();
        d = r.draw();
    }
    st = r.mod.status();
    CHECK(st.fromView);
    CHECK(st.replay);
    CHECK_EQ(st.goalForPlayer, 0);
    CHECK(anyText(d, "But orange dans"));
}

// ---- Goal wording ---------------------------------------------------------------------------------------
TEST(pr_goal_wording_is_the_same_on_the_field_and_in_the_panel) {
    CHECK(PredictionModule::goalLabel(1, 0) == "But pour toi");
    CHECK(PredictionModule::goalLabel(-1, 0) == "Contre ton camp");
    CHECK(PredictionModule::goalLabel(-1, 1) == "But pour toi");
    CHECK(PredictionModule::goalLabel(1, 1) == "Contre ton camp");
    CHECK(PredictionModule::goalLabel(1, -1) == "But orange");
    CHECK(PredictionModule::goalLabel(-1, -1) == "But bleu");
    for (int side = -1; side <= 1; side += 2) {
        Harness h;
        h.set("rlt_pr_panel", "1");
        h.setBall({0.f, side * 4000.f, kR}, {0.f, side * 2000.f, 0.f});
        h.tick();
        const DrawList d = h.draw();
        const std::string expect = side > 0 ? "But pour toi dans " : "Contre ton camp dans ";
        CHECK(anyText(d, expect));
        CHECK_EQ(d.panels.size(), 1u);
        bool panelHas = false;
        for (const auto& l : d.panels[0].lines) {
            if (l.text.find(expect) == 0) panelHas = true;
            CHECK(l.text.find("Dans ton but") == std::string::npos);
            CHECK(l.text.find("But adverse") == std::string::npos);
            // "dans" once per line.
            const size_t first = l.text.find("dans");
            if (first != std::string::npos) CHECK(l.text.find("dans", first + 1) == std::string::npos);
        }
        CHECK(panelHas);
        CHECK(!anyText(d, "BUT dans"));
    }
}

// ---- First-touch zone colour ----------------------------------------------------------------------------
TEST(pr_zone_colour_stands_out_from_the_path_colours) {
    const PredictionConfig c;
    CHECK(rgbDistance(c.zoneColor, c.pathColor) > 150.f);
    CHECK(rgbDistance(c.zoneColor, c.opponentGoalColor) > 150.f);
    CHECK(rgbDistance(c.zoneColor, c.ownGoalColor) > 100.f);
    CHECK(rgbDistance(c.zoneColor, c.interceptColor) > 150.f);
    CHECK(rgbDistance(c.zoneColor, c.landingColor) > 150.f);
    // The schema default is the same amber.
    Harness h;
    CHECK(sameRgb(h.mod.config().zoneColor, Color(255.f, 170.f, 40.f, 240.f)));
    // Kept as configured over a normal or goal-coloured path, white over a path of a close colour.
    CHECK(sameRgb(h.mod.zoneColorOver(c.pathColor), c.zoneColor));
    CHECK(sameRgb(h.mod.zoneColorOver(c.opponentGoalColor), c.zoneColor));
    CHECK(sameRgb(h.mod.zoneColorOver(c.ownGoalColor), c.zoneColor));
    CHECK(sameRgb(h.mod.zoneColorOver(colors::Orange), colors::White));
    CHECK_NEAR(h.mod.zoneColorOver(colors::Orange).a, c.zoneColor.a, 1e-6f);
    CHECK(PredictionModule::colorsClose(Color(10.f, 10.f, 10.f, 255.f), Color(20.f, 20.f, 20.f, 0.f)));
    CHECK(!PredictionModule::colorsClose(colors::Orange, colors::Blue));
}

TEST(pr_zone_drawn_white_over_a_path_of_the_same_colour) {
    // Replay, ball dropping towards the orange goal: the path is orange (team unknown), the amber zone would vanish.
    Harness h;
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    h.snap.car.valid = false;
    h.setBall({0.f, 3500.f, 600.f}, {0.f, 800.f, 0.f});
    const DrawList d = h.draw();
    const PredictionStatus st = h.mod.status();
    CHECK(st.hasZone);
    CHECK_EQ(st.goalSide, 1);
    CHECK(sameRgb(d.lines3D[0].color, colors::Orange));
    const float w = h.mod.config().lineWidth;
    int thickWhite = 0, thickAmber = 0;
    for (const auto& l : d.lines3D) {
        if (l.width <= w) continue;
        if (sameRgb(l.color, colors::White)) ++thickWhite;
        if (sameRgb(l.color, h.mod.config().zoneColor)) ++thickAmber;
    }
    CHECK(thickWhite > 0);
    CHECK_EQ(thickAmber, 0);
    // Same ball, normal path colour (goal warning off): the zone keeps its amber.
    h.set("rlt_pr_goal_warning", "0");
    h.command("recompute");
    const DrawList n = h.draw();
    int amber = 0;
    for (const auto& l : n.lines3D)
        if (l.width > w && sameRgb(l.color, h.mod.config().zoneColor)) ++amber;
    CHECK(amber > 0);
}
