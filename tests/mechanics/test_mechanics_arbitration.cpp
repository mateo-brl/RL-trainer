// Activity arbitration with other modules: the focus slow motion and "restore_speed" never stop another module's
// exercise (TickContext::otherActivityRunning), the wish waits and is applied once that exercise ends; a drill
// started by the player still takes over. The engine restores the game speed whenever an activity ends.
#include <string>

#include "tests/mechanics/MechRig.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

TEST(mechanics_tick_context_reports_other_activity) {
    Rig rig;
    CHECK(!rig.ctx().otherActivityRunning);
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    CHECK(rig.ctx().otherActivityRunning);
    rig.command("drill", {"halfflip"});
    CHECK(rig.host.ownsActivity(&rig.mod));
    CHECK(!rig.ctx().otherActivityRunning);  // its own activity is not "another" one
}

TEST(mechanics_focus_command_waits_for_other_drill) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    rig.command("focus", {"speedflip"});
    CHECK(rig.mod.focus() == Focus::SpeedFlip);  // the focus itself applies at once (grading filter)
    CHECK(rig.host.requests == 1);               // only the other module's request
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.mod.speedWaiting());
    CHECK(rig.toastContains("quand il se terminera"));
    CHECK(!rig.toastContains("Ralenti indisponible"));
    rig.run(30, idle());
    CHECK(rig.host.ownsActivity(&other));
    CHECK(rig.speeds.empty());
    // Home "Stop" button ends the other exercise: the wish is applied on the next tick.
    rig.host.stopActivity();
    rig.run(2, idle());
    CHECK(rig.host.ownsActivity(&rig.mod));
    CHECK(rig.host.name == "Mécas : focus Speedflip");
    CHECK(rig.speeds.size() == 1 && rig.speeds.back() == 0.5f);
    CHECK(!rig.mod.speedWaiting());
    // Focus off: restored by the module itself as the owner, released next tick.
    rig.command("focus", {"off"});
    CHECK(rig.speeds.size() == 2 && rig.speeds.back() == 1.f);
    rig.tick(Inputs{});
    CHECK(!rig.host.ownsActivity(&rig.mod));
}

TEST(mechanics_waiting_focus_slowmo_dropped) {
    // Slider back to 1, focus off, stop, map change or module disabled while waiting: nothing is taken later.
    for (int variant = 0; variant < 5; ++variant) {
        Rig rig;
        rig.command("focus", {"stall"});
        OtherModule other;
        other.attach(&rig.host);
        CHECK(other.start("Kickoff"));
        rig.set("rlt_me_slowmo", "0.5");
        rig.run(3, idle());
        CHECK_MSG(rig.mod.speedWaiting(), std::to_string(variant));
        switch (variant) {
        case 0: rig.set("rlt_me_slowmo", "1"); break;
        case 1: rig.command("focus", {"off"}); break;
        case 2: rig.command("stop"); break;
        case 3: rig.setContext(GameMode::Freeplay, "Stadium_P", true); break;
        default: rig.set("rlt_me_enabled", "0"); break;
        }
        rig.run(2, idle());
        CHECK_MSG(!rig.mod.speedWaiting(), std::to_string(variant));
        CHECK(rig.host.ownsActivity(&other));
        const int requests = rig.host.requests;
        other.stop();
        rig.run(5, idle());
        CHECK_MSG(rig.host.requests == requests, std::to_string(variant));
        CHECK(!rig.host.ownsActivity(&rig.mod));
        CHECK(rig.speeds.empty());
    }
}

TEST(mechanics_slider_after_stop_button_takes_activity_when_free) {
    // The stop button ends the focus slow motion; the module does not take it back by itself, but a later slider
    // change (a new request from the player) does when no other exercise runs.
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    rig.command("focus", {"stall"});
    rig.run(2, idle());
    CHECK(rig.host.ownsActivity(&rig.mod));
    rig.host.stopActivity();
    CHECK(rig.host.engineRestores == 1);
    rig.run(20, idle());
    CHECK(rig.host.requests == 1);
    CHECK(!rig.host.ownsActivity(&rig.mod));
    CHECK(rig.mod.focus() == Focus::Stall);
    rig.set("rlt_me_slowmo", "0.75");
    rig.run(2, idle());
    CHECK(rig.host.requests == 2);
    CHECK(rig.host.ownsActivity(&rig.mod));
    CHECK(!rig.speeds.empty() && rig.speeds.back() == 0.75f);
}

TEST(mechanics_drill_command_still_takes_over) {
    Rig rig;
    rig.set("rlt_me_slowmo", "0.5");
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    rig.command("drill", {"wavedash"});
    CHECK(rig.mod.drillActive());
    CHECK(rig.host.ownsActivity(&rig.mod));
    Actions a = rig.tick(Inputs{});
    CHECK(a.setCar.has_value());
    CHECK(a.setGameSpeed.has_value() && *a.setGameSpeed == 0.5f);
    // A waiting slow motion or restore wish is dropped by the drill (the drill applies its own speed).
    Rig w;
    w.command("focus", {"stall"});
    OtherModule o;
    o.attach(&w.host);
    CHECK(o.start("Kickoff"));
    w.command("restore_speed");
    CHECK(w.mod.speedWaiting());
    w.command("drill", {"stall"});
    CHECK(!w.mod.speedWaiting());
    CHECK(w.host.ownsActivity(&w.mod));
}

TEST(mechanics_restore_and_slowmo_wishes_exclusive) {
    // A waiting focus slow motion: "restore_speed" explains that the slow motion follows the setting.
    Rig rig;
    rig.command("focus", {"stall"});
    OtherModule other;
    other.attach(&rig.host);
    CHECK(other.start("Kickoff"));
    rig.set("rlt_me_slowmo", "0.5");
    rig.run(2, idle());
    rig.command("restore_speed");
    CHECK(rig.toastContains("suit le réglage"));
    // A waiting restore replaced by a newer slow motion wish: only the slow motion is applied when free.
    Rig r2;
    r2.command("focus", {"stall"});
    OtherModule o2;
    o2.attach(&r2.host);
    CHECK(o2.start("Kickoff"));
    r2.command("restore_speed");
    r2.set("rlt_me_slowmo", "0.5");
    r2.run(2, idle());
    o2.stop();
    r2.run(3, idle());
    CHECK(r2.host.ownsActivity(&r2.mod));
    CHECK(r2.speeds.size() == 1 && r2.speeds.back() == 0.5f);
    CHECK(!r2.toastContains("Vitesse du jeu remise"));
}
