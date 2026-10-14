// Playability of every movement drill (drill audit, cross-cutting fixes 1, 5 and 10): the drill is driven through
// MechanicsModule like the engine does (MechRig: get-ready phase, OBB touch, a falling ball), by scripted players who
// answer 0.3 to 0.5 s after GO. The tests assert the SUCCESS of a fair attempt at niveau 1 (a Diamond player who never
// trained the move) and at niveau 10 for a clean one, the recorded failure (with its tip) of the typical mistakes,
// and that a time limit is never silent.
#include <functional>
#include <memory>
#include <string>

#include "tests/mechanics/MechRig.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

namespace {

constexpr int kReact = 48;  // 0.4 s: the scripted player's answer after GO

std::string describe(const Rig& rig) {
    std::string s;
    for (const auto& r : rig.results) {
        s += " [" + r.skill + " " + std::to_string(static_cast<int>(r.score)) + (r.success ? " OK" : " FAIL");
        for (const auto& t : r.tips) s += " " + t.code;
        s += "]";
    }
    s += " gap " + std::to_string(static_cast<int>(rig.closestGap));
    return s.empty() ? std::string(" (no result)") : s;
}

// Runs `script` (tick index since GO, sim) until the first result or `seconds`.
void play(Rig& rig, const Script& script, float seconds = 7.f) {
    for (int i = 0; i < static_cast<int>(seconds * 120.f) && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
}

std::string firstTip(const AttemptResult* r) { return r && !r->tips.empty() ? r->tips.front().code : std::string(); }

// Steering toward a point (sim axes): + = right.
float steerTo(const MiniSim& c, const Vec3& target) {
    const Vec3 to = normalize(Vec3{target.x - c.pos.x, target.y - c.pos.y, 0.f});
    const Vec3 f = normalize(Vec3{c.fwd().x, c.fwd().y, 0.f});
    const float crossZ = f.x * to.y - f.y * to.x;  // > 0: target to the left
    return clampf(-crossZ * 6.f, -1.f, 1.f);
}

// Speedflip of a player: boost and throttle held from the countdown, (optional) steer toward `aim` until the jump,
// jump at `jumpAt` ticks after GO held `hold` ticks, dodge `toFlip` ticks after the jump at `angleDeg` (+ right), stick
// pulled back `cancelAfter` ticks after the dodge, air roll then nose toward `aim` until the wheels touch.
struct SpeedflipPlan {
    int jumpAt = 60, hold = 8, toFlip = 14, cancelAfter = 6;
    float angleDeg = -30.f;
    bool steer = true;
    Vec3 aim{0.f, 0.f, 0.f};  // sim axes (game ball at the centre spot: sim origin)
};
Script speedflip(const SpeedflipPlan& p) {
    auto inverted = std::make_shared<bool>(false);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        const int J = p.jumpAt, D = J + p.toFlip;
        if (t < J) {
            if (p.steer) in.steer = steerTo(c, p.aim);
        } else if (t < J + p.hold) {
            in.jump = true;
        } else if (t < D) {
        } else if (t < D + p.cancelAfter) {
            in.jump = t < D + 3;
            const float a = p.angleDeg * kDegToRad;
            in.yaw = std::sin(a);
            in.pitch = -std::cos(a);
        } else {
            in.pitch = 1.f;
            if (c.up().z < -0.3f) *inverted = true;
            in.roll = sgn(p.angleDeg);
            if (c.up().z > 0.3f && t > D + 30) {
                Inputs pd;
                orientPD(c, normalize(Vec3{p.aim.x - c.pos.x, p.aim.y - c.pos.y, 0.f}, c.fwd()), pd);
                in.roll = pd.roll;
                in.yaw = pd.yaw;
                in.pitch = pd.pitch;
            }
            if (c.wheelCount() >= 3) {
                in.roll = in.pitch = in.yaw = 0.f;
                if (p.steer) in.steer = steerTo(c, p.aim);
            }
        }
        return in;
    };
}

Inputs boostHold() {
    Inputs in;
    in.boost = true;
    return in;
}

} // namespace

// ---- Speedflip --------------------------------------------------------------------------------------------------
TEST(play_speedflip_level1_beginner_succeeds) {
    // Stage 1 at niveau 1: no ball, car held for the whole get-ready phase (pose with zero velocity and boost re-sent on
    // every tick), then a hesitant beginner: jump 70 ticks after GO, flip 14 ticks later at 45 degrees, cancel 12 ticks
    // after the flip, air roll back on the wheels.
    Rig rig;
    rig.command("drill", {"speedflip"});
    int held = 0;
    Inputs in;
    rig.tick(in);
    rig.tick(in);
    in.throttle = 1.f;
    in.boost = true;
    while (rig.mod.status().phase == "Compte à rebours" && held < 600) {
        const Actions a = rig.tick(in);
        if (rig.mod.status().phase != "Compte à rebours") break;
        CHECK(a.freezeCar && a.setCar && length(a.setCar->vel) < 1e-3f && std::fabs(a.setCar->pos.z - 20.f) <= 5.f);
        CHECK(a.setBoost && *a.setBoost == 100.f);
        ++held;
    }
    CHECK(held >= 359);
    CHECK(rig.mod.drillSetup().stage == 1);
    SpeedflipPlan p;
    p.jumpAt = 70;
    p.toFlip = 14;
    p.cancelAfter = 12;
    p.angleDeg = -45.f;
    p.steer = false;
    p.aim = rig.sim.pos + rig.sim.fwd() * 3000.f;
    play(rig, speedflip(p));
    CHECK_EQ(rig.count("mech.speedflip"), 1);
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r && r->success, describe(rig));
}

TEST(play_speedflip_never_silent) {
    struct Case {
        const char* what;
        int toFlip;
        float angle;
        bool flip;
        const char* tip;
    };
    for (const Case& k : {Case{"flip avant tardif", 40, 0.f, true, "me.sf_jump_long"},
                          Case{"flip de côté", 10, -90.f, true, "me.sf_side_flip"},
                          Case{"pas de flip", 10, 0.f, false, "me.sf_no_flip"}}) {
        Rig rig;
        CHECK(rig.startDrill("speedflip", 0.f, false, boostHold()));
        SpeedflipPlan p;
        p.toFlip = k.toFlip;
        p.angleDeg = k.angle;
        p.steer = false;
        Script s = speedflip(p);
        if (!k.flip) {
            s = [](int t, MiniSim&) {
                Inputs in;
                in.throttle = 1.f;
                in.boost = true;
                in.jump = t >= 60 && t < 68;
                return in;
            };
        }
        play(rig, s);
        CHECK_MSG(rig.count("mech.speedflip") == 1, std::string(k.what) + describe(rig));
        const AttemptResult* r = rig.result("mech.speedflip");
        CHECK_MSG(r && !r->success && firstTip(r) == k.tip, std::string(k.what) + describe(rig));
        CHECK(rig.mod.status().drillAttempts == 1);
    }
}

