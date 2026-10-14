// MapsModule end to end: contexts, Kismet runs, own timing, results, stats, HUD, commands, settings.
#include <set>

#include "tests/TestFramework.h"

#include "tests/maps/MapsTestUtil.h"

using namespace rlt;
using namespace maps_test;

namespace {

const char* kGiant = "2025564083";

void enterGiant(Harness& h) {
    h.setContext(GameMode::Workshop, true, "LethamyrGiantRings", kGiant, "", MapCategory::Rings);
}

bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

const Metric* metric(const AttemptResult& r, const std::string& key) {
    for (const auto& m : r.metrics)
        if (m.key == key) return &m;
    return nullptr;
}

} // namespace

TEST(maps_schema_defaults_parse_and_are_french) {
    SettingsSchema schema;
    describeGeneralSettings(schema);
    MapsModule::describeSettings(schema);
    Settings s;
    s.maps.enabled = false;
    s.maps.hudX = 0.5f;
    schema.applyDefaults(s);
    CHECK(s.maps.enabled);
    CHECK(s.maps.readKismet && s.maps.fallbackTiming && s.maps.autoLevel && s.maps.announcePb);
    CHECK_NEAR(s.maps.hudX, 0.01f, 1e-6f);
    CHECK_NEAR(s.maps.hudY, 0.12f, 1e-6f);
    CHECK_NEAR(s.maps.startSpeed, 100.f, 1e-4f);
    CHECK(s.maps.fallbackFinish == 0);
    CHECK(s.maps.extraFolders.empty());
    const auto defs = schema.forModule("maps");
    CHECK(defs.size() >= 18);
    std::set<std::string> cvars;
    bool anyAdvanced = false;
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_map_", 0) == 0, d->cvar);
        CHECK_MSG(cvars.insert(d->cvar).second, d->cvar);
        CHECK(!d->label.empty() && !d->tooltip.empty() && !d->section.empty());
        CHECK(!hasEmDash(d->label) && !hasEmDash(d->tooltip));
        Settings t;
        CHECK_MSG(schema.apply(t, d->cvar, d->defaultValue), d->cvar);
        CHECK(d->read && d->read(t) == d->read(s));
        anyAdvanced = anyAdvanced || d->advanced;
    }
    CHECK(anyAdvanced);
    CHECK(schema.sections("maps").size() == 3);
    // Values are clamped by the schema.
    CHECK(schema.apply(s, "rlt_map_hud_x", "7"));
    CHECK(s.maps.hudX == 1.f);
    CHECK(schema.apply(s, "rlt_map_extra_folders", "D:\\Maps;E:\\x"));
    CHECK(s.maps.extraFolders == "D:\\Maps;E:\\x");
    CHECK(!schema.apply(s, "rlt_map_start_speed", "fast"));
}

TEST(maps_context_identifies_known_map) {
    Harness h;
    enterGiant(h);
    const MapsLiveStatus st = h.mod.status();
    CHECK(st.onCustomMap);
    CHECK(st.key == kGiant);
    CHECK(st.title == "Lethamyr's Giant Rings");
    CHECK(st.category == MapCategory::Rings);
    CHECK(st.categorySource == "catalogue");
    CHECK(st.inCatalog && st.author == "Lethamyr" && st.catalogLevels == 20);
    CHECK(st.profileSource == ProfileSource::WorkshopId);
    CHECK(st.profileKey == "leth_rings");
    CHECK(!st.ownStart);
    CHECK(st.finalLevel == 20);
    CHECK(!st.hints.empty());
    MapStatsView v;
    CHECK(h.mod.statsFor(kGiant, v));
    CHECK(v.sessions == 1 && v.title == "Lethamyr's Giant Rings" && v.lastDate == "2026-10-13");
}

TEST(maps_stock_map_is_ignored) {
    Harness h;
    h.setContext(GameMode::Freeplay, false, "Stadium_P");
    CHECK(!h.mod.status().onCustomMap);
    h.tick(1.f, {}, {});
    h.tick(1.1f, {}, {900.f, 0.f, 0.f});
    h.kismet(lethVars(true, 3));
    CHECK(h.mod.statsList().empty());
    CHECK(h.draw().panels.empty());
    CHECK(h.all.results.empty());
}

