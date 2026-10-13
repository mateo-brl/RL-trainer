// Kickoff drill through the module API: activity gating, placement, freeze, countdown, GO timing, touches,
// ghost duel and ball impulses, auto reset, timeout, modes, commands, events, robustness, passive analysis.
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

// Ticks one idle frame (so the command has a snapshot context), then sends "start".
Actions start(Harness& h, int64_t& frame, std::vector<std::string> args = {}) {
    h.tick(makeSnap(++frame));
    return h.command("start", std::move(args));
}

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

int resultCount(const Harness& h) {
    int n = 0;
    for (const auto& a : h.log) n += static_cast<int>(a.results.size());
    return n;
}

bool anyWorldControl(const Actions& a) {
    return a.setBall || a.setCar || a.setBoost || a.overrideInputs || a.freezeCar || a.setGameSpeed ||
           a.countdownMessage || a.goMessage;
}

Script fastSpeedflip(float side = 0.55f) {
    Script s;
    s.sideDodge = side;
    return s;
}

} // namespace

TEST(ko_module_placement_only_after_activity_granted) {
    Harness h;
    int64_t f = 100;
    h.host.allow = false;
    Actions a = start(h, f, {"diagonal_left"});
    CHECK(!h.mod.running());
    CHECK_EQ(h.host.requests, 1);
    CHECK_EQ(a.toasts.size(), size_t(1));
    CHECK(!h.mod.status().message.empty());
    for (int i = 0; i < 20; ++i)
        CHECK(!anyWorldControl(h.tick(restingSnap(++f, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue))));
    // Context without world control (workshop, online): refused before even asking.
    h.host.allow = true;
    h.game.worldControlAllowed = false;
    start(h, f, {"diagonal_left"});
    CHECK(!h.mod.running());
    CHECK_EQ(h.host.requests, 1);
    // Non-standard map category (rings map with control allowed): refused.
    h.game.worldControlAllowed = true;
    h.game.category = MapCategory::Rings;
    start(h, f, {"diagonal_left"});
    CHECK(!h.mod.running());
    h.game.category = MapCategory::Standard;
    // Granted: placement on the next tick.
    a = start(h, f, {"diagonal_left"});
    CHECK(h.mod.running());
    CHECK(h.host.lastName == "Kickoff");
    CHECK(!a.setCar);  // nothing moves from the command itself
    a = h.tick(makeSnap(++f));
    CHECK(a.setCar.has_value());
    if (a.setCar) {
        const KickoffPose pose = kickoffPose(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue);
        CHECK_NEAR(a.setCar->pos.x, 2048.f, 1e-3);
        CHECK_NEAR(a.setCar->pos.y, -2560.f, 1e-3);
        CHECK_NEAR(a.setCar->pos.z, 20.f, 1e-3);
        CHECK(a.setCar->rot == pose.rot);
        CHECK_EQ(a.setCar->rot.yaw, 24576);
        CHECK(length(a.setCar->vel) == 0.f && length(a.setCar->angVel) == 0.f);
        CHECK(a.setCar->boost && std::fabs(*a.setCar->boost - 33.333f) < 0.01f);
    }
    CHECK(a.setBall.has_value());
    if (a.setBall) {
        CHECK_NEAR(a.setBall->pos.z, 93.15f, 1e-3);
        CHECK(length(flat(a.setBall->pos)) == 0.f && length(a.setBall->vel) == 0.f && length(a.setBall->angVel) == 0.f);
    }
    CHECK(a.setBoost && std::fabs(*a.setBoost - 33.333f) < 0.01f);
    CHECK(a.overrideInputs.has_value());  // inputs zeroed on the placement tick...
    CHECK(!a.freezeCar);                  // ...but the freeze starts after the placement
    CHECK(a.countdownMessage && *a.countdownMessage == 3);
    CHECK(h.mod.phase() == KickoffPhase::Countdown);
}

TEST(ko_module_freeze_and_countdown_schedule) {
    Harness h;
    int64_t f = 1000;
    start(h, f, {"diagonal_right"});
    std::vector<std::pair<int, int64_t>> messages;
    int64_t place = -1, go = -1;
    int freezeTicks = 0, holdTicks = 0, boostTicks = 0, ballPins = 0;
    bool freezeOutside = false;
    for (int i = 0; i < 500; ++i) {
        const Actions a = h.tick(restingSnap(++f, KickoffSpawn::DiagonalRight, KickoffTeam::Blue));
        if (place < 0 && a.setCar) place = f;
        if (a.countdownMessage) messages.push_back({*a.countdownMessage, f});
        if (a.goMessage) go = f;
        const bool inCountdown = place >= 0 && f > place && (go < 0 || f < go);
        if (a.freezeCar) {
            ++freezeTicks;
            if (!inCountdown) freezeOutside = true;
        } else if (inCountdown && go < 0) {
            freezeOutside = true;  // a countdown tick without freeze
        }
        if (inCountdown && go < 0 && a.setCar) ++holdTicks;
        if (inCountdown && go < 0 && a.setBoost) ++boostTicks;
        if (inCountdown && go < 0 && a.setBall) ++ballPins;
        if (go >= 0 && f > go + 30) break;
    }
    CHECK(place > 0);
    CHECK_EQ(go - place, int64_t(360));
    CHECK_EQ(messages.size(), size_t(3));
    if (messages.size() == 3) {
        CHECK(messages[0].first == 3 && messages[0].second == place);
        CHECK(messages[1].first == 2 && messages[1].second == place + 120);
        CHECK(messages[2].first == 1 && messages[2].second == place + 240);
    }
    CHECK_EQ(freezeTicks, 359);
    CHECK(!freezeOutside);
    CHECK_EQ(holdTicks, 359);   // placement re-applied every countdown tick
    CHECK_EQ(boostTicks, 359);  // boost refilled every countdown tick
    CHECK_EQ(ballPins, 359);
    CHECK(h.mod.phase() == KickoffPhase::Live);
    CHECK_EQ(h.mod.goFrame(), go);
    // GO tick: boost refilled, no freeze (pre-held inputs act now).
    const Actions& goActs = h.log[static_cast<size_t>(go - 1001)];
    CHECK(goActs.goMessage && !goActs.freezeCar && goActs.setBoost.has_value() && !goActs.setCar);
}

