// Shared drill helpers (cross-cutting fixes 1 to 4 of the drill audit): step tracker, get-ready hold, hanging ball,
// "Ramassage" start. The scripted players react 0.3 to 0.5 s after the cue, and the tests assert the success of the
// sequence, not only that something happened.

#include "tests/TestFramework.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/common/HangingBall.h"
#include "core/common/Hold.h"
#include "core/common/Pickup.h"
#include "core/common/Steps.h"
#include "core/modules/prediction/BallPhysics.h"

using namespace rlt;

namespace {

constexpr float kDt = 1.f / 120.f;
constexpr float kGravity = 650.f;
constexpr float kReaction = 0.4f;  // seconds between a cue and the scripted player's answer

// Ground acceleration of the car with full throttle (RocketSim throttle curve).
float throttleAccel(float v) {
    if (v < 1400.f) return 1600.f - 1440.f * (v / 1400.f);
    if (v < 1410.f) return 160.f * (1410.f - v) / 10.f;
    return 0.f;
}

// Distance from point q to the Octane hitbox (0 inside).
float obbDist(const Vec3& carPos, const Basis& b, const Vec3& q) {
    const Vec3 off{13.88f, 0.f, 20.75f}, ext{118.01f, 84.20f, 36.16f};
    const Vec3 d = q - (carPos + b.toWorld(off));
    const float ex = std::max(0.f, std::fabs(dot(d, b.fwd)) - ext.x * 0.5f);
    const float ey = std::max(0.f, std::fabs(dot(d, b.right)) - ext.y * 0.5f);
    const float ez = std::max(0.f, std::fabs(dot(d, b.up)) - ext.z * 0.5f);
    return std::sqrt(ex * ex + ey * ey + ez * ez);
}

// Minimal world: the ball follows the core physics (pr::stepBall), the car a simple kinematic model. A tick is:
// snapshot -> module -> apply actions -> physics step (as in the game: actions run in the pre-physics hook).
struct World {
    int64_t frame = 1200;
    pr::SimBall ball;
    Vec3 carPos{0.f, 0.f, 17.f};
    Vec3 carVel;
    Rot carRot{};
    bool carGround = true;
    bool carGravity = false;   // free fall (car placed in the air)
    bool jumped = false;
    float jumpHeld = 0.f;      // seconds the jump has been held since the jump
    int touchFrame = 0;        // VehicleWrapper::GetLastBallTouchFrame
    bool touchFrameUpdates = true;
    int touches = 0;
    Inputs in;

    Snapshot snap() const {
        Snapshot s;
        s.frame = frame;
        s.time = static_cast<float>(frame) / 120.f;
        s.car.valid = true;
        s.car.pos = carPos;
        s.car.vel = carVel;
        s.car.rot = carRot;
        s.car.basis = basisFromRot(carRot);
        s.car.onGround = carGround;
        s.car.wheelContacts = carGround ? 4 : 0;
        s.car.jumped = jumped;
        s.car.hasFlip = true;
        s.car.lastBallTouchFrame = touchFrame;
        s.ball.valid = true;
        s.ball.pos = ball.pos;
        s.ball.vel = ball.vel;
        s.ball.angVel = ball.angVel;
        s.in = in;
        return s;
    }

    void apply(const Actions& a) {
        if (a.setBall) {
            ball.pos = a.setBall->pos;
            ball.vel = a.setBall->vel;
            ball.angVel = a.setBall->angVel;
        }
        if (a.setCar) {
            carPos = a.setCar->pos;
            carRot = a.setCar->rot;
            carVel = a.setCar->vel;
        }
    }

    // One physics step. Returns true when the car touched the ball during it.
    bool step(const Actions& a) {
        pr::stepBall(ball);
        const Basis b = basisFromRot(carRot);
        if (carGravity) {
            carVel.z -= kGravity * kDt;
            carPos += carVel * kDt;
        } else if (carGround) {
            const bool frozen = a.freezeCar;
            const float throttle = frozen ? 0.f : in.throttle;
            float v = dot(carVel, b.fwd);
            if (throttle > 0.f) v += throttleAccel(v) * throttle * kDt;
            else v = std::max(0.f, v - 525.f * kDt);
            carVel = flat(b.fwd) * v;
            if (!frozen && in.jump && !jumped) {
                jumped = true;
                carGround = false;
                carVel.z = 291.667f;
                jumpHeld = 0.f;
            }
            carPos += carVel * kDt;
        } else {
            // Airborne after a jump: sticky jump acceleration while held (0.2 s max), gravity.
            if (in.jump && jumpHeld < 0.2f) carVel.z += 1458.333f * kDt;
            jumpHeld += kDt;
            carVel.z -= kGravity * kDt;
            carPos += carVel * kDt;
            if (carPos.z <= 17.f) {
                carPos.z = 17.f;
                carVel.z = 0.f;
                carGround = true;
                jumped = false;
            }
        }
        bool hit = false;
        if (obbDist(carPos, b, ball.pos) <= 91.25f) {
            // Crude car hit: the ball takes the car's velocity plus a push away from the hitbox centre.
            const Vec3 n = normalize(ball.pos - (carPos + b.toWorld({13.88f, 0.f, 20.75f})), {0.f, 0.f, 1.f});
            ball.vel = carVel * 1.3f + n * 500.f;
            if (touchFrameUpdates) touchFrame = static_cast<int>(frame);
            ++touches;
            hit = true;
        }
        ++frame;
        return hit;
    }
};

Snapshot snapAt(int64_t frame) {
    Snapshot s;
    s.frame = frame;
    s.time = static_cast<float>(frame) / 120.f;
    s.car.valid = true;
    s.ball.valid = true;
    return s;
}

bool hasDash(const std::string& s) {
    return s.find("\xE2\x80\x94") != std::string::npos || s.find("\xE2\x80\x93") != std::string::npos;
}

// Steps driven by flags the test flips: one step per flag.
struct Flags {
    bool a = false, b = false, c = false, d = false;
};

std::vector<Step> threeSteps(const Flags& f) {
    std::vector<Step> v(3);
    v[0].id = "wall";
    v[0].label = "Va au mur de côté";
    v[0].check = stepWhen([&f](const StepInput&) { return f.a; });
    v[0].timeout = 6.f;
    v[0].failCode = "CEIL_STEP_WALL";
    v[0].tip = "Roule vers le mur de côté et boost.";
    v[1].id = "ceiling";
    v[1].label = "Monte au plafond";
    v[1].check = stepWhen([&f](const StepInput&) { return f.b; });
    v[1].timeout = 4.f;
    v[1].hold = 0.5f;
    v[1].failCode = "CEIL_STEP_FELL_WALL";
    v[1].tip = "Boost jusqu'au milieu du mur et monte plus droit.";
    v[2].id = "shot";
    v[2].label = "Flippe dans la balle";
    v[2].check = stepWhen([&f](const StepInput&) { return f.c; });
    v[2].timeout = 2.5f;
    v[2].failCode = "CEIL_LATE";
    v[2].tip = "Flippe quand la balle est juste devant ton nez.";
    return v;
}

} // namespace

// ---- StepTracker -------------------------------------------------------------------------------------------------

