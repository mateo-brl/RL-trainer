// RunTracker: Kismet-driven runs (Leth rings, ring counter, map timer) and own timing fallback.
#include <cstdio>

#include "tests/TestFramework.h"

#include "core/modules/maps/MapRun.h"
#include "tests/maps/MapsTestUtil.h"

using namespace rlt;
using namespace maps_test;

namespace {

int count(const RunTracker& r, RunEvent::Kind k) {
    int n = 0;
    for (const auto& e : r.events())
        if (e.kind == k) ++n;
    return n;
}

// Leth pattern on a bare tracker: Timing false -> true at t0 + 0.1, Level 1..20 (2 s each), then 20 -> 1.
float lethTrackerRun(RunTracker& r, float t0, float levelTime = 2.f) {
    float t = t0;
    r.onKismet(lethVars(false, 1), t);
    t += 0.1f;
    r.onKismet(lethVars(true, 1), t);
    for (int lvl = 2; lvl <= 20; ++lvl) {
        t += levelTime;
        r.onKismet(lethVars(true, lvl), t);
    }
    t += levelTime;
    r.onKismet(lethVars(true, 1), t);
    return t;
}

} // namespace

TEST(maps_run_leth_giant_rings_full_course) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    CHECK(r.hasProfile() && !r.ownStartActive());
    r.arm(0.f);
    CHECK(r.phase() == RunPhase::Idle);  // the map gives the start: no own arming
    lethTrackerRun(r, 10.f, 2.f);
    CHECK(count(r, RunEvent::Kind::Started) == 1);
    CHECK(count(r, RunEvent::Kind::Split) == 19);
    CHECK(count(r, RunEvent::Kind::Finished) == 1);
    CHECK(count(r, RunEvent::Kind::Aborted) == 0);
    CHECK(!r.running());
    const RunSummary& s = r.last();
    CHECK(s.valid && s.finished && s.recordEligible);
    CHECK(s.source == RunSource::Kismet);
    CHECK_NEAR(s.time, 40.f, 1e-3f);
    CHECK(s.splits.size() == 19);
    CHECK_NEAR(s.splits.front().time, 2.f, 1e-3f);
    CHECK(s.splits.front().level == 2);
    CHECK(s.splits.back().level == 20);
    CHECK_NEAR(s.splits.back().time, 38.f, 1e-3f);
    CHECK(!s.splits.front().hasDelta);  // no personal best yet
}

TEST(maps_run_first_sample_never_starts_a_run) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    r.onKismet(lethVars(true, 5), 1.f);  // plugin attached mid-run: Timing already true
    CHECK(!r.running());
    r.onKismet(lethVars(true, 6), 2.f);  // level up without a run: ignored
    CHECK(!r.running());
    CHECK(r.events().empty());
    CHECK(r.hasLevel() && r.level() == 6);
}

TEST(maps_run_split_deltas_against_personal_best) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    std::vector<float> pb;
    for (int i = 1; i <= 19; ++i) pb.push_back(2.5f * static_cast<float>(i));
    r.setPersonalBest(pb, 50.f, true);
    r.onKismet(lethVars(false, 1), 0.f);
    r.onKismet(lethVars(true, 1), 1.f);
    r.onKismet(lethVars(true, 2), 3.f);  // 2.0 s vs PB 2.5 s
    CHECK(r.splits().size() == 1);
    CHECK(r.splits()[0].hasDelta);
    CHECK_NEAR(r.splits()[0].delta, -0.5f, 1e-4f);
    float live = 0.f;
    CHECK(!r.liveDelta(4.f, live));     // 3.0 s elapsed, next PB split at 5.0 s
    CHECK(r.liveDelta(7.f, live));      // 6.0 s elapsed: 1.0 s behind
    CHECK_NEAR(live, 1.f, 1e-4f);
    r.onKismet(lethVars(true, 3), 8.f);  // 7.0 s vs 5.0 s
    CHECK_NEAR(r.splits()[1].delta, 2.f, 1e-4f);
    CHECK(r.lastSplitAt() == 8.f);
}

TEST(maps_run_course_restart_aborts) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    r.onKismet(lethVars(false, 1), 0.f);
    r.onKismet(lethVars(true, 1), 1.f);
    r.onKismet(lethVars(true, 2), 3.f);
    r.onKismet(lethVars(true, 3), 5.f);
    r.onKismet(lethVars(true, 1), 6.f);  // back to 1 before the final level: restart
    CHECK(!r.running());
    CHECK(count(r, RunEvent::Kind::Aborted) == 1);
    CHECK(count(r, RunEvent::Kind::Finished) == 0);
    CHECK(r.last().valid && !r.last().finished);
    CHECK_NEAR(r.last().time, 5.f, 1e-4f);
}

