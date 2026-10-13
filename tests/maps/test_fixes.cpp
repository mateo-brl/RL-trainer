// Regression tests for the review fixes: arm anchoring (map entry, re-enable), partial splits after a
// profile change, reset-ended runs, start-only pins, catalogue final level with signature profiles, deaths
// baseline, map timer anchoring.
#include <string>

#include "tests/TestFramework.h"

#include "core/modules/maps/MapRun.h"
#include "tests/maps/MapsTestUtil.h"

using namespace rlt;
using namespace maps_test;

namespace {

const Vec3 kMoving{900.f, 0.f, 0.f};

const Tip* findTip(const AttemptResult& r, const std::string& code) {
    for (const auto& t : r.tips)
        if (t.code == code) return &t;
    return nullptr;
}

std::vector<KismetVar> timingLevel(bool timing, int level) { return {kBool("Timing", timing), kInt("Level", level)}; }

} // namespace

// ---- Arm anchoring -------------------------------------------------------------------------------------
TEST(maps_fix_map_entry_never_starts_a_run_in_the_past) {
    // Fresh session: the engine has no snapshot at ContextChanged, so the module arms with its own time (0).
    // The player is already driving at the first tick (4000 s of physics time in this session).
    Harness h;
    h.setContext(GameMode::Workshop, true, "Unknown_Obstacle_Course_P");
    CHECK(!h.hasSnapshot);
    h.tick(4000.f, {}, kMoving);
    CHECK(h.mod.status().phase == RunPhase::Armed);  // grace period from the first tick, not from 0
    h.tick(4000.6f, {}, kMoving);
    MapsLiveStatus st = h.mod.status();
    CHECK(st.phase == RunPhase::Running);
    CHECK_NEAR(st.runTime, 0.6f, 2e-3f);
    h.tick(4010.f, {}, kMoving);
    Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1);
    if (!g.results.empty()) {
        const AttemptResult& r = g.results.front();
        CHECK_NEAR(r.valueForRecord, 10.f, 2e-3f);
        CHECK(!r.hasRecordValue);  // the start was not seen: never a record
        const Tip* t = findTip(r, "maps.not_eligible");
        CHECK(t != nullptr && t->text.find("départ") != std::string::npos);
    }
    CHECK(g.toasts.empty());  // no "Premier chrono"
    CHECK(!h.mod.status().hasPb);
    CHECK(!h.mod.status().last.cleanStart);
    // After a reset the next attempt is clean and becomes the first record.
    h.event(GameEventKind::ResetShot);
    h.tick(4010.1f, {}, {});
    h.tick(4010.2f, {}, kMoving);
    h.tick(4020.2f, {}, kMoving);
    g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1 && g.results.front().hasRecordValue);
    CHECK(!g.toasts.empty() && g.toasts.front().title == "Premier chrono");
    CHECK(h.mod.status().hasPb);
}

TEST(maps_fix_map_entry_with_the_car_at_rest_is_a_clean_start) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Unknown_Obstacle_Course_P");
    h.tick(4000.f, {}, {});
    h.tick(4000.5f, {}, kMoving);
    CHECK(h.mod.status().phase == RunPhase::Running);
    h.tick(4010.5f, {}, kMoving);
    const Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1);
    if (!g.results.empty()) {
        CHECK_NEAR(g.results.front().valueForRecord, 10.f, 2e-3f);
        CHECK(g.results.front().hasRecordValue);
    }
}

TEST(maps_fix_arm_from_the_previous_map_is_reanchored) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Map_A");
    for (int i = 1; i <= 50; ++i) h.tick(10.f * static_cast<float>(i), {}, {});  // up to 500 s
    // Next map: same time base, the module arms with 500 s, first tick at 530 s while driving.
    h.setContext(GameMode::Workshop, true, "Map_B");
    CHECK(h.mod.status().key == "map_b");
    h.tick(530.f, {}, kMoving);
    h.tick(530.6f, {}, kMoving);
    const MapsLiveStatus st = h.mod.status();
    CHECK(st.phase == RunPhase::Running);
    CHECK_NEAR(st.runTime, 0.6f, 2e-3f);
}