TEST(play_speedflip_stage2_reach_then_touch) {
    // Niveau 4 (right diagonal): an aimed speedflip that passes beside the ball without touching it counts (the small
    // steer before the jump comes with practice); niveau 6 asks for the real touch.
    for (int n : {4, 5, 6}) {
        Rig rig;
        CHECK(rig.startDrill("speedflip", levelOf(n), false, boostHold()));
        CHECK(rig.mod.drillSetup().stage == 2 && rig.mod.drillSetup().spotKind == 0);
        CHECK_NEAR(rig.mod.drillSetup().reachGap, n == 4 ? 150.f : (n == 5 ? 80.f : 0.f), 1e-3f);
        SpeedflipPlan p;
        p.angleDeg = -30.f;
        // Aim 320 uu beside the ball (sim axes, perpendicular to the approach from the right diagonal): the hitbox
        // passes about 40 uu from the ball surface.
        const Vec3 approach = normalize(Vec3{-rig.sim.pos.x, -rig.sim.pos.y, 0.f});
        p.aim = Vec3{approach.y, -approach.x, 0.f} * 320.f;
        play(rig, speedflip(p));
        const AttemptResult* r = rig.result("mech.speedflip");
        CHECK_MSG(r != nullptr, describe(rig));
        CHECK_MSG(!rig.touched && rig.closestGap > 0.f && rig.closestGap <= rig.mod.drillSetup().reachGap + (n == 6 ? 100.f : 0.f),
                  describe(rig));
        if (n < 6) CHECK_MSG(r && r->success, describe(rig));
        else CHECK_MSG(r && !r->success && firstTip(r) == "me.drill_no_touch", describe(rig));
    }
}

TEST(play_speedflip_stage3_real_kickoff) {
    // Niveau 7, right diagonal: SpeedFlipTrainer timeline (jump 59 ticks after GO, held 11, 26 degrees, cancel after
    // 5 ticks) with the small steer before the jump: touch within 2.3 s, detector success.
    Rig rig;
    CHECK(rig.startDrill("speedflip", levelOf(7), false, boostHold()));
    CHECK(rig.mod.drillSetup().stage == 3 && rig.mod.drillSetup().kickoff);
    SpeedflipPlan p;
    p.jumpAt = 59;
    p.hold = 11;
    p.toFlip = 12;
    p.cancelAfter = 5;
    p.angleDeg = -26.f;
    const float go = rig.last.time;
    play(rig, speedflip(p));
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r && r->success, describe(rig));
    CHECK(rig.touched && rig.touchTime - go <= 2.3f);
}

// ---- Half-flip --------------------------------------------------------------------------------------------------
namespace {
// Half-flip `react` ticks after GO: keep reversing, jump, back flip `toFlip` ticks later, push the stick forward
// `cancelAfter` ticks after the flip, air roll once upside down, then drive to the ball.
Script halfFlipBot(int react, int toFlip, int cancelAfter) {
    auto inverted = std::make_shared<bool>(false);
    auto landed = std::make_shared<int>(-1);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = -1.f;
        const int J = react, D = J + toFlip;
        if (t >= J && t < J + 4) in.jump = true;
        if (t >= D && t < D + 3) {
            in.jump = true;
            in.pitch = 1.f;
        }
        if (t >= D + 3) {
            const bool cancelling = t >= D + cancelAfter;
            in.pitch = cancelling ? -1.f : 1.f;
            if (cancelling) {
                if (c.up().z < 0.f) *inverted = true;
                if (*inverted) in.roll = c.up().z > 0.6f ? rollToLevel(c) : 1.f;
            }
            if (t > D + 10 && c.wheelCount() >= 3 && *landed < 0) *landed = t;
            if (*landed >= 0) {
                in.pitch = in.roll = 0.f;
                in.throttle = 1.f;
                in.boost = true;
                in.steer = steerTo(c, {0.f, 0.f, 0.f});  // the ball (sim axes)
            }
        }
        return in;
    };
}
} // namespace

TEST(play_halfflip_level1) {
    {
        Rig rig;
        rig.command("drill", {"halfflip"});
        rig.tick(Inputs{});
        CHECK(rig.waitGo());
        CHECK(length(rig.sim.vel) > 290.f && length(rig.sim.vel) < 310.f);  // reversing at 300 from GO, not before
        rig.ballGame = rig.mod.drillSetup().ball.pos;
        const float go = rig.last.time;
        play(rig, halfFlipBot(60, 10, 24));
        const AttemptResult* r = rig.result("mech.halfflip");
        CHECK_MSG(r && r->success, describe(rig));
        CHECK(rig.touched && rig.touchTime - go < 3.5f);
    }
    {
        Rig rig;  // cancelled far too late: the car finishes its flip
        CHECK(rig.startDrill("halfflip"));
        play(rig, halfFlipBot(60, 10, 70));
        const AttemptResult* r = rig.result("mech.halfflip");
        CHECK_MSG(r && !r->success && firstTip(r) == "me.hf_late_cancel", describe(rig));
    }
    {
        Rig rig;  // no flip at all: one recorded failure at the time limit
        CHECK(rig.startDrill("halfflip"));
        play(rig, [](int, MiniSim&) {
            Inputs in;
            in.throttle = -1.f;
            return in;
        });
        CHECK_EQ(rig.count("mech.halfflip"), 1);
        CHECK_MSG(firstTip(rig.result("mech.halfflip")) == "me.hf_no_flip", describe(rig));
        CHECK(rig.mod.status().drillAttempts == 1);
    }
}

// ---- Flip cancel --------------------------------------------------------------------------------------------------
namespace {
// Front flip `react` ticks after GO, stick pulled back at `strength` `cancelAfter` ticks after the flip and held for
// a quarter of a second, then the car levelled for the landing.
Script flipCancelBot(int react, int cancelAfter, float strength) {
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        const int J = react, D = J + 10;
        if (t >= J && t < J + 4) in.jump = true;
        if (t >= D && t < D + 3) {
            in.jump = true;
            in.pitch = -1.f;
        }
        if (t >= D + cancelAfter && t < D + cancelAfter + 30) in.pitch = strength;
        else if (t >= D + cancelAfter + 30 && c.wheelCount() < 3) in.pitch = pitchToNose(c, 0.f);
        return in;
    };
}
} // namespace

TEST(play_flipcancel_levels) {
    for (int n : {1, 10}) {
        Rig rig;
        CHECK(rig.startDrill("flipcancel", levelOf(n)));
        play(rig, flipCancelBot(kReact, 15, 0.85f));
        const AttemptResult* r = rig.result("mech.flipcancel");
        if (n == 1) CHECK_MSG(r && r->success, describe(rig));
        else CHECK_MSG(r && !r->success && firstTip(r) == "me.fc_late", describe(rig));
    }
    Rig rig;
    CHECK(rig.startDrill("flipcancel"));
    play(rig, [](int, MiniSim&) {
        Inputs in;
        in.throttle = 1.f;
        return in;
    });
    CHECK_EQ(rig.count("mech.flipcancel"), 1);
    CHECK_MSG(firstTip(rig.result("mech.flipcancel")) == "me.fc_no_flip", describe(rig));
}