TEST(ko_module_hold_car_off_and_drift) {
    Harness h;
    h.set("rlt_ko_hold_car", "0");
    h.set("rlt_ko_native_countdown", "0");
    int64_t f = 0;
    start(h, f, {"back"});
    Actions a = h.tick(makeSnap(++f));
    CHECK(a.setCar && !a.countdownMessage);
    a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
    CHECK(a.freezeCar && !a.setCar);  // resting on the spawn: no re-pin
    Snapshot moved = restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue);
    moved.car.pos.x += 200.f;  // the player pressed the game's reset during the countdown
    a = h.tick(moved);
    CHECK(a.freezeCar && a.setCar.has_value());
    if (a.setCar) CHECK_NEAR(a.setCar->pos.x, 0.f, 1e-3);
}

TEST(ko_module_go_to_touch_timing_in_frames) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    int64_t f = 5000;
    start(h, f, {"diagonal_right"});
    const int64_t go = h.runCountdown(f);
    CHECK_EQ(go, h.mod.goFrame());
    const SimOut sim = simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, Script{}, go);
    CHECK(sim.touchIndex > 0);
    playAttempt(h, sim, f);
    const AttemptResult* r = lastResult(h);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->skill == "kickoff.diagonal");
    CHECK(r->title == "Kickoff (diagonale droite)");
    CHECK_NEAR(r->valueForRecord, static_cast<float>(sim.touchIndex) / 120.f, 1e-5);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && !v.passive && v.metrics.touched);
    CHECK_EQ(v.metrics.ticksToTouch, sim.touchIndex);
    CHECK(v.metrics.type == KickoffType::Speedflip);
    CHECK(!v.hasDuel);  // ghost off: outcome from the ball exit
    CHECK(v.grade.total >= 75);
    CHECK_EQ(resultCount(h), 1);
    // Chat line with time and score, then the main tip.
    bool chat = false;
    for (const auto& a : h.log)
        for (const auto& c : a.chat)
            if (c.text.find("Kickoff diagonale droite") != std::string::npos &&
                c.text.find("/100") != std::string::npos)
                chat = true;
    CHECK(chat);
    CHECK(h.mod.phase() == KickoffPhase::Done);
    // Status reflects the frozen chrono.
    CHECK_NEAR(h.mod.status().liveTime, static_cast<float>(sim.touchIndex) / 120.f, 1e-5);
}

TEST(ko_module_ghost_player_narrow_win_nudges_ball) {
    Harness h;  // default ghost: Pro
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    const int64_t go = h.runCountdown(f);
    CHECK(h.mod.ghostActive());
    CHECK_NEAR(h.mod.ghost().arrivalTime(), 1.95f, 1e-4);
    // The ghost starts from the mirrored spawn (orange diagonal left).
    CHECK_NEAR(h.mod.ghost().path().start.x, -2048.f, 1e-3);
    CHECK_NEAR(h.mod.ghost().path().start.y, 2560.f, 1e-3);
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    std::vector<Actions> acts;
    playAttempt(h, sim, f, &acts);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.hasDuel);
    CHECK(v.duel.outcome == KickoffDuel::PlayerNarrow);
    CHECK(v.duel.dt > 0.f && v.duel.dt < 0.10f);
    // The narrow-win nudge is applied at the ghost arrival (1.95 s), after the player's touch.
    int nudges = 0;
    for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].setBall) {
            ++nudges;
            CHECK_NEAR(static_cast<float>(i) / 120.f, 1.95f, 1.5f / 120.f);
            CHECK(acts[i].setBall->vel.z > 300.f);  // popped up
            CHECK(dot(flat(acts[i].setBall->vel), kickoffPath(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue).dir) >
                  0.f);
        }
    CHECK_EQ(nudges, 1);
}

