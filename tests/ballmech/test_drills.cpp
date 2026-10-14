// Drill tests: activity gating (fake host), ready / launch world actions, scripted inputs, resolution by grades,
// foreign grades, timeouts, landing, lost activity, commands.
#include "core/common/Controls.h"
#include "core/modules/ballmech/Mechanics.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace bmtest;

namespace {
// Renumbers a scenario so it continues right after the harness' latest snapshot.
std::vector<SimTick> continueFrom(const Harness& h, std::vector<SimTick> ticks) {
    if (ticks.empty()) return ticks;
    const int64_t offset = h.latest.frame + 1 - ticks.front().s.frame;
    for (auto& t : ticks) {
        t.s.frame += offset;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    return ticks;
}

// A valid resting state so the module has a snapshot to work with.
void warmUp(Harness& h, int n = 10) {
    Sim sim;
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.ball.pos = {0, 1000, 93};
    sim.step(n);
    h.run(sim.out);
}

// Runs idle ticks with the player pressing the throttle (counts as activity).
void active(Harness& h, int n) {
    SimTick t;
    t.s = h.latest;
    for (int i = 0; i < n; ++i) {
        t.s.frame += 1;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
        t.s.in = Inputs{};
        t.s.in.throttle = 1.f;
        h.tick(t);
    }
}

bool anyToastWarning(const Harness& h) {
    for (const auto& t : h.toasts)
        if (t.kind == ToastKind::Warning) return true;
    return false;
}

void skipReady(Harness& h) {
    const int ready = static_cast<int>(h.settings.ballmech.drillReady * kTickRate) + 2;
    h.idle(ready);
}
} // namespace

TEST(bm_drill_refused_without_world_control) {
    Harness h;
    h.game.worldControlAllowed = false;
    warmUp(h);
    h.command("drill", {"flipreset"});
    CHECK(!h.mod.drillActive());
    CHECK(h.host.owner == nullptr);
    CHECK(anyToastWarning(h));
    Harness r;
    r.host.allow = false;  // the engine refuses (another context)
    warmUp(r);
    r.command("drill", {"pinch"});
    CHECK(!r.mod.drillActive());
    CHECK_EQ(r.host.requests, 1);
    Harness u;
    warmUp(u);
    u.command("drill", {"unknown"});
    CHECK(!u.mod.drillActive());
    CHECK(anyToastWarning(u));
    Harness d;
    d.set("rlt_bm_enabled", "0");
    warmUp(d);
    d.command("drill", {"flick"});
    CHECK(!d.mod.drillActive());
    // Workshop map with exercises allowed: the setups assume the standard arena, so they are refused.
    Harness c;
    c.game.mode = GameMode::Workshop;
    c.game.isCustomMap = true;
    warmUp(c);
    c.command("drill", {"flick"});
    CHECK(!c.mod.drillActive());
    CHECK(c.host.owner == nullptr);
    CHECK(anyToastWarning(c));
}

TEST(bm_drill_ready_then_launch) {
    Harness h;
    warmUp(h);
    h.command("drill", {"powershot"});
    CHECK(h.mod.drillActive());
    CHECK(h.host.owner == &h.mod);
    CHECK(h.host.lastName.find("Frappe puissante") != std::string::npos);
    h.idle(1);
    // Ready: car and ball held in place.
    CHECK(h.last.freezeCar);
    CHECK(h.last.setCar.has_value() && h.last.setBall.has_value());
    if (h.last.setCar) CHECK(length(h.last.setCar->vel) < 1e-3f);
    if (h.last.setCar) CHECK(h.last.setCar->boost.has_value());
    // Launch after the ready time: velocities applied, GO message.
    bool launched = false;
    for (int i = 0; i < 200 && !launched; ++i) {
        h.idle(1);
        if (h.last.setCar && !h.last.freezeCar && length(h.last.setCar->vel) > 1.f) {
            launched = true;
            CHECK(h.last.goMessage);
            CHECK(h.last.setBall.has_value());
            CHECK(h.latest.time >= 0.f);
        }
    }
    CHECK(launched);
    h.idle(1);
    CHECK(!h.last.setCar.has_value());  // no more placement during the attempt
    CHECK(!h.last.freezeCar);
    const auto u = h.mod.uiState();
    CHECK(u.drill.active);
    CHECK(u.drill.mechId == "powershot");
    CHECK(u.drill.timeLeft > 0.f);
    // The drill panel is drawn in the exercise zone.
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
    ViewContext v;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    h.mod.draw(v, dc, dl);
    bool drillPanel = false;
    for (const auto& p : dl.panels)
        if (p.title == "Exercice : Frappe puissante (niveau 1/10)") {
            drillPanel = true;
            CHECK_NEAR(p.pos.y, 0.12f * 1080.f, 0.5f);
        }
    CHECK(drillPanel);
}

TEST(bm_drill_flipreset_burns_the_flip) {
    Harness h;
    warmUp(h);
    h.command("drill", {"flipreset"});
    std::vector<int> jumps;  // -1 = no override, 0/1 = jump value
    for (int i = 0; i < 160; ++i) {
        h.idle(1);
        if (h.last.overrideInputs) jumps.push_back(h.last.overrideInputs->jump ? 1 : 0);
        else if (!jumps.empty()) jumps.push_back(-1);
        if (jumps.size() >= 6) break;
    }
    CHECK(jumps.size() >= 5);
    if (jumps.size() >= 5) {
        CHECK_EQ(jumps[0], 0);
        CHECK_EQ(jumps[1], 1);
        CHECK_EQ(jumps[2], 1);
        CHECK_EQ(jumps[3], 0);
        CHECK_EQ(jumps[4], -1);
    }
}

TEST(bm_drill_success_levels_up) {
    Harness h;
    warmUp(h);
    const float level0 = h.mod.drillLevel("powershot");
    h.command("drill", {"powershot"});
    skipReady(h);
    h.run(continueFrom(h, powerShotScenario(PS::FlipShot)));
    h.idle(5);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.powershot");
        CHECK(h.results[0].success);
    }
    const auto u = h.mod.uiState();
    CHECK_EQ(u.drill.attempts, 1);
    CHECK_EQ(u.drill.successes, 1);
    CHECK(u.drill.lastSuccess);
    CHECK(h.mod.drillLevel("powershot") > level0);
    // After the pause, a new attempt is prepared (car frozen again).
    h.idle(static_cast<int>(h.settings.ballmech.drillPause * kTickRate) + 3);
    CHECK(h.last.freezeCar);
    CHECK(h.mod.uiState().drill.phase == "Prépare-toi");
}

