// Detector tests: synthetic good and bad executions of every movement mechanic through MiniSim (RocketSim rules)
// or kinematic tick sequences, ported from the reference implementation tests, plus arbitration checks.
#include <memory>
#include <string>

#include "tests/TestFramework.h"
#include "tests/mechanics/MiniSim.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

namespace {

std::string describe(const std::vector<MechGrade>& grades) {
    std::string s;
    for (const auto& g : grades) {
        s += std::string(" [") + mechInfo(g.mech).key + " " + g.variant + " score=" + std::to_string(static_cast<int>(g.score)) +
             (g.success ? " OK" : " FAIL");
        for (const auto& e : g.errors) s += std::string(" ") + e.code;
        s += "]";
    }
    return s.empty() ? std::string(" (no grade)") : s;
}

struct WdState {
    int phase = 0, dodgeT = -1, jumpT = -1;
};

// Forward wavedash: dodge when the rear wheels are `dodgeRearZ` from the floor, nose held at `tiltDeg`.
Script wavedashScript(float dodgeRearZ, float tiltDeg, bool handbrake, int startTick = 10) {
    auto S = std::make_shared<WdState>();
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.handbrake = handbrake && S->phase >= 2;
        if (S->phase == 0 && t >= startTick) {
            S->phase = 1;
            S->jumpT = t;
        }
        if (S->phase == 1) {
            in.jump = t - S->jumpT < 3;
            if (t - S->jumpT >= 3) {
                in.pitch = pitchToNose(c, tiltDeg);
                if (c.vel.z < 0.f && c.rearWheelZ() < dodgeRearZ) {
                    S->phase = 2;
                    S->dodgeT = t;
                }
            }
        }
        if (S->phase == 2) {
            in.jump = t - S->dodgeT < 3;
            in.pitch = t - S->dodgeT < 3 ? -1.f : 0.f;
        }
        return in;
    };
}

// SpeedFlipTrainer 26-degree bot timeline (jump 59, held 11 ticks, dodge, cancel after `cancelAfter` ticks).
Script speedflipScript(float angleDeg, int cancelAfter, int boostUntil, bool boostAtStart = true) {
    auto passedInverted = std::make_shared<bool>(false);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = (boostAtStart || t > 3) && t < boostUntil;
        const int J = 59, JD = 11, D = J + JD + 1;
        if (t < J) {
            in.steer = 0.03f;
        } else if (t < J + JD) {
            in.jump = true;
            in.steer = in.yaw = 0.25f * sgn(angleDeg == 0.f ? 1.f : -angleDeg);
        } else if (t < D) {
        } else if (t < D + cancelAfter) {
            in.jump = t < D + 5;
            const float a = angleDeg * kDegToRad;
            in.yaw = std::sin(a);
            in.pitch = -std::cos(a);
        } else {
            in.pitch = 1.f;
            if (c.up().z < -0.3f) *passedInverted = true;
            const float side = sgn(angleDeg == 0.f ? 1.f : angleDeg);
            in.roll = side;
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

Script halfflipScript(int cancelAfter, bool doRoll, bool cancel = true) {
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
            const bool cancelling = cancel && t >= D + cancelAfter;
            in.pitch = cancelling ? -1.f : 1.f;
            if (doRoll && cancelling) {
                if (c.up().z < 0.f) *inverted = true;
                if (*inverted) in.roll = c.up().z > 0.6f ? rollToLevel(c) : 1.f;
            }
            if (t > D + 10 && c.wheelCount() >= 3 && *landed < 0) *landed = t;
            if (*landed >= 0) {
                in.pitch = 0.f;
                in.roll = 0.f;
                in.throttle = 1.f;
            }
        }
        return in;
    };
}

Script aerialScript(int hold, int gap, int boostFrom, bool pitchAtDJ, float stickAtDJ = 0.f) {
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
            in.pitch = pitchAtDJ ? 1.f : stickAtDJ;
        }
        if (t >= DJ + 2 && t < DJ + 30) in.pitch = 1.f;
        return in;
    };
}

Script chainScript(int groundWait, bool boost) {
    auto S = std::make_shared<WdState>();
    auto waitStart = std::make_shared<int>(-1);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.handbrake = true;
        in.boost = boost && S->phase == 0;
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
                if (c.vel.z < 0.f && c.rearWheelZ() < 6.f) {
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

Inputs throttleOnly(int, MiniSim&) {
    Inputs in;
    in.throttle = 1.f;
    return in;
}

DetectConfig withFocus(Focus f) {
    DetectConfig c;
    c.focus = f;
    return c;
}

} // namespace

// ---- Wavedash -------------------------------------------------------------------------------------------
TEST(mech_wavedash_good_forward) {
    Run r;
    r.sim.vel = {1000.f, 0.f, 0.f};
    r.go(160, wavedashScript(6.f, 20.f, false));
    const MechGrade* g = r.find(Mech::Wavedash);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK_MSG(g->success, describe(r.grades));
        CHECK(g->score >= 75.f);
        CHECK(g->variant == "forward");
        CHECK(g->metric("gainAlongDash") > 350.f);
        CHECK(g->errors.empty());
    }
    CHECK(r.count(Mech::Recovery) == 0);    // a short hop is not a recovery
    CHECK(r.count(Mech::FastAerial) == 0);  // nor a fast aerial
    CHECK(r.count(Mech::FlipCancel) == 0);  // one report per dodge
}