TEST(ko_module_ghost_wins_clear_and_hits_ball) {
    Harness h;
    int64_t f = 0;
    start(h, f, {"diagonal_right"});
    const int64_t go = h.runCountdown(f);
    Script slow;
    slow.kind = Script::BoostOnly;  // ~2.13 s vs Pro 1.95 s
    const SimOut sim = simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, slow, go, 4.f);
    std::vector<Actions> acts;
    playAttempt(h, sim, f, &acts);
    int hits = 0;
    size_t hitIndex = 0;
    for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].setBall) {
            ++hits;
            hitIndex = i;
            CHECK(acts[i].setBall->vel.y < -1500.f);  // sent toward the blue half
            CHECK(acts[i].setBall->vel.z > 300.f);
        }
    CHECK_EQ(hits, 1);
    CHECK_NEAR(static_cast<float>(hitIndex) / 120.f, 1.95f, 1.5f / 120.f);
    // The result is produced at the ghost hit, with an estimated arrival.
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.hasDuel && v.duel.outcome == KickoffDuel::GhostClear);
    CHECK(v.metrics.estimated && !v.metrics.touched);
    CHECK(v.metrics.timeToTouch > 2.05f && v.metrics.timeToTouch < 2.3f);
    CHECK(v.grade.sOutcome == 0.f);
    CHECK_EQ(resultCount(h), 1);  // the later real touch does not grade a second time
    bool lateTip = false;
    for (const auto& t : v.grade.tips) lateTip = lateTip || t.tip == KickoffTip::LateArrival;
    CHECK(lateTip);
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success && !r->hasRecordValue);
}

TEST(ko_module_ghost_beginner_loses_clear_no_ball_change) {
    Harness h;
    h.set("rlt_ko_ghost", "1");  // Débutant: 2.40 s on the diagonal
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    const int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    std::vector<Actions> acts;
    playAttempt(h, sim, f, &acts);
    for (const auto& a : acts) CHECK(!a.setBall);
    CHECK(h.mod.lastResult().duel.outcome == KickoffDuel::PlayerClear);
    CHECK(h.mod.lastResult().grade.sOutcome == 100.f);
}

TEST(ko_module_ghost_hits_ball_setting_off) {
    Harness h;
    h.set("rlt_ko_ghost_hits_ball", "0");
    int64_t f = 0;
    start(h, f, {"diagonal_right"});
    const int64_t go = h.runCountdown(f);
    Script slow;
    slow.kind = Script::BoostOnly;
    std::vector<Actions> acts;
    playAttempt(h, simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, slow, go, 4.f), f, &acts);
    for (const auto& a : acts) CHECK(!a.setBall);
    // The ball stays put: the real (late) touch is graded, not an estimate, and the duel is still lost.
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.metrics.touched && !v.metrics.estimated);
    CHECK(v.duel.outcome == KickoffDuel::GhostClear);
    CHECK(v.duel.dt < -0.10f);
    CHECK_EQ(resultCount(h), 1);
}

TEST(ko_module_orange_mirroring) {
    Harness h;
    h.set("rlt_ko_team", "1");
    int64_t f = 0;
    start(h, f, {"diagonal_right"});
    const Actions a = h.tick(makeSnap(++f));
    CHECK(a.setCar.has_value());
    if (a.setCar) {
        CHECK_NEAR(a.setCar->pos.x, 2048.f, 1e-3);
        CHECK_NEAR(a.setCar->pos.y, 2560.f, 1e-3);
        CHECK_EQ(a.setCar->rot.yaw, -24576);
    }
    CHECK(h.mod.currentTeam() == KickoffTeam::Orange);
    // Ghost from the blue side.
    CHECK_NEAR(h.mod.ghost().path().start.x, -2048.f, 1e-3);
    CHECK_NEAR(h.mod.ghost().path().start.y, -2560.f, 1e-3);
    const int64_t go = h.runCountdown(f);
    playAttempt(h, simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Orange, Script{}, go, 4.f), f);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.metrics.touched && v.metrics.team == KickoffTeam::Orange);
    CHECK(v.metrics.type == KickoffType::Speedflip);
    // Same attempt as blue gives the same time.
    Harness hb;
    int64_t fb = 0;
    start(hb, fb, {"diagonal_right"});
    const int64_t gob = hb.runCountdown(fb);
    playAttempt(hb, simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, Script{}, gob, 4.f), fb);
    CHECK_NEAR(hb.mod.lastResult().metrics.timeToTouch, v.metrics.timeToTouch, 1e-5);
    CHECK_NEAR(hb.mod.lastResult().metrics.ballExitAngleDeg, v.metrics.ballExitAngleDeg, 0.01);
}

TEST(ko_module_activity_stopped_releases_freeze) {
    Harness h;
    int64_t f = 0;
    start(h, f, {"back"});
    h.tick(makeSnap(++f));
    Actions a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
    CHECK(a.freezeCar);
    h.host.steal();  // another module takes the activity
    CHECK(!h.mod.running());
    CHECK(h.mod.phase() == KickoffPhase::Idle);
    for (int i = 0; i < 10; ++i) {
        a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
        CHECK(!a.freezeCar && !a.setCar && !a.setBall && !a.overrideInputs);
    }
    // Stop command: releases the activity and the freeze.
    start(h, f, {"back"});
    h.tick(makeSnap(++f));
    a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
    CHECK(a.freezeCar);
    const int releases = h.host.releases;
    h.command("stop");
    CHECK(h.host.owner == nullptr);
    CHECK_EQ(h.host.releases, releases + 1);
    a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
    CHECK(!a.freezeCar && !a.setCar);
    // Ownership lost without notification (defensive): the drill stops by itself.
    start(h, f, {"back"});
    h.host.owner = nullptr;
    a = h.tick(makeSnap(++f));
    CHECK(!h.mod.running() && !a.freezeCar);
}

