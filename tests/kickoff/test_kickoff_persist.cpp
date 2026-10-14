// Settings schema, persistence (stats, personal bests, adaptive level) and draw output sanity.
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

TEST(ko_schema_defaults_and_conventions) {
    SettingsSchema schema;
    KickoffModule::describeSettings(schema);
    Settings s;
    schema.applyDefaults(s);
    const KickoffConfig def;
    const auto defs = schema.forModule("kickoff");
    CHECK(defs.size() >= 25);
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_ko_", 0) == 0, d->cvar);
        CHECK_MSG(!d->label.empty() && !d->tooltip.empty(), d->cvar);
        CHECK_MSG(!d->section.empty(), d->cvar);
        CHECK_MSG(!hasEmDash(d->label) && !hasEmDash(d->tooltip) && !hasEmDash(d->section), d->cvar);
        for (const auto& c : d->choices) CHECK(!hasEmDash(c));
        Settings tmp;
        CHECK_MSG(schema.apply(tmp, d->cvar, d->defaultValue), d->cvar);  // default parses
        CHECK_MSG(d->read && d->read(s) == d->defaultValue, d->cvar + " read back " + (d->read ? d->read(s) : ""));
    }
    // Struct defaults and schema defaults agree.
    CHECK(s.kickoff.enabled == def.enabled);
    CHECK_EQ(s.kickoff.spawnMode, def.spawnMode);
    CHECK_EQ(s.kickoff.ghostLevel, def.ghostLevel);
    CHECK_EQ(s.kickoff.gradeTarget, def.gradeTarget);
    CHECK_EQ(s.kickoff.countdownSeconds, 3);
    CHECK(!s.kickoff.goJitter);
    CHECK(s.kickoff.ghostHitsBall);
    CHECK_NEAR(s.kickoff.resetDelay, 2.f, 1e-6);
    CHECK_NEAR(s.kickoff.timeout, 6.f, 1e-6);
    CHECK_NEAR(s.kickoff.goJitterMax, 0.4f, 1e-6);
    CHECK_NEAR(s.kickoff.hudX, 0.01f, 1e-6);
    CHECK_NEAR(s.kickoff.hudY, 0.12f, 1e-6);
    CHECK_NEAR(s.kickoff.spawnBoost, 33.3333f, 1e-3);
    CHECK_NEAR(s.kickoff.ballHeight, 93.15f, 1e-4);
    CHECK(s.kickoff.ghostColor == def.ghostColor);
    // Choice lists.
    const SettingDef* ghost = schema.find("rlt_ko_ghost");
    CHECK(ghost && ghost->choices.size() == static_cast<size_t>(kKoGhostModeCount));
    const SettingDef* spawn = schema.find("rlt_ko_spawn");
    CHECK(spawn && spawn->choices.size() == static_cast<size_t>(kKoSpawnCount));
    CHECK(spawn && spawn->choices[0] == "Diagonale gauche");
    // Advanced knobs are flagged.
    CHECK(schema.find("rlt_ko_duel_window")->advanced);
    CHECK(!schema.find("rlt_ko_go_jitter")->advanced);
    // Clamping of out-of-range values.
    CHECK(schema.apply(s, "rlt_ko_timeout", "99"));
    CHECK_NEAR(s.kickoff.timeout, 10.f, 1e-6);
    CHECK(schema.apply(s, "rlt_ko_ghost", "42"));
    CHECK_EQ(s.kickoff.ghostLevel, kKoGhostModeCount - 1);
    CHECK(!schema.apply(s, "rlt_ko_timeout", "abc"));
    // Sections in French, in order.
    const auto sections = schema.sections("kickoff");
    CHECK(sections.size() == 5 && sections[0] == "Kickoff" && sections[1] == "Exercice" && sections[2] == "Fantôme");
    // Names.
    for (int i = 0; i < kKoGhostModeCount; ++i)
        CHECK(std::string(kickoffGhostModeName(static_cast<KickoffGhostMode>(i))).size() > 2);
    for (int i = 0; i < 4; ++i) CHECK(std::string(kickoffModeName(static_cast<KickoffMode>(i))).size() > 2);
    for (int i = 0; i < kKoTypeCount; ++i) CHECK(!hasEmDash(kickoffTypeName(static_cast<KickoffType>(i))));
}

