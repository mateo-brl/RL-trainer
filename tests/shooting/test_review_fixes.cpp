// Regression tests of the review fixes, end to end through ShootingModule:
//   - realistic dodge timing (press at D, IsDodging from D+1): clean flicks are not "(?)", a double jump is
//     not graded as a flick;
//   - "flick diagonal" / "flick side" refused (not drills);
//   - the HUD cue follows the button style chosen in "Ma config";
//   - zone / setup selections are consistent between commands, cvars, the status and the flick target;
//   - the "zone", "setup" and "targets" console commands save the choice through the module's own cvars
//     (Actions::setCvars), like the tab does with ctx.setCvar.

#include <string>

#include "core/common/Controls.h"
#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

void runToLive(Harness& h, World& w, int maxTicks = 600) {
    for (int i = 0; i < maxTicks && h.mod.phase() != ShootPhase::Live; ++i) {
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
}

void runTicks(Harness& h, World& w, int n) {
    for (int i = 0; i < n; ++i) {
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
}

float yawDegOf(const Rot& r) { return static_cast<float>(r.yaw) * 360.f / 65536.f; }

void playFlick(Harness& h, World& w, FlickScript s) {
    s.start = w.car.pos;
    s.yawDeg = yawDegOf(w.car.rot);
    s.startFrame = w.frame;
    s.team = w.car.team;
    const ScriptRun run = buildFlick(s);
    for (size_t i = 0; i < run.snaps.size(); ++i) {
        for (const auto& t : run.touches)
            if (t.first == i) h.touch(t.second);
        h.tick(run.snaps[i]);
    }
    w.frame = run.snaps.back().frame + 1;
    w.car = run.snaps.back().car;
    w.ball = run.snaps.back().ball;
}

ViewContext viewFor(const World& w) {
    ViewContext v;
    v.valid = true;
    v.camPos = w.car.pos + Vec3{0.f, -300.f, 150.f};
    v.camBasis = basisFromRot(rotDeg(-5.f, 90.f, 0.f));
    v.fovDeg = 110.f;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.carValid = true;
    v.carVisualPos = w.car.pos;
    v.carVisualBasis = w.car.basis;
    v.ballValid = true;
    v.ballVisualPos = w.ball.pos;
    v.ballVel = w.ball.vel;
    return v;
}

bool panelHas(const DrawList& d, const std::string& needle) {
    for (const auto& p : d.panels)
        for (const auto& l : p.lines)
            if (l.text.find(needle) != std::string::npos) return true;
    return false;
}

// Number of setCvars entries for `cvar`, and the last value emitted for it ("" when absent).
int cvarCount(const Actions& a, const std::string& cvar) {
    int n = 0;
    for (const auto& c : a.setCvars)
        if (c.first == cvar) ++n;
    return n;
}
std::string cvarValue(const Actions& a, const std::string& cvar) {
    std::string v;
    for (const auto& c : a.setCvars)
        if (c.first == cvar) v = c.second;
    return v;
}

// The engine only honours the emitting module's own cvars: every entry must be a "shooting" setting.
bool onlyOwnCvars(const Harness& h, const Actions& a) {
    for (const auto& c : a.setCvars) {
        const SettingDef* d = h.schema.find(c.first);
        if (!d || d->module != "shooting") return false;
    }
    return true;
}

// What the glue does with Actions::setCvars: set each cvar (validated by the schema), settings re-applied.
bool applyCvars(Harness& h, const Actions& a) {
    bool ok = true;
    for (const auto& c : a.setCvars) ok = h.set(c.first, c.second) && ok;
    return ok;
}

} // namespace

// ---- Realistic dodge timing through the module ------------------------------------------------------------
TEST(shooting_fix_clean_flicks_are_not_uncertain_in_the_drill) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 5);
    FlickScript s; // press at D, IsDodging + engine direction from D+1 (in-game timing)
    s.launch = {2200.f, 0.f, 350.f};
    playFlick(h, w, s);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) {
        CHECK_MSG(h.results[0].title == "Flick avant", h.results[0].title);
        CHECK(h.results[0].skill == "shoot.flick.front");
    }
    CHECK_MSG(h.mod.status().lastFlick == "Flick avant", h.mod.status().lastFlick);

    // A musty with the same timing, requested and performed: no "(?)" either.
    Harness m;
    World mw;
    m.tick(mw.snap());
    m.command("flick", {"musty"});
    runToLive(m, mw);
    mw.ballRides = true;
    runTicks(m, mw, 5);
    FlickScript ms;
    ms.pitchDeg = -70.f;
    ms.dodgeDir = {-1.f, 0.f, 0.f};
    ms.delay = 0.35f;
    ms.launch = {1500.f, 0.f, 900.f};
    playFlick(m, mw, ms);
    CHECK_EQ(m.results.size(), static_cast<size_t>(1));
    if (!m.results.empty()) {
        CHECK_MSG(m.results[0].title == "Musty flick", m.results[0].title);
        CHECK(m.results[0].skill == "shoot.flick.musty");
    }
}

