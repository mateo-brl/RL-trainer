// Detector-level tests: port of the research reference suite (good and bad executions for every family, signal
// fallbacks, and the cross-detection matrix with hard negatives).
#include <map>

#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace rlt::bm;
using namespace bmtest;

// ---- Flip reset ------------------------------------------------------------------------------------------------
TEST(bm_det_flipreset_good) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::Good));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "used");
    CHECK(g[0].score >= 0.75f);
    CHECK_EQ(g[0].value("source"), 1.f);
    CHECK(g[0].value("alignment_deg", 99.f) < 18.f);
    CHECK_NEAR(g[0].value("time_to_use"), 0.62f, 0.1f);
    CHECK(g[0].value("on_target") == 1.f);
    CHECK(g[0].errors.empty());
    CHECK(!g[0].weak);
    CHECK(g[0].hasShot);
}

TEST(bm_det_flipreset_wasted) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::Wasted));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].variant == "wasted");
    CHECK(g[0].hasError("FR_RESET_WASTED"));
    CHECK(g[0].score >= 0.45f && g[0].score < 0.75f);  // reset obtained, not used
}

TEST(bm_det_flipreset_near_miss) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::NoReset));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].variant == "no_reset");
    CHECK(g[0].hasError("FR_NO_RESET"));
    CHECK(g[0].hasError("FR_POOR_ALIGNMENT"));
    CHECK(g[0].weak);
    CHECK(g[0].score < 0.15f);
}

TEST(bm_det_flipreset_fast_and_missed) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::FastMissed));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].variant == "flip_missed");
    CHECK(g[0].hasError("FR_FLIP_MISSED_BALL"));
    CHECK(g[0].hasError("FR_TOO_FAST_CONTACT"));
    CHECK(g[0].hasError("FR_POOR_ALIGNMENT"));
    CHECK(g[0].score < 0.5f);
    CHECK(!g[0].success);
}

TEST(bm_det_flipreset_fallback_wheels) {
    // No game event: the wheels attributed to the ball are enough (source 2).
    FlipResetDetector fr;
    auto g = run(fr, flipResetScenario(FR::Good, false));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK_EQ(g[0].value("source"), 2.f);
}

TEST(bm_det_flipreset_fallback_flip_regained) {
    // No event and no wheel count: the flip coming back near the ball, underside facing it (source 3).
    auto seq = flipResetScenario(FR::Good, false);
    for (auto& t : seq) t.s.car.wheelContacts = 0;
    FlipResetDetector fr;
    auto g = run(fr, seq);
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK_EQ(g[0].value("source"), 3.f);
}

TEST(bm_det_flipreset_chain) {
    FlipResetDetector fr;
    auto g = run(fr, flipResetChain());
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK_EQ(g[0].value("resets_in_chain"), 2.f);
}

TEST(bm_det_flipreset_reports_acquisition_once) {
    // A contact lasting several ticks is one reset (rising edge only).
    FlipResetDetector fr;
    const auto frames = buildFrames(flipResetScenario(FR::Good));
    int acquisitions = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        fr.update(frames[i - 1], frames[i], refParams());
        if (fr.acquiredSource() != 0) ++acquisitions;
    }
    CHECK_EQ(acquisitions, 1);
}

// ---- Ceiling shot ----------------------------------------------------------------------------------------------
TEST(bm_det_ceiling_good) {
    CeilingShotDetector c;
    auto g = run(c, ceilingScenario(true));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "drop");
    CHECK_EQ(g[0].value("flip_shot"), 1.f);
    CHECK(g[0].score >= 0.8f);
    CHECK(g[0].value("separation_at_touch") > 120.f);
}

TEST(bm_det_ceiling_jump_off_wasted) {
    CeilingShotDetector c;
    auto g = run(c, ceilingScenario(false));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].variant == "jump_off");
    CHECK(g[0].hasError("CEIL_JUMPED_OFF"));
    CHECK(g[0].hasError("CEIL_FLIP_WASTED"));
    CHECK(g[0].score <= 0.25f);
}

TEST(bm_det_ceiling_drop_far_from_ball_is_not_an_attempt) {
    auto seq = ceilingScenario(false);
    for (auto& t : seq) {
        t.s.ball.pos = {0, -4000, 93};
        t.s.ball.vel = {};
        t.s.in.jump = false;
        t.s.car.dodging = false;
        t.touched = false;
    }
    CeilingShotDetector c;
    auto g = run(c, seq);
    CHECK(g.empty());
}