TEST(maps_leth_run_produces_result_and_personal_best) {
    Harness h;
    enterGiant(h);
    const Actions last = lethRun(h, 5.f, 2.f);
    CHECK(last.results.size() == 1);
    const AttemptResult& r = last.results.front();
    CHECK(r.module == "maps");
    CHECK(r.skill == std::string("map.") + kGiant);
    CHECK(r.title == "Lethamyr's Giant Rings");
    CHECK(r.success);
    CHECK(r.hasRecordValue && r.lowerIsBetter);
    CHECK_NEAR(r.valueForRecord, 40.f, 1e-3f);
    CHECK(r.score == 80.f);  // first finished run
    CHECK(r.verdict == Verdict::Good);
    CHECK(metric(r, "time") != nullptr);
    CHECK(metric(r, "splits") != nullptr && metric(r, "splits")->value == 19.f);
    CHECK(metric(r, "pb_delta") == nullptr);
    CHECK(last.requestSave);
    CHECK(!last.toasts.empty() && last.toasts.front().title == "Premier chrono");
    MapStatsView v;
    CHECK(h.mod.statsFor(kGiant, v));
    CHECK(v.hasBest && std::fabs(v.best - 40.f) < 1e-3f);
    CHECK(v.attempts == 1 && v.finishes == 1);
    CHECK(v.lastRuns.size() == 1);
    CHECK(v.maxLevel == 20);
    const MapsLiveStatus st = h.mod.status();
    CHECK(st.hasPb && st.pbSplits.size() == 19);
    CHECK(st.lastWasPb);
    // Never touches the world.
    CHECK(!h.all.setBall && !h.all.setCar && !h.all.overrideInputs && !h.all.freezeCar && !h.all.setGameSpeed);
    CHECK(h.host.requests == 0);
}

TEST(maps_slower_run_scores_lower_and_names_the_slow_segment) {
    Harness h;
    enterGiant(h);
    lethRun(h, 5.f, 2.f);
    // Second run: level 7 (segment 6) is 3 s slower.
    const Actions a = lethRun(h, 100.f, 2.f, {0.f, 0.f, 0.f, 0.f, 0.f, 3.f});
    CHECK(a.results.size() == 1);
    const AttemptResult& r = a.results.front();
    CHECK_NEAR(r.valueForRecord, 43.f, 1e-3f);
    CHECK(r.score < 100.f && r.score > 70.f);  // 7.5 % slower
    const Metric* d = metric(r, "pb_delta");
    CHECK(d != nullptr && std::fabs(d->value - 3.f) < 1e-3f);
    CHECK(!r.tips.empty());
    CHECK(r.tips.front().code == "maps.slow_segment");
    CHECK(r.tips.front().text.find("segment 6") != std::string::npos);
    CHECK(a.toasts.empty());  // not a record: no announcement
    MapStatsView v;
    h.mod.statsFor(kGiant, v);
    CHECK(std::fabs(v.best - 40.f) < 1e-3f);  // record unchanged
    CHECK(v.finishes == 2 && v.lastRuns.size() == 2);
    // Third run beats the record: score 100, chat + toast.
    const Actions pb = lethRun(h, 200.f, 1.9f);
    CHECK(pb.results.size() == 1 && pb.results.front().score == 100.f);
    CHECK(!pb.toasts.empty() && pb.toasts.front().title == "Nouveau record !");
    CHECK(!pb.chat.empty());
    h.mod.statsFor(kGiant, v);
    CHECK(std::fabs(v.best - 38.f) < 1e-3f);
}

TEST(maps_announce_pb_can_be_disabled) {
    Harness h;
    h.settings.maps.announcePb = false;
    h.applySettings();
    enterGiant(h);
    const Actions a = lethRun(h, 5.f, 2.f);
    CHECK(a.results.size() == 1);
    CHECK(a.toasts.empty() && a.chat.empty());
}

TEST(maps_result_builder_rules) {
    RunSummary run;
    run.valid = run.finished = true;
    run.time = 60.f;
    run.hasDeaths = true;
    run.deaths = 5;
    AttemptResult r = makeMapRunResult("k", "", MapCategory::Rings, run, true, 50.f, {}, 1.f);
    CHECK(r.title == "k");
    CHECK_NEAR(r.score, 50.f, 1e-3f);  // 20 % slower
    CHECK(r.verdict == Verdict::Ok);
    bool deaths = false, cat = false;
    for (const auto& t : r.tips) {
        deaths = deaths || t.code == "maps.deaths";
        cat = cat || t.code == "maps.rings.boost";
        CHECK(!hasEmDash(t.text));
    }
    CHECK(deaths && cat);
    CHECK(r.tips.front().code == "maps.deaths");  // 2.5 points vs 1
    // Way slower: score floors at 0.
    run.time = 100.f;
    CHECK(makeMapRunResult("k", "t", MapCategory::Dribble, run, true, 50.f, {}, 1.f).score == 0.f);
    // Skipped levels: no record value, capped score, explanatory tip.
    run.time = 40.f;
    run.recordEligible = false;
    r = makeMapRunResult("k", "t", MapCategory::Other, run, true, 50.f, {}, 1.f);
    CHECK(!r.hasRecordValue && r.score <= 60.f);
    bool ne = false;
    for (const auto& t : r.tips) ne = ne || t.code == "maps.not_eligible";
    CHECK(ne);
    CHECK(r.tips.size() <= 3);
    // Resets as fallback when the map has no death counter.
    RunSummary k;
    k.valid = k.finished = true;
    k.time = 10.f;
    k.resets = 4;
    r = makeMapRunResult("k", "t", MapCategory::Obstacle, k, false, 0.f, {}, 1.f);
    CHECK(!r.tips.empty() && r.tips.front().code == "maps.resets");
    CHECK(formatDeltaSeconds(1.234f) == "+1.23 s");
    CHECK(formatDeltaSeconds(-0.5f) == "-0.50 s");
    CHECK(formatDeltaSeconds(0.001f) == "0.00 s");
}

