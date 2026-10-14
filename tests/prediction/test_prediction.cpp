// PredictionModule: settings schema, recompute throttling, draw from cache, replays, goal colours, intercept
// display, accuracy probe, persistence, commands, robustness and activity gating.
#include <cmath>
#include <string>

#include "tests/TestFramework.h"
#include "tests/prediction/PredictionHarness.h"

using namespace rlt;
using namespace rlt_test_pr;

namespace {
const float kR = pr::phys::kBallArenaRadius;
const char* kEmDash = "\xE2\x80\x94";

bool noEmDash(const std::string& s) { return s.find(kEmDash) == std::string::npos; }
} // namespace

// ---- Settings ------------------------------------------------------------------------------------------
TEST(pr_schema_defaults_parse_and_naming) {
    SettingsSchema schema;
    PredictionModule::describeSettings(schema);
    const auto defs = schema.forModule("prediction");
    CHECK(defs.size() >= 30);
    Settings s;
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_pr_", 0) == 0, d->cvar);
        CHECK(!d->label.empty());
        CHECK(!d->tooltip.empty());
        CHECK(!d->section.empty());
        CHECK_MSG(d->apply && d->apply(s, d->defaultValue), d->cvar);
        CHECK_MSG(d->read && d->read(s) == d->defaultValue, d->cvar);
        CHECK(noEmDash(d->label) && noEmDash(d->tooltip) && noEmDash(d->section));
        for (const auto& c : d->choices) CHECK(noEmDash(c));
        CHECK_EQ(d->advanced, d->section == "Avancé");
    }
    const auto sections = schema.sections("prediction");
    CHECK(sections.size() >= 6);
    CHECK(sections.front() == "Prédiction");
    CHECK(sections.back() == "Avancé");

    // The schema defaults are the struct defaults.
    const PredictionConfig def;
    Settings applied;
    applied.prediction.horizon = 1.f;
    applied.prediction.enabled = false;
    applied.prediction.zoneMax = 1.f;
    schema.applyDefaults(applied);
    const PredictionConfig& c = applied.prediction;
    CHECK(c.enabled == def.enabled);
    CHECK_NEAR(c.horizon, def.horizon, 1e-6f);
    CHECK_NEAR(c.zoneMin, def.zoneMin, 1e-6f);
    CHECK_NEAR(c.zoneMax, def.zoneMax, 1e-6f);
    CHECK_EQ(c.recomputeTicks, def.recomputeTicks);
    CHECK_NEAR(c.recomputeSpeedDelta, def.recomputeSpeedDelta, 1e-6f);
    CHECK(c.pathColor == def.pathColor);
    CHECK(c.ownGoalColor == def.ownGoalColor);
    CHECK(c.showPanel == def.showPanel);
    CHECK_EQ(c.arenaMode, def.arenaMode);
    CHECK_NEAR(c.horizon, 3.f, 1e-6f);
    CHECK_EQ(c.recomputeTicks, 4);
    // Trajectories are opt-in: the whole module is off by default, its elements on once it is switched on.
    CHECK(!def.enabled);
    CHECK(schema.find("rlt_pr_enabled") && schema.find("rlt_pr_enabled")->defaultValue == "0");
    CHECK(def.showPath && def.showBounces && def.showLanding && def.showIntercept);
}

TEST(pr_off_by_default_draws_nothing) {
    // A new player: schema defaults, nothing drawn and nothing simulated even with a ball in the air.
    Harness h;
    h.set("rlt_pr_enabled", "0");
    Settings fresh;
    h.schema.applyDefaults(fresh);
    CHECK(!fresh.prediction.enabled);
    h.setBall({0.f, 0.f, 800.f}, {300.f, 0.f, 200.f});
    h.run(10);
    CHECK(!h.mod.path().valid);
    CHECK(h.draw().empty());
    CHECK(!h.mod.status().enabled && !h.mod.status().active);
    CHECK_EQ(h.mod.status().recomputes, 0LL);
}

TEST(pr_show_command_while_off_says_where_to_switch_it_on) {
    Harness h;
    h.set("rlt_pr_enabled", "0");
    const Actions a = h.command("show");
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    CHECK(!a.toasts.empty() && a.toasts[0].text.find("Trajectoires") != std::string::npos);
    CHECK(!a.toasts.empty() && a.toasts[0].text.find("affichee") == std::string::npos);
    h.set("rlt_pr_enabled", "1");
    const Actions b = h.command("hide");
    CHECK(!b.toasts.empty() && b.toasts[0].text.find("masquee") != std::string::npos);
    const Actions c = h.command("toggle");
    CHECK(!c.toasts.empty() && c.toasts[0].text.find("affichee") != std::string::npos);
}

