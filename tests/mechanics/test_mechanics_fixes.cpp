// Module-level regression tests for the review fixes: game speed restore under the engine's real rules (filter
// on non-owners, stop before ContextChanged, handover), focus/drill of a disabled family, slow motion turned on
// during a focus, pending kickoff speedflip on reset, drills that need the ball, HUD wording.
#include <memory>
#include <string>

#include "tests/mechanics/MechRig.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

namespace {

// SpeedFlipTrainer 26-degree bot timeline (jump at 59, held 11 ticks, diagonal dodge, cancel after 5 ticks).
Script kickoffSpeedflip(float angleDeg) {
    auto passedInverted = std::make_shared<bool>(false);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        const int J = 59, JD = 11, D = J + JD + 1, cancelAfter = 5;
        if (t < J) {
            in.steer = 0.03f;
        } else if (t < J + JD) {
            in.jump = true;
            in.steer = in.yaw = 0.25f * sgn(-angleDeg);
        } else if (t < D) {
        } else if (t < D + cancelAfter) {
            in.jump = t < D + 5;
            const float a = angleDeg * kDegToRad;
            in.yaw = std::sin(a);
            in.pitch = -std::cos(a);
        } else {
            in.pitch = 1.f;
            if (c.up().z < -0.3f) *passedInverted = true;
            in.roll = sgn(angleDeg);
            if (*passedInverted && c.up().z > 0.3f) {
                Inputs pd;
                orientPD(c, Vec3{1.f, 0.f, 0.f}, pd);
                in.roll = pd.roll;
                in.yaw = pd.yaw;
                in.pitch = t < D + 110 ? 1.f : pd.pitch;
            }
            if (c.wheelCount() >= 3) {
                in.roll = 0.f;
                in.pitch = 0.f;
                in.yaw = 0.f;
            }
        }
        return in;
    };
}

int countSpeed(const std::vector<float>& speeds, float v) {
    int n = 0;
    for (float s : speeds) n += std::fabs(s - v) < 1e-6f ? 1 : 0;
    return n;
}

} // namespace

// ---- Game speed restore under the engine's rules ---------------------------------------------------------
TEST(mechanics_speed_external_stop_engine_restores) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    CHECK(rig.speeds.size() == 1 && rig.speeds.back() == 0.5f);
    CHECK_NEAR(rig.mod.status().gameSpeed, 0.5f, 1e-6f);
    CHECK_NEAR(rig.host.gameSpeed, 0.5f, 1e-6f);

    // Home "Stop" button: Engine::stopActivity clears the owner, restores the speed, calls onActivityStopped.
    rig.host.stopActivity();
    CHECK(!rig.mod.drillActive());
    CHECK(rig.host.engineRestores == 1);
    CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
    rig.run(30, idle());
    CHECK(rig.speeds.size() == 1);   // nothing to compensate: the module sends no speed of its own
    CHECK(!rig.raw.setGameSpeed);
    CHECK(rig.host.requests == 1);   // and never takes the activity back
    CHECK_NEAR(rig.mod.appliedGameSpeed(), 1.f, 1e-6f);
    MechanicsModule::Status st = rig.mod.status();
    CHECK_NEAR(st.gameSpeed, 1.f, 1e-6f);
    CHECK(!st.speedWaiting);

    // Normal speed chosen: the next drill has nothing to send (the game already runs at 1).
    rig.set("rlt_me_slowmo", "1");
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    CHECK(rig.speeds.size() == 1);
    rig.command("stop");
    rig.run(5, idle());
    CHECK(rig.speeds.size() == 1);
    CHECK(!rig.host.ownsActivity(&rig.mod));
    // Slow motion again: the module does not believe the game is still slowed down, it sends its speed.
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    CHECK(rig.speeds.size() == 2 && rig.speeds.back() == 0.5f);

    // A drill that never touched the speed does not send one (a speed the player set by hand stays).
    Rig clean;
    clean.command("drill", {"halfflip"});
    clean.run(10, idle());
    clean.command("stop");
    clean.run(3, idle());
    CHECK(clean.speeds.empty());
    CHECK(clean.host.engineRestores == 0);
}

