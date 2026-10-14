// Automatic demos ("Voir la démo", Demos.h) and the 3D guides turned off by default:
//  - every timeline meets the module's own input-based checks (jump to flip delay, flip angle, cancel delay and
//    strength, first jump held and stick centred for the fast aerial, powerslide length...),
//  - played through MiniSim (RocketSim rules) by the pilot, every demo is graded a success by the detectors and passes
//    the drill's top level checks that do not need the ball, for both teams,
//  - in the module: activity, hold and countdown, slow motion, inputs overridden on every tick, HUD step in sync, no
//    grade for the player, speed restored and activity released at the end, every stop path,
//  - the saved "Repères 3D" of the previous version is moved once to the new default (off).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "tests/mechanics/MechRig.h"

#include "core/modules/mechanics/Demos.h"
#include "core/modules/mechanics/Drills.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

namespace {

const Focus kDemoFamilies[] = {Focus::SpeedFlip,  Focus::FlipCancel, Focus::HalfFlip, Focus::Wavedash,
                               Focus::FastAerial, Focus::Powerslide, Focus::Stall};

std::string describeGrades(const std::vector<MechGrade>& grades) {
    std::string s;
    for (const auto& g : grades) {
        s += std::string(" [") + mechInfo(g.mech).key + " " + g.variant + " score=" + std::to_string(static_cast<int>(g.score)) +
             (g.success ? " OK" : " FAIL");
        for (const auto& e : g.errors) s += std::string(" ") + e.code;
        for (const auto& m : g.metrics) s += std::string(" ") + m.key + "=" + std::to_string(m.value);
        s += "]";
    }
    return s.empty() ? std::string(" (no grade)") : s;
}

bool hasDash(const std::string& s) {
    return s.find("\xE2\x80\x94") != std::string::npos || s.find("\xE2\x80\x93") != std::string::npos;
}

// ---- Timeline facts read from the raw inputs (no physics) ---------------------------------------------------
struct Timeline {
    std::vector<Inputs> in;  // ticks 0 .. length - 1
    int firstPress = -1, firstRelease = -1, secondPress = -1, presses = 0;
};

Timeline timeline(const DemoScript& d) {
    Timeline tl;
    bool prev = false;
    for (int t = 0; t < d.length; ++t) {
        const Inputs i = demoInputs(d, t);
        tl.in.push_back(i);
        if (i.jump && !prev) {
            ++tl.presses;
            if (tl.firstPress < 0) tl.firstPress = t;
            else if (tl.secondPress < 0) tl.secondPress = t;
        }
        if (!i.jump && prev && tl.firstRelease < 0) tl.firstRelease = t;
        prev = i.jump;
    }
    return tl;
}

// Flip direction the game reads at the press (DodgeForward, DodgeStrafe), and its angle (0 forward, -90 left).
Dir2 flipDir(const Inputs& in) { return {in.dodgeForward, in.dodgeStrafe}; }
float flipAngleDeg(const Inputs& in) { return std::atan2(in.dodgeStrafe, in.dodgeForward) * kRadToDeg; }

// DodgeTracker's cancel rule: first tick after the flip where the stick opposes the flip's pitch at 0.8 or more.
int cancelDelay(const Timeline& tl, int flip) {
    const float fp = sgn(tl.in[static_cast<size_t>(flip)].dodgeForward);
    for (int t = flip + 1; t < static_cast<int>(tl.in.size()) && t <= flip + 78; ++t)
        if (tl.in[static_cast<size_t>(t)].pitch * fp >= 0.8f) return t - flip;
    return -1;
}

// ---- Played through MiniSim and the detection hub, like the module drives the car ---------------------------
struct DemoRun {
    std::vector<MechGrade> grades;
    const MechGrade* find(Mech m) const {
        for (const auto& g : grades)
            if (g.mech == m) return &g;
        return nullptr;
    }
};

DemoRun simulate(Focus f, int team, float angleDeg = 30.f) {
    const DemoScript d = makeDemo(f, team, angleDeg);
    DetectConfig cfg;
    cfg.focus = f;  // the detectors know what is trained, like in the drill
    cfg.speedflipTargetAngleDeg = angleDeg;
    Run r(cfg);
    r.sim.team = team;
    r.sim.place(d.car);
    DemoPilot pilot;
    pilot.start(r.tick);
    r.go(d.length + 120, [&](int t, MiniSim& c) {
        // The pilot reads the state BEFORE the physics step, like the SetVehicleInput pre-hook.
        if (t >= d.length) return Inputs{};
        return pilot.update(d, c.snap(r.tick, {}));
    });
    r.flush();
    return DemoRun{r.grades};
}

// The module's drill success rule (emitAttempt): the level's pass conditions, its score and, at the top levels, the
// detector's own success. The checks that need the ball (none in a demo) are left out.
bool passesTopLevel(Focus f, int team, const MechGrade& g, std::string& why) {
    Rng rng(1);
    DrillSetup top = makeDrill(f, 1.f, 0, team, rng);
    top.checks.erase(std::remove_if(top.checks.begin(), top.checks.end(),
                                    [](const DrillCheck& c) {
                                        if (!c.key) return false;
                                        const std::string k = c.key;
                                        return k == "timeToBall" || k == "ballZAtTouch" || k == "aimErrorDeg";
                                    }),
                     top.checks.end());
    const DrillJudgement j = judgeDrill(top, g);
    why = j.code ? j.code : "";
    return j.pass && g.score >= top.passScore && (g.success || !top.requireDetector);
}

Mech primaryOf(Focus f) { return focusInfo(f).primary; }

// Plays the rig with an idle player until the demo is over, feeding a separate detection hub with what the car really
// did (the module's own detection is off during a demo).
struct Watch {
    MovementCoach coach;
    std::vector<MechGrade> grades;
    int countdownTicks = 0, playTicks = 0, overrides = 0, stepMismatch = 0, stepBack = 0;
    bool resultDuringDemo = false;
    std::vector<int> stepAt;    // HUD step per tick of the timeline
    std::vector<float> upZAt;   // car's up.z per tick (snapshot the step was computed on)
};

Watch playDemo(Rig& rig, Focus f) {
    Watch w;
    w.coach.cfg.focus = f;
    w.coach.cfg.speedflipTargetAngleDeg = rig.mod.config().speedflipAngle;
    for (int i = 0; i < 1200 && rig.mod.demoActive(); ++i) {
        const size_t before = rig.results.size();
        rig.tick(Inputs{});
        if (rig.results.size() != before) w.resultDuringDemo = true;
        Snapshot s = rig.last;
        s.in = rig.applied;
        w.coach.step(s, w.grades);
        if (!rig.mod.demoActive()) break;
        const MechanicsModule::Status st = rig.mod.status();
        if (st.demoPhase == "Mise en place") {
            ++w.countdownTicks;
            CHECK(rig.routed.freezeCar && rig.routed.setCar.has_value() && !rig.routed.overrideInputs);
        } else {
            ++w.playTicks;
            if (rig.routed.overrideInputs) ++w.overrides;
            // The HUD step is the timeline's, except while a landing key waits for the car: then the step before it.
            const DemoScript& d = rig.mod.demoScript();
            const int t = rig.mod.demoTick();
            const DemoKey& key = d.keys[static_cast<size_t>(demoKeyIndex(d, t))];
            const bool canWait = key.assist == DemoAssist::Level && key.afterInverted;
            if (st.demoStep != demoStep(d, t) && !(canWait && st.demoStep == demoStep(d, t) - 1)) ++w.stepMismatch;
            if (!w.stepAt.empty() && st.demoStep < w.stepAt.back()) ++w.stepBack;
            w.stepAt.push_back(st.demoStep);
            w.upZAt.push_back(rig.last.car.basis.up.z);
        }
    }
    const Snapshot s = rig.sim.snap(rig.frame, {});
    w.coach.flushPending(s, w.grades, false);
    return w;
}

DrawList drawNow(Rig& rig, bool compact = true) {
    DrawList dl;
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, true, 1.f, compact};
    rig.mod.draw(view, dctx, dl);
    return dl;
}

} // namespace