TEST(pr_old_default_migrated_once) {
    // A player of the previous version: config.cfg holds the old default (module on) and the Store has no flag. The
    // first tick turns it off once; a reload never does it again; a value chosen differently is kept.
    auto cvar = [](const Actions& a, const std::string& name) {
        for (const auto& c : a.setCvars)
            if (c.first == name) return c.second;
        return std::string();
    };
    Harness h;
    h.mod.load(Store{});  // save of the previous version: no flag
    h.set("rlt_pr_enabled", "1");
    CHECK(!h.mod.defaultsMigrated());
    // No snapshot (main menu): nothing moves yet.
    h.hasSnapshot = false;
    const Actions menu = h.tick();
    CHECK(menu.setCvars.empty() && menu.toasts.empty() && !menu.requestSave);
    CHECK(!h.mod.defaultsMigrated());
    h.hasSnapshot = true;
    h.setBall({0.f, 0.f, 800.f});
    const Actions first = h.tick();
    CHECK_EQ(first.setCvars.size(), static_cast<size_t>(1));
    CHECK(cvar(first, "rlt_pr_enabled") == "0");
    CHECK(first.requestSave);
    CHECK_EQ(first.toasts.size(), static_cast<size_t>(1));
    CHECK(!first.toasts.empty() && noEmDash(first.toasts[0].text) &&
          first.toasts[0].text.find("\xE2\x80\x93") == std::string::npos);
    CHECK(!first.toasts.empty() && first.toasts[0].text.find("Trajectoires") != std::string::npos);
    // The value the engine sets back through the cvar is a valid setting.
    for (const auto& c : first.setCvars) CHECK_MSG(h.set(c.first, c.second), c.first);
    CHECK(!h.mod.config().enabled);
    CHECK(h.mod.defaultsMigrated());
    h.run(1);
    CHECK(h.draw().empty());
    const Actions second = h.tick();
    CHECK(second.setCvars.empty() && second.toasts.empty());
    // The flag is saved: a reload where the player turned it back on does not migrate again.
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("prediction.defaults_v2", 0), 1);
    Harness again;
    again.mod.load(st);
    again.set("rlt_pr_enabled", "1");
    CHECK(again.mod.defaultsMigrated());
    again.setBall({0.f, 0.f, 800.f});
    const Actions reloaded = again.tick();
    CHECK(reloaded.setCvars.empty() && reloaded.toasts.empty());
    CHECK(again.mod.config().enabled);
    CHECK(!again.draw().lines3D.empty());
    // A value chosen differently (already off) is kept; only the flag is set.
    Harness chosen;
    chosen.mod.load(Store{});
    chosen.set("rlt_pr_enabled", "0");
    const Actions kept = chosen.tick();
    CHECK(kept.setCvars.empty() && kept.toasts.empty() && kept.requestSave);
    CHECK(chosen.mod.defaultsMigrated());
    CHECK(!chosen.mod.config().enabled);
}

TEST(pr_schema_clamps_and_rejects) {
    Harness h;
    CHECK(h.set("rlt_pr_horizon", "50"));
    CHECK_NEAR(h.mod.config().horizon, 6.f, 1e-6f);
    CHECK(h.set("rlt_pr_horizon", "0.2"));
    CHECK_NEAR(h.mod.config().horizon, 1.f, 1e-6f);
    CHECK(!h.set("rlt_pr_horizon", "beaucoup"));
    CHECK_NEAR(h.mod.config().horizon, 1.f, 1e-6f);
    CHECK(h.set("rlt_pr_unit", "9"));
    CHECK_EQ(h.mod.config().unit, 3);
    CHECK(h.set("rlt_pr_zone_min", "10"));
    CHECK_NEAR(h.mod.config().zoneMin, 93.f, 1e-6f);
    CHECK(h.set("rlt_pr_path_color", "#FF000080"));
    CHECK(h.mod.config().pathColor == Color(255.f, 0.f, 0.f, 128.f));
    CHECK(!h.set("rlt_pr_path_color", "rouge"));
}

// ---- Recompute throttling ------------------------------------------------------------------------------
TEST(pr_recompute_every_four_ticks_for_a_free_ball) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {300.f, 200.f, 400.f});
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 1);
    CHECK(h.mod.path().valid);
    CHECK_EQ(h.mod.path().frame, h.snap.frame);
    h.run(3);
    CHECK_EQ(h.mod.status().recomputes, 1);
    h.run(1);
    CHECK_EQ(h.mod.status().recomputes, 2);
    h.run(36);
    CHECK_EQ(h.mod.status().recomputes, 11);
    CHECK_EQ(h.mod.status().forcedRecomputes, 0);
    // Slower setting.
    CHECK(h.set("rlt_pr_recompute_ticks", "8"));
    h.run(1);  // settings change forces one recompute
    const long long base = h.mod.status().recomputes;
    h.run(16);
    CHECK_EQ(h.mod.status().recomputes, base + 2);
}

TEST(pr_recompute_at_once_after_an_unexpected_velocity_change) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {300.f, 0.f, 0.f});
    h.tick();
    h.run(1);
    CHECK_EQ(h.mod.status().recomputes, 1);
    // A touch adds 60 uu/s: more than the 50 uu/s threshold.
    h.advance();
    h.snap.ball.vel.y += 60.f;
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 2);
    CHECK_EQ(h.mod.status().forcedRecomputes, 1);
    CHECK(h.mod.path().start.vel == h.snap.ball.vel);
    // 30 uu/s is below the threshold: wait for the regular period.
    h.advance();
    h.snap.ball.vel.y += 30.f;
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 2);
    // Threshold setting respected.
    CHECK(h.set("rlt_pr_recompute_delta", "20"));
    h.run(1);
    const long long base = h.mod.status().recomputes;
    h.advance();
    h.snap.ball.vel.x -= 25.f;
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, base + 1);
}

