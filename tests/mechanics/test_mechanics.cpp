// Module-level tests: settings schema, results and HUD, focus filter, deadzone and bindings from the profile,
// drills (activity gating, placement, countdown, game speed restore, adaptive level), persistence, robustness.
#include <functional>
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
        // The numbers of the targets are detail lines; the compact HUD says what to do in words.
        bool target = false, todo = false;
        for (const auto& l : dl.panels[0].lines) {
            if (l.text.find("Objectif") == 0) target = target || l.detail;
            todo = todo || (!l.detail && l.text.find("À faire") == 0);
        }
        CHECK(target);
        CHECK(todo);
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

// ---- Drills: get-ready phase and speedflip setup per level ---------------------------------------------------
TEST(mechanics_drill_speedflip_countdown) {
    // Level 1 (a new player): the diagonal flip alone, from a standstill in the own half, ball parked away. The car is
    // held (pose and boost re-sent on every tick, inputs frozen) until the throttle, then a 3 s countdown.
    Rig rig;
    rig.command("drill", {"speedflip"});
    CHECK(rig.mod.drillActive());
    CHECK(rig.host.name == "Mécas : Speedflip");
    Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value());
    CHECK(a.setBall.has_value());
    CHECK(a.freezeCar);
    CHECK(!a.countdownMessage.has_value());  // waiting for the player first
    if (a.setCar) {
        CHECK(a.setCar->boost.has_value() && *a.setCar->boost == 100.f);
        CHECK(a.setCar->pos.y < -3000.f);
        CHECK(length(a.setCar->vel) < 1e-3f);
    }
    if (a.setBall) CHECK(length2D(a.setBall->pos) > 4000.f);  // parked out of the way
    CHECK(rig.mod.drillSetup().stage == 1 && !rig.mod.drillSetup().kickoff);
    // Nothing happens while the player does not press the throttle (he may still be in the menu).
    for (int i = 0; i < 4 * 120; ++i) {
        a = rig.tick(Inputs{});
        CHECK(a.freezeCar && a.setCar.has_value() && !a.goMessage);
    }
    CHECK(rig.mod.status().waitingForThrottle);
    Inputs gas;
    gas.throttle = 1.f;
    a = rig.tick(gas);
    CHECK(a.countdownMessage.has_value() && *a.countdownMessage == 3);
    CHECK(!rig.mod.status().waitingForThrottle);
    int frozen = 1, held = 1;
    bool go = false;
    std::vector<int> messages;
    for (int i = 0; i < 400 && !go; ++i) {
        a = rig.tick(gas);
        frozen += a.freezeCar ? 1 : 0;
        held += (a.setCar && length(a.setCar->vel) < 1e-3f && a.setBoost && *a.setBoost == 100.f) ? 1 : 0;
        if (a.countdownMessage) messages.push_back(*a.countdownMessage);
        go = a.goMessage;
        if (go) CHECK(!a.freezeCar && !a.setCar);  // the GO tick lets the inputs through, nothing re-placed
    }
    CHECK(go);
    CHECK(frozen >= 359 && held >= 359);  // 3 s held, every tick
    CHECK(messages == std::vector<int>({2, 1}));
    CHECK(rig.mod.status().phase == "Essai en cours");
    CHECK(rig.mod.kickoffLive() == false);  // no kickoff timing at this step

    // Level 5: the real kickoff from a diagonal spawn, ball at the centre, match boost.
    Rig ko;
    Store st;
    st.setFloat("mechanics.level.speedflip.level", 0.45f);
    st.setInt("mechanics.level.speedflip.attempts", 1);
    ko.mod.load(st);
    ko.command("drill", {"speedflip"});
    Actions k = ko.tick(Inputs{});
    CHECK(ko.mod.drillSetup().stage == 2 && ko.mod.drillSetup().kickoff);
    CHECK(k.boostRules.has_value() && !k.boostRules->unlimited);
    CHECK(!k.resetPickups);  // not on the teleport tick: the pads are respawned at GO
    if (k.setCar) {
        CHECK(k.setCar->boost.has_value() && std::fabs(*k.setCar->boost - 33.33f) < 0.01f);
        CHECK(length2D(k.setCar->pos) > 2000.f);  // a kickoff spawn
        CHECK(std::fabs(k.setCar->pos.z - 20.f) < 0.01f);
    }
    if (k.setBall) CHECK(length(k.setBall->pos - Vec3{0.f, 0.f, 92.75f}) < 1.f);
    Inputs boost;
    boost.boost = true;  // boost held at GO
    CHECK(ko.waitGo(boost));
    CHECK(ko.routed.resetPickups && !ko.routed.setCar);  // GO tick: every pad back, nothing re-placed
    CHECK(ko.mod.kickoffLive());
    // Orange team: the spawn is mirrored.
    Rig orange;
    orange.sim.team = 1;
    orange.last = orange.sim.snap(orange.frame, {});
    orange.mod.load(st);
    orange.command("drill", {"speedflip"});
    Actions o = orange.tick(Inputs{});
    CHECK(o.setCar.has_value());
    if (o.setCar) CHECK(o.setCar->pos.y > 2000.f);
}

