// Module-level tests: settings schema, results and HUD, focus filter, deadzone and bindings from the profile,
// drills (activity gating, placement, countdown, game speed restore, adaptive level), persistence, robustness.
#include <memory>
#include <string>

#include "tests/mechanics/MechRig.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

// ---- Settings ------------------------------------------------------------------------------------------
TEST(mechanics_schema_defaults_and_labels) {
    SettingsSchema schema;
    MechanicsModule::describeSettings(schema);
    Settings s;
    schema.applyDefaults(s);
    const MechanicsConfig def;
    CHECK(s.mechanics.enabled == def.enabled);
    CHECK_NEAR(s.mechanics.hudX, 0.01f, 1e-6f);
    CHECK_NEAR(s.mechanics.hudY, 0.40f, 1e-6f);
    CHECK_NEAR(s.mechanics.slowMotion, 1.f, 1e-6f);
    CHECK(s.mechanics.chatSummary == false);
    CHECK(s.mechanics.hudMetrics == def.hudMetrics);
    CHECK(s.mechanics.chainGapMs == def.chainGapMs);
    const auto defs = schema.forModule("mechanics");
    CHECK(defs.size() >= 30);
    bool hasAdvanced = false;
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_me_", 0) == 0, d->cvar);
        CHECK_MSG(!d->label.empty() && !d->tooltip.empty(), d->cvar);
        CHECK_MSG(!hasEmDash(d->label) && !hasEmDash(d->tooltip) && !hasEmDash(d->section), d->cvar);
        // Every default round-trips through apply/read.
        Settings t;
        CHECK_MSG(d->apply(t, d->defaultValue), d->cvar);
        CHECK_MSG(d->read(t) == d->defaultValue, d->cvar + " " + d->read(t) + " vs " + d->defaultValue);
        hasAdvanced = hasAdvanced || d->advanced;
    }
    CHECK(hasAdvanced);
    const auto sections = schema.sections("mechanics");
    for (const char* sec : {"Sol", "Flips", "Air", "Mur", "Boost", "Focus et exercices"})
        CHECK_MSG(std::find(sections.begin(), sections.end(), std::string(sec)) != sections.end(), sec);
    // Clamping and bad values.
    CHECK(schema.apply(s, "rlt_me_slowmo", "0.05"));
    CHECK_NEAR(s.mechanics.slowMotion, 0.25f, 1e-6f);
    CHECK(!schema.apply(s, "rlt_me_hud_x", "abc"));
}

// ---- Free detection ------------------------------------------------------------------------------------
TEST(mechanics_free_wavedash_result_and_hud) {
    Rig rig;
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(160, groundWavedash());
    const AttemptResult* r = rig.result("mech.wavedash");
    CHECK(r != nullptr);
    if (r) {
        CHECK(r->module == "mechanics");
        CHECK(r->title == "Wavedash");
        CHECK(r->success);
        CHECK(r->verdict == Verdict::Good || r->verdict == Verdict::Perfect);
        CHECK(r->metrics.size() >= 2 && r->metrics.size() <= 5);
        bool anyIdeal = false;
        for (const auto& m : r->metrics) anyIdeal = anyIdeal || m.hasIdeal;
        CHECK(anyIdeal);
        CHECK(r->hasRecordValue);
    }
    CHECK(rig.results.size() == 1);
    CHECK(rig.mod.lastAttempt().valid);
    CHECK(rig.mod.stats().get(Mech::Wavedash).attempts == 1);
    CHECK(rig.chat.empty());  // chat summary is off by default

    DrawList dl;
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, false, 1.f};
    rig.mod.draw(view, dctx, dl);
    CHECK(dl.panels.size() == 1);
    if (!dl.panels.empty()) {
        const DrawPanel& p = dl.panels[0];
        CHECK_NEAR(p.pos.x, 0.01f * 1920.f, 0.01f);
        CHECK_NEAR(p.pos.y, 0.40f * 1080.f, 0.01f);
        CHECK(p.title == "Dernière méca");
        CHECK(p.lines.size() >= 3 && p.lines.size() <= 20);
        bool bar = false;
        for (const auto& l : p.lines) bar = bar || (l.bar >= 0.f && l.bar <= 1.f);
        CHECK(bar);
    }
    CHECK(dl.lines3D.empty());  // no drill, no guide

    // The panel fades after hudSeconds, stays forever with 0.
    rig.run(8 * 120 + 30, idle());
    DrawList dl2;
    DrawContext dctx2{rig.settings, rig.game, rig.profile, rig.last, true, false, 1.f};
    rig.mod.draw(view, dctx2, dl2);
    CHECK(dl2.panels.empty());
    rig.set("rlt_me_hud_seconds", "0");
    DrawList dl3;
    rig.mod.draw(view, dctx2, dl3);
    CHECK(dl3.panels.size() == 1);
    // Disabled HUD and replays draw nothing.
    rig.set("rlt_me_hud", "0");
    DrawList dl4;
    rig.mod.draw(view, dctx2, dl4);
    CHECK(dl4.empty());
    rig.set("rlt_me_hud", "1");
    GameContext replay = rig.game;
    replay.mode = GameMode::Replay;
    DrawContext dctx5{rig.settings, replay, rig.profile, rig.last, true, false, 1.f};
    DrawList dl5;
    rig.mod.draw(view, dctx5, dl5);
    CHECK(dl5.empty());
}