TEST(mech_wavedash_early_dodge_penalized) {
    Run r;
    r.sim.vel = {1000.f, 0.f, 0.f};
    r.go(160, wavedashScript(30.f, 5.f, false));
    const MechGrade* g = r.find(Mech::Wavedash);
    if (g) {
        CHECK_MSG(!g->success || g->score < 70.f, describe(r.grades));
        CHECK_MSG(g->hasError("WD_NOSE_DOWN") || g->hasError("WD_TOO_EARLY") || g->hasError("WD_CRASH_LANDING"),
                  describe(r.grades));
    }
}

TEST(mech_wavedash_late_press_needs_intent) {
    auto lateScript = [](std::shared_ptr<int> landT) {
        return [landT](int t, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            if (t >= 10 && t < 13) in.jump = true;
            if (t > 20 && *landT < 0 && c.wheelCount() >= 3) *landT = t;
            if (*landT >= 0 && t >= *landT + 2 && t < *landT + 5) {
                in.jump = true;
                in.pitch = -1.f;
            }
            return in;
        };
    };
    {
        Run r(withFocus(Focus::Wavedash));
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(160, lateScript(std::make_shared<int>(-1)));
        const MechGrade* g = r.find(Mech::Wavedash);
        CHECK_MSG(g && g->hasError("WD_LATE_JUMPED") && !g->success, describe(r.grades));
    }
    {
        Run r;  // no intent: a jump right after landing is just a jump
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(160, lateScript(std::make_shared<int>(-1)));
        bool late = false;
        for (const auto& g : r.grades) late = late || g.variant == "late";
        CHECK_MSG(!late, describe(r.grades));
    }
}

TEST(mech_wavedash_side_needs_powerslide) {
    for (bool hb : {false, true}) {
        Run r;
        r.sim.vel = {1000.f, 0.f, 0.f};
        auto S = std::make_shared<WdState>();
        r.go(160, [=](int t, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            in.handbrake = hb && S->phase >= 2;
            if (S->phase == 0 && t >= 10) {
                S->phase = 1;
                S->jumpT = t;
            }
            if (S->phase == 1) {
                in.jump = t - S->jumpT < 3;
                if (t - S->jumpT >= 3) {
                    in.roll = rollToLeftDown(c, 15.f);
                    auto p = c.wheels();
                    const float leftZ = std::min(p[1].z, p[3].z);
                    if (c.vel.z < 0.f && leftZ < 6.f) {
                        S->phase = 2;
                        S->dodgeT = t;
                    }
                }
            }
            if (S->phase == 2) {
                in.jump = t - S->dodgeT < 3;
                in.yaw = t - S->dodgeT < 3 ? 1.f : 0.f;
            }
            return in;
        });
        const MechGrade* g = r.find(Mech::Wavedash);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g) {
            CHECK(g->variant == "side-right");
            if (hb) CHECK_MSG(g->metric("gainRatio") > 0.7f, describe(r.grades));
            else CHECK_MSG(g->hasError("WD_SIDE_NO_POWERSLIDE"), describe(r.grades));
        }
    }
}

TEST(mech_wavedash_centered_stick_is_double_jump) {
    // Dropped from a small height (no jump), jump pressed near the ground with the stick centred.
    Run r(withFocus(Focus::Wavedash));
    r.sim.pos = {0.f, 0.f, 120.f};
    r.sim.vel = {900.f, 0.f, -100.f};
    auto pressed = std::make_shared<bool>(false);
    r.go(120, [pressed](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (!*pressed && c.vel.z < 0.f && c.rearWheelZ() < 10.f) {
            *pressed = true;
            in.jump = true;
        }
        return in;
    });
    const MechGrade* g = r.find(Mech::Wavedash);
    CHECK_MSG(g && g->hasError("WD_NO_STICK"), describe(r.grades));
}

// ---- Chain wavedash ------------------------------------------------------------------------------------
TEST(mech_chain_wavedash_good) {
    Run r;
    r.sim.vel = {600.f, 0.f, 0.f};
    r.go(520, chainScript(2, false));
    r.go(80, throttleOnly);
    r.flush();
    const MechGrade* g = r.find(Mech::ChainWavedash);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK(g->metric("dashes") >= 3.f);
        CHECK(g->metric("maxSpeed") >= 2200.f);
        CHECK_MSG(g->success, describe(r.grades));
        CHECK(g->hasMetric("timeToSupersonic"));
    }
    CHECK(r.count(Mech::Wavedash) >= 3);  // the parts are graded too (the chain is an aggregate)
}

TEST(mech_chain_wavedash_slow_and_boosted) {
    Run r;
    r.sim.vel = {600.f, 0.f, 0.f};
    r.go(520, chainScript(18, true));
    r.go(80, throttleOnly);
    r.flush();
    const MechGrade* g = r.find(Mech::ChainWavedash);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK(g->hasError("CHAIN_GROUND_TIME"));
        CHECK(g->hasError("CHAIN_BOOST_USED"));
        CHECK(!g->success);
    }
}