// Boost pads of the speedflip kickoff drill (same cause as the kickoff report on v1.0.7: "il me fait prendre les
// boosts"). Pad model of tests/kickoff/test_kickoff_round2.cpp (PadWorld): in the glue the pad reset runs before the
// physics step that moves a placed car, so a teleport takes every available pad whose pickup radius its straight move
// crosses, after a reset sent on the same tick. Small pads come back 4 s after a pickup, big ones 10 s (RLBot "Useful
// Game Values"). Soccar pads: KickoffData.cpp kickoffSoccarPads (not linked here).
namespace {
struct MechPad {
    float x, y;
    bool big;
};
const std::vector<MechPad>& mechSoccarPads() {
    static const std::vector<MechPad> pads = {
        {0, -4240, false},    {-1792, -4184, false}, {1792, -4184, false}, {-940, -3308, false},  {940, -3308, false},
        {0, -2816, false},    {-3584, -2484, false}, {3584, -2484, false}, {-1788, -2300, false}, {1788, -2300, false},
        {-2048, -1036, false}, {0, -1024, false},    {2048, -1036, false}, {-1024, 0, false},     {1024, 0, false},
        {-2048, 1036, false}, {0, 1024, false},      {2048, 1036, false},  {-1788, 2300, false},  {1788, 2300, false},
        {-3584, 2484, false}, {3584, 2484, false},   {0, 2816, false},     {-940, 3308, false},   {940, 3308, false},
        {-1792, 4184, false}, {1792, 4184, false},   {0, 4240, false},     {-3584, 0, true},      {3584, 0, true},
        {-3072, 4096, true},  {3072, 4096, true},    {-3072, -4096, true}, {3072, -4096, true}};
    return pads;
}
float segDist2D(float px, float py, const Vec3& a, const Vec3& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.f;
    t = std::max(0.f, std::min(1.f, t));
    const float ex = a.x + dx * t - px, ey = a.y + dy * t - py;
    return std::sqrt(ex * ex + ey * ey);
}
struct MechPadWorld {
    std::vector<int64_t> backAt = std::vector<int64_t>(mechSoccarPads().size(), 0);
    int teleportPickups = 0;
    // `from`: car position of the tick's snapshot; `reset` / `to`: what the tick sends.
    void step(int64_t frame, const Vec3& from, bool reset, const std::optional<CarPlacement>& to) {
        if (reset) std::fill(backAt.begin(), backAt.end(), 0);
        if (!to) return;
        const auto& pads = mechSoccarPads();
        for (size_t i = 0; i < pads.size(); ++i) {
            const float r = pads[i].big ? 208.f : 144.f;
            if (frame < backAt[i] || segDist2D(pads[i].x, pads[i].y, from, to->pos) >= r) continue;
            backAt[i] = frame + (pads[i].big ? 10 : 4) * 120;
            ++teleportPickups;
        }
    }
    // Pads still missing at `frame` on the straight route from `spawn` to the ball at the centre.
    int missingOnRoute(int64_t frame, const Vec3& spawn) const {
        const auto& pads = mechSoccarPads();
        int n = 0;
        for (size_t i = 0; i < pads.size(); ++i)
            if (frame < backAt[i] && segDist2D(pads[i].x, pads[i].y, spawn, Vec3{0.f, 0.f, 0.f}) < (pads[i].big ? 208.f : 144.f))
                ++n;
        return n;
    }
};
}  // namespace

