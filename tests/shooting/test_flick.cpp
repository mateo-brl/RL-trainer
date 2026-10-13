// Flick detection and classification: unit tests of classifyFlick and scripted sequences fed to the analyzer
// (good and bad executions, timing, power measurement, edge cases).

#include <cmath>
#include <vector>

#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

FlickFeatures feat(float fwd, float side) {
    FlickFeatures f;
    const float l = std::sqrt(fwd * fwd + side * side);
    f.dodgeFwd = l > 0.f ? fwd / l : 0.f;
    f.dodgeSide = l > 0.f ? side / l : 0.f;
    f.launchFwd = 0.9f;
    f.vertFrac = 0.4f;
    f.delay = 0.2f;
    f.keptFrac = 1.f;
    return f;
}

FlickReport single(const FlickScript& s) {
    FlickAnalyzer a;
    const auto reps = runAnalyzer(a, buildFlick(s));
    CHECK_EQ(reps.size(), static_cast<size_t>(1));
    return reps.empty() ? FlickReport{} : reps.front();
}

} // namespace

// ---- classifyFlick ----------------------------------------------------------------------------------------
TEST(shooting_classify_basic_directions) {
    const FlickThresholds t;
    CHECK(classifyFlick(feat(1.f, 0.f), t).base == FlickType::Front);
    CHECK(classifyFlick(feat(1.f, 1.f), t).base == FlickType::Diagonal);
    CHECK(classifyFlick(feat(0.1f, 1.f), t).base == FlickType::Side);
    FlickFeatures back = feat(-1.f, 0.f);
    back.vertFrac = 0.95f;
    CHECK(classifyFlick(back, t).base == FlickType::Backflip);
    CHECK(!classifyFlick(back, t).uncertain);
    const FlickClass none = classifyFlick(feat(0.f, 0.f), t);
    CHECK(none.base == FlickType::Other && none.uncertain);
}

TEST(shooting_classify_45_and_90_by_yaw) {
    const FlickThresholds t;
    FlickFeatures f = feat(0.7f, 0.7f);
    f.yawTurnDeg = 45.f;
    CHECK(classifyFlick(f, t).base == FlickType::Flick45);
    CHECK(!classifyFlick(f, t).uncertain);
    f.yawTurnDeg = -40.f; // turned the other way
    CHECK(classifyFlick(f, t).base == FlickType::Flick45);
    f.yawTurnDeg = 27.f; // close to the lower edge
    CHECK(classifyFlick(f, t).base == FlickType::Flick45);
    CHECK(classifyFlick(f, t).uncertain);
    f.yawTurnDeg = 15.f; // not turned enough: plain diagonal
    CHECK(classifyFlick(f, t).base == FlickType::Diagonal);
    FlickFeatures n = feat(0.2f, 1.f);
    n.yawTurnDeg = 90.f;
    CHECK(classifyFlick(n, t).base == FlickType::Flick90);
    n.yawTurnDeg = 140.f;
    CHECK(classifyFlick(n, t).base == FlickType::Side);
}

TEST(shooting_classify_backflip_family) {
    const FlickThresholds t;
    FlickFeatures musty = feat(-1.f, 0.f);
    musty.pitchDeg = -70.f;
    CHECK(classifyFlick(musty, t).base == FlickType::Musty);
    CHECK(!classifyFlick(musty, t).uncertain);
    musty.pitchDeg = -38.f; // just past the gate
    CHECK(classifyFlick(musty, t).base == FlickType::Musty);
    CHECK(classifyFlick(musty, t).uncertain);

    FlickFeatures breezi = feat(-1.f, 0.f);
    breezi.rollDeg = 320.f;
    breezi.airRollFrac = 0.8f;
    breezi.pitchDeg = -50.f; // spin wins over the nose-down rule
    CHECK(classifyFlick(breezi, t).base == FlickType::Breezi);
    breezi.airRollFrac = 0.1f; // rolled without air roll input: not a breezi
    CHECK(classifyFlick(breezi, t).base == FlickType::Musty);

    FlickFeatures rev = feat(-1.f, 1.f);
    rev.underside = 0.8f;
    rev.launchFwd = 0.9f;
    rev.vertFrac = 0.35f;
    CHECK(classifyFlick(rev, t).base == FlickType::Reverse);
    CHECK(!classifyFlick(rev, t).uncertain);
    rev.underside = 0.22f;
    CHECK(classifyFlick(rev, t).uncertain);
    FlickFeatures slightBack = feat(-0.35f, 1.f); // back-side dodge, rolled, flat forward launch
    slightBack.underside = 0.6f;
    slightBack.vertFrac = 0.3f;
    CHECK(classifyFlick(slightBack, t).base == FlickType::Reverse);

    // Calibration knob: a stricter musty gate turns the same flick into a back flick.
    FlickThresholds strict;
    strict.mustyPitchDeg = -80.f;
    musty.pitchDeg = -70.f;
    CHECK(classifyFlick(musty, strict).base == FlickType::Backflip);
}

