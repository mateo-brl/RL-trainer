// Record values and button style of the AttemptResults the module emits: a failed drill (timeout, landing, lost
// ball, wrong mechanic) has no record value, a graded attempt only reports a measured, finite one
// (bm::recordValueOf), and every tip uses the button style the player chose (Settings::profile.buttonStyle).
#include <cmath>
#include <limits>

#include "core/common/Controls.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Primitives.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace rlt::bm;
using namespace bmtest;

namespace {
std::vector<SimTick> continueFrom(const Harness& h, std::vector<SimTick> ticks) {
    if (ticks.empty()) return ticks;
    const int64_t offset = h.latest.frame + 1 - ticks.front().s.frame;
    for (auto& t : ticks) {
        t.s.frame += offset;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    return ticks;
}

void warmUp(Harness& h, int n = 10) {
    Sim sim;
    orient(sim.s.car, PI / 2, 0, 0);
    sim.s.ball.pos = {0, 1000, 93};
    sim.step(n);
    h.run(sim.out);
}

void skipReady(Harness& h) { h.skipReady(); }

// Idle ticks with the throttle pressed (counts as activity, so the drill times out instead of pausing).
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

// Air roll drill whose car lands on the floor: failed with DRILL_LANDED.
void landAirRollDrill(Harness& h) {
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
        x.car.pos.z = arena::kCarRestZ;
        x.car.vel = {};
    });
    h.run(continueFrom(h, sim.out));
}

// The failed drill result: single, not a success, no record value and a zero (unused) value.
void checkNoRecord(const Harness& h, const char* what) {
    CHECK_MSG(h.results.size() == 1u, what);
    if (h.results.empty()) return;
    const AttemptResult& r = h.results[0];
    CHECK_MSG(!r.success, what);
    CHECK_MSG(!r.hasRecordValue, what);
    CHECK_MSG(r.valueForRecord == 0.f, what);
}

// Gamepad profile of a PlayStation player (the SDK reports Xbox key names for both pads).
void usePlayStationPad(Harness& h) {
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{"Jump", "XboxTypeS_A"},
                                 {"AirRollLeft", "XboxTypeS_LeftShoulder"},
                                 {"AirRollRight", "XboxTypeS_RightShoulder"},
                                 {"Boost", "XboxTypeS_B"}};
    h.settings.profile.buttonStyle = 2;  // "Style des boutons" : PlayStation
    h.mod.onSettingsChanged(h.settings);
}

const Tip* findTip(const AttemptResult& r, const std::string& code) {
    for (const auto& t : r.tips)
        if (t.code == code) return &t;
    return nullptr;
}
} // namespace

// ---- Failed drills -----------------------------------------------------------------------------------------------
TEST(bm_rec_failed_drills_have_no_record_value) {
    Harness timeout;
    warmUp(timeout);
    timeout.command("drill", {"aerial"});
    skipReady(timeout);
    active(timeout, static_cast<int>((timeout.mod.uiState().drill.timeLeft + 0.5f) * kTickRate));
    checkNoRecord(timeout, "timeout");
    if (!timeout.results.empty()) CHECK(hasTip(timeout.results[0], "bm.drill_timeout"));

    Harness landed;
    landAirRollDrill(landed);
    checkNoRecord(landed, "landed");
    if (!landed.results.empty()) CHECK(hasTip(landed.results[0], "bm.drill_landed"));

    Harness lost;
    warmUp(lost);
    lost.command("drill", {"flick"});
    skipReady(lost);
    Sim sim;
    sim.s.car.pos = {0, 0, 17};
    sim.s.car.vel = {0, 500, 0};
    sim.s.ball.pos = {0, 600, 93};
    sim.s.ball.vel = {0, 900, 0};
    sim.step(150, [](Snapshot& x, int) { x.in.throttle = 1.f; });
    lost.run(continueFrom(lost, sim.out));
    checkNoRecord(lost, "ball lost");
    if (!lost.results.empty()) CHECK(hasTip(lost.results[0], "bm.drill_ball_lost"));

    // Another mechanic resolved the attempt: still a failed drill, even though that shot had a measured speed.
    Harness wrong;
    warmUp(wrong);
    wrong.command("drill", {"flick", "any"});
    skipReady(wrong);
    wrong.run(continueFrom(wrong, powerShotScenario(PS::FlipShot)));
    wrong.idle(10);
    checkNoRecord(wrong, "wrong mechanic");
    if (!wrong.results.empty()) CHECK(hasTip(wrong.results[0], "bm.drill_wrong_mech"));

    // The UI copy of the last result says the same.
    const auto u = landed.mod.uiState();
    CHECK(u.hasLast);
    CHECK(!u.last.hasRecordValue);
}