TEST(ko_module_auto_reset_after_delay) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    // Feed up to the result.
    int64_t doneFrame = -1;
    for (size_t i = 0; i < sim.snaps.size(); ++i) {
        h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
        if (h.mod.phase() == KickoffPhase::Done) {
            doneFrame = f;
            break;
        }
    }
    CHECK(doneFrame > 0);
    CHECK_EQ(doneFrame, go + sim.touchIndex + 2);  // touch resolved two ticks after the contact tick
    int64_t replace = -1;
    for (int i = 0; i < 400 && replace < 0; ++i) {
        const Actions a = h.tick(restingSnap(++f, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
        if (a.setCar) replace = f;
    }
    CHECK_EQ(replace - doneFrame, int64_t(240));  // reset delay 2 s
    CHECK(h.mod.phase() == KickoffPhase::Countdown);
    // Auto repeat off: waits for Reset.
    h.set("rlt_ko_auto_repeat", "0");
    go = h.runCountdown(f);
    const SimOut sim2 = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    for (size_t i = 0; i < sim2.snaps.size(); ++i) {
        h.tick(sim2.snaps[i], static_cast<int>(i) == sim2.touchIndex);
        f = sim2.snaps[i].frame;
    }
    for (int i = 0; i < 600; ++i) CHECK(!h.tick(makeSnap(++f)).setCar);
    CHECK(h.mod.phase() == KickoffPhase::Done);
    h.event(GameEventKind::ResetShot);
    const Actions a = h.tick(makeSnap(++f));
    CHECK(a.setCar.has_value());
}

TEST(ko_module_timeout_without_touch) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_timeout", "3");
    int64_t f = 0;
    start(h, f, {"offcenter_right"});
    const int64_t go = h.runCountdown(f, false);
    int64_t resultFrame = -1;
    for (int i = 0; i < 500 && resultFrame < 0; ++i) {
        const Actions a = h.tick(restingSnap(++f, KickoffSpawn::OffCenterRight, KickoffTeam::Blue, false));
        if (!a.results.empty()) resultFrame = f;
    }
    CHECK_EQ(resultFrame - go, int64_t(362));  // 3 s + the two-tick touch resolution margin
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && !v.metrics.touched && !v.metrics.estimated);
    CHECK(v.grade.total <= 20);
    CHECK(!v.grade.tips.empty() && v.grade.tips[0].tip == KickoffTip::NoTouch);
    CHECK(v.metrics.type == KickoffType::Fake);
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success && r->skill == "kickoff.decale");
}

TEST(ko_module_sequence_mode_runs_five_spawns) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_timeout", "3");
    h.set("rlt_ko_reset_delay", "0.5");
    int64_t f = 0;
    start(h, f, {"sequence"});
    CHECK(h.mod.mode() == KickoffMode::Sequence);
    std::vector<KickoffSpawn> order;
    bool summary = false;
    for (int i = 0; i < 5000 && h.mod.running(); ++i) {
        const Actions a = h.tick(makeSnap(++f));
        if (a.overrideInputs) order.push_back(h.mod.currentSpawn());  // placement tick only
        for (const auto& t : a.toasts)
            if (t.text.find("Série terminée") != std::string::npos) summary = true;
    }
    CHECK_EQ(order.size(), size_t(5));
    for (size_t i = 0; i < order.size(); ++i) CHECK(order[i] == kickoffSpawnFromIndex(static_cast<int>(i)));
    CHECK(summary);
    CHECK(!h.mod.running());
    CHECK(h.host.owner == nullptr);
    CHECK_EQ(resultCount(h), 5);
}

TEST(ko_module_weakness_and_random_modes) {
    Harness h;
    Store st;
    // Every spawn played with good scores except off-centre right.
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const std::string p = std::string("kickoff.") + kickoffSpawnInfo(kickoffSpawnFromIndex(i)).key + ".";
        st.setInt(p + "attempts", 4);
        st.setString(p + "recent_scores", i == 3 ? "30 35 40 20" : "85 90 80 88");
    }
    h.mod.load(st);
    CHECK(h.mod.stats().weakest() == KickoffSpawn::OffCenterRight);
    int64_t f = 0;
    start(h, f, {"weakness"});
    h.tick(makeSnap(++f));
    CHECK(h.mod.currentSpawn() == KickoffSpawn::OffCenterRight);
    // Unplayed spawns come first.
    Harness h2;
    CHECK(h2.mod.stats().weakest() == KickoffSpawn::DiagonalLeft);
    // Random mode covers several spawns.
    Harness h3;
    h3.set("rlt_ko_ghost", "0");
    h3.set("rlt_ko_reset_delay", "0.5");
    int64_t f3 = 0;
    start(h3, f3, {"random"});
    std::array<int, kKoSpawnCount> seen{};
    for (int rep = 0; rep < 30; ++rep) {
        h3.command("next");
        h3.tick(makeSnap(++f3));
        ++seen[static_cast<size_t>(kickoffIndex(h3.mod.currentSpawn()))];
    }
    int distinct = 0;
    for (int c : seen) distinct += c > 0 ? 1 : 0;
    CHECK(distinct >= 4);
}