TEST(shooting_classify_delayed_flag_and_category) {
    const FlickThresholds t;
    FlickFeatures f = feat(1.f, 0.f);
    f.delay = 0.8f;
    f.keptFrac = 0.9f;
    FlickClass c = classifyFlick(f, t);
    CHECK(c.base == FlickType::Front && c.delayed);
    CHECK(categoryOf(c) == FlickType::Delayed);
    f.keptFrac = 0.3f; // ball not kept on the car: not a delayed flick
    CHECK(!classifyFlick(f, t).delayed);
    FlickFeatures b = feat(-1.f, 0.f);
    b.rollDeg = 300.f;
    b.airRollFrac = 1.f;
    b.delay = 0.9f;
    c = classifyFlick(b, t);
    CHECK(c.base == FlickType::Breezi && !c.delayed); // a breezi is never "delayed"
    CHECK(flickMatches(classifyFlick(f, t), -1));
    FlickClass d;
    d.base = FlickType::Flick45;
    d.delayed = true;
    CHECK(flickMatches(d, static_cast<int>(FlickType::Delayed)));
    CHECK(flickMatches(d, static_cast<int>(FlickType::Flick45)));
    CHECK(!flickMatches(d, static_cast<int>(FlickType::Musty)));
}

TEST(shooting_flick_type_parsing_and_labels) {
    int t = 99;
    CHECK(parseFlickType("any", t) && t == -1);
    CHECK(parseFlickType("front", t) && t == static_cast<int>(FlickType::Front));
    CHECK(parseFlickType("45", t) && t == static_cast<int>(FlickType::Flick45));
    CHECK(parseFlickType("90", t) && t == static_cast<int>(FlickType::Flick90));
    CHECK(parseFlickType("Musty", t) && t == static_cast<int>(FlickType::Musty));
    CHECK(parseFlickType("breezi", t) && t == static_cast<int>(FlickType::Breezi));
    CHECK(parseFlickType("reverse-45", t) && t == static_cast<int>(FlickType::Reverse));
    CHECK(parseFlickType("delayed", t) && t == static_cast<int>(FlickType::Delayed));
    CHECK(parseFlickType("backflip", t) && t == static_cast<int>(FlickType::Backflip));
    CHECK(!parseFlickType("ceiling", t));
    CHECK(parseFlickType("other", t) && t == -1); // coach launches may pass the skill id
    for (int i = 0; i < kFlickTypeCount; ++i) {
        CHECK(std::string(flickTypeId(static_cast<FlickType>(i))).size() > 0);
        CHECK(std::string(flickTypeLabel(static_cast<FlickType>(i))).size() > 0);
        if (i == static_cast<int>(FlickType::Other)) continue; // not requestable
        int back = -2;
        CHECK(parseFlickType(flickTypeId(static_cast<FlickType>(i)), back));
        CHECK_EQ(back, i);
    }
    CHECK(flickDelayBand(FlickType::Breezi).hi > flickDelayBand(FlickType::Front).hi);
}

// ---- Analyzer on scripted sequences -----------------------------------------------------------------------
TEST(shooting_analyzer_front_flick_from_standstill) {
    FlickScript s;
    const ScriptRun run = buildFlick(s);
    const FlickReport r = single(s);
    const Vec3 post = run.snaps[run.dodgeIndex + 4].ball.vel; // ball velocity in TouchEvent.post
    CHECK(r.cls.base == FlickType::Front);
    CHECK(!r.cls.delayed);
    CHECK(r.category == FlickType::Front);
    CHECK(r.contact && !r.inferredContact && !r.lostBall);
    // Power = ball speed right after the flick (TouchEvent.post): flick impulse + the car's own jump velocity.
    CHECK_NEAR(r.power, length(post), 1e-3f);
    CHECK(r.power > std::sqrt(1800.f * 1800.f + 600.f * 600.f));
    CHECK_NEAR(r.f.delay, 0.2f, 0.01f);
    CHECK_NEAR(r.carryTime, 0.4f, 0.02f);
    CHECK_NEAR(r.f.dodgeFwd, 1.f, 1e-3f);
    CHECK(r.f.launchFwd > 0.9f);
    CHECK_NEAR(r.elevationDeg, std::atan2(post.z, length2D(post)) * kRadToDeg, 0.1f);
    CHECK_NEAR(r.roofOffset.x, 10.f, 1.f);
    CHECK_NEAR(r.roofOffset.y, 0.f, 1.f);
    CHECK(r.dodgeToTouch >= 0.f && r.dodgeToTouch < 0.05f);
    // Realistic timing (press at D, IsDodging from D+1): the engine confirms the dodge and its direction.
    CHECK(!r.cls.uncertain);
    CHECK(r.dodgeConfirmed);
    CHECK(!r.dodgeFromInput);
    CHECK(!r.dirMismatch);
    CHECK_EQ(r.dodgeFrame, run.snaps[run.dodgeIndex].frame); // timed from the press, not from the flag
}