TEST(mech_chain_respects_configured_gap) {
    DetectConfig cfg;
    cfg.chainMaxGapTicks = 10;  // 18 ticks on the ground now breaks the chain
    Run r(cfg);
    r.sim.vel = {600.f, 0.f, 0.f};
    r.go(520, chainScript(18, false));
    r.go(80, throttleOnly);
    r.flush();
    CHECK_MSG(r.count(Mech::ChainWavedash) == 0, describe(r.grades));
}

// ---- Speedflip -----------------------------------------------------------------------------------------
TEST(mech_speedflip_good_left) {
    Run r;
    r.go(260, speedflipScript(-26.f, 5, 100000));
    const MechGrade* g = r.find(Mech::SpeedFlip);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK(g->variant == "left");
        CHECK_NEAR(g->metric("dodgeAngleDeg"), -26.f, 1.5f);
        CHECK(g->metric("cancelTicks") <= 6.f);
        CHECK_MSG(g->score >= 70.f, describe(r.grades));
    }
    CHECK(r.count(Mech::FastAerial) == 0);
    CHECK(r.count(Mech::FlipCancel) == 0);  // the dodge is reported once, as a speedflip
}

TEST(mech_speedflip_good_right) {
    Run r;
    r.go(260, speedflipScript(32.f, 5, 100000));
    const MechGrade* g = r.find(Mech::SpeedFlip);
    CHECK_MSG(g && g->variant == "right", describe(r.grades));
}

TEST(mech_speedflip_bad_straight_late_cancel_needs_intent) {
    {
        Run r(withFocus(Focus::SpeedFlip));
        r.go(260, speedflipScript(0.f, 15, 80));
        const MechGrade* g = r.find(Mech::SpeedFlip);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g) {
            CHECK(!g->success);
            CHECK(g->hasError("SF_LATE_CANCEL"));
            CHECK(g->hasError("SF_ANGLE_STRAIGHT"));
            CHECK(g->hasError("SF_BOOST_RELEASED"));
        }
    }
    {
        Run r;  // without focus a straight flip is not a speedflip
        r.go(260, speedflipScript(0.f, 15, 80));
        CHECK_MSG(r.count(Mech::SpeedFlip) == 0, describe(r.grades));
    }
}

TEST(mech_speedflip_too_wide) {
    Run r;
    r.go(260, speedflipScript(-60.f, 4, 100000));
    const MechGrade* g = r.find(Mech::SpeedFlip);
    CHECK_MSG(g && g->hasError("SF_ANGLE_WIDE"), describe(r.grades));
}

TEST(mech_speedflip_target_angle_from_config) {
    DetectConfig cfg;
    cfg.speedflipTargetAngleDeg = 45.f;
    Run r(cfg);
    r.go(260, speedflipScript(-26.f, 5, 100000));
    const MechGrade* g = r.find(Mech::SpeedFlip);
    CHECK_MSG(g && g->hasError("SF_ANGLE_STRAIGHT"), describe(r.grades));  // 26 is too straight for a 45 target
}

TEST(mech_speedflip_kickoff_time_to_ball) {
    for (bool boostAtStart : {true, false}) {
        Run r;
        r.ballInSnapshot = true;          // ball still at the centre spot
        r.ballPos = {0.f, 0.f, 92.75f};
        r.sim.pos = {-2600.f, 0.f, k::kCarRestZ};
        r.touchRadius = 450.f;
        r.go(20, [](int, MiniSim&) { return Inputs{}; });  // still car: kickoff armed
        CHECK(r.coach.kickoff().phase() == KickoffPhase::Countdown);
        r.go(600, speedflipScript(-26.f, 5, 100000, boostAtStart));
        const MechGrade* g = r.find(Mech::SpeedFlip);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g) {
            CHECK(g->variant == "left-kickoff");
            CHECK_MSG(g->metric("timeToBall") > 1.0f && g->metric("timeToBall") < 3.5f, describe(r.grades));
            CHECK(g->hasError("KO_NO_BOOST_AT_START") == !boostAtStart);
            CHECK(g->metric("kickoffFirstJumpTicks") >= 50.f);
        }
    }
}

// ---- Half-flip -----------------------------------------------------------------------------------------
TEST(mech_halfflip_good) {
    Run r;
    r.sim.vel = {-800.f, 0.f, 0.f};
    r.go(260, halfflipScript(24, true));
    const MechGrade* g = r.find(Mech::HalfFlip);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK(g->metric("headingChangeDeg") > 150.f);
        CHECK_MSG(g->success, describe(r.grades));
    }
}

TEST(mech_halfflip_early_cancel) {
    Run r;
    r.sim.vel = {-800.f, 0.f, 0.f};
    r.go(260, halfflipScript(3, true));
    const MechGrade* g = r.find(Mech::HalfFlip);
    CHECK_MSG(g && g->hasError("HF_EARLY_CANCEL"), describe(r.grades));
}