TEST(ko_module_commands) {
    Harness h;
    int64_t f = 0;
    h.tick(makeSnap(++f));
    Actions a = h.command("start", {"nowhere"});
    CHECK(!h.mod.running() && a.toasts.size() == 1 && a.toasts[0].kind == ToastKind::Warning);
    a = h.command("ghost", {"elite"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Elite);
    CHECK(h.mod.status().ghostOverridden);
    a = h.command("ghost", {"banana"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Elite && a.toasts[0].kind == ToastKind::Warning);
    h.command("ghost", {"off"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Off);
    h.command("ghost", {"pb"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::PersonalBest);
    h.command("ghost", {"beginner"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Beginner);
    h.command("ghost", {"standard"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Standard);
    h.command("ghost", {"adaptive"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Adaptive);
    h.command("ghost", {"pro"});
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Pro);
    // Changing the setting wins over the console override; other settings do not clear it.
    h.command("ghost", {"elite"});
    h.set("rlt_ko_hud_x", "0.2");
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Elite);
    h.set("rlt_ko_ghost", "2");
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Standard);
    CHECK(!h.mod.status().ghostOverridden);
    // Ghost mode used from the next placement.
    start(h, f, {"back"});
    h.tick(makeSnap(++f));
    CHECK(h.mod.ghostActive());
    CHECK_NEAR(h.mod.ghost().arrivalTime(), 2.63f, 1e-4);
    // "next" skips to a new placement.
    h.command("next");
    a = h.tick(makeSnap(++f));
    CHECK(a.setCar.has_value());
    // Unknown command.
    a = h.command("dance");
    CHECK(a.toasts.size() == 1);
    // reset_stats
    Store st;
    st.setInt("kickoff.back.attempts", 7);
    st.setFloat("kickoff.back.best_time", 2.5f);
    h.mod.load(st);
    CHECK_EQ(h.mod.stats().of(KickoffSpawn::BackCenter).attempts, 7);
    a = h.command("reset_stats");
    CHECK(a.requestSave);
    CHECK_EQ(h.mod.stats().totalAttempts(), 0);
    CHECK(h.mod.stats().of(KickoffSpawn::BackCenter).bestTime == 0.f);
    // Mode names come from the setting when no argument is given.
    h.command("stop");
    h.set("rlt_ko_mode", "0");
    h.set("rlt_ko_spawn", "4");
    start(h, f);
    h.tick(makeSnap(++f));
    CHECK(h.mod.mode() == KickoffMode::Chosen && h.mod.currentSpawn() == KickoffSpawn::BackCenter);
}

TEST(ko_module_context_change_and_events) {
    Harness h;
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    h.tick(makeSnap(++f));
    CHECK(h.mod.running());
    // Map change: transient state reset, activity released.
    h.game.mapName = "Park_P";
    h.event(GameEventKind::ContextChanged);
    CHECK(!h.mod.running());
    CHECK(h.host.owner == nullptr);
    // Losing world control (e.g. switched to a workshop map): stop.
    start(h, f, {"diagonal_left"});
    h.tick(makeSnap(++f));
    h.game.worldControlAllowed = false;
    h.event(GameEventKind::ContextChanged);
    CHECK(!h.mod.running());
    h.game.worldControlAllowed = true;
    // Map unloaded.
    start(h, f, {"diagonal_left"});
    h.event(GameEventKind::MapUnloaded);
    CHECK(!h.mod.running());
    // Frame counter going backwards (new map): reset.
    start(h, f, {"diagonal_left"});
    h.tick(makeSnap(++f));
    h.tick(makeSnap(3));
    CHECK(!h.mod.running());
    // Module disabled mid-drill.
    f = 20;
    start(h, f, {"diagonal_left"});
    h.tick(makeSnap(++f));
    h.set("rlt_ko_enabled", "0");
    const Actions a = h.tick(makeSnap(++f));
    CHECK(!h.mod.running() && !a.freezeCar);
    h.set("rlt_ko_enabled", "1");
}

TEST(ko_module_reset_shot_and_teleport_restart_same_spawn) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_mode", "1");  // random: a restart must keep the same spawn anyway
    int64_t f = 0;
    start(h, f);
    int64_t go = h.runCountdown(f);
    const KickoffSpawn sp = h.mod.currentSpawn();
    const SimOut sim = simulate(sp, KickoffTeam::Blue, Script{}, go, 4.f);
    for (int i = 0; i < 60; ++i) h.tick(sim.snaps[static_cast<size_t>(i)]);
    f = sim.snaps[59].frame;
    h.event(GameEventKind::ResetShot);
    Actions a = h.tick(makeSnap(++f));
    CHECK(a.setCar.has_value());
    CHECK(h.mod.currentSpawn() == sp);
    CHECK_EQ(resultCount(h), 0);
    // Teleport during the live phase (car jumps 2000 uu in one tick): same spawn again, no result.
    go = h.runCountdown(f);
    const SimOut sim2 = simulate(sp, KickoffTeam::Blue, Script{}, go, 4.f);
    for (int i = 0; i < 30; ++i) h.tick(sim2.snaps[static_cast<size_t>(i)]);
    Snapshot jump = sim2.snaps[30];
    jump.car.pos = jump.car.pos + Vec3{2000.f, 0.f, 0.f};
    h.tick(jump);
    f = jump.frame;
    CHECK(h.mod.phase() == KickoffPhase::Placing);
    a = h.tick(makeSnap(++f));
    CHECK(a.setCar && h.mod.currentSpawn() == sp);
    CHECK_EQ(resultCount(h), 0);
}

TEST(ko_module_invalid_car_and_ball_robustness) {
    Harness h;
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    const int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f);
    // Ten invalid ticks in the middle of the approach do not break the attempt.
    for (size_t i = 0; i < sim.snaps.size(); ++i) {
        Snapshot s = sim.snaps[i];
        if (i >= 50 && i < 60) {
            s.car.valid = false;
            s.ball.valid = false;
        }
        h.tick(s, static_cast<int>(i) == sim.touchIndex);
        f = s.frame;
        if (h.mod.phase() == KickoffPhase::Done) break;
    }
    CHECK(h.mod.lastResult().valid && h.mod.lastResult().metrics.touched);
    // A car missing for more than a second restarts the rep.
    for (int i = 0; i < 800 && h.mod.phase() != KickoffPhase::Live; ++i)
        h.tick(restingSnap(++f, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
    CHECK(h.mod.phase() == KickoffPhase::Live);
    for (int i = 0; i < 125; ++i) {
        Snapshot s = makeSnap(++f);
        s.car.valid = false;
        h.tick(s);
    }
    CHECK(h.mod.phase() == KickoffPhase::Placing || h.mod.phase() == KickoffPhase::Countdown);
    // No snapshot yet: nothing happens.
    Harness h2;
    Actions a;
    TickContext c{h2.latest, h2.history, h2.settings, h2.game, h2.profile, false, false, true};
    h2.mod.onTick(c, a);
    CHECK(a.empty());
}

TEST(ko_module_go_jitter) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_go_jitter", "1");
    h.set("rlt_ko_go_jitter_max", "0.4");
    int64_t f = 0;
    start(h, f, {"back"});
    int minD = 1 << 30, maxD = -(1 << 30);
    for (int rep = 0; rep < 30; ++rep) {
        h.command("next");
        Actions a = h.tick(makeSnap(++f));
        CHECK(a.setCar.has_value());
        const int d = static_cast<int>(h.mod.goFrame() - h.mod.nominalGoFrame());
        minD = std::min(minD, d);
        maxD = std::max(maxD, d);
        // "1" still comes 240 ticks after the placement (nominal schedule).
        int64_t one = -1;
        for (int i = 0; i < 300 && one < 0; ++i) {
            a = h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
            if (a.countdownMessage && *a.countdownMessage == 1) one = f;
        }
        CHECK_EQ(one - h.mod.placeFrame(), int64_t(240));
    }
    CHECK(minD >= 0 && maxD <= 48 && maxD > minD);
}

TEST(ko_module_pb_ghost_and_adaptive) {
    Harness h;
    h.set("rlt_ko_ghost", "5");  // Mon record
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    h.tick(makeSnap(++f));
    CHECK(h.mod.status().ghostFallback);  // no record yet: Pro ghost
    CHECK_NEAR(h.mod.ghost().arrivalTime(), 1.95f, 1e-4);
    int64_t go = h.runCountdown(f);
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f), f);
    const float pb = h.mod.stats().of(KickoffSpawn::DiagonalLeft).bestTime;
    CHECK(pb > 1.8f && pb < 1.95f);
    CHECK(h.mod.lastResult().newPb);
    CHECK(!h.mod.stats().of(KickoffSpawn::DiagonalLeft).pbProfile.empty());
    // Next rep: the ghost replays the record.
    h.command("next");
    h.tick(makeSnap(++f));
    CHECK(!h.mod.status().ghostFallback);
    CHECK(h.mod.ghost().model() == KickoffGhostModel::Samples);
    CHECK_NEAR(h.mod.ghost().arrivalTime(), pb, 1e-5);
    // Same attempt again: arrives at exactly the PB time -> neutral-ish duel, not a new record.
    go = h.runCountdown(f);
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), go, 4.f), f);
    CHECK(!h.mod.lastResult().newPb);
    CHECK(std::fabs(h.mod.lastResult().duel.dt) < 1e-4f);

    // Adaptive ghost: wins raise the level.
    Harness ha;
    ha.set("rlt_ko_ghost", "6");
    int64_t fa = 0;
    const float before = ha.mod.adaptiveLevel();
    start(ha, fa, {"diagonal_left"});
    for (int rep = 0; rep < 3; ++rep) {
        const int64_t g = ha.runCountdown(fa);
        playAttempt(ha, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fastSpeedflip(), g, 4.f), fa);
        CHECK(kickoffDuelWon(ha.mod.lastResult().duel.outcome));
        ha.command("next");
        ha.tick(makeSnap(++fa));
    }
    CHECK(ha.mod.adaptiveLevel() > before);
}