TEST(mechanics_speedflip_kickoff_pads_respawned_at_go_not_on_the_teleport) {
    Rig rig;
    rig.set("rlt_me_drill_adaptive", "0");  // stays on the kickoff step whatever the attempts give
    rig.command("drill", {"speedflip", "kickoff"});
    CHECK(rig.mod.drillActive());
    MechPadWorld now, old;  // old: v1.0.7 timing, the reset sent with the placement (the tick with the boost rules)
    Inputs in;
    in.boost = true;
    int gos = 0, resets = 0, holdTicks = 0, oldMissing = 0;
    Vec3 spawn;
    bool placed = false;
    for (int i = 0; i < 40 * 120 && gos < 4; ++i) {
        in.throttle = i < 2 ? 0.f : 1.f;  // first attempt: throttle released, then pressed
        rig.tick(in);
        const Vec3 from = rig.last.car.pos;  // the snapshot the module saw: the car before this tick's placement
        const int64_t frame = rig.last.frame;
        const Actions& a = rig.routed;
        if (a.boostRules) {
            CHECK(a.setCar.has_value());
            if (a.setCar) spawn = a.setCar->pos;
            placed = true;
        }
        if (a.setCar) {
            ++holdTicks;
            CHECK_MSG(!a.resetPickups, "pad reset on a tick that places the car, frame " + std::to_string(frame));
        }
        if (a.resetPickups) {
            ++resets;
            CHECK(a.goMessage);  // only at GO
        }
        now.step(frame, from, a.resetPickups, a.setCar);
        old.step(frame, from, a.boostRules.has_value(), a.setCar);
        if (a.goMessage) {
            CHECK(placed && a.resetPickups && !a.setCar);
            ++gos;
            CHECK_EQ(now.missingOnRoute(frame, spawn), 0);
            oldMissing += old.missingOnRoute(frame, spawn);
        }
    }
    CHECK_EQ(gos, 4);
    CHECK_EQ(resets, gos);  // exactly once per attempt
    CHECK(holdTicks > 4 * 200);
    // The model reproduces the report: the teleports back to the spawn ran over pads of the route, and with the reset
    // on the teleport tick some were still missing at GO (2 s countdown, 4 s respawn).
    CHECK(now.teleportPickups > 0);
    CHECK(oldMissing > 0);
}