TEST(maps_run_timing_drop_mid_run_aborts_and_at_final_finishes) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    r.onKismet(lethVars(false, 1), 0.f);
    r.onKismet(lethVars(true, 1), 1.f);
    r.onKismet(lethVars(true, 4), 2.f);  // jumped levels: run continues but is not record-eligible
    CHECK(r.running() && !r.recordEligible());
    r.onKismet(lethVars(false, 4), 3.f);
    CHECK(!r.running() && !r.last().finished);
    r.clearEvents();

    // Falling edge exactly at the final level counts as a finish.
    RunTracker f;
    f.setProfile(findKismetProfile("leth_rings"));
    f.onKismet(lethVars(false, 1), 0.f);
    f.onKismet(lethVars(true, 1), 1.f);
    for (int lvl = 2; lvl <= 20; ++lvl) f.onKismet(lethVars(true, lvl), 1.f + static_cast<float>(lvl));
    f.onKismet(lethVars(false, 20), 30.f);
    CHECK(f.last().finished && f.last().recordEligible);
    CHECK_NEAR(f.last().time, 29.f, 1e-4f);
}

TEST(maps_run_resets_and_deaths_during_kismet_run) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    r.onKismet(lethVars(false, 1, 2), 0.f);
    r.onKismet(lethVars(true, 1, 2), 1.f);
    r.onReset(2.f);  // checkpoint respawn: the run goes on
    r.onReset(3.f);
    CHECK(r.running());
    CHECK(r.resetsInRun() == 2);
    r.onKismet(lethVars(true, 2, 5), 4.f);
    CHECK(r.hasDeaths() && r.deathsInRun() == 3);
    r.onGoal(5.f);  // a goal does not end a rings run
    CHECK(r.running());
    r.onUnload(6.f);
    CHECK(!r.running() && r.last().deaths == 3 && r.last().resets == 2 && !r.last().finished);
    CHECK(!r.hasLevel());
}

TEST(maps_run_ring_counter_profile) {
    RunTracker r;
    r.setProfile(findKismetProfile("sjr1"));
    r.onKismet({kInt("Rings", 0)}, 0.f);
    r.onKismet({kInt("Rings", 1)}, 1.f);  // first ring: start
    CHECK(r.running() && r.source() == RunSource::Kismet);
    for (int i = 2; i <= 161; ++i) r.onKismet({kInt("Rings", i)}, 1.f + 0.5f * static_cast<float>(i - 1));
    CHECK(r.running());
    CHECK(r.splits().size() == 160);
    r.onKismet({kInt("Rings", 162)}, 100.f);
    CHECK(!r.running() && r.last().finished);
    CHECK_NEAR(r.last().time, 99.f, 1e-3f);
    CHECK(r.last().splits.size() == 161);
    // Counter reset mid-run aborts, then 0 -> 1 restarts.
    r.clearEvents();
    r.onKismet({kInt("Rings", 0)}, 101.f);
    r.onKismet({kInt("Rings", 1)}, 102.f);
    r.onKismet({kInt("Rings", 2)}, 103.f);
    r.onKismet({kInt("Rings", 0)}, 104.f);
    CHECK(count(r, RunEvent::Kind::Started) == 1 && count(r, RunEvent::Kind::Aborted) == 1);
    r.onKismet({kInt("Rings", 1)}, 105.f);
    CHECK(r.running());
}

TEST(maps_run_map_timer_string_profile) {
    RunTracker r;
    r.setProfile(findKismetProfile("sjr2"));
    r.onKismet({kStr("displaytimer", "03:12")}, 0.f);
    r.onKismet({kStr("displaytimer", "00:00")}, 1.f);  // map reset its timer: start
    CHECK(r.running());
    r.onKismet({kStr("displaytimer", "00:01")}, 2.f);
    r.onKismet({kStr("displaytimer", "00:02")}, 3.f);
    CHECK(r.hasMapTimer() && r.mapTimer() == "00:02");
    r.onKismet({kStr("displaytimer", "00:00")}, 4.f);  // restart: previous run aborted, new one started
    CHECK(count(r, RunEvent::Kind::Aborted) == 1 && count(r, RunEvent::Kind::Started) == 2);
    r.onKismet({kStr("displaytimer", "00:05")}, 9.f);
    r.onKismet({kStr("displaytimer", "00:02")}, 10.f);  // went backwards without zero: reset
    CHECK(!r.running());
}