TEST(bm_drill_foreign_grade_is_a_failure) {
    Harness h;
    warmUp(h);
    const float level0 = h.mod.drillLevel("flipreset");
    h.command("drill", {"flipreset"});
    skipReady(h);
    h.idle(8);  // let the scripted double jump finish
    h.run(continueFrom(h, aerialScenario(AE::AirRoll)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);  // one result per attempt: the drill's
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.flipreset");
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.fr_no_reset"));
    }
    CHECK_EQ(h.countSkill("ball.airroll"), 0);
    CHECK(h.mod.drillLevel("flipreset") < level0 || level0 == 0.f);
    CHECK(h.mod.uiState().drill.lastVerdict.find("Air roll shot") != std::string::npos);
}

TEST(bm_drill_matching_grade_resolves_with_drill_criteria) {
    // Musty drill, plain backflip: reported under the drill's skill, failed, with the musty tip.
    Harness h;
    warmUp(h);
    h.command("drill", {"musty"});
    skipReady(h);
    h.run(continueFrom(h, flickScenario(FK::MustyNoseLevel)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.musty");
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.fl_musty_nose_not_down"));
        CHECK(h.results[0].title == "Flick arrière");
    }
}

TEST(bm_drill_timeouts) {
    Harness h;
    warmUp(h);
    h.command("drill", {"aerial"});
    skipReady(h);
    const float level0 = h.mod.drillLevel("aerial");
    // Level 1 gives more time than the aerial's 6 s: wait for the real time left.
    CHECK(h.mod.uiState().drill.timeLeft > 6.5f);
    active(h, static_cast<int>((h.mod.uiState().drill.timeLeft + 0.5f) * kTickRate));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.aerial");
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.drill_timeout"));
        CHECK_EQ(h.results[0].score, 0.f);
    }
    CHECK(h.mod.drillLevel("aerial") <= level0);

    // Idle player: silent restarts, then the drill pauses itself after 3 attempts.
    Harness idle;
    warmUp(idle);
    idle.command("drill", {"aerial"});
    idle.idle(static_cast<int>(40.f * kTickRate));
    CHECK(idle.results.empty());
    CHECK(!idle.mod.drillActive());
    CHECK(idle.host.owner == nullptr);
    CHECK(idle.host.releases >= 1);
}