TEST(mech_halfflip_plain_backflip_ignored_without_intent) {
    Run r;
    r.sim.vel = {-800.f, 0.f, 0.f};
    r.go(260, halfflipScript(24, false, false));
    CHECK_MSG(r.count(Mech::HalfFlip) == 0, describe(r.grades));
    CHECK_MSG(r.count(Mech::FlipCancel) == 0, describe(r.grades));
}

TEST(mech_halfflip_late_push_without_roll_is_not_a_halfflip) {
    Run r;
    r.sim.vel = {-800.f, 0.f, 0.f};
    r.go(300, halfflipScript(45, false));  // pushes forward late, never rolls
    CHECK_MSG(r.count(Mech::HalfFlip) == 0, describe(r.grades));
    Run f(withFocus(Focus::HalfFlip));     // in focus it is graded (late cancel, no roll)
    f.sim.vel = {-800.f, 0.f, 0.f};
    f.go(300, halfflipScript(45, false));
    const MechGrade* g = f.find(Mech::HalfFlip);
    CHECK_MSG(g && g->hasError("HF_LATE_CANCEL") && g->hasError("HF_NO_ROLL"), describe(f.grades));
}

TEST(mech_halfflip_plain_backflip_in_focus_is_failure) {
    Run r(withFocus(Focus::HalfFlip));
    r.sim.vel = {-800.f, 0.f, 0.f};
    r.go(320, halfflipScript(24, false, false));
    const MechGrade* g = r.find(Mech::HalfFlip);
    CHECK_MSG(g && g->hasError("HF_NO_CANCEL") && !g->success, describe(r.grades));
}

// ---- Flip cancel ---------------------------------------------------------------------------------------
TEST(mech_flip_cancel_forward_flip) {
    auto script = [](int cancelAfter, bool cancel) {
        return [=](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            if (t >= 10 && t < 13) in.jump = true;
            if (t >= 20 && t < 23) {
                in.jump = true;
                in.pitch = -1.f;  // forward flip, no boost: not a speedflip
            }
            if (cancel && t >= 20 + cancelAfter && t < 20 + cancelAfter + 90) in.pitch = 1.f;  // held until landing
            return in;
        };
    };
    {
        Run r;
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(300, script(4, true));
        const MechGrade* g = r.find(Mech::FlipCancel);
        CHECK_MSG(g && g->success && g->variant == "forward", describe(r.grades));
        CHECK(r.count(Mech::SpeedFlip) == 0);
    }
    {
        Run r;
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(300, script(14, true));
        const MechGrade* g = r.find(Mech::FlipCancel);
        CHECK_MSG(g && g->hasError("FC_LATE") && !g->success, describe(r.grades));
    }
    {
        Run r;  // plain forward flip: nothing to grade
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(300, script(0, false));
        CHECK_MSG(r.count(Mech::FlipCancel) == 0, describe(r.grades));
    }
    {
        Run r(withFocus(Focus::FlipCancel));  // in focus, a missing cancel is a failure
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(300, script(0, false));
        const MechGrade* g = r.find(Mech::FlipCancel);
        CHECK_MSG(g && g->hasError("FC_NO_CANCEL"), describe(r.grades));
    }
}

// ---- Fast aerial ---------------------------------------------------------------------------------------
TEST(mech_fast_aerial_good) {
    Run r;
    r.go(120, aerialScript(24, 5, 10, false));
    const MechGrade* g = r.find(Mech::FastAerial);
    CHECK_MSG(g && g->success && g->score >= 85.f, describe(r.grades));
    if (g) CHECK(!g->hasError("FA_STICK_NEAR_DEADZONE"));
}

TEST(mech_fast_aerial_bad_timing) {
    Run r(withFocus(Focus::FastAerial));
    r.go(120, aerialScript(8, 25, 30, false));
    const MechGrade* g = r.find(Mech::FastAerial);
    CHECK_MSG(g != nullptr, describe(r.grades));
    if (g) {
        CHECK(!g->success);
        CHECK(g->hasError("FA_HOLD_SHORT") && g->hasError("FA_GAP_SLOW"));
    }
}

TEST(mech_fast_aerial_accidental_dodge_reported_once) {
    Run r;
    r.go(260, aerialScript(24, 5, 10, true));
    const MechGrade* g = r.find(Mech::FastAerial);
    CHECK_MSG(g && g->hasError("FA_ACCIDENTAL_DODGE"), describe(r.grades));
    // The accidental backflip is claimed by the fast aerial: no half-flip / flip cancel / stall for it.
    CHECK_MSG(r.grades.size() == 1, describe(r.grades));
}

TEST(mech_double_jump_aerial) {
    Run r;
    r.go(120, aerialScript(22, 4, 10 + 22 + 4 + 2, false));
    const MechGrade* g = r.find(Mech::DoubleJumpAerial);
    CHECK_MSG(g && g->success, describe(r.grades));
}

TEST(mech_fast_aerial_deadzone_near_miss_uses_config) {
    for (float dz : {0.5f, 0.9f}) {
        DetectConfig cfg;
        cfg.dodgeDeadzone = dz;
        Run r(cfg);
        r.sim.deadzone = dz;
        r.go(120, aerialScript(24, 5, 10, false, 0.4f));  // stick at 0.4 on the second press
        const MechGrade* g = r.find(Mech::FastAerial);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g) CHECK(g->hasError("FA_STICK_NEAR_DEADZONE") == (dz < 0.55f));
    }
}