TEST(steps_validate_in_order_with_hold_and_clock_per_step) {
    Flags f;
    StepTracker t;
    t.setSteps(threeSteps(f));
    CHECK(t.idle());
    CHECK(!t.reached("wall"));
    int64_t fr = 1000;
    t.start(snapAt(fr).time);
    CHECK(t.running());
    CHECK_EQ(t.current(), 0);
    CHECK(t.reached("wall"));
    CHECK(!t.reached("ceiling"));
    CHECK_EQ(t.currentLine(), std::string("1/3 Va au mur de côté"));

    // The player reaches the wall after 2.5 s (well inside the 6 s).
    for (int i = 0; i < 300; ++i) t.update(snapAt(++fr));
    CHECK(t.running());
    CHECK_EQ(t.doneCount(), 0);
    f.a = true;
    CHECK_EQ(t.update(snapAt(++fr)), 1);
    CHECK(t.done("wall"));
    CHECK(t.doneThisTick("wall"));
    CHECK_EQ(t.current(), 1);
    CHECK_NEAR(t.stepElapsed(snapAt(fr).time), 0.f, 1e-4f);
    CHECK_NEAR(t.timeLeft(snapAt(fr).time), 4.f, 1e-3f);

    // Ceiling: 3.8 s later (the 4 s clock started at the wall), held 0.5 s = exactly 60 ticks.
    for (int i = 0; i < 456 - 60; ++i) t.update(snapAt(++fr));
    f.b = true;
    int ticks = 0;
    while (t.current() == 1 && ticks < 100) {
        t.update(snapAt(++fr));
        ++ticks;
        CHECK(!t.doneThisTick("wall"));
    }
    CHECK_EQ(ticks, 60);
    CHECK(t.done("ceiling"));
    CHECK(t.running());
    CHECK(t.onLastStep());

    // Shot 1 s after the ceiling.
    for (int i = 0; i < 120; ++i) t.update(snapAt(++fr));
    f.c = true;
    t.update(snapAt(++fr));
    CHECK(t.isComplete());
    CHECK_EQ(t.doneCount(), 3);
    CHECK(t.missingText().empty());
    CHECK(!t.onLastStep());
}

TEST(steps_timeout_fails_on_the_missing_step_with_its_tip) {
    Flags f;
    StepTracker t;
    t.setSteps(threeSteps(f));
    int64_t fr = 1000;
    t.start(snapAt(fr).time);
    f.a = true;
    t.update(snapAt(++fr));  // wall at once
    // The ceiling never comes: the 4 s of the step run out (counted from the wall, not from the start).
    int64_t failFrame = -1;
    for (int i = 0; i < 1000 && t.running(); ++i) {
        t.update(snapAt(++fr));
        if (t.failed()) failFrame = fr;
    }
    CHECK(t.failed());
    CHECK_EQ(failFrame - 1001, static_cast<int64_t>(481));  // first tick past 4 s
    CHECK_EQ(t.failure().index, 1);
    CHECK_EQ(t.failure().id, std::string("ceiling"));
    CHECK_EQ(t.failure().code, std::string("CEIL_STEP_FELL_WALL"));
    CHECK(t.failure().timedOut);
    CHECK_EQ(t.missingText(), std::string("Il manquait : Monte au plafond"));
    const Tip tip = t.missedTip(30.f);
    CHECK_EQ(tip.code, std::string("CEIL_STEP_FELL_WALL"));
    CHECK_EQ(tip.text, std::string("Boost jusqu'au milieu du mur et monte plus droit."));
    CHECK_NEAR(tip.weight, 30.f, 1e-6f);
    CHECK(t.state("ceiling") == StepState::Failed);
    CHECK(t.state("shot") == StepState::Pending);
    // Finished: further updates change nothing.
    f.b = f.c = true;
    CHECK_EQ(t.update(snapAt(++fr)), 0);
    CHECK(t.failed());
}

TEST(steps_check_failure_carries_its_own_code) {
    StepTracker t;
    Step s;
    s.id = "drop";
    s.label = "Lâche le plafond sans sauter";
    s.failCode = "CEIL_STEP_DROP";
    s.tip = "Laisse-toi tomber.";
    s.check = [](const StepInput& in) {
        if (in.snap.in.jump) return StepVerdict::fail("CEIL_JUMPED_OFF", "Ne saute pas : laisse-toi juste tomber.");
        return StepVerdict::pending();
    };
    t.add(s);
    int64_t fr = 500;
    t.start(snapAt(fr).time);
    Snapshot sn = snapAt(++fr);
    t.update(sn);
    CHECK(t.running());
    sn = snapAt(++fr);
    sn.in.jump = true;
    t.update(sn);
    CHECK(t.failed());
    CHECK(!t.failure().timedOut);
    CHECK_EQ(t.failure().code, std::string("CEIL_JUMPED_OFF"));
    CHECK_EQ(t.missedTip().text, std::string("Ne saute pas : laisse-toi juste tomber."));
    CHECK_EQ(t.missingText(), std::string("Il manquait : Lâche le plafond sans sauter"));
}

TEST(steps_info_step_never_blocks_and_keeps_the_clock) {
    // Ceiling route end: 3 drop (required), 4 place yourself (info), 5 flip into the ball within 2.5 s of the drop.
    bool drop = false, placed = false, shot = false;
    StepTracker t;
    Step s3;
    s3.id = "drop";
    s3.label = "Laisse-toi tomber";
    s3.check = stepWhen([&](const StepInput&) { return drop; });
    Step s4;
    s4.id = "place";
    s4.label = "Place-toi derrière la balle";
    s4.optional = true;
    s4.check = stepWhen([&](const StepInput&) { return placed; });
    Step s5;
    s5.id = "shot";
    s5.label = "Flippe dans la balle";
    s5.timeout = 2.5f;
    s5.failCode = "CEIL_LATE";
    s5.check = stepWhen([&](const StepInput&) { return shot; });
    t.setSteps({s3, s4, s5});

    // (a) The player flips without the info step: the info step is skipped, the attempt completes.
    int64_t fr = 2000;
    t.start(snapAt(fr).time);
    drop = true;
    t.update(snapAt(++fr));
    CHECK(t.done("drop"));
    CHECK_EQ(t.current(), 1);           // the info step is the HUD's current line
    CHECK_EQ(t.currentRequired(), 2);   // but the flip is what can end the attempt
    CHECK(t.onLastStep());
    {
        // Both are highlighted main lines; the drop is a detail line once its flash is over.
        std::vector<PanelLine> ls;
        StepHudStyle st;
        appendStepLines(ls, t, snapAt(fr).time + 2.f, st);
        CHECK_EQ(ls.size(), static_cast<size_t>(3));
        CHECK(ls[0].detail);
        CHECK(!ls[1].detail && ls[1].color == st.current);
        CHECK(!ls[2].detail && ls[2].color == st.current);
        CHECK_EQ(ls[2].text, std::string("3/3 Flippe dans la balle"));
    }
    for (int i = 0; i < 100; ++i) t.update(snapAt(++fr));
    shot = true;
    t.update(snapAt(++fr));
    CHECK(t.isComplete());
    CHECK(t.state("place") == StepState::Skipped);

    // (b) Info step validated, then the flip: both done.
    drop = placed = shot = false;
    t.start(snapAt(++fr).time);
    drop = true;
    t.update(snapAt(++fr));
    placed = true;
    t.update(snapAt(++fr));
    CHECK(t.done("place"));
    CHECK_EQ(t.current(), 2);
    shot = true;
    t.update(snapAt(++fr));
    CHECK(t.isComplete());
    CHECK_EQ(t.doneCount(), 3);

    // (c) The flip's 2.5 s count from the drop even when the info step took 2 s.
    drop = placed = shot = false;
    t.start(snapAt(++fr).time);
    drop = true;
    t.update(snapAt(++fr));
    const int64_t dropFrame = fr;
    for (int i = 0; i < 240; ++i) t.update(snapAt(++fr));
    placed = true;
    while (t.running() && fr < dropFrame + 1000) t.update(snapAt(++fr));
    CHECK(t.failed());
    CHECK_EQ(fr - dropFrame, static_cast<int64_t>(301));
    CHECK_EQ(t.failure().id, std::string("shot"));
    CHECK_EQ(t.failure().code, std::string("CEIL_LATE"));
}