TEST(bm_det_ceiling_off_on_custom_maps) {
    CeilingShotDetector c;
    auto g = run(c, ceilingScenario(true), refParams(), false);
    CHECK(g.empty());
}

// ---- Flicks ----------------------------------------------------------------------------------------------------
namespace {
struct FlickCase {
    FK kind;
    const char* target;
    const char* variant;
    bool success;
    const char* error;
};
} // namespace

TEST(bm_det_flick_matrix) {
    const FlickCase cases[] = {
        {FK::F45, "45", "45", true, nullptr},
        {FK::F90, "", "90", true, nullptr},
        {FK::Musty, "musty", "musty", true, nullptr},
        {FK::Breezi, "breezi", "breezi", true, nullptr},
        {FK::FrontDelayed, "", "front_delayed", true, nullptr},
        {FK::Reverse, "", "reverse", true, nullptr},
        {FK::LostBall, "45", "45", false, "FL_LOST_BALL"},
        {FK::OverRotated45, "45", "90", false, "FL_OVER_ROTATED"},
        {FK::MustyNoseLevel, "musty", "backflip", false, "FL_MUSTY_NOSE_NOT_DOWN"},
    };
    for (const auto& fc : cases) {
        FlickDetector fd(fc.target);
        auto g = run(fd, flickScenario(fc.kind));
        CHECK_MSG(g.size() == 1u, std::string("variant ") + fc.variant);
        if (g.empty()) continue;
        CHECK_MSG(g[0].variant == fc.variant, "got " + g[0].variant + " expected " + fc.variant);
        CHECK_MSG(g[0].success == fc.success, std::string("success for ") + fc.variant);
        if (fc.error) CHECK_MSG(g[0].hasError(fc.error), std::string("missing ") + fc.error);
        if (fc.success) CHECK_MSG(g[0].score >= 0.7f, "score " + std::to_string(g[0].score));
        else CHECK_MSG(g[0].score < 0.7f, "score " + std::to_string(g[0].score));
    }
}

TEST(bm_det_flick_45_metrics) {
    FlickDetector fd("45");
    auto g = run(fd, flickScenario(FK::F45));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK_NEAR(std::fabs(g[0].value("yaw_deg")), 45.f, 3.f);
    CHECK_NEAR(g[0].value("delay"), 0.30f, 0.03f);
    CHECK_NEAR(g[0].value("ball_dv"), 1581.f, 30.f);
    CHECK(g[0].value("dodge_to_touch") < 0.05f);
    CHECK(g[0].value("carry_time") >= 0.35f);
}

TEST(bm_det_flick_musty_pitch_and_breezi_roll) {
    FlickDetector m("musty");
    auto gm = run(m, flickScenario(FK::Musty));
    CHECK_EQ(gm.size(), 1u);
    if (!gm.empty()) CHECK_NEAR(gm[0].value("pitch_deg"), -70.f, 3.f);
    FlickDetector b("breezi");
    auto gb = run(b, flickScenario(FK::Breezi));
    CHECK_EQ(gb.size(), 1u);
    if (!gb.empty()) {
        CHECK(std::fabs(gb[0].value("roll_deg")) >= 230.f);
        CHECK_NEAR(gb[0].value("delay"), 0.95f, 0.05f);
    }
}

TEST(bm_det_flick_jump_unsettled) {
    auto seq = flickScenario(FK::F45);
    // Only ~0.12 s of stable carry: the ball slides (relative speed 400) before that.
    for (size_t i = 0; i < 34 && i < seq.size(); ++i) seq[i].s.ball.vel = seq[i].s.car.vel + Vec3{0, 400, 0};
    FlickDetector fd("45");
    auto g = run(fd, seq);
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) CHECK(g[0].hasError("FL_JUMP_UNSETTLED"));
}

TEST(bm_det_flick_dribble_pop_is_not_graded) {
    // Carry, jump, land again without dodging: not a flick.
    Sim sim;
    setGround(sim.s.car);
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.car.pos = {0, -3000, bm::arena::kCarRestZ};
    sim.s.car.vel = {0, 900, 0};
    sim.attached = true;
    sim.attachOffset = {0, 20, 125};
    sim.step(40);
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.in.jump = true;
        x.car.vel.z = 292;
    });
    sim.carGravity = true;
    sim.step(60, [&](Snapshot& x, int) {
        x.in.jump = false;
        if (x.car.pos.z <= bm::arena::kCarRestZ) {
            x.car.pos.z = bm::arena::kCarRestZ;
            x.car.vel.z = 0;
            setGround(x.car);
        }
    });
    FlickDetector fd;
    CHECK(run(fd, sim.out).empty());
}