TEST(bm_drill_landing_fails_airborne_drill) {
    Harness h;
    warmUp(h);
    h.command("drill", {"airroll"});
    skipReady(h);
    Sim sim;
    setAir(sim.s.car);
    sim.s.car.pos = {0, 1300, 500};
    sim.s.ball.pos = {0, 3000, 1000};
    sim.step(40);
    sim.step(150, [](Snapshot& x, int) {
        setGround(x.car);
        x.car.pos.z = bm::arena::kCarRestZ;
        x.car.vel = {};
    });
    h.run(continueFrom(h, sim.out));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.airroll");
        CHECK(hasTip(h.results[0], "bm.drill_landed"));
    }
}

TEST(bm_drill_flick_ball_lost) {
    Harness h;
    warmUp(h);
    h.command("drill", {"flick"});
    skipReady(h);
    Sim sim;
    sim.s.car.pos = {0, 0, 17};
    sim.s.car.vel = {0, 500, 0};
    sim.s.ball.pos = {0, 600, 93};
    sim.s.ball.vel = {0, 900, 0};
    sim.step(150, [](Snapshot& x, int) { x.in.throttle = 1.f; });
    h.run(continueFrom(h, sim.out));
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.drill_ball_lost"));
}

TEST(bm_drill_activity_lost_or_stopped) {
    Harness h;
    warmUp(h);
    h.command("drill", {"pinch"});
    skipReady(h);
    OtherModule other;
    other.attach(&h.host);
    CHECK(other.take());
    CHECK(!h.mod.drillActive());  // onActivityStopped
    h.idle(3);
    CHECK(!h.last.setCar && !h.last.freezeCar && !h.last.overrideInputs);

    Harness s;
    warmUp(s);
    s.command("drill", {"pinch"});
    s.idle(3);
    s.command("stop");
    CHECK(!s.mod.drillActive());
    CHECK(s.host.owner == nullptr);
    CHECK(s.host.releases >= 1);
    s.idle(3);
    CHECK(!s.last.setCar && !s.last.freezeCar);

    Harness w;
    warmUp(w);
    w.command("drill", {"pinch"});
    w.idle(3);
    w.game.worldControlAllowed = false;  // e.g. the player loaded a workshop map
    w.idle(2);
    CHECK(!w.mod.drillActive());
    CHECK(!w.last.setCar && !w.last.freezeCar);
}

TEST(bm_drill_reset_shot_and_next_restart_attempt) {
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling"});
    skipReady(h);
    h.idle(5);
    CHECK(h.mod.uiState().drill.phase == "À toi !");
    h.event(GameEventKind::ResetShot);
    h.idle(1);
    CHECK(h.last.freezeCar);
    CHECK(h.results.empty());  // not counted as a failure
    skipReady(h);
    h.command("next");
    h.idle(1);
    CHECK(h.last.freezeCar);
}