TEST(maps_profile_verified_against_variables) {
    // Known id, but the map update renamed its variables: fall back to signature, then to own timing.
    Harness h;
    enterGiant(h);
    h.step(1.f, {kInt("Stage", 1), kBool("Running", false)});
    MapsLiveStatus st = h.mod.status();
    CHECK(st.profileSource == ProfileSource::None);
    CHECK(st.ownStart);
    CHECK(st.kismetSeen && st.kismetVars == 2);
    // Unknown map whose variables match the Leth signature.
    Harness s;
    s.setContext(GameMode::Workshop, true, "SomeRingsReupload");
    s.step(1.f, lethVars(false, 1));
    st = s.mod.status();
    CHECK(st.profileSource == ProfileSource::Signature);
    CHECK(st.profileKey == "leth_rings");
    CHECK(st.category == MapCategory::Rings);
    CHECK(st.categorySource == "variables de la map");
    const Actions a = lethRun(s, 2.f, 1.f);
    CHECK(a.results.size() == 1 && a.results.front().skill == "map.someringsreupload");
}

TEST(maps_auto_level_candidate_becomes_splits) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "MysteryCourse");
    h.step(0.f, {kInt("CheckpointIndex", 0), kInt("Seconds", 0)});
    CHECK(h.mod.status().profileSource == ProfileSource::None);
    // Own timing: rest then move.
    h.tick(0.5f, {}, {});
    h.tick(0.6f, {}, {800.f, 0.f, 0.f});
    CHECK(h.mod.status().phase == RunPhase::Running);
    h.step(3.f, {kInt("CheckpointIndex", 1), kInt("Seconds", 3)});
    MapsLiveStatus st = h.mod.status();
    CHECK(st.profileSource == ProfileSource::Auto);
    CHECK(st.profileKey == "auto");
    CHECK(st.phase == RunPhase::Running);  // the own-timed run goes on
    h.step(5.f, {kInt("CheckpointIndex", 2), kInt("Seconds", 5)});
    st = h.mod.status();
    CHECK(st.splits.size() == 1 && st.splits[0].level == 2);
    // With auto level off, nothing is guessed.
    Harness n;
    n.settings.maps.autoLevel = false;
    n.applySettings();
    n.setContext(GameMode::Workshop, true, "MysteryCourse");
    n.step(0.f, {kInt("CheckpointIndex", 0)});
    n.step(1.f, {kInt("CheckpointIndex", 1)});
    CHECK(n.mod.status().profileSource == ProfileSource::None);
}

TEST(maps_own_timing_fallback_through_module) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Unknown_Obstacle_Course_P");
    MapsLiveStatus st = h.mod.status();
    CHECK(st.phase == RunPhase::Armed);
    CHECK(st.category == MapCategory::Obstacle);
    CHECK(st.categorySource == "nom de la map");
    h.tick(0.1f, {}, {});
    h.tick(0.2f, {}, {50.f, 0.f, 0.f});
    h.tick(0.3f, {}, {600.f, 0.f, 0.f});
    CHECK(h.mod.status().phase == RunPhase::Running);
    h.tick(10.3f, {}, {600.f, 0.f, 0.f});
    const Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1);
    CHECK_NEAR(g.results.front().valueForRecord, 10.f, 1e-3f);
    // Reset arms a new attempt; a reset mid-run aborts it (counted as an attempt, no result).
    h.event(GameEventKind::ResetShot);
    h.tick(11.f, {}, {});
    h.tick(11.1f, {}, {900.f, 0.f, 0.f});
    h.tick(14.1f, {}, {900.f, 0.f, 0.f});
    const Actions r = h.event(GameEventKind::ResetShot);
    CHECK(r.results.empty());
    MapStatsView v;
    h.mod.statsFor(h.mod.status().key, v);
    CHECK(v.attempts == 2 && v.finishes == 1 && v.goals == 1);
    // Tiny nudge then reset: noise, not an attempt.
    h.tick(15.f, {}, {});
    h.tick(15.1f, {}, {900.f, 0.f, 0.f});
    h.tick(15.5f, {}, {900.f, 0.f, 0.f});
    h.event(GameEventKind::ResetShot);
    h.mod.statsFor(h.mod.status().key, v);
    CHECK(v.attempts == 2);
}

TEST(maps_kismet_reading_can_be_disabled) {
    Harness h;
    h.settings.maps.readKismet = false;
    h.applySettings();
    enterGiant(h);
    CHECK(h.mod.status().profileSource == ProfileSource::None);
    CHECK(h.mod.status().ownStart);
    h.step(1.f, lethVars(false, 1));
    CHECK(!h.mod.status().kismetSeen);
    // Turning it back on restores the known profile.
    h.settings.maps.readKismet = true;
    h.applySettings();
    CHECK(h.mod.status().profileSource == ProfileSource::WorkshopId);
}