TEST(mechanics_family_toggle_and_master_switch) {
    {
        Rig rig;
        rig.set("rlt_me_wavedash", "0");
        rig.sim.vel = {1000.f, 0.f, 0.f};
        rig.run(160, groundWavedash());
        CHECK(rig.count("mech.wavedash") == 0);
    }
    {
        Rig rig;
        rig.set("rlt_me_enabled", "0");
        rig.sim.vel = {1000.f, 0.f, 0.f};
        rig.run(160, groundWavedash());
        CHECK(rig.results.empty());
        rig.command("drill", {"wavedash"});
        CHECK(!rig.mod.drillActive());
        CHECK(rig.host.requests == 0);
    }
}

TEST(mechanics_chat_summary_and_min_score) {
    Rig rig;
    rig.set("rlt_me_chat", "1");
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(160, groundWavedash());
    CHECK(rig.chat.size() == 1);
    if (!rig.chat.empty()) CHECK(rig.chat[0].text.find("Wavedash") != std::string::npos);

    Rig low;
    low.set("rlt_me_min_score", "100");
    low.set("rlt_me_chat", "1");
    low.sim.vel = {1000.f, 0.f, 0.f};
    low.run(160, groundWavedash(10));
    // Graded and counted, but neither shown nor written to the chat.
    CHECK(low.count("mech.wavedash") == 1);
    if (!low.results.empty() && low.results[0].score < 100.f) {
        CHECK(!low.mod.lastAttempt().valid);
        CHECK(low.chat.empty());
    }
}

TEST(mechanics_map_category_filters_powerslide) {
    for (MapCategory cat : {MapCategory::Standard, MapCategory::Rings}) {
        Rig rig;
        rig.game.category = cat;
        rig.sim.vel = {1400.f, 0.f, 0.f};
        rig.run(200, [](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            if (t >= 10 && t < 30) {
                in.steer = 1.f;
                in.handbrake = true;
            }
            return in;
        });
        CHECK(rig.count("mech.powerslide") == (cat == MapCategory::Standard ? 1 : 0));
    }
}

// ---- Focus ---------------------------------------------------------------------------------------------
TEST(mechanics_focus_filters_other_mechanics) {
    Rig rig;
    rig.command("focus", {"speedflip"});
    CHECK(rig.mod.focus() == Focus::SpeedFlip);
    CHECK(rig.mod.detectConfig().focus == Focus::SpeedFlip);
    CHECK(rig.host.requests == 0);  // no slow motion asked: no world control needed
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(160, groundWavedash());
    CHECK(rig.count("mech.wavedash") == 0);
    CHECK(rig.mod.stats().get(Mech::Wavedash).attempts == 0);
    // Targets are shown permanently in focus mode.
    DrawList dl;
    ViewContext view;
    view.valid = true;
    DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, false, 1.f};
    rig.mod.draw(view, dctx, dl);
    CHECK(dl.panels.size() == 1);
    if (!dl.panels.empty()) {
        CHECK(dl.panels[0].title.find("Speedflip") != std::string::npos);
        bool target = false;
        for (const auto& l : dl.panels[0].lines) target = target || l.text.find("Objectif") == 0;
        CHECK(target);
    }
    rig.command("focus", {"off"});
    CHECK(rig.mod.focus() == Focus::None);
    rig.sim = MiniSim{};
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(200, groundWavedash());
    CHECK(rig.count("mech.wavedash") == 1);
}

TEST(mechanics_unknown_commands_warn) {
    Rig rig;
    rig.command("focus", {"backflip"});
    CHECK(rig.toastContains("inconnue"));
    rig.toasts.clear();
    rig.command("drill", {});
    CHECK(rig.toastContains("inconnue"));
    rig.toasts.clear();
    rig.command("dance");
    CHECK(rig.toastContains("inconnue"));
    CHECK(rig.mod.focus() == Focus::None);
    CHECK(!rig.mod.drillActive());
}