TEST(bm_drill_commands) {
    Harness h;
    warmUp(h);
    h.command("level", {"flick", "80"});
    CHECK_NEAR(h.mod.drillLevel("flick"), 0.8f, 1e-4f);
    h.command("level", {"flick", "0.25"});
    CHECK_NEAR(h.mod.drillLevel("flick"), 0.25f, 1e-4f);
    h.command("level", {"flick", "abc"});
    CHECK_NEAR(h.mod.drillLevel("flick"), 0.25f, 1e-4f);
    h.command("level", {"flick", "1"});  // percent
    CHECK_NEAR(h.mod.drillLevel("flick"), 0.01f, 1e-4f);
    h.command("level", {"flick", "150"});  // clamped
    CHECK_NEAR(h.mod.drillLevel("flick"), 1.f, 1e-4f);
    h.command("level", {"flick", "25"});
    h.command("variant", {"90"});
    CHECK(h.mod.uiState().flickVariant == "90");
    h.command("variant", {"nonsense"});
    CHECK(h.mod.uiState().flickVariant == "90");
    h.command("drill", {"flick", "any"});
    CHECK(h.mod.uiState().flickVariant.empty());
    CHECK(h.mod.drillActive());
    h.command("drill", {"flick", "45"});
    CHECK(h.mod.uiState().flickVariant == "45");
    // A wrong flick variant inside the drill is reported under ball.flick and fails.
    skipReady(h);
    h.run(continueFrom(h, flickScenario(FK::F90)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.flick");
        CHECK(!h.results[0].success);
    }
}

TEST(bm_drill_orange_team_setup) {
    Harness h;
    Sim sim;
    sim.s.car.team = 1;
    sim.s.ball.pos = {0, -1000, 93};
    sim.step(10);
    h.run(sim.out);
    h.command("drill", {"powershot"});
    h.idle(1);
    CHECK(h.last.setBall.has_value() && h.last.setCar.has_value());
    if (h.last.setBall && h.last.setCar) {
        CHECK(h.last.setBall->pos.y < 0.f);  // orange attacks -Y: the ball is in the orange attacking half
        CHECK(basisFromRot(h.last.setCar->rot).fwd.y < -0.9f);
    }
}

TEST(bm_drill_not_resolved_by_earlier_play) {
    Harness h;
    warmUp(h);
    // Free play: a power shot is graded as usual.
    h.run(continueFrom(h, powerShotScenario(PS::FlipShot)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(h.results[0].skill == "ball.powershot");
    // Start the flip reset drill right after: nothing resolves it before the player plays.
    h.command("drill", {"flipreset"});
    skipReady(h);
    h.idle(30);
    CHECK_EQ(h.results.size(), 1u);
    CHECK_EQ(h.mod.uiState().drill.attempts, 0);
}

TEST(bm_drill_doubletap_read_mode) {
    // Level 0: the drill sends the ball to the backboard itself; the player only reads the rebound.
    Harness h;
    warmUp(h);
    h.command("level", {"doubletap", "0"});
    h.command("drill", {"doubletap"});
    skipReady(h);
    // Continuous flight (no teleport): ball already heading to the backboard, the car follows and meets the rebound.
    Sim sim;
    setAir(sim.s.car);
    orient(sim.s.car, PI / 2, 0.3f, 0);
    sim.s.ball.pos = {0, 3700, 800};
    sim.s.ball.vel = {0, 2400, 500};
    sim.s.car.pos = {0, 3000, 700};
    sim.s.car.vel = {0, 1300, 380};
    sim.ballGravity = true;
    bool bounced = false;
    for (int i = 0; i < 200 && !bounced; ++i) {
        sim.step(1, [&](Snapshot& x, int) {
            if (x.ball.pos.y >= bm::arena::kBackWallY - x.ball.radius) {
                x.ball.vel.y = -0.6f * x.ball.vel.y;
                bounced = true;
            }
        });
    }
    for (int i = 0; i < 120 && distance(sim.s.car.pos, sim.s.ball.pos) > 160.f; ++i) sim.step(1);
    sim.hit({0, 1800, -1100});
    sim.step(30);
    const auto seq = sim.out;
    h.run(continueFrom(h, seq));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.doubletap");
        CHECK(h.results[0].title == "Double tap (lecture du rebond)");
        CHECK(h.results[0].success);
    }
    CHECK_EQ(h.mod.uiState().drill.successes, 1);
}

TEST(bm_drill_flick_success) {
    Harness h;
    warmUp(h);
    h.command("level", {"flick", "20"});
    h.command("drill", {"flick", "any"});
    skipReady(h);
    h.run(continueFrom(h, flickScenario(FK::F45)));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.flick");
        CHECK(h.results[0].title == "Flick en diagonale");  // no degrees in the HUD title
        CHECK(h.results[0].success);
        CHECK(h.results[0].valueForRecord > 1500.f);  // ball speed after the flick
    }
    const auto u = h.mod.uiState();
    CHECK(u.hardestFlick > 1500.f);
    CHECK(h.mod.drillLevel("flick") > 0.2f);
}

TEST(bm_drill_prompts_use_button_style) {
    // PlayStation players see their own button names in the drill instructions (Settings::profile.buttonStyle).
    Harness h;
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{"Jump", "XboxTypeS_A"}};
    h.settings.profile.buttonStyle = 2;
    h.mod.onSettingsChanged(h.settings);
    warmUp(h);
    h.command("drill", {"flipreset"});
    CHECK(h.mod.drillActive());
    const std::string ps = actionKeyLabel(h.profile, GameAction::Jump, KeyStyle::PlayStation);
    CHECK(!ps.empty());
    bool found = false;
    for (const auto& t : h.toasts) found = found || t.text.find("le saut (" + ps + ")") != std::string::npos;
    CHECK(found);
    CHECK(h.mod.uiState().keyStyle == KeyStyle::PlayStation);
}

