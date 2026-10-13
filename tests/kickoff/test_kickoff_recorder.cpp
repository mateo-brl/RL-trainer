// Recorder, classifier, grade (weights, tips) and splits on synthetic good and bad kickoffs.
#include <map>

#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

KickoffDuelResult duelVsPro(const KickoffMetrics& m) {
    const KickoffGhost ghost(m.spawn, kickoffOpponent(m.team),
                             KickoffGhostProfile::fromBenchmark(kickoffBenchmark(m.spawn, KickoffLevel::Pro)));
    KickoffPlayerContact pc;
    pc.touched = m.touched;
    pc.time = m.timeToTouch;
    pc.speedToBall = m.speedToBall;
    pc.lateralOffset = m.lateralOffset;
    pc.flipIntoBall = m.flipIntoBall;
    return kickoffResolveDuel(pc, ghost.arrivalTime(), ghost.contactSpeed());
}

bool hasTip(const KickoffGrade& g, KickoffTip t) {
    for (const auto& x : g.tips)
        if (x.tip == t) return true;
    return false;
}

} // namespace

TEST(ko_recorder_classifies_synthetic_attempts) {
    struct Case {
        const char* name;
        KickoffSpawn sp;
        Script sc;
        KickoffType expect;
    };
    std::vector<Case> cases;
    {
        Script s;
        s.kind = Script::Speedflip;
        cases.push_back({"speedflip diag", KickoffSpawn::DiagonalRight, s, KickoffType::Speedflip});
    }
    {
        Script s;
        s.kind = Script::Speedflip;
        s.jumpAt = 0.47f;
        cases.push_back({"speedflip off", KickoffSpawn::OffCenterLeft, s, KickoffType::Speedflip});
    }
    {
        Script s;
        s.kind = Script::Speedflip;
        s.jumpAt = 0.48f;
        cases.push_back({"speedflip back", KickoffSpawn::BackCenter, s, KickoffType::Speedflip});
    }
    {
        Script s;
        s.kind = Script::LateSpeedflip;
        s.jumpAt = 0.80f;
        s.dodgeGapTicks = 30;
        s.cancelTicks = 20;
        s.sideDodge = -0.15f;
        s.contactLateral = 60.f;
        cases.push_back({"sloppy speedflip", KickoffSpawn::DiagonalLeft, s, KickoffType::FrontFlip});
    }
    {
        Script s;
        s.kind = Script::FrontFlip;
        s.jumpAt = 0.70f;
        s.dodgeGapTicks = 12;
        cases.push_back({"front flip diag", KickoffSpawn::DiagonalRight, s, KickoffType::FrontFlip});
    }
    {
        Script s;
        s.kind = Script::DoubleFlip;
        s.jumpAt = 0.65f;
        s.dodgeGapTicks = 12;
        cases.push_back({"double flip back", KickoffSpawn::BackCenter, s, KickoffType::DoubleFlip});
    }
    {
        Script s;
        s.kind = Script::Wavedash;
        s.jumpAt = 0.55f;
        s.dodgeGapTicks = 6;
        cases.push_back({"wavedash off", KickoffSpawn::OffCenterRight, s, KickoffType::Wavedash});
    }
    {
        Script s;
        s.kind = Script::BoostOnly;
        cases.push_back({"boost only diag", KickoffSpawn::DiagonalRight, s, KickoffType::BoostOnly});
    }
    {
        Script s;
        s.kind = Script::BoostOnly;
        s.reaction = 0.6f;
        cases.push_back({"delayed diag", KickoffSpawn::DiagonalRight, s, KickoffType::Delayed});
    }
    {
        Script s;
        s.kind = Script::Fake;
        cases.push_back({"fake back", KickoffSpawn::BackCenter, s, KickoffType::Fake});
    }

    std::map<std::string, int> scores;
    for (const auto& c : cases) {
        const KickoffMetrics m = runRecorder(c.sp, KickoffTeam::Blue, c.sc);
        const KickoffDuelResult d = duelVsPro(m);
        const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, &d);
        scores[c.name] = g.total;
        CHECK_MSG(m.type == c.expect, std::string(c.name) + " classified " + kickoffTypeName(m.type));
        CHECK(g.total >= 0 && g.total <= 100);
        CHECK(g.tips.size() <= 3);
        if (m.touched) CHECK(m.hasBallExit);
        for (size_t i = 1; i < g.tips.size(); ++i) CHECK(g.tips[i - 1].loss >= g.tips[i].loss);
    }
    CHECK(scores["speedflip diag"] >= 80);
    CHECK(scores["speedflip diag"] > scores["front flip diag"]);
    CHECK(scores["front flip diag"] > scores["delayed diag"]);
    CHECK(scores["boost only diag"] > scores["delayed diag"]);
    CHECK(scores["fake back"] <= 20);
}