TEST(pr_recompute_after_teleport_touch_and_reset) {
    Harness h;
    h.setBall({0.f, 0.f, 600.f});
    h.tick();
    h.advance();
    h.snap.ball.pos.x += 500.f;  // reset shot / teleport
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 2);
    CHECK(h.mod.path().start.pos == h.snap.ball.pos);

    TouchEvent t;
    t.pre = h.snap;
    t.frame = h.snap.frame;
    Actions a;
    h.mod.onTouch(t, h.tc(), a);
    h.advance();
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 3);

    h.event(GameEventKind::ResetShot);
    h.advance();
    h.tick();
    CHECK_EQ(h.mod.status().recomputes, 4);
}

TEST(pr_paused_snapshot_keeps_the_cache) {
    Harness h;
    h.setBall({0.f, 0.f, 600.f}, {0.f, 500.f, 0.f});
    h.tick();
    const int64_t frame = h.mod.path().frame;
    h.snap.paused = true;
    for (int i = 0; i < 20; ++i) {
        ++h.snap.frame;
        h.tick();
    }
    CHECK_EQ(h.mod.status().recomputes, 1);
    CHECK_EQ(h.mod.path().frame, frame);
    CHECK(h.mod.path().valid);
}

// ---- Draw ----------------------------------------------------------------------------------------------
TEST(pr_draw_reads_the_cache_and_never_simulates) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {500.f, 300.f, 200.f});
    h.tick();
    const long long before = h.mod.status().recomputes;
    DrawList d;
    for (int i = 0; i < 50; ++i) d = h.draw();
    CHECK_EQ(h.mod.status().recomputes, before);
    CHECK_EQ(h.mod.status().viewRecomputes, 0);
    CHECK(!h.mod.status().fromView);
    CHECK(!d.lines3D.empty());
    // The path starts at the ball and its next vertex is a cached sample.
    CHECK(d.lines3D[0].a == h.snap.ball.pos);
    CHECK(d.lines3D[0].b == h.mod.path().samples[3].pos);
    CHECK(d.lines3D[0].occluder.enabled);
    CHECK_NEAR(d.lines3D[0].occluder.radius, h.view().ballRadius, 1e-6f);
}

TEST(pr_draw_skips_the_part_already_travelled) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {500.f, 300.f, 200.f});
    h.tick();
    h.run(2);  // no recompute yet: the cache is 2 ticks old
    CHECK_EQ(h.mod.status().recomputes, 1);
    const DrawList d = h.draw();
    CHECK(!d.lines3D.empty());
    CHECK(d.lines3D[0].a == h.snap.ball.pos);
    CHECK(d.lines3D[0].b == h.mod.path().samples[5].pos);
    // samples[1] is where the ball is now (2 ticks after the start): samples[0] is already behind it.
    for (const auto& l : d.lines3D) CHECK(!(l.a == h.mod.path().samples[0].pos) && !(l.b == h.mod.path().samples[0].pos));
}

TEST(pr_draw_line_budget) {
    Harness h;
    h.setBall({3500.f, 4300.f, 1500.f}, {2500.f, 2000.f, -1500.f}, {3.f, -2.f, 1.f});
    h.tick();
    const DrawList d = h.draw();
    CHECK(!d.lines3D.empty());
    CHECK(d.lines3D.size() < 700);
    // Worst case: 6 s horizon, a vertex every tick, everything on.
    h.set("rlt_pr_horizon", "6");
    h.set("rlt_pr_draw_step", "1");
    h.set("rlt_pr_panel", "1");
    h.advance();
    h.tick();
    const DrawList w = h.draw();
    CHECK(w.lines3D.size() < 1500);
    CHECK(w.texts3D.size() < 40);
    for (const auto& t : w.texts3D) CHECK(noEmDash(t.text));
    // A ball bouncing on the floor for 6 s.
    Harness b;
    b.set("rlt_pr_horizon", "6");
    b.set("rlt_pr_draw_step", "1");
    b.setBall({0.f, 0.f, 1800.f}, {300.f, 300.f, 0.f});
    b.tick();
    CHECK(b.draw().lines3D.size() < 1500);
}

TEST(pr_landing_marker_and_text) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f});
    h.tick();
    const PredictionStatus st = h.mod.status();
    CHECK(st.active);
    CHECK(st.hasLanding);
    CHECK_NEAR(st.landingIn, std::sqrt(2.f * (800.f - kR) / 650.f), 0.05f);
    CHECK_NEAR(st.landingPos.z, kR, 1.f);
    const DrawList d = h.draw();
    CHECK(anyText(d, "Atterrit dans"));
    // A 91.25 uu circle on the floor exists (32 segments at z ~ 2).
    int floorLines = 0;
    for (const auto& l : d.lines3D)
        if (std::fabs(l.a.z - 2.f) < 0.5f && std::fabs(l.b.z - 2.f) < 0.5f) ++floorLines;
    CHECK(floorLines >= 32);
    // Times count down as the ball falls.
    h.run(12);
    CHECK_NEAR(h.mod.status().landingIn, st.landingIn - 0.1f, 1e-3f);
    // Hidden with the setting.
    h.set("rlt_pr_show_landing", "0");
    CHECK(!anyText(h.draw(), "Atterrit"));
}

TEST(pr_first_touch_zone_drawn_and_reported) {
    Harness h;
    h.setBall({0.f, 0.f, 900.f}, {0.f, 0.f, 200.f});
    h.tick();
    const PredictionStatus st = h.mod.status();
    CHECK(st.hasApex);
    CHECK(st.hasZone);
    CHECK(st.zoneIn > st.apexIn);
    CHECK(st.zoneHeight <= 300.f && st.zoneHeight >= 120.f);
    const DrawList d = h.draw();
    CHECK(anyText(d, "Zone de touche"));
    bool thick = false;
    for (const auto& l : d.lines3D)
        if (sameRgb(l.color, h.mod.config().zoneColor) && l.width > h.mod.config().lineWidth) thick = true;
    CHECK(thick);
    h.set("rlt_pr_show_zone", "0");
    CHECK(!anyText(h.draw(), "Zone de touche"));
}