// ---- Catalogue ------------------------------------------------------------------------------------------------
TEST(demo_catalogue_and_texts) {
    int n = 0;
    for (int i = 0; i < kFocusCount; ++i) n += hasDemo(static_cast<Focus>(i)) ? 1 : 0;
    CHECK_EQ(n, 7);
    for (Focus f : kDemoFamilies) {
        CHECK(hasDemo(f));
        for (int team : {0, 1}) {
            const DemoScript d = makeDemo(f, team);
            const std::string id = focusInfo(f).id;
            CHECK_MSG(d.focus == f && !d.steps.empty() && d.steps.size() <= 6 && !d.keys.empty(), id);
            CHECK_MSG(d.keys.front().at == 0 && d.length > d.keys.back().at, id);
            CHECK_MSG(d.speed > 0.24f && d.speed < 0.76f && d.countdown >= 1.f, id);
            for (size_t k = 1; k < d.keys.size(); ++k) CHECK_MSG(d.keys[k].at > d.keys[k - 1].at, id);
            std::vector<bool> shown(d.steps.size(), false);
            int prevStep = 0;
            for (const DemoKey& k : d.keys) {
                CHECK_MSG(k.step >= 0 && k.step < static_cast<int>(d.steps.size()), id);
                CHECK_MSG(k.step >= prevStep, id);  // the HUD steps only move forward
                prevStep = k.step;
                if (k.step >= 0 && k.step < static_cast<int>(shown.size())) shown[static_cast<size_t>(k.step)] = true;
            }
            for (bool b : shown) CHECK_MSG(b, id);  // every step is shown at some point
            for (const std::string& s : d.steps) CHECK_MSG(!s.empty() && !hasDash(s) && s.size() < 60, s);
            CHECK_MSG(!hasDash(d.variant), d.variant);
            // The ball is parked far from the car.
            CHECK_MSG(length2D(d.ball.pos - d.car.pos) > 3000.f, id);
        }
        // Orange: the same demo turned 180 degrees.
        const DemoScript b = makeDemo(f, 0), o = makeDemo(f, 1);
        CHECK_NEAR(o.car.pos.x, -b.car.pos.x, 1e-3f);
        CHECK_NEAR(o.car.pos.y, -b.car.pos.y, 1e-3f);
        CHECK_NEAR(o.car.vel.y, -b.car.vel.y, 1e-3f);
        const int dy = ((o.car.rot.yaw - b.car.rot.yaw) % 65536 + 65536) % 65536;
        CHECK_EQ(dy, 32768);
    }
    CHECK(!hasDemo(Focus::ChainDash) && !hasDemo(Focus::WallDash) && !hasDemo(Focus::None));
    CHECK(makeDemo(Focus::WallDash, 0).keys.empty());
    // Outside the timeline nothing is pressed; the dodge fields follow the stick like the game's controller.
    const DemoScript d = makeDemo(Focus::SpeedFlip, 0);
    const Inputs before = demoInputs(d, -1), after = demoInputs(d, d.length);
    CHECK(!before.jump && !before.boost && before.throttle == 0.f && !after.jump && after.throttle == 0.f);
    for (int t = 0; t < d.length; ++t) {
        const Inputs i = demoInputs(d, t);
        CHECK_NEAR(i.dodgeForward, clampf(-i.pitch, -1.f, 1.f), 1e-6f);
        CHECK_NEAR(i.dodgeStrafe, clampf(i.yaw + i.roll, -1.f, 1.f), 1e-6f);
    }
}