TEST(maps_stats_time_touches_goals_sessions) {
    Harness h;
    enterGiant(h);
    for (int i = 0; i < 240; ++i) h.tick(1.f + static_cast<float>(i) / 120.f, {}, {});
    h.tick(10.f, {}, {});                     // hitch: big gap not counted
    h.tick(10.1f, {}, {}, true, true);        // paused: not counted
    h.touch(true);
    h.touch(false);                           // continuing contact: not a new touch
    h.touch(true);
    h.event(GameEventKind::Goal);
    MapStatsView v;
    h.mod.statsFor(kGiant, v);
    CHECK_NEAR(v.secondsPlayed, 239.f / 120.f, 0.02f);
    CHECK(v.touches == 2 && v.goals == 1 && v.sessions == 1);
    CHECK_NEAR(h.mod.status().sessionSeconds, 239.f / 120.f, 0.02f);
    // Reload of the same map (servertravel): same session.
    h.event(GameEventKind::MapUnloaded);
    CHECK(!h.mod.status().onCustomMap);
    h.setContext(GameMode::Menu, false, "");
    enterGiant(h);
    h.mod.statsFor(kGiant, v);
    CHECK(v.sessions == 1);
    // Going to freeplay then back: new session.
    h.setContext(GameMode::Freeplay, false, "Stadium_P");
    enterGiant(h);
    h.mod.statsFor(kGiant, v);
    CHECK(v.sessions == 2);
}

TEST(maps_map_change_aborts_run_and_resets_state) {
    Harness h;
    enterGiant(h);
    h.step(1.f, lethVars(false, 1));
    h.step(2.f, lethVars(true, 1));
    h.step(5.f, lethVars(true, 2));
    CHECK(h.mod.status().phase == RunPhase::Running);
    h.setContext(GameMode::Workshop, true, "DribbleChallenge2", "964271505");
    const MapsLiveStatus st = h.mod.status();
    CHECK(st.key == "964271505");
    CHECK(st.phase != RunPhase::Running);
    CHECK(st.splits.empty());
    CHECK(st.kismetVars == 0);
    CHECK(st.category == MapCategory::Dribble);
    MapStatsView v;
    h.mod.statsFor(kGiant, v);
    CHECK(v.attempts == 1 && v.finishes == 0);  // abandoned after 3 s
    CHECK(h.mod.statsList().front().key == "964271505");
}

TEST(maps_stem_key_migrates_to_workshop_id) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "MyRingsMap");
    for (int i = 0; i < 120; ++i) h.tick(static_cast<float>(i) / 120.f, {}, {});
    h.command("fav", {});
    CHECK(h.mod.isFavorite("myringsmap"));
    // The glue learns the workshop id of the same map (scan finished): stats move to the id key.
    h.setContext(GameMode::Workshop, true, "MyRingsMap", "3000000001", "My Rings Map");
    MapStatsView v;
    CHECK(!h.mod.statsFor("myringsmap", v));
    CHECK(h.mod.statsFor("3000000001", v));
    CHECK(v.sessions == 1);
    CHECK(v.secondsPlayed > 0.9);
    CHECK(v.favorite);
    CHECK(h.mod.status().key == "3000000001");
    CHECK(h.mod.status().title == "My Rings Map");
}

TEST(maps_hud_only_on_custom_maps) {
    Harness h;
    h.setContext(GameMode::Freeplay, false, "Stadium_P");
    h.tick(1.f);
    CHECK(h.draw().panels.empty());
    enterGiant(h);
    h.tick(2.f);
    DrawList d = h.draw(2560.f, 1440.f);
    CHECK(d.panels.size() == 1);
    CHECK(d.lines3D.empty());
    if (!d.panels.empty()) {
        const DrawPanel& p = d.panels.front();
        CHECK_NEAR(p.pos.x, 0.01f * 2560.f, 0.01f);
        CHECK_NEAR(p.pos.y, 0.12f * 1440.f, 0.01f);
        CHECK(p.title == "Lethamyr's Giant Rings");
        CHECK(containsText(p, "En attente du depart"));  // ASCII-folded HUD text
        CHECK(containsText(p, "Record : aucun"));
        CHECK(containsText(p, "Profil : Anneaux de Lethamyr"));
        CHECK(p.lines.size() <= 10);
    }
    // HUD off.
    h.settings.maps.hudEnabled = false;
    h.applySettings();
    CHECK(h.draw().panels.empty());
    h.settings.maps.hudEnabled = true;
    h.applySettings();
    // Replay of a custom map: no panel.
    h.game.mode = GameMode::Replay;
    CHECK(h.draw().panels.empty());
}