// ---- Wavedash ---------------------------------------------------------------------------------------------------
namespace {
// Drop wavedash answering `react` ticks after GO: tilt the nose to `tiltDeg`, dash when the rear wheels are about to be
// `rearZ` uu above the floor.
Script dropDashBot(int react, float tiltDeg, float rearZ) {
    auto S = std::make_shared<WdS>();
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (S->phase == 0 && t >= react && c.wheelCount() == 0) {
            in.pitch = pitchToNose(c, tiltDeg);
            if (c.vel.z < 0.f && c.rearWheelZ() + c.vel.z * k::kDt < rearZ) {
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
int firstContactTicks(Rig& rig) {
    for (int i = 0; i < 300; ++i) {
        if (rig.sim.wheelCount() > 0) return i;
        rig.tick(Inputs{});
    }
    return -1;
}
} // namespace

TEST(play_wavedash_drop_timing) {
    // The car is dropped on GO: the wheels touch about 1 s later at niveau 1, 0.6 s at niveau 10.
    for (int n : {1, 10}) {
        Rig rig;
        CHECK(rig.startDrill("wavedash", levelOf(n)));
        const int contact = firstContactTicks(rig);
        CHECK_MSG(contact >= (n == 1 ? 114 : 66), std::to_string(contact));
    }
    // A player answering 0.3, 0.5 or 0.7 s after GO (tilt 15 degrees, dash when the rear wheels are 20 uu up) succeeds.
    for (int react : {36, 60, 84}) {
        Rig rig;
        CHECK(rig.startDrill("wavedash"));
        play(rig, dropDashBot(react, 15.f, 20.f), 4.f);
        const AttemptResult* r = rig.result("mech.wavedash");
        CHECK_MSG(r && r->success, std::to_string(react) + describe(rig));
    }
    // Too late (the wheels are already down): a recorded failure with its tip.
    Rig late;
    CHECK(late.startDrill("wavedash"));
    auto landed = std::make_shared<int>(-1);
    play(late, [landed](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (*landed < 0 && c.wheelCount() >= 3) *landed = t;
        if (*landed >= 0 && t >= *landed + 3 && t < *landed + 6) {
            in.jump = true;
            in.pitch = -1.f;
        }
        return in;
    }, 4.f);
    CHECK_MSG(firstTip(late.result("mech.wavedash")) == "me.wd_late_jumped", describe(late));
}

TEST(play_wavedash_now_cue) {
    // Niveaux 1 to 3: "Maintenant !" shows up shortly before the rear wheels touch, never long before.
    Rig rig;
    CHECK(rig.startDrill("wavedash"));
    int cueAt = -1, contact = -1;
    for (int i = 0; i < 240 && contact < 0; ++i) {
        rig.tick(Inputs{});
        if (rig.sim.wheelCount() > 0) contact = i;
        DrawList dl;
        ViewContext view;
        view.valid = true;
        DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, true, 1.f, true};
        rig.mod.draw(view, dctx, dl);
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines)
                if (l.text == "Maintenant !" && cueAt < 0) cueAt = i;
    }
    CHECK(cueAt >= 0 && contact > cueAt);
    CHECK_MSG(contact - cueAt >= 24 && contact - cueAt <= 34, std::to_string(contact - cueAt));  // about 0.25 s
}

// ---- Chain wavedash ---------------------------------------------------------------------------------------------
namespace {
Script chainBot(int groundWait) {
    auto S = std::make_shared<WdS>();
    auto waitStart = std::make_shared<int>(-1);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.handbrake = true;
        if (S->phase == 0 && c.wheelCount() >= 3) {
            if (*waitStart < 0) *waitStart = t;
            if (t - *waitStart >= groundWait) {
                S->phase = 1;
                S->jumpT = t;
                *waitStart = -1;
            }
        }
        if (S->phase == 1) {
            in.jump = t - S->jumpT < 3;
            if (t - S->jumpT >= 3) {
                in.pitch = pitchToNose(c, 18.f);
                if (c.vel.z < 0.f && c.rearWheelZ() + c.vel.z * k::kDt < 6.f) {
                    S->phase = 2;
                    S->dodgeT = t;
                }
            }
        }
        if (S->phase == 2) {
            in.jump = t - S->dodgeT < 3;
            in.pitch = t - S->dodgeT < 3 ? -1.f : 0.f;
            if (t - S->dodgeT > 3 && c.wheelCount() >= 3) S->phase = 0;
        }
        return in;
    };
}
} // namespace

TEST(play_chaindash_levels) {
    {
        Rig rig;  // niveau 1, a small pause on the ground between the dashes is fine
        CHECK(rig.startDrill("chaindash"));
        play(rig, chainBot(14), 9.f);
        const AttemptResult* r = rig.result("mech.chaindash");
        CHECK_MSG(r && r->success, describe(rig));
    }
    {
        Rig rig;  // niveau 10 asks for no pause
        CHECK(rig.startDrill("chaindash", 1.f));
        play(rig, chainBot(14), 9.f);
        CHECK_MSG(firstTip(rig.result("mech.chaindash")) == "me.chain_ground_time", describe(rig));
    }
    {
        Rig rig;  // niveau 10 from 300 uu/s without pause: supersonic in time
        CHECK(rig.startDrill("chaindash", 1.f));
        const float go = rig.last.time;
        play(rig, chainBot(1), 9.f);
        const AttemptResult* r = rig.result("mech.chaindash");
        CHECK_MSG(r && r->success, describe(rig));
        CHECK(rig.last.time - go <= 4.6f);
    }
    {
        Rig rig;  // the attempt ends at the end of the lane, before the back wall
        CHECK(rig.startDrill("chaindash"));
        for (int i = 0; i < 9 * 120 && rig.mod.status().phase == "Essai en cours"; ++i) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = true;
            rig.tick(in);
        }
        CHECK(rig.mod.status().phase == "Résultat");
        CHECK(-rig.sim.pos.y >= 3800.f && -rig.sim.pos.y < 4300.f);
    }
}

// ---- Zap dash ---------------------------------------------------------------------------------------------------
TEST(play_zapdash_level1) {
    // Dropped nose down: the front wheels touch first; the flip pressed on that first contact, without any jump, is the
    // zap dash (fallback while the suspension bounce is unverified in game).
    Rig rig;
    CHECK(rig.startDrill("zapdash"));
    auto S = std::make_shared<int>(-1);
    play(rig, [S](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (*S < 0 && t >= kReact) in.pitch = pitchToNose(c, -10.f);
        const int w = c.wheelCount();
        if (*S < 0 && w > 0 && w < 3) *S = t;
        if (*S >= 0 && t < *S + 3) {
            in.jump = true;
            in.pitch = -1.f;
        }
        return in;
    }, 4.f);
    const AttemptResult* r = rig.result("mech.zapdash");
    CHECK_MSG(r && r->success, describe(rig));
    // Idle: one recorded failure with the zap tip.
    Rig idleRig;
    CHECK(idleRig.startDrill("zapdash"));
    play(idleRig, idle(), 4.f);
    CHECK_EQ(idleRig.count("mech.zapdash"), 1);
    CHECK_MSG(firstTip(idleRig.result("mech.zapdash")) == "me.zap_no_flip", describe(idleRig));
}