TEST(pr_only_airborne_hides_a_rolling_ball) {
    Harness h;
    h.set("rlt_pr_only_airborne", "1");
    h.setBall({0.f, 0.f, kR}, {600.f, 0.f, 0.f});
    h.tick();
    h.run(40);
    CHECK(h.mod.path().valid);
    CHECK(h.draw().lines3D.empty());
    h.setBall({0.f, 0.f, 600.f}, {600.f, 0.f, 300.f});
    h.advance();
    h.tick();
    CHECK(!h.draw().lines3D.empty());
    h.set("rlt_pr_only_airborne", "0");
    h.setBall({0.f, 0.f, kR}, {600.f, 0.f, 0.f});
    h.advance();
    h.tick();
    CHECK(!h.draw().lines3D.empty());
}

TEST(pr_panel_shows_the_numbers) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f});
    h.tick();
    CHECK(h.draw().panels.empty());  // off by default
    h.set("rlt_pr_panel", "1");
    const DrawList d = h.draw();
    CHECK_EQ(d.panels.size(), 1u);
    CHECK(anyText(d, "Prediction"));  // accents folded on the canvas by default
    CHECK(anyText(d, "Atterrit dans"));
    CHECK_NEAR(d.panels[0].pos.x, 0.01f * 1920.f, 1e-3f);
    CHECK_NEAR(d.panels[0].pos.y, 0.62f * 1080.f, 1e-3f);
}

// Compact HUD: at most three lines, the ball speed and "ball on the floor" are details, no empty panel.
TEST(pr_panel_compact_keeps_three_lines) {
    auto drawCompact = [](Harness& h) {
        DrawContext dc{h.settings, h.game, h.profile, h.snap, h.hasSnapshot, false, 1.f, true};
        DrawList d;
        h.mod.draw(h.view(), dc, d);
        return d;
    };
    auto shown = [](const DrawPanel& p) {
        size_t n = 0;
        for (const auto& l : p.lines) n += l.detail ? 0u : 1u;
        return n;
    };
    // Ball resting on the floor and no car (no interception): only a detail line, so no panel at all in the
    // compact HUD.
    Harness still;
    still.set("rlt_pr_only_airborne", "0");
    still.set("rlt_pr_panel", "1");
    still.snap.car.valid = false;
    still.setBall({0.f, 0.f, kR});
    still.tick();
    CHECK(anyText(still.draw(), "Balle au sol"));
    CHECK(drawCompact(still).panels.empty());
    CHECK(!drawCompact(still).lines3D.empty());  // the path itself stays
    // Ball rolling into a goal: the goal line stays, the ball speed is a detail.
    for (int side = -1; side <= 1; side += 2) {
        Harness h;
        h.set("rlt_pr_panel", "1");
        h.setBall({0.f, side * 4000.f, kR}, {0.f, side * 2000.f, 0.f});
        h.tick();
        const DrawList d = drawCompact(h);
        CHECK_EQ(d.panels.size(), 1u);
        if (d.panels.empty()) continue;
        const DrawPanel& p = d.panels[0];
        CHECK(shown(p) >= 1u && shown(p) <= 3u);
        bool goal = false;
        for (const auto& l : p.lines) {
            if (l.text.find(side > 0 ? "But pour toi" : "Contre ton camp") == 0) goal = !l.detail;
            if (l.text.find("Arrive a") == 0 || l.text.find("Balle au sol") == 0) CHECK(l.detail);
        }
        CHECK(goal);
    }
}

// ---- Goals ---------------------------------------------------------------------------------------------
TEST(pr_goal_colour_depends_on_the_team) {
    for (int team = 0; team <= 1; ++team) {
        Harness h;
        h.snap.car.team = team;
        h.snap.car.pos = {2000.f, 0.f, 17.f};
        h.setBall({0.f, 4000.f, kR}, {0.f, 2000.f, 0.f});
        h.tick();
        const PredictionStatus st = h.mod.status();
        CHECK_EQ(st.goalSide, 1);
        CHECK_EQ(st.goalForPlayer, team == 0 ? 1 : -1);
        CHECK(st.goalIn > 0.4f && st.goalIn < 0.8f);
        const DrawList d = h.draw();
        CHECK(!d.lines3D.empty());
        const Color expect = team == 0 ? h.mod.config().opponentGoalColor : h.mod.config().ownGoalColor;
        CHECK(sameRgb(d.lines3D[0].color, expect));
        CHECK(anyText(d, team == 0 ? "But pour toi dans" : "Contre ton camp dans"));
    }
    // Blue goal (-Y) for the blue team is an own goal.
    Harness h;
    h.setBall({0.f, -4000.f, kR}, {0.f, -2000.f, 0.f});
    h.tick();
    CHECK_EQ(h.mod.status().goalForPlayer, -1);
    CHECK(sameRgb(h.draw().lines3D[0].color, h.mod.config().ownGoalColor));
    // Warning off: normal colour.
    h.set("rlt_pr_goal_warning", "0");
    CHECK(sameRgb(h.draw().lines3D[0].color, h.mod.config().pathColor));
}