TEST(ko_recorder_speedflip_metrics) {
    const KickoffMetrics m = runRecorder(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, Script{});
    CHECK(m.touched);
    CHECK_NEAR(m.timeToTouch, 1.87f, 0.08f);
    CHECK_EQ(m.ticksToTouch, static_cast<int>(std::lround(m.timeToTouch * 120.f)));
    CHECK(m.firstJump >= 0.44f && m.firstJump <= 0.46f);
    CHECK_NEAR(m.dodge - m.firstJump, 13.f * kTickDt, 1e-3);
    CHECK_NEAR(m.cancel - m.dodge, 3.f * kTickDt, 1e-3);
    CHECK(m.dodgeAngleDeg < -20.f && m.dodgeAngleDeg > -40.f);
    CHECK(m.padsPicked == 1);
    CHECK_NEAR(m.boostPicked, 12.f, 0.5f);
    CHECK(m.boostUsed > 30.f);  // spawn tank used up
    CHECK(!m.boostLooksUnlimited);
    CHECK(m.reaction == 0.f);  // inputs held at GO
    CHECK_NEAR(m.lateralOffset, 5.f, 0.5f);
    CHECK(m.landing > m.dodge);
    CHECK(m.boostHeldRatio > 0.97f);
    CHECK(m.pathDevRms < 5.f);
    CHECK_NEAR(m.ballExitAngleDeg, 38.66f, 0.1f);  // ball leaves along the diagonal
    CHECK(m.projectedY > 0.f);
    CHECK(m.speedToBall > 1800.f);
    // Orange is the mirror image: same numbers.
    const KickoffMetrics mo = runRecorder(KickoffSpawn::DiagonalRight, KickoffTeam::Orange, Script{});
    CHECK_NEAR(mo.timeToTouch, m.timeToTouch, 1e-4);
    CHECK(mo.type == m.type);
    CHECK_NEAR(mo.ballExitAngleDeg, m.ballExitAngleDeg, 0.01);
    CHECK_NEAR(mo.lateralOffset, m.lateralOffset, 0.05);
    CHECK(mo.projectedY > 0.f);
}