TEST(bm_det_flick_double_jump_is_not_a_flick) {
    auto seq = flickScenario(FK::FrontDelayed);
    // Replace the dodge by a double jump (no stick).
    for (auto& t : seq) {
        t.s.in.pitch = t.s.in.yaw = t.s.in.dodgeForward = t.s.in.dodgeStrafe = 0.f;
        t.s.car.dodging = false;
        t.s.car.dodgeDir = {};
    }
    FlickDetector fd;
    auto g = run(fd, seq);
    CHECK(g.empty());
}

// ---- Double tap ------------------------------------------------------------------------------------------------
TEST(bm_det_doubletap_good) {
    DoubleTapDetector dt;
    auto g = run(dt, doubleTapScenario(DTK::Good));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].errors.empty());
    CHECK(g[0].score >= 0.75f);
    CHECK(g[0].value("backboard_height") > arena::kGoalHeight);
}

TEST(bm_det_doubletap_landed) {
    DoubleTapDetector dt;
    auto g = run(dt, doubleTapScenario(DTK::Landed));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("DT_LANDED"));
}

TEST(bm_det_doubletap_late_no_second_touch) {
    DoubleTapDetector dt;
    auto g = run(dt, doubleTapScenario(DTK::LateNoSecond));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("DT_NO_SECOND_TOUCH"));
    CHECK(g[0].hasError("DT_LATE_FOLLOW"));
    CHECK(g[0].weak);       // the car never followed: not a real attempt outside the drill
    CHECK(g[0].drillOnly);
}

TEST(bm_det_doubletap_read_mode) {
    // Drill read mode: the ball is already on its way to the backboard, the car waits on the ground.
    auto seq = doubleTapScenario(DTK::Good);
    DoubleTapDetector dt;
    const auto frames = buildFrames(seq);
    // Arm right after the first touch was evaluated: drop the first-touch flag to emulate a drill-sent ball.
    std::vector<MechGrade> gs;
    bool armed = false;
    for (size_t i = 1; i < frames.size(); ++i) {
        Frame f = frames[i];
        if (!armed && f.s.ball.vel.y > 2000.f) {
            dt.armReadMode(f.t);
            armed = true;
        }
        if (armed && gs.empty() && f.touched && f.s.ball.pos.y < 4000.f) f.touched = f.touchNew = false;
        if (auto g = dt.update(frames[i - 1], f, refParams())) gs.push_back(*g);
    }
    CHECK(armed);
    CHECK_EQ(gs.size(), 1u);
    if (!gs.empty()) {
        CHECK(gs[0].variant == "read");
        CHECK(gs[0].success);
        CHECK(!gs[0].hasError("DT_LOW_BACKBOARD"));
    }
}

// ---- Air dribble -----------------------------------------------------------------------------------------------
TEST(bm_det_airdribble_ground_carry) {
    AirDribbleDetector ad;
    auto g = run(ad, airDribbleScenario(AD::GroundCarry));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "ground_to_air_carry");
    CHECK(g[0].score >= 0.75f);
    CHECK_EQ(g[0].value("finished_on_target"), 1.f);
    CHECK(g[0].value("touches") >= 18.f);
    CHECK(g[0].value("air_time") > 1.f);
    CHECK(g[0].value("ball_travel") > 1000.f);
    CHECK(!g[0].weak);
}

TEST(bm_det_airdribble_wall_bump) {
    AirDribbleDetector ad;
    auto g = run(ad, airDribbleScenario(AD::WallBump));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "wall_to_air_bump");
    CHECK(g[0].hasError("AD_NO_FINISH"));
}

TEST(bm_det_airdribble_short) {
    AirDribbleDetector ad;
    auto g = run(ad, airDribbleScenario(AD::Short));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("AD_SHORT"));
    CHECK(g[0].weak);  // a single air touch is only a weak claim
    CHECK(g[0].drillOnly);
}

TEST(bm_det_airdribble_continuous_contact_counts_slowly) {
    // The game reports a hit on every tick of a continuous push: it must not count 120 touches per second.
    auto seq = airDribbleScenario(AD::GroundCarry);
    bool inAir = false;
    for (auto& t : seq) {
        if (t.s.car.wheelContacts == 0 && t.s.ball.vel.y < 1000.f) inAir = true;
        if (inAir && !t.touched && t.s.ball.vel.y < 1000.f) {
            t.touched = true;
            t.newContact = false;
        }
    }
    AirDribbleDetector ad;
    auto g = run(ad, seq);
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) CHECK(g[0].value("touches") < 25.f);
}