TEST(maps_run_sjr3_reaching_final_level_finishes) {
    RunTracker r;
    r.setProfile(findKismetProfile("sjr3"));
    r.onKismet({kStr("displaytimer", "01:00"), kInt("Level", 1)}, 0.f);
    r.onKismet({kStr("displaytimer", "00:00"), kInt("Level", 1)}, 1.f);
    for (int lvl = 2; lvl <= 15; ++lvl) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "00:%02d", lvl);
        r.onKismet({kStr("displaytimer", buf), kInt("Level", lvl)}, 1.f + static_cast<float>(lvl));
    }
    CHECK(!r.running() && r.last().finished);
    CHECK(r.last().splits.size() == 14);
}

TEST(maps_run_generic_profile_without_final_never_fakes_a_finish) {
    RunTracker r;
    r.setProfile(findKismetProfile("timing_level"));
    r.onKismet({kBool("Timing", false), kInt("Level", 1)}, 0.f);
    r.onKismet({kBool("Timing", true), kInt("Level", 1)}, 1.f);
    r.onKismet({kBool("Timing", true), kInt("Level", 2)}, 2.f);
    r.onGoal(3.f);
    CHECK(r.running());  // a goal on an unknown level map is just a level transition
    r.onKismet({kBool("Timing", false), kInt("Level", 2)}, 4.f);
    CHECK(!r.running() && !r.last().finished);
}

TEST(maps_run_dribble_overhaul_goal_at_final_level) {
    RunTracker r;
    r.setProfile(findKismetProfile("dribble2_overhaul"));
    r.onKismet({kBool("Timing", false), kInt("Level", 1)}, 0.f);
    r.onKismet({kBool("Timing", true), kInt("Level", 1)}, 1.f);
    for (int lvl = 2; lvl <= 29; ++lvl)
        r.onKismet({kBool("Timing", true), kInt("Level", lvl)}, static_cast<float>(lvl));
    r.onGoal(29.5f);  // goal before the last level: just a level transition
    CHECK(r.running());
    r.onKismet({kBool("Timing", true), kInt("Level", 30)}, 30.f);
    r.onGoal(30.5f);  // goal on level 30: course finished
    CHECK(!r.running());
    CHECK(r.last().finished);
    CHECK_NEAR(r.last().time, 29.5f, 1e-4f);
}

TEST(maps_run_own_timing_start_on_move_after_reset) {
    RunTracker r;
    r.arm(0.f);
    CHECK(r.phase() == RunPhase::Armed);
    r.onTick(carSnap(0.1f, {}, {0.f, 0.f, 0.f}));   // at rest
    r.onTick(carSnap(0.5f, {}, {50.f, 0.f, 0.f}));  // creeping below the threshold
    CHECK(!r.running());
    r.onTick(carSnap(1.0f, {1.f, 0.f, 0.f}, {500.f, 0.f, 0.f}));
    CHECK(r.running() && r.source() == RunSource::Own && r.recordEligible());
    CHECK_NEAR(r.startTime(), 1.f, 1e-5f);
    CHECK_NEAR(r.elapsed(4.f), 3.f, 1e-5f);
    r.onGoal(6.f);
    CHECK(!r.running() && r.last().finished);
    CHECK_NEAR(r.last().time, 5.f, 1e-5f);
    // After a finish, nothing starts until a reset.
    r.onTick(carSnap(6.1f, {2.f, 0.f, 0.f}, {0.f, 0.f, 0.f}));
    r.onTick(carSnap(6.2f, {3.f, 0.f, 0.f}, {900.f, 0.f, 0.f}));
    CHECK(!r.running());
    r.onReset(7.f);
    CHECK(r.phase() == RunPhase::Armed);
}

TEST(maps_run_own_timing_reset_aborts_or_finishes) {
    RunTracker r;
    r.arm(0.f);
    r.onTick(carSnap(0.1f, {}, {}));
    r.onTick(carSnap(0.2f, {}, {300.f, 0.f, 0.f}));
    r.onReset(3.f);
    CHECK(!r.last().finished && r.phase() == RunPhase::Armed);
    CHECK_NEAR(r.last().time, 2.8f, 1e-4f);

    RunOptions o;
    o.resetFinishes = true;
    RunTracker f;
    f.setOptions(o);
    f.arm(0.f);
    f.onTick(carSnap(0.1f, {}, {}));
    f.onTick(carSnap(0.2f, {}, {300.f, 0.f, 0.f}));
    f.onReset(3.f);
    CHECK(f.last().finished);
}