TEST(maps_fix_reenable_while_driving_never_counts_the_disabled_time) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Some_Map");
    for (int i = 0; i <= 20; ++i) h.tick(static_cast<float>(i), {}, {});
    h.settings.maps.enabled = false;
    h.applySettings();
    for (int i = 21; i < 320; i += 10) h.tick(static_cast<float>(i), {}, kMoving);  // ignored while disabled
    h.settings.maps.enabled = true;
    h.applySettings();  // arms with the module's last time (20 s)
    CHECK(h.mod.status().phase == RunPhase::Armed);
    h.tick(320.f, {}, kMoving);
    h.tick(320.6f, {}, kMoving);
    MapsLiveStatus st = h.mod.status();
    CHECK(st.phase == RunPhase::Running);
    CHECK_NEAR(st.runTime, 0.6f, 2e-3f);
    h.tick(330.f, {}, kMoving);
    const Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1);
    if (!g.results.empty()) {
        CHECK_NEAR(g.results.front().valueForRecord, 10.f, 2e-3f);
        CHECK(!g.results.front().hasRecordValue);  // the player was mid-course when the module came back
    }
    CHECK(!h.mod.status().hasPb);
}

TEST(maps_fix_tracker_anchor_keeps_a_fresh_arm) {
    // An arm followed by ticks at 120 Hz keeps its time: a car never at rest starts at the arm time.
    RunTracker r;
    r.arm(10.f);
    r.onTick(carSnap(10.008f, {}, kMoving));
    r.onTick(carSnap(10.6f, {}, kMoving));
    CHECK(r.running() && r.recordEligible());
    CHECK_NEAR(r.startTime(), 10.f, 1e-5f);
    // A paused or invalid first tick does not anchor yet.
    RunTracker p;
    p.arm(0.f);
    Snapshot paused = carSnap(5.f, {}, kMoving);
    paused.paused = true;
    p.onTick(paused);
    p.onTick(carSnap(5.f, {}, kMoving, false));
    p.onTick(carSnap(5.01f, {}, {}));  // first real tick: anchored here, at rest
    p.onTick(carSnap(5.2f, {}, kMoving));
    CHECK(p.running() && p.recordEligible());
    CHECK_NEAR(p.startTime(), 5.2f, 1e-5f);
}

// ---- Partial splits ------------------------------------------------------------------------------------
TEST(maps_fix_profile_found_mid_run_does_not_store_shifted_pb_splits) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "MysteryCourse");
    h.step(0.f, {kInt("CheckpointIndex", 0)});
    h.tick(0.5f, {}, {});
    h.tick(1.f, {}, kMoving);  // own-timed run starts at 1 s
    CHECK(h.mod.status().phase == RunPhase::Running);
    h.step(3.f, {kInt("CheckpointIndex", 1)}, {}, kMoving);  // the counter qualifies now: profile mid-run
    CHECK(h.mod.status().profileSource == ProfileSource::Auto);
    h.step(5.f, {kInt("CheckpointIndex", 2)}, {}, kMoving);
    h.step(7.f, {kInt("CheckpointIndex", 3)}, {}, kMoving);
    CHECK(h.mod.status().splits.size() == 2);
    h.tick(9.f, {}, kMoving);
    Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1 && g.results.front().hasRecordValue);
    MapsLiveStatus st = h.mod.status();
    CHECK(st.last.partialSplits);
    CHECK(st.hasPb && std::fabs(st.pb - 8.f) < 1e-3f);
    CHECK(st.pbSplits.empty());  // shifted splits are not kept as the record's splits

    // Run 2 at the same pace, full splits: no delta against a record without splits, no segment tip.
    h.event(GameEventKind::ResetShot);
    h.step(9.05f, {kInt("CheckpointIndex", 0)});
    h.tick(10.f, {}, kMoving);
    h.step(12.f, {kInt("CheckpointIndex", 1)}, {}, kMoving);
    h.step(14.f, {kInt("CheckpointIndex", 2)}, {}, kMoving);
    h.step(16.f, {kInt("CheckpointIndex", 3)}, {}, kMoving);
    st = h.mod.status();
    CHECK(st.splits.size() == 3);
    for (const auto& s : st.splits) CHECK(!s.hasDelta);
    h.tick(18.f, {}, kMoving);
    g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1);
    if (!g.results.empty()) CHECK(findTip(g.results.front(), "maps.slow_segment") == nullptr);

    // Run 3 is faster: a full record, its splits are stored.
    h.event(GameEventKind::ResetShot);
    h.step(18.05f, {kInt("CheckpointIndex", 0)});
    h.tick(19.f, {}, kMoving);
    h.step(20.5f, {kInt("CheckpointIndex", 1)}, {}, kMoving);
    h.step(22.f, {kInt("CheckpointIndex", 2)}, {}, kMoving);
    h.step(23.5f, {kInt("CheckpointIndex", 3)}, {}, kMoving);
    h.tick(25.f, {}, kMoving);
    g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1 && g.results.front().score == 100.f);
    st = h.mod.status();
    CHECK(st.pbSplits.size() == 3);
    CHECK(!st.last.partialSplits);

    // Run 4: split 1 is level 1 on both sides, the delta is right.
    h.event(GameEventKind::ResetShot);
    h.step(25.05f, {kInt("CheckpointIndex", 0)});
    h.tick(26.f, {}, kMoving);
    h.step(28.f, {kInt("CheckpointIndex", 1)}, {}, kMoving);
    st = h.mod.status();
    CHECK(st.splits.size() == 1 && st.splits[0].level == 1 && st.splits[0].hasDelta);
    if (!st.splits.empty()) CHECK_NEAR(st.splits[0].delta, 0.5f, 1e-3f);
}

