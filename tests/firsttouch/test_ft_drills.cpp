// Drills: catalogue, spawns relative to the car pose, adaptive level, and the drill state machine
// (activity gating with a fake host, timeout, success/failure, automatic next rep, stop paths).

#include <cmath>

#include "tests/firsttouch/ft_test_util.h"

using namespace rlt;
using namespace rlt::ft;
using namespace fttest;

namespace {

DrillPose originPose(float yawDeg = 0.f, Vec3 pos = Vec3{0.f, 0.f, kCarZ}) {
    return poseFromCar(groundCar(pos, Vec3{}, yawDeg));
}

bool insideArena(const Vec3& p) {
    return std::fabs(p.x) <= 3990.5f && std::fabs(p.y) <= 4990.5f && std::fabs(p.x) + std::fabs(p.y) <= 7881.f &&
           p.z >= kBallZ - 0.01f && p.z <= 1900.5f;
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

Snapshot baseSnap(int64_t frame, Inputs in = {}) {
    Snapshot s = makeSnap(frame, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 2000.f, kBallZ}, Vec3{}));
    s.in = in;
    return s;
}

Inputs throttle() {
    Inputs in;
    in.throttle = 1.f;
    return in;
}

bool hasToast(const Actions& a, ToastKind kind, const std::string& sub = "") {
    for (const auto& t : a.toasts)
        if (t.kind == kind && (sub.empty() || contains(t.text, sub))) return true;
    return false;
}

// Starts a drill at frame 100 and runs the placement tick (frame 101). Returns the placement actions.
Actions startDrill(Harness& h, const std::string& type) {
    h.set("rlt_ft_drill_seed", "1234");
    h.tick(baseSnap(100));
    const Actions cmd = h.command("drill", {type});
    CHECK(!cmd.setBall);  // nothing moves before the next tick
    return h.tick(baseSnap(101));
}

// Ticks with (or without) input from `from` to `to` inclusive, returns merged actions.
Actions tickRange(Harness& h, int64_t from, int64_t to, bool input) {
    Actions acc;
    for (int64_t f = from; f <= to; ++f) acc.merge(h.tick(baseSnap(f, input ? throttle() : Inputs{})));
    return acc;
}

} // namespace

// ---- Catalogue --------------------------------------------------------------------------------------------

TEST(ft_drill_ids_names_roundtrip) {
    for (int i = 0; i <= static_cast<int>(DrillType::Mix); ++i) {
        const auto t = static_cast<DrillType>(i);
        DrillType back = DrillType::RollIn;
        CHECK(parseDrillId(drillId(t), back));
        CHECK(back == t);
        CHECK(std::string(drillName(t)).size() > 3);
        CHECK(std::string(drillDescription(t)).size() > 20);
        CHECK(noEmDash(drillName(t)) && noEmDash(drillDescription(t)));
        CHECK(!drillInstruction(t, gamepadProfile(), KeyStyle::Auto).empty());
    }
    DrillType t = DrillType::Lob;
    CHECK(!parseDrillId("bogus", t));
    CHECK(t == DrillType::Lob);
}

TEST(ft_drill_instructions_use_player_buttons) {
    CHECK(contains(drillInstruction(DrillType::High, gamepadProfile(), KeyStyle::Auto), "Saut (A)"));
    CHECK(contains(drillInstruction(DrillType::RollIn, gamepadProfile(), KeyStyle::Auto), "Boost (B)"));
    PlayerProfile unknown;
    CHECK(contains(drillInstruction(DrillType::High, unknown, KeyStyle::PlayStation), "Saut"));
}

TEST(ft_drill_instructions_follow_button_style) {
    CHECK(contains(drillInstruction(DrillType::High, gamepadProfile(), KeyStyle::PlayStation), "Saut (Croix)"));
    CHECK(contains(drillInstruction(DrillType::RollIn, gamepadProfile(), KeyStyle::PlayStation), "Boost (Rond)"));
    CHECK(contains(drillInstruction(DrillType::RollIn, gamepadProfile(), KeyStyle::Xbox), "Boost (B)"));
    CHECK(!contains(drillInstruction(DrillType::High, gamepadProfile(), KeyStyle::PlayStation), "(A)"));
}