TEST(maps_hud_shows_timer_level_and_split_delta) {
    Harness h;
    enterGiant(h);
    lethRun(h, 1.f, 2.f);  // record 40 s, splits every 2 s
    // New run, ahead of the record on split 1.
    h.step(100.f, lethVars(false, 1));
    h.step(100.1f, lethVars(true, 1));
    h.step(101.6f, lethVars(true, 2));  // 1.5 s vs 2.0 s
    DrawList d = h.draw();
    CHECK(d.panels.size() == 1);
    const DrawPanel& p = d.panels.front();
    CHECK(containsText(p, "Chrono 0:01.50"));
    CHECK(containsText(p, "Niveau 2/20"));
    CHECK(containsText(p, "Split 1 : -0.50 s"));
    CHECK(containsText(p, "Record : 0:40.00"));
    bool green = false, bar = false;
    for (const auto& l : p.lines) {
        if (l.text.find("Split 1") != std::string::npos) green = l.color == h.settings.maps.aheadColor;
        if (l.text.find("Niveau") != std::string::npos) bar = l.bar > 0.09f && l.bar < 0.11f;
    }
    CHECK(green && bar);
    // Much later without a split: live delta in red.
    h.tick(106.f);
    d = h.draw();
    CHECK(containsText(d.panels.front(), "Retard : +"));
    // After a finish, the last run line appears.
    float t = 106.f;
    for (int lvl = 3; lvl <= 20; ++lvl) h.step(t += 1.f, lethVars(true, lvl));
    h.step(t += 1.f, lethVars(true, 1));
    d = h.draw();
    CHECK(containsText(d.panels.front(), "Dernier run"));
    CHECK(containsText(d.panels.front(), "(record !)"));
}

TEST(maps_hud_own_timing_prompts_and_profile_line) {
    Harness h;
    h.settings.maps.hudProfile = true;
    h.applySettings();
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.tick(0.1f, {}, {});
    DrawList d = h.draw();
    CHECK(containsText(d.panels.front(), "Pret : le chrono part quand tu bouges"));
    CHECK(containsText(d.panels.front(), "Chrono auto"));
    CHECK(d.panels.front().title == "Some Map");
    h.tick(0.2f, {}, {800.f, 0.f, 0.f});
    h.tick(3.2f, {}, {800.f, 0.f, 0.f});
    h.event(GameEventKind::Goal);
    d = h.draw();
    CHECK(containsText(d.panels.front(), "Dernier run : 0:03.00"));
    CHECK(containsText(d.panels.front(), "pour relancer un run"));
    h.settings.maps.hudProfile = false;
    h.applySettings();
    d = h.draw();
    CHECK(!containsText(d.panels.front(), "Chrono auto"));
}

