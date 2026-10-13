// Regression tests for the review fixes: horizontal lateral contact offset on a rolled car, unlimited-boost runs
// kept out of the records, the missed-pad tip (visible, but not while a pad picked on a passive kickoff may still be
// respawning; the drill respawns every pad at each placement), the HUD prompts in the player's button style.
#include "core/common/Controls.h"
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

Actions start(Harness& h, int64_t& frame, std::vector<std::string> args = {}) {
    h.tick(makeSnap(++frame));
    return h.command("start", std::move(args));
}

bool hasTip(const KickoffGrade& g, KickoffTip t) {
    for (const auto& x : g.tips)
        if (x.tip == t) return true;
    return false;
}

const AttemptResult* lastAttempt(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

Script fastSpeedflip() {
    Script s;
    s.sideDodge = 0.55f;
    return s;
}

// Contact on the back-centre line (blue, heading +Y): car `lateral` uu to the LEFT of the ball (ball to its right),
// rolled by `rollDeg`, pitched by `pitchDeg`. Returns the recorded metrics.
KickoffMetrics contactWith(float rollDeg, float pitchDeg, float lateral, Vec3 vel = {0.f, 1800.f, 0.f}) {
    KickoffRecorder rec;
    rec.begin(KickoffSpawn::BackCenter, KickoffTeam::Blue, 0);
    const KickoffPath p = kickoffPath(KickoffSpawn::BackCenter, KickoffTeam::Blue);
    Snapshot a = makeSnap(0);
    a.car.pos = p.pointAt(0.f);
    a.car.vel = vel;
    rec.feed(a);
    Snapshot pre = makeSnap(250);
    // Left of the ball for a car heading +Y is +X (right = -X).
    pre.car.pos = Vec3{lateral, -kKoContactDist, kKoCarRestZ + 20.f};
    pre.car.vel = vel;
    pre.car.onGround = false;
    const Rot rot{static_cast<int>(std::lround(pitchDeg * 65536.f / 360.f)), 16384,
                  static_cast<int>(std::lround(rollDeg * 65536.f / 360.f))};
    pre.car.rot = rot;
    pre.car.basis = basisFromRot(rot);
    Snapshot post = makeSnap(252);
    post.ball.vel = {0.f, 2500.f, 0.f};
    rec.touch(pre, post);
    return rec.finish();
}

// Removes the boost pad pickups from a simulated attempt (as if the pads on the path were missed or empty).
void stripPads(SimOut& sim) {
    float removed = 0.f;
    for (size_t i = 1; i < sim.snaps.size(); ++i) {
        const float raw = sim.snaps[i].car.boost + removed;
        const float prevShown = sim.snaps[i - 1].car.boost;
        const float prevRaw = prevShown + removed;
        if (raw - prevRaw > 5.f) removed += raw - prevRaw;
        sim.snaps[i].car.boost = std::max(0.f, sim.snaps[i].car.boost - removed);
    }
}

// Unlimited boost: the tank shown by the game never moves.
void unlimitedBoost(SimOut& sim) {
    for (auto& s : sim.snaps) s.car.boost = kKoSpawnBoost;
}

} // namespace