// ---- Wall dash --------------------------------------------------------------------------------------------------
namespace {
// Kinematic wall run on the wall x = -4096 (blue, attempt 0): roll along +Y, tap jump, flip toward the wall `press`
// ticks after the first press, back on the wall 12 ticks later 300 uu/s faster.
void wallRun(Rig& rig, int press) {
    Snapshot s;
    const Vec3 n{1.f, 0.f, 0.f};
    const Vec3 f{0.f, 1.f, 0.f};
    s.car.basis.up = n;
    s.car.basis.fwd = f;
    s.car.basis.right = cross(n, f);
    s.car.groundNormal = n;
    s.car.pos = {-4096.f + 17.f, -2500.f, 700.f};
    s.car.vel = f * 700.f;
    s.car.wheelContacts = 4;
    s.car.hasFlip = true;
    s.in.throttle = 1.f;
    auto step = [&](int k) {
        for (int i = 0; i < k; ++i) {
            s.car.pos += s.car.vel * k::kDt;
            rig.feed(s);
            s.in.jump = false;
        }
    };
    step(20);
    s.in.jump = true;
    s.car.jumped = true;
    s.car.vel += n * 292.f;
    step(1);
    s.car.wheelContacts = 0;
    step(press - 1);
    s.in.jump = true;
    s.in.pitch = -0.7f;
    s.in.roll = -0.7f;
    s.car.dodging = true;
    s.car.hasFlip = false;
    s.car.dodgeDir = {0.707f, -0.707f, 0.f};
    s.car.vel += f * 300.f;
    step(1);
    s.in.pitch = s.in.roll = 0.f;
    s.car.vel -= n * 292.f;
    step(11);
    s.car.wheelContacts = 4;
    s.car.dodging = false;
    s.car.jumped = false;
    s.car.hasFlip = true;
    step(30);
}
} // namespace

TEST(play_walldash_hold_and_dash) {
    // The car is put with its wheels against the wall (repo basis convention) for both walls.
    Rng rng(1);
    for (int attempt : {0, 1}) {
        const DrillSetup d = makeDrill(Focus::WallDash, 0.f, attempt, 0, rng);
        const Vec3 normal = d.car.pos.x < 0.f ? Vec3{1.f, 0.f, 0.f} : Vec3{-1.f, 0.f, 0.f};
        CHECK(dot(basisFromRot(d.car.rot).up, normal) > 0.99f);
        CHECK_NEAR(d.car.pos.z, 700.f, 1e-3f);
    }
    for (int n : {1, 10}) {
        Rig rig;
        CHECK(rig.startDrill("walldash", levelOf(n)));
        wallRun(rig, 25);
        const AttemptResult* r = rig.result("mech.walldash");
        if (n == 1) CHECK_MSG(r && r->success, describe(rig));
        else CHECK_MSG(r && !r->success && firstTip(r) == "me.wall_tap_slow", describe(rig));
    }
}

// ---- Fast aerial ------------------------------------------------------------------------------------------------
TEST(play_fastaerial_levels) {
    {
        // Niveau 1: the ball waits in the air (held still on every tick until touched); a clean fast aerial answering
        // 0.6 s after GO reaches it in time, the ball is never put back after the hit.
        Rig rig;
        CHECK(rig.startDrill("fastaerial"));
        const float go = rig.last.time;
        const Vec3 hang = rig.mod.drillSetup().ball.pos;
        auto bot = fastAerialAt(&rig, 72, 24, 4);
        int afterTouch = 0;
        for (int i = 0; i < 6 * 120 && rig.results.empty(); ++i) {
            const Actions a = rig.tick(bot(i, rig.sim));
            if (!rig.touched) CHECK(length(rig.ballGame - hang) < 1.f);
            else if (a.setBall) ++afterTouch;
        }
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK_MSG(r && r->success, describe(rig));
        CHECK(rig.touched && rig.touchTime - go <= 2.f);
        CHECK(afterTouch <= 1);  // at most the tick the rig resolved the hit on
    }
    {
        // A double jump without boost never gets that high.
        Rig rig;
        CHECK(rig.startDrill("fastaerial"));
        play(rig, [](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            in.jump = (t >= 48 && t < 72) || (t >= 76 && t < 78);
            in.pitch = t >= 48 && t < 76 ? 1.f : 0.f;
            return in;
        });
        CHECK(!rig.touched);
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK_MSG(r && !r->success, describe(rig));
    }
    {
        // Niveau 10: the ball falls from 15 m on GO; a clean fast aerial answering 0.5 s after GO touches it above 7 m.
        Rig rig;
        CHECK(rig.startDrill("fastaerial", 1.f));
        play(rig, fastAerialAt(&rig, 60, 24, 4));
        const AttemptResult* r = rig.result("mech.fastaerial");
        CHECK_MSG(r && r->success, describe(rig));
        CHECK(rig.touchBallZ >= 700.f);
    }
    {
        // No double jump: a recorded failure with its tip.
        Rig rig;
        CHECK(rig.startDrill("fastaerial"));
        play(rig, [](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = t >= 48;
            in.jump = t >= 48 && t < 72;
            in.pitch = t >= 48 && t < 90 ? 1.f : 0.f;
            return in;
        });
        CHECK_MSG(firstTip(rig.result("mech.fastaerial")) == "me.fa_no_double_jump", describe(rig));
    }
}