// ---- The timelines meet the module's own input-based checks ---------------------------------------------------
TEST(demo_speedflip_timeline_checks) {
    for (float angle : {30.f, 45.f, 20.f}) {
        const DemoScript d = makeDemo(Focus::SpeedFlip, 0, angle);
        const Timeline tl = timeline(d);
        CHECK_EQ(tl.presses, 2);
        // Kickoff: first jump between 50 and 80 ticks after GO (SpeedFlipDetector early/late tips).
        CHECK(tl.firstPress >= SpeedFlipDetector::kKickoffJumpEarlyTicks && tl.firstPress <= SpeedFlipDetector::kKickoffJumpLateTicks);
        // Flip right after the jump: within the drill's top level (14 ticks) and the detector's ideal (13).
        CHECK_EQ(tl.secondPress, d.flipAt);
        CHECK(tl.secondPress - tl.firstPress <= 13);
        // Diagonal forward flip at the angle the module grades (left), a real diagonal for the detector.
        const Inputs& flip = tl.in[static_cast<size_t>(d.flipAt)];
        CHECK_NEAR(flipAngleDeg(flip), -angle, 0.5f);
        CHECK(dodgeKindFromAngle(flipAngleDeg(flip), false) == DodgeKind::DiagForward);
        CHECK(stickMag(flip) >= 0.5f);  // past the dodge deadzone: a flip, never a double jump
        // Cancel within the green window (50 ms = 6 ticks), stick fully back through the flip's torque time.
        const int cancel = cancelDelay(tl, d.flipAt);
        CHECK(cancel >= 1 && cancel <= 6);
        for (int t = d.flipAt + cancel; t < d.flipAt + 78; ++t) CHECK(tl.in[static_cast<size_t>(t)].pitch >= 0.99f);
        // Boost and throttle held from GO (boost ratio, "garde le boost").
        for (int t = 0; t < 230; ++t) CHECK(tl.in[static_cast<size_t>(t)].boost && tl.in[static_cast<size_t>(t)].throttle >= 0.99f);
    }
}

TEST(demo_flipcancel_halfflip_timeline_checks) {
    {
        const DemoScript d = makeDemo(Focus::FlipCancel, 0);
        const Timeline tl = timeline(d);
        CHECK_EQ(tl.presses, 2);
        const Inputs& flip = tl.in[static_cast<size_t>(d.flipAt)];
        CHECK(dodgeKindFromAngle(flipAngleDeg(flip), false) == DodgeKind::Forward);
        CHECK(flipDir(flip).x > 0.9f);
        // Drill top level: cancel within 6 ticks, stick at 90 % or more (mean over the 24 ticks after the cancel).
        const int cancel = cancelDelay(tl, d.flipAt);
        CHECK(cancel >= 1 && cancel <= 6);
        float strength = 0.f;
        for (int t = d.flipAt + cancel; t < d.flipAt + cancel + 24; ++t) strength += clamp01(tl.in[static_cast<size_t>(t)].pitch);
        CHECK(strength / 24.f >= 0.9f);
        for (int t = d.flipAt + cancel; t < d.flipAt + 78; ++t) CHECK(tl.in[static_cast<size_t>(t)].pitch >= 0.99f);
    }
    {
        const DemoScript d = makeDemo(Focus::HalfFlip, 0);
        const Timeline tl = timeline(d);
        CHECK_EQ(tl.presses, 2);
        // Back flip right after the jump (drill top level: 14 ticks), stick straight back.
        CHECK(d.flipAt - tl.firstPress <= 14);
        const Inputs& flip = tl.in[static_cast<size_t>(d.flipAt)];
        CHECK(dodgeKindFromAngle(flipAngleDeg(flip), false) == DodgeKind::Back);
        CHECK(std::fabs(std::fabs(flipAngleDeg(flip)) - 180.f) < 1.f);
        // Cancel inside the detector's ideal band (14 to 30 ticks), stick pushed fully until the torque time is over.
        const int cancel = cancelDelay(tl, d.flipAt);
        CHECK(cancel >= 14 && cancel <= 30);
        for (int t = d.flipAt + cancel; t < d.flipAt + 78; ++t) CHECK(tl.in[static_cast<size_t>(t)].pitch <= -0.99f);
        // Reversing until the flip, the roll waits for the car to be upside down.
        for (int t = 0; t < d.flipAt; ++t) CHECK(tl.in[static_cast<size_t>(t)].throttle <= -0.99f);
        bool rollKey = false;
        for (const DemoKey& k : d.keys)
            if (k.assist == DemoAssist::Level && k.at < 200) rollKey = k.afterInverted && k.rollDir != 0.f && k.at > d.flipAt + cancel;
        CHECK(rollKey);
        for (int t = 0; t < d.flipAt + cancel; ++t) CHECK(tl.in[static_cast<size_t>(t)].roll == 0.f);
    }
}