TEST(shooting_fix_unconfirmed_dodge_still_marked_in_the_drill) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"any"});
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 5);
    FlickScript s;
    s.engineReportsDodge = false; // the engine never confirms the dodge
    playFlick(h, w, s);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) CHECK(h.results[0].title.find("(?)") != std::string::npos);
}

TEST(shooting_fix_double_jump_is_not_graded_as_a_flick) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"any"});
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 5);
    // The stick passed the analyzer deadzone, the game made a double jump that pops the ball.
    FlickScript dj;
    dj.doubleJump = true;
    dj.dodgeDir = {0.6f, 0.f, 0.f};
    dj.contact = true;
    dj.launch = {300.f, 0.f, 900.f};
    playFlick(h, w, dj);
    // No graded flick (a "ball lost" result, without record value, may follow once the ball lands).
    bool flickResult = false;
    for (const auto& r : h.results)
        if (r.hasRecordValue && r.skill.rfind("shoot.flick.", 0) == 0) flickResult = true;
    CHECK(!flickResult);
}

// ---- Requestable flicks -----------------------------------------------------------------------------------
TEST(shooting_fix_flick_command_refuses_non_drill_types) {
    Harness h;
    World w;
    h.tick(w.snap());
    for (const char* t : {"diagonal", "side", "lateral"}) {
        h.toasts.clear();
        h.command("flick", {t});
        CHECK_MSG(h.mod.mode() == ShootMode::None, t);
        CHECK_MSG(!h.toasts.empty() && h.toasts.back().kind == ToastKind::Warning, t);
    }
    CHECK_EQ(h.host.requests, 0);
    h.command("flick", {"musty"});
    CHECK(h.mod.mode() == ShootMode::Flick);
    CHECK_EQ(h.mod.status().requestedFlick, static_cast<int>(FlickType::Musty));
    // Refused while a drill runs: the request is unchanged.
    h.command("flick", {"diagonal"});
    CHECK_EQ(h.mod.status().requestedFlick, static_cast<int>(FlickType::Musty));
    CHECK(h.mod.mode() == ShootMode::Flick);
    for (int i = 0; i < kRequestableFlickCount; ++i) {
        h.command("flick", {flickTypeId(static_cast<FlickType>(i))});
        CHECK_EQ(h.mod.status().requestedFlick, i);
    }
}

// ---- Button style -----------------------------------------------------------------------------------------
TEST(shooting_fix_hud_cue_follows_button_style) {
    Harness h;
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"AirRoll", "XboxTypeS_LeftShoulder"}};
    World w;
    h.tick(w.snap());
    h.command("flick", {"musty"});
    runToLive(h, w);
    const std::string xbox = actionPrompt(h.profile, GameAction::Jump, KeyStyle::Xbox) + ", nez en bas";
    const std::string ps = actionPrompt(h.profile, GameAction::Jump, KeyStyle::PlayStation) + ", nez en bas";
    CHECK(xbox != ps);
    CHECK(panelHas(h.draw(viewFor(w)), xbox)); // Auto: Xbox names
    h.settings.profile.buttonStyle = 2; // "Ma config": PlayStation
    const DrawList d = h.draw(viewFor(w));
    CHECK(panelHas(d, ps));
    CHECK(!panelHas(d, xbox));
    // The delayed cue quotes the configured threshold.
    h.set("rlt_sh_delayed_sec", "0.8");
    h.command("flick", {"delayed"});
    runToLive(h, w);
    CHECK(panelHas(h.draw(viewFor(w)), "garde la balle 0,8 s"));
}