TEST(play_fastaerial_geometry_guard) {
    // Every level: the best of a few plain strategies (throttle from GO or not, drive a little, jump held 0.1 to 0.2 s,
    // aim a little above or below) with a 0.5 s reaction touches the ball within the level's limits. The old niveau 10
    // (touch within 1.6 s) failed this.
    Rng rng(1);
    for (int n = 1; n <= 10; ++n) {
        const DrillSetup d = makeDrill(Focus::FastAerial, levelOf(n), 0, 0, rng);
        float limitTime = 1e9f, minZ = 0.f;
        for (const DrillCheck& c : d.checks) {
            if (c.key && std::string(c.key) == "timeToBall") limitTime = c.hi;
            if (c.key && std::string(c.key) == "ballZAtTouch") minZ = c.lo;
        }
        bool ok = false;
        for (int early = 0; early < 2 && !ok; ++early)
            for (int drive : {0, 10, 20, 35})
                for (int hold : {12, 18, 24})
                    for (float lead : {-150.f, 0.f, 150.f, 300.f}) {
                        if (ok) break;
                        MiniSim c;
                        c.pos = mirrorY(d.car.pos);
                        c.orient(mirrorY(basisFromRot(d.car.rot).fwd), {0.f, 0.f, 1.f});
                        Vec3 ball = mirrorY(d.ball.pos), bv;
                        const int react = 60, J = react + drive, DJ = J + hold + 3;
                        for (int t = 0; t < 600; ++t) {
                            Inputs in;
                            in.throttle = (early || t >= react) ? 1.f : 0.f;
                            in.boost = t >= J;
                            if (t >= J && t < J + hold) {
                                in.jump = true;
                                in.pitch = 1.f;
                            }
                            if (t >= J + hold && t < DJ) in.pitch = 1.f;
                            if (t >= DJ && t < DJ + 2) in.jump = true;
                            if (t >= DJ + 2) {
                                float tau = 0.3f;
                                Vec3 q = ball;
                                for (int it = 0; it < 3; ++it) {
                                    q = ball + bv * tau;
                                    if (!d.hangBall) q.z -= 325.f * tau * tau;
                                    tau = length(q - c.pos) / 1400.f;
                                }
                                q.z += lead;
                                const Vec3 to = normalize(q - c.pos);
                                const Vec3 e = cross(c.fwd(), to) + cross(c.up(), Vec3{0.f, 0.f, 1.f}) * 0.2f;
                                const Vec3 acc = e * 16.f - c.w * 4.5f;
                                in.pitch = clampf(dot(acc, c.right()) / 12.46f, -1.f, 1.f);
                                in.yaw = clampf(dot(acc, -c.up()) / 9.11f, -1.f, 1.f);
                                in.roll = clampf(dot(acc, c.fwd()) / 38.34f, -1.f, 1.f);
                            }
                            c.tick(in);
                            if (!d.hangBall) {
                                bv.z += k::kGravityZ * k::kDt;
                                ball += bv * k::kDt;
                            }
                            const Snapshot s = c.snap(0, in);
                            if (hitboxGap(s.car, mirrorY(ball), Vec3{118.01f, 84.2f, 36.16f} * 0.5f, {13.88f, 0.f, 20.75f}) <= 0.f) {
                                ok = (t + 1) * k::kDt <= limitTime && ball.z >= minZ;
                                break;
                            }
                        }
                    }
        CHECK_MSG(ok, "fast aerial niveau " + std::to_string(n) + " out of reach");
    }
}

// ---- Stall ------------------------------------------------------------------------------------------------------
namespace {
Script stallBot(int pressAt, float pitch = 0.f) {
    return [=](int t, MiniSim&) {
        Inputs in;
        if (t >= pressAt - 12) {
            in.yaw = 1.f;
            in.roll = -1.f;
            in.pitch = pitch;
        }
        in.jump = t >= pressAt && t < pressAt + 3;
        return in;
    };
}
} // namespace

TEST(play_stall_levels) {
    {
        Rig rig;
        CHECK(rig.startDrill("stall"));
        play(rig, stallBot(180));  // 1.5 s after GO
        CHECK_MSG(rig.result("mech.stall") && rig.result("mech.stall")->success, describe(rig));
    }
    {
        Rig rig;
        CHECK(rig.startDrill("stall", 1.f));
        play(rig, stallBot(150));  // 1.25 s after GO at niveau 10 (released 5 m up)
        CHECK_MSG(rig.result("mech.stall") && rig.result("mech.stall")->success, describe(rig));
    }
    {
        Rig rig;  // stick pushed up at the press: a real flip, not a stall
        CHECK(rig.startDrill("stall"));
        play(rig, stallBot(120, 0.5f));
        CHECK_MSG(firstTip(rig.result("mech.stall")) == "me.st_pitch_leak", describe(rig));
    }
}

// ---- Helicopter -------------------------------------------------------------------------------------------------
TEST(play_helicopter_levels) {
    auto spin = [](int t, MiniSim&) {
        Inputs in;
        if (t >= 60 && t < 60 + 72) {
            in.yaw = 1.f;
            in.roll = 1.f;
        }
        return in;
    };
    {
        Rig rig;
        CHECK(rig.startDrill("helicopter"));
        play(rig, spin);
        CHECK_MSG(rig.result("mech.helicopter") && rig.result("mech.helicopter")->success, describe(rig));
    }
    {
        Rig rig;  // 0.6 s of spin is far from the 1.25 s of niveau 10
        CHECK(rig.startDrill("helicopter", 1.f));
        play(rig, spin);
        CHECK_MSG(firstTip(rig.result("mech.helicopter")) == "me.as_short", describe(rig));
    }
    {
        Rig rig;
        CHECK(rig.startDrill("helicopter"));
        play(rig, idle());
        CHECK_EQ(rig.count("mech.helicopter"), 1);
        CHECK_MSG(firstTip(rig.result("mech.helicopter")) == "me.as_no_spin", describe(rig));
    }
}

// ---- Recovery ---------------------------------------------------------------------------------------------------
TEST(play_recovery_throws_stay_in_the_field) {
    // Niveau 10 throws (the most spread): the ballistic flight lands inside the field, before any wall or goal.
    Rng rng(42);
    for (int i = 0; i < 500; ++i) {
        const DrillSetup d = makeDrill(Focus::Recovery, 1.f, i, 0, rng);
        Vec3 p = d.car.pos, v = d.car.vel;
        for (int t = 0; t < 1200 && p.z >= 100.f; ++t) {
            v.z += k::kGravityZ * k::kDt;
            p += v * k::kDt;
        }
        CHECK(std::fabs(p.x) < 4000.f && p.y > -5000.f && p.y < 5000.f);
    }
}

TEST(play_recovery_level1) {
    // 20 consecutive attempts (20 different random throws, each shown held during its 1.5 s countdown): a player who
    // orients the car to its flight direction 0.4 s after GO lands well every time.
    Rig rig;
    rig.set("rlt_me_drill_adaptive", "0");  // stay at niveau 1 for the 20 throws
    CHECK(rig.startDrill("recovery"));
    auto bot = [](int t, MiniSim& c) {
        Inputs in;
        if (t >= kReact && c.wheelCount() < 3) {
            const Vec3 v{c.vel.x, c.vel.y, 0.f};
            orientPD(c, length(v) > 50.f ? v : c.fwd(), in);
        }
        if (c.wheelCount() >= 3) in.throttle = 1.f;
        return in;
    };
    int ok = 0;
    std::vector<Vec3> starts;
    for (int attempt = 0; attempt < 20; ++attempt) {
        starts.push_back(rig.mod.drillSetup().car.pos);
        const size_t before = rig.results.size();
        for (int i = 0; i < 7 * 120 && rig.results.size() == before; ++i) rig.tick(bot(i, rig.sim));
        if (rig.results.size() > before) ok += rig.results.back().success ? 1 : 0;
        // Next attempt: 1.5 s countdown after the reading delay, no wait (the player is active).
        int guard = 0;
        while (rig.mod.status().phase != "Essai en cours" && guard++ < 600) rig.tick(Inputs{});
    }
    CHECK_MSG(ok == 20, std::to_string(ok) + "/20 " + describe(rig));
    CHECK(length(starts[0] - starts[1]) > 1.f && length(starts[1] - starts[2]) > 1.f);  // different throws
}