namespace {

// Lines the compact HUD shows (the engine drops the detail lines).
std::vector<std::string> shownLines(const DrawPanel& p) {
    std::vector<std::string> out;
    for (const auto& l : p.lines)
        if (!l.detail) out.push_back(l.text);
    return out;
}

bool shownHas(const DrawPanel& p, const std::string& needle) {
    for (const auto& s : shownLines(p))
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(maps_hud_compact_keeps_chrono_split_and_record) {
    Harness h;
    h.settings.maps.hudProfile = true;
    h.applySettings();
    enterGiant(h);
    lethRun(h, 1.f, 2.f);  // record 40 s, splits every 2 s
    h.step(100.f, lethVars(false, 1));
    h.step(100.1f, lethVars(true, 1));
    h.step(101.6f, lethVars(true, 2, 3));
    DrawPanel p = h.draw().panels.front();
    CHECK_EQ(shownLines(p).size(), static_cast<size_t>(3));
    CHECK(shownHas(p, "Chrono 0:01.50"));
    CHECK(shownHas(p, "Split 1 : -0.50 s"));
    CHECK(shownHas(p, "Niveau 2/20"));
    CHECK(!shownHas(p, "Record"));  // a split is shown: the record waits
    CHECK(!shownHas(p, "Profil"));
    CHECK(!shownHas(p, "Morts"));
    CHECK(containsText(p, "Record : 0:40.00"));  // still in the full HUD
    CHECK(containsText(p, "Profil : "));
    // Finished: the last run and the record stay, the level goes.
    float t = 101.6f;
    for (int lvl = 3; lvl <= 20; ++lvl) h.step(t += 1.f, lethVars(true, lvl));
    h.step(t += 1.f, lethVars(true, 1));
    p = h.draw().panels.front();
    CHECK(shownLines(p).size() <= 3u);
    CHECK(shownHas(p, "Dernier run"));
    CHECK(shownHas(p, "(record !)"));
    CHECK(shownHas(p, "Record : "));
    CHECK(!shownHas(p, "Niveau"));
    for (const auto& s : shownLines(p)) CHECK(s.find(" ms") == std::string::npos);
}

TEST(maps_hud_compact_own_timing_shows_the_prompt) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.tick(0.1f, {}, {});
    DrawPanel p = h.draw().panels.front();
    CHECK(shownHas(p, "Pret : le chrono part quand tu bouges"));
    CHECK(!shownHas(p, "Record : aucun"));  // nothing to beat yet: detail
    h.tick(0.2f, {}, {800.f, 0.f, 0.f});
    h.tick(3.2f, {}, {800.f, 0.f, 0.f});
    h.event(GameEventKind::Goal);
    p = h.draw().panels.front();
    CHECK(shownHas(p, "Dernier run : 0:03.00"));
    CHECK(shownHas(p, "pour relancer un run"));
    CHECK(shownLines(p).size() <= 3u);
}

TEST(maps_hud_compact_skips_a_panel_without_main_line) {
    Harness h;
    h.settings.maps.fallbackTiming = false;
    h.applySettings();
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.tick(0.1f, {}, {});
    CHECK_EQ(h.draw().panels.size(), static_cast<size_t>(1));  // full HUD: map name and "no record yet"
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    DrawContext d{h.settings, h.game, h.profile, h.snap, h.hasSnapshot, false, 1.f, true};
    DrawList out;
    h.mod.draw(v, d, out);
    CHECK(out.panels.empty());
}

TEST(maps_commands_favorites_played_category) {
    Harness h;
    h.command("fav", {kGiant});
    CHECK(h.mod.isFavorite(kGiant));
    h.command("togglefav", {kGiant});
    CHECK(!h.mod.isFavorite(kGiant));
    h.command("togglefav", {kGiant});
    h.command("unfav", {kGiant});
    CHECK(h.mod.favorites().empty());
    Actions a = h.command("fav", {});  // no current map, no id
    CHECK(!a.toasts.empty() && a.toasts.front().kind == ToastKind::Warning);

    h.command("played", {"964271505", "Dribbling Challenge #2", "dribble", "C:\\w\\964271505\\Dribble.udk"});
    h.command("played", {"my_map", "Ma map", "airdribble"});
    auto list = h.mod.statsList();
    CHECK(list.size() == 2);
    CHECK(list.front().key == "my_map" && list.front().title == "Ma map");
    CHECK(list.front().category == MapCategory::AirDribble);
    CHECK(list.back().path == "C:\\w\\964271505\\Dribble.udk");

    h.command("category", {"rings", "my_map"});
    MapStatsView v;
    h.mod.statsFor("my_map", v);
    CHECK(v.category == MapCategory::Rings && v.categoryOverride);
    CHECK(h.mod.categoryOverrides().count("my_map") == 1);
    a = h.command("category", {"banana", "my_map"});
    CHECK(!a.toasts.empty());
    h.command("category", {"auto", "my_map"});
    h.mod.statsFor("my_map", v);
    CHECK(!v.categoryOverride);
    // The player's category wins over the catalogue on the current map.
    enterGiant(h);
    h.command("category", {"obstacle"});
    CHECK(h.mod.status().category == MapCategory::Obstacle);
    CHECK(h.mod.status().categoryOverride);
    GameContext g = h.game;
    h.mod.refineContext(g);
    CHECK(g.category == MapCategory::Obstacle);
    CHECK(g.mapTitle == "Lethamyr's Giant Rings");
}

TEST(maps_commands_navigation_and_load) {
    Harness h;
    Actions a = h.command("reload");
    CHECK(a.consoleCommands.size() == 1 && a.consoleCommands[0] == "unreal:servertravel ?");
    a = h.command("menu");
    CHECK(a.consoleCommands[0] == "unreal:start MENU_Main_p?close");
    a = h.command("freeplay");
    CHECK(a.consoleCommands[0] == "load_freeplay");
    a = h.command("load", {kGiant});
    CHECK(a.consoleCommands.empty() && !a.toasts.empty());  // path unknown yet
    h.command("register", {kGiant, "C:\\Steam\\workshop\\content\\252950\\2025564083\\GiantRings.udk", "odd"});
    a = h.command("load", {kGiant});
    CHECK(a.consoleCommands.size() == 1);
    CHECK(a.consoleCommands[0] == "load_workshop \"C:\\Steam\\workshop\\content\\252950\\2025564083\\GiantRings.udk\"");
    a = h.command("load", {"GiantRings"});  // by stem
    CHECK(a.consoleCommands.size() == 1);
    // A path from "played" is remembered too.
    h.command("played", {"abc_map", "Abc", "other", "D:\\maps\\abc.udk"});
    a = h.command("load", {"abc_map"});
    CHECK(a.consoleCommands.size() == 1 && a.consoleCommands[0] == "load_workshop \"D:\\maps\\abc.udk\"");
    a = h.command("load", {});
    CHECK(a.consoleCommands.empty());
    // Never world control from commands.
    CHECK(!h.all.setBall && !h.all.setCar && !h.all.setGameSpeed);
}

TEST(maps_commands_pins_define_timing) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Pinned_Map");
    h.step(0.f, {kBool("Go", false), kInt("Stage", 1)});
    CHECK(h.mod.status().profileSource == ProfileSource::None);
    Actions a = h.command("pin", {"start", "Go"});
    CHECK(!a.toasts.empty() && a.toasts.front().kind == ToastKind::Ok);
    h.command("pin", {"level", "Stage"});
    h.command("pin", {"final", "3"});
    MapsLiveStatus st = h.mod.status();
    CHECK(st.profileSource == ProfileSource::Pins);
    CHECK(st.pinStart == "Go" && st.pinLevel == "Stage" && st.pinFinal == 3);
    CHECK(st.finalLevel == 3);
    h.step(1.f, {kBool("Go", false), kInt("Stage", 1)});
    h.step(2.f, {kBool("Go", true), kInt("Stage", 1)});
    h.step(4.f, {kBool("Go", true), kInt("Stage", 2)});
    h.step(6.f, {kBool("Go", true), kInt("Stage", 3)});
    a = h.step(8.f, {kBool("Go", true), kInt("Stage", 1)});  // wrap after the final level
    CHECK(a.results.size() == 1);
    CHECK_NEAR(a.results.front().valueForRecord, 6.f, 1e-3f);
    // Bad pins are refused.
    a = h.command("pin", {"level", ""});
    CHECK(!a.toasts.empty() && a.toasts.front().kind == ToastKind::Warning);
    a = h.command("pin", {"final", "-2"});
    CHECK(a.toasts.front().kind == ToastKind::Warning);
    a = h.command("pin", {"color", "x"});
    CHECK(a.toasts.front().kind == ToastKind::Warning);
    // Unpin everything: back to detection.
    h.command("unpin", {});
    st = h.mod.status();
    CHECK(st.pinStart.empty() && st.pinLevel.empty() && st.pinFinal == 0);
    h.step(9.f, {kBool("Go", false), kInt("Stage", 1)});
    CHECK(h.mod.status().profileSource != ProfileSource::Pins);
    // Single-role unpin with an explicit map id.
    h.command("pin", {"timer", "clock", "other_map"});
    MapStatsView v;
    CHECK(h.mod.statsFor("other_map", v));
    h.command("unpin", {"timer", "other_map"});
}