// ---- Profile: deadzone and bindings --------------------------------------------------------------------
TEST(mechanics_deadzone_and_buttons_from_profile) {
    auto aerial = [](int hold, int gap, int boostFrom, float stick) {
        return [=](int t, MiniSim&) {
            Inputs in;
            const int J = 10, DJ = J + hold + gap;
            in.throttle = 1.f;
            in.boost = t >= boostFrom;
            if (t >= J && t < J + hold) {
                in.jump = true;
                in.pitch = 1.f;
            }
            if (t >= J + hold && t < DJ) in.pitch = 1.f;
            if (t >= DJ && t < DJ + 2) {
                in.jump = true;
                in.pitch = stick;
            }
            if (t >= DJ + 2 && t < DJ + 30) in.pitch = 1.f;
            return in;
        };
    };
    for (float dz : {0.5f, 0.9f}) {
        Rig rig;
        PlayerProfile p;
        p.valid = true;
        p.dodgeDeadzone = dz;
        rig.setProfile(p);
        rig.sim.deadzone = dz;
        CHECK_NEAR(rig.mod.detectConfig().dodgeDeadzone, dz, 1e-6f);
        rig.run(120, aerial(24, 5, 10, 0.4f));
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK(r != nullptr);
        if (!r) continue;
        bool nearMiss = false;
        for (const auto& t : r->tips) nearMiss = nearMiss || t.code == "me.fa_stick_near_deadzone";
        CHECK(nearMiss == (dz < 0.55f));
    }
    {
        Rig rig;
        PlayerProfile p;
        p.valid = true;
        p.usingGamepad = true;
        p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"AirRollLeft", "XboxTypeS_LeftShoulder"}};
        rig.setProfile(p);
        rig.command("focus", {"fastaerial"});
        rig.run(120, aerial(24, 5, 30, 0.f));  // boost far too late
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK(r != nullptr);
        if (r) {
            bool button = false;
            for (const auto& t : r->tips) button = button || t.text.find("Boost (B)") != std::string::npos;
            CHECK(button);
            for (size_t i = 1; i < r->tips.size(); ++i) CHECK(r->tips[i - 1].weight >= r->tips[i].weight);
        }
        MechGrade g;
        g.mech = Mech::SpeedFlip;
        g.variant = "left";
        CHECK(tipText("SF_BAD_LANDING", g, p, 0.5f).find("Air roll gauche (") != std::string::npos);
    }
}

// ---- Drills: activity gating ---------------------------------------------------------------------------
TEST(mechanics_drill_refused_without_world_control) {
    {
        Rig rig;
        rig.game.worldControlAllowed = false;  // workshop / online: analysis only
        rig.command("drill", {"wavedash"});
        CHECK(!rig.mod.drillActive());
        CHECK(rig.host.requests == 0);
        CHECK(rig.toastContains("freeplay"));
        rig.run(30, idle());
        CHECK(rig.setCars == 0);
        CHECK(rig.raw.setCar.has_value() == false);
    }
    {
        Rig rig;
        rig.host.allow = false;  // another module keeps the activity
        rig.command("drill", {"speedflip"});
        CHECK(rig.host.requests == 1);
        CHECK(!rig.mod.drillActive());
        rig.run(30, idle());
        CHECK(rig.setCars == 0);
        CHECK(!rig.raw.setCar && !rig.raw.freezeCar && !rig.raw.setGameSpeed);
    }
}

TEST(mechanics_focus_slow_motion_needs_activity) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.host.allow = false;
    rig.command("focus", {"stall"});
    CHECK(rig.mod.focus() == Focus::Stall);  // focus still works, without slow motion
    CHECK(rig.toastContains("Ralenti indisponible"));
    rig.run(5, idle());
    CHECK(rig.speedChanges == 0);

    Rig ok;
    ok.set("rlt_me_slowmo", "0.5");
    ok.command("focus", {"stall"});
    CHECK(ok.host.ownsActivity(&ok.mod));
    ok.run(2, idle());
    CHECK_NEAR(ok.lastSpeed, 0.5f, 1e-6f);
    ok.command("focus", {"off"});
    CHECK_NEAR(ok.lastSpeed, 1.f, 1e-6f);  // restored while still owner
    ok.run(2, idle());
    CHECK(!ok.host.ownsActivity(&ok.mod));
}