TEST(shooting_analyzer_45_flick_driving) {
    FlickScript s;
    s.carSpeed = 900.f;
    s.yawTurnDeg = 45.f;
    s.dodgeDir = {0.7071f, 0.7071f, 0.f};
    s.delay = 0.25f;
    s.launch = {2000.f, 300.f, 450.f};
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Flick45);
    CHECK(!r.cls.uncertain && r.dodgeConfirmed && !r.dodgeFromInput);
    CHECK_NEAR(std::fabs(r.f.yawTurnDeg), 45.f, 1.5f);
    CHECK_NEAR(r.carSpeedAtJump, 900.f, 1.f);
    CHECK(r.power > 2500.f); // the car speed adds to the flick
    CHECK(r.gain > 1500.f);
}

TEST(shooting_analyzer_90_flick) {
    FlickScript s;
    s.yawTurnDeg = -90.f;
    s.dodgeDir = {0.2f, -0.98f, 0.f};
    s.delay = 0.3f;
    s.launch = {2100.f, 0.f, 300.f};
    CHECK(single(s).cls.base == FlickType::Flick90);
}

TEST(shooting_analyzer_musty_vs_backflip) {
    FlickScript musty;
    musty.pitchDeg = -70.f;
    musty.dodgeDir = {-1.f, 0.f, 0.f};
    musty.delay = 0.35f;
    musty.launch = {1500.f, 0.f, 900.f};
    const FlickReport m = single(musty);
    CHECK(m.cls.base == FlickType::Musty);
    CHECK(!m.cls.uncertain && m.dodgeConfirmed);
    CHECK_NEAR(m.f.pitchDeg, -70.f, 1.f);
    CHECK_NEAR(m.f.delay, 0.35f + 1.f / 120.f, 0.005f); // from the first press (J-1) to the dodge press (D)

    FlickScript back = musty;
    back.pitchDeg = 0.f;
    back.launch = {150.f, 0.f, 1300.f}; // popped straight up
    const FlickReport b = single(back);
    CHECK(b.cls.base == FlickType::Backflip);
    CHECK(b.f.vertFrac > 0.9f);
}

TEST(shooting_analyzer_breezi_with_spin) {
    FlickScript s;
    s.delay = 0.9f;
    s.rollDeg = 300.f;
    s.airRollInput = true;
    s.dodgeDir = {-1.f, 0.f, 0.f};
    s.launch = {1600.f, 0.f, 700.f};
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Breezi);
    CHECK_NEAR(r.f.rollDeg, 300.f, 8.f);
    CHECK(r.f.airRollFrac > 0.9f);
    CHECK(r.f.spinRate > 5.f);
    CHECK(!r.cls.delayed);
}

TEST(shooting_analyzer_reverse_45) {
    FlickScript s;
    s.delay = 0.3f;
    s.rollDeg = 60.f;
    s.airRollInput = true;
    s.dodgeDir = {-0.7071f, 0.7071f, 0.f};
    s.launch = {1700.f, 0.f, 500.f};
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Reverse);
    CHECK(std::fabs(r.f.underside) > 0.5f);
}

TEST(shooting_analyzer_delayed_flick) {
    FlickScript s;
    s.delay = 0.8f;
    s.launch = {1700.f, 0.f, 500.f};
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Front);
    CHECK(r.cls.delayed);
    CHECK(r.category == FlickType::Delayed);
    CHECK(r.f.keptFrac > 0.9f);
    CHECK(r.roofTime > 1.1f); // 0.4 s carry + 0.8 s in the air with the ball
}