// ---- Lateral offset measured in the horizontal plane -------------------------------------------------------------
TEST(ko_fix_lateral_offset_ignores_roll) {
    // Centred hit, car rolled (diagonal flip into the ball): the old basis.right projection read -26/-54/-72 uu.
    for (float roll : {0.f, 20.f, 45.f, 70.f, -45.f}) {
        const KickoffMetrics m = contactWith(roll, 0.f, 0.f);
        CHECK(m.touched);
        CHECK(std::fabs(m.lateralOffset) < 5.f);
        const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, nullptr);
        CHECK(g.sContact > 99.f);
        CHECK(!hasTip(g, KickoffTip::OffCenterContact));
        CHECK_NEAR(kickoffContactQuality(m.lateralOffset, false), kickoffContactQuality(0.f, false), 0.01);
    }
    // The height stays measured on the car's own up axis.
    CHECK(contactWith(0.f, 0.f, 0.f).contactHeight > 15.f);
    CHECK(contactWith(45.f, 0.f, 0.f).contactHeight < contactWith(0.f, 0.f, 0.f).contactHeight);
    // A real offset keeps its size and sign whatever the roll: ball 30 uu to the right of the car.
    for (float roll : {0.f, 45.f, -60.f}) CHECK_NEAR(contactWith(roll, 0.f, 30.f).lateralOffset, 30.f, 0.5);
    CHECK_NEAR(contactWith(30.f, 0.f, -40.f).lateralOffset, -40.f, 0.5);
    // Nose almost vertical (front flip into the ball): the heading comes from the velocity.
    CHECK_NEAR(contactWith(0.f, -80.f, 25.f).lateralOffset, 25.f, 0.5);
    // Degenerate: nose vertical and no velocity -> the path direction is the heading.
    CHECK_NEAR(contactWith(0.f, -89.f, 25.f, Vec3{}).lateralOffset, 25.f, 0.5);
}

TEST(ko_fix_rolled_contact_does_not_lose_the_duel) {
    // Same arrival as the ghost (inside the window): the power decides. A rolled but centred hit must weigh exactly
    // like a flat centred hit.
    const KickoffMetrics flatHit = contactWith(0.f, 0.f, 0.f);
    const KickoffMetrics rolled = contactWith(45.f, 0.f, 0.f);
    KickoffPlayerContact a, b;
    a.touched = b.touched = true;
    a.time = b.time = 2.48f;
    a.speedToBall = b.speedToBall = 2280.f;
    a.lateralOffset = flatHit.lateralOffset;
    b.lateralOffset = rolled.lateralOffset;
    a.flipIntoBall = b.flipIntoBall = true;
    const KickoffDuelResult da = kickoffResolveDuel(a, 2.49f, 2280.f);
    const KickoffDuelResult db = kickoffResolveDuel(b, 2.49f, 2280.f);
    CHECK(da.outcome == db.outcome);
    CHECK_NEAR(da.playerPower, db.playerPower, 1.f);
}

// ---- Unlimited boost never becomes a record ----------------------------------------------------------------------
TEST(ko_fix_stats_record_skips_unrealistic_runs) {
    KickoffStats st;
    KickoffMetrics m;
    m.spawn = KickoffSpawn::DiagonalLeft;
    m.touched = true;
    m.timeToTouch = 2.01f;
    m.speedToBall = 2000.f;
    m.along = {0.f, 10.f, 20.f, 40.f};
    CHECK(st.record(m, 70, nullptr));
    const std::vector<float> pbProfile = st.of(KickoffSpawn::DiagonalLeft).pbProfile;
    KickoffMetrics fast = m;
    fast.timeToTouch = 1.85f;
    fast.speedToBall = 2400.f;
    fast.along = {0.f, 30.f, 60.f, 90.f};
    KickoffDuelResult d;
    d.outcome = KickoffDuel::PlayerClear;
    CHECK(!st.record(fast, 95, &d, false));
    const KickoffSpawnStats& s = st.of(KickoffSpawn::DiagonalLeft);
    CHECK_EQ(s.attempts, 2);
    CHECK_EQ(s.touches, 2);
    CHECK_EQ(s.duels, 1);
    CHECK_EQ(s.wins, 1);
    CHECK_EQ(s.bestScore, 95);
    CHECK_EQ(s.recentScores.size(), size_t(2));
    CHECK_NEAR(s.bestTime, 2.01f, 1e-6);
    CHECK_NEAR(s.bestSpeed, 2000.f, 1e-3);
    CHECK_EQ(s.recentTimes.size(), size_t(1));  // the unrealistic time does not pull the average down
    CHECK_NEAR(s.avgTime(), 2.01f, 1e-6);
    CHECK(s.pbProfile == pbProfile);
    KickoffGhostProfile prof;
    CHECK(st.personalBestProfile(KickoffSpawn::DiagonalLeft, prof));
    // The AttemptResult of such a run carries no record value for the coach.
    fast.boostLooksUnlimited = true;
    const KickoffGrade g = kickoffGrade(fast, KickoffLevel::Pro, &d);
    const AttemptResult r = kickoffMakeResult(fast, g, &d, KickoffLevel::Pro, 3.f);
    CHECK(!r.hasRecordValue);
    fast.boostLooksUnlimited = false;
    CHECK(kickoffMakeResult(fast, g, &d, KickoffLevel::Pro, 3.f).hasRecordValue);
}