// ---- Drills: speedflip kickoff setup -------------------------------------------------------------------
TEST(mechanics_drill_speedflip_countdown) {
    Rig rig;
    rig.command("drill", {"speedflip"});
    CHECK(rig.mod.drillActive());
    CHECK(rig.host.name == "Mécas : Speedflip");
    Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value());
    CHECK(a.setBall.has_value());
    CHECK(a.freezeCar);
    CHECK(a.countdownMessage.has_value());
    if (a.setCar) {
        CHECK(a.setCar->boost.has_value() && *a.setCar->boost == 33.f);
        CHECK(length2D(a.setCar->pos) > 2000.f);  // a kickoff spawn
    }
    if (a.setBall) CHECK(length(a.setBall->pos - Vec3{0.f, 0.f, 92.75f}) < 1.f);
    int frozen = 0;
    bool go = false;
    for (int i = 0; i < 130 && !go; ++i) {
        Inputs in;
        in.boost = true;  // boost held at GO
        a = rig.tick(in);
        frozen += a.freezeCar ? 1 : 0;
        go = a.goMessage;
    }
    CHECK(go);
    CHECK(frozen >= 110);  // about 1 s frozen
    CHECK(rig.mod.status().phase == "Essai en cours");
    // Orange team: the spawn is mirrored.
    Rig orange;
    orange.sim.team = 1;
    orange.last = orange.sim.snap(orange.frame, {});
    orange.command("drill", {"speedflip"});
    Actions o = orange.tick(Inputs{});
    CHECK(o.setCar.has_value() && a.setCar.has_value() == false);
    if (o.setCar) CHECK(o.setCar->pos.y > 2000.f);
}

// ---- Drills: wavedash attempt, adaptive level, auto-reset, speed restore -----------------------------------
TEST(mechanics_drill_wavedash_full_cycle) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    const float level0 = rig.mod.level(Focus::Wavedash);
    rig.command("drill", {"wavedash"});
    CHECK(rig.mod.drillActive());
    Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value());
    CHECK(a.setGameSpeed.has_value() && std::fabs(*a.setGameSpeed - 0.5f) < 1e-6f);
    if (a.setCar) CHECK(a.setCar->pos.z > 60.f);  // dropped from a small height
    rig.run(150, dropWavedash());
    const AttemptResult* r = rig.result("mech.wavedash");
    CHECK_MSG(r != nullptr, "no wavedash result in the drill");
    if (r) {
        CHECK(r->success);
        CHECK(rig.mod.level(Focus::Wavedash) > level0);  // staircase went up
    }
    CHECK(rig.mod.status().phase == "Résultat");
    const int cars = rig.setCars;
    rig.run(200, idle());  // drill delay 1.5 s then automatic reset
    CHECK(rig.setCars == cars + 1);
    // Stop: the speed goes back to 1 in the same callback (still owner), the activity is released next tick.
    Actions s = rig.command("stop");
    CHECK(s.setGameSpeed.has_value() && *s.setGameSpeed == 1.f);
    CHECK(!rig.mod.drillActive());
    CHECK(rig.host.ownsActivity(&rig.mod));
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.releases == 1);
    CHECK_NEAR(rig.mod.appliedGameSpeed(), 1.f, 1e-6f);
    rig.run(10, idle());
    CHECK(rig.setCars == cars + 1);  // no placement after the stop
}

TEST(mechanics_drill_timeout_restarts) {
    Rig rig;
    rig.command("drill", {"stall"});
    rig.tick(Inputs{});
    rig.run(3 * 120 + 10, idle());  // nothing attempted: time limit 3 s
    CHECK(rig.results.empty());
    CHECK(rig.mod.status().message.find("Temps écoulé") != std::string::npos);
    const int cars = rig.setCars;
    rig.run(130, idle());
    CHECK(rig.setCars == cars + 1);
    CHECK_NEAR(rig.mod.level(Focus::Stall), 0.2f, 1e-6f);  // a timeout does not move the staircase
}

TEST(mechanics_activity_taken_away) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    CHECK_NEAR(rig.lastSpeed, 0.5f, 1e-6f);
    // Another module takes the activity: the engine calls onActivityStopped (no Actions there).
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    CHECK(!rig.mod.drillActive());
    CHECK(rig.mod.focus() == Focus::None);  // the drill's focus ends with the drill
    const int requests = rig.host.requests;
    rig.run(20, idle());
    // Not the owner any more: no world control at all (it would override the new owner's speed), never a request
    // to take the activity back. The engine restored the speed at the handover.
    CHECK(!rig.raw.setCar && !rig.raw.setGameSpeed && !rig.raw.freezeCar);
    CHECK(rig.host.requests == requests);
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.host.engineRestores == 1);
    CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
    CHECK_NEAR(rig.mod.appliedGameSpeed(), 1.f, 1e-6f);
    CHECK(!rig.mod.speedWaiting());
}