TEST(mechanics_speed_normal_stop_restores_itself) {
    // Its own normal stop: the module routes 1 as the owner, then releases; the engine has nothing left to do.
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("drill", {"wavedash"});
    rig.tick(Inputs{});
    Actions a = rig.command("stop");
    CHECK(a.setGameSpeed.has_value() && *a.setGameSpeed == 1.f);
    CHECK(rig.host.ownsActivity(&rig.mod));
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.engineRestores == 0);
    CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
}

TEST(mechanics_speed_restore_command) {
    Rig rig;
    // No exercise runs: takes the activity for one callback, restores, gives it back next tick.
    Actions a = rig.command("restore_speed");
    CHECK(a.setGameSpeed.has_value() && *a.setGameSpeed == 1.f);  // routed: the module owned the activity
    CHECK(rig.toastContains("Vitesse du jeu remise"));
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(!rig.raw.setCar && !rig.raw.freezeCar);
    // Refused where world control is not allowed.
    Rig ws;
    ws.game.worldControlAllowed = false;
    Actions b = ws.command("restore_speed");
    CHECK(!b.setGameSpeed);
    CHECK(ws.host.requests == 0);
    CHECK(ws.toastContains("freeplay"));
    // While its own drill runs, the slow motion follows the setting: the command explains it.
    Rig own;
    own.set("rlt_me_slowmo", "0.5");
    own.command("drill", {"wavedash"});
    own.tick(Inputs{});
    own.command("restore_speed");
    CHECK(own.mod.drillActive());
    CHECK(own.toastContains("Ralenti"));
}

TEST(mechanics_speed_restore_waits_for_other_drill) {
    Rig rig;
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    const int requests = rig.host.requests;
    Actions a = rig.command("restore_speed");
    CHECK(!a.setGameSpeed);
    CHECK(rig.host.requests == requests);  // the other exercise is not stopped for this
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.mod.speedWaiting() && rig.mod.status().speedWaiting);
    CHECK(rig.toastContains("quand il se terminera"));
    rig.run(20, idle());
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.host.requests == requests);
    CHECK(rig.speeds.empty());
    // The other exercise ends: the wish is applied on the next tick, then the activity is given back.
    other.stop();
    Actions t = rig.tick(Inputs{});
    CHECK(t.setGameSpeed.has_value() && *t.setGameSpeed == 1.f);
    CHECK(!rig.mod.speedWaiting());
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    rig.run(5, idle());
    CHECK(rig.speeds.size() == 1);  // applied once

    // "stop" and a map change drop the wish.
    for (int variant = 0; variant < 2; ++variant) {
        Rig w;
        OtherModule o;
        o.attach(&w.host);
        CHECK(o.start("Kickoff"));
        w.command("restore_speed");
        CHECK(w.mod.speedWaiting());
        if (variant == 0) w.command("stop");
        else w.setContext(GameMode::Freeplay, "Stadium_P", true);
        CHECK(!w.mod.speedWaiting());
        o.stop();
        w.run(5, idle());
        CHECK(w.speeds.empty());
        CHECK(!w.host.ownsActivity(&w.mod));
    }
}

TEST(mechanics_speed_leaving_freeplay_and_map_unloaded) {
    {
        // Freeplay -> workshop: Engine::setContext stops the activity (and restores the speed) BEFORE ContextChanged.
        Rig rig;
        rig.set("rlt_me_slowmo", "0.5");
        rig.command("drill", {"powerslide"});
        rig.tick(Inputs{});
        Actions a = rig.setContext(GameMode::Workshop, "Obstacle_course", false);
        CHECK(rig.host.stops == 1);
        CHECK(rig.host.engineRestores == 1);
        CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
        CHECK(!rig.mod.drillActive());
        CHECK(!a.setGameSpeed);
        rig.run(10, idle());
        CHECK(rig.speeds.size() == 1);
        CHECK(rig.host.requests == 1);  // never tries to take the activity back by itself
        // Back in freeplay: the next drill sends its speed, then 1 at its own stop.
        rig.setContext(GameMode::Freeplay, "Park_P", true);
        rig.command("drill", {"powerslide"});
        rig.tick(Inputs{});
        CHECK(rig.speeds.size() == 2 && rig.speeds.back() == 0.5f);
        rig.command("stop");
        CHECK(rig.speeds.size() == 3 && rig.speeds.back() == 1.f);
    }
    {
        Rig rig;
        rig.set("rlt_me_slowmo", "0.25");
        rig.command("drill", {"stall"});
        rig.tick(Inputs{});
        rig.mapUnloaded();
        CHECK(!rig.mod.drillActive());
        CHECK(rig.host.engineRestores == 1);
        CHECK(countSpeed(rig.speeds, 1.f) == 0);  // the module itself sends nothing
        CHECK_NEAR(rig.mod.appliedGameSpeed(), 1.f, 1e-6f);
    }
}