// ---- Stall / air spin ----------------------------------------------------------------------------------
namespace {
Script stallScript(float yaw, float roll, bool useDJ) {
    return [=](int t, MiniSim&) {
        Inputs in;
        if (t >= 10 && t < 34) in.jump = true;
        if (useDJ && t >= 40 && t < 42) in.jump = true;
        if (t >= 70 && t < 73) {
            in.jump = true;
            in.yaw = yaw;
            in.roll = roll;
        }
        if (t >= 73 && t < 140) {
            in.yaw = yaw;
            in.roll = roll;
        }
        return in;
    };
}
} // namespace

TEST(mech_stall_good) {
    Run r;
    r.go(200, stallScript(1.f, -1.f, false));
    const MechGrade* g = r.find(Mech::Stall);
    CHECK_MSG(g && g->success && g->variant == "airroll-left", describe(r.grades));
    if (g) CHECK(g->metric("hoverTicks") >= 30.f);
    CHECK(r.count(Mech::AirSpin) == 0);
}

TEST(mech_stall_mismatch) {
    Run r;
    r.go(330, stallScript(1.f, -0.6f, false));
    const MechGrade* g = r.find(Mech::Stall);
    CHECK_MSG(g && g->hasError("ST_YAW_ROLL_MISMATCH") && !g->success, describe(r.grades));
    // The side dodge it produced is not also reported as another mechanic.
    CHECK_MSG(r.count(Mech::Wavedash) == 0 && r.count(Mech::FlipCancel) == 0, describe(r.grades));
}

TEST(mech_stall_no_flip) {
    Run r;
    r.go(200, stallScript(1.f, -1.f, true));
    const MechGrade* g = r.find(Mech::Stall);
    CHECK_MSG(g && g->hasError("ST_NO_FLIP"), describe(r.grades));
}

TEST(mech_helicopter) {
    Run r;
    r.sim.pos = {0.f, 0.f, 900.f};
    r.sim.vel = {0.f, 0.f, 300.f};
    r.go(90, [](int t, MiniSim&) {
        Inputs in;
        if (t < 70) {
            in.yaw = 1.f;
            in.roll = 1.f;
        }
        return in;
    });
    const MechGrade* g = r.find(Mech::AirSpin);
    CHECK_MSG(g && g->variant == "helicopter", describe(r.grades));
}

TEST(mech_air_spin_ignores_speedflip_barrel_roll) {
    Run r;
    r.go(260, speedflipScript(-26.f, 5, 100000));
    CHECK_MSG(r.count(Mech::AirSpin) == 0, describe(r.grades));
}

// ---- Recovery ------------------------------------------------------------------------------------------
namespace {
void recoverySetup(MiniSim& c) {
    c.pos = {0.f, 0.f, 650.f};
    c.vel = {700.f, 300.f, 300.f};
    const Vec3 f = normalize(Vec3{0.3f, -0.5f, 0.8f});
    const Vec3 u = normalize(cross(f, Vec3{1.f, 0.f, 0.f}));
    c.orient(f, u);
    c.w = {1.5f, -1.f, 2.f};
    c.hasJumped = true;  // jumped long ago: no flip left
    c.airTimeSinceJump = 2.f;
}
} // namespace

TEST(mech_recovery_good) {
    Run r;
    recoverySetup(r.sim);
    r.go(340, [](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (c.wheelCount() == 0) orientPD(c, c.vel, in);
        return in;
    });
    const MechGrade* g = r.find(Mech::Recovery);
    CHECK_MSG(g && g->success, describe(r.grades));
}

TEST(mech_recovery_bad) {
    Run r;
    recoverySetup(r.sim);
    r.go(340, [](int, MiniSim&) { return Inputs{}; });
    const MechGrade* g = r.find(Mech::Recovery);
    CHECK_MSG(g && !g->success, describe(r.grades));
    CHECK_MSG(r.count(Mech::Recovery) == 1, describe(r.grades));
}

TEST(mech_recovery_roof_landing_is_graded) {
    // Thrown upside down without any input: lands on the roof, the wheels never touch.
    Run r;
    r.sim.pos = {0.f, 0.f, 600.f};
    r.sim.vel = {600.f, 0.f, 200.f};
    r.sim.orient({1.f, 0.f, 0.f}, {0.f, 0.f, -1.f});
    r.sim.hasJumped = true;
    r.sim.airTimeSinceJump = 2.f;
    r.go(400, [](int, MiniSim&) { return Inputs{}; });
    const MechGrade* g = r.find(Mech::Recovery);
    CHECK_MSG(g && g->variant == "crash" && g->hasError("RC_CRASHED") && !g->success, describe(r.grades));
    CHECK_MSG(r.count(Mech::Recovery) == 1, describe(r.grades));
}