TEST(ft_drill_pose_is_flat_and_clamped) {
    CarState car = airCar(Vec3{3900.f, 5000.f, 600.f}, Vec3{}, Rot{8000, 16384, 3000});
    const DrillPose p = poseFromCar(car);
    CHECK_NEAR(p.pos.x, 3000.f, 1e-3f);
    CHECK_NEAR(p.pos.y, 4000.f, 1e-3f);
    CHECK_NEAR(p.pos.z, 17.01f, 1e-3f);
    CHECK_NEAR(p.fwd.z, 0.f, 1e-5f);
    CHECK_NEAR(length(p.fwd), 1.f, 1e-4f);
    CHECK_NEAR(dot(p.fwd, p.right), 0.f, 1e-5f);
    CHECK_NEAR(p.yawDeg, 90.f, 0.1f);
    const CarPlacement cp = carPlacementFor(p, true);
    CHECK(cp.boost.has_value() && *cp.boost == 100.f);
    CHECK(cp.vel == Vec3{});
    CHECK(cp.rot.pitch == 0 && cp.rot.roll == 0);
    CHECK(!carPlacementFor(p, false).boost.has_value());
}

TEST(ft_drill_clamp_ball_to_arena) {
    CHECK(insideArena(clampBallToArena(Vec3{5000.f, 6000.f, 3000.f})));
    CHECK(insideArena(clampBallToArena(Vec3{-3990.f, -4990.f, 0.f})));
    const Vec3 in{100.f, 200.f, 300.f};
    CHECK(clampBallToArena(in) == in);
}

TEST(ft_drill_spawns_are_deterministic) {
    for (int i = 0; i < kDrillKinds; ++i) {
        Rng a(42), b(42);
        const DrillSpawn sa = makeSpawn(static_cast<DrillType>(i), originPose(30.f), 0.5f, a);
        const DrillSpawn sb = makeSpawn(static_cast<DrillType>(i), originPose(30.f), 0.5f, b);
        CHECK(sa.ball.pos == sb.ball.pos);
        CHECK(sa.ball.vel == sb.ball.vel);
        CHECK(sa.type == static_cast<DrillType>(i));
    }
}

TEST(ft_drill_spawns_stay_inside_arena_everywhere) {
    Rng rng(7);
    for (int i = 0; i < kDrillKinds; ++i) {
        for (int k = 0; k < 120; ++k) {
            const Vec3 pos{rng.range(-4000.f, 4000.f), rng.range(-5000.f, 5000.f), kCarZ};
            const DrillPose pose = originPose(rng.range(-180.f, 180.f), pos);
            const DrillSpawn s = makeSpawn(static_cast<DrillType>(i), pose, rng.uniform(), rng);
            CHECK(insideArena(s.ball.pos));
            CHECK(finite(s.ball.vel));
            CHECK(length(s.ball.vel) < 2500.f);
            CHECK(s.ball.angVel == Vec3{});
        }
    }
}

TEST(ft_drill_roll_in_comes_towards_the_car) {
    Rng rng(3);
    for (int k = 0; k < 50; ++k) {
        const DrillSpawn s = makeSpawn(DrillType::RollIn, originPose(), rng.uniform(), rng);
        CHECK(s.ball.pos.x > 1400.f);
        CHECK_NEAR(s.ball.pos.z, kBallZ, 1e-3f);
        CHECK(s.ball.vel.x < 0.f);
        CHECK_NEAR(s.ball.vel.z, 0.f, 1e-4f);
        // Aimed at the car (within the miss tolerance): the lateral distance shrinks along the path.
        const float tHit = s.ball.pos.x / -s.ball.vel.x;
        CHECK(std::fabs(s.ball.pos.y + s.ball.vel.y * tHit) < 300.f);
    }
}