TEST(mechanics_master_switch_restores_as_owner) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("drill", {"wavedash"});
    rig.tick(Inputs{});
    rig.set("rlt_me_enabled", "0");
    Actions a = rig.tick(Inputs{});
    CHECK(a.setGameSpeed.has_value() && *a.setGameSpeed == 1.f);  // still the owner during this tick: routed
    CHECK(!rig.mod.drillActive());
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.engineRestores == 0);
}

// ---- Focus / drill of a family whose toggle is off ------------------------------------------------------
TEST(mechanics_focus_and_drill_ignore_family_toggle) {
    {
        Rig rig;
        rig.set("rlt_me_wavedash", "0");
        rig.command("drill", {"wavedash"});
        rig.tick(Inputs{});
        rig.run(150, dropWavedash());
        const AttemptResult* r = rig.result("mech.wavedash");
        CHECK_MSG(r != nullptr, "the drill asked for wavedashes: graded even with the free-play toggle off");
        if (r) CHECK(r->success);
        CHECK(rig.mod.status().drillAttempts == 1);
    }
    {
        Rig rig;
        rig.set("rlt_me_wavedash", "0");
        rig.command("focus", {"wavedash"});
        rig.sim.vel = {1000.f, 0.f, 0.f};
        rig.run(160, groundWavedash());
        CHECK(rig.count("mech.wavedash") == 1);
        rig.command("focus", {"off"});
        rig.sim = MiniSim{};
        rig.sim.vel = {1000.f, 0.f, 0.f};
        rig.run(200, groundWavedash());
        CHECK(rig.count("mech.wavedash") == 1);  // free play again: the toggle applies
    }
    {
        // Focus on powerslide in a rings map: the map filter does not hide what the player asked for.
        Rig rig;
        rig.game.category = MapCategory::Rings;
        rig.command("focus", {"powerslide"});
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
        CHECK(rig.count("mech.powerslide") == 1);
    }
}