TEST(mech_zap_focus_flags_plain_wavedash) {
    Run r(withFocus(Focus::ZapDash));
    r.sim.vel = {1000.f, 0.f, 0.f};
    r.go(160, wavedashScript(6.f, 20.f, false));
    const MechGrade* g = r.find(Mech::ZapDash);
    CHECK_MSG(g && g->hasError("ZAP_NO_BOUNCE") && !g->success, describe(r.grades));
    CHECK(r.count(Mech::Wavedash) == 0);
}

TEST(mech_recovery_wavedash_reported_once) {
    // Long fall with the flip available, wavedash at the landing: one recovery grade (variant wavedash).
    auto script = [](std::shared_ptr<WdState> S) {
        return [S](int, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            if (S->phase == 0) {
                in.pitch = pitchToNose(c, 18.f);
                if (c.vel.z < 0.f && c.rearWheelZ() < 6.f) S->phase = 1, S->dodgeT = 0;
            }
            if (S->phase >= 1 && S->dodgeT < 3) {
                in.jump = true;
                in.pitch = -1.f;
                S->dodgeT++;
            }
            return in;
        };
    };
    {
        Run r;
        r.sim.pos = {0.f, 0.f, 500.f};
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(260, script(std::make_shared<WdState>()));
        const MechGrade* g = r.find(Mech::Recovery);
        CHECK_MSG(g && g->variant == "wavedash" && g->hasMetric("dashGain"), describe(r.grades));
        CHECK_MSG(r.count(Mech::Wavedash) == 0, describe(r.grades));
    }
    {
        Run r(withFocus(Focus::Wavedash));  // training wavedashes: the dash is reported, not the recovery
        r.sim.pos = {0.f, 0.f, 500.f};
        r.sim.vel = {1000.f, 0.f, 0.f};
        r.go(260, script(std::make_shared<WdState>()));
        CHECK_MSG(r.count(Mech::Wavedash) == 1 && r.count(Mech::Recovery) == 0, describe(r.grades));
    }
}

// ---- Powerslide / boost --------------------------------------------------------------------------------
TEST(mech_powerslide_short_and_long) {
    for (int slide : {20, 90}) {
        Run r;
        r.sim.vel = {1400.f, 0.f, 0.f};
        r.go(200, [=](int t, MiniSim&) {
            Inputs in;
            in.throttle = 1.f;
            if (t >= 10 && t < 10 + slide) {
                in.steer = 1.f;
                in.handbrake = true;
            }
            return in;
        });
        const MechGrade* g = r.find(Mech::Powerslide);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g && slide == 20) CHECK_MSG(g->success, describe(r.grades));
        if (g && slide == 90) CHECK_MSG(g->hasError("PS_SPEED_LOSS") || g->hasError("PS_EXIT_SLIP"), describe(r.grades));
    }
}

TEST(mech_boost_efficiency_window) {
    for (bool feather : {false, true}) {
        DetectConfig cfg;
        cfg.speedWindowTicks = 600;
        Run r(cfg);
        r.sim.vel = {2250.f, 0.f, 0.f};
        r.sim.pos = {-4000.f, 0.f, k::kCarRestZ};
        r.go(620, [=](int, MiniSim& c) {
            Inputs in;
            in.throttle = 1.f;
            in.boost = feather ? length(c.vel) < 2230.f : true;
            if (c.boost <= 0.f) c.boost = 100.f;
            return in;
        });
        const MechGrade* g = r.find(Mech::BoostEfficiency);
        CHECK_MSG(g != nullptr, describe(r.grades));
        if (g && !feather) CHECK(g->hasError("SM_BOOST_AT_MAX"));
        if (g && feather) CHECK_MSG(g->success, describe(r.grades));
    }
}

// ---- Kinematic sequences: wall dash, zap dash ------------------------------------------------------------
namespace {
struct Kin {
    MovementCoach coach;
    Snapshot s;
    std::vector<MechGrade> grades;
    explicit Kin(DetectConfig cfg = {}) {
        coach.cfg = cfg;
        s.frame = 5000;
        s.car.valid = true;
        s.ball.valid = false;
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            s.car.pos += s.car.vel * k::kDt;
            s.time = static_cast<float>(s.frame) / 120.f;
            coach.step(s, grades);
            s.frame++;
            s.in.jump = false;
        }
    }
    const MechGrade* find(Mech m) const {
        for (const auto& g : grades)
            if (g.mech == m) return &g;
        return nullptr;
    }
};