TEST(mechanics_drill_air_start_held_during_countdown) {
    // Cross-cutting fix 1: an air start is re-placed on every tick, so the car does not fall during the countdown.
    for (const char* id : {"wavedash", "stall", "helicopter", "zapdash", "recovery"}) {
        Rig rig;
        rig.command("drill", {id});
        rig.tick(Inputs{});
        const Vec3 pose = rig.mod.drillSetup().car.pos;
        float maxDrift = 0.f;
        Inputs gas;
        rig.tick(gas);
        gas.throttle = 1.f;
        for (int i = 0; i < 4 * 120 && rig.mod.status().phase == "Compte à rebours"; ++i) {
            rig.tick(gas);
            if (rig.mod.status().phase == "Compte à rebours") maxDrift = std::max(maxDrift, length(rig.sim.pos - mirrorY(pose)));
        }
        CHECK_MSG(rig.mod.status().phase == "Essai en cours", id);
        CHECK_MSG(maxDrift < 1.f, std::string(id) + " drift " + std::to_string(maxDrift));
    }
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
    if (a.setCar) CHECK(a.setCar->pos.z > 300.f);  // held 3.5 m up at level 1
    CHECK(rig.waitGo());
    rig.run(240, dropWavedash());
    const AttemptResult* r = rig.result("mech.wavedash");
    CHECK_MSG(r != nullptr, "no wavedash result in the drill");
    if (r) {
        CHECK(r->success);
        CHECK(rig.mod.level(Focus::Wavedash) > level0);  // staircase went up
    }
    CHECK(rig.mod.status().phase == "Résultat");
    const int cars = rig.setCars;
    rig.run(200, idle());  // drill delay 1.5 s then automatic reset: held again, 2 s countdown without waiting
    CHECK(rig.setCars > cars);
    CHECK(rig.mod.status().phase == "Compte à rebours" && !rig.mod.status().waitingForThrottle);
    // Stop: the speed goes back to 1 in the same callback (still owner), the activity is released next tick.
    Actions s = rig.command("stop");
    CHECK(s.setGameSpeed.has_value() && *s.setGameSpeed == 1.f);
    CHECK(!rig.mod.drillActive());
    CHECK(rig.host.ownsActivity(&rig.mod));
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.releases == 1);
    CHECK_NEAR(rig.mod.appliedGameSpeed(), 1.f, 1e-6f);
    const int stopped = rig.setCars;
    rig.run(10, idle());
    CHECK(rig.setCars == stopped);  // no placement after the stop
}