TEST(pr_goal_relation_and_neutral_colours) {
    CHECK_EQ(PredictionModule::goalRelation(1, 0), 1);
    CHECK_EQ(PredictionModule::goalRelation(-1, 0), -1);
    CHECK_EQ(PredictionModule::goalRelation(1, 1), -1);
    CHECK_EQ(PredictionModule::goalRelation(-1, 1), 1);
    CHECK_EQ(PredictionModule::goalRelation(1, -1), 0);
    CHECK_EQ(PredictionModule::goalRelation(0, 0), 0);
    PredictionModule m;
    CHECK(sameRgb(m.goalColor(1, -1), colors::Orange));
    CHECK(sameRgb(m.goalColor(-1, -1), colors::Blue));
}

// ---- Replays and missing ticks -------------------------------------------------------------------------
TEST(pr_replay_predicts_from_the_view_at_most_every_six_frames) {
    Harness h;
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    h.snap.car.valid = false;
    h.setBall({0.f, 4000.f, 300.f}, {0.f, 1500.f, 300.f});
    DrawList d;
    for (int f = 0; f < 30; ++f) {
        h.advance();
        d = h.draw();
    }
    const PredictionStatus st = h.mod.status();
    CHECK(st.fromView);
    CHECK(st.active);
    CHECK_EQ(st.viewRecomputes, 5);
    CHECK_EQ(st.recomputes, 0);
    CHECK(!st.hasIntercept);
    CHECK(!d.lines3D.empty());
    CHECK(!anyText(d, "Interception"));
    // Team unknown in a replay: goal coloured with the goal's own team colour.
    CHECK_EQ(st.goalForPlayer, 0);
    CHECK(sameRgb(d.lines3D[0].color, colors::Orange));
    CHECK(anyText(d, "But orange"));
    // The cached path is followed between recomputes: the drawn path starts at the current ball.
    CHECK(distance(d.lines3D[0].a, h.snap.ball.pos) < 1.f);
}

TEST(pr_replay_without_ball_draws_nothing) {
    Harness h;
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    h.snap.ball.valid = false;
    for (int f = 0; f < 10; ++f) CHECK(h.draw().lines3D.empty());
    CHECK(!h.mod.status().active);
}

TEST(pr_falls_back_to_the_view_when_ticks_stop) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {1500.f, 0.f, 0.f});
    h.tick();
    // No tick but the ball does not move (pause not reported yet): keep the tick cache.
    for (int f = 0; f < 100; ++f) h.draw();
    CHECK(!h.mod.status().fromView);
    CHECK_EQ(h.mod.status().viewRecomputes, 0);
    // The ball moves on without ticks (car demolished): predict from the view.
    for (int f = 0; f < 10; ++f) {
        h.advance();
        h.draw();
    }
    CHECK(h.mod.status().fromView);
    CHECK(!h.mod.status().hasIntercept);
    CHECK(h.mod.status().viewRecomputes >= 1);
    h.tick();
    h.draw();
    CHECK(!h.mod.status().fromView);
}

TEST(pr_game_pause_keeps_the_tick_cache) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {500.f, 0.f, 0.f});
    h.tick();
    h.event(GameEventKind::Paused);
    DrawList d;
    for (int f = 0; f < 200; ++f) d = h.draw();
    CHECK(!h.mod.status().fromView);
    CHECK_EQ(h.mod.status().viewRecomputes, 0);
    CHECK(!d.lines3D.empty());
    h.event(GameEventKind::Unpaused);
    h.run(1);
    CHECK(!h.mod.status().fromView);
}

// ---- Intercept -----------------------------------------------------------------------------------------
TEST(pr_no_intercept_without_a_car) {
    Harness h;
    h.snap.car.valid = false;
    h.setBall({0.f, 0.f, 600.f}, {0.f, -300.f, 200.f});
    h.tick();
    CHECK(!h.mod.intercept().found);
    CHECK(!h.mod.status().hasIntercept);
    CHECK(!anyText(h.draw(), "Interception"));
    // Same ball with the car back.
    h.snap.car.valid = true;
    h.command("recompute");
    h.advance();
    h.tick();
    CHECK(h.mod.intercept().found);
    const PredictionStatus st = h.mod.status();
    CHECK(st.hasIntercept);
    CHECK(st.interceptIn > 0.1f);
    CHECK_NEAR(st.interceptBallSpeed, length2D(h.mod.intercept().ballVel), 1e-3f);
    const DrawList d = h.draw();
    CHECK(anyText(d, "Interception dans"));
    CHECK(anyText(d, "Arrive a"));
    CHECK(anyText(d, "km/h"));
    h.set("rlt_pr_show_intercept", "0");
    CHECK(!anyText(h.draw(), "Interception"));
}