// ---- Aerial touch ----------------------------------------------------------------------------------------------
TEST(bm_det_aerial_air_roll_shot) {
    AerialDetector ae;
    auto g = run(ae, aerialScenario(AE::AirRoll));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "air_roll_shot");
    CHECK(g[0].score >= 0.8f);
    CHECK(g[0].value("air_roll_frac") > 90.f);
    CHECK_EQ(g[0].value("air_roll_consistency"), 100.f);
    CHECK_EQ(g[0].value("air_roll_dir"), 1.f);
}

TEST(bm_det_aerial_redirect) {
    AerialDetector ae;
    auto g = run(ae, aerialScenario(AE::Redirect));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "redirect");
    CHECK(g[0].value("redirect_angle") > 80.f);
}

TEST(bm_det_aerial_off_center) {
    AerialDetector ae;
    auto g = run(ae, aerialScenario(AE::OffCenter));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("AE_OFF_TARGET"));
    CHECK(g[0].hasError("AE_OFF_CENTER"));
    CHECK(g[0].score < 0.5f);
}

TEST(bm_det_aerial_soft_tap_not_graded) {
    auto seq = aerialScenario(AE::AirRoll);
    bm::DetectParams p = refParams();
    p.aerialMinSpeed = 3000.f;  // the 2680 uu/s shot becomes a "control touch"
    AerialDetector ae;
    CHECK(run(ae, seq, p).empty());
}

TEST(bm_det_aerial_impact_point_from_hook) {
    // With the hook's contact point, the angle between the push direction and the goal is measured.
    auto seq = aerialScenario(AE::AirRoll);
    auto frames = buildFrames(seq);
    for (auto& f : frames) {
        if (!f.touched) continue;
        f.hasHit = true;
        f.hitLocation = f.s.ball.pos + Vec3{0.f, -f.s.ball.radius, 0.f};  // hit from behind, toward +Y
    }
    AerialDetector ae;
    std::vector<MechGrade> gs;
    for (size_t i = 1; i < frames.size(); ++i)
        if (auto g = ae.update(frames[i - 1], frames[i], refParams())) gs.push_back(*g);
    CHECK_EQ(gs.size(), 1u);
    if (!gs.empty()) {
        CHECK(gs[0].has("impact_err_deg"));
        CHECK(gs[0].value("impact_err_deg", 99.f) < 30.f);
    }
}

// ---- Pinch -----------------------------------------------------------------------------------------------------
TEST(bm_det_pinch_ground) {
    PinchDetector pi;
    auto g = run(pi, pinchScenario(PI_::Ground));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "ground_pinch");
    CHECK_NEAR(g[0].value("exit_speed"), 4837.f, 20.f);
    CHECK(!g[0].weak);
}

TEST(bm_det_pinch_kuxir) {
    PinchDetector pi;
    auto g = run(pi, pinchScenario(PI_::Kuxir));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "wall_pinch_kuxir");
    CHECK_EQ(g[0].value("on_target"), 1.f);
}

TEST(bm_det_pinch_weak) {
    PinchDetector pi;
    auto g = run(pi, pinchScenario(PI_::Weak));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("PI_NOT_PINCHED"));
    CHECK(g[0].weak);
    CHECK(g[0].drillOnly);
}

TEST(bm_det_pinch_threshold_setting) {
    bm::DetectParams p = refParams();
    p.pinchSpeed = 5000.f;  // stricter threshold: the 4837 uu/s ground pinch no longer counts
    PinchDetector pi;
    auto g = run(pi, pinchScenario(PI_::Ground), p);
    CHECK_EQ(g.size(), 1u);
    if (!g.empty()) CHECK(!g[0].success);
}

// ---- Power shot ------------------------------------------------------------------------------------------------
TEST(bm_det_powershot_flip) {
    PowerShotDetector ps;
    auto g = run(ps, powerShotScenario(PS::FlipShot));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "flip_shot");
    CHECK(g[0].errors.empty());
    CHECK(g[0].score >= 0.8f);
    CHECK(g[0].value("dodge_to_contact") < 0.15f);
}

TEST(bm_det_powershot_slide) {
    PowerShotDetector ps;
    auto g = run(ps, powerShotScenario(PS::SlideShot));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(g[0].success);
    CHECK(g[0].variant == "powerslide_shot");
}