TEST(ko_fix_unlimited_boost_run_is_not_a_pb) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    // 1) Limited boost front flip: the first record.
    Script ff;
    ff.kind = Script::FrontFlip;
    ff.jumpAt = 0.70f;
    ff.dodgeGapTicks = 12;
    int64_t go = h.runCountdown(f);
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, ff, go, 4.f), f);
    CHECK(h.mod.lastResult().newPb);
    CHECK(!h.mod.lastResult().metrics.boostLooksUnlimited);
    const KickoffSpawnStats before = h.mod.stats().of(KickoffSpawn::DiagonalLeft);
    CHECK(before.bestTime > 1.95f);
    // 2) Faster speedflip, but the tank never drains (freeplay unlimited boost).
    h.command("next");
    h.tick(makeSnap(++f));
    go = h.runCountdown(f);
    SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    unlimitedBoost(sim);
    std::vector<Actions> acts;
    playAttempt(h, sim, f, &acts);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.metrics.touched);
    CHECK(v.metrics.boostLooksUnlimited);
    CHECK(v.metrics.timeToTouch < before.bestTime);  // faster, yet...
    CHECK(!v.newPb);
    const KickoffSpawnStats& after = h.mod.stats().of(KickoffSpawn::DiagonalLeft);
    CHECK_NEAR(after.bestTime, before.bestTime, 1e-6);
    CHECK_NEAR(after.bestSpeed, before.bestSpeed, 1e-3);
    CHECK(after.pbProfile == before.pbProfile);
    CHECK_EQ(after.recentTimes.size(), before.recentTimes.size());
    CHECK_EQ(after.attempts, 2);
    CHECK_EQ(after.touches, 2);
    // No record toast, no "Nouveau record", the chat says why; the coach gets no record value.
    bool recordToast = false, notCounted = false, newRecordLine = false;
    for (const auto& a : acts) {
        for (const auto& t : a.toasts) recordToast = recordToast || t.title.find("record") != std::string::npos;
        for (const auto& c : a.chat) {
            notCounted = notCounted || c.text.find("record non compté") != std::string::npos;
            newRecordLine = newRecordLine || c.text.find("Nouveau record") != std::string::npos;
            CHECK(!hasEmDash(c.text));
        }
    }
    CHECK(!recordToast);
    CHECK(notCounted);
    CHECK(!newRecordLine);
    const AttemptResult* r = lastAttempt(h);
    CHECK(r && !r->hasRecordValue);
    // The result card says it too.
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
    h.mod.draw(view, dc, dl);
    bool cardNote = false;
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines) cardNote = cardNote || l.text.find("record non compt") != std::string::npos;
    CHECK(cardNote);
    // 3) "Mon record" ghost replays the limited run, not the unlimited one.
    h.set("rlt_ko_ghost", "5");
    h.command("next");
    h.tick(makeSnap(++f));
    CHECK(!h.mod.status().ghostFallback);
    CHECK_NEAR(h.mod.ghost().arrivalTime(), before.bestTime, 1e-5);
}

TEST(ko_fix_passive_unlimited_run_is_not_a_pb) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    // Car resting on the back spawn, ball on the spot, drill not running: passive analysis arms.
    for (int i = 0; i < 5; ++i) h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
    SimOut sim = simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, fastSpeedflip(), f + 1, 4.f);
    unlimitedBoost(sim);
    // The simulator's first tick has the car still at rest (inputs pressed): the passive GO is seen on the next one.
    for (size_t i = 0; i < sim.snaps.size() && i < static_cast<size_t>(sim.touchIndex) + 4; ++i) {
        h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
    }
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.passive && v.metrics.touched);
    CHECK(v.metrics.boostLooksUnlimited);
    CHECK(!v.newPb);
    const KickoffSpawnStats& s = h.mod.stats().of(KickoffSpawn::BackCenter);
    CHECK_EQ(s.attempts, 1);
    CHECK_NEAR(s.bestTime, 0.f, 1e-6);
    CHECK(s.pbProfile.empty());
}