TEST(pr_intercept_speed_units_follow_profile_and_setting) {
    Harness h;
    h.profile.metric = false;
    h.setBall({0.f, 0.f, 600.f}, {0.f, -300.f, 200.f});
    h.tick();
    CHECK(anyText(h.draw(), " mph"));
    CHECK(h.mod.status().unit == SpeedUnit::Mph);
    h.set("rlt_pr_unit", "1");
    CHECK(anyText(h.draw(), " km/h"));
    h.set("rlt_pr_unit", "3");
    CHECK(anyText(h.draw(), " uu/s"));
    // Exact text: the ball's horizontal speed at the intercept.
    h.set("rlt_pr_unit", "1");
    const std::string expect = "Arrive a " + PredictionModule::formatBallSpeed(h.mod.intercept().ballSpeed2D, SpeedUnit::Kph);
    CHECK(anyText(h.draw(), expect));
    CHECK(PredictionModule::formatBallSpeed(1000.f, SpeedUnit::Kph) == "36 km/h");
    CHECK(PredictionModule::formatBallSpeed(447.04f, SpeedUnit::Mph) == "10 mph");
    CHECK(PredictionModule::formatBallSpeed(1234.4f, SpeedUnit::Uus) == "1234 uu/s");
    CHECK(PredictionModule::formatBallSpeed(std::nanf(""), SpeedUnit::Kph) == "0 km/h");
}

TEST(pr_intercept_jump_hint_uses_the_player_button) {
    Harness h;
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}};
    h.set("rlt_pr_ground_reach", "93");  // anything above 93 uu needs a jump
    h.setBall({0.f, 0.f, kR}, {0.f, -200.f, 0.f});
    h.tick();
    CHECK(h.mod.intercept().found);
    CHECK(h.mod.intercept().jump == pr::JumpKind::Jump);
    CHECK(anyText(h.draw(), "Saut (A)"));
    // "Ma config" > PlayStation: same pad keys, PlayStation names.
    h.settings.profile.buttonStyle = 2;
    DrawList d = h.draw();
    CHECK(anyText(d, "Saut (Croix)"));
    CHECK(!anyText(d, "Saut (A)"));
    h.settings.profile.buttonStyle = 1;
    CHECK(anyText(h.draw(), "Saut (A)"));
}

TEST(pr_intercept_skipped_while_the_car_flies_high) {
    Harness h;
    h.snap.car.pos.z = 900.f;
    h.snap.car.onGround = false;
    h.snap.car.wheelContacts = 0;
    h.setBall({0.f, 0.f, 600.f}, {0.f, -300.f, 200.f});
    h.tick();
    CHECK(!h.mod.intercept().found);
}

// ---- Commands, settings, events ------------------------------------------------------------------------
TEST(pr_toggle_command_hides_and_shows) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f});
    h.tick();
    Actions a = h.command("toggle");
    CHECK(h.mod.hidden());
    CHECK_EQ(a.toasts.size(), 1u);
    CHECK(a.toasts[0].text.find("masquee") != std::string::npos);
    CHECK(h.draw().lines3D.empty());
    CHECK(h.mod.status().hidden);
    const long long r = h.mod.status().recomputes;
    h.run(10);
    CHECK_EQ(h.mod.status().recomputes, r);  // nothing predicted while hidden
    a = h.command("toggle");
    CHECK(!h.mod.hidden());
    h.run(1);
    CHECK(!h.draw().lines3D.empty());
    h.command("hide");
    CHECK(h.mod.hidden());
    h.command("show");
    CHECK(!h.mod.hidden());
    h.command("inconnue");
    CHECK(!h.mod.hidden());
}

TEST(pr_disabled_setting_and_invalid_ball) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f});
    h.tick();
    h.set("rlt_pr_enabled", "0");
    CHECK(!h.mod.path().valid);
    h.run(5);
    CHECK(!h.mod.path().valid);
    CHECK(h.draw().lines3D.empty());
    CHECK(!h.mod.status().active);
    h.set("rlt_pr_enabled", "1");
    h.run(1);
    CHECK(h.mod.path().valid);
    h.snap.ball.valid = false;
    h.run(1);
    CHECK(!h.mod.path().valid);
    CHECK(h.draw().lines3D.empty());
    h.snap.ball.valid = true;
    h.run(1);
    CHECK(h.mod.path().valid);
}

TEST(pr_no_snapshot_does_nothing) {
    Harness h;
    h.hasSnapshot = false;
    h.setBall({0.f, 0.f, 800.f});
    h.tick();
    CHECK(!h.mod.path().valid);
    CHECK(h.draw().lines3D.empty());
}

TEST(pr_context_change_resets_transient_state) {
    Harness h;
    h.setBall({0.f, 4000.f, kR}, {0.f, 2000.f, 0.f});
    h.tick();
    CHECK(h.mod.path().valid);
    h.event(GameEventKind::ContextChanged);
    CHECK(!h.mod.path().valid);
    CHECK(h.draw().lines3D.empty());
    // A ring map: free flight, no goals.
    h.game.mode = GameMode::Workshop;
    h.game.isCustomMap = true;
    h.game.category = MapCategory::Rings;
    h.run(1);
    CHECK(h.mod.path().arena == pr::ArenaMode::None);
    CHECK_EQ(h.mod.path().goalIndex, -1);
    h.set("rlt_pr_arena", "1");
    h.run(1);
    CHECK(h.mod.path().arena == pr::ArenaMode::Soccar);
}

TEST(pr_arena_choice) {
    GameContext stock;
    CHECK(PredictionModule::chooseArena(0, stock, 91.25f) == pr::ArenaMode::Soccar);
    CHECK(PredictionModule::chooseArena(0, stock, 100.f) == pr::ArenaMode::FloorOnly);
    GameContext custom;
    custom.isCustomMap = true;
    custom.category = MapCategory::Dribble;
    CHECK(PredictionModule::chooseArena(0, custom, 91.25f) == pr::ArenaMode::FloorOnly);
    custom.category = MapCategory::AirDribble;
    CHECK(PredictionModule::chooseArena(0, custom, 91.25f) == pr::ArenaMode::None);
    custom.category = MapCategory::Rings;
    CHECK(PredictionModule::chooseArena(0, custom, 91.25f) == pr::ArenaMode::None);
    CHECK(PredictionModule::chooseArena(1, custom, 91.25f) == pr::ArenaMode::Soccar);
    CHECK(PredictionModule::chooseArena(2, stock, 91.25f) == pr::ArenaMode::FloorOnly);
    CHECK(PredictionModule::chooseArena(3, stock, 91.25f) == pr::ArenaMode::None);
}