// ---- Slow motion turned on during a focus ---------------------------------------------------------------
TEST(mechanics_slowmo_after_focus_requests_once) {
    Rig rig;
    rig.command("focus", {"stall"});
    CHECK(rig.host.requests == 0);
    rig.set("rlt_me_slowmo", "0.5");
    rig.run(3, idle());
    CHECK(rig.host.requests == 1);
    CHECK(rig.host.ownsActivity(&rig.mod));
    CHECK(rig.speeds.size() == 1 && rig.speeds.back() == 0.5f);
    CHECK_NEAR(rig.mod.status().gameSpeed, 0.5f, 1e-6f);
    // Moving the slider while owning: the new speed follows, no new request.
    rig.set("rlt_me_slowmo", "0.75");
    rig.run(2, idle());
    CHECK(rig.host.requests == 1);
    CHECK(!rig.speeds.empty() && rig.speeds.back() == 0.75f);

    // Another module takes the activity: the focus stays, the module never takes it back on its own...
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    CHECK(rig.mod.focus() == Focus::Stall);
    CHECK(rig.host.engineRestores == 1);  // the engine restored the focus slow motion
    rig.run(20, idle());
    rig.set("rlt_me_hud_x", "0.02");  // ...not when an unrelated setting changes...
    rig.run(5, idle());
    rig.set("rlt_me_slowmo", "0.5");  // ...nor when the slider moves (that would stop the other drill): it waits
    rig.run(5, idle());
    CHECK(rig.host.requests == 2);  // only the other module's request
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.mod.status().speedWaiting);
    rig.command("focus", {"stall"});  // an explicit focus command does not steal it either
    rig.run(2, idle());
    CHECK(rig.host.requests == 2);
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.toastContains("quand il se terminera"));
    // The other exercise ends: the slow motion wish is applied.
    other.stop();
    rig.run(2, idle());
    CHECK(rig.host.ownsActivity(&rig.mod));
    CHECK(!rig.mod.speedWaiting());
    CHECK(!rig.speeds.empty() && rig.speeds.back() == 0.5f);

    // Slow motion back to 1 while owning: the activity is given back after the restore.
    Rig back;
    back.command("focus", {"stall"});
    back.set("rlt_me_slowmo", "0.5");
    back.run(2, idle());
    back.set("rlt_me_slowmo", "1");
    back.run(3, idle());
    CHECK(!back.speeds.empty() && back.speeds.back() == 1.f);
    CHECK(!back.host.ownsActivity(&back.mod));

    // No world control: the focus stays without slow motion, with a toast, and no request.
    Rig ws;
    ws.game.worldControlAllowed = false;
    ws.command("focus", {"stall"});
    ws.set("rlt_me_slowmo", "0.5");
    ws.run(3, idle());
    CHECK(ws.host.requests == 0);
    CHECK(ws.toastContains("Ralenti indisponible"));
    CHECK(ws.speeds.empty());
}

// ---- Kickoff speedflip waiting for the ball, then reset ---------------------------------------------------
TEST(mechanics_reset_shot_grades_pending_kickoff_speedflip) {
    Rig rig;
    rig.ballGame = {0.f, 0.f, 92.75f};
    rig.sim.pos = {-4600.f, 0.f, k::kCarRestZ};  // far spawn: lands well before the ball
    rig.run(20, idle());                          // still car at a kickoff spot: kickoff armed
    rig.run(240, kickoffSpeedflip(-26.f));
    CHECK(!rig.touched);
    CHECK(rig.count("mech.speedflip") == 0);  // landed, waiting for the touch
    rig.event(GameEventKind::ResetShot);      // missed it, reset straight away
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r != nullptr, "the pending kickoff speedflip was dropped by the reset");
    if (r) {
        bool firstJump = false, hold = false;
        for (const auto& m : r->metrics) {
            firstJump = firstJump || m.key == "kickoffFirstJumpTicks";
            hold = hold || m.key == "firstJumpHoldTicks";
            CHECK(m.key != "timeToBall");  // never reached the ball: no time shown
        }
        CHECK(firstJump && hold);
    }
    CHECK(rig.mod.stats().get(Mech::SpeedFlip).attempts == 1);
    rig.run(400, idle());
    CHECK(rig.count("mech.speedflip") == 1);  // not reported a second time
}

// ---- Drills that need the ball ------------------------------------------------------------------------
TEST(mechanics_speedflip_drill_needs_the_ball) {
    Rig rig;  // kickoff step of the drill (level 5)
    Store st;
    st.setFloat("mechanics.level.speedflip.level", 0.45f);
    rig.mod.load(st);
    rig.command("drill", {"speedflip"});
    rig.tick(Inputs{});
    for (int i = 0; i < 125; ++i) {
        Inputs in;
        in.boost = true;
        in.throttle = 1.f;
        rig.tick(in);
    }
    // A clean speedflip that flies away from the ball (the car is turned before the jump).
    rig.sim.orient(Vec3{-1.f, 0.f, 0.f}, Vec3{0.f, 0.f, 1.f});
    auto script = kickoffSpeedflip(-26.f);
    for (int i = 0; i < 5 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r != nullptr, "no speedflip graded in the drill");
    if (!r) return;
    CHECK(!rig.touched);  // graded at the time limit, before the automatic reset
    CHECK(!r->success);
    CHECK(!r->tips.empty() && r->tips.front().code == "me.drill_no_touch");
    CHECK(rig.mod.status().drillAttempts == 1 && rig.mod.status().drillSuccesses == 0);
}