TEST(ko_stats_record_and_weakest) {
    KickoffStats st;
    KickoffMetrics m;
    m.spawn = KickoffSpawn::BackCenter;
    m.touched = true;
    m.timeToTouch = 2.6f;
    m.speedToBall = 2000.f;
    m.type = KickoffType::FrontFlip;
    for (int i = 0; i < 300; ++i) m.along.push_back(static_cast<float>(i) * 15.f);
    CHECK(st.record(m, 61, nullptr));  // first touch is a record
    m.timeToTouch = 2.7f;
    KickoffDuelResult neutral;
    neutral.outcome = KickoffDuel::Neutral;
    CHECK(!st.record(m, 50, &neutral));
    m.timeToTouch = 2.5f;
    KickoffDuelResult win;
    win.outcome = KickoffDuel::PlayerNarrow;
    CHECK(st.record(m, 75, &win));
    KickoffDuelResult loss;
    loss.outcome = KickoffDuel::GhostClear;
    KickoffMetrics miss = m;
    miss.touched = false;
    miss.type = KickoffType::Fake;
    CHECK(!st.record(miss, 5, &loss));
    const KickoffSpawnStats& s = st.of(KickoffSpawn::BackCenter);
    CHECK_EQ(s.attempts, 4);
    CHECK_EQ(s.touches, 3);
    CHECK_EQ(s.duels, 3);
    CHECK_EQ(s.wins, 1);
    CHECK_EQ(s.neutrals, 1);
    CHECK_EQ(s.losses, 1);
    CHECK_NEAR(s.bestTime, 2.5f, 1e-6);
    CHECK_NEAR(s.bestSpeed, 2000.f, 1e-3);
    CHECK_EQ(s.bestScore, 75);
    CHECK_NEAR(s.avgTime(), (2.6f + 2.7f + 2.5f) / 3.f, 1e-5);
    CHECK_NEAR(s.avgScore(), (61.f + 50.f + 75.f + 5.f) / 4.f, 1e-4);
    CHECK_NEAR(s.winRate(), 1.f / 3.f, 1e-5);
    CHECK(s.favouriteType() == KickoffType::FrontFlip);
    CHECK_EQ(s.pbProfile.size(), size_t(25));  // 0.0 .. 2.4 s every 0.1 s (< 2.5 s)
    CHECK_NEAR(s.pbProfile[10], 12.f * 10.f * 15.f, 1e-3);
    // Recent lists are bounded.
    for (int i = 0; i < 30; ++i) st.record(m, 40, nullptr);
    CHECK_EQ(st.of(KickoffSpawn::BackCenter).recentScores.size(), kKoRecentCount);
    CHECK_EQ(st.of(KickoffSpawn::BackCenter).recentTimes.size(), kKoRecentCount);
    // PB ghost profile.
    KickoffGhostProfile prof;
    CHECK(st.personalBestProfile(KickoffSpawn::BackCenter, prof));
    CHECK(prof.model == KickoffGhostModel::Samples);
    CHECK_NEAR(prof.arrive, 2.5f, 1e-6);
    CHECK(!st.personalBestProfile(KickoffSpawn::DiagonalLeft, prof));
    st.reset();
    CHECK_EQ(st.totalAttempts(), 0);
    // Downsampling helper.
    const std::vector<float> ds = kickoffDownsample({0.f, 1.f, 2.f, 3.f}, 10.f);
    CHECK(!ds.empty() && ds.size() <= kKoPbMaxSamples);
    CHECK(kickoffDownsample({}, 2.f).empty());
}