TEST(maps_fix_partial_run_gets_no_split_deltas) {
    RunTracker r;
    r.setPersonalBest({2.f, 4.f, 6.f}, 8.f, true);
    r.arm(0.f);
    r.onTick(carSnap(0.01f, {}, {}));
    r.onTick(carSnap(0.1f, {}, kMoving));
    CHECK(r.running());
    r.setProfile(findKismetProfile("egyptian_tomb"));  // level counter found during the run
    CHECK(r.running() && r.partialSplits());
    r.onKismet({kInt("ankh", 1)}, 2.f);
    r.onKismet({kInt("ankh", 2)}, 3.f);
    CHECK(r.splits().size() == 1 && !r.splits()[0].hasDelta);
    float live = 0.f;
    CHECK(!r.liveDelta(5.f, live));  // index-based PB splits are not used, the total is
    CHECK(r.liveDelta(9.1f, live));
    CHECK_NEAR(live, 1.f, 1e-3f);
    r.onGoal(9.1f);
    CHECK(r.last().finished && r.last().partialSplits);
    // A new run is whole again.
    r.onReset(10.f);
    r.onTick(carSnap(10.01f, {}, {}));
    r.onTick(carSnap(10.1f, {}, kMoving));
    CHECK(r.running() && !r.partialSplits());
    // The result builder skips the segment comparison for partial runs.
    RunSummary s = r.last();
    s.time = 20.f;
    const AttemptResult res = makeMapRunResult("k", "t", MapCategory::Obstacle, s, true, 8.f, {2.f, 4.f, 6.f}, 1.f);
    CHECK(findTip(res, "maps.slow_segment") == nullptr);
}

// ---- Reset-ended runs ----------------------------------------------------------------------------------
TEST(maps_fix_reset_ended_run_is_never_a_record) {
    Harness h;
    h.settings.maps.fallbackFinish = 1;  // "But ou reset"
    h.applySettings();
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.tick(0.1f, {}, {});
    h.tick(0.2f, {}, kMoving);
    h.tick(1.5f, {}, kMoving);
    Actions a = h.event(GameEventKind::ResetShot);  // gave up after 1.3 s
    CHECK(a.results.size() == 1);
    if (!a.results.empty()) {
        const AttemptResult& r = a.results.front();
        CHECK(r.success);
        CHECK(!r.hasRecordValue);
        CHECK(r.score <= 60.f);
        CHECK(findTip(r, "maps.reset_end") != nullptr);
        CHECK(findTip(r, "maps.not_eligible") == nullptr);
    }
    CHECK(a.toasts.empty());
    MapsLiveStatus st = h.mod.status();
    CHECK(!st.hasPb && !st.lastWasPb);
    CHECK(st.last.finished && st.last.endedByReset);
    MapStatsView v;
    CHECK(h.mod.statsFor("some_map", v));
    CHECK(v.finishes == 1 && v.attempts == 1 && v.lastRuns.size() == 1 && !v.hasBest);
    CHECK(containsText(h.draw().panels.front(), "Run fini sur reset"));
    // A longer run finished on a goal is the record.
    h.tick(1.6f, {}, {});
    h.tick(1.7f, {}, kMoving);
    h.tick(11.7f, {}, kMoving);
    a = h.event(GameEventKind::Goal);
    CHECK(a.results.size() == 1 && a.results.front().hasRecordValue);
    CHECK(!a.toasts.empty() && a.toasts.front().title == "Premier chrono");
    st = h.mod.status();
    CHECK(st.hasPb && std::fabs(st.pb - 10.f) < 1e-3f);
    // A quick reset-ended run never beats it.
    h.event(GameEventKind::ResetShot);
    h.tick(11.8f, {}, {});
    h.tick(11.9f, {}, kMoving);
    h.tick(14.f, {}, kMoving);
    a = h.event(GameEventKind::ResetShot);
    CHECK(a.results.size() == 1 && !a.results.front().hasRecordValue && a.results.front().score <= 60.f);
    CHECK(std::fabs(h.mod.status().pb - 10.f) < 1e-3f);
}