Kin wallRun(int secondPress, bool reattach, bool expect) {
    Kin k(expect ? withFocus(Focus::WallDash) : DetectConfig{});
    const Vec3 n{-1.f, 0.f, 0.f};
    const Vec3 f = normalize(Vec3{0.f, 0.6f, 0.8f});
    k.s.car.basis.up = n;
    k.s.car.basis.fwd = f;
    k.s.car.basis.right = cross(n, f);
    k.s.car.groundNormal = n;
    k.s.car.pos = {4096.f - 17.f, -2000.f, 600.f};
    k.s.car.vel = f * 900.f;
    k.s.car.wheelContacts = 4;
    k.s.car.hasFlip = true;
    k.s.in.throttle = 1.f;
    k.tick(20);
    k.s.in.jump = true;
    k.s.car.jumped = true;
    k.s.car.vel += n * 292.f;
    k.tick(1);
    k.s.car.wheelContacts = 2;
    k.tick(1);
    k.s.car.wheelContacts = 0;
    k.tick(secondPress - 2);
    k.s.in.jump = true;
    k.s.in.pitch = -0.7f;
    k.s.in.roll = 0.7f;
    k.s.car.dodging = true;
    k.s.car.hasFlip = false;
    k.s.car.dodgeDir = {0.707f, 0.707f, 0.f};
    k.s.car.vel += f * 480.f;
    k.tick(1);
    k.s.in.pitch = k.s.in.roll = 0.f;
    if (reattach) {
        k.s.car.vel -= n * 292.f;
        k.tick(4);
        k.s.car.wheelContacts = 2;
        k.tick(1);
        k.s.car.wheelContacts = 4;
        k.s.car.dodging = false;
        k.s.car.jumped = false;
        k.s.car.hasFlip = true;
        k.tick(20);
    } else {
        k.tick(250);
    }
    return k;
}
} // namespace

TEST(mech_wall_dash_good) {
    Kin k = wallRun(8, true, false);
    const MechGrade* g = k.find(Mech::WallDash);
    CHECK_MSG(g && g->success, describe(k.grades));
    if (g) CHECK(g->variant.rfind("wall-", 0) == 0);
}

TEST(mech_wall_dash_fell_off) {
    Kin k = wallRun(16, false, true);
    const MechGrade* g = k.find(Mech::WallDash);
    CHECK_MSG(g && g->hasError("WALL_FELL_OFF") && g->hasError("WALL_TAP_SLOW"), describe(k.grades));
}

TEST(mech_wall_dash_chain) {
    Kin k;
    const Vec3 n{-1.f, 0.f, 0.f};
    const Vec3 f = normalize(Vec3{0.f, 1.f, 0.2f});
    k.s.car.basis.up = n;
    k.s.car.basis.fwd = normalize(f - n * dot(f, n));
    k.s.car.basis.right = cross(n, k.s.car.basis.fwd);
    k.s.car.groundNormal = n;
    k.s.car.pos = {4096.f - 17.f, -3000.f, 500.f};
    k.s.car.vel = k.s.car.basis.fwd * 700.f;
    k.s.car.wheelContacts = 4;
    k.s.car.hasFlip = true;
    k.s.in.throttle = 1.f;
    k.tick(20);
    for (int dash = 0; dash < 4; ++dash) {
        k.s.in.jump = true;  // first tap
        k.s.car.jumped = true;
        k.tick(1);
        k.s.car.wheelContacts = 0;
        k.tick(7);
        k.s.in.jump = true;  // second tap: diagonal flip toward the wall
        k.s.in.pitch = -0.7f;
        k.s.in.roll = 0.7f;
        k.s.car.dodging = true;
        k.s.car.hasFlip = false;
        k.s.car.dodgeDir = {0.707f, 0.707f, 0.f};
        k.s.car.vel += k.s.car.basis.fwd * 480.f;
        k.tick(1);
        k.s.in.pitch = k.s.in.roll = 0.f;
        k.tick(4);
        k.s.car.wheelContacts = 4;  // back on the wall
        k.s.car.dodging = false;
        k.s.car.jumped = false;
        k.s.car.hasFlip = true;
        k.tick(4);
    }
    k.tick(60);
    const MechGrade* g = k.find(Mech::WallChain);
    CHECK_MSG(g != nullptr, describe(k.grades));
    if (g) {
        CHECK(g->metric("dashes") >= 3.f);
        CHECK_MSG(g->success, describe(k.grades));
    }
    CHECK(k.find(Mech::ChainWavedash) == nullptr);
    CHECK(familyOf(Mech::WallChain) == Focus::WallDash);
}

TEST(mech_zap_dash_and_speed_zap_combo) {
    Kin k;
    k.s.car.wheelContacts = 0;
    k.s.car.hasFlip = false;
    k.s.car.jumped = true;
    k.s.car.pos = {0.f, 0.f, 60.f};
    k.s.car.vel = {1500.f, 0.f, -300.f};
    k.s.in.throttle = 1.f;
    k.tick(10);
    k.s.car.wheelContacts = 4;  // nose-down landing: flip and jump state reset
    k.s.car.vel.z = 0.f;
    k.s.car.pos.z = 17.f;
    k.s.car.hasFlip = true;
    k.s.car.jumped = false;
    k.tick(2);
    k.s.car.wheelContacts = 2;  // bounce: front wheels lift without jumping
    k.s.car.pos.z = 22.f;
    k.tick(3);
    k.s.in.jump = true;
    k.s.in.pitch = -1.f;
    k.s.car.dodging = true;
    k.s.car.hasFlip = false;
    k.s.car.dodgeDir = {1.f, 0.f, 0.f};
    k.s.car.vel += Vec3{500.f, 0.f, 0.f};
    k.tick(1);
    k.s.in.pitch = 0.f;
    k.tick(2);
    k.s.car.wheelContacts = 4;
    k.s.car.pos.z = 17.f;
    k.s.car.dodging = false;
    k.s.car.hasFlip = true;
    k.tick(20);
    const MechGrade* g = k.find(Mech::ZapDash);
    CHECK_MSG(g && g->success, describe(k.grades));
    CHECK(k.find(Mech::Wavedash) == nullptr);
}