TEST(demo_wavedash_fastaerial_powerslide_timeline_checks) {
    {
        const DemoScript d = makeDemo(Focus::Wavedash, 0);
        const Timeline tl = timeline(d);
        // A drop: a single press, the flip itself, forward, with the nose raised before it.
        CHECK_EQ(tl.presses, 1);
        CHECK_EQ(tl.firstPress, d.flipAt);
        const Inputs& flip = tl.in[static_cast<size_t>(d.flipAt)];
        CHECK(dodgeKindFromAngle(flipAngleDeg(flip), false) == DodgeKind::Forward && stickMag(flip) >= 0.5f);
        int noseUp = 0;
        for (int t = 0; t < d.flipAt; ++t) noseUp += tl.in[static_cast<size_t>(t)].pitch > 0.5f ? 1 : 0;
        CHECK(noseUp >= 5 && noseUp <= 15);
        CHECK(d.car.pos.z >= 200.f && d.car.pos.z <= 350.f);  // the drill's drop heights (2 to 3.5 m)
    }
    {
        const DemoScript d = makeDemo(Focus::FastAerial, 0);
        const Timeline tl = timeline(d);
        CHECK_EQ(tl.presses, 2);
        CHECK_EQ(d.flipAt, -1);  // a double jump, never a flip
        // FastAerialTrainer windows used by the detector: first jump held 23 to 27 ticks, second press within 9 ticks of
        // the release, boost from the first press, stick pulled during the first jump, centred at the second press.
        const int hold = tl.firstRelease - tl.firstPress, gap = tl.secondPress - tl.firstRelease;
        CHECK(hold >= 23 && hold <= 27);
        CHECK(gap >= 1 && gap <= 9);
        CHECK(tl.in[static_cast<size_t>(tl.firstPress)].boost);
        float tilt = 0.f;
        for (int t = tl.firstPress; t < tl.secondPress; ++t) tilt += clamp01(tl.in[static_cast<size_t>(t)].pitch);
        CHECK(tilt / static_cast<float>(tl.secondPress - tl.firstPress) >= 0.7f);
        const Inputs& second = tl.in[static_cast<size_t>(tl.secondPress)];
        CHECK(stickMag(second) < 0.7f * k::kDodgeDeadzoneDefault && std::fabs(second.dodgeForward) < 0.1f &&
              std::fabs(second.dodgeStrafe) < 0.1f);
        CHECK(tl.in[static_cast<size_t>(tl.secondPress + 1)].jump);  // the press lasts more than one tick
    }
    {
        const DemoScript d = makeDemo(Focus::Powerslide, 0);
        const Timeline tl = timeline(d);
        CHECK_EQ(tl.presses, 0);
        int slide = 0, firstSlide = -1, lastSlide = -1;
        for (int t = 0; t < d.length; ++t) {
            const Inputs& i = tl.in[static_cast<size_t>(t)];
            CHECK(i.throttle >= 0.99f);
            if (!i.handbrake) continue;
            ++slide;
            if (firstSlide < 0) firstSlide = t;
            lastSlide = t;
            CHECK(std::fabs(i.steer) >= 0.99f);  // the slide always comes with the turn
        }
        CHECK(slide >= 4 && slide <= 24 && lastSlide - firstSlide + 1 == slide);  // one short slide
        CHECK(length(d.car.vel) > 400.f);  // fast enough for the detector
    }
}