TEST(maps_fix_reset_mode_off_still_aborts) {
    RunTracker r;
    r.arm(0.f);
    r.onTick(carSnap(0.01f, {}, {}));
    r.onTick(carSnap(0.1f, {}, kMoving));
    r.onReset(3.f);
    CHECK(!r.last().finished && !r.last().endedByReset);
}

// ---- Pins without a level variable ---------------------------------------------------------------------
TEST(maps_fix_start_only_pin_finishes_on_fall_or_goal) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Pinned_Map");
    h.step(0.f, {kBool("Go", false), kInt("Other", 0)});
    h.command("pin", {"start", "Go"});
    MapsLiveStatus st = h.mod.status();
    CHECK(st.profileSource == ProfileSource::Pins);
    CHECK(!st.ownStart);
    CHECK(st.finalLevel == 0);
    h.step(1.f, {kBool("Go", false), kInt("Other", 0)});
    h.step(2.f, {kBool("Go", true), kInt("Other", 0)});
    CHECK(h.mod.status().phase == RunPhase::Running);
    Actions a = h.step(7.f, {kBool("Go", false), kInt("Other", 0)});  // the map stops its run
    CHECK(a.results.size() == 1);
    if (!a.results.empty()) {
        CHECK(a.results.front().success && a.results.front().hasRecordValue);
        CHECK_NEAR(a.results.front().valueForRecord, 5.f, 1e-3f);
    }
    // A goal ends it too.
    h.step(8.f, {kBool("Go", true), kInt("Other", 0)});
    h.tick(10.f);
    a = h.event(GameEventKind::Goal);
    CHECK(a.results.size() == 1);
    if (!a.results.empty()) CHECK_NEAR(a.results.front().valueForRecord, 2.f, 1e-3f);
    // Start + final level but no level variable: the final level is ignored, the run still finishes.
    h.command("pin", {"final", "5"});
    st = h.mod.status();
    CHECK(st.pinFinal == 5 && st.finalLevel == 0);
    h.step(11.f, {kBool("Go", false), kInt("Other", 0)});
    h.step(12.f, {kBool("Go", true), kInt("Other", 0)});
    h.tick(15.f);
    a = h.event(GameEventKind::Goal);
    CHECK(a.results.size() == 1);
    MapStatsView v;
    h.mod.statsFor("pinned_map", v);
    CHECK(v.finishes == 3);
}

TEST(maps_fix_start_and_level_pins_keep_level_rules) {
    // With a level variable and no final level, a falling edge mid-course is still an abandon.
    Harness h;
    h.setContext(GameMode::Workshop, true, "Pinned_Map");
    h.command("pin", {"start", "Go"});
    h.command("pin", {"level", "Stage"});
    h.step(1.f, {kBool("Go", false), kInt("Stage", 1)});
    h.step(2.f, {kBool("Go", true), kInt("Stage", 1)});
    h.step(3.f, {kBool("Go", true), kInt("Stage", 2)});
    const Actions a = h.step(4.f, {kBool("Go", false), kInt("Stage", 2)});
    CHECK(a.results.empty());
    CHECK(h.mod.status().last.valid && !h.mod.status().last.finished);
}

// ---- Catalogue final level -----------------------------------------------------------------------------
TEST(maps_fix_catalogue_final_level_ends_signature_runs) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "DribbleChallenge2", "964271505");
    h.step(0.f, timingLevel(false, 1));
    MapsLiveStatus st = h.mod.status();
    CHECK(st.profileKey == "timing_level");
    CHECK(st.profileSource == ProfileSource::Signature);
    CHECK(st.finalLevel == 30);
    h.step(1.f, timingLevel(true, 1));
    for (int lvl = 2; lvl <= 30; ++lvl) h.step(static_cast<float>(lvl), timingLevel(true, lvl));
    Actions a = h.step(31.f, timingLevel(true, 1));  // back to level 1 after the last level
    CHECK(a.results.size() == 1);
    if (!a.results.empty()) {
        CHECK(a.results.front().success && a.results.front().hasRecordValue);
        CHECK_NEAR(a.results.front().valueForRecord, 30.f, 1e-3f);
    }
    // A goal on the last level finishes too.
    h.step(40.f, timingLevel(false, 1));
    h.step(41.f, timingLevel(true, 1));
    for (int lvl = 2; lvl <= 30; ++lvl) h.step(40.f + static_cast<float>(lvl), timingLevel(true, lvl));
    h.tick(71.5f);
    a = h.event(GameEventKind::Goal);
    CHECK(a.results.size() == 1);
    // A goal before the last level is a level transition.
    h.step(80.f, timingLevel(false, 1));
    h.step(81.f, timingLevel(true, 1));
    h.step(82.f, timingLevel(true, 2));
    h.tick(82.5f);
    a = h.event(GameEventKind::Goal);
    CHECK(a.results.empty() && h.mod.status().phase == RunPhase::Running);
}