// ---- Powerslide -------------------------------------------------------------------------------------------------
namespace {
// Turn toward the ball, powerslide until the nose is within 18 degrees of it (the car keeps turning a little after
// the release), or never slide, then drive into it.
Script powerslideBot(const Rig* rig, bool slide, bool wrongWay) {
    auto released = std::make_shared<bool>(false);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        const Vec3 ball = mirrorY(rig->ballGame);
        const Vec3 to = normalize(Vec3{ball.x - c.pos.x, ball.y - c.pos.y, 0.f});
        const Vec3 f = normalize(Vec3{c.fwd().x, c.fwd().y, 0.f});
        const float crossZ = f.x * to.y - f.y * to.x;
        const float ang = std::acos(clampf(dot(f, to), -1.f, 1.f)) * kRadToDeg;
        if (t >= kReact) {
            in.steer = crossZ > 0.f ? -1.f : 1.f;
            if (wrongWay && !*released) in.steer = -in.steer;
            if (ang < 8.f) in.steer = clampf(-crossZ * 6.f, -1.f, 1.f);
            in.handbrake = slide && !*released && (wrongWay ? t < kReact + 40 : ang > 18.f);
            if (slide && !in.handbrake && t > kReact + 4) *released = true;
        }
        return in;
    };
}
} // namespace

TEST(play_powerslide_levels) {
    {
        Rig rig;
        CHECK(rig.startDrill("powerslide"));
        play(rig, powerslideBot(&rig, true, false));
        const AttemptResult* r = rig.result("mech.powerslide");
        CHECK_MSG(r && r->success, describe(rig));
        CHECK(rig.touched);
    }
    {
        Rig rig;  // the slide turns the car away from the ball
        CHECK(rig.startDrill("powerslide"));
        play(rig, powerslideBot(&rig, true, true));
        CHECK_MSG(firstTip(rig.result("mech.powerslide")) == "me.ps_not_aimed", describe(rig));
    }
    {
        Rig rig;  // no powerslide at all: one recorded failure
        CHECK(rig.startDrill("powerslide"));
        play(rig, powerslideBot(&rig, false, false));
        CHECK_EQ(rig.count("mech.powerslide"), 1);
        CHECK_MSG(firstTip(rig.result("mech.powerslide")) == "me.ps_no_slide", describe(rig));
    }
}

// ---- "Parcours complet" -----------------------------------------------------------------------------------------
namespace {
std::string steps(const Rig& rig) {
    const StepTracker& t = rig.mod.fullTracker();
    std::string s;
    for (int i = 0; i < t.size(); ++i) s += " " + t.steps()[static_cast<size_t>(i)].id + "=" + std::to_string(static_cast<int>(t.state(i)));
    if (t.failed()) s += " missing " + t.failure().id + " (" + t.failure().code + ")";
    return s + describe(rig);
}
bool fullOk(const Rig& rig, const std::string& skill) {
    const AttemptResult* r = rig.result(skill);
    return r && r->success && rig.mod.fullTracker().isComplete() && rig.count(skill) == 1;
}
} // namespace

TEST(full_speedflip_kickoff) {
    {
        // A real kickoff with no help: boost held from the countdown, a small steer toward the ball, a short jump,
        // the diagonal flip cancelled at once, back on the wheels and into the ball.
        Rig rig;
        CHECK(rig.startDrill("speedflip", -1.f, true, boostHold()));
        CHECK(rig.mod.drillFull() && rig.mod.drillSetup().kickoff && rig.mod.drillSetup().kickoffRules);
        SpeedflipPlan p;
        p.jumpAt = 60;
        p.hold = 9;
        p.toFlip = 12;
        p.cancelAfter = 5;
        p.angleDeg = -28.f;
        play(rig, speedflip(p));
        CHECK_MSG(fullOk(rig, "mech.speedflip"), steps(rig));
    }
    {
        // No boost at GO: the first step is named, with its tip.
        Rig rig;
        CHECK(rig.startDrill("speedflip", -1.f, true));
        play(rig, [](int, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            return in;
        });
        const AttemptResult* r = rig.result("mech.speedflip");
        CHECK_MSG(r && !r->success && firstTip(r) == "me.sf_no_boost", steps(rig));
        CHECK(rig.mod.fullTracker().failure().id == "sf.boost");
        CHECK(rig.mod.status().message.find("Boost et accélérateur") != std::string::npos);
    }
}

TEST(full_wavedash_from_the_floor) {
    {
        Rig rig;
        CHECK(rig.startDrill("wavedash", -1.f, true));
        CHECK(rig.mod.drillSetup().car.pos.z < 30.f);  // on the floor: the player hops himself
        play(rig, groundWavedash(kReact), 4.f);
        CHECK_MSG(fullOk(rig, "mech.wavedash"), steps(rig));
        CHECK(rig.mod.fullTracker().done("wd.hop") && rig.mod.fullTracker().done("wd.gain"));
    }
    {
        // A full jump instead of a small hop: the hop step fails with its tip.
        Rig rig;
        CHECK(rig.startDrill("wavedash", -1.f, true));
        play(rig, [](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            in.jump = t >= kReact && t < kReact + 24;
            return in;
        }, 4.f);
        CHECK_MSG(firstTip(rig.result("mech.wavedash")) == "me.wd_high_hop", steps(rig));
    }
}