TEST(pr_never_takes_world_control) {
    Harness h;
    h.setBall({0.f, 0.f, 800.f}, {500.f, 500.f, 500.f});
    for (int i = 0; i < 200; ++i) {
        h.advance();
        h.tick();
        h.draw();
    }
    h.command("toggle");
    h.command("toggle");
    h.command("reset_stats");
    h.event(GameEventKind::ResetShot);
    h.game.worldControlAllowed = false;
    h.run(10);
    CHECK_EQ(h.host.requests, 0);
    CHECK_EQ(h.host.releases, 0);
    CHECK_EQ(h.worldControlActions, 0);
}

// ---- Accuracy probe and persistence --------------------------------------------------------------------
TEST(pr_accuracy_probe_measures_the_one_second_error) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    h.run(PredictionModule::kProbeTicks);
    PredictionAccuracy acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 1);
    CHECK(acc.meanError < 1.f);  // same physics as the synthetic ball
    // A small unforeseen push (below the recompute threshold) shows up as error 1 s later.
    h.run(30);
    h.advance();
    h.snap.ball.vel.x += 30.f;
    h.tick();
    h.run(PredictionModule::kProbeTicks);
    acc = h.mod.accuracy();
    CHECK_EQ(acc.count, 2);
    CHECK(acc.lastError > 10.f && acc.lastError < 30.f);
    CHECK_NEAR(acc.maxError, acc.lastError, 1e-4f);
    CHECK_EQ(acc.recent.size(), 2u);
}

TEST(pr_accuracy_probe_ignores_touches_teleports_and_flags_disturbances) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();  // frame 1000: probe targets frame 1120
    h.run(20);
    TouchEvent t;
    t.pre = h.snap;
    t.frame = h.snap.frame;
    Actions a;
    h.mod.onTouch(t, h.tc(), a);
    // Up to the original target: the touched probe was dropped, the replacement (started at 1021) is not due.
    h.run(PredictionModule::kProbeTicks - 20);
    CHECK_EQ(h.snap.frame, 1120);
    CHECK_EQ(h.mod.accuracy().count, 0);
    CHECK_EQ(h.mod.accuracy().disturbed, 0);
    // The replacement completes at 1141 with the model's (zero) error.
    h.run(21);
    CHECK_EQ(h.mod.accuracy().count, 1);
    CHECK(h.mod.accuracy().lastError < 1.f);
    // Teleport: dropped, not counted as disturbed.
    Harness g;
    g.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    g.tick();
    g.run(30);
    g.advance();
    g.snap.ball.pos.x += 1000.f;
    g.tick();
    g.run(95);
    CHECK_EQ(g.mod.accuracy().count, 0);
    CHECK_EQ(g.mod.accuracy().disturbed, 0);
    // Another car smashes the ball: the error is far too big to be the model's, counted as disturbed.
    Harness k;
    k.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    k.tick();
    k.run(30);
    k.advance();
    k.snap.ball.vel.x += 1500.f;
    k.tick();
    k.run(PredictionModule::kProbeTicks);
    CHECK(k.mod.accuracy().disturbed >= 1);
}

TEST(pr_save_load_roundtrip) {
    Harness h;
    h.setBall({0.f, -2000.f, 1500.f}, {300.f, 400.f, 300.f});
    h.tick();
    for (int i = 0; i < 3; ++i) {
        h.run(PredictionModule::kProbeTicks);
        h.advance();
        h.snap.ball.vel.x += 20.f;
        h.tick();
    }
    const PredictionAccuracy a = h.mod.accuracy();
    CHECK(a.count >= 3);
    Store st;
    h.mod.save(st);
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("prediction.", 0) == 0, kv.first);
    PredictionModule fresh;
    fresh.load(st);
    const PredictionAccuracy b = fresh.accuracy();
    CHECK_EQ(b.count, a.count);
    CHECK_NEAR(b.meanError, a.meanError, 0.01f);
    CHECK_NEAR(b.maxError, a.maxError, 0.01f);
    CHECK_EQ(b.disturbed, a.disturbed);
    CHECK_EQ(b.recent.size(), a.recent.size());
    for (size_t i = 0; i < a.recent.size() && i < b.recent.size(); ++i) CHECK_NEAR(b.recent[i], a.recent[i], 0.06f);
    // Reset command clears and asks for a save.
    Actions r = h.command("reset_stats");
    CHECK(r.requestSave);
    CHECK_EQ(h.mod.accuracy().count, 0);
}