TEST(mech_combo_detector_rules) {
    ComboDetector combo;
    std::vector<MechGrade> out;
    MechGrade sf;
    sf.mech = Mech::SpeedFlip;
    sf.variant = "left";
    sf.success = true;
    sf.score = 90.f;
    sf.tickStart = 0;
    sf.tickEnd = 140;
    MechGrade zd;
    zd.mech = Mech::ZapDash;
    zd.variant = "forward";
    zd.success = true;
    zd.score = 85.f;
    zd.tickStart = 150;
    zd.tickEnd = 158;
    combo.onGrade(sf, out);
    combo.onGrade(zd, out);
    CHECK(out.size() == 1 && out[0].mech == Mech::SpeedZap && out[0].success);
    MechGrade hf;
    hf.mech = Mech::HalfFlip;
    hf.variant = "roll-right";
    hf.success = true;
    hf.score = 80.f;
    hf.tickStart = 300;
    hf.tickEnd = 420;
    MechGrade wd;
    wd.mech = Mech::Wavedash;
    wd.variant = "forward";
    wd.success = true;
    wd.score = 82.f;
    wd.tickStart = 480;
    wd.tickEnd = 520;
    out.clear();
    combo.onGrade(hf, out);
    combo.onGrade(wd, out);
    CHECK(out.empty());  // 60 ticks later: not a combo
    wd.tickStart = 440;
    combo.onGrade(hf, out);
    combo.onGrade(wd, out);
    CHECK(out.size() == 1 && out[0].mech == Mech::HalfFlipWavedash);
}

// ---- Robustness ----------------------------------------------------------------------------------------
TEST(mech_teleport_resets_detectors) {
    Run r;
    r.sim.vel = {1000.f, 0.f, 0.f};
    // Start a wavedash, then teleport the car mid-air: the dodge in progress must not produce a grade.
    r.go(40, wavedashScript(6.f, 20.f, false));
    r.sim.pos = {3000.f, 3000.f, 300.f};
    r.sim.vel = {};
    r.go(120, [](int, MiniSim&) { return Inputs{}; });
    for (const auto& g : r.grades) CHECK_MSG(g.mech != Mech::Wavedash || g.tickEnd < 1040, describe(r.grades));
}

TEST(mech_motion_tracker_edges) {
    MotionTracker mt;
    Snapshot s;
    s.frame = 10;
    s.car.valid = true;
    s.car.wheelContacts = 4;
    TickEvents ev = mt.step(s);
    CHECK(!ev.valid);
    s.frame = 11;
    s.in.jump = true;
    s.car.jumped = true;
    ev = mt.step(s);
    CHECK(ev.valid && ev.jumpPressed && ev.firstJump);
    s.frame = 12;
    s.car.wheelContacts = 1;
    ev = mt.step(s);
    CHECK(ev.tookOff);
    s.frame = 13;
    s.car.pos = {5000.f, 0.f, 17.f};  // teleport
    ev = mt.step(s);
    CHECK(ev.reset && !ev.valid);
    s.frame = 80;  // frame gap
    ev = mt.step(s);
    CHECK(ev.reset);
}

TEST(mech_dodge_dir_from_inputs_and_kinds) {
    Inputs in;
    in.yaw = 1.f;
    in.roll = -1.f;  // stall: zero direction
    Dir2 d = dodgeDirFromInputs(in);
    CHECK(d.x == 0.f && d.y == 0.f);
    in = Inputs{};
    in.pitch = -std::cos(30.f * kDegToRad);
    in.yaw = std::sin(30.f * kDegToRad);
    d = dodgeDirFromInputs(in);
    CHECK_NEAR(std::atan2(d.y, d.x) * kRadToDeg, 30.f, 0.01f);
    CHECK(dodgeKindFromAngle(30.f, false) == DodgeKind::DiagForward);
    CHECK(dodgeKindFromAngle(170.f, false) == DodgeKind::Back);
    CHECK(dodgeKindFromAngle(-90.f, false) == DodgeKind::Side);
    // RocketSim impulse: side dodge at 1000 uu/s forward -> about 696 uu/s sideways.
    const Vec3 dv = dodgeDeltaV({0.f, 1.f}, {1000.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    CHECK_NEAR(dv.y, 695.6f, 1.f);
}

TEST(mech_catalog_ids_roundtrip) {
    for (int i = 0; i < kFocusCount; ++i) {
        const Focus f = static_cast<Focus>(i);
        CHECK(focusFromId(focusInfo(f).id) == f);
        CHECK(familyOf(focusInfo(f).primary) == f);
    }
    CHECK(focusFromId("SpeedFlip") == Focus::SpeedFlip);
    CHECK(focusFromId("nope") == Focus::None);
    CHECK(variantLabel("left-kickoff") == "gauche kickoff");
    CHECK(variantLabel("roll-right+forward") == "air roll droite + avant");
}