TEST(demo_stall_timeline_checks) {
    // One press, stick and air roll on opposite sides at full deflection (no flip direction, no pitch), held from
    // before the press: the detector's stall inputs, past the player's flip deadzone.
    const DemoScript d = makeDemo(Focus::Stall, 0);
    const Timeline tl = timeline(d);
    CHECK_EQ(tl.presses, 1);
    CHECK(tl.firstPress == d.flipAt);
    const Inputs& at = tl.in[static_cast<size_t>(tl.firstPress)];
    CHECK(at.yaw * at.roll < 0.f && std::fabs(at.yaw) >= 0.99f && std::fabs(at.roll) >= 0.99f);
    CHECK_NEAR(at.yaw + at.roll, 0.f, 1e-6f);
    CHECK_NEAR(at.pitch, 0.f, 1e-6f);
    CHECK(flipDir(at).x == 0.f && flipDir(at).y == 0.f);  // nothing for the game to flip toward
    for (int t = tl.firstPress - 10; t < tl.firstPress; ++t)  // set before the press
        CHECK(tl.in[static_cast<size_t>(t)].yaw > 0.99f && tl.in[static_cast<size_t>(t)].roll < -0.99f);
    CHECK(d.car.pos.z > 400.f && d.car.vel.z > 0.f && !d.kickoffBoost);  // released in the air, rising
}

// ---- Played through the physics model: graded a success, top level passed ------------------------------------
TEST(demo_simulated_success_both_teams) {
    for (Focus f : kDemoFamilies) {
        for (int team : {0, 1}) {
            const DemoRun r = simulate(f, team);
            const std::string id = std::string(focusInfo(f).id) + " team " + std::to_string(team);
            const MechGrade* g = r.find(primaryOf(f));
            CHECK_MSG(g && g->success, id + describeGrades(r.grades));
            if (!g) continue;
            std::string why;
            CHECK_MSG(passesTopLevel(f, team, *g, why), id + " " + why + describeGrades(r.grades));
            // One graded move, never a second flip or a wrong kind of move.
            for (const MechGrade& o : r.grades)
                CHECK_MSG(o.mech == g->mech || o.mech == Mech::Recovery || o.mech == Mech::BoostEfficiency ||
                              o.mech == Mech::AirSpin,
                          id + describeGrades(r.grades));
        }
    }
    // The fast aerial windows as the detector measured them.
    const DemoRun fa = simulate(Focus::FastAerial, 0);
    const MechGrade* g = fa.find(Mech::FastAerial);
    CHECK(g && g->metric("holdTicks") >= 23.f && g->metric("holdTicks") <= 27.f && g->metric("gapTicks") <= 9.f &&
          g->metric("boostStartTicks") <= 2.f && g->metric("tiltInput") >= 0.7f && g->errors.empty());
}

TEST(demo_speedflip_follows_the_graded_angle) {
    // The demo flips at the angle set in the expert settings: top level passed across the usual range...
    for (float angle : {26.f, 30.f, 38.f, 45.f}) {
        const DemoRun r = simulate(Focus::SpeedFlip, 0, angle);
        const MechGrade* g = r.find(Mech::SpeedFlip);
        std::string why;
        CHECK_MSG(g && g->success && passesTopLevel(Focus::SpeedFlip, 0, *g, why),
                  std::to_string(angle) + " " + why + describeGrades(r.grades));
        if (g) CHECK_NEAR(g->metric("absAngleDeg"), angle, 0.5f);
    }
    // ... and a clean speedflip for the detector at the ends of the setting.
    for (float angle : {15.f, 20.f, 60.f}) {
        const DemoRun r = simulate(Focus::SpeedFlip, 0, angle);
        const MechGrade* g = r.find(Mech::SpeedFlip);
        CHECK_MSG(g && g->success && g->metric("landedOnWheels") > 0.5f, std::to_string(angle) + describeGrades(r.grades));
    }
}