TEST(ft_drill_roll_away_goes_away_and_pass_crosses) {
    Rng rng(5);
    for (int k = 0; k < 50; ++k) {
        const DrillSpawn a = makeSpawn(DrillType::RollAway, originPose(), rng.uniform(), rng);
        CHECK(a.ball.pos.x > 300.f);
        CHECK(a.ball.vel.x > 0.f);
        const DrillSpawn p = makeSpawn(DrillType::Pass, originPose(), rng.uniform(), rng);
        CHECK(std::fabs(p.ball.pos.y) > 400.f);
        CHECK(p.ball.vel.y * p.ball.pos.y < 0.f);  // travels towards the car's path
        CHECK(p.ball.pos.x > -900.f);
    }
}

TEST(ft_drill_lob_lands_ahead_wall_and_high) {
    Rng rng(11);
    for (int k = 0; k < 50; ++k) {
        const float level = rng.uniform();
        const DrillSpawn l = makeSpawn(DrillType::Lob, originPose(), level, rng);
        CHECK(l.ball.vel.z > 300.f);
        // Ballistic landing point (no drag).
        const float g = 650.f, z0 = l.ball.pos.z - kBallZ, vz = l.ball.vel.z;
        const float t = (vz + std::sqrt(vz * vz + 2.f * g * z0)) / g;
        const float landX = l.ball.pos.x + l.ball.vel.x * t;
        CHECK(landX > 600.f && landX < 1600.f);

        const DrillSpawn w = makeSpawn(DrillType::Wall, originPose(0.f, Vec3{2000.f, 0.f, kCarZ}), level, rng);
        CHECK_NEAR(w.ball.pos.x, 3985.f, 1e-3f);
        CHECK(w.ball.vel.x < 0.f);
        CHECK(w.ball.pos.z >= 450.f);
        const DrillSpawn w2 = makeSpawn(DrillType::Wall, originPose(90.f, Vec3{-1500.f, 0.f, kCarZ}), level, rng);
        CHECK_NEAR(w2.ball.pos.x, -3985.f, 1e-3f);
        CHECK(w2.ball.vel.x > 0.f);
        CHECK(w2.ball.pos.y > 0.f);  // ahead of a car facing +y

        const DrillSpawn hb = makeSpawn(DrillType::High, originPose(), level, rng);
        CHECK(hb.ball.pos.z >= 350.f);
        CHECK(hb.ball.vel.z >= 450.f);
        CHECK(hb.ball.pos.x > 600.f);
    }
}

TEST(ft_drill_higher_level_is_harder) {
    auto meanSpeed = [](DrillType t, float level) {
        Rng rng(99);
        float sum = 0.f;
        for (int k = 0; k < 200; ++k) sum += length2D(makeSpawn(t, originPose(), level, rng).ball.vel);
        return sum / 200.f;
    };
    CHECK(meanSpeed(DrillType::RollIn, 1.f) > meanSpeed(DrillType::RollIn, 0.f) + 300.f);
    CHECK(meanSpeed(DrillType::RollAway, 1.f) > meanSpeed(DrillType::RollAway, 0.f) + 300.f);
    CHECK(meanSpeed(DrillType::Pass, 1.f) > meanSpeed(DrillType::Pass, 0.f) + 200.f);
    auto meanApex = [](DrillType t, float level) {
        Rng rng(5);
        float sum = 0.f;
        for (int k = 0; k < 200; ++k) {
            const DrillSpawn s = makeSpawn(t, originPose(), level, rng);
            sum += s.ball.pos.z + s.ball.vel.z * s.ball.vel.z / 1300.f;
        }
        return sum / 200.f;
    };
    CHECK(meanApex(DrillType::Lob, 1.f) > meanApex(DrillType::Lob, 0.f) + 200.f);
    CHECK(meanApex(DrillType::High, 1.f) > meanApex(DrillType::High, 0.f) + 200.f);
}