TEST(mechanics_focus_survives_drill_it_preceded) {
    Rig rig;
    rig.command("focus", {"halfflip"});
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    rig.host.stopActivity();
    CHECK(rig.mod.focus() == Focus::HalfFlip);
}

namespace {
// Drop wavedash of the drill with a chosen nose tilt and dodge height (rear wheels above the floor).
Script dropDash(float tiltDeg, float rearZ) {
    auto S = std::make_shared<WdS>();
    return [=](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (S->phase == 0 && c.wheelCount() == 0) {
            in.pitch = pitchToNose(c, tiltDeg);
            if (c.vel.z < 0.f && c.rearWheelZ() < rearZ) {
                S->phase = 1;
                S->dodgeT = 0;
            }
        }
        if (S->phase == 1 && S->dodgeT < 3) {
            in.jump = true;
            in.pitch = -1.f;
            S->dodgeT++;
        }
        return in;
    };
}
} // namespace

TEST(mechanics_drill_below_level_explains) {
    // Hardest level: the drill asks for 80/100. A slightly early dash is a detector success (72) but not enough.
    Rig rig;
    Store st;
    st.setFloat("mechanics.level.wavedash.level", 1.f);
    rig.mod.load(st);
    CHECK_NEAR(rig.mod.level(Focus::Wavedash), 1.f, 1e-6f);
    rig.command("drill", {"wavedash"});
    rig.tick(Inputs{});
    rig.run(150, dropDash(35.f, 38.f));
    const AttemptResult* r = rig.result("mech.wavedash");
    CHECK_MSG(r != nullptr, "no wavedash graded");
    if (!r) return;
    CHECK(!r->success);
    CHECK(r->score >= 60.f && r->score < 80.f);
    bool below = false;
    for (const auto& t : r->tips) {
        if (t.code != "me.drill_below_level") continue;
        below = true;
        CHECK(t.text.find("80/100") != std::string::npos);
    }
    CHECK(below);
    // Ranked after the technique tip (what to fix comes first).
    CHECK(!r->tips.empty() && r->tips.front().code != "me.drill_below_level");
    for (size_t i = 1; i < r->tips.size(); ++i) CHECK(r->tips[i - 1].weight >= r->tips[i].weight);
    CHECK(rig.mod.status().drillAttempts == 1 && rig.mod.status().drillSuccesses == 0);
    // The stats show a dedicated line for this error, not the generic one.
    const MechanicsModule::StatRow* row = nullptr;
    const auto rows = rig.mod.statRows();
    for (const auto& x : rows)
        if (x.focus == Focus::Wavedash) row = &x;
    CHECK(row != nullptr);
    if (row && !row->topErrorTip.empty())
        CHECK(row->topErrorTip.find("Recommence en te concentrant") == std::string::npos);

    // Same level, a cleaner dash (83) passes.
    Rig ok;
    ok.mod.load(st);
    ok.command("drill", {"wavedash"});
    ok.tick(Inputs{});
    ok.run(150, dropDash(20.f, 34.f));
    const AttemptResult* g = ok.result("mech.wavedash");
    CHECK(g != nullptr && g->success);
}

TEST(mechanics_context_change_stops_drill) {
    // Freeplay to another freeplay map: world control stays allowed, so the engine does not stop the activity
    // before ContextChanged and the module restores the speed itself while still the owner.
    Rig rig;
    rig.set("rlt_me_slowmo", "0.75");
    rig.command("drill", {"powerslide"});
    rig.tick(Inputs{});
    Actions a = rig.setContext(GameMode::Freeplay, "Stadium_P", true);
    CHECK(!rig.mod.drillActive());
    CHECK(a.setGameSpeed.has_value() && *a.setGameSpeed == 1.f);
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.engineRestores == 0);  // restored by the module itself
}

TEST(mechanics_reset_shot_replaces_drill) {
    Rig rig;
    rig.command("drill", {"recovery"});
    rig.tick(Inputs{});
    const int cars = rig.setCars;
    rig.run(20, idle());
    rig.event(GameEventKind::ResetShot);
    rig.tick(Inputs{});
    CHECK(rig.setCars == cars + 1);
}

TEST(mechanics_drill_guides_bounded) {
    for (const char* id : {"chaindash", "walldash", "speedflip", "fastaerial", "powerslide"}) {
        Rig rig;
        rig.command("drill", {id});
        rig.tick(Inputs{});
        DrawList dl;
        ViewContext view;
        view.valid = true;
        view.ballValid = true;
        DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, true, 1.f};
        rig.mod.draw(view, dctx, dl);
        CHECK_MSG(!dl.lines3D.empty(), id);
        CHECK_MSG(dl.lines3D.size() < 100, id);
        CHECK(dl.panels.size() == 1);
    }
}