TEST(ko_recorder_touch_timing_and_rollback) {
    KickoffRecorder rec;
    rec.begin(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, 0);
    Snapshot a0 = makeSnap(0);
    rec.feed(a0);
    Snapshot pre = makeSnap(229);
    pre.car.basis = basisFromRot(rotFromYawDeg(90.f));
    pre.car.pos = {0.f, -158.f, 17.f};
    pre.ball.pos = {0.f, 0.f, 92.75f};
    Snapshot post = makeSnap(231);
    post.ball.vel = {0.f, 1500.f, 300.f};
    rec.touch(pre, post);
    CHECK(rec.touched());
    CHECK_NEAR(rec.metrics().timeToTouch, 229.f / 120.f, 1e-5);
    CHECK_EQ(rec.metrics().ticksToTouch, 229);
    CHECK_NEAR(rec.metrics().ballExitAngleDeg, 0.f, 1e-3);
    // A touch before GO is ignored; a second touch too.
    KickoffRecorder r2;
    r2.begin(KickoffSpawn::BackCenter, KickoffTeam::Blue, 100);
    Snapshot early = makeSnap(90);
    r2.touch(early, makeSnap(92));
    CHECK(!r2.touched());
    // Duplicate and older frames are ignored by feed().
    KickoffRecorder r3;
    r3.begin(KickoffSpawn::BackCenter, KickoffTeam::Blue, 10);
    Snapshot s = makeSnap(10);
    s.car.pos = kickoffPose(KickoffSpawn::BackCenter, KickoffTeam::Blue).pos;
    r3.feed(s);
    r3.feed(s);
    Snapshot old = makeSnap(5);
    r3.feed(old);
    CHECK_EQ(r3.metrics().along.size(), size_t(1));
    // Invalid car ticks are skipped without breaking anything.
    Snapshot inv = makeSnap(11);
    inv.car.valid = false;
    r3.feed(inv);
    CHECK_EQ(r3.metrics().along.size(), size_t(1));
}

TEST(ko_recorder_dodge_from_engine_flag) {
    // A dodge seen only through the car's "dodging" flag (no input edge) is still detected, once.
    KickoffRecorder rec;
    rec.begin(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, 0);
    const KickoffPose pose = kickoffPose(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, kKoCarRestZ);
    for (int f = 0; f < 120; ++f) {
        Snapshot s = makeSnap(f);
        s.car.pos = pose.pos;
        s.car.basis = basisFromRot(pose.rot);
        s.in.throttle = 1.f;
        s.in.boost = true;
        if (f >= 54) s.car.onGround = false;
        if (f == 54) s.in.jump = true;  // first jump (edge while grounded on the previous tick? grounded here)
        if (f >= 60 && f < 90) {
            s.car.dodging = true;
            s.car.dodgeDir = {0.866f, 0.5f, 0.f};  // 30 deg right
        }
        if (f == 54) s.car.onGround = true;
        rec.feed(s);
    }
    const KickoffMetrics& m = rec.finish();
    CHECK_EQ(m.dodges, 1);
    CHECK_NEAR(m.dodgeAngleDeg, 30.f, 0.5f);
    CHECK_NEAR(m.dodge, 60.f * kTickDt, 1e-4);
}

TEST(ko_recorder_unlimited_boost_and_estimate) {
    KickoffRecorder rec;
    rec.begin(KickoffSpawn::BackCenter, KickoffTeam::Blue, 0);
    const KickoffPath p = kickoffPath(KickoffSpawn::BackCenter, KickoffTeam::Blue);
    for (int f = 0; f < 120; ++f) {
        Snapshot s = makeSnap(f);
        s.car.pos = p.pointAt(static_cast<float>(f) * 10.f);
        s.car.vel = p.dir * 1200.f;
        s.car.boost = 100.f;  // unlimited boost: the tank never moves
        s.in.boost = true;
        s.in.throttle = 1.f;
        rec.feed(s);
    }
    Snapshot now = makeSnap(120);
    now.car.pos = p.pointAt(p.length - 600.f);
    now.car.vel = p.dir * 1500.f;
    rec.estimate(now);
    const KickoffMetrics& m = rec.finish();
    CHECK(m.boostLooksUnlimited);
    CHECK(m.estimated && !m.touched);
    CHECK_NEAR(m.timeToTouch, 1.f + 600.f / 1500.f, 1e-3);
    CHECK_NEAR(m.speedToBall, 1500.f, 1e-3);
    // Grade of an estimated attempt: neutral contact score, no centring tip, no pad tip with unlimited boost.
    KickoffDuelResult d;
    d.outcome = KickoffDuel::GhostClear;
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, &d);
    CHECK_NEAR(g.sContact, 50.f, 1e-4);
    CHECK(!hasTip(g, KickoffTip::OffCenterContact));
    CHECK(!hasTip(g, KickoffTip::MissedPad));
    CHECK(hasTip(g, KickoffTip::LateArrival));
    CHECK_NEAR(g.sOutcome, 0.f, 1e-6);
}