TEST(ft_drill_mix_never_repeats) {
    Rng rng(1);
    DrillType prev = DrillType::RollIn;
    int seen[kDrillKinds] = {};
    for (int k = 0; k < 300; ++k) {
        const DrillType t = pickMixType(rng, prev);
        CHECK(t != prev);
        CHECK(static_cast<int>(t) >= 0 && static_cast<int>(t) < kDrillKinds);
        ++seen[static_cast<int>(t)];
        prev = t;
    }
    for (int i = 0; i < kDrillKinds; ++i) CHECK(seen[i] > 10);
}

// ---- State machine ----------------------------------------------------------------------------------------

TEST(ft_drill_refused_without_activity) {
    Harness h;
    h.host.allow = false;
    h.tick(baseSnap(100));
    const Actions a = h.command("drill", {"roll_in"});
    CHECK(h.host.requests == 1);
    CHECK(hasToast(a, ToastKind::Warning));
    CHECK(!h.mod.drillStatus().active);
    const Actions t = tickRange(h, 101, 140, true);
    CHECK(!t.setBall && !t.setCar);
}

TEST(ft_drill_refused_in_workshop_or_without_world_control) {
    Harness h;
    h.game.mode = GameMode::Workshop;
    h.tick(baseSnap(100));
    CHECK(hasToast(h.command("drill", {"pass"}), ToastKind::Warning, "freeplay"));
    CHECK(h.host.requests == 0);
    h.game.mode = GameMode::Freeplay;
    h.game.worldControlAllowed = false;
    CHECK(hasToast(h.command("drill", {"pass"}), ToastKind::Warning));
    CHECK(h.host.requests == 0);
    CHECK(!h.mod.drillStatus().active);
    CHECK(!tickRange(h, 101, 120, true).setBall);
}

TEST(ft_drill_unknown_type_and_disabled_module) {
    Harness h;
    h.tick(baseSnap(100));
    CHECK(hasToast(h.command("drill", {"bogus"}), ToastKind::Warning, "inconnu"));
    CHECK(h.host.requests == 0);
    h.set("rlt_ft_enabled", "0");
    CHECK(hasToast(h.command("drill", {"pass"}), ToastKind::Warning));
    CHECK(h.host.requests == 0);
}

TEST(ft_drill_places_ball_after_grant_timeout_lowers_level_then_next_rep) {
    Harness h;
    const Actions place = startDrill(h, "roll_in");
    CHECK(h.host.ownsActivity(&h.mod));
    CHECK(contains(h.host.lastName, "Roulante vers toi"));
    CHECK(place.setBall.has_value());
    CHECK(place.setCar.has_value());
    CHECK(place.setBall->pos.x > 1400.f);      // ahead of the car (pose at origin, facing +x)
    CHECK(place.setCar->vel == Vec3{});
    CHECK(place.setCar->boost.has_value());
    auto st = h.mod.drillStatus();
    CHECK(st.active);
    CHECK(st.phase == FirstTouchModule::DrillPhase::WaitTouch);
    CHECK_NEAR(st.timeLeft, 6.f, 0.02f);
    CHECK_NEAR(h.mod.drillLevel(DrillType::RollIn).level, 0.2f, 1e-5f);

    // No touch for 6 s while driving: failed rep, level down.
    const Actions wait = tickRange(h, 102, 101 + 724, true);
    CHECK(!wait.setBall);
    st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK(contains(st.lastOutcome, "Temps"));
    CHECK(!st.lastSuccess);
    CHECK(st.reps == 1 && st.successes == 0);
    const auto lv = h.mod.drillLevel(DrillType::RollIn);
    CHECK_NEAR(lv.level, 0.2f - 0.04f * 0.75f / 0.25f, 1e-4f);
    CHECK(lv.attempts == 1);
    CHECK(lv.sessionReps == 1);

    // After the pause the next ball comes automatically.
    const Actions next = tickRange(h, 826, 826 + 186, true);
    CHECK(next.setBall.has_value());
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch);
}