namespace {
// Half-flip of the drill (back dodge, cancel after 24 ticks, air roll once inverted), then drive (or not) to the ball.
Script drillHalfFlip(bool driveToBall) {
    auto inverted = std::make_shared<bool>(false);
    auto landed = std::make_shared<int>(-1);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = -1.f;
        const int J = 10, D = 20;
        if (t >= J && t < J + 4) in.jump = true;
        if (t >= D && t < D + 3) {
            in.jump = true;
            in.pitch = 1.f;
        }
        if (t >= D + 3) {
            const bool cancelling = t >= D + 24;
            in.pitch = cancelling ? -1.f : 1.f;
            if (cancelling) {
                if (c.up().z < 0.f) *inverted = true;
                if (*inverted) in.roll = c.up().z > 0.6f ? rollToLevel(c) : 1.f;
            }
            if (t > D + 10 && c.wheelCount() >= 3 && *landed < 0) *landed = t;
            if (*landed >= 0) {
                in.pitch = 0.f;
                in.roll = 0.f;
                in.throttle = driveToBall ? 1.f : -1.f;  // brake / reverse away from the ball
                in.boost = driveToBall;
            }
        }
        return in;
    };
}
} // namespace

TEST(mechanics_halfflip_drill_waits_for_the_ball) {
    {
        Rig rig;
        rig.command("drill", {"halfflip"});
        rig.tick(Inputs{});
        auto script = drillHalfFlip(true);
        int t = 0;
        for (; t < 400 && rig.results.empty(); ++t) {
            rig.tick(script(t, rig.sim));
            if (rig.results.empty()) CHECK(!rig.touched);  // graded at the landing, held until the ball
        }
        const AttemptResult* r = rig.result("mech.halfflip");
        CHECK_MSG(r != nullptr, "no half-flip graded in the drill");
        if (r) {
            CHECK(rig.touched);
            CHECK(r->success);
            bool ttb = false;
            for (const auto& m : r->metrics) ttb = ttb || (m.key == "timeToBall" && m.value > 0.5f && m.value < 3.5f);
            CHECK(ttb);
        }
        CHECK(rig.mod.status().drillSuccesses == 1);
    }
    {
        Rig rig;  // same half-flip, then the car never reaches the ball: a miss after 2.5 s
        rig.command("drill", {"halfflip"});
        rig.tick(Inputs{});
        auto script = drillHalfFlip(false);
        for (int t = 0; t < 900 && rig.results.empty(); ++t) rig.tick(script(t, rig.sim));
        const AttemptResult* r = rig.result("mech.halfflip");
        CHECK_MSG(r != nullptr, "no half-flip graded in the drill");
        if (r) {
            CHECK(!rig.touched);
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code == "me.drill_no_touch");
        }
        CHECK(rig.mod.status().drillAttempts == 1 && rig.mod.status().drillSuccesses == 0);
    }
}

// ---- HUD wording -----------------------------------------------------------------------------------------
TEST(mechanics_hud_chain_plural) {
    Rig rig;
    rig.sim.vel = {1000.f, 0.f, 0.f};
    auto script = groundWavedash();
    bool checked = false;
    for (int i = 0; i < 200 && !checked; ++i) {
        rig.tick(script(i, rig.sim));
        if (rig.count("mech.wavedash") == 0 || rig.mod.status().chainDashes != 1) continue;
        DrawList dl;
        ViewContext view;
        view.valid = true;
        view.screenW = 1920.f;
        view.screenH = 1080.f;
        DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, false, 1.f};
        rig.mod.draw(view, dctx, dl);
        CHECK(dl.panels.size() == 1);
        if (dl.panels.empty()) break;
        bool line = false;
        for (const auto& l : dl.panels[0].lines) {
            if (l.text.find("Chaîne en cours") == std::string::npos) continue;
            line = true;
            CHECK(l.text.find("1 dash,") != std::string::npos);
            CHECK(l.text.find("dashes") == std::string::npos);
        }
        CHECK(line);
        checked = true;
    }
    CHECK(checked);
}