TEST(mechanics_drill_timeout_restarts) {
    // Fix 5: nothing attempted until the time limit is a failed attempt, recorded, with the tip of the first missing
    // step. The player did nothing at all: the next attempt waits for the throttle again (he may be in the menu).
    Rig rig;
    CHECK(rig.startDrill("stall"));
    rig.run(4 * 120 + 10, idle());  // time limit 4 s at the first level
    CHECK(rig.count("mech.stall") == 1);
    const AttemptResult* r = rig.result("mech.stall");
    if (r) {
        CHECK(!r->success);
        CHECK(!r->tips.empty() && r->tips.front().code == "me.st_no_stall");
    }
    CHECK(rig.mod.status().drillAttempts == 1);
    CHECK(rig.mod.status().message.find("Temps écoulé") != std::string::npos);
    // The message names the step that was missing.
    CHECK(rig.mod.status().message.find("Stick d'un côté") != std::string::npos);
    CHECK(rig.mod.stats().get(Mech::Stall).attempts == 1);
    rig.run(200, idle());  // the reading delay, then the next placement, waiting for the throttle
    CHECK(rig.mod.status().phase == "Compte à rebours" && rig.mod.status().waitingForThrottle);
    CHECK_NEAR(rig.mod.level(Focus::Stall), 0.f, 1e-6f);  // already at the lowest level
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

TEST(mechanics_drill_below_level_explains) {
    // Hardest level: the drill asks for 85/100. A sloppy fast aerial (jump held too short, slow double jump) still
    // touches the falling ball high enough, but it is not the clean execution the level asks for.
    Rig rig;
    CHECK(rig.startDrill("fastaerial", 1.f));
    CHECK_NEAR(rig.mod.level(Focus::FastAerial), 1.f, 1e-6f);
    auto sloppy = fastAerialAt(&rig, 36, 12, 18);
    for (int i = 0; i < 6 * 120 && rig.results.empty(); ++i) rig.tick(sloppy(i, rig.sim));
    const AttemptResult* r = rig.result("mech.fastaerial");
    CHECK_MSG(r != nullptr, "no fast aerial graded");
    if (!r) return;
    CHECK(rig.touched && rig.touchBallZ >= 700.f);
    CHECK(!r->success);
    CHECK(r->score < 85.f);
    bool below = false;
    for (const auto& t : r->tips) {
        if (t.code != "me.drill_below_level") continue;
        below = true;
        CHECK(t.text.find("85/100") != std::string::npos);
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
        if (x.focus == Focus::FastAerial) row = &x;
    CHECK(row != nullptr);
    if (row && !row->topErrorTip.empty())
        CHECK(row->topErrorTip.find("Recommence en te concentrant") == std::string::npos);

    // Same level, a clean fast aerial (jump held 0.2 s, double jump right away) passes.
    Rig ok;
    CHECK(ok.startDrill("fastaerial", 1.f));
    auto clean = fastAerialAt(&ok, 36, 24, 4);
    for (int i = 0; i < 6 * 120 && ok.results.empty(); ++i) ok.tick(clean(i, ok.sim));
    const AttemptResult* g = ok.result("mech.fastaerial");
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
    // The player's own reset during an attempt: placed again, 3 s countdown without waiting for the throttle.
    Rig rig;
    CHECK(rig.startDrill("recovery"));
    rig.run(20, idle());
    CHECK(rig.mod.status().phase == "Essai en cours");
    rig.event(GameEventKind::ResetShot);
    const Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value() && a.freezeCar);
    CHECK(rig.mod.status().phase == "Compte à rebours" && !rig.mod.status().waitingForThrottle);
    int ticks = 0;
    while (rig.mod.status().phase == "Compte à rebours" && ticks < 600) {
        rig.tick(Inputs{});
        ++ticks;
    }
    CHECK(ticks >= 358 && ticks <= 362);
}

// The 3D lines of the drills (line to the ball, lane, target, wall band) are off by default since the player found
// them gadget-like; turned on in the settings they stay bounded.
TEST(mechanics_drill_guides_bounded) {
    for (const char* id : {"chaindash", "walldash", "speedflip", "fastaerial", "powerslide"}) {
        for (bool on : {false, true}) {
            Rig rig;
            if (on) rig.set("rlt_me_guides", "1");
            rig.command("drill", {id});
            rig.tick(Inputs{});
            DrawList dl;
            ViewContext view;
            view.valid = true;
            view.ballValid = true;
            DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, true, 1.f};
            rig.mod.draw(view, dctx, dl);
            CHECK_MSG(on ? !dl.lines3D.empty() : dl.lines3D.empty(), id);
            CHECK_MSG(dl.lines3D.size() < 100, id);
            CHECK(dl.panels.size() == 1);
        }
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
        CHECK(rig.startDrill("fastaerial"));
        CHECK(rig.mod.drillSetup().hangBall);
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
        CHECK(rig.startDrill("fastaerial"));
        rig.run(90, script);
        rig.run(600, idle());  // never touched: graded as a miss once the touch window is over
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK(r != nullptr);
        if (r) {
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code == "me.drill_no_touch");
        }
    }
}

namespace {
// Steering toward the ball (sim axes, ball at the centre spot): + = right.
float steerToBall(const MiniSim& c, const Vec3& ballSim) {
    const Vec3 to = normalize(Vec3{ballSim.x - c.pos.x, ballSim.y - c.pos.y, 0.f});
    const Vec3 f = normalize(Vec3{c.fwd().x, c.fwd().y, 0.f});
    const float crossZ = f.x * to.y - f.y * to.x;  // > 0: ball to the left
    return clampf(-crossZ * 6.f, -1.f, 1.f);
}
} // namespace