TEST(mechanics_drill_fast_aerial_waits_for_touch) {
    auto script = [](int t, MiniSim&) {
        Inputs in;
        const int J = 10, hold = 24, gap = 5, DJ = J + hold + gap;
        in.throttle = 1.f;
        in.boost = t >= J;
        if (t >= J && t < J + hold) {
            in.jump = true;
            in.pitch = 1.f;
        }
        if (t >= J + hold && t < DJ) in.pitch = 1.f;
        if (t >= DJ && t < DJ + 2) in.jump = true;
        if (t >= DJ + 2 && t < DJ + 30) in.pitch = 1.f;
        return in;
    };
    {
        Rig rig;
        rig.command("drill", {"fastaerial"});
        rig.tick(Inputs{});
        rig.run(90, script);
        CHECK(rig.count("mech.fastaerial") == 0);  // graded, but held until the ball is touched
        TouchEvent t;
        t.pre = rig.last;
        t.post1 = rig.last;
        t.post = rig.last;
        Actions a;
        rig.mod.onTouch(t, rig.ctx(), a);
        rig.route(a);
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK(r != nullptr);
        if (r) {
            bool ttb = false;
            for (const auto& m : r->metrics) ttb = ttb || m.key == "timeToBall";
            CHECK(ttb);
            CHECK(r->success);
        }
    }
    {
        Rig rig;
        rig.command("drill", {"fastaerial"});
        rig.tick(Inputs{});
        rig.run(90, script);
        rig.run(400, idle());  // never touched: graded as a miss after 2.5 s
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK(r != nullptr);
        if (r) {
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code == "me.drill_no_touch");
        }
    }
}

TEST(mechanics_drill_speedflip_end_to_end) {
    Rig rig;
    rig.command("drill", {"speedflip"});
    rig.tick(Inputs{});
    for (int i = 0; i < 125; ++i) {
        Inputs in;
        in.boost = true;
        in.throttle = 1.f;
        rig.tick(in);
    }
    CHECK(rig.mod.status().phase == "Essai en cours");
    // Jump, diagonal dodge, cancel: the drill grades it (focus on speedflip), result at the touch or the time limit.
    rig.run(5 * 120, [](int t, MiniSim&) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        if (t >= 30 && t < 41) in.jump = true;
        if (t >= 42 && t < 47) {
            in.jump = t < 45;
            in.pitch = -std::cos(30.f * kDegToRad);
            in.yaw = -std::sin(30.f * kDegToRad);
        }
        if (t >= 47 && t < 140) in.pitch = 1.f;
        return in;
    });
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK(r != nullptr);
    CHECK(rig.mod.status().drillAttempts == 1);
    if (r) {
        bool angle = false;
        for (const auto& m : r->metrics) angle = angle || (m.key == "absAngleDeg" && m.hasIdeal && std::fabs(m.ideal - 30.f) < 0.01f);
        CHECK(angle);
    }
}

TEST(mechanics_drill_boost_window) {
    Rig rig;
    rig.command("drill", {"boost"});
    Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value());
    if (a.setCar) CHECK(length(a.setCar->vel) > 2200.f);
    rig.run(5 * 120 + 20, [](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = length(c.vel) < 2230.f;
        return in;
    });
    const AttemptResult* r = rig.result("mech.boost");
    CHECK(r != nullptr);
    if (r) CHECK(r->success);
}

TEST(mechanics_status_read_model) {
    Rig rig;
    MechanicsModule::Status st = rig.mod.status();
    CHECK(st.enabled && !st.drillActive && st.focus == Focus::None);
    rig.command("drill", {"chaindash"});
    rig.tick(Inputs{});
    st = rig.mod.status();
    CHECK(st.drillActive && st.focus == Focus::ChainDash);
    CHECK(!st.goal.empty() && !st.variant.empty());
    CHECK(st.timeLeft > 3.f);
    CHECK(st.worldControl);
}