// ---- In the module --------------------------------------------------------------------------------------------
TEST(demo_module_drives_the_car_and_gives_it_back) {
    for (Focus f : kDemoFamilies) {
        const std::string id = focusInfo(f).id;
        Rig rig;
        rig.tick(Inputs{});
        rig.command("demo", {id});
        CHECK_MSG(rig.host.owner == &rig.mod && rig.mod.demoActive(), id);
        CHECK_MSG(rig.toastContains("Démo : " + std::string(focusInfo(f).title)), id);
        const Watch w = playDemo(rig, f);
        const DemoScript d = makeDemo(f, 0, rig.mod.config().speedflipAngle);
        // Held 1.5 s, then the whole timeline with the inputs overridden on every tick, the HUD step in sync.
        CHECK_MSG(w.countdownTicks >= static_cast<int>(d.countdown * 120.f) - 2 &&
                      w.countdownTicks <= static_cast<int>(d.countdown * 120.f) + 2,
                  id + " " + std::to_string(w.countdownTicks));
        CHECK_MSG(w.playTicks >= d.length - 1 && w.overrides == w.playTicks && w.stepMismatch == 0 && w.stepBack == 0, id);
        CHECK_MSG(!w.stepAt.empty() && w.stepAt.back() == static_cast<int>(d.steps.size()) - 1, id);  // all shown
        if (f == Focus::SpeedFlip) {
            // "Roues au sol" appears once the car is back from upside down, not while it still rolls.
            const auto it = std::find(w.stepAt.begin(), w.stepAt.end(), 4);
            CHECK(it != w.stepAt.end() && w.upZAt[static_cast<size_t>(it - w.stepAt.begin())] > 0.4f);
        }
        CHECK(!rig.speeds.empty() && std::fabs(rig.speeds.front() - d.speed) < 1e-4f);
        // The car really did the move (graded by a separate hub), and the module recorded nothing for the player.
        const MechGrade* g = nullptr;
        for (const auto& x : w.grades)
            if (x.mech == primaryOf(f) && !g) g = &x;
        CHECK_MSG(g && g->success, id + describeGrades(w.grades));
        CHECK_MSG(!w.resultDuringDemo && rig.results.empty() && rig.mod.stats().sessionTotal() == 0, id);
        // End: speed back to 1 as the owner, activity released on the next tick, "Rejouer" / "À toi" offered.
        CHECK(!rig.mod.demoActive());
        CHECK_NEAR(rig.lastSpeed, 1.f, 1e-6f);
        CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
        rig.tick(Inputs{});
        CHECK_MSG(rig.host.owner == nullptr && !rig.routed.overrideInputs, id);
        const MechanicsModule::Status st = rig.mod.status();
        CHECK(st.demoEnded && st.demoFocus == f && !st.demoActive);
        CHECK(rig.toastContains("Démo terminée"));
        for (const auto& t : rig.toasts) CHECK_MSG(!hasDash(t.title) && !hasDash(t.text), t.text);
        const DrawList dl = drawNow(rig);
        CHECK(dl.panels.size() == 1 && dl.panels[0].title.find("Démo terminée") == 0);
        // The player's own inputs count again.
        Inputs mine;
        mine.throttle = 0.7f;
        rig.tick(mine);
        CHECK_NEAR(rig.applied.throttle, 0.7f, 1e-6f);
    }
}

TEST(demo_module_kickoff_conditions_and_hold) {
    Rig rig;
    rig.tick(Inputs{});
    rig.command("demo", {"speedflip"});
    rig.tick(Inputs{});
    // Placed at the diagonal kickoff spawn with 33 boost and match boost rules, ball parked, slow motion.
    CHECK(rig.routed.setCar.has_value() && rig.routed.freezeCar && rig.routed.boostRules.has_value());
    if (rig.routed.boostRules) CHECK(!rig.routed.boostRules->unlimited && !rig.routed.boostRules->noBoost);
    if (rig.routed.setCar) {
        CHECK_NEAR(rig.routed.setCar->pos.x, -2048.f, 1.f);
        CHECK_NEAR(rig.routed.setCar->pos.y, -2560.f, 1.f);
        CHECK(rig.routed.setCar->boost && std::fabs(*rig.routed.setCar->boost - 33.33f) < 0.1f);
    }
    CHECK(rig.routed.setBall.has_value() && length2D(rig.routed.setBall->pos) > 4000.f);
    CHECK_NEAR(rig.lastSpeed, 0.5f, 1e-6f);
    // The player's presses during the countdown change nothing (car held, inputs zeroed).
    Inputs mash;
    mash.throttle = 1.f;
    mash.jump = true;
    mash.boost = true;
    for (int i = 0; i < 60; ++i) {
        rig.tick(mash);
        CHECK(rig.routed.freezeCar && !rig.routed.overrideInputs);
    }
    CHECK_NEAR(rig.last.car.pos.x, -2048.f, 1.f);
    CHECK(rig.mod.status().demoPhase == "Mise en place");
    // Orange: the mirrored spawn.
    Rig orange;
    orange.sim.team = 1;
    orange.tick(Inputs{});
    orange.command("demo", {"speedflip"});
    orange.tick(Inputs{});
    CHECK(orange.routed.setCar && std::fabs(orange.routed.setCar->pos.x - 2048.f) < 1.f &&
          std::fabs(orange.routed.setCar->pos.y - 2560.f) < 1.f);
}