TEST(ko_grade_weights_exact) {
    // Hand-built metrics: every sub-score known, total = 0.40 T + 0.15 (S + C + O + E).
    KickoffMetrics m;
    m.spawn = KickoffSpawn::DiagonalLeft;
    m.touched = true;
    m.timeToTouch = 1.95f;   // Pro arrive -> T = 100
    m.speedToBall = 1750.f;  // (1750 - 1300) / (2200 - 1300) = 50 %
    m.lateralOffset = 45.f;  // (75 - 45) / 60 = 50 %
    m.reaction = 0.f;        // R = 100
    m.boostHeldRatio = 1.f;  // B = 100
    m.pathDevRms = 0.f;      // P = 100
    m.dodges = 1;            // not a speedflip attempt: flip = 60
    m.padsPicked = 1;
    m.type = KickoffType::FrontFlip;
    KickoffDuelResult d;
    d.outcome = KickoffDuel::Neutral;  // O = 55
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, &d);
    CHECK_NEAR(g.sTime, 100.f, 1e-3);
    CHECK_NEAR(g.sSpeed, 50.f, 1e-3);
    CHECK_NEAR(g.sContact, 50.f, 1e-3);
    CHECK_NEAR(g.sOutcome, 55.f, 1e-3);
    CHECK_NEAR(g.sExec, 0.2f * 100 + 0.2f * 100 + 0.2f * 100 + 0.4f * 60, 1e-3);
    const float expect = 0.40f * 100.f + 0.15f * (50.f + 50.f + 55.f + 84.f);
    CHECK_EQ(g.total, static_cast<int>(std::lround(expect)));
    // The biggest losses are the tips: low speed (7.5), centring (7.5 + half the outcome loss), flip into ball / try speedflip.
    CHECK(!g.tips.empty() && g.tips.size() <= 3);
    CHECK(hasTip(g, KickoffTip::OffCenterContact) || hasTip(g, KickoffTip::LowContactSpeed));
    // Outcome variants.
    KickoffDuelResult win;
    win.outcome = KickoffDuel::PlayerClear;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Pro, &win).sOutcome, 100.f, 1e-4);
    KickoffDuelResult narrow;
    narrow.outcome = KickoffDuel::PlayerNarrow;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Pro, &narrow).sOutcome, 80.f, 1e-4);
    KickoffDuelResult gn;
    gn.outcome = KickoffDuel::GhostNarrow;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Pro, &gn).sOutcome, 25.f, 1e-4);
    // Without a duel: the exit direction decides (30 deg -> 100, 150 deg -> 0), no exit = 50.
    m.hasBallExit = true;
    m.ballExitAngleDeg = 90.f;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Pro, nullptr).sOutcome, 50.f, 1e-3);
    m.ballExitAngleDeg = 10.f;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Pro, nullptr).sOutcome, 100.f, 1e-3);
    m.ballExitAngleDeg = 170.f;
    m.projectedY = -1500.f;
    const KickoffGrade own = kickoffGrade(m, KickoffLevel::Pro, nullptr);
    CHECK_NEAR(own.sOutcome, 0.f, 1e-3);
    CHECK(hasTip(own, KickoffTip::BallToOwnHalf));
    // Custom weights: only time counts.
    KickoffGradeWeights w;
    w.time = 1.f;
    w.speed = w.contact = w.outcome = w.exec = 0.f;
    CHECK_EQ(kickoffGrade(m, KickoffLevel::Pro, &d, w).total, 100);
    // Easier target level: same time scores 100, slower time scores less against Elite.
    m.timeToTouch = 2.07f;
    CHECK_NEAR(kickoffGrade(m, KickoffLevel::Standard, &d).sTime, 100.f, 1e-3);
    CHECK(kickoffGrade(m, KickoffLevel::Elite, &d).sTime < kickoffGrade(m, KickoffLevel::Pro, &d).sTime);
}