TEST(ko_module_passive_analysis_freeplay_reset) {
    Harness h;  // drill not running
    h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    // Freeplay reset: car at rest on a spawn, ball at rest on the centre spot.
    for (int i = 0; i < 5; ++i) {
        Snapshot s = restingSnap(++f, KickoffSpawn::OffCenterLeft, KickoffTeam::Blue, false);
        CHECK(!anyWorldControl(h.tick(s)));
    }
    // The player drives: the simulator's first tick has the car still at rest (inputs pressed), it moves next tick.
    const SimOut sim = simulate(
        KickoffSpawn::OffCenterLeft, KickoffTeam::Blue,
        [] {
            Script s;
            s.jumpAt = 0.47f;
            return s;
        }(),
        f + 1, 4.f);
    std::vector<Actions> acts;
    for (size_t i = 0; i < sim.snaps.size(); ++i) {
        acts.push_back(h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex));
        f = sim.snaps[i].frame;
    }
    for (const auto& a : acts) CHECK(!anyWorldControl(a));
    CHECK_EQ(resultCount(h), 1);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.passive && v.metrics.touched);
    CHECK(v.metrics.spawn == KickoffSpawn::OffCenterLeft);
    CHECK(v.metrics.type == KickoffType::Speedflip);
    CHECK_NEAR(v.metrics.timeToTouch, static_cast<float>(sim.touchIndex) / 120.f, 2.f / 120.f);
    // No chat in passive mode.
    for (const auto& a : acts) CHECK(a.chat.empty());
    CHECK_EQ(h.mod.stats().of(KickoffSpawn::OffCenterLeft).attempts, 1);
    // Draw: result card shown for a few seconds.
    DrawList dl;
    ViewContext view;
    view.valid = true;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, false, 1.f};
    h.mod.draw(view, dc, dl);
    CHECK_EQ(dl.panels.size(), size_t(1));
}