TEST(full_flipcancel_halfflip_stall_helicopter) {
    {
        Rig rig;
        CHECK(rig.startDrill("flipcancel", -1.f, true));
        auto J = std::make_shared<int>(-1);
        play(rig, [J](int t, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            if (*J < 0 && length(c.vel) > 850.f) *J = t;
            if (*J < 0) return in;
            const int D = *J + 10;
            if (t < *J + 4) in.jump = true;
            if (t >= D && t < D + 3) {
                in.jump = true;
                in.pitch = -1.f;
            }
            if (t >= D + 8 && t < D + 38) in.pitch = 1.f;
            else if (t >= D + 38 && c.wheelCount() < 3) in.pitch = pitchToNose(c, 0.f);
            return in;
        }, 6.f);
        CHECK_MSG(fullOk(rig, "mech.flipcancel"), steps(rig));
    }
    {
        Rig rig;  // the player reverses himself, then the half-flip and the drive to the ball
        CHECK(rig.startDrill("halfflip", -1.f, true));
        CHECK(length(rig.sim.vel) < 1.f);
        auto inverted = std::make_shared<bool>(false);
        auto landed = std::make_shared<int>(-1);
        auto J = std::make_shared<int>(-1);
        play(rig, [=](int t, MiniSim& c) {
            Inputs in;
            in.throttle = -1.f;
            if (*J < 0 && -dot(c.vel, c.fwd()) > 400.f) *J = t;
            if (*J < 0) return in;
            const int D = *J + 10;
            if (t < *J + 4) in.jump = true;
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
                    in.pitch = in.roll = 0.f;
                    in.throttle = 1.f;
                    in.boost = true;
                    in.steer = steerTo(c, {0.f, 0.f, 0.f});
                }
            }
            return in;
        }, 7.f);
        CHECK_MSG(fullOk(rig, "mech.halfflip"), steps(rig));
    }
    {
        Rig rig;  // one jump held, then the stall inputs and the press near the top
        CHECK(rig.startDrill("stall", -1.f, true));
        play(rig, [](int t, MiniSim&) {
            Inputs in;
            in.jump = (t >= kReact && t < kReact + 24) || (t >= 100 && t < 103);
            if (t >= 90) {
                in.yaw = 1.f;
                in.roll = -1.f;
            }
            return in;
        }, 4.f);
        CHECK_MSG(fullOk(rig, "mech.stall"), steps(rig));
    }
    {
        Rig rig;  // jump and double jump, spin 0.7 s, then wheels back down
        CHECK(rig.startDrill("helicopter", -1.f, true));
        play(rig, [](int t, MiniSim& c) {
            Inputs in;
            in.jump = (t >= kReact && t < kReact + 24) || (t >= kReact + 28 && t < kReact + 31);
            if (t >= 100 && t < 184) {
                in.yaw = 1.f;
                in.roll = 1.f;
            } else if (t >= 184 && c.wheelCount() < 3) {
                orientPD(c, c.fwd(), in);
            }
            return in;
        }, 6.f);
        CHECK_MSG(fullOk(rig, "mech.helicopter"), steps(rig));
    }
}

TEST(full_boost_powerslide_fastaerial_chain) {
    {
        Rig rig;  // throttle alone to mid speed, boost to supersonic, release at max, stay supersonic
        CHECK(rig.startDrill("boost", -1.f, true));
        play(rig, [](int, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            const float v = length(c.vel);
            in.boost = v >= 1400.f && v < 2290.f;
            return in;
        }, 9.f);
        CHECK_MSG(fullOk(rig, "mech.boost"), steps(rig));
    }
    {
        Rig rig;  // boost held from the start: the throttle step fails
        CHECK(rig.startDrill("boost", -1.f, true));
        play(rig, [](int, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = true;
            return in;
        }, 9.f);
        CHECK_MSG(firstTip(rig.result("mech.boost")) == "me.sm_early_boost", steps(rig));
    }
    {
        Rig rig;  // speed first, then the slide toward the ball
        CHECK(rig.startDrill("powerslide", -1.f, true));
        auto bot = powerslideBot(&rig, true, false);
        play(rig, [bot](int t, MiniSim& c) {
            if (length(c.vel) < 950.f) {
                Inputs in;
                in.throttle = 1.f;
                return in;
            }
            return bot(t + kReact, c);
        }, 7.f);
        CHECK_MSG(fullOk(rig, "mech.powerslide"), steps(rig));
    }
    for (int n : {1, 10}) {
        Rig rig;  // lob from the centre: drive a little, wait under it, take off
        CHECK(rig.startDrill("fastaerial", levelOf(n), true));
        auto bot = fastAerialAt(&rig, 0, 24, 4);
        play(rig, [bot](int t, MiniSim& c) {
            if (t < 180) {
                Inputs in;
                in.throttle = t < 30 ? 1.f : 0.f;
                return in;
            }
            return bot(t - 180, c);
        }, 6.f);
        CHECK_MSG(fullOk(rig, "mech.fastaerial"), "niveau " + std::to_string(n) + steps(rig));
    }
    {
        Rig rig;  // orange team: everything rotated, the lob comes toward +Y
        rig.sim.team = 1;
        rig.last = rig.sim.snap(rig.frame, {});
        CHECK(rig.startDrill("fastaerial", 0.f, true));
        CHECK(rig.mod.drillSetup().mirrored && rig.mod.drillSetup().car.pos.y > 4000.f);
        auto bot = fastAerialAt(&rig, 0, 24, 4);
        play(rig, [bot](int t, MiniSim& c) {
            if (t < 180) {
                Inputs in;
                in.throttle = t < 30 ? 1.f : 0.f;
                return in;
            }
            return bot(t - 180, c);
        }, 6.f);
        CHECK_MSG(fullOk(rig, "mech.fastaerial"), "orange" + steps(rig));
    }
    {
        Rig rig;  // from a standstill, no boost, no pause
        CHECK(rig.startDrill("chaindash", -1.f, true));
        play(rig, chainBot(1), 8.f);
        CHECK_MSG(fullOk(rig, "mech.chaindash"), steps(rig));
    }
}

namespace {
// Kinematic car for the sequences MiniSim cannot make (walls, suspension bounce): integrates position (and gravity in
// the air) and feeds the rig.
struct KinCar {
    Snapshot s;
    bool gravity = false;
    explicit KinCar(const Vec3& pos, const Vec3& up, const Vec3& fwd) {
        s.car.pos = pos;
        s.car.basis.up = up;
        s.car.basis.fwd = fwd;
        s.car.basis.right = cross(up, fwd);
        s.car.groundNormal = up;
        s.car.wheelContacts = 4;
        s.car.hasFlip = true;
    }
    void step(Rig& rig, int n) {
        for (int i = 0; i < n; ++i) {
            if (gravity) s.car.vel.z += k::kGravityZ * k::kDt;
            s.car.pos += s.car.vel * k::kDt;
            rig.feed(s);
            s.in.jump = false;
        }
    }
};
} // namespace