TEST(ko_grade_no_touch_and_tips) {
    KickoffMetrics m;
    m.spawn = KickoffSpawn::BackCenter;
    m.progress = 2225.f;  // half the path
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, nullptr);
    CHECK_EQ(g.total, 10);
    CHECK(g.tips.size() == 1 && g.tips[0].tip == KickoffTip::NoTouch);
    // Speedflip faults are surfaced: no cancel, too straight.
    KickoffMetrics s;
    s.spawn = KickoffSpawn::DiagonalRight;
    s.touched = true;
    s.timeToTouch = 1.95f;
    s.speedToBall = 2200.f;
    s.reaction = 0.f;
    s.boostHeldRatio = 1.f;
    s.padsPicked = 1;
    s.firstJump = 0.45f;
    s.dodge = 0.56f;
    s.dodgeSide = -0.3f;
    s.dodgeForward = 0.95f;
    s.dodgeAngleDeg = -2.f;
    s.cancel = -1.f;
    s.landing = 1.3f;
    s.dodges = 1;
    s.type = KickoffType::DiagonalFlip;
    KickoffDuelResult d;
    d.outcome = KickoffDuel::PlayerClear;
    const KickoffGrade gs = kickoffGrade(s, KickoffLevel::Pro, &d);
    CHECK(gs.speedflipAttempt);
    CHECK(hasTip(gs, KickoffTip::NoCancel));
    CHECK(hasTip(gs, KickoffTip::DodgeTooStraight));
    // Late reaction and no pad.
    KickoffMetrics r = s;
    r.reaction = 0.25f;
    r.padsPicked = 0;
    r.cancel = 0.59f;
    r.dodgeAngleDeg = -30.f;
    r.landing = 1.2f;
    const KickoffGrade gr = kickoffGrade(r, KickoffLevel::Pro, &d);
    CHECK(hasTip(gr, KickoffTip::SlowReaction));
    // A clean, fast speedflip only gets the congratulation.
    KickoffMetrics best = r;
    best.reaction = 0.f;
    best.padsPicked = 1;
    best.lateralOffset = 3.f;
    best.timeToTouch = 1.88f;
    best.speedToBall = 2250.f;
    best.landing = 1.3f;
    best.type = KickoffType::Speedflip;
    const KickoffGrade gb = kickoffGrade(best, KickoffLevel::Pro, &d);
    CHECK(gb.total >= 90);
    CHECK(gb.tips.size() == 1 && gb.tips[0].tip == KickoffTip::GreatKickoff);
    // Every tip has French text, a stable code and no em dash.
    for (int i = 0; i < static_cast<int>(KickoffTip::Count); ++i) {
        const auto t = static_cast<KickoffTip>(i);
        CHECK(std::string(kickoffTipText(t)).size() > 10);
        CHECK(std::string(kickoffTipCode(t)).rfind("kickoff.", 0) == 0);
        CHECK(!hasEmDash(kickoffTipText(t)));
    }
}

TEST(ko_splits_vs_reference) {
    const KickoffMetrics m = runRecorder(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, Script{});
    const KickoffGhost ref(
        KickoffSpawn::DiagonalRight, KickoffTeam::Blue,
        KickoffGhostProfile::fromBenchmark(kickoffBenchmark(KickoffSpawn::DiagonalRight, KickoffLevel::Pro)));
    const auto sp = kickoffSplits(m, ref);
    CHECK_EQ(sp.size(), size_t(4));
    for (const auto& s : sp) CHECK(s.reached);
    CHECK_NEAR(sp.back().delta, m.timeToTouch - 1.95f, 0.01);
    CHECK(sp[0].playerTime < sp[1].playerTime && sp[1].playerTime < sp[2].playerTime &&
          sp[2].playerTime < sp[3].playerTime);
    CHECK(sp[3].delta < 0.f);  // the synthetic speedflip beats the Pro benchmark
    CHECK_NEAR(kickoffTimeAtDistance({0.f, 10.f, 20.f}, 15.f), 1.5f * kTickDt, 1e-6);
    CHECK(kickoffTimeAtDistance({0.f, 10.f}, 50.f) < 0.f);
    CHECK(kickoffTimeAtDistance({}, 5.f) < 0.f);
}