// ---- Missed pad tip ----------------------------------------------------------------------------------------------
TEST(ko_fix_missed_pad_tip_can_show) {
    KickoffMetrics m;
    m.spawn = KickoffSpawn::DiagonalRight;
    m.touched = true;
    m.timeToTouch = 1.88f;
    m.speedToBall = 2250.f;
    m.lateralOffset = 3.f;
    m.reaction = 0.f;
    m.boostHeldRatio = 1.f;
    m.firstJump = 0.45f;
    m.dodge = 0.56f;
    m.dodgeSide = -0.5f;
    m.dodgeForward = 0.87f;
    m.dodgeAngleDeg = -30.f;
    m.cancel = 0.59f;
    m.landing = 1.3f;
    m.dodges = 1;
    m.type = KickoffType::Speedflip;
    KickoffDuelResult d;
    d.outcome = KickoffDuel::PlayerClear;
    m.padsPicked = 1;
    CHECK(!hasTip(kickoffGrade(m, KickoffLevel::Pro, &d), KickoffTip::MissedPad));
    // Clean speedflip but the pad on the line was missed: the tip is shown (it used to be dropped under 2.5 points).
    m.padsPicked = 0;
    m.boostAtTouch = 8.f;
    const KickoffGrade g = kickoffGrade(m, KickoffLevel::Pro, &d);
    CHECK(hasTip(g, KickoffTip::MissedPad));
    float lossKept = 0.f;
    for (const auto& t : g.tips)
        if (t.tip == KickoffTip::MissedPad) lossKept = t.loss;
    CHECK(lossKept >= 2.5f);
    // Tank dry at the touch: the pad would have bought boost time, the tip weighs more.
    m.boostAtTouch = 0.f;
    const KickoffGrade dry = kickoffGrade(m, KickoffLevel::Pro, &d);
    float lossDry = 0.f;
    for (const auto& t : dry.tips)
        if (t.tip == KickoffTip::MissedPad) lossDry = t.loss;
    CHECK(lossDry > lossKept);
    // Not the player's fault: a pad picked on the previous rep may still be respawning, or boost is unlimited.
    KickoffMetrics cd = m;
    cd.padsMaybeOnCooldown = true;
    CHECK(!hasTip(kickoffGrade(cd, KickoffLevel::Pro, &d), KickoffTip::MissedPad));
    KickoffMetrics ul = m;
    ul.boostLooksUnlimited = true;
    CHECK(!hasTip(kickoffGrade(ul, KickoffLevel::Pro, &d), KickoffTip::MissedPad));
    // The grade total does not depend on the tip.
    CHECK_EQ(kickoffGrade(cd, KickoffLevel::Pro, &d).total, dry.total);
}

TEST(ko_fix_recorder_pad_cooldown_flag) {
    KickoffRecorderParams p;
    p.padsBusyUntilFrame = 1480;
    KickoffRecorder rec;
    rec.begin(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, 1200, p);
    CHECK(rec.metrics().padsMaybeOnCooldown);
    rec.begin(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, 1480, p);
    CHECK(!rec.metrics().padsMaybeOnCooldown);
    rec.begin(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, 1200);
    CHECK(!rec.metrics().padsMaybeOnCooldown);
    // The pickup frame is recorded.
    const KickoffMetrics m = runRecorder(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip());
    CHECK_EQ(m.padsPicked, 1);
    CHECK(m.lastPadFrame > 1000 && m.lastPadFrame < 1000 + 120);
}