TEST(full_wall_and_recovery_kinematic) {
    {
        // Wall dash (blue, wall x = -4096): on the wall at 3 m, riding along it, then one wall dash (niveau 1).
        Rig rig;
        CHECK(rig.startDrill("walldash", -1.f, true));
        CHECK(rig.mod.drillSetup().car.pos.z < 30.f && rig.mod.drillSetup().runDashes == 1);
        KinCar k({-4096.f + 17.f, -2500.f, 300.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        k.s.car.vel = {0.f, 700.f, 0.f};
        k.s.in.throttle = 1.f;
        k.step(rig, 40);
        CHECK_MSG(rig.mod.fullTracker().done("wall.climb") && rig.mod.fullTracker().done("wall.ride"), steps(rig));
        // Tap jump, flip toward the wall 8 ticks later, wheels back on the wall 12 ticks after, faster.
        k.s.in.jump = true;
        k.s.car.jumped = true;
        k.s.car.vel += Vec3{292.f, 0.f, 0.f};
        k.step(rig, 1);
        k.s.car.wheelContacts = 0;
        k.step(rig, 7);
        k.s.in.jump = true;
        k.s.in.pitch = -0.7f;
        k.s.in.roll = -0.7f;
        k.s.car.dodging = true;
        k.s.car.hasFlip = false;
        k.s.car.dodgeDir = {0.707f, -0.707f, 0.f};
        k.s.car.vel += Vec3{0.f, 300.f, 0.f};
        k.step(rig, 1);
        k.s.in.pitch = k.s.in.roll = 0.f;
        k.s.car.vel -= Vec3{292.f, 0.f, 0.f};
        k.step(rig, 11);
        k.s.car.wheelContacts = 4;
        k.s.car.dodging = false;
        k.s.car.jumped = false;
        k.s.car.hasFlip = true;
        k.step(rig, 30);
        CHECK_MSG(fullOk(rig, "mech.walldash"), steps(rig));
    }
    {
        // Recovery (blue, wall x = -4096): on the wall 5 m up, jump off it, fly, land on the wheels in the motion.
        Rig rig;
        CHECK(rig.startDrill("recovery", -1.f, true));
        KinCar k({-4096.f + 17.f, -2000.f, 500.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        k.s.car.vel = {0.f, 600.f, 0.f};
        k.s.in.throttle = 1.f;
        k.step(rig, 10);
        CHECK_MSG(rig.mod.fullTracker().done("rc.climb"), steps(rig));
        k.s.in.jump = true;
        k.s.car.jumped = true;
        k.s.car.vel = {500.f, 600.f, 0.f};
        k.step(rig, 1);
        // In the air: upright, nose along the flight direction.
        k.s.car.wheelContacts = 0;
        k.s.car.basis.up = {0.f, 0.f, 1.f};
        k.s.car.basis.fwd = normalize(Vec3{500.f, 600.f, 0.f});
        k.s.car.basis.right = cross(k.s.car.basis.up, k.s.car.basis.fwd);
        k.s.car.groundNormal = {0.f, 0.f, 1.f};
        k.gravity = true;
        for (int i = 0; i < 300 && k.s.car.pos.z > 17.f; ++i) k.step(rig, 1);
        k.gravity = false;
        k.s.car.pos.z = 17.f;
        k.s.car.vel.z = 0.f;
        k.s.car.wheelContacts = 4;
        k.s.car.jumped = false;
        k.step(rig, 120);
        CHECK_MSG(fullOk(rig, "mech.recovery"), steps(rig));
    }
}

TEST(full_zapdash_kinematic_bounce) {
    // Zap dash "parcours complet": small hop, nose a little down at the first contact, the suspension bounce (wheels
    // under 3 again 6 ticks after the landing, UNVERIFIED in game), flip forward during it without a new jump.
    Rig rig;
    CHECK(rig.startDrill("zapdash", -1.f, true));
    KinCar k({0.f, -2500.f, 17.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f});
    k.s.car.vel = {0.f, 1000.f, 0.f};
    k.s.in.throttle = 1.f;
    k.step(rig, 10);
    // Hop: 6 ticks of jump, up to about 90 uu, then down.
    k.s.in.jump = true;
    k.s.car.jumped = true;
    k.s.car.vel.z = 330.f;
    k.gravity = true;
    k.step(rig, 1);
    k.s.car.wheelContacts = 0;
    for (int i = 0; i < 5; ++i) {
        k.s.in.jump = true;
        k.step(rig, 1);
    }
    const float nose = -8.f * kDegToRad;
    for (int i = 0; i < 200 && k.s.car.pos.z > 25.f; ++i) {
        if (k.s.car.vel.z < 0.f) {
            k.s.car.basis.fwd = {0.f, std::cos(nose), std::sin(nose)};
            k.s.car.basis.up = {0.f, -std::sin(nose), std::cos(nose)};
            k.s.car.basis.right = cross(k.s.car.basis.up, k.s.car.basis.fwd);
        }
        k.step(rig, 1);
    }
    // Front wheels first, then all four (landing), then the bounce.
    k.gravity = false;
    k.s.car.vel.z = 0.f;
    k.s.car.pos.z = 20.f;
    k.s.car.wheelContacts = 2;
    k.step(rig, 1);
    k.s.car.basis.fwd = {0.f, 1.f, 0.f};
    k.s.car.basis.up = {0.f, 0.f, 1.f};
    k.s.car.basis.right = cross(k.s.car.basis.up, k.s.car.basis.fwd);
    k.s.car.pos.z = 17.f;
    k.s.car.wheelContacts = 4;
    k.s.car.jumped = false;
    k.step(rig, 6);
    k.s.car.wheelContacts = 1;
    k.s.car.pos.z = 19.f;
    k.step(rig, 2);
    k.s.in.jump = true;
    k.s.in.pitch = -1.f;
    k.s.car.dodging = true;
    k.s.car.hasFlip = false;
    k.s.car.dodgeDir = {1.f, 0.f, 0.f};
    k.s.car.vel += Vec3{0.f, 450.f, 0.f};
    k.step(rig, 1);
    k.s.in.pitch = 0.f;
    k.step(rig, 6);
    k.s.car.wheelContacts = 4;
    k.s.car.pos.z = 17.f;
    k.s.car.dodging = false;
    k.s.car.hasFlip = true;
    k.step(rig, 30);
    CHECK_MSG(fullOk(rig, "mech.zapdash"), steps(rig));
}

TEST(full_sequence_hud_and_staircase) {
    // The "parcours complet" has its own staircase (a failure there never lowers the regular drill), and its HUD keeps
    // at most three main lines: the step to do now (accent), the step that just turned green, the time left.
    Rig rig;
    CHECK(rig.startDrill("wavedash", -1.f, true));
    const float regular = rig.mod.level(Focus::Wavedash);
    for (int i = 0; i < 60; ++i) {
        rig.tick(groundWavedash(kReact)(i, rig.sim));
        DrawList dl;
        ViewContext view;
        view.valid = true;
        DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, true, 1.f, true};
        rig.mod.draw(view, dctx, dl);
        CHECK(dl.panels.size() == 1);
        if (dl.panels.empty()) continue;
        int mains = 0;
        for (const auto& l : dl.panels[0].lines) mains += l.detail ? 0 : 1;
        CHECK(mains >= 1 && mains <= 3);
        CHECK(dl.panels[0].title.find("Parcours complet") == 0);
    }
    play(rig, [](int, MiniSim&) { return Inputs{}; }, 4.f);  // no hop at all: fails
    CHECK(!rig.results.empty() && !rig.results.back().success);
    CHECK_NEAR(rig.mod.level(Focus::Wavedash), regular, 1e-6f);
    CHECK_NEAR(rig.mod.fullLevel(Focus::Wavedash), 0.f, 1e-6f);
    // Saved under its own key.
    Store st;
    rig.mod.save(st);
    CHECK(st.getInt("mechanics.full.wavedash.attempts", 0) == 1);
}