TEST(ko_save_load_roundtrip_with_pbs) {
    Harness h;
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"diagonal_left"});
    const int64_t go = h.runCountdown(f);
    Script s;
    s.sideDodge = 0.55f;
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, s, go, 4.f), f);
    const KickoffSpawnStats before = h.mod.stats().of(KickoffSpawn::DiagonalLeft);
    CHECK(before.bestTime > 0.f && !before.pbProfile.empty());
    Store store;
    h.mod.save(store);
    for (const auto& kv : store.raw()) CHECK_MSG(kv.first.rfind("kickoff.", 0) == 0, kv.first);
    CHECK(store.raw().size() < 120);  // bounded
    // Through the text format too.
    Store parsed;
    parsed.parse(store.serialize());
    KickoffModule other;
    other.load(parsed);
    const KickoffSpawnStats after = other.stats().of(KickoffSpawn::DiagonalLeft);
    CHECK_EQ(after.attempts, before.attempts);
    CHECK_EQ(after.touches, before.touches);
    CHECK_EQ(after.duels, before.duels);
    CHECK_EQ(after.wins, before.wins);
    CHECK_NEAR(after.bestTime, before.bestTime, 1e-4);
    CHECK_NEAR(after.bestSpeed, before.bestSpeed, 0.01);
    CHECK_EQ(after.bestScore, before.bestScore);
    CHECK_EQ(after.recentScores.size(), before.recentScores.size());
    CHECK_EQ(after.pbProfile.size(), before.pbProfile.size());
    for (size_t i = 0; i < after.pbProfile.size(); ++i) CHECK_NEAR(after.pbProfile[i], before.pbProfile[i], 0.06);
    CHECK(after.typeCounts == before.typeCounts);
    CHECK_NEAR(other.adaptiveLevel(), h.mod.adaptiveLevel(), 1e-5);
    // The loaded PB drives the "Mon record" ghost.
    KickoffGhostProfile prof;
    CHECK(other.stats().personalBestProfile(KickoffSpawn::DiagonalLeft, prof));
    const KickoffGhost g(KickoffSpawn::DiagonalLeft, KickoffTeam::Orange, prof);
    CHECK_NEAR(g.arrivalTime(), before.bestTime, 1e-4);
    // Corrupt values are bounded.
    Store bad;
    bad.setString("kickoff.back.recent_scores", "500 -20 nan 70 x 12");
    bad.setFloat("kickoff.back.best_time", -3.f);
    bad.setInt("kickoff.back.attempts", -9);
    bad.setString("kickoff.back.pb_profile", std::string(5000, '1'));
    KickoffModule m;
    m.load(bad);
    const KickoffSpawnStats& b = m.stats().of(KickoffSpawn::BackCenter);
    CHECK_EQ(b.attempts, 0);
    CHECK(b.bestTime == 0.f);
    for (int v : b.recentScores) CHECK(v >= 0 && v <= 100);
    CHECK(b.pbProfile.size() <= kKoPbMaxSamples);
    // Empty store: defaults.
    KickoffModule empty;
    empty.load(Store{});
    CHECK_EQ(empty.stats().totalAttempts(), 0);
}

// A player of the previous version has the old defaults (Pro ghost, Pro grading) saved in config.cfg: the first
// tick moves them once to Adaptive / Standard. A deliberate other choice is kept, and it never runs twice.
TEST(ko_old_defaults_migrated_once) {
    Harness h;
    h.mod.load(Store{});  // save of the previous version: no flag
    h.set("rlt_ko_ghost", "3");
    h.set("rlt_ko_grade_target", "2");
    int64_t f = 0;
    Actions first = h.tick(makeSnap(++f));
    CHECK_EQ(first.setCvars.size(), size_t(2));
    bool ghost = false, grade = false;
    for (const auto& c : first.setCvars) {
        ghost = ghost || (c.first == "rlt_ko_ghost" && c.second == "6");
        grade = grade || (c.first == "rlt_ko_grade_target" && c.second == "1");
    }
    CHECK(ghost && grade);
    CHECK(first.requestSave);
    CHECK_EQ(first.toasts.size(), size_t(1));
    CHECK(!first.toasts.empty() && !hasEmDash(first.toasts[0].text));
    Actions second = h.tick(makeSnap(++f));
    CHECK(second.setCvars.empty() && second.toasts.empty());
    // The flag is saved: a reload does not migrate again, even with the old pair.
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("kickoff.defaults_v2", 0), 1);
    Harness again;
    again.mod.load(st);
    again.set("rlt_ko_ghost", "3");
    again.set("rlt_ko_grade_target", "2");
    CHECK(again.tick(makeSnap(1)).setCvars.empty());
    // A player who chose something else (Pro ghost, Standard grading) keeps it; only the flag is set.
    Harness chosen;
    chosen.mod.load(Store{});
    chosen.set("rlt_ko_ghost", "3");
    Actions kept = chosen.tick(makeSnap(1));
    CHECK(kept.setCvars.empty() && kept.toasts.empty() && kept.requestSave);
}