TEST(bm_unplayed_old_save_starts_at_niveau_1) {
    // The previous version saved every drill at its old start level (0.15) even when never played.
    Store st;
    st.setFloat("ballmech.drill.flick.level", 0.15f);
    st.setInt("ballmech.drill.flick.attempts", 0);
    st.setFloat("ballmech.drill.pinch.level", 0.5f);
    st.setInt("ballmech.drill.pinch.attempts", 0);
    st.setInt("ballmech.drill.pinch.chosen", 1);  // set with the - / + buttons: kept
    BallMechModule m;
    m.load(st);
    CHECK_NEAR(m.drillLevel("flick"), 0.f, 1e-6f);
    for (const auto& v : m.uiState().mechs)
        if (v.id == "flick") CHECK(v.levelNumber == 1);
    CHECK_NEAR(m.drillLevel("pinch"), 0.5f, 1e-6f);
}

TEST(bm_drill_beginner_progression) {
    // New player: level 1, more time than the mechanic's base time.
    Harness h;
    warmUp(h);
    CHECK_NEAR(h.mod.drillLevel("flipreset"), 0.f, 1e-6f);
    h.command("drill", {"flipreset"});
    skipReady(h);
    CHECK(h.mod.uiState().drill.levelNumber == 1);
    CHECK(h.mod.uiState().drill.timeLeft > bm::mechInfo(bm::Mech::FlipReset).drillTimeout);
    CHECK(h.mod.uiState().drill.action.find("4 roues") != std::string::npos);
    bool note = false;  // the start message says what level 1 asks
    for (const auto& t : h.toasts)
        note = note || t.text.find("Niveau 1/10 : poser les 4 roues sur la balle suffit") != std::string::npos;
    CHECK(note);
    h.idle(8);  // let the scripted double jump finish
    // Typical first try: the wheels touch the ball, the reset is not used. At level 1 that is the goal.
    h.run(continueFrom(h, flipResetScenario(FR::Wasted)));
    h.idle(200);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.flipreset");
        CHECK(h.results[0].success);
    }
    CHECK_EQ(h.mod.uiState().drill.successes, 1);
    CHECK(h.mod.uiState().drill.lastVerdict.rfind("Réussi", 0) == 0);

    // Level 10: the same attempt fails, the reset has to be used.
    Harness x;
    warmUp(x);
    x.command("level", {"flipreset", "100"});
    x.command("drill", {"flipreset"});
    skipReady(x);
    x.idle(8);
    x.run(continueFrom(x, flipResetScenario(FR::Wasted)));
    x.idle(200);
    CHECK_EQ(x.results.size(), 1u);
    if (!x.results.empty()) {
        CHECK(!x.results[0].success);
        CHECK(hasTip(x.results[0], "bm.drill_need_convert"));
    }
    CHECK(x.mod.uiState().drill.lastVerdict.rfind("Raté", 0) == 0);
    CHECK(!x.mod.uiState().drill.tip.empty());

    // A soft, off-target backflip flick: fine for the flick drill at level 1, too weak at level 10.
    Harness f;
    warmUp(f);
    f.command("level", {"flick", "0"});
    f.command("drill", {"flick", "any"});
    skipReady(f);
    f.run(continueFrom(f, flickScenario(FK::MustyNoseLevel)));
    f.idle(10);
    CHECK_EQ(f.results.size(), 1u);
    if (!f.results.empty()) CHECK(f.results[0].success);
    Harness g;
    warmUp(g);
    g.command("level", {"flick", "100"});
    g.command("drill", {"flick", "any"});
    skipReady(g);
    g.run(continueFrom(g, flickScenario(FK::MustyNoseLevel)));
    g.idle(10);
    CHECK_EQ(g.results.size(), 1u);
    if (!g.results.empty()) {
        CHECK(!g.results[0].success);
        CHECK(hasTip(g.results[0], "bm.drill_need_power"));
    }
}