TEST(bm_det_powershot_weak_side) {
    PowerShotDetector ps;
    auto g = run(ps, powerShotScenario(PS::WeakSide));
    CHECK_EQ(g.size(), 1u);
    if (g.empty()) return;
    CHECK(!g[0].success);
    CHECK(g[0].hasError("PS_WEAK"));
    CHECK(g[0].hasError("PS_SIDE_HIT"));
    CHECK(g[0].score < 0.3f);
}

TEST(bm_det_powershot_needs_a_touch) {
    // Touches only come from the engine (TouchEvent): a velocity jump alone is not a touch.
    auto seq = powerShotScenario(PS::FlipShot);
    for (auto& t : seq) t.touched = false;
    PowerShotDetector ps;
    CHECK(run(ps, seq).empty());
}

TEST(bm_det_powershot_min_speed_setting) {
    // Default module threshold (1500 uu/s): a soft 1270 uu/s ground hit is not graded.
    bm::DetectParams p = refParams();
    p.powerMinSpeed = 1500.f;
    PowerShotDetector ps;
    CHECK(run(ps, powerShotScenario(PS::WeakSide), p).empty());
}

// ---- Cross-detection matrix ------------------------------------------------------------------------------------
TEST(bm_det_cross_matrix) {
    std::vector<std::pair<std::string, std::vector<SimTick>>> scen = {
        {"flipreset_good", flipResetScenario(FR::Good)},       {"ceiling_good", ceilingScenario(true)},
        {"flick_45", flickScenario(FK::F45)},                  {"flick_musty", flickScenario(FK::Musty)},
        {"doubletap_good", doubleTapScenario(DTK::Good)},      {"airdribble_carry", airDribbleScenario(AD::GroundCarry)},
        {"aerial_airroll", aerialScenario(AE::AirRoll)},       {"pinch_ground", pinchScenario(PI_::Ground)},
        {"powershot_flip", powerShotScenario(PS::FlipShot)}};
    FlipResetDetector fr;
    CeilingShotDetector ceil;
    FlickDetector flick;
    DoubleTapDetector dt;
    AirDribbleDetector ad;
    AerialDetector ae;
    PinchDetector pinch;
    PowerShotDetector ps;
    std::vector<Detector*> all = {&fr, &ceil, &flick, &dt, &ad, &ae, &pinch, &ps};
    std::map<std::string, std::map<std::string, int>> fired;
    for (auto& [name, seq] : scen)
        for (auto* d : all) fired[name][familyId(d->family())] = static_cast<int>(run(*d, seq).size());
    for (auto& [name, row] : fired) {
        if (name != "flipreset_good") CHECK_MSG(row["flip_reset"] == 0, "flip_reset fired on " + name);
        if (name != "ceiling_good") CHECK_MSG(row["ceiling_shot"] == 0, "ceiling fired on " + name);
        if (name != "doubletap_good") CHECK_MSG(row["double_tap"] == 0, "double_tap fired on " + name);
        if (name != "pinch_ground") CHECK_MSG(row["pinch"] == 0, "pinch fired on " + name);
        if (name.rfind("flick", 0) != 0) CHECK_MSG(row["flick"] == 0, "flick fired on " + name);
        if (name != "airdribble_carry") CHECK_MSG(row["air_dribble"] == 0, "air_dribble fired on " + name);
    }
    // Each scenario is detected by its own detector.
    CHECK_EQ(fired["flipreset_good"]["flip_reset"], 1);
    CHECK_EQ(fired["ceiling_good"]["ceiling_shot"], 1);
    CHECK_EQ(fired["flick_45"]["flick"], 1);
    CHECK_EQ(fired["flick_musty"]["flick"], 1);
    CHECK_EQ(fired["doubletap_good"]["double_tap"], 1);
    CHECK_EQ(fired["airdribble_carry"]["air_dribble"], 1);
    CHECK_EQ(fired["aerial_airroll"]["aerial_touch"], 1);
    CHECK_EQ(fired["pinch_ground"]["pinch"], 1);
    CHECK_EQ(fired["powershot_flip"]["power_shot"], 1);
    // Power shot vs pinch separation, and no power shot on flicks (carry exclusion).
    CHECK_EQ(fired["pinch_ground"]["power_shot"], 0);
    CHECK_EQ(fired["flick_45"]["power_shot"], 0);
    CHECK_EQ(fired["flick_musty"]["power_shot"], 0);
}