// ---- Graded attempts ---------------------------------------------------------------------------------------------
TEST(bm_rec_graded_attempts_report_measured_values) {
    struct Case {
        std::vector<SimTick> seq;
        const char* skill;
        float min;  // the record value is at least this (speed in uu/s, duration in s, or score)
    };
    std::vector<Case> cases;
    cases.push_back({flickScenario(FK::F45), "ball.flick", 1500.f});
    cases.push_back({airDribbleScenario(AD::GroundCarry), "ball.airdribble", 0.65f});
    cases.push_back({pinchScenario(PI_::Ground), "ball.pinch", 1000.f});
    cases.push_back({powerShotScenario(PS::FlipShot), "ball.powershot", 1000.f});
    cases.push_back({aerialScenario(AE::Redirect), "ball.aerial", 1.f});
    for (auto& c : cases) {
        Harness h;
        h.run(c.seq);
        h.idle(30);
        CHECK_MSG(h.countSkill(c.skill) == 1, c.skill);
        for (const auto& r : h.results) {
            if (r.skill != c.skill) continue;
            CHECK_MSG(r.hasRecordValue, c.skill);
            CHECK_MSG(std::isfinite(r.valueForRecord), c.skill);
            CHECK_MSG(r.valueForRecord >= c.min, c.skill);
        }
        // Whatever the module emitted: a record value is always finite, and absent values stay at 0.
        for (const auto& r : h.results) {
            if (r.hasRecordValue) CHECK_MSG(std::isfinite(r.valueForRecord), r.skill.c_str());
            else CHECK_MSG(r.valueForRecord == 0.f, r.skill.c_str());
        }
    }
}

TEST(bm_rec_record_value_rules) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    float v = -1.f;

    // Flicks: the ball speed after the flick, only when the shot was measured.
    MechGrade fl;
    fl.family = Family::Flick;
    CHECK(!recordValueOf(Mech::Flick, fl, 80.f, v));
    CHECK_EQ(v, 0.f);
    fl.hasShot = true;
    fl.ballSpeed = 2100.f;
    CHECK(recordValueOf(Mech::Musty, fl, 80.f, v));
    CHECK_EQ(v, 2100.f);
    fl.ballSpeed = nan;
    CHECK(!recordValueOf(Mech::Breezi, fl, 80.f, v));
    CHECK_EQ(v, 0.f);
    fl.ballSpeed = inf;
    CHECK(!recordValueOf(Mech::Flick, fl, 80.f, v));
    fl.ballSpeed = 0.f;
    CHECK(!recordValueOf(Mech::Flick, fl, 80.f, v));

    // Air dribble: the duration metric.
    MechGrade ad;
    ad.family = Family::AirDribble;
    CHECK(!recordValueOf(Mech::AirDribble, ad, 70.f, v));
    ad.metrics.push_back(makeMetric("duration", "Durée", 2.5f, "s"));
    CHECK(recordValueOf(Mech::AirDribble, ad, 70.f, v));
    CHECK_EQ(v, 2.5f);
    ad.metrics.back().value = inf;
    CHECK(!recordValueOf(Mech::AirDribble, ad, 70.f, v));

    // Pinch / power shot: the exit speed metric, else the measured ball speed.
    MechGrade pi;
    pi.family = Family::Pinch;
    CHECK(!recordValueOf(Mech::Pinch, pi, 60.f, v));
    pi.hasShot = true;
    pi.ballSpeed = 3000.f;
    CHECK(recordValueOf(Mech::PowerShot, pi, 60.f, v));
    CHECK_EQ(v, 3000.f);
    pi.metrics.push_back(makeMetric("exit_speed", "Vitesse de sortie", 4200.f, "uu/s"));
    CHECK(recordValueOf(Mech::Pinch, pi, 60.f, v));
    CHECK_EQ(v, 4200.f);
    pi.metrics.back().value = nan;
    CHECK(!recordValueOf(Mech::Pinch, pi, 60.f, v));

    // Other mechanics: the score.
    MechGrade ae;
    ae.family = Family::Aerial;
    CHECK(recordValueOf(Mech::Aerial, ae, 72.f, v));
    CHECK_EQ(v, 72.f);
    CHECK(recordValueOf(Mech::FlipReset, ae, 0.f, v));  // a real (poor) score
    CHECK(!recordValueOf(Mech::Ceiling, ae, nan, v));
}