TEST(mechanics_drill_speedflip_end_to_end) {
    // Kickoff step (level 5, a diagonal): a player who steers a little toward the ball, then speedflips, succeeds.
    Rig rig;
    Inputs hold;
    hold.boost = true;
    CHECK(rig.startDrill("speedflip", 0.45f, false, hold));
    CHECK(rig.mod.status().phase == "Essai en cours");
    auto inverted = std::make_shared<bool>(false);
    for (int t = 0; t < 6 * 120 && rig.results.empty(); ++t) {
        MiniSim& c = rig.sim;
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        const int J = 60, D = J + 9;
        if (t < J) in.steer = steerToBall(c, {0.f, 0.f, 0.f});
        else if (t < J + 8) in.jump = true;
        else if (t >= D && t < D + 4) {
            in.jump = t < D + 3;
            in.pitch = -std::cos(30.f * kDegToRad);
            in.yaw = -std::sin(30.f * kDegToRad);
        } else if (t >= D + 4) {
            in.pitch = 1.f;
            if (c.up().z < -0.3f) *inverted = true;
            in.roll = -1.f;
            if (c.up().z > 0.3f && t > D + 30) {
                Inputs pd;
                orientPD(c, normalize(Vec3{-c.pos.x, -c.pos.y, 0.f}), pd);
                in.roll = pd.roll;
                in.yaw = pd.yaw;
                in.pitch = pd.pitch;
            }
            if (c.wheelCount() >= 3) {
                in.roll = in.pitch = in.yaw = 0.f;
                in.steer = steerToBall(c, {0.f, 0.f, 0.f});
            }
        }
        rig.tick(in);
    }
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK(r != nullptr);
    CHECK(rig.mod.status().drillAttempts == 1);
    if (r) {
        std::string tips;
        for (const auto& tp : r->tips) tips += tp.code + " ";
        CHECK_MSG(r->success, tips + " gap " + std::to_string(rig.closestGap));
        bool angle = false;
        for (const auto& m : r->metrics) angle = angle || (m.key == "absAngleDeg" && m.hasIdeal && std::fabs(m.ideal - 30.f) < 0.01f);
        CHECK(angle);
    }
}

TEST(mechanics_drill_boost_window) {
    // Gestion du boost (spec proof test): the car starts below supersonic, the throttle alone never gets there.
    auto runBoost = [](std::function<Inputs(MiniSim&)> drive) {
        auto rig = std::make_unique<Rig>();
        rig->command("drill", {"boost"});
        Actions a = rig->tick(Inputs{});
        CHECK(a.setCar.has_value());
        if (a.setCar) CHECK(length(a.setCar->vel) < 1e-3f);  // held still before GO
        CHECK(rig->waitGo());
        CHECK(length(rig->sim.vel) > 1590.f && length(rig->sim.vel) < 2200.f);  // launched below supersonic
        for (int i = 0; i < 5 * 120 && rig->results.empty(); ++i) rig->tick(drive(rig->sim));
        return rig;
    };
    {
        auto rig = runBoost([](MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            return in;
        });
        const AttemptResult* r = rig->result("mech.boost");
        CHECK(r != nullptr);
        if (r) {
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code == "me.sm_slow_to_supersonic");
        }
    }
    {
        auto rig = runBoost([](MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = length(c.vel) < 2290.f;
            return in;
        });
        const AttemptResult* r = rig->result("mech.boost");
        CHECK(r != nullptr);
        if (r) {
            std::string tips;
            for (const auto& t : r->tips) tips += t.code + " ";
            CHECK_MSG(r->success, tips);
            for (const auto& m : r->metrics)
                if (m.key == "boostUsed") CHECK(m.value <= 35.f);
        }
    }
    {
        auto rig = runBoost([](MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = true;
            return in;
        });
        const AttemptResult* r = rig->result("mech.boost");
        CHECK(r != nullptr);
        if (r) {
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code == "me.sm_boost_at_max");
        }
    }
}

TEST(mechanics_status_read_model) {
    Rig rig;
    MechanicsModule::Status st = rig.mod.status();
    CHECK(st.enabled && !st.drillActive && st.focus == Focus::None);
    CHECK(rig.startDrill("chaindash"));
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
    CHECK_NEAR(rig.mod.level(Focus::Wavedash), 0.f, 1e-6f);  // back to the first level
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
    st.setInt("mechanics.level.wavedash.attempts", 3);
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