TEST(maps_commands_reset_pb_forget_restart) {
    Harness h;
    enterGiant(h);
    lethRun(h, 1.f, 2.f);
    MapStatsView v;
    h.mod.statsFor(kGiant, v);
    CHECK(v.hasBest);
    h.command("reset_pb", {});
    h.mod.statsFor(kGiant, v);
    CHECK(!v.hasBest);
    CHECK(!h.mod.status().hasPb);
    h.command("fav", {});
    h.command("forget", {});
    h.mod.statsFor(kGiant, v);
    CHECK(v.finishes == 0 && v.attempts == 0 && !v.favorite);  // fresh entry for the current map
    // Restart aborts an own-timed run and re-arms.
    Harness o;
    o.setContext(GameMode::Workshop, true, "Some_Map");
    o.tick(0.1f, {}, {});
    o.tick(0.2f, {}, {800.f, 0.f, 0.f});
    o.tick(5.f, {}, {800.f, 0.f, 0.f});
    o.command("restart");
    CHECK(o.mod.status().phase == RunPhase::Armed);
    MapStatsView ov;
    o.mod.statsFor("some_map", ov);
    CHECK(ov.attempts == 1);
    Actions help = o.command("help");
    CHECK(!help.chat.empty());
    o.command("unknown_command", {"x"});  // ignored
}

TEST(maps_activity_gating_never_controls_the_world) {
    Harness h;
    h.host.allow = true;
    h.settings.general.controlInWorkshop = true;
    h.applySettings();
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.game.worldControlAllowed = true;
    for (int i = 0; i < 50; ++i) h.tick(static_cast<float>(i) * 0.1f, {}, {i > 10 ? 900.f : 0.f, 0.f, 0.f});
    h.event(GameEventKind::Goal);
    h.event(GameEventKind::ResetShot);
    h.kismet(lethVars(true, 2));
    h.command("restart");
    h.mod.onActivityStopped();
    CHECK(h.host.requests == 0 && h.host.releases == 0);
    CHECK(!h.all.setBall && !h.all.setCar && !h.all.setBoost && !h.all.overrideInputs && !h.all.freezeCar);
    CHECK(!h.all.setGameSpeed && !h.all.countdownMessage && !h.all.goMessage);
}

TEST(maps_disabled_module_does_nothing) {
    Harness h;
    h.settings.maps.enabled = false;
    h.applySettings();
    enterGiant(h);
    lethRun(h, 1.f, 2.f);
    CHECK(!h.mod.status().onCustomMap);
    CHECK(h.all.results.empty());
    CHECK(h.draw().panels.empty());
    CHECK(h.mod.statsList().empty());
    // Re-enabled while on the map: picks the map back up.
    h.settings.maps.enabled = true;
    h.applySettings();
    CHECK(h.mod.status().onCustomMap);
    CHECK(h.mod.status().key == kGiant);
    // Disabled mid-run: the run is dropped.
    h.step(50.f, lethVars(false, 1));
    h.step(51.f, lethVars(true, 1));
    h.settings.maps.enabled = false;
    h.applySettings();
    CHECK(!h.mod.status().onCustomMap);
}