TEST(maps_fix_final_level_shown_only_when_the_timer_uses_it) {
    // Unknown map with the generic pair: no final level, nothing advertised.
    Harness u;
    u.setContext(GameMode::Workshop, true, "Mystery_Timing_Map");
    u.step(0.f, timingLevel(false, 1));
    CHECK(u.mod.status().profileKey == "timing_level");
    CHECK(u.mod.status().finalLevel == 0);
    u.step(1.f, timingLevel(true, 1));
    u.step(2.f, timingLevel(true, 2));
    const DrawList d = u.draw();
    CHECK(containsText(d.panels.front(), "Niveau 2"));
    CHECK(!containsText(d.panels.front(), "Niveau 2/"));
    // Catalogue map without any profile (no variables): own timing, no "k/N" either.
    Harness c;
    c.setContext(GameMode::Workshop, true, "DribbleChallenge2", "964271505");
    CHECK(c.mod.status().catalogLevels == 30);
    CHECK(c.mod.status().finalLevel == 0);
    // A pinned level counter on a catalogue map uses the catalogue's count unless the player sets one.
    c.command("pin", {"level", "Stage"});
    CHECK(c.mod.status().finalLevel == 30);
    c.command("pin", {"final", "12"});
    CHECK(c.mod.status().finalLevel == 12);
}

// ---- Deaths baseline -----------------------------------------------------------------------------------
TEST(maps_fix_deaths_counter_reset_by_the_map_rebases_the_run) {
    RunTracker r;
    r.setProfile(findKismetProfile("leth_rings"));
    r.onKismet(lethVars(false, 1, 12), 0.f);
    r.onKismet(lethVars(true, 1, 12), 1.f);   // run start with 12 deaths from earlier attempts
    r.onKismet(lethVars(true, 1, 0), 1.1f);   // the map clears its counter one sample later
    CHECK(r.deathsInRun() == 0);
    r.onKismet(lethVars(true, 2, 3), 3.f);
    CHECK(r.deathsInRun() == 3);
    r.onUnload(4.f);
    CHECK(r.last().hasDeaths && r.last().deaths == 3);
}

// ---- Map timer anchoring -------------------------------------------------------------------------------
TEST(maps_fix_map_timer_held_at_zero_does_not_count_the_wait) {
    RunTracker r;
    r.setProfile(findKismetProfile("sjr2"));
    r.onKismet({kStr("displaytimer", "03:12")}, 0.f);
    r.onKismet({kStr("displaytimer", "00:00")}, 1.f);  // map reset: run started
    CHECK(r.running());
    CHECK_NEAR(r.startTime(), 1.f, 1e-5f);
    r.onKismet({kStr("displaytimer", "00:00")}, 3.f);  // held while the player waits at the start
    r.onKismet({kStr("displaytimer", "00:01")}, 6.f);  // the map's clock really started at 5 s
    CHECK_NEAR(r.startTime(), 5.f, 1e-4f);
    CHECK_NEAR(r.elapsed(8.f), 3.f, 1e-4f);
    r.onKismet({kStr("displaytimer", "00:02")}, 7.f);  // later ticks never move the start again
    CHECK_NEAR(r.startTime(), 5.f, 1e-4f);
    // No hold: the start stays at the zero (within one Kismet sample).
    r.onKismet({kStr("displaytimer", "00:00")}, 10.f);
    r.onKismet({kStr("displaytimer", "00:01")}, 11.02f);
    CHECK(r.startTime() >= 10.f && r.startTime() <= 10.03f);
    // Never earlier than the zero itself, even when the first value read is late.
    r.onKismet({kStr("displaytimer", "00:00")}, 20.f);
    r.onKismet({kStr("displaytimer", "00:05")}, 21.f);
    CHECK_NEAR(r.startTime(), 20.f, 1e-5f);
}

// ---- Catalogue text ------------------------------------------------------------------------------------
TEST(maps_fix_catalogue_french_agreement) {
    const CatalogMap* wall = MapCatalog::findById("814263405");
    CHECK(wall != nullptr);
    if (wall) {
        CHECK(wall->trains.find("poursuivie") != std::string::npos);
        CHECK(wall->trains.find("poursuivi ") == std::string::npos);
    }
}