TEST(demo_module_stop_paths) {
    auto playing = [](Rig& rig, const char* id) {
        rig.tick(Inputs{});
        rig.command("demo", {id});
        for (int i = 0; i < 400 && rig.mod.status().demoPhase != "Démo en cours"; ++i) rig.tick(Inputs{});
        for (int i = 0; i < 20; ++i) rig.tick(Inputs{});
        CHECK(rig.mod.demoActive() && rig.routed.overrideInputs.has_value());
    };
    {  // Reset: the player takes the car back at once.
        Rig rig;
        playing(rig, "speedflip");
        rig.event(GameEventKind::ResetShot);
        CHECK(!rig.mod.demoActive() && rig.toastContains("Démo arrêtée"));
        CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
        rig.tick(Inputs{});
        CHECK(rig.host.owner == nullptr && !rig.routed.overrideInputs && !rig.routed.freezeCar);
        CHECK(!rig.mod.status().demoEnded);  // stopped: nothing offered
    }
    {  // "rlt mechanics stop"
        Rig rig;
        playing(rig, "halfflip");
        rig.command("stop");
        CHECK(!rig.mod.demoActive());
        CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
        rig.tick(Inputs{});
        CHECK(rig.host.owner == nullptr && !rig.routed.overrideInputs);
    }
    {  // Engine stop (stop button, "rlt engine stop"): the engine restores the speed.
        Rig rig;
        playing(rig, "wavedash");
        rig.host.stopActivity();
        CHECK(!rig.mod.demoActive());
        CHECK_NEAR(rig.host.gameSpeed, 1.f, 1e-6f);
        rig.tick(Inputs{});
        CHECK(!rig.routed.overrideInputs && rig.host.owner == nullptr);
    }
    {  // Map change.
        Rig rig;
        playing(rig, "powerslide");
        rig.setContext(GameMode::Freeplay, "Stadium_P", true);
        CHECK(!rig.mod.demoActive());
        rig.tick(Inputs{});
        CHECK(!rig.routed.overrideInputs && rig.host.owner == nullptr);
    }
    {  // Another module takes the activity.
        Rig rig;
        playing(rig, "flipcancel");
        OtherModule other;
        other.attach(&rig.host);
        CHECK(other.start("Kickoff"));
        CHECK(!rig.mod.demoActive());
        rig.tick(Inputs{});
        CHECK(!rig.routed.overrideInputs);
    }
    {  // Module turned off.
        Rig rig;
        playing(rig, "fastaerial");
        rig.set("rlt_me_enabled", "0");
        rig.tick(Inputs{});
        CHECK(!rig.mod.demoActive());
        rig.tick(Inputs{});
        CHECK(!rig.routed.overrideInputs && rig.host.owner == nullptr);
    }
}

TEST(demo_module_refusals) {
    {  // Not in freeplay.
        Rig rig;
        rig.setContext(GameMode::OfflineMatch, "Stadium_P", false);
        rig.tick(Inputs{});
        rig.command("demo", {"speedflip"});
        CHECK(!rig.mod.demoActive() && rig.host.owner == nullptr && rig.toastContains("freeplay uniquement"));
    }
    {  // Workshop map with the exercises allowed there: still no demo (Stadium positions, maybe inside the map).
        Rig rig;
        rig.setContext(GameMode::Workshop, "Custom_Map", true);
        rig.tick(Inputs{});
        rig.command("demo", {"speedflip"});
        CHECK(!rig.mod.demoActive() && rig.host.owner == nullptr && rig.toastContains("freeplay uniquement"));
    }
    {  // A family without a demo points to the videos.
        Rig rig;
        rig.tick(Inputs{});
        rig.command("demo", {"walldash"});
        CHECK(!rig.mod.demoActive() && rig.host.owner == nullptr && rig.toastContains("tutos vidéo"));
    }
    {  // Unknown id.
        Rig rig;
        rig.tick(Inputs{});
        rig.command("demo", {"moonwalk"});
        CHECK(!rig.mod.demoActive() && rig.toastContains("Méca inconnue"));
    }
    {  // The help of an unknown command lists "demo".
        Rig rig;
        rig.command("dance");
        CHECK(rig.toastContains("demo"));
    }
}

TEST(demo_and_drill_replace_each_other) {
    Rig rig;
    CHECK(rig.startDrill("halfflip"));
    rig.command("demo", {"halfflip"});
    CHECK(rig.mod.demoActive() && !rig.mod.drillActive() && rig.host.owner == &rig.mod);
    for (int i = 0; i < 300; ++i) rig.tick(Inputs{});
    CHECK(rig.mod.demoActive() && rig.results.empty());
    // "À toi : lancer l'exercice" while the demo runs: the drill takes over, the player drives again.
    rig.command("drill", {"halfflip"});
    CHECK(!rig.mod.demoActive() && rig.mod.drillActive());
    rig.tick(Inputs{});
    CHECK(!rig.routed.overrideInputs && rig.routed.freezeCar);
    CHECK(rig.waitGo());
    Inputs back;
    back.throttle = -1.f;
    rig.tick(back);
    CHECK_NEAR(rig.applied.throttle, -1.f, 1e-6f);
}

TEST(demo_never_graded_as_the_players_move) {
    // The fast aerial demo ends in the air: its landing is not the player's recovery.
    Rig rig;
    rig.tick(Inputs{});
    rig.command("demo", {"fastaerial"});
    playDemo(rig, Focus::FastAerial);
    CHECK(!rig.mod.demoActive() && rig.last.car.wheelContacts == 0);
    for (int i = 0; i < 4 * 120; ++i) rig.tick(Inputs{});
    CHECK(rig.last.car.wheelContacts >= 3);
    CHECK_MSG(rig.results.empty() && rig.mod.stats().sessionTotal() == 0, std::to_string(rig.results.size()));
    // Back on the wheels: the player's own moves are graded again.
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(160, groundWavedash());
    CHECK(rig.result("mech.wavedash") != nullptr);
}