TEST(maps_run_own_timing_never_at_rest_starts_at_arm_time) {
    RunTracker r;
    r.arm(10.f);
    r.onTick(carSnap(10.1f, {}, {800.f, 0.f, 0.f}));
    CHECK(!r.running());  // grace period: maybe the reset velocity is not cleared yet
    r.onTick(carSnap(10.6f, {}, {800.f, 0.f, 0.f}));
    CHECK(r.running());
    CHECK_NEAR(r.startTime(), 10.f, 1e-5f);
    // Armed "unclean" (mid-map profile change): such a run is never a record.
    RunTracker u;
    u.arm(0.f, false);
    u.onTick(carSnap(0.6f, {}, {800.f, 0.f, 0.f}));
    CHECK(u.running() && !u.recordEligible());
}

TEST(maps_run_own_timing_disabled) {
    RunOptions o;
    o.ownTiming = false;
    RunTracker r;
    r.setOptions(o);
    r.arm(0.f);
    CHECK(r.phase() == RunPhase::Idle);
    r.onTick(carSnap(0.1f, {}, {}));
    r.onTick(carSnap(0.2f, {}, {900.f, 0.f, 0.f}));
    CHECK(!r.running());
}

TEST(maps_run_invalid_car_paused_and_teleports) {
    RunTracker r;
    r.arm(0.f);
    r.onTick(carSnap(0.1f, {}, {}, false));
    r.onTick(carSnap(0.2f, {}, {900.f, 0.f, 0.f}, false));  // invalid car: ignored
    CHECK(!r.running());
    Snapshot paused = carSnap(0.3f, {}, {900.f, 0.f, 0.f});
    paused.paused = true;
    r.onTick(paused);
    CHECK(!r.running());
    r.onTick(carSnap(0.31f, {}, {}));
    r.onTick(carSnap(0.32f, {10.f, 0.f, 0.f}, {900.f, 0.f, 0.f}));
    CHECK(r.running());
    r.onTick(carSnap(0.33f, {3000.f, 0.f, 0.f}, {900.f, 0.f, 0.f}));  // teleport (map checkpoint)
    CHECK(r.running());
    r.onTick(carSnap(0.34f, {3010.f, 0.f, 0.f}, {900.f, 0.f, 0.f}));
    r.onTick(carSnap(2.0f, {9000.f, 0.f, 0.f}, {900.f, 0.f, 0.f}));  // big gap between ticks: not a teleport
    r.abort(3.f);
    CHECK(r.last().respawns == 1);
    CHECK(r.phase() == RunPhase::Armed);
}

TEST(maps_run_kismet_profile_cancels_own_run) {
    RunTracker r;
    r.arm(0.f);
    r.onTick(carSnap(0.1f, {}, {}));
    r.onTick(carSnap(0.2f, {}, {500.f, 0.f, 0.f}));
    CHECK(r.running());
    r.setProfile(findKismetProfile("leth_rings"));  // the map's own start signal is now known
    CHECK(!r.running());
    CHECK(count(r, RunEvent::Kind::Cancelled) == 1);
    CHECK(!r.last().valid);  // a cancelled run is not a run
    // A level-only profile keeps own timing and adds splits.
    RunTracker l;
    l.setProfile(findKismetProfile("egyptian_tomb"));
    CHECK(l.ownStartActive());
    l.arm(0.f);
    l.onTick(carSnap(0.1f, {}, {}));
    l.onKismet({kInt("ankh", 0)}, 0.1f);
    l.onTick(carSnap(0.2f, {}, {500.f, 0.f, 0.f}));
    l.onKismet({kInt("ankh", 1)}, 2.2f);
    CHECK(l.running() && l.splits().size() == 1);
    CHECK_NEAR(l.splits()[0].time, 2.f, 1e-4f);
    l.onKismet({kInt("ankh", 0)}, 3.f);  // level back to 0: course restarted
    CHECK(!l.running() && l.phase() == RunPhase::Armed);
}

TEST(maps_run_clear_keeps_options) {
    RunOptions o;
    o.startSpeed = 333.f;
    RunTracker r;
    r.setOptions(o);
    r.setProfile(findKismetProfile("leth_rings"));
    r.clear();
    CHECK(!r.hasProfile());
    CHECK(r.options().startSpeed == 333.f);
    CHECK(std::string(runSourceLabel(RunSource::Own)).size() > 3);
}

TEST(maps_run_time_base_restart_reanchors_arming) {
    RunTracker r;
    r.arm(500.f);  // armed with the previous map's time
    r.onTick(carSnap(0.1f, {}, {800.f, 0.f, 0.f}));  // new level: physics time restarted near zero
    CHECK(!r.running());
    r.onTick(carSnap(0.7f, {}, {800.f, 0.f, 0.f}));
    CHECK(r.running());
    CHECK_NEAR(r.startTime(), 0.1f, 1e-5f);
}