TEST(steps_chain_on_one_tick_without_counting_holds_twice) {
    // Every condition is already true: a step without hold validates as soon as it becomes current, a step with a hold
    // still needs its full hold time, counted once per tick.
    int calls0 = 0, calls1 = 0;
    StepTracker t;
    Step a;
    a.id = "a";
    a.label = "A";
    a.check = [&](const StepInput&) { ++calls0; return StepVerdict::done(); };
    Step b;
    b.id = "b";
    b.label = "B";
    b.check = [&](const StepInput&) { ++calls1; return StepVerdict::done(); };
    Step c;
    c.id = "c";
    c.label = "C";
    c.hold = 0.25f;
    c.check = stepWhenSnap([](const Snapshot&) { return true; });
    t.setSteps({a, b, c});
    int64_t fr = 10;
    t.start(snapAt(fr).time);
    CHECK_EQ(t.update(snapAt(++fr)), 2);
    CHECK_EQ(calls0, 1);
    CHECK_EQ(calls1, 1);
    CHECK(t.doneThisTick("a"));
    CHECK(t.doneThisTick("b"));
    CHECK_EQ(t.current(), 2);
    CHECK(t.holdProgress() > 0.f);  // C ran once on that tick too
    int ticks = 1;
    while (t.running() && ticks < 100) {
        t.update(snapAt(++fr));
        ++ticks;
    }
    CHECK(t.isComplete());
    CHECK_EQ(ticks, 30);  // 0.25 s at 120 Hz, the first tick included
}

TEST(steps_hold_grace_tolerates_short_breaks) {
    // "Porte-la": ball on the roof 1.0 s, small hops of up to 0.3 s off the roof allowed.
    bool on = false;
    StepTracker t;
    Step s;
    s.id = "carry";
    s.label = "Porte-la";
    s.hold = 1.f;
    s.holdGrace = 0.3f;
    s.check = stepWhen([&](const StepInput&) { return on; });
    t.add(s);
    int64_t fr = 0;
    t.start(snapAt(fr).time);
    auto run = [&](bool value, int ticks) {
        on = value;
        for (int i = 0; i < ticks && t.running(); ++i) t.update(snapAt(++fr));
    };
    run(true, 72);   // 0.6 s
    run(false, 24);  // 0.2 s hop: kept
    run(true, 47);
    CHECK(t.running());
    CHECK_NEAR(t.holdProgress(), 119.f / 120.f, 0.01f);
    run(true, 1);
    CHECK(t.isComplete());
    // A break longer than the grace starts the hold again.
    t.start(snapAt(++fr).time);
    run(true, 72);
    run(false, 48);  // 0.4 s off the roof
    run(true, 60);
    CHECK(t.running());
    CHECK_NEAR(t.holdProgress(), 0.5f, 0.01f);
}

TEST(steps_gate_generic_checks_and_groups) {
    Flags f;
    StepTracker t;
    std::vector<Step> v = threeSteps(f);
    v[0].group = "pickup";
    t.setSteps(v);
    int64_t fr = 0;
    t.start(snapAt(fr).time);
    // A module switches its landing check on only from the step that needs it.
    CHECK(!t.reached("shot"));
    CHECK(!t.groupDone("pickup"));
    CHECK(!t.groupDone("nothing"));
    f.a = true;
    t.update(snapAt(++fr));
    CHECK(t.groupDone("pickup"));
    CHECK(t.reached("ceiling"));
    CHECK(!t.reached("shot"));
    CHECK(!t.onLastStep());
    f.b = true;
    for (int i = 0; i < 60; ++i) t.update(snapAt(++fr));
    CHECK(t.reached("shot"));
    CHECK(t.onLastStep());
    CHECK_EQ(t.indexOf("shot"), 2);
    CHECK_EQ(t.indexOf("nope"), -1);
    // complete() validates the current step from the outside (a graded shot seen by the module).
    CHECK(t.complete(snapAt(fr).time));
    CHECK(t.isComplete());
    CHECK(t.doneThisTick("shot"));
    CHECK(!t.complete(snapAt(fr).time));
}

TEST(steps_global_timeout_is_never_silent) {
    Flags f;
    StepTracker t;
    t.setSteps(threeSteps(f));
    int64_t fr = 0;
    t.start(snapAt(fr).time);
    f.a = true;
    t.update(snapAt(++fr));
    // Running: the message already names the step in progress.
    CHECK_EQ(t.missingText(), std::string("Il manquait : Monte au plafond"));
    CHECK_EQ(t.missedTip().code, std::string("CEIL_STEP_FELL_WALL"));
    t.failOnTimeout(snapAt(fr).time);
    CHECK(t.failed());
    CHECK(t.failure().timedOut);
    CHECK_EQ(t.failure().tip, std::string("Boost jusqu'au milieu du mur et monte plus droit."));
    // A module failure keeps the step reached and uses its own code ("landed" after the drop, say).
    t.start(snapAt(++fr).time);
    t.update(snapAt(++fr));
    t.fail(snapAt(fr).time, "DRILL_LANDED");
    CHECK_EQ(t.failure().code, std::string("DRILL_LANDED"));
    CHECK_EQ(t.failure().id, std::string("ceiling"));
    CHECK(!t.failure().timedOut);
    // A step without tip still gives a readable tip.
    StepTracker u;
    Step s;
    s.id = "x";
    s.label = "Saute";
    u.add(s);
    u.start(0.f);
    u.failOnTimeout(1.f);
    CHECK_EQ(u.missedTip().text, std::string("Il manquait : Saute."));
}

TEST(steps_hud_lines_green_current_and_details) {
    Flags f;
    StepTracker t;
    std::vector<Step> v = threeSteps(f);
    t.setSteps(v);
    int64_t fr = 0;
    t.start(snapAt(fr).time);
    StepHudStyle st;
    auto mains = [](const std::vector<PanelLine>& ls) {
        int n = 0;
        for (const auto& l : ls) n += l.detail ? 0 : 1;
        return n;
    };
    std::vector<PanelLine> lines;
    appendStepLines(lines, t, snapAt(fr).time, st);
    CHECK_EQ(lines.size(), static_cast<size_t>(3));
    CHECK_EQ(mains(lines), 1);
    CHECK_EQ(lines[0].text, std::string("1/3 Va au mur de côté"));
    CHECK(lines[0].color == st.current);
    CHECK(!lines[0].detail);
    CHECK(lines[1].detail && lines[2].detail);
    CHECK(lines[1].color == st.upcoming);

    // Just validated: green "OK" main line for 1 s, then a detail line.
    f.a = true;
    t.update(snapAt(++fr));
    lines.clear();
    appendStepLines(lines, t, snapAt(fr).time, st);
    CHECK_EQ(lines[0].text, std::string("1/3 Va au mur de côté : OK"));
    CHECK(lines[0].color == st.done);
    CHECK(!lines[0].detail);
    CHECK(!lines[1].detail);
    CHECK(lines[1].color == st.current);
    CHECK_EQ(mains(lines), 2);
    lines.clear();
    appendStepLines(lines, t, snapAt(fr + 121).time, st);
    CHECK(lines[0].detail);
    CHECK_EQ(mains(lines), 1);

    // Hold progress shows as a bar on the current line.
    f.b = true;
    for (int i = 0; i < 30; ++i) t.update(snapAt(++fr));
    lines.clear();
    appendStepLines(lines, t, snapAt(fr).time + 2.f, st);
    CHECK_NEAR(lines[1].bar, 0.5f, 0.02f);

    // Failure: the missing step is a red main line.
    f.b = false;
    for (int i = 0; i < 600 && t.running(); ++i) t.update(snapAt(++fr));
    CHECK(t.failed());
    lines.clear();
    appendStepLines(lines, t, snapAt(fr).time + 2.f, st);
    CHECK(lines[1].color == st.failed);
    CHECK(!lines[1].detail);
    CHECK_EQ(mains(lines), 1);

    // No check mark glyph (the HUD font has none), no dash characters anywhere.
    for (const auto& l : lines) {
        CHECK_MSG(l.text.find("\xE2\x9C\x93") == std::string::npos && l.text.find("\xE2\x9C\x94") == std::string::npos,
                  l.text);
        CHECK_MSG(!hasDash(l.text), l.text);
    }
    // Compact options: no done / upcoming lines at all.
    StepHudStyle bare;
    bare.showDone = bare.showUpcoming = false;
    bare.flashSeconds = 0.f;
    bare.numbered = false;
    lines.clear();
    appendStepLines(lines, t, snapAt(fr).time, bare);
    CHECK_EQ(lines.size(), static_cast<size_t>(1));
    CHECK_EQ(lines[0].text, std::string("Monte au plafond"));
    // Idle tracker: nothing.
    StepTracker idle;
    idle.setSteps(v);
    lines.clear();
    appendStepLines(lines, idle, 0.f, st);
    CHECK(lines.empty());
}