TEST(maps_save_load_roundtrip_through_module) {
    Store st;
    {
        Harness h;
        enterGiant(h);
        lethRun(h, 1.f, 2.f);
        h.command("fav", {});
        h.command("pin", {"final", "20", "other_map"});
        h.mod.save(st);
    }
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("maps.", 0) == 0, kv.first);
    Store parsed;
    parsed.parse(st.serialize());
    Harness h2;
    h2.mod.load(parsed);
    MapStatsView v;
    CHECK(h2.mod.statsFor(kGiant, v));
    CHECK(v.hasBest && std::fabs(v.best - 40.f) < 1e-3f && v.favorite);
    CHECK(h2.mod.statsFor("other_map", v));
    // Entering the map after load uses the stored record for split deltas.
    enterGiant(h2);
    CHECK(h2.mod.status().hasPb && h2.mod.status().pbSplits.size() == 19);
    h2.step(100.f, lethVars(false, 1));
    h2.step(100.1f, lethVars(true, 1));
    h2.step(101.1f, lethVars(true, 2));
    CHECK(h2.mod.status().splits.size() == 1 && h2.mod.status().splits[0].hasDelta);
    CHECK_NEAR(h2.mod.status().splits[0].delta, -1.f, 1e-3f);
    // Load while on a map keeps working.
    h2.mod.load(parsed);
    CHECK(h2.mod.status().onCustomMap);
}

TEST(maps_refine_context_for_unknown_and_other_maps) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Air_Dribble_Practice_P");
    GameContext g = h.game;
    h.mod.refineContext(g);
    CHECK(g.mapTitle == "Air Dribble Practice");
    CHECK(g.category == MapCategory::AirDribble);
    GameContext stock;
    stock.isCustomMap = false;
    h.mod.refineContext(stock);
    CHECK(stock.mapTitle.empty() && stock.category == MapCategory::Standard);
    // Another custom map known from stats with an override.
    h.command("category", {"shooting", "elsewhere"});
    GameContext other;
    other.isCustomMap = true;
    other.mapName = "Elsewhere";
    h.mod.refineContext(other);
    CHECK(other.category == MapCategory::Shooting);
}

TEST(maps_profile_drop_keeps_first_run_eligible_until_the_player_moves) {
    // Known id but renamed variables, discovered before the player moved: the own-timed run can be a record.
    Harness h;
    enterGiant(h);
    h.tick(0.5f, {}, {});
    h.step(1.3f, {kInt("Stage", 1)});
    CHECK(h.mod.status().profileSource == ProfileSource::None);
    h.tick(2.f, {}, {});
    h.tick(2.1f, {}, {900.f, 0.f, 0.f});
    h.tick(9.1f, {}, {900.f, 0.f, 0.f});
    Actions g = h.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1 && g.results.front().hasRecordValue);
    // Same situation, but the player was already driving: the run cannot be a record.
    Harness m;
    enterGiant(m);
    m.tick(0.5f, {}, {});
    m.tick(0.6f, {}, {1200.f, 0.f, 0.f});
    m.step(1.3f, {kInt("Stage", 1)});
    m.tick(1.4f, {}, {1200.f, 0.f, 0.f});
    m.tick(2.0f, {}, {1200.f, 0.f, 0.f});
    m.tick(8.0f, {}, {1200.f, 0.f, 0.f});
    g = m.event(GameEventKind::Goal);
    CHECK(g.results.size() == 1 && !g.results.front().hasRecordValue);
}

TEST(maps_ticks_without_context_are_ignored) {
    Harness h;
    h.tick(1.f, {}, {});
    h.tick(1.1f, {}, {900.f, 0.f, 0.f});
    h.kismet(lethVars(true, 2));
    h.touch(true);
    h.event(GameEventKind::Goal);
    h.event(GameEventKind::ResetShot);
    h.event(GameEventKind::MapUnloaded);
    CHECK(h.mod.statsList().empty());
    CHECK(h.all.empty());
    CHECK(!h.mod.status().onCustomMap);
}

TEST(maps_tiny_runs_are_not_shown_as_runs) {
    Harness h;
    h.setContext(GameMode::Workshop, true, "Some_Map");
    h.tick(0.1f, {}, {});
    h.tick(0.2f, {}, {900.f, 0.f, 0.f});
    const Actions g = h.event(GameEventKind::ResetShot);  // 0 s run: a nudge
    CHECK(g.results.empty());
    CHECK(!h.mod.status().last.valid);
    DrawList d = h.draw();
    CHECK(!containsText(d.panels.front(), "Dernier run"));
    CHECK(!containsText(d.panels.front(), "Run arrete"));
}