TEST(ft_drill_idle_reps_are_ignored_then_stopped) {
    Harness h;
    startDrill(h, "pass");
    int64_t f = 102;
    for (int rep = 0; rep < 3; ++rep) {
        tickRange(h, f, f + 721 + 181, false);
        f += 721 + 182;
    }
    CHECK_NEAR(h.mod.drillLevel(DrillType::Pass).level, 0.2f, 1e-5f);
    CHECK(h.mod.drillLevel(DrillType::Pass).attempts == 0);
    CHECK(!h.mod.drillStatus().active);
    CHECK(h.host.releases == 1);
    // The drill is stopped (activity released), not paused: the message must say so (review).
    CHECK(hasToast(h.all, ToastKind::Info, "Exercice arrêté : aucune action détectée. Relance-le quand tu veux."));
    CHECK(!hasToast(h.all, ToastKind::Info, "pause"));
    // Nothing resumes on its own afterwards.
    const Actions later = tickRange(h, f, f + 400, true);
    CHECK(!later.setBall.has_value());
    CHECK(!h.mod.drillStatus().active);
}

TEST(ft_drill_good_touch_raises_level_and_reports_result) {
    Harness h;
    startDrill(h, "roll_in");
    const Actions run = runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 130), 125);
    CHECK(run.results.size() == 1);
    if (!run.results.empty()) {
        const AttemptResult& r = run.results.front();
        CHECK(r.success);
        bool hasLevel = false;
        for (const auto& m : r.metrics)
            if (m.key == "drill_level") hasLevel = std::fabs(m.value - 20.f) < 1e-3f;
        CHECK(hasLevel);
    }
    const auto st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK(st.lastSuccess);
    CHECK(contains(st.lastOutcome, "Réussi"));
    CHECK(st.reps == 1 && st.successes == 1);
    CHECK_NEAR(h.mod.drillLevel(DrillType::RollIn).level, 0.24f, 1e-4f);
    CHECK_NEAR(h.mod.drillLevel(DrillType::RollIn).best, 0.24f, 1e-4f);
    CHECK(h.mod.lastTouch().inDrill);
    CHECK(run.requestSave);

    // Pause then automatic next rep.
    Snapshot s = h.latest;
    bool placed = false;
    for (int i = 0; i < 200 && !placed; ++i) {
        s = advance(s);
        placed = h.tick(s).setBall.has_value();
    }
    CHECK(placed);
}

TEST(ft_drill_bad_touch_fails_rep) {
    Harness h;
    startDrill(h, "roll_away");
    const Actions run = runScenario(h, groundPickup(1700.f, 800.f, 1500.f, 2200.f, 130), 125);
    CHECK(run.results.size() == 1);
    const auto st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK(!st.lastSuccess);
    CHECK(contains(st.lastOutcome, "Raté"));
    CHECK_NEAR(h.mod.drillLevel(DrillType::RollAway).level, 0.08f, 1e-4f);
}

TEST(ft_drill_touch_before_placement_is_ignored) {
    Harness h;
    h.set("rlt_ft_drill_seed", "1234");
    h.tick(baseSnap(98));
    h.tick(baseSnap(99));
    h.tick(baseSnap(100));
    h.command("drill", {"roll_in"});
    h.tick(baseSnap(101));  // placement
    // Touch whose pre state (frame 100) predates the placement, resolved at frame 102.
    Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1000.f, 100);
    TouchEvent t = buildTouch(sc);
    t.post1.frame = 101;
    t.post.frame = 102;
    h.tickWithTouch(t);
    CHECK(!h.mod.analysisPending());
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch);
}