TEST(ko_attempt_result_fields) {
    const KickoffMetrics m = runRecorder(KickoffSpawn::OffCenterLeft, KickoffTeam::Blue, [] {
        Script s;
        s.jumpAt = 0.47f;
        return s;
    }());
    const KickoffDuelResult d = duelVsPro(m);
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, &d);
    const AttemptResult r = kickoffMakeResult(m, g, &d, KickoffLevel::Pro, 12.5f);
    CHECK(r.module == "kickoff");
    CHECK(r.skill == "kickoff.decale");
    CHECK(r.title == "Kickoff (décalé gauche)");
    CHECK_NEAR(r.score, static_cast<float>(g.total), 1e-6);
    CHECK(r.verdict == verdictFromScore(r.score));
    CHECK(r.hasRecordValue && r.lowerIsBetter);
    CHECK_NEAR(r.valueForRecord, m.timeToTouch, 1e-6);
    CHECK_NEAR(r.time, 12.5f, 1e-6);
    bool hasTime = false, hasSpeed = false, hasAngle = false;
    for (const auto& x : r.metrics) {
        if (x.key == "time") hasTime = x.hasIdeal && std::fabs(x.ideal - 2.20f) < 1e-4f;
        if (x.key == "speed") hasSpeed = x.hasIdeal;
        if (x.key == "dodge_angle") hasAngle = std::fabs(x.ideal - 30.f) < 1e-4f;
        CHECK(!hasEmDash(x.label));
    }
    CHECK(hasTime && hasSpeed && hasAngle);
    CHECK_EQ(r.tips.size(), g.tips.size());
    for (const auto& t : r.tips) CHECK(t.code.rfind("kickoff.", 0) == 0);
    CHECK(kickoffAttemptTitle(KickoffSpawn::DiagonalLeft) == "Kickoff (diagonale gauche)");
    CHECK(kickoffSkillId(KickoffSpawn::BackCenter) == "kickoff.centre");
    CHECK(kickoffSkillId(KickoffSpawn::DiagonalRight) == "kickoff.diagonal");
}

TEST(ko_grade_blames_late_start_on_reaction) {
    // Same approach, but started 0.25 s late: the time lost is explained by the reaction, not by the approach.
    KickoffMetrics m;
    m.spawn = KickoffSpawn::DiagonalLeft;
    m.touched = true;
    m.timeToTouch = 1.95f + 0.25f;
    m.speedToBall = 2200.f;
    m.reaction = 0.25f;
    m.boostHeldRatio = 1.f;
    m.padsPicked = 1;
    m.dodges = 1;
    m.type = KickoffType::FrontFlip;
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Standard, nullptr);
    CHECK(!g.tips.empty());
    if (!g.tips.empty()) CHECK(g.tips[0].tip == KickoffTip::SlowReaction);
    for (const auto& t : g.tips) CHECK(t.tip != KickoffTip::LateArrival || t.loss < 3.f);
    // Same late time with an instant reaction: the approach is to blame.
    m.reaction = 0.f;
    const KickoffGrade g2 = kickoffGrade(m, KickoffLevel::Standard, nullptr);
    CHECK(!g2.tips.empty() && g2.tips[0].tip == KickoffTip::LateArrival);
    // The total does not depend on the attribution.
    m.reaction = 0.25f;
    KickoffMetrics m2 = m;
    CHECK_EQ(kickoffGrade(m, KickoffLevel::Standard, nullptr).total,
             kickoffGrade(m2, KickoffLevel::Standard, nullptr).total);
}