TEST(demo_hud_panel) {
    Rig rig;
    rig.tick(Inputs{});
    rig.command("demo", {"speedflip"});
    rig.tick(Inputs{});
    {  // Countdown: the steps to watch and the start.
        const DrawList dl = drawNow(rig);
        CHECK(dl.panels.size() == 1);
        if (!dl.panels.empty()) {
            const DrawPanel& p = dl.panels[0];
            CHECK(p.title == "Démo : Speedflip");
            bool honest = false, first = false, start = false;
            for (const PanelLine& l : p.lines) {
                CHECK_MSG(!hasDash(l.text) && !l.detail, l.text);
                honest = honest || l.text == "Démo automatique (timings standards)";
                first = first || l.text == "1. Boost et accélérateur à fond";
                start = start || l.text.find("Départ dans") != std::string::npos;
            }
            CHECK(honest && first && start);
        }
    }
    for (int i = 0; i < 400 && rig.mod.demoTick() < 73; ++i) rig.tick(Inputs{});
    {  // Playing, tick 73: step 3 (the flip, from tick 71 to the cancel at 76) is highlighted.
        const int step = demoStep(rig.mod.demoScript(), rig.mod.demoTick());
        CHECK_EQ(step, 2);
        CHECK_EQ(rig.mod.status().demoStep, step);
        const DrawList dl = drawNow(rig);
        CHECK(dl.panels.size() == 1);
        if (!dl.panels.empty()) {
            const DrawPanel& p = dl.panels[0];
            const std::string current = "3. " + rig.mod.demoScript().steps[2];
            bool found = false, stopHint = false;
            for (const PanelLine& l : p.lines) {
                if (l.text == current) {
                    found = true;
                    CHECK(l.color.r == 255 && l.color.g == 199 && l.color.b == 64);  // accent
                }
                stopHint = stopHint || l.text.find("Reset pour arrêter") != std::string::npos;
            }
            CHECK(found && stopHint);
        }
    }
    // The tab rows say which families have a demo.
    for (const auto& r : rig.mod.statRows()) CHECK_MSG(r.hasDemo == hasDemo(r.focus), r.id);
    const MechanicsModule::Status st = rig.mod.status();
    CHECK(st.demoActive && st.demoFocus == Focus::SpeedFlip && st.demoSteps.size() == 5 && !st.demoVariant.empty());
    CHECK(st.gameSpeed < 0.6f);
}

// ---- 3D guides (the "trajectory" lines) off by default, old saved value moved once ----------------------------
TEST(guides_off_by_default_and_migrated_once) {
    {
        SettingsSchema schema;
        MechanicsModule::describeSettings(schema);
        Settings s;
        schema.applyDefaults(s);
        CHECK(!s.mechanics.showGuides && !MechanicsConfig{}.showGuides);
        const SettingDef* d = schema.find("rlt_me_guides");
        CHECK(d && d->defaultValue == "0" && !hasDash(d->tooltip));
    }
    Store saved;
    {  // A v1.0.7 player: "Repères 3D" saved on (the old default) in config.cfg, no flag in the progress file.
        Rig rig;
        rig.mod.load(Store{});
        rig.set("rlt_me_guides", "1");
        // No snapshot yet (menu): nothing happens.
        {
            Actions a;
            rig.mod.onTick(rig.ctx(), a);
            CHECK(a.setCvars.empty());
        }
        Actions first = rig.tick(Inputs{});
        CHECK_EQ(first.setCvars.size(), size_t(1));
        if (!first.setCvars.empty()) CHECK(first.setCvars[0].first == "rlt_me_guides" && first.setCvars[0].second == "0");
        CHECK(first.requestSave);
        CHECK(rig.toastContains("page HUD, section Trajectoires"));  // the toast shared by every trajectory
        for (const auto& t : rig.toasts) CHECK(!hasDash(t.text));
        const size_t toasts = rig.toasts.size();
        Actions second = rig.tick(Inputs{});
        CHECK(second.setCvars.empty() && rig.toasts.size() == toasts);
        rig.mod.save(saved);
        CHECK_EQ(saved.getInt("mechanics.defaults_v2", 0), 1);
    }
    {  // Reloaded with the flag: never again, even with the value on (the player turned it back on himself).
        Rig rig;
        rig.mod.load(saved);
        rig.set("rlt_me_guides", "1");
        Actions a = rig.tick(Inputs{});
        CHECK(a.setCvars.empty() && !rig.toastContains("section Trajectoires"));
    }
    {  // A player who had turned them off keeps his choice: only the flag is set.
        Rig rig;
        rig.mod.load(Store{});
        rig.set("rlt_me_guides", "0");
        Actions a = rig.tick(Inputs{});
        CHECK(a.setCvars.empty() && a.requestSave && rig.toasts.empty());
        Store st;
        rig.mod.save(st);
        CHECK_EQ(st.getInt("mechanics.defaults_v2", 0), 1);
    }
    {  // The module turned off: the saved value is still moved.
        Rig rig;
        rig.mod.load(Store{});
        rig.set("rlt_me_enabled", "0");
        rig.set("rlt_me_guides", "1");
        Actions a = rig.tick(Inputs{});
        CHECK_EQ(a.setCvars.size(), size_t(1));
    }
}