TEST(pr_load_is_bounded_and_robust) {
    Store st;
    st.setInt("prediction.acc.count", 5);
    st.setFloat("prediction.acc.mean", 12.f);
    std::vector<std::string> many;
    for (int i = 0; i < 200; ++i) many.push_back(std::to_string(i));
    many.push_back("n'importe quoi");
    many.push_back("-4");
    st.setList("prediction.acc.recent", many);
    PredictionModule m;
    m.load(st);
    const PredictionAccuracy a = m.accuracy();
    CHECK_EQ(a.count, 5);
    CHECK_NEAR(a.meanError, 12.f, 1e-3f);
    CHECK(a.recent.size() <= static_cast<size_t>(PredictionModule::kAccuracyHistory));
    CHECK(!a.recent.empty());
    CHECK_NEAR(a.recent.back(), 199.f, 1e-3f);

    Store bad;
    bad.setString("prediction.acc.count", "abc");
    bad.setString("prediction.acc.mean", "-5");
    PredictionModule n;
    n.load(bad);
    CHECK_EQ(n.accuracy().count, 0);
    Store negative;
    negative.setInt("prediction.acc.count", 3);
    negative.setFloat("prediction.acc.mean", -5.f);
    n.load(negative);
    CHECK_EQ(n.accuracy().count, 0);
    PredictionModule e;
    e.load(Store{});
    CHECK_EQ(e.accuracy().count, 0);
}

TEST(pr_status_counts_bounces_and_apex) {
    Harness h;
    h.set("rlt_pr_horizon", "6");
    h.setBall({0.f, 0.f, 1000.f}, {0.f, 0.f, 300.f});
    h.tick();
    const PredictionStatus st = h.mod.status();
    CHECK(st.ballAirborne);
    CHECK(st.hasApex);
    CHECK(st.apexHeight > 1000.f);
    CHECK(st.bounces >= 2);
    CHECK(st.hasLanding);
    CHECK(st.landingIn > st.apexIn);
    CHECK(st.arena == pr::ArenaMode::Soccar);
    CHECK_NEAR(st.ballHeight, 1000.f, 1e-3f);
}

TEST(pr_time_markers_and_bounce_circles) {
    Harness h;
    h.setBall({3000.f, 0.f, 1000.f}, {1500.f, 0.f, 0.f});  // side wall bounce after ~0.7 s
    h.tick();
    DrawList d = h.draw();
    CHECK(anyText(d, "1 s"));
    CHECK(anyText(d, "2 s"));
    int bounceLines = 0;
    for (const auto& l : d.lines3D)
        if (sameRgb(l.color, h.mod.config().bounceColor)) ++bounceLines;
    CHECK(bounceLines >= 20);  // one 20-segment circle on the wall at least
    CHECK(h.mod.status().bounces >= 1);
    h.set("rlt_pr_time_markers", "0");
    h.set("rlt_pr_show_bounces", "0");
    d = h.draw();
    CHECK(!anyText(d, "1 s"));
    for (const auto& l : d.lines3D) CHECK(!sameRgb(l.color, h.mod.config().bounceColor));
    // Path off: only the other markers remain (landing circle, zone...).
    h.set("rlt_pr_show_path", "0");
    h.set("rlt_pr_show_zone", "0");
    d = h.draw();
    for (const auto& l : d.lines3D) CHECK(!sameRgb(l.color, h.mod.config().pathColor));
}

TEST(pr_hud_accents_follow_the_general_option) {
    Harness h;
    h.setBall({0.f, 0.f, 600.f}, {0.f, -300.f, 200.f});
    h.tick();
    CHECK(anyText(h.draw(), "Arrive a "));
    h.settings.general.hudAccents = true;
    CHECK(anyText(h.draw(), "Arrive à "));
}

TEST(pr_draw_without_a_visible_ball_has_no_occluder) {
    Harness h;
    h.setBall({0.f, 0.f, 600.f}, {400.f, 0.f, 200.f});
    h.tick();
    ViewContext v = h.view();
    v.ballValid = false;
    const DrawList d = h.drawWith(v);
    CHECK(!d.lines3D.empty());
    for (const auto& l : d.lines3D) CHECK(!l.occluder.enabled);
    CHECK(d.lines3D[0].a == h.snap.ball.pos);  // starts at the physics position
}

TEST(pr_intercept_marker_sits_under_the_predicted_ball) {
    Harness h;
    h.setBall({0.f, 0.f, 600.f}, {0.f, -300.f, 200.f});
    h.tick();
    const pr::InterceptResult ir = h.mod.intercept();
    CHECK(ir.found);
    const DrawList d = h.draw();
    int ground = 0;
    for (const auto& l : d.lines3D) {
        if (!sameRgb(l.color, h.mod.config().interceptColor)) continue;
        const Vec3 c{ir.ballPos.x, ir.ballPos.y, 2.f};
        if (std::fabs(l.a.z - 2.f) < 0.01f && std::fabs(distance(l.a, c) - 70.f) < 0.5f) ++ground;
    }
    CHECK(ground >= 24);
    // Intercepts closer than 0.1 s are not drawn (the car is already on the ball).
    Harness close;
    close.snap.car.pos = {0.f, 0.f, 17.f};
    close.setBall({0.f, 60.f, kR}, {});
    close.tick();
    CHECK(close.mod.intercept().found);
    CHECK(!anyText(close.draw(), "Interception"));
}

TEST(pr_only_airborne_in_replays) {
    Harness h;
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    h.set("rlt_pr_only_airborne", "1");
    h.setBall({0.f, 0.f, kR}, {500.f, 0.f, 0.f});
    CHECK(h.draw().lines3D.empty());
    h.setBall({0.f, 0.f, 700.f}, {500.f, 0.f, 0.f});
    h.command("recompute");
    CHECK(!h.draw().lines3D.empty());
}