TEST(ko_module_passive_analysis_opponent_first_and_gating) {
    Harness h;
    h.game.mode = GameMode::OfflineMatch;
    h.game.worldControlAllowed = false;
    int64_t f = 0;
    for (int i = 0; i < 5; ++i) h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Orange, false));
    Script slow;
    slow.kind = Script::BoostOnly;
    SimOut sim = simulate(KickoffSpawn::BackCenter, KickoffTeam::Orange, slow, f + 1, 4.f);
    // The opponent hits the ball at 2.3 s: the ball flies toward us before our touch.
    const int hit = ticksOf(2.3f);
    for (size_t i = static_cast<size_t>(hit); i < sim.snaps.size(); ++i) sim.snaps[i].ball.vel = {0.f, 2500.f, 400.f};
    for (int i = 0; i <= hit + 6 && i < sim.touchIndex; ++i) h.tick(sim.snaps[static_cast<size_t>(i)]);
    CHECK_EQ(resultCount(h), 1);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.passive && v.hasDuel && v.duel.outcome == KickoffDuel::GhostClear);
    CHECK(v.metrics.estimated);
    CHECK(v.opponent == "adversaire");
    // Passive analysis off: nothing.
    Harness off;
    off.set("rlt_ko_passive", "0");
    int64_t g = 0;
    for (int i = 0; i < 5; ++i) off.tick(restingSnap(++g, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
    for (const auto& s : simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, g + 1, 4.f).snaps)
        off.tick(s, false);
    CHECK_EQ(resultCount(off), 0);
    CHECK(!off.mod.passiveLive());
    // Workshop maps: no passive analysis.
    Harness ws;
    ws.game.mode = GameMode::Workshop;
    ws.game.category = MapCategory::Rings;
    int64_t w = 0;
    for (int i = 0; i < 5; ++i) ws.tick(restingSnap(++w, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
    ws.tick(simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, w + 1, 4.f).snaps[3]);
    CHECK(!ws.mod.passiveLive());
    // Driving around away from the ball (no touch, ball never moves): nothing reported.
    Harness away;
    int64_t a = 0;
    for (int i = 0; i < 5; ++i) away.tick(restingSnap(++a, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, false));
    Script fake;
    fake.kind = Script::Fake;
    for (const auto& s : simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, fake, a + 1, 8.f).snaps) away.tick(s);
    CHECK_EQ(resultCount(away), 0);
}

TEST(ko_module_passive_ignores_vertical_settling_and_duel_stats) {
    Harness h;
    h.game.mode = GameMode::OfflineMatch;
    h.game.worldControlAllowed = false;
    int64_t f = 0;
    // Car settling on its suspension after the spawn: vertical speed only, no GO.
    for (int i = 0; i < 20; ++i) {
        Snapshot s = restingSnap(++f, KickoffSpawn::DiagonalRight, KickoffTeam::Blue, false);
        s.car.vel = {0.f, 0.f, i < 10 ? -60.f : 0.f};
        h.tick(s);
        CHECK(!h.mod.passiveLive());
    }
    // Opponent first while we were going for the ball: graded, but not counted as a ghost duel.
    Script slow;
    slow.kind = Script::BoostOnly;
    SimOut sim = simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, slow, f + 1, 4.f);
    const int hit = ticksOf(1.9f);
    for (size_t i = static_cast<size_t>(hit); i < sim.snaps.size(); ++i)
        sim.snaps[i].ball.vel = {-1500.f, -2000.f, 500.f};
    for (int i = 0; i <= hit + 6; ++i) h.tick(sim.snaps[static_cast<size_t>(i)]);
    CHECK_EQ(resultCount(h), 1);
    const KickoffSpawnStats& st = h.mod.stats().of(KickoffSpawn::DiagonalRight);
    CHECK_EQ(st.attempts, 1);
    CHECK_EQ(st.duels, 0);
    CHECK_EQ(st.losses, 0);
    CHECK(st.bestTime == 0.f);  // an estimated time is never a record
}