// ---- Button style ------------------------------------------------------------------------------------------------
TEST(bm_rec_result_tips_use_button_style) {
    // Xbox and PlayStation labels of the air roll button differ, so the tip text tells which style was used.
    PlayerProfile pad;
    pad.valid = true;
    pad.usingGamepad = true;
    pad.gamepadBindings = {{"AirRollLeft", "XboxTypeS_LeftShoulder"}};
    const std::string ps = actionKeyLabel(pad, GameAction::AirRollLeft, KeyStyle::PlayStation);
    const std::string xb = actionKeyLabel(pad, GameAction::AirRollLeft, KeyStyle::Xbox);
    CHECK(!ps.empty() && !xb.empty() && ps != xb);

    // Failed drill (failDrill): the air roll drill's own tip names the PlayStation button.
    Harness f;
    usePlayStationPad(f);
    landAirRollDrill(f);
    CHECK_EQ(f.results.size(), 1u);
    if (!f.results.empty()) {
        const Tip* t = findTip(f.results[0], "bm.ae_no_air_roll");
        CHECK(t != nullptr);
        if (t) {
            CHECK(t->text.find("(" + ps + ")") != std::string::npos);
            CHECK(t->text.find("(" + xb + ")") == std::string::npos);
        }
    }

    // Graded attempt (makeResult): an aerial without air roll in the air roll drill, same tip, same style.
    Harness g;
    usePlayStationPad(g);
    warmUp(g);
    g.command("drill", {"airroll"});
    skipReady(g);
    g.run(continueFrom(g, aerialScenario(AE::Redirect)));
    g.idle(10);
    CHECK_EQ(g.results.size(), 1u);
    if (!g.results.empty()) {
        CHECK(g.results[0].skill == "ball.airroll");
        CHECK(g.results[0].hasRecordValue);  // graded: the score is a real value
        const Tip* t = findTip(g.results[0], "bm.ae_no_air_roll");
        CHECK(t != nullptr);
        if (t) {
            CHECK(t->text.find("(" + ps + ")") != std::string::npos);
            CHECK(t->text.find("(" + xb + ")") == std::string::npos);
        }
    }
    CHECK(g.mod.uiState().keyStyle == KeyStyle::PlayStation);

    // Default style (Auto): Xbox names.
    Harness a;
    a.profile = pad;
    landAirRollDrill(a);
    CHECK_EQ(a.results.size(), 1u);
    if (!a.results.empty()) {
        const Tip* t = findTip(a.results[0], "bm.ae_no_air_roll");
        CHECK(t != nullptr);
        if (t) CHECK(t->text.find("(" + xb + ")") != std::string::npos);
    }
}