TEST(ko_fix_missed_pad_in_drill_and_quick_next) {
    // Fresh drill, pad stripped from the run: the player is told about the pad.
    {
        Harness h;
        h.set("rlt_ko_ghost", "0");
        int64_t f = 0;
        start(h, f, {"diagonal_left"});
        const int64_t go = h.runCountdown(f);
        SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
        stripPads(sim);
        playAttempt(h, sim, f);
        const KickoffResultView& v = h.mod.lastResult();
        CHECK(v.valid && v.metrics.touched);
        CHECK_EQ(v.metrics.padsPicked, 0);
        CHECK(!v.metrics.padsMaybeOnCooldown);
        CHECK(hasTip(v.grade, KickoffTip::MissedPad));
    }
    // Rep 1 picks the pad, "next" right after the touch with a 1 s countdown: rep 2 starts < 4 s after the
    // pickup, but its placement respawned every pad, so missing the pad is blamed (it used to be excused).
    {
        Harness h;
        h.set("rlt_ko_ghost", "0");
        h.set("rlt_ko_countdown", "1");
        int64_t f = 0;
        start(h, f, {"diagonal_left"});
        int64_t go = h.runCountdown(f);
        const SimOut sim1 = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
        for (size_t i = 0; i < sim1.snaps.size(); ++i) {
            h.tick(sim1.snaps[i], static_cast<int>(i) == sim1.touchIndex);
            f = sim1.snaps[i].frame;
            if (h.mod.phase() == KickoffPhase::Done) break;
        }
        CHECK(h.mod.phase() == KickoffPhase::Done);
        const int64_t pickup = h.mod.lastResult().metrics.lastPadFrame;
        CHECK_EQ(h.mod.lastResult().metrics.padsPicked, 1);
        CHECK(pickup > 0);
        h.command("next");
        const Actions place = h.tick(makeSnap(++f));
        CHECK(h.mod.phase() == KickoffPhase::Countdown);
        CHECK(place.resetPickups);
        go = h.runCountdown(f);
        CHECK(go < pickup + 4 * 120);  // the pad would still be respawning without the reset
        SimOut sim2 = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
        stripPads(sim2);
        playAttempt(h, sim2, f);
        const KickoffResultView& v = h.mod.lastResult();
        CHECK(v.valid && v.metrics.touched);
        CHECK_EQ(v.metrics.padsPicked, 0);
        CHECK(!v.metrics.padsMaybeOnCooldown);
        CHECK(hasTip(v.grade, KickoffTip::MissedPad));
    }
}

namespace {

// Passive kickoff (drill not running): the car rests on `spawn` for a few ticks, then the simulated attempt runs
// until a few ticks after the touch. `strip` removes the pad pickups.
const KickoffResultView& passiveKickoff(Harness& h, int64_t& f, KickoffSpawn spawn, bool strip) {
    for (int i = 0; i < 5; ++i) h.tick(restingSnap(++f, spawn, KickoffTeam::Blue, false));
    SimOut sim = simulate(spawn, KickoffTeam::Blue, fastSpeedflip(), f + 1, 4.f);
    if (strip) stripPads(sim);
    for (size_t i = 0; i < sim.snaps.size() && i < static_cast<size_t>(sim.touchIndex) + 4; ++i) {
        h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
    }
    return h.mod.lastResult();
}

} // namespace