TEST(ft_drill_stop_command_releases_activity) {
    Harness h;
    startDrill(h, "lob");
    const Actions a = h.command("stop");
    CHECK(h.host.releases == 1);
    CHECK(!h.host.ownsActivity(&h.mod));
    CHECK(!h.mod.drillStatus().active);
    CHECK(hasToast(a, ToastKind::Info, "arrêté"));
    CHECK(!tickRange(h, 102, 400, true).setBall);
    // Stop when nothing runs: no second release.
    h.command("stop");
    CHECK(h.host.releases == 1);
}

TEST(ft_drill_activity_taken_by_another_module) {
    Harness h;
    startDrill(h, "wall");
    h.host.owner = nullptr;          // the engine gave the activity to someone else...
    h.mod.onActivityStopped();       // ...and notified us
    CHECK(!h.mod.drillStatus().active);
    const Actions a = tickRange(h, 102, 400, true);
    CHECK(!a.setBall && !a.setCar && !a.setBoost);
    CHECK(h.host.releases == 0);

    // Lost the activity without notification: the drill stops at the next tick, no placement.
    Harness h2;
    startDrill(h2, "high");
    h2.host.owner = nullptr;
    CHECK(!h2.tick(baseSnap(102)).setBall);
    CHECK(!h2.mod.drillStatus().active);
}

TEST(ft_drill_world_control_lost_stops_and_releases) {
    Harness h;
    startDrill(h, "pass");
    h.game.worldControlAllowed = false;
    const Actions a = h.tick(baseSnap(102));
    CHECK(!a.setBall);
    CHECK(!h.mod.drillStatus().active);
    CHECK(h.host.releases == 1);
}

TEST(ft_drill_reset_shot_replays_rep_without_counting) {
    Harness h;
    startDrill(h, "roll_in");
    tickRange(h, 102, 200, true);
    h.event(GameEventKind::ResetShot);
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Place);
    CHECK(h.tick(baseSnap(201)).setBall.has_value());
    CHECK(h.mod.drillLevel(DrillType::RollIn).attempts == 0);
    // A goal during the rep: pause, not counted.
    h.event(GameEventKind::Goal);
    const auto st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK(contains(st.lastOutcome, "But"));
    CHECK(h.mod.drillLevel(DrillType::RollIn).attempts == 0);
    // Skip command: next ball at once.
    h.command("skip");
    CHECK(h.tick(baseSnap(202)).setBall.has_value());
}

TEST(ft_drill_mix_records_into_concrete_drills) {
    Harness h;
    startDrill(h, "mix");
    CHECK(contains(h.host.lastName, "Mix"));
    int64_t f = 102;
    DrillType prev = h.mod.drillStatus().current;
    int changes = 0;
    for (int rep = 0; rep < 6; ++rep) {
        tickRange(h, f, f + 721 + 181, true);
        f += 721 + 182;
        const DrillType cur = h.mod.drillStatus().current;
        if (cur != prev) ++changes;
        prev = cur;
    }
    CHECK(changes >= 5);
    int total = 0;
    for (int i = 0; i < kDrillKinds; ++i) total += h.mod.drillLevel(static_cast<DrillType>(i)).attempts;
    CHECK(total == 6);
    CHECK(h.mod.drillStatus().reps == 6);
}

TEST(ft_drill_map_change_stops_drill) {
    Harness h;
    startDrill(h, "high");
    h.event(GameEventKind::ContextChanged);  // same map: periodic refresh, nothing happens
    CHECK(h.mod.drillStatus().active);
    h.game.mapName = "Stadium_P";
    h.event(GameEventKind::ContextChanged);
    CHECK(!h.mod.drillStatus().active);
    CHECK(h.host.releases == 1);
}