// ---- Robustness ----------------------------------------------------------------------------------------
TEST(mechanics_pause_and_invalid_car) {
    Rig rig;
    rig.sim.vel = {1000.f, 0.f, 0.f};
    auto script = groundWavedash();
    for (int i = 0; i < 160; ++i) rig.tick(script(i, rig.sim), true);
    CHECK(rig.results.empty());

    Rig bad;
    for (int i = 0; i < 50; ++i) {
        bad.last = bad.sim.snap(bad.frame++, {});
        bad.last.car.valid = false;
        bad.last.ball.valid = false;
        bad.hasSnap = true;
        Actions a;
        bad.mod.onTick(bad.ctx(), a);
        CHECK(a.results.empty());
    }
    DrawList dl;
    ViewContext view;
    DrawContext dctx{bad.settings, bad.game, bad.profile, bad.last, true, false, 1.f};
    bad.mod.draw(view, dctx, dl);  // nothing to show, must not crash
    CHECK(dl.lines3D.empty());
}

TEST(mechanics_map_change_resets_transient_state) {
    Rig rig;
    rig.sim.vel = {1000.f, 0.f, 0.f};
    auto script = groundWavedash();
    for (int i = 0; i < 40; ++i) rig.tick(script(i, rig.sim));  // in the air, dodge not finished
    rig.game.mapName = "Stadium_P";
    rig.event(GameEventKind::ContextChanged);
    rig.sim = MiniSim{};
    rig.run(100, idle());
    CHECK(rig.results.empty());
}

// ---- Stats, records, persistence -----------------------------------------------------------------------
TEST(mechanics_records_and_save_load) {
    Rig rig;
    for (int k = 0; k < 2; ++k) {
        rig.sim = MiniSim{};
        rig.sim.vel = {800.f + 300.f * static_cast<float>(k), 0.f, 0.f};
        rig.run(200, groundWavedash());
        rig.run(10, idle());
    }
    const MechStat& st = rig.mod.stats().get(Mech::Wavedash);
    CHECK(st.attempts == 2);
    CHECK(st.hasPb);
    CHECK(st.recent.size() == 2);
    rig.command("drill", {"wavedash"});
    rig.tick(Inputs{});
    rig.run(150, dropWavedash());
    rig.command("stop");

    Store store;
    rig.mod.save(store);
    CHECK(!store.empty());
    for (const auto& kv : store.raw()) CHECK_MSG(kv.first.rfind("mechanics.", 0) == 0, kv.first);
    CHECK(store.raw().size() < 400);

    MechanicsModule other;
    other.load(store);
    const MechStat& o = other.stats().get(Mech::Wavedash);
    CHECK(o.attempts == rig.mod.stats().get(Mech::Wavedash).attempts);
    CHECK(o.successes == rig.mod.stats().get(Mech::Wavedash).successes);
    CHECK_NEAR(o.best, rig.mod.stats().get(Mech::Wavedash).best, 1e-3f);
    CHECK(o.hasPb == rig.mod.stats().get(Mech::Wavedash).hasPb);
    CHECK_NEAR(o.pb, rig.mod.stats().get(Mech::Wavedash).pb, 0.01f);
    CHECK(o.recent.size() == rig.mod.stats().get(Mech::Wavedash).recent.size());
    CHECK_NEAR(other.level(Focus::Wavedash), rig.mod.level(Focus::Wavedash), 1e-4f);
    CHECK(o.sessionAttempts == 0);  // session counters are not persisted

    // Rows for the UI.
    const auto rows = rig.mod.statRows();
    CHECK(rows.size() == static_cast<size_t>(kFocusCount));
    for (const auto& row : rows) {
        CHECK(!row.title.empty() && !row.what.empty() && !row.key.empty());
        CHECK(!hasEmDash(row.what) && !hasEmDash(row.key));
    }

    // reset_stats for one family, then everything.
    rig.command("reset_stats", {"wavedash"});
    CHECK(rig.mod.stats().get(Mech::Wavedash).attempts == 0);
    CHECK_NEAR(rig.mod.level(Focus::Wavedash), 0.2f, 1e-6f);
    rig.command("reset_stats");
    CHECK(rig.mod.stats().sessionTotal() == 0);
    Store empty;
    rig.mod.save(empty);
    CHECK(empty.keysWithPrefix("mechanics.stats.").empty());
}

TEST(mechanics_load_is_robust_to_bad_data) {
    Store st;
    st.setString("mechanics.stats.wavedash.attempts", "-5");
    st.setString("mechanics.stats.wavedash.successes", "99");
    st.setString("mechanics.stats.wavedash.best", "500");
    std::vector<std::string> recent(100, "70");
    st.setList("mechanics.stats.wavedash.recent", recent);
    st.setList("mechanics.stats.wavedash.errors", {"=3", "me.wd_nose_down=2", "garbage"});
    st.setString("mechanics.level.wavedash.level", "7");
    MechanicsModule m;
    m.load(st);
    const MechStat& s = m.stats().get(Mech::Wavedash);
    CHECK(s.attempts == 0);
    CHECK(s.successes == 0);
    CHECK(s.best <= 100.f);
    CHECK(s.recent.size() <= MechStat::kRecentMax);
    CHECK(s.errors.size() == 1);
    CHECK(m.level(Focus::Wavedash) <= 1.f);
}