TEST(shooting_analyzer_stick_direction_confirmed_by_engine_is_certain) {
    // IsDodging rises at D+1 but the dodge component gives no direction: the stick direction (what the game
    // derives the dodge from) is used and the result is NOT uncertain.
    FlickScript s;
    s.engineDodgeDir = false;
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Front);
    CHECK(r.dodgeFromInput);
    CHECK(r.dodgeConfirmed);
    CHECK(!r.cls.uncertain);
    // Same for a musty (backflip direction from the stick pulled back).
    FlickScript m;
    m.engineDodgeDir = false;
    m.pitchDeg = -70.f;
    m.dodgeDir = {-1.f, 0.f, 0.f};
    m.delay = 0.35f;
    m.launch = {1500.f, 0.f, 900.f};
    const FlickReport mr = single(m);
    CHECK(mr.cls.base == FlickType::Musty);
    CHECK(!mr.cls.uncertain);
}

TEST(shooting_analyzer_dodge_never_confirmed_is_uncertain) {
    // The engine never reports IsDodging (nor a double jump): the stick direction is kept, flagged uncertain.
    FlickScript s;
    s.engineReportsDodge = false;
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Front);
    CHECK(r.dodgeFromInput);
    CHECK(!r.dodgeConfirmed);
    CHECK(r.cls.uncertain);
    CHECK(r.contact && r.power > 1700.f); // still measured and graded
}

TEST(shooting_analyzer_engine_direction_replaces_the_stick) {
    // Stick straight forward, engine reports a slightly diagonal dodge: the engine direction wins.
    FlickScript s;
    s.engineDirOverride = {0.8f, 0.6f, 0.f};
    const FlickReport r = single(s);
    CHECK(!r.dodgeFromInput && r.dodgeConfirmed && !r.dirMismatch);
    CHECK_NEAR(r.f.dodgeFwd, 0.8f, 1e-3f);
    CHECK_NEAR(r.f.dodgeSide, 0.6f, 1e-3f);
    CHECK(!r.cls.uncertain);
    // Engine direction opposite to the stick (wrong frame for GetDodgeDirection?): kept but flagged uncertain.
    FlickScript bad;
    bad.engineDirOverride = {-1.f, 0.f, 0.f};
    bad.launch = {150.f, 0.f, 1300.f};
    const FlickReport b = single(bad);
    CHECK(b.dirMismatch);
    CHECK(b.cls.base == FlickType::Backflip);
    CHECK(b.cls.uncertain);
}

TEST(shooting_analyzer_dodge_seen_from_engine_flag_first) {
    // Stick below the dodge deadzone used by the analyzer (profile not read yet): no input edge, the engine
    // flag at D+1 opens the dodge, still timed from the press at D.
    FlickScript s;
    s.dodgeDir = {0.45f, 0.f, 0.f};
    s.engineDirOverride = {1.f, 0.f, 0.f};
    const ScriptRun run = buildFlick(s);
    FlickAnalyzer a;
    const auto reps = runAnalyzer(a, run);
    CHECK_EQ(reps.size(), static_cast<size_t>(1));
    if (!reps.empty()) {
        CHECK(reps[0].cls.base == FlickType::Front);
        CHECK(reps[0].dodgeConfirmed && !reps[0].cls.uncertain);
        CHECK_EQ(reps[0].dodgeFrame, run.snaps[run.dodgeIndex].frame);
        CHECK_NEAR(reps[0].f.delay, 0.2f + 1.f / 120.f, 0.005f);
    }
    // Legacy glue timing (IsDodging already set at the press tick) is still handled.
    FlickScript legacy;
    legacy.sameTickFlags = true;
    const FlickReport l = single(legacy);
    CHECK(l.cls.base == FlickType::Front);
    CHECK(l.dodgeConfirmed && !l.cls.uncertain);
}

TEST(shooting_analyzer_no_contact_and_lost_ball) {
    FlickScript miss;
    miss.contact = false;
    const FlickReport r = single(miss);
    CHECK(!r.contact);
    CHECK(!r.lostBall);
    CHECK_NEAR(r.power, 0.f, 1e-3f);

    FlickScript lost;
    lost.contact = false;
    lost.delay = 0.3f;
    lost.loseBallBeforeDodge = true;
    const FlickReport l = single(lost);
    CHECK(!l.contact);
    CHECK(l.lostBall);
}

TEST(shooting_analyzer_inferred_contact_without_touch_event) {
    FlickScript s;
    s.deliverTouch = false;
    const FlickReport r = single(s);
    CHECK(r.contact && r.inferredContact);
    CHECK(r.power > 1700.f);
    CHECK(r.cls.base == FlickType::Front);
}