TEST(ko_module_touch_at_timeout_edge_counts) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_timeout", "3");
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    const int64_t go = h.runCountdown(f, false);
    // Car parked right at the contact point; it touches the ball on the last tick before the timeout.
    const KickoffPath p = kickoffPath(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue);
    const Rot rot = rotFromDirection(p.dir);
    for (int i = 0; i <= 362; ++i) {
        Snapshot s = makeSnap(go + i);
        s.car.pos = p.pointAt(p.length);
        s.car.rot = rot;
        s.car.basis = basisFromRot(rot);
        s.car.vel = p.dir * 100.f;
        h.tick(s, i == 359);
        f = s.frame;
    }
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.metrics.touched);
    CHECK_EQ(v.metrics.ticksToTouch, 359);
    CHECK_EQ(resultCount(h), 1);
}

TEST(ko_module_feedback_toggles_and_pb_toast) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    // First attempt: a slower front flip (record, but no toast for the very first time).
    Script ff;
    ff.kind = Script::FrontFlip;
    ff.jumpAt = 0.70f;
    ff.dodgeGapTicks = 12;
    int64_t go = h.runCountdown(f);
    std::vector<Actions> acts;
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, ff, go, 4.f), f, &acts);
    int toasts = 0;
    for (const auto& a : acts) toasts += static_cast<int>(a.toasts.size());
    CHECK_EQ(toasts, 0);
    CHECK(h.mod.lastResult().newPb);
    // Second attempt: faster speedflip -> new record toast.
    h.command("next");
    h.tick(makeSnap(++f));
    go = h.runCountdown(f);
    acts.clear();
    Script sf;
    sf.sideDodge = 0.55f;
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, sf, go, 4.f), f, &acts);
    bool pbToast = false;
    for (const auto& a : acts)
        for (const auto& t : a.toasts) pbToast = pbToast || t.title.find("record") != std::string::npos;
    CHECK(pbToast);
    // Chat off: no chat line; toasts off: no record toast.
    h.set("rlt_ko_chat", "0");
    h.set("rlt_ko_record_toasts", "0");
    h.command("next");
    h.tick(makeSnap(++f));
    go = h.runCountdown(f);
    acts.clear();
    Script faster = sf;
    faster.jumpAt = 0.44f;
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, faster, go, 4.f), f, &acts);
    for (const auto& a : acts) CHECK(a.chat.empty() && a.toasts.empty());
    CHECK_EQ(resultCount(h), 3);  // results are always reported to the coach
}

TEST(ko_module_ghost_close_race_waits_for_touch) {
    Harness h;  // Pro ghost: 1.95 s on the diagonal
    int64_t f = 0;
    start(h, f, {"diagonal_left"});
    const int64_t go = h.runCountdown(f, false);
    Script late = fastSpeedflip();
    late.reaction = 0.12f;  // arrives ~0.05 s after the ghost: inside the 0.10 s window
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, late, go, 4.f);
    std::vector<Actions> acts;
    playAttempt(h, sim, f, &acts);
    const KickoffResultView& v = h.mod.lastResult();
    CHECK(v.valid && v.metrics.touched && !v.metrics.estimated);  // the ghost waited for the real touch
    CHECK(v.duel.dt < 0.f && v.duel.dt > -0.10f);
    CHECK(v.duel.outcome == KickoffDuel::Neutral || v.duel.outcome == KickoffDuel::GhostNarrow);
    // The ball is nudged once, after the player's touch was known (touch tick + 2), not at the ghost arrival.
    int nudges = 0;
    size_t at = 0;
    for (size_t i = 0; i < acts.size(); ++i)
        if (acts[i].setBall) {
            ++nudges;
            at = i;
        }
    CHECK_EQ(nudges, 1);
    CHECK(static_cast<int>(at) >= sim.touchIndex + 2);
    CHECK(static_cast<float>(at) / 120.f > 1.95f);
    // The reaction was slow: the player is told so.
    bool reactionTip = false;
    for (const auto& t : v.grade.tips) reactionTip = reactionTip || t.tip == KickoffTip::SlowReaction;
    CHECK(reactionTip);
}

TEST(ko_module_chat_lines_content) {
    Harness h;
    int64_t f = 0;
    start(h, f, {"diagonal_right"});
    int64_t go = h.runCountdown(f);
    Script slow;
    slow.kind = Script::BoostOnly;
    std::vector<Actions> acts;
    playAttempt(h, simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, slow, go, 4.f), f, &acts);
    std::vector<std::string> lines;
    for (const auto& a : acts)
        for (const auto& c : a.chat) lines.push_back(c.text);
    CHECK_EQ(lines.size(), size_t(2));
    if (lines.size() == 2) {
        CHECK(lines[0].find("Kickoff diagonale droite : ~") == 0);
        CHECK(lines[0].find("(estimé)") != std::string::npos);
        CHECK(lines[0].find("kickoff perdu (net)") != std::string::npos);
        CHECK(lines[1].find("Conseil : ") == 0);
        CHECK(!hasEmDash(lines[0]) && !hasEmDash(lines[1]));
    }
    // No touch at all (ghost off): "balle pas touchée".
    Harness h2;
    h2.set("rlt_ko_ghost", "0");
    h2.set("rlt_ko_timeout", "3");
    int64_t f2 = 0;
    start(h2, f2, {"back"});
    h2.runCountdown(f2, false);
    std::string first;
    for (int i = 0; i < 400 && first.empty(); ++i) {
        const Actions a = h2.tick(restingSnap(++f2, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
        if (!a.chat.empty()) first = a.chat[0].text;
    }
    CHECK(first.find("balle pas touchée") != std::string::npos);
    CHECK(first.find("/100") != std::string::npos);
}