TEST(mechanics_texts_have_no_em_dash) {
    for (int i = 0; i < kMechCount; ++i) {
        CHECK(!hasEmDash(praiseText(static_cast<Mech>(i))));
        CHECK(!hasEmDash(mechInfo(static_cast<Mech>(i)).title));
    }
    PlayerProfile p;
    MechGrade g;
    for (const char* code :
         {"WD_CRASH_LANDING", "WD_NOSE_DOWN", "WD_TOO_EARLY", "WD_SIDE_NO_POWERSLIDE", "WD_HIGH_HOP", "WD_NO_STICK",
          "WALL_TAP_SLOW", "WALL_FELL_OFF", "CHAIN_NO_POWERSLIDE", "CHAIN_BOOST_USED", "SF_ANGLE_STRAIGHT",
          "SF_ANGLE_WIDE", "SF_BOOST_RELEASED", "SF_BAD_LANDING", "KO_NO_BOOST_AT_START", "HF_NO_ROLL",
          "FA_ACCIDENTAL_DODGE", "FA_GAP_SLOW", "FA_BOOST_LATE", "FA_STICK_NEAR_DEADZONE", "ST_YAW_ROLL_MISMATCH",
          "RC_ON_ROOF", "PS_EXIT_SLIP", "SM_BOOST_AT_MAX", "SM_NO_THROTTLE", "HF_EARLY_CANCEL", "DRILL_NO_TOUCH",
          "KO_FIRST_JUMP_EARLY", "KO_FIRST_JUMP_LATE", "DRILL_BELOW_LEVEL"}) {
        const std::string t = tipText(code, g, p, 0.5f);
        CHECK_MSG(!t.empty() && !hasEmDash(t), code);
        CHECK_MSG(t.find("Recommence en te concentrant") == std::string::npos, code);  // a dedicated line exists
    }
    CHECK(tipCode("SF_LATE_CANCEL") == "me.sf_late_cancel");
}

TEST(mechanics_score_sum_keeps_precision) {
    // Boost windows alone add a result every 20 s: the sum of scores grows past what a float keeps exactly.
    Store st;
    st.setInt("mechanics.stats.boost.attempts", 200001);
    st.setDouble("mechanics.stats.boost.scoreSum", 12345678.5);
    MechanicsModule m;
    m.load(st);
    const MechStat& s = m.stats().get(Mech::BoostEfficiency);
    CHECK(s.scoreSum == 12345678.5);
    CHECK_NEAR(s.average(), 12345678.5 / 200001.0, 1e-3);
    Store out;
    m.save(out);
    CHECK(out.getDouble("mechanics.stats.boost.scoreSum", 0.0) == 12345678.5);
    // Accumulates in double: adding one more score is exact.
    MechStats stats;
    stats.load(st);
    AttemptResult r;
    r.score = 77.f;
    stats.record(Mech::BoostEfficiency, r);
    CHECK(stats.get(Mech::BoostEfficiency).scoreSum == 12345755.5);
    // Corrupt sums are clamped to what the attempt count allows.
    Store bad;
    bad.setInt("mechanics.stats.wavedash.attempts", 2);
    bad.setDouble("mechanics.stats.wavedash.scoreSum", 1e12);
    MechanicsModule b;
    b.load(bad);
    CHECK(b.stats().get(Mech::Wavedash).scoreSum <= 200.0);
    CHECK(b.stats().get(Mech::Wavedash).average() <= 100.f);
}

TEST(mechanics_tab_texts_in_french) {
    // Variant labels are proper French, with hyphens where French needs them.
    CHECK(variantLabel("u-turn") == "demi-tour");
    CHECK(variantLabel("roll-left") == "air roll à gauche");
    // Decimal comma in the HUD lines and the records.
    CHECK(formatRecord(Mech::SpeedFlip, 2.41f, SpeedUnit::Uus) == "2,41 s");
    MechGrade g;
    g.mech = Mech::Stall;
    g.add("sinceJumpTicks", 160.f);
    const auto lines = metricLines(g, DetectConfig{}, SpeedUnit::Uus, 5);
    CHECK(lines.size() == 1);
    if (!lines.empty()) {
        CHECK(lines[0].value == "1,33 s");
        CHECK(lines[0].ideal == "<= 1,25 s");
    }
}