TEST(shooting_analyzer_double_jump_and_landing_are_not_flicks) {
    // Real double jump with the ball: no stick at the press, bDoubleJumped rises at D+1, no dodge.
    FlickScript dj;
    dj.doubleJump = true;
    dj.dodgeDir = {0.f, 0.f, 0.f};
    dj.contact = false;
    FlickAnalyzer a;
    CHECK(runAnalyzer(a, buildFlick(dj)).empty());
    CHECK(a.phase() == FlickPhase::Idle);

    // The stick passed the analyzer deadzone but the game made a double jump (its own deadzone is higher):
    // the provisional dodge is dropped, even when the pop then hits the ball.
    FlickScript dj2;
    dj2.doubleJump = true;
    dj2.dodgeDir = {0.6f, 0.f, 0.f};
    dj2.contact = true;
    FlickAnalyzer a2;
    const ScriptRun run2 = buildFlick(dj2);
    bool sawDodged = false;
    std::vector<FlickReport> reps;
    for (size_t i = 0; i < run2.snaps.size(); ++i) {
        for (const auto& t : run2.touches)
            if (t.first == i) a2.onTouch(t.second);
        if (auto r = a2.update(run2.snaps[i])) reps.push_back(*r);
        if (i == run2.dodgeIndex) sawDodged = a2.phase() == FlickPhase::Dodged; // provisional at the press
    }
    CHECK(sawDodged);
    CHECK(reps.empty());
    CHECK(a2.phase() == FlickPhase::Idle);

    // Jump and land without dodging: a pop, not graded.
    FlickAnalyzer b;
    ScriptRun run;
    const Hitbox hb;
    for (int i = 0; i < 200; ++i) {
        const bool air = i >= 60 && i < 100;
        CarState c = carAt({0.f, 0.f, air ? 60.f : 17.f}, rotDeg(0.f, 90.f, 0.f));
        c.onGround = !air;
        c.wheelContacts = air ? 0 : 4;
        c.jumped = air;
        Inputs in;
        in.jump = i >= 60 && i < 63;
        run.snaps.push_back(snapOf(9000 + i, c, ballAt(roofSpawn(c.pos, c.basis, hb, 91.25f, 0.f)), in));
    }
    CHECK(runAnalyzer(b, run).empty());
    CHECK(b.phase() == FlickPhase::Carry); // back to carrying after landing
}

TEST(shooting_analyzer_carry_phase_and_settled_time) {
    FlickAnalyzer a;
    const Hitbox hb;
    for (int i = 0; i < 60; ++i) {
        const CarState c = carAt({0.f, 0.f, 17.f}, rotDeg(0.f, 90.f, 0.f));
        a.update(snapOf(100 + i, c, ballAt(roofSpawn(c.pos, c.basis, hb, 91.25f, 0.f))));
    }
    CHECK(a.phase() == FlickPhase::Carry);
    CHECK(a.ballOnRoofNow());
    CHECK_NEAR(a.settledTime(159), 59.f / 120.f, 1e-3f);
    // Ball knocked off the roof while driving: back to idle after a short grace period.
    for (int i = 0; i < 30; ++i) {
        const CarState c = carAt({0.f, 0.f, 17.f}, rotDeg(0.f, 90.f, 0.f));
        a.update(snapOf(160 + i, c, ballAt({0.f, 250.f, 93.15f})));
    }
    CHECK(a.phase() == FlickPhase::Idle);
}

TEST(shooting_analyzer_teleport_and_invalid_reset) {
    FlickScript s;
    ScriptRun run = buildFlick(s);
    // Teleport the car in the middle of the air phase: the attempt is dropped.
    for (size_t i = run.jumpIndex + 5; i < run.snaps.size(); ++i) {
        run.snaps[i].car.pos += Vec3{2000.f, 0.f, 0.f};
        run.snaps[i].ball.pos += Vec3{2000.f, 0.f, 0.f};
    }
    FlickAnalyzer a;
    CHECK(runAnalyzer(a, run).empty());

    FlickAnalyzer b;
    ScriptRun run2 = buildFlick(s);
    run2.snaps[run2.jumpIndex + 2].ball.valid = false;
    CHECK(runAnalyzer(b, run2).empty());
}

TEST(shooting_analyzer_orange_team_and_offsets) {
    FlickScript s;
    s.team = 1;
    s.yawDeg = -90.f;
    s.start = {500.f, 2000.f, 17.f};
    s.offY = 40.f; // ball off to the right of the roof
    s.offX = -25.f; // and towards the back
    const FlickReport r = single(s);
    CHECK(r.cls.base == FlickType::Front);
    CHECK_NEAR(r.roofOffset.y, 40.f, 2.f);
    CHECK_NEAR(r.roofOffset.x, -25.f, 2.f);
    CHECK(r.heading.y < -0.99f);
}