// ---- Zone / setup selections ------------------------------------------------------------------------------
TEST(shooting_fix_zone_and_setup_selection_outside_the_drill) {
    Harness h;
    World w;
    h.tick(w.snap());
    CHECK_EQ(h.mod.status().zoneSelection, kZoneRandom);
    h.command("zone", {"tl"});
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK_EQ(h.mod.status().zoneSelection, static_cast<int>(Zone::TopLeft));
    CHECK(!h.toasts.empty() && h.toasts.back().kind == ToastKind::Info); // feedback, not silence
    h.command("setup", {"pass"});
    CHECK_EQ(h.mod.status().setupSelection, static_cast<int>(TargetSetup::Pass));
    // An unrelated settings change keeps the selection made by command.
    h.set("rlt_sh_hud_scale", "1.5");
    CHECK_EQ(h.mod.status().zoneSelection, static_cast<int>(Zone::TopLeft));
    CHECK_EQ(h.mod.status().setupSelection, static_cast<int>(TargetSetup::Pass));
    // "targets" without arguments uses the selection.
    h.command("targets");
    runToLive(h, w);
    CHECK(h.mod.currentZone() == Zone::TopLeft);
    CHECK(h.mod.currentSetup() == TargetSetup::Pass);
    h.command("stop");

    // The cvar (what the tab's zone buttons now set) drives the selection, and the flick target "Zone choisie
    // dans Cibles" follows it.
    h.set("rlt_sh_target_zone", "3");
    CHECK_EQ(h.mod.status().zoneSelection, static_cast<int>(Zone::LowRight));
    h.set("rlt_sh_flick_target", "1");
    h.command("flick", {"any"});
    runToLive(h, w);
    CHECK(h.mod.status().flickZoneActive);
    CHECK_EQ(h.mod.status().flickZone, static_cast<int>(Zone::LowRight));
    h.command("stop");
    h.command("zone", {"center"});
    h.command("flick", {"any"});
    runToLive(h, w);
    CHECK_EQ(h.mod.status().flickZone, static_cast<int>(Zone::Center));
    // Setup cvar change applies too.
    h.set("rlt_sh_target_setup", "4");
    CHECK_EQ(h.mod.status().setupSelection, static_cast<int>(TargetSetup::Roof));
}