TEST(ko_fix_pad_cooldown_passive_only) {
    // Outside the drill the plugin does not touch the pads: a pad picked on the previous passive kickoff may still
    // be respawning, so missing it is not blamed.
    {
        Harness h;
        int64_t f = 0;
        const KickoffResultView& r1 = passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, false);
        CHECK(r1.valid && r1.passive && r1.metrics.touched);
        CHECK_EQ(r1.metrics.padsPicked, 1);
        CHECK(!r1.metrics.padsMaybeOnCooldown);
        const KickoffResultView& r2 = passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, true);
        CHECK(r2.valid && r2.passive && r2.metrics.touched);
        CHECK_EQ(r2.metrics.padsPicked, 0);
        CHECK(r2.metrics.padsMaybeOnCooldown);
        CHECK(!hasTip(r2.grade, KickoffTip::MissedPad));
        // Map change: the pickup memory is dropped.
        passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, false);
        h.game.mapName = "Park_P";
        h.event(GameEventKind::ContextChanged);
        h.game.mapName = "Stadium_P";
        h.event(GameEventKind::ContextChanged);
        const KickoffResultView& r3 = passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, true);
        CHECK(r3.valid && r3.passive && r3.metrics.touched);
        CHECK(!r3.metrics.padsMaybeOnCooldown);
        CHECK(hasTip(r3.grade, KickoffTip::MissedPad));
    }
    // A drill started right after a passive pickup: its placement respawned every pad, the earlier pickup is
    // forgotten and the missed pad is blamed.
    {
        Harness h;
        h.set("rlt_ko_ghost", "0");
        h.set("rlt_ko_countdown", "1");
        int64_t f = 0;
        const KickoffResultView& r1 = passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, false);
        CHECK_EQ(r1.metrics.padsPicked, 1);
        const int64_t pickup = r1.metrics.lastPadFrame;
        start(h, f, {"diagonal_left"});
        const int64_t go = h.runCountdown(f);
        CHECK(go < pickup + 4 * 120);
        SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
        stripPads(sim);
        playAttempt(h, sim, f);
        const KickoffResultView& v = h.mod.lastResult();
        CHECK(v.valid && !v.passive && v.metrics.touched);
        CHECK(!v.metrics.padsMaybeOnCooldown);
        CHECK(hasTip(v.grade, KickoffTip::MissedPad));
        // Stopped drill, then a passive kickoff that picks nothing: no pickup since the placement, nothing excused.
        h.command("stop");
        const KickoffResultView& r2 = passiveKickoff(h, f, KickoffSpawn::DiagonalLeft, true);
        CHECK(r2.valid && r2.passive && r2.metrics.touched);
        CHECK(!r2.metrics.padsMaybeOnCooldown);
    }
}

// ---- HUD prompts in the player's button style --------------------------------------------------------------------
TEST(ko_fix_hud_prompts_use_button_style) {
    Harness h;
    h.set("rlt_ko_auto_repeat", "0");
    h.set("rlt_ko_ghost", "0");
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{actionBindingName(GameAction::Boost), "XboxTypeS_B"},
                                 {actionBindingName(GameAction::Throttle), "XboxTypeS_RightTrigger"},
                                 {actionBindingName(GameAction::ResetShot), "XboxTypeS_Y"}};
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    auto panelText = [&]() {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        std::string all;
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines) all += l.text + "\n";
        return all;
    };
    int64_t f = 0;
    start(h, f, {"back"});
    for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
    CHECK(h.mod.phase() == KickoffPhase::Countdown);
    h.settings.profile.buttonStyle = 0;  // Auto: Xbox names
    std::string txt = panelText();
    CHECK(txt.find("Boost (B)") != std::string::npos);
    CHECK(txt.find("(RT)") != std::string::npos);
    h.settings.profile.buttonStyle = 2;  // PlayStation
    txt = panelText();
    CHECK(txt.find("Boost (Rond)") != std::string::npos);
    CHECK(txt.find("(R2)") != std::string::npos);
    CHECK(txt.find("Boost (B)") == std::string::npos);
    // Result shown, waiting for Reset (auto repeat off): the reset prompt follows the style too.
    const int64_t go = h.runCountdown(f);
    playAttempt(h, simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, fastSpeedflip(), go, 4.f), f);
    CHECK(h.mod.phase() == KickoffPhase::Done);
    for (int i = 0; i < 300; ++i) h.tick(makeSnap(++f));  // past the result delay: waiting for Reset
    CHECK(h.mod.phase() == KickoffPhase::Done);
    txt = panelText();
    CHECK(txt.find("Reset (Triangle)") != std::string::npos);
    h.settings.profile.buttonStyle = 1;  // Xbox
    txt = panelText();
    CHECK(txt.find("Reset (Y)") != std::string::npos);
}