// ---- Hold and ready phase -----------------------------------------------------------------------------------------

TEST(hold_keeps_an_air_start_in_place_during_the_countdown) {
    // Ceiling start: car upside down against the ceiling, ball at 1450. freezeCar alone lets both fall.
    HoldPose h;
    h.car.pos = {3350.f, -800.f, 2027.f};
    h.car.rot = Rot{0, 0, 32768};
    h.ball = BallPlacement{{2300.f, 700.f, 1450.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
    h.boost = 100.f;

    for (int variant = 0; variant < 2; ++variant) {
        World w;
        w.carGround = false;
        w.carGravity = true;
        w.carPos = h.car.pos;
        w.carRot = h.car.rot;
        w.ball.pos = h.ball->pos;
        ReadyPhase ready;
        ready.start(w.snap().time, ReadyPhase::autoReset());
        bool go = false;
        int held = 0;
        while (!go && held < 400) {
            Actions a;
            const Snapshot s = w.snap();
            go = ready.update(s, a);
            if (!go) {
                if (variant == 0) {
                    applyHold(h, &s, a);
                    ++held;
                    CHECK(a.setCar && a.setBall && a.freezeCar);
                    CHECK(a.setCar->vel == Vec3{} && a.setCar->angVel == Vec3{});
                    CHECK(a.setBall->vel == Vec3{} && a.setBall->angVel == Vec3{});
                    CHECK(a.setBoost && *a.setBoost == 100.f);
                    CHECK(a.setCar->boost && *a.setCar->boost == 100.f);
                } else {
                    a.freezeCar = true;  // the old behaviour: placed once, then only frozen
                    ++held;
                }
            }
            w.apply(a);
            w.step(a);
        }
        CHECK(go);
        CHECK_EQ(held, 240);  // 2 s at 120 Hz
        const Snapshot end = w.snap();
        if (variant == 0) {
            // Only the GO tick itself (no hold any more) has moved them: 0.14 uu, 11 uu/s.
            CHECK_NEAR(end.car.pos.z, 2027.f, 0.5f);
            CHECK_NEAR(end.ball.pos.z, 1450.f, 0.5f);
            CHECK(length(end.ball.vel) < 20.f);
        } else {
            CHECK(end.car.pos.z < 2027.f - 1000.f);  // fell about 1300 uu in 2 s
            CHECK(end.ball.pos.z < 1450.f - 1000.f);
        }
    }
}

TEST(hold_keeps_the_settled_height_on_the_floor) {
    HoldPose h;
    h.car.pos = {0.f, -2000.f, 17.f};
    h.car.rot = rotFromYawDeg(90.f);
    Snapshot s = snapAt(5);
    s.car.pos = {1.f, -2001.f, 16.2f};  // settled a bit lower, near the pose
    Actions a;
    applyHold(h, &s, a);
    CHECK_NEAR(a.setCar->pos.z, 16.2f, 1e-4f);
    CHECK_NEAR(a.setCar->pos.x, 0.f, 1e-4f);  // x and y always back to the pose
    s.car.pos = {500.f, -2000.f, 16.2f};       // far away (teleport just asked): the pose height
    Actions b;
    applyHold(h, &s, b);
    CHECK_NEAR(b.setCar->pos.z, 17.f, 1e-4f);
    Actions c;
    applyHold(h, nullptr, c);
    CHECK_NEAR(c.setCar->pos.z, 17.f, 1e-4f);
    CHECK(!c.setBall);  // no ball in the pose: the ball is left alone
    CHECK(!c.setBoost);
    HoldPose noFreeze = h;
    noFreeze.freeze = false;
    Actions d;
    applyHold(noFreeze, nullptr, d);
    CHECK(!d.freezeCar);
}

TEST(ready_waits_for_the_throttle_then_counts_three_seconds) {
    ReadyPhase r;
    int64_t fr = 3000;
    r.start(snapAt(fr).time, ReadyPhase::firstAttempt());
    CHECK(r.waiting());
    CHECK_EQ(r.hudLine(snapAt(fr).time), std::string("Appuie sur accélérer quand tu es prêt"));
    // The player reads the line and presses the throttle 0.4 s later.
    std::vector<std::pair<int64_t, int>> messages;
    int64_t goFrame = -1, pressFrame = -1;
    int gos = 0;
    for (int i = 0; i < 1200; ++i) {
        Snapshot s = snapAt(++fr);
        const float sinceStart = static_cast<float>(i + 1) / 120.f;
        s.in.throttle = sinceStart >= kReaction ? 1.f : 0.f;
        if (s.in.throttle > 0.f && pressFrame < 0) pressFrame = fr;
        Actions a;
        if (r.update(s, a)) {
            goFrame = fr;
            ++gos;
            CHECK(a.goMessage);
        }
        if (a.countdownMessage) messages.push_back({fr, *a.countdownMessage});
        if (fr == pressFrame + 60) CHECK_EQ(r.hudLine(s.time), std::string("Départ dans 3"));
    }
    CHECK_EQ(gos, 1);
    CHECK_EQ(goFrame - pressFrame, static_cast<int64_t>(360));
    CHECK_EQ(messages.size(), static_cast<size_t>(3));
    if (messages.size() == 3) {
        CHECK_EQ(messages[0].second, 3);
        CHECK_EQ(messages[0].first, pressFrame);
        CHECK_EQ(messages[1].second, 2);
        CHECK_EQ(messages[1].first, pressFrame + 120);
        CHECK_EQ(messages[2].second, 1);
        CHECK_EQ(messages[2].first, pressFrame + 240);
    }
    CHECK(!r.active());
    CHECK(r.hudLine(snapAt(fr).time).empty());
    CHECK(!hasDash(r.hudLine(0.f)));
}

TEST(ready_needs_a_new_press_when_the_throttle_is_already_held) {
    ReadyPhase r;
    int64_t fr = 0;
    r.start(snapAt(fr).time, ReadyPhase::firstAttempt());
    // Still holding the throttle from the previous drill: no start.
    for (int i = 0; i < 240; ++i) {
        Snapshot s = snapAt(++fr);
        s.in.throttle = 1.f;
        Actions a;
        CHECK(!r.update(s, a));
    }
    CHECK(r.waiting());
    // Released 0.3 s, pressed again: the countdown starts.
    for (int i = 0; i < 36; ++i) {
        Actions a;
        r.update(snapAt(++fr), a);
    }
    Snapshot s = snapAt(++fr);
    s.in.throttle = 1.f;
    Actions a;
    r.update(s, a);
    CHECK(r.counting());
    // Half a trigger is not enough for a press, a full one is.
    ReadyPhase g;
    g.start(0.f, ReadyPhase::firstAttempt());
    Snapshot t = snapAt(1);
    t.in.throttle = 0.4f;
    Actions b;
    g.update(t, b);
    CHECK(g.waiting());
}

TEST(ready_auto_reset_and_timed_hold) {
    ReadyPhase r;
    int64_t fr = 100;
    r.start(snapAt(fr).time, ReadyPhase::autoReset());
    CHECK(r.counting());
    int msgs = 0;
    int64_t go = -1;
    for (int i = 0; i < 400 && go < 0; ++i) {
        Actions a;
        if (r.update(snapAt(++fr), a)) go = fr;
        if (a.countdownMessage) ++msgs;
    }
    CHECK_EQ(go - 100, static_cast<int64_t>(240));
    CHECK_EQ(msgs, 2);
    ReadyPhase m;
    m.start(0.f, ReadyPhase::manualRetry());
    CHECK(m.counting());
    CHECK_NEAR(m.remaining(0.f), 3.f, 1e-6f);
    // Timed hold (Mécas balle Ready 1.5 s at niveaux 1-3): no message.
    ReadyPhase t;
    fr = 0;
    t.start(snapAt(fr).time, ReadyPhase::timed(1.5f));
    go = -1;
    for (int i = 0; i < 400 && go < 0; ++i) {
        Actions a;
        if (t.update(snapAt(++fr), a)) {
            go = fr;
            CHECK(!a.goMessage);
        }
        CHECK(!a.countdownMessage);
    }
    CHECK_EQ(go, static_cast<int64_t>(180));
}

TEST(ready_waits_for_the_throttle_release_at_go_when_asked) {
    // Ball balanced on a stopped car: the player keeps the throttle held from his press through the countdown (the
    // kickoff habit). GO waits until he lets go, with a line that says so; then GO comes on the release tick.
    for (bool first : {true, false}) {
        ReadyPhase r;
        int64_t fr = 1000;
        ReadyPhase::Options o = first ? ReadyPhase::firstAttempt() : ReadyPhase::timed(1.5f);
        o.waitThrottleRelease = true;
        r.start(snapAt(fr).time, o);
        int64_t pressFrame = -1, goFrame = -1;
        for (int i = 0; i < 1200 && goFrame < 0; ++i) {
            Snapshot s = snapAt(++fr);
            const float since = static_cast<float>(i + 1) / 120.f;
            // Pressed after a 0.4 s reaction, held 6 s, then let go.
            s.in.throttle = since >= kReaction && since < kReaction + 6.f ? 1.f : 0.f;
            if (s.in.throttle > 0.f && pressFrame < 0) pressFrame = fr;
            Actions a;
            if (r.update(s, a)) goFrame = fr;
            if (pressFrame >= 0 && fr == pressFrame + 600) {
                CHECK(r.active() && r.counting());
                CHECK(r.waitingRelease(s.time));
                CHECK_EQ(r.hudLine(s.time), std::string("Lâche l'accélérateur pour partir"));
                CHECK(!a.goMessage && !a.countdownMessage);
            }
        }
        CHECK(pressFrame >= 0);
        CHECK(goFrame - pressFrame >= 719 && goFrame - pressFrame <= 721);  // the release tick, 6 s after the press
        CHECK(!r.active());
    }
    // Throttle let go before the end of the countdown: GO on time, as without the option.
    ReadyPhase r;
    ReadyPhase::Options o = ReadyPhase::autoReset();
    o.waitThrottleRelease = true;
    int64_t fr = 100;
    r.start(snapAt(fr).time, o);
    int64_t go = -1;
    for (int i = 0; i < 400 && go < 0; ++i) {
        Actions a;
        if (r.update(snapAt(++fr), a)) go = fr;
    }
    CHECK_EQ(go - 100, static_cast<int64_t>(240));
    CHECK(!hasDash("Lâche l'accélérateur pour partir"));
}

// ---- Hanging ball -------------------------------------------------------------------------------------------------

TEST(hanging_ball_waits_still_then_exactly_one_release) {
    World w;
    w.ball.pos = {0.f, 0.f, 93.15f};
    HangingBall hb;
    const Vec3 point{2300.f, 700.f, 1450.f};
    hb.hang(point);
    int held = 0, released = 0, after = 0;
    for (int i = 0; i < 600; ++i) {
        Actions a;
        if (i == 360) hb.release({-600.f, 0.f, 250.f});  // step validated 3 s in
        const HangingBall::Event e = hb.update(w.snap(), a);
        if (i < 360) {
            CHECK(e == HangingBall::Event::Held);
            CHECK(a.setBall && a.setBall->vel == Vec3{} && a.setBall->angVel == Vec3{});
            CHECK(a.setBall && a.setBall->pos == point);
            ++held;
        } else if (i == 360) {
            CHECK(e == HangingBall::Event::Released);
            CHECK(a.setBall && a.setBall->vel == Vec3(-600.f, 0.f, 250.f));
            ++released;
        } else {
            CHECK(e == HangingBall::Event::None);
            if (a.setBall) ++after;
        }
        w.apply(a);
        w.step(a);
        if (i > 1 && i < 360) CHECK_NEAR(w.ball.pos.z, 1450.f, 0.1f);  // never falls while hanging
    }
    CHECK_EQ(held, 360);
    CHECK_EQ(released, 1);
    CHECK_EQ(after, 0);
    CHECK(hb.released());
    CHECK(hb.free());
    // After the release the core physics moves it: up, then down, toward mid-field.
    CHECK(w.ball.pos.x < 2300.f - 900.f);
    CHECK(w.touches == 0);
}

TEST(hanging_ball_hit_by_the_player_is_never_erased) {
    // Balle haute like: ball hanging 1000 uu ahead at 345. The player waits 0.4 s, drives at it, jumps under it.
    for (int path = 0; path < 2; ++path) {
        World w;
        w.touchFrameUpdates = path == 0;  // path 1: no touch frame, the movement test must catch the hit
        HangingBall hb;
        hb.hang({1000.f, 0.f, 345.f});
        int touchedEvents = 0;
        int64_t hitFrame = -1;
        bool setBallAfterHit = false;
        for (int i = 0; i < 600; ++i) {
            const float t = static_cast<float>(i) / 120.f;
            w.in = Inputs{};
            if (t >= kReaction) w.in.throttle = 1.f;
            // Jumps (held) when the bumper is about 0.55 s from the ball: the roof then meets the ball's underside.
            const float toBall = (w.ball.pos.x - (w.carPos.x + 73.f)) / std::max(100.f, w.carVel.x);
            if (t >= kReaction && (toBall < 0.55f || w.jumped) && hitFrame < 0) w.in.jump = true;
            Actions a;
            const HangingBall::Event e = hb.update(w.snap(), a);
            if (e == HangingBall::Event::Touched) ++touchedEvents;
            if (hitFrame >= 0 && a.setBall) setBallAfterHit = true;
            w.apply(a);
            const int64_t f = w.frame;
            if (w.step(a) && hitFrame < 0) hitFrame = f;
        }
        CHECK(hitFrame > 0);
        CHECK_EQ(touchedEvents, 1);
        CHECK(!setBallAfterHit);
        CHECK(hb.touched());
        CHECK_EQ(hb.freeFrame(), hitFrame + 1);  // let go on the tick right after the contact
        CHECK(!hb.released());
        // The hit sent it away and nothing put it back.
        CHECK(distance(w.ball.pos, Vec3(1000.f, 0.f, 345.f)) > 500.f);
    }
}

TEST(hanging_ball_release_on_a_touch_tick_is_dropped) {
    HangingBall hb;
    hb.hang({0.f, 0.f, 800.f});
    Snapshot s = snapAt(10);
    s.ball.pos = {0.f, 0.f, 800.f};
    s.car.lastBallTouchFrame = 4;
    Actions a;
    CHECK(hb.update(s, a) == HangingBall::Event::Held);
    // Same tick: the module asks for the release and the snapshot shows a touch (touch frame changed).
    hb.release({0.f, 0.f, 280.f});
    s = snapAt(11);
    s.ball.pos = {0.f, 0.f, 800.f};
    s.ball.vel = {0.f, 0.f, -5.4f};
    s.car.lastBallTouchFrame = 10;
    Actions b;
    CHECK(hb.update(s, b) == HangingBall::Event::Touched);
    CHECK(!b.setBall);
    CHECK(hb.touched());
    // The caller's own touch flag works too, and moving the point on purpose is not a touch.
    HangingBall m;
    m.hang({0.f, 0.f, 500.f});
    Snapshot u = snapAt(20);
    u.ball.pos = {0.f, 0.f, 500.f};
    Actions c;
    CHECK(m.update(u, c) == HangingBall::Event::Held);
    m.hang({0.f, 300.f, 500.f});
    u = snapAt(21);
    u.ball.pos = {0.f, 0.f, 500.f};
    u.ball.vel = {0.f, 0.f, -5.4f};
    Actions d;
    CHECK(m.update(u, d) == HangingBall::Event::Held);
    CHECK(d.setBall && d.setBall->pos == Vec3(0.f, 300.f, 500.f));
    u = snapAt(22);
    u.ball.pos = {0.f, 300.f, 500.f};
    Actions e;
    CHECK(m.update(u, e, true) == HangingBall::Event::Touched);
    CHECK(!e.setBall);
    // A gap in the frames does not read as a touch; the touch frame still does.
    HangingBall g;
    g.hang({0.f, 0.f, 600.f});
    Snapshot v = snapAt(30);
    v.ball.pos = {0.f, 0.f, 600.f};
    Actions f1;
    g.update(v, f1);
    v = snapAt(40);
    v.ball.pos = {0.f, 0.f, 590.f};
    v.ball.vel = {0.f, 0.f, -60.f};
    Actions f2;
    CHECK(g.update(v, f2) == HangingBall::Event::Held);
    g.stop();
    CHECK(g.idle());
    Actions f3;
    CHECK(g.update(v, f3) == HangingBall::Event::None);
    CHECK(!f3.setBall);
}

// ---- Ramassage ----------------------------------------------------------------------------------------------------

TEST(pickup_setup_rest_then_rolling_ball) {
    Rng rng(7);
    for (int niveau = 1; niveau <= 10; ++niveau) {
        for (float yaw : {90.f, -90.f, 0.f, 37.f}) {
            const Vec3 carPos{-1500.f, -3600.f, 17.f};
            const pickup::Setup su = pickup::makeSetup(carPos, yaw, niveau, rng);
            const Basis b = basisFromRot(su.car.rot);
            CHECK(su.car.vel == Vec3{} && su.car.angVel == Vec3{});
            CHECK(su.car.pos == carPos);
            const Vec3 rel = su.ball.pos - carPos;
            CHECK_NEAR(su.ball.pos.z, 93.15f, 1e-3f);
            CHECK(su.heldBall.pos == su.ball.pos);
            CHECK(su.heldBall.vel == Vec3{} && su.heldBall.angVel == Vec3{});
            if (niveau <= 3) {
                CHECK(!su.rolling);
                CHECK_NEAR(dot(flat(rel), b.fwd), 600.f, 1.f);
                CHECK_NEAR(dot(flat(rel), b.right), 0.f, 1.f);
                CHECK(su.ball.vel == Vec3{});
            } else {
                CHECK(su.rolling);
                CHECK_NEAR(dot(flat(rel), b.fwd), 1500.f, 1.f);
                const float v = -dot(su.ball.vel, b.fwd);
                CHECK(v >= 350.f && v <= 450.f);
                CHECK_NEAR(length(su.ball.vel), v, 0.5f);
                // Rolling spin: the contact point does not slip (prediction physics convention).
                const Vec3 slip = su.ball.vel + cross(su.ball.angVel, Vec3{0.f, 0.f, -91.25f});
                CHECK(length(slip) < 1.f);
            }
        }
    }
}

TEST(pickup_rolling_ball_reaches_a_waiting_car_well_inside_the_window) {
    // Core ball physics from the setup (niveaux 4-10, 200 seeds): a player who only waits has the ball at his bumper
    // after 3.1 to 4.0 s, still rolling, so about half of the 8 s remain for the 0.5 s on the roof; one who drives to
    // meet it slowly (0.4 s reaction, 300 uu/s) gets it before 2.6 s. A resting ball does not move by itself.
    Rng rng(11);
    float worstWait = 0.f, worstDrive = 0.f;
    for (int seed = 0; seed < 200; ++seed) {
        const pickup::Setup su = pickup::makeSetup({0.f, -3000.f, 17.f}, 90.f, 4 + seed % 7, rng);
        const Basis b = basisFromRot(su.car.rot);
        for (int drive = 0; drive < 2; ++drive) {
            pr::SimBall ball;
            ball.pos = su.ball.pos;
            ball.vel = su.ball.vel;
            ball.angVel = su.ball.angVel;
            Vec3 car = su.car.pos;
            float t = 0.f;
            while (t < 8.f && obbDist(car, b, ball.pos) > 91.25f) {
                pr::stepBall(ball);
                if (drive) car += b.fwd * (std::clamp((t - kReaction) * 900.f, 0.f, 300.f) * kDt);
                t += kDt;
            }
            if (drive) {
                worstDrive = std::max(worstDrive, t);
                CHECK(t < 2.6f);
            } else {
                worstWait = std::max(worstWait, t);
                CHECK(t < 4.2f);
            }
            CHECK(length(ball.vel) > 300.f);
            CHECK(ball.pos.z < 100.f);  // still on the floor, no hop
        }
    }
    CHECK(worstWait > 3.5f);
    CHECK(worstDrive > 1.5f);
    const pickup::Setup rest = pickup::makeSetup({0.f, -3000.f, 17.f}, 90.f, 1, rng);
    pr::SimBall ball;
    ball.pos = rest.ball.pos;
    for (int i = 0; i < 960; ++i) pr::stepBall(ball);
    CHECK(distance(ball.pos, rest.ball.pos) < 1.f);
}

namespace {
// Scripted "Ramassage" with a reaction of 0.4 s: the player drives slowly into the resting ball; when the bumper
// reaches it the ball is put on the roof with some relative speed (the climb is not simulated: UNVERIFIED in game),
// and the player's small throttle corrections make it settle at the centre.
struct PickupRun {
    int r1Frames = -1, r2Frames = -1;
    bool complete = false, failed = false;
    std::string code;
};

PickupRun runPickup(StepTracker& t, pickup::Progress& prog, const pickup::Options& opt, bool reachBall, bool dropAfterR1,
                    bool jumpEarly) {
    PickupRun run;
    t.clear();
    pickup::addSteps(t, opt);
    Rng rng(3);
    const pickup::Setup su = pickup::makeSetup({0.f, -3000.f, 17.f}, 90.f, prog.niveau(), rng);
    Snapshot s = snapAt(5000);
    s.car.pos = su.car.pos;
    s.car.rot = su.car.rot;
    s.car.basis = basisFromRot(su.car.rot);
    s.car.onGround = true;
    s.car.wheelContacts = 4;
    s.ball.pos = su.ball.pos;
    const float roofZ = 20.75f + 36.16f * 0.5f + 91.25f;
    t.start(s.time);
    prog.beginAttempt();
    float speed = 0.f;
    bool onRoof = false, dropped = false;
    float offset = 60.f;  // the ball lands off centre toward the back
    float relV = 120.f;
    for (int i = 0; i < 1500 && t.running(); ++i) {
        const float tt = static_cast<float>(i) / 120.f;
        s.frame += 1;
        s.time = static_cast<float>(s.frame) / 120.f;
        if (reachBall && tt >= kReaction && !onRoof) speed = std::min(300.f, speed + 900.f * kDt);
        s.car.pos.y += speed * kDt;
        s.car.vel = {0.f, speed, 0.f};
        if (!onRoof && !dropped && reachBall && s.ball.pos.y - s.car.pos.y < 13.88f + 59.f + 91.25f) onRoof = true;
        if (onRoof) {
            // Correction after the reaction time: the offset and the relative speed fade out.
            offset = std::max(0.f, offset - (tt > 3.f ? 60.f : 20.f) * kDt);
            relV = std::max(0.f, relV - 100.f * kDt);
            s.ball.pos = s.car.pos + Vec3{0.f, 13.88f + offset, roofZ};
            s.ball.vel = s.car.vel + Vec3{0.f, -relV, 0.f};
            if (dropAfterR1 && t.done(pickup::kStepRoof)) {
                // Too much throttle: the ball slides off the back and lands behind the car.
                s.ball.pos = Vec3{s.car.pos.x, s.car.pos.y - 250.f, 93.15f};
                s.ball.vel = {};
                onRoof = false;
                dropped = true;
            }
        }
        s.in = Inputs{};
        if (jumpEarly && t.done(pickup::kStepRoof)) s.in.jump = true;
        t.update(s);
        prog.update(t);
        if (t.done(pickup::kStepRoof) && run.r1Frames < 0) run.r1Frames = i + 1;
        if (t.done(pickup::kStepSettle) && run.r2Frames < 0) run.r2Frames = i + 1;
    }
    run.complete = t.isComplete();
    run.failed = t.failed();
    run.code = t.failure().code;
    return run;
}
} // namespace

TEST(pickup_scripted_player_succeeds_and_feeds_its_own_staircase) {
    StepTracker t;
    pickup::Progress prog;
    CHECK_EQ(prog.niveau(), 1);
    const PickupRun ok = runPickup(t, prog, {}, true, false, false);
    CHECK(ok.complete);
    CHECK(ok.r1Frames > 0 && ok.r1Frames < 8 * 120);
    CHECK(ok.r2Frames > ok.r1Frames);
    CHECK(t.groupDone(pickup::kGroup));
    CHECK_EQ(prog.staircase().attempts(), 1);
    CHECK_EQ(prog.staircase().successes(), 1);
    CHECK_EQ(prog.update(t), 0);  // once per attempt
    // HUD of the pickup in plain words, no dash.
    CHECK_EQ(t.steps()[0].label, std::string("Ramasse la balle sur ton toit"));
    CHECK_EQ(t.steps()[1].label, std::string("Garde-la au centre du toit"));
    for (const Step& s : t.steps()) {
        CHECK(!hasDash(s.label) && !hasDash(s.tip));
        CHECK(!s.tip.empty());
    }

    // Never reaching the ball: R1 fails at 8 s with its tip, and only the pickup staircase records it.
    Staircase mechanic(0.75f, 0.03f, 0.f);
    const PickupRun miss = runPickup(t, prog, {}, false, false, false);
    CHECK(miss.failed);
    CHECK_EQ(miss.code, std::string("pickup.no_roof"));
    CHECK(t.failure().timedOut);
    CHECK_EQ(t.failure().group, std::string(pickup::kGroup));
    CHECK_EQ(t.missedTip().text, std::string(pickup::kRoofTip));
    CHECK_EQ(prog.staircase().attempts(), 2);
    CHECK_EQ(mechanic.attempts(), 0);

    // Ball dropped before it settled: R2 fails at once with the drop tip.
    pickup::Options slow;
    slow.settleTimeout = 6.f;
    StepTracker t2;
    pickup::Progress p2;
    const PickupRun drop = runPickup(t2, p2, slow, true, true, false);
    CHECK(drop.r1Frames > 0);
    CHECK(drop.failed);
    CHECK_EQ(drop.code, std::string("pickup.dropped"));
    CHECK_EQ(t2.missedTip().text, std::string(pickup::kDropTip));
    CHECK(!t2.failure().timedOut);

    // Flick: jumping before the ball is stable fails with the module's code.
    pickup::Options flick;
    flick.jumpCode = "FL_JUMP_UNSETTLED";
    StepTracker t3;
    pickup::Progress p3;
    const PickupRun jump = runPickup(t3, p3, flick, true, false, true);
    CHECK(jump.failed);
    CHECK_EQ(jump.code, std::string("FL_JUMP_UNSETTLED"));
    CHECK_EQ(t3.missedTip().text, std::string(pickup::kJumpTip));
    CHECK_EQ(p3.staircase().attempts(), 1);
    CHECK_EQ(p3.staircase().successes(), 0);
}

TEST(pickup_settle_tip_eases_off_when_the_ball_slides_back) {
    // Accelerating pushes the ball toward the back of the roof: a ball sliding back means the car outruns it, so the
    // player eases off (never the reversed advice).
    const std::string tip = pickup::kSettleTip;
    CHECK(tip.find("accélère un peu si elle glisse vers l'arrière") == std::string::npos);
    CHECK(tip.find("si elle glisse vers l'arrière, lâche un peu l'accélérateur") != std::string::npos);
    CHECK(tip.find("si elle glisse vers l'avant, accélère un peu") != std::string::npos);
    StepTracker t;
    pickup::addSteps(t, pickup::Options{});
    for (const auto& st : t.steps()) CHECK_MSG(st.tip.find("accélère un peu si elle glisse vers l'arrière") == std::string::npos, st.id);
}

TEST(pickup_geometry_and_custom_roof_test) {
    Snapshot s = snapAt(1);
    s.car.pos = {0.f, 0.f, 17.f};
    s.car.rot = rotFromYawDeg(90.f);
    s.car.basis = basisFromRot(s.car.rot);
    const float roofZ = 20.75f + 36.16f * 0.5f + 91.25f;
    s.ball.pos = s.car.pos + Vec3{0.f, 13.88f, roofZ};
    CHECK(pickup::ballOnRoof(s));
    CHECK(pickup::ballSettled(s, true));
    CHECK_NEAR(pickup::roofOffsetX(s), 0.f, 1e-3f);
    s.ball.pos = s.car.pos + Vec3{0.f, 13.88f + 55.f, roofZ};  // on the roof, but 55 uu toward the nose
    CHECK(pickup::ballOnRoof(s));
    CHECK(!pickup::ballSettled(s, true));
    s.ball.pos = s.car.pos + Vec3{0.f, 13.88f, roofZ};
    s.ball.vel = {0.f, 200.f, 0.f};  // sliding
    CHECK(!pickup::ballSettled(s, true));
    s.car.vel = {0.f, 120.f, 0.f};   // the car follows it: relative 80
    CHECK(pickup::ballSettled(s, true));
    s.ball.vel = {0.f, 120.f, 500.f};  // a vertical hop is not a planar slide
    CHECK_NEAR(pickup::relativePlanarSpeed(s), 0.f, 1e-3f);
    s.ball.pos = s.car.pos + Vec3{0.f, 13.88f, roofZ + 60.f};  // above the tolerance
    CHECK(!pickup::ballOnRoof(s));
    s.ball.pos = {300.f, 0.f, 93.15f};
    CHECK(!pickup::ballOnRoof(s));

    // The module's own roof test replaces the default one.
    int calls = 0;
    pickup::Options o;
    o.onRoof = [&calls](const Snapshot&) {
        ++calls;
        return true;
    };
    o.roofCode = "AD_NO_CATCH";
    StepTracker t;
    pickup::addSteps(t, o);
    CHECK_EQ(t.steps()[0].failCode, std::string("AD_NO_CATCH"));
    CHECK_EQ(t.steps()[0].group, std::string(pickup::kGroup));
    t.start(0.f);
    Snapshot u = snapAt(0);
    u.car.basis = basisFromRot(Rot{});
    u.ball.pos = {500.f, 0.f, 93.15f + 300.f};  // nowhere near the roof: only the custom test says yes
    for (int i = 0; i < 59; ++i) {
        u.frame += 1;
        u.time = static_cast<float>(u.frame) / 120.f;
        t.update(u);
    }
    CHECK(!t.done(pickup::kStepRoof));
    u.frame += 1;
    u.time = static_cast<float>(u.frame) / 120.f;
    t.update(u);
    CHECK(t.done(pickup::kStepRoof));
    CHECK(calls >= 60);
}

TEST(pickup_progress_moves_to_the_rolling_ball) {
    pickup::Progress p;
    Rng rng(1);
    CHECK(!pickup::makeSetup({0.f, 0.f, 17.f}, 90.f, p.niveau(), rng).rolling);
    // Successful pickups raise the pickup's own level: rolling ball from niveau 4.
    StepTracker t;
    pickup::addSteps(t);
    int attempts = 0;
    while (p.niveau() < pickup::kRollNiveau && attempts < 100) {
        p.staircase().record(true);
        ++attempts;
    }
    CHECK(attempts <= 12);
    const pickup::Setup su = pickup::makeSetup({0.f, 0.f, 17.f}, 90.f, p.niveau(), rng);
    CHECK(su.rolling);
    // The rolling ball gets its own words on the HUD.
    StepTracker r;
    pickup::addSteps(r, pickup::options(su.rolling));
    CHECK_EQ(r.steps()[0].label, std::string(pickup::kRoofLabelRolling));
    CHECK_EQ(r.steps()[0].tip, std::string(pickup::kRoofTipRolling));
    CHECK_EQ(pickup::options(false).roofLabel, std::string(pickup::kRoofLabel));
    Store st;
    p.save(st, "ballmech.pickup");
    pickup::Progress q;
    q.load(st, "ballmech.pickup");
    CHECK_EQ(q.niveau(), p.niveau());
    q.reset();
    CHECK_EQ(q.niveau(), 1);
}

// ---- Everything together: a ceiling-like parcours ---------------------------------------------------------------

TEST(parcours_hold_steps_and_hanging_ball_together) {
    // Ready (car held on the floor, ball hanging at 1450), GO, steps "wall" -> "ceiling" -> "drop", the ball released
    // with (-600, 0, 250) on the drop tick. Scripted player: each step 0.4 s after the previous cue plus travel time.
    World w;
    w.carPos = {2900.f, -1600.f, 17.f};
    w.carRot = rotFromYawDeg(20.f);
    w.ball.pos = {0.f, 0.f, 93.15f};
    const Vec3 hangPoint{2300.f, -100.f, 1450.f};
    HoldPose hold;
    hold.car.pos = w.carPos;
    hold.car.rot = w.carRot;
    hold.ball = BallPlacement{hangPoint, {}, {}};
    hold.boost = 100.f;

    struct Script {
        float wallAt = -1.f, ceilAt = -1.f, dropAt = -1.f;
    } sc;
    float goTime = -1.f;
    StepTracker t;
    Step s1;
    s1.id = "wall";
    s1.label = "Va au mur de côté";
    s1.timeout = 6.f;
    s1.check = stepWhen([&](const StepInput& in) { return sc.wallAt >= 0.f && in.now >= sc.wallAt; });
    Step s2;
    s2.id = "ceiling";
    s2.label = "Monte au plafond";
    s2.timeout = 4.f;
    s2.check = stepWhen([&](const StepInput& in) { return sc.ceilAt >= 0.f && in.now >= sc.ceilAt; });
    Step s3;
    s3.id = "drop";
    s3.label = "Laisse-toi tomber, sans sauter";
    s3.timeout = 1.5f;
    s3.check = stepWhen([&](const StepInput& in) { return sc.dropAt >= 0.f && in.now >= sc.dropAt; });
    t.setSteps({s1, s2, s3});

    ReadyPhase ready;
    HangingBall hb;
    ready.start(w.snap().time, ReadyPhase::firstAttempt());
    int releases = 0, heldTicks = 0, setBallAfterRelease = 0;
    Vec3 releaseVel;
    for (int i = 0; i < 2400; ++i) {
        Snapshot s = w.snap();
        const float now = s.time;
        // The player presses the throttle 0.4 s after the ready line.
        s.in.throttle = static_cast<float>(i) / 120.f >= kReaction ? 1.f : 0.f;
        Actions a;
        if (ready.active()) {
            if (ready.update(s, a)) {
                goTime = now;
                t.start(now);
                hb.hang(hangPoint);
                sc.wallAt = now + kReaction + 2.0f;           // drives to the wall
                sc.ceilAt = sc.wallAt + kReaction + 2.2f;     // climbs
                sc.dropAt = sc.ceilAt + kReaction;            // lets go
            } else {
                applyHold(hold, &s, a);
                CHECK(a.setBall && a.setBall->pos == hangPoint && a.setBall->vel == Vec3{});
            }
        } else if (t.running()) {
            t.update(s);
            if (t.doneThisTick("drop")) hb.release({-600.f, 0.f, 250.f});
        }
        const HangingBall::Event e = hb.update(s, a);
        if (e == HangingBall::Event::Held) ++heldTicks;
        if (e == HangingBall::Event::Released) {
            ++releases;
            releaseVel = a.setBall->vel;
        } else if (hb.released() && a.setBall) {
            ++setBallAfterRelease;
        }
        w.apply(a);
        w.carGround = true;
        w.in = Inputs{};  // the car itself is not simulated on the walls here
        w.step(a);
        if (goTime >= 0.f && !hb.free()) CHECK_NEAR(w.ball.pos.z, 1450.f, 0.1f);
    }
    CHECK(goTime > 0.f);
    CHECK(t.isComplete());
    CHECK_EQ(releases, 1);
    CHECK(releaseVel == Vec3(-600.f, 0.f, 250.f));
    CHECK_EQ(setBallAfterRelease, 0);
    CHECK(heldTicks > 600);  // still hanging during the whole climb
}