// ---- Console commands save the zone / setup choice ---------------------------------------------------------
TEST(shooting_fix_commands_persist_zone_and_setup_cvars) {
    Harness h;
    World w;
    h.tick(w.snap());

    // "zone" outside the drill: selection now, saved through the module's own cvar (choice index).
    Actions a = h.command("zone", {"tl"});
    CHECK(onlyOwnCvars(h, a));
    CHECK_EQ(a.setCvars.size(), size_t{1});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::string("0"));
    CHECK(applyCvars(h, a));
    CHECK_EQ(h.settings.shooting.targetZone, static_cast<int>(Zone::TopLeft));
    CHECK_EQ(h.mod.status().zoneSelection, static_cast<int>(Zone::TopLeft));
    a = h.command("zone", {"random"});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(kZoneRandom));
    CHECK(applyCvars(h, a));
    CHECK_EQ(h.settings.shooting.targetZone, kZoneRandom);

    // "setup".
    a = h.command("setup", {"pass"});
    CHECK(onlyOwnCvars(h, a));
    CHECK_EQ(a.setCvars.size(), size_t{1});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_setup"), std::to_string(static_cast<int>(TargetSetup::Pass)));
    CHECK(applyCvars(h, a));
    CHECK_EQ(h.settings.shooting.targetSetup, static_cast<int>(TargetSetup::Pass));
    CHECK_EQ(h.mod.status().setupSelection, static_cast<int>(TargetSetup::Pass));

    // "targets <zone> <setup>": both saved, the drill starts with them.
    a = h.command("targets", {"br", "roof"});
    CHECK(onlyOwnCvars(h, a));
    CHECK_EQ(a.setCvars.size(), size_t{2});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(static_cast<int>(Zone::LowRight)));
    CHECK_EQ(cvarValue(a, "rlt_sh_target_setup"), std::to_string(static_cast<int>(TargetSetup::Roof)));
    CHECK(h.mod.mode() == ShootMode::Targets);
    CHECK(applyCvars(h, a));
    CHECK_EQ(h.settings.shooting.targetZone, static_cast<int>(Zone::LowRight));
    CHECK_EQ(h.settings.shooting.targetSetup, static_cast<int>(TargetSetup::Roof));
    runToLive(h, w);
    CHECK(h.mod.currentZone() == Zone::LowRight);
    CHECK(h.mod.currentSetup() == TargetSetup::Roof);

    // During the drill: "zone" / "setup" still save (the cvar catches up with the same selection).
    a = h.command("zone", {"center"});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(static_cast<int>(Zone::Center)));
    a.setCvars.clear(); // glue not run yet: a later unrelated change must not undo the command
    h.set("rlt_sh_hud_scale", "1.2");
    CHECK_EQ(h.mod.status().zoneSelection, static_cast<int>(Zone::Center));
    a = h.command("setup", {"rolling"});
    CHECK_EQ(cvarValue(a, "rlt_sh_target_setup"), std::to_string(static_cast<int>(TargetSetup::Rolling)));
    CHECK(applyCvars(h, a));
    CHECK_EQ(h.mod.status().setupSelection, static_cast<int>(TargetSetup::Rolling));
    h.command("stop");

    // Only what the command names is saved: no argument = nothing, zone only = zone only, setup only = setup only.
    a = h.command("targets");
    CHECK(a.setCvars.empty());
    h.command("stop");
    a = h.command("targets", {"tr"});
    CHECK_EQ(cvarCount(a, "rlt_sh_target_zone"), 1);
    CHECK_EQ(cvarCount(a, "rlt_sh_target_setup"), 0);
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(static_cast<int>(Zone::TopRight)));
    h.command("stop");
    a = h.command("targets", {"bouncing"});
    CHECK_EQ(cvarCount(a, "rlt_sh_target_zone"), 0);
    CHECK_EQ(cvarValue(a, "rlt_sh_target_setup"), std::to_string(static_cast<int>(TargetSetup::Bouncing)));
    h.command("stop");
    // Several zones: the last one wins and is saved once.
    a = h.command("targets", {"tl", "bl"});
    CHECK_EQ(cvarCount(a, "rlt_sh_target_zone"), 1);
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(static_cast<int>(Zone::LowLeft)));
    h.command("stop");

    // Bad arguments and unrelated commands save nothing.
    CHECK(h.command("zone", {"nowhere"}).setCvars.empty());
    CHECK(h.command("zone").setCvars.empty());
    CHECK(h.command("setup", {"sideways"}).setCvars.empty());
    CHECK(h.command("setup").setCvars.empty());
    CHECK(h.command("targets", {"tl", "nowhere"}).setCvars.empty());
    CHECK(h.command("flick", {"musty"}).setCvars.empty());
    CHECK(h.command("next").setCvars.empty());
    CHECK(h.command("stop").setCvars.empty());
    CHECK(h.command("power").setCvars.empty());
    CHECK(h.command("stop").setCvars.empty());

    // Saved even where the drill cannot start (like the tab's zone buttons).
    Harness g;
    g.game.worldControlAllowed = false;
    a = g.command("targets", {"center", "pass"});
    CHECK(g.mod.mode() == ShootMode::None);
    CHECK_EQ(cvarValue(a, "rlt_sh_target_zone"), std::to_string(static_cast<int>(Zone::Center)));
    CHECK_EQ(cvarValue(a, "rlt_sh_target_setup"), std::to_string(static_cast<int>(TargetSetup::Pass)));

    // Module disabled: refused, nothing saved.
    Harness d;
    d.set("rlt_sh_enabled", "0");
    CHECK(d.command("zone", {"tl"}).setCvars.empty());
    CHECK(d.command("setup", {"roof"}).setCvars.empty());
    CHECK(d.command("targets", {"tl", "roof"}).setCvars.empty());

    // Persistence: a fresh module loading the saved settings starts with the choice made by command.
    Harness p;
    a = p.command("zone", {"bl"});
    CHECK(applyCvars(p, a));
    a = p.command("setup", {"bouncing"});
    CHECK(applyCvars(p, a));
    ShootingModule fresh;
    fresh.onSettingsChanged(p.settings);
    CHECK_EQ(fresh.status().zoneSelection, static_cast<int>(Zone::LowLeft));
    CHECK_EQ(fresh.status().setupSelection, static_cast<int>(TargetSetup::Bouncing));
}