TEST(ft_drill_without_car_reset_follows_current_pose) {
    Harness h;
    h.set("rlt_ft_drill_reset_car", "0");
    const Actions place = startDrill(h, "roll_away");
    CHECK(place.setBall.has_value());
    CHECK(!place.setCar.has_value());
    CHECK(place.setBoost.has_value());
    h.set("rlt_ft_drill_boost", "0");
    h.command("skip");
    // Car now at another place facing +y: the ball spawns in front of it.
    Snapshot s = makeSnap(150, groundCar(Vec3{-1000.f, 500.f, kCarZ}, Vec3{}, 90.f), makeBall(Vec3{}, Vec3{}));
    const Actions next = h.tick(s);
    CHECK(next.setBall.has_value());
    CHECK(!next.setBoost.has_value());
    if (next.setBall) {
        CHECK(next.setBall->pos.y > 700.f);
        CHECK(next.setBall->vel.y > 0.f);
    }
}

// Regression (review): a Lob rep at low level sends the ball back slowly towards the car. Charging into it was
// reported "[MAUVAIS] Écart +21 km/h" (car faster) in the chat while the tip, the HUD and the drill outcome said
// "Accélère : la balle va plus vite que toi".
TEST(ft_drill_slow_returning_ball_is_told_to_cushion_not_accelerate) {
    Harness h;
    h.set("rlt_ft_chat", "1");
    const Actions place = startDrill(h, "lob");
    CHECK(place.setBall.has_value());
    // Car drives at 700 into a ball that comes back slowly (120 uu/s) towards it after the bounce.
    const Actions run = runScenario(h, groundPickup(700.f, -120.f, 650.f, 300.f, 300), 125);
    CHECK(run.results.size() == 1);
    if (!run.results.empty()) {
        const AttemptResult& r = run.results.front();
        CHECK(!hasTip(r.tips, "ft.too_slow"));
        CHECK(hasTip(r.tips, "ft.head_on"));
        for (const auto& t : r.tips) CHECK(!contains(t.text, "Accélère"));
    }
    CHECK(run.chat.size() == 1);
    if (!run.chat.empty()) CHECK(contains(run.chat.front().text, "Écart +"));
    const auto& last = h.mod.lastTouch();
    CHECK(last.valid && last.inDrill);
    CHECK(last.speedDiff > 0.f);
    for (const auto& t : last.tips) CHECK(t.code != "ft.too_slow");
    CHECK(!contains(h.mod.drillStatus().lastOutcome, "Accélère"));
}

// The button style chosen in "Ma config" reaches the drill status, the drill panel and the graded tips.
TEST(ft_button_style_reaches_drill_status_panel_and_tips) {
    Harness h;
    h.settings.profile.buttonStyle = 2;  // PlayStation
    h.mod.onSettingsChanged(h.settings);
    CHECK(h.mod.keyStyle() == KeyStyle::PlayStation);
    startDrill(h, "high");
    const auto st = h.mod.drillStatus();
    CHECK(st.active && st.phase == FirstTouchModule::DrillPhase::WaitTouch);
    CHECK(contains(st.instruction, "Saut (Croix)"));
    CHECK(!contains(st.instruction, "(A)"));
    const DrawList dl = h.draw();
    bool croix = false, xbox = false;
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines) {
            if (contains(l.text, "Croix")) croix = true;
            if (contains(l.text, "(A)")) xbox = true;
        }
    CHECK(croix);
    CHECK(!xbox);
    h.command("stop");

    // Rammed ground touch: the "too fast" tip names the PlayStation boost button.
    const Actions run = runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 2000), 130);
    CHECK(run.results.size() == 1);
    if (!run.results.empty()) {
        bool rond = false;
        for (const auto& t : run.results.front().tips) {
            if (contains(t.text, "Boost (Rond)")) rond = true;
            CHECK(!contains(t.text, "Boost (B)"));
        }
        CHECK(rond);
    }
    // Back to Auto: Xbox names.
    h.settings.profile.buttonStyle = 0;
    h.mod.onSettingsChanged(h.settings);
    CHECK(h.mod.keyStyle() == KeyStyle::Auto);
}