TEST(ko_draw_output_sanity) {
    Harness h;
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    view.ballValid = true;
    view.ballVisualPos = {0.f, 0.f, 93.f};
    // Idle: nothing drawn.
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, false, false, 1.f};
        h.mod.draw(view, dc, dl);
        CHECK(dl.empty());
    }
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"offcenter_left"});
    for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::OffCenterLeft, KickoffTeam::Blue));
    // Countdown with the defaults: the game's countdown is the only one (the panel does not count, no big number)
    // and no line is drawn, only the ghost box.
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        CHECK_EQ(dl.panels.size(), size_t(1));
        if (!dl.panels.empty()) {
            CHECK(dl.panels[0].title.find("Kickoff") != std::string::npos);
            CHECK_NEAR(dl.panels[0].pos.x, 0.01f * 1920.f, 1e-3);
            CHECK_NEAR(dl.panels[0].pos.y, 0.12f * 1080.f, 1e-3);
            bool countdown = false;
            for (const auto& l : dl.panels[0].lines) {
                countdown = countdown || l.text.find("Decompte") != std::string::npos;
                CHECK(!hasEmDash(l.text));
                for (unsigned char c : l.text) CHECK(c < 128);  // folded to ASCII (accents off)
            }
            CHECK(!countdown);
        }
        CHECK(dl.texts2D.empty());
        CHECK_EQ(dl.lines3D.size(), size_t(12));  // ghost box
        bool occluded = false;
        for (const auto& l : dl.lines3D) occluded = occluded || l.occluder.enabled;
        CHECK(occluded);
    }
    // Options: the big number (the panel still does not count) and the lines to the ball.
    h.set("rlt_ko_big_countdown", "1");
    h.set("rlt_ko_show_path", "1");
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        bool countdown = false;
        for (const auto& l : dl.panels[0].lines) countdown = countdown || l.text.find("Decompte") != std::string::npos;
        CHECK(!countdown);
        bool big = false;
        for (const auto& t : dl.texts2D) big = big || (t.text == "3" && t.align == Align::Center && t.scale >= 4.f);
        CHECK(big);
        CHECK(dl.lines3D.size() >= 12 + 1 + 3 + 16 + 1);  // ghost box, path, split ticks, contact circle, ghost path
        CHECK(dl.lines3D.size() < 1500);
    }
    // Game countdown and big number both off: the panel is the only countdown, as a main line.
    h.set("rlt_ko_native_countdown", "0");
    h.set("rlt_ko_big_countdown", "0");
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        bool countdown = false;
        for (const auto& l : dl.panels[0].lines)
            countdown = countdown || (l.text.find("Decompte : 3") != std::string::npos && !l.detail);
        CHECK(countdown);
        CHECK(dl.texts2D.empty());
    }
    h.set("rlt_ko_native_countdown", "1");
    h.set("rlt_ko_big_countdown", "1");
    // Live (big countdown on): GO text, chrono; then the result card after the touch.
    const int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::OffCenterLeft, KickoffTeam::Blue, Script{}, go, 4.f);
    h.tick(sim.snaps[0]);
    h.tick(sim.snaps[1]);
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        bool goText = false;
        for (const auto& t : dl.texts2D) goText = goText || t.text == "GO !";
        CHECK(goText);
        bool chrono = false;
        for (const auto& l : dl.panels[0].lines) chrono = chrono || l.text.find("Chrono") != std::string::npos;
        CHECK(chrono);
    }
    f = sim.snaps[1].frame;
    for (size_t i = 2; i < sim.snaps.size(); ++i) {
        h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
        if (h.mod.phase() == KickoffPhase::Done) break;
    }
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.5f};
        h.mod.draw(view, dc, dl);
        CHECK_EQ(dl.panels.size(), size_t(1));
        bool note = false, split = false;
        for (const auto& l : dl.panels[0].lines) {
            note = note || l.text.find("/100") != std::string::npos;
            split = split || l.text.find("25 %") != std::string::npos;
        }
        CHECK(note && split);
        CHECK_NEAR(dl.panels[0].scale, 1.5f, 1e-6);
        CHECK(dl.lines3D.size() < 1500);
    }
    // HUD off and replay view (invalid view, no snapshot): no crash, no panel; 3D skipped without a valid view.
    h.set("rlt_ko_hud", "0");
    {
        DrawList dl;
        ViewContext replay;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, false, false, 1.f};
        h.mod.draw(replay, dc, dl);
        CHECK(dl.panels.empty() && dl.lines3D.empty());
    }
    h.set("rlt_ko_hud", "1");
    h.set("rlt_ko_enabled", "0");
    {
        DrawList dl;
        DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
        h.mod.draw(view, dc, dl);
        CHECK(dl.empty());
    }
}

TEST(ko_status_view) {
    Harness h;
    KickoffStatusView v = h.mod.status();
    CHECK(!v.running && v.phase == KickoffPhase::Idle && v.ghost == KickoffGhostMode::Adaptive);
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"sequence"});
    h.tick(makeSnap(++f));
    for (int i = 0; i < 130; ++i) h.tick(restingSnap(++f, h.mod.currentSpawn(), KickoffTeam::Blue));
    v = h.mod.status();
    CHECK(v.running && v.phase == KickoffPhase::Countdown);
    CHECK_EQ(v.countdown, 2);
    CHECK(v.mode == KickoffMode::Sequence && v.sequenceTotal == 5 && v.sequenceIndex == 0);
    CHECK(v.spawn == KickoffSpawn::DiagonalLeft);
    CHECK_NEAR(v.ghostArrive, 2.40f, 1e-4);  // adaptive ghost of a new player: the beginner time
    const auto rows = h.mod.spawnStats();
    CHECK_EQ(rows.size(), size_t(kKoSpawnCount));
}
