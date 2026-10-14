// Pedagogy and compact HUD of the first-touch module: drill levels a beginner can pass (niveau 1 judges the touch
// alone), demanding top levels, one tip in words per rep, at most three main HUD lines without milliseconds or
// degrees, no heatmap and no duplicated stats panel in compact mode, difficulty tags and plain step lists.

#include <cmath>

#include "tests/firsttouch/ft_test_util.h"

using namespace rlt;
using namespace rlt::ft;
using namespace fttest;

namespace {

bool noEnDash(const std::string& s) { return s.find("\xE2\x80\x93") == std::string::npos; }

bool plainHudText(const std::string& s) {
    return !contains(s, " ms") && !contains(s, "°") && !contains(s, "deg") && noEmDash(s) && noEnDash(s);
}

// Every main line of every panel: at most 3 per panel, no measures in ms or degrees.
void checkCompactPanels(const DrawList& dl) {
    for (const auto& p : dl.panels) {
        const auto main = mainLines(p);
        CHECK_MSG(main.size() <= 3, p.title);
        for (const auto& t : main) CHECK_MSG(plainHudText(t), t);
        CHECK_MSG(plainHudText(p.title), p.title);
    }
}

bool no2DExtras(const DrawList& dl) {
    return dl.texts2D.empty() && dl.rects2D.empty() && dl.lines2D.empty() && dl.triangles2D.empty();
}

Snapshot restSnap(int64_t frame) {
    return makeSnap(frame, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 2000.f, kBallZ}, Vec3{}));
}

// Starts `type` at frame 100 (placement at 101) with the given adaptive level.
void startAt(Harness& h, DrillType type, float level) {
    setDrillLevel(h.mod, type, level);
    h.set("rlt_ft_drill_seed", "4321");
    h.tick(restSnap(100));
    h.command("drill", {drillId(type)});
    Snapshot s = restSnap(101);
    h.tick(s);
    passReady(h, s);  // get-ready hold of the first ball
}

// Typical beginner touch on a ball rolling away: arrives 350 uu/s (about 13 km/h) too fast and pushes the ball
// away (it leaves 500 uu/s faster than the car), so the ball is lost right after the touch.
Scenario beginnerTouch(int64_t frame) { return groundPickup(1250.f, 900.f, 1000.f, 1500.f, frame); }

// Decent touch of an average player: arrives 400 uu/s too fast, then keeps the ball close most of the window.
Scenario decentTouch(int64_t frame) { return groundPickup(1300.f, 900.f, 1000.f, 1250.f, frame); }

} // namespace

// ---- Levels and criteria ----------------------------------------------------------------------------------

TEST(ft_player_levels_go_from_1_to_10) {
    CHECK(playerLevel(0.f) == 1);
    CHECK(playerLevel(1.f) == 10);
    CHECK(playerLevel(-3.f) == 1 && playerLevel(7.f) == 10);
    for (int n = 1; n <= 10; ++n) CHECK(playerLevel(levelOfPlayer(n)) == n);
    CHECK_NEAR(levelOfPlayer(1), 0.f, 1e-6f);
    CHECK_NEAR(levelOfPlayer(10), 1.f, 1e-6f);
    CHECK(playerLevel(kDrillStartLevel) == 1);  // a new player starts at niveau 1
    FirstTouchModule m;
    for (int i = 0; i < kDrillKinds; ++i) CHECK_NEAR(m.drillLevel(static_cast<DrillType>(i)).level, kDrillStartLevel, 1e-6f);
}

TEST(ft_unplayed_old_save_starts_at_niveau_1) {
    // The previous version saved every drill at its old start level (0.2) even when never played.
    Store st;
    st.setFloat("firsttouch.drill.roll_in.level", 0.2f);
    st.setInt("firsttouch.drill.roll_in.attempts", 0);
    st.setFloat("firsttouch.drill.lob.level", 0.5f);
    st.setInt("firsttouch.drill.lob.attempts", 4);
    FirstTouchModule m;
    m.load(st);
    CHECK(playerLevel(m.drillLevel(DrillType::RollIn).level) == 1);
    CHECK_NEAR(m.drillLevel(DrillType::Lob).level, 0.5f, 1e-6f);  // a played level is kept
}

TEST(ft_drill_criteria_are_wide_at_niveau_1_and_demanding_at_niveau_10) {
    const DrillCriteria low = drillCriteria(0.f, 60.f, 0.6f, 6.f);
    CHECK(low.firstStep);
    CHECK_NEAR(low.minScore, 50.f, 1e-4f);
    CHECK_NEAR(low.minControl, 0.f, 1e-6f);  // control not judged
    CHECK_NEAR(low.timeout, 9.f, 1e-4f);     // 1.5 x the time
    const DrillCriteria high = drillCriteria(1.f, 60.f, 0.6f, 6.f);
    CHECK(!high.firstStep);
    CHECK_NEAR(high.minScore, 75.f, 1e-4f);
    CHECK_NEAR(high.minControl, 0.7f, 1e-4f);
    CHECK_NEAR(high.timeout, 6.f, 1e-4f);
    // The first step lasts until niveau 3, the control counts from niveau 4.
    CHECK(drillCriteria(levelOfPlayer(kControlLevel - 1), 60.f, 0.6f, 6.f).firstStep);
    const DrillCriteria four = drillCriteria(levelOfPlayer(kControlLevel), 60.f, 0.6f, 6.f);
    CHECK(!four.firstStep && four.minControl > 0.3f && four.minControl < 0.6f);
    // Monotonic with the level.
    float prevScore = -1.f, prevTime = 1e9f;
    for (int n = 1; n <= 10; ++n) {
        const DrillCriteria c = drillCriteria(levelOfPlayer(n), 60.f, 0.6f, 6.f);
        CHECK(c.minScore >= prevScore);
        CHECK(c.timeout <= prevTime);
        prevScore = c.minScore;
        prevTime = c.timeout;
    }
    // Follows the player's thresholds.
    CHECK_NEAR(drillCriteria(1.f, 80.f, 0.8f, 10.f).minScore, 95.f, 1e-4f);
    CHECK_NEAR(drillCriteria(1.f, 80.f, 0.8f, 10.f).minControl, 0.9f, 1e-4f);
    CHECK_NEAR(drillCriteria(1.f, 95.f, 1.f, 6.f).minScore, 100.f, 1e-4f);
}

TEST(ft_touch_only_score_ignores_the_control_part) {
    const ScoreParams sp;
    TouchScore s;
    s.sSpeed = 1.f;
    s.sContact = 1.f;
    s.sControl = 0.f;
    s.score = 40.f;  // ball lost: capped
    CHECK_NEAR(touchOnlyScore(s, sp), 100.f, 1e-4f);
    s.sSpeed = 0.f;  // a ram with a perfect contact point stays under the niveau 1 threshold
    CHECK(touchOnlyScore(s, sp) < drillCriteria(0.f, 60.f, 0.6f, 6.f).minScore);
    const DrillCriteria first = drillCriteria(0.f, 60.f, 0.6f, 6.f);
    const DrillCriteria full = drillCriteria(1.f, 60.f, 0.6f, 6.f);
    s.sSpeed = 0.6f;
    CHECK_NEAR(drillRepScore(first, s, sp), touchOnlyScore(s, sp), 1e-6f);
    CHECK_NEAR(drillRepScore(full, s, sp), 40.f, 1e-6f);
    CHECK(drillRepSuccess(first, 55.f, 0.f));
    CHECK(!drillRepSuccess(full, 80.f, 0.5f));
    CHECK(drillRepSuccess(full, 80.f, 0.75f));
}

TEST(ft_first_step_tip_is_about_the_touch_not_the_control) {
    const std::vector<Tip> tips = {{"ft.lost_control", "Reste collé à la balle après la touche.", 30.f},
                                   {"ft.too_fast", "Tu arrives trop vite : relâche Boost (B) plus tôt.", 20.f}};
    CHECK(drillRepTip(drillCriteria(0.f, 60.f, 0.6f, 6.f), tips) == tips[1].text);
    CHECK(drillRepTip(drillCriteria(1.f, 60.f, 0.6f, 6.f), tips) == tips[0].text);
    CHECK(drillRepTip(drillCriteria(0.f, 60.f, 0.6f, 6.f), {}).empty());
}

// The beginner touch fails the normal grading, passes niveau 1 (first step), and fails niveau 10.
TEST(ft_beginner_touch_passes_niveau_1_and_fails_niveau_10) {
    {
        Harness free;  // outside a drill: the player's thresholds
        const Actions a = runScenario(free, beginnerTouch(1000), 130);
        CHECK(a.results.size() == 1);
        if (!a.results.empty()) CHECK(!a.results.front().success);
        CHECK(!free.mod.lastTouch().controlled);
    }
    {
        Harness h;
        startAt(h, DrillType::RollAway, kDrillStartLevel);
        CHECK(h.mod.drillStatus().criteria.firstStep);
        const Actions a = runScenario(h, beginnerTouch(afterGo(h)), 125);
        CHECK(a.results.size() == 1);
        if (!a.results.empty()) CHECK(a.results.front().success);  // the coach sees the same verdict
        const auto st = h.mod.drillStatus();
        CHECK_MSG(st.lastSuccess, st.lastOutcome);
        CHECK(contains(st.lastOutcome, "Réussi"));
        CHECK(st.lastTip.empty());
        CHECK(h.mod.lastTouch().success);
        CHECK(h.mod.drillLevel(DrillType::RollAway).level > kDrillStartLevel);  // the level goes up
    }
    {
        Harness h;
        startAt(h, DrillType::RollAway, 1.f);
        CHECK(!h.mod.drillStatus().criteria.firstStep);
        const Actions a = runScenario(h, beginnerTouch(afterGo(h)), 125);
        CHECK(a.results.size() == 1);
        if (!a.results.empty()) CHECK(!a.results.front().success);
        const auto st = h.mod.drillStatus();
        CHECK(!st.lastSuccess);
        CHECK(contains(st.lastOutcome, "Raté"));
        CHECK(!st.lastTip.empty());
        CHECK(h.mod.drillLevel(DrillType::RollAway).level < 1.f);
    }
}

// Niveau 10 stays demanding: a decent touch that passes the normal grading is not enough there.
TEST(ft_top_level_stays_demanding) {
    Harness free;
    const Actions a = runScenario(free, decentTouch(1000), 130);
    CHECK(a.results.size() == 1);
    if (!a.results.empty()) CHECK_MSG(a.results.front().success, std::to_string(a.results.front().score));
    const auto& last = free.mod.lastTouch();
    CHECK(!drillRepSuccess(drillCriteria(1.f, 60.f, 0.6f, 6.f), last.score, last.controlFraction));

    Harness h;
    startAt(h, DrillType::RollAway, 1.f);
    runScenario(h, decentTouch(afterGo(h)), 125);
    CHECK(!h.mod.drillStatus().lastSuccess);
    // The clean touch still passes it.
    Harness g;
    startAt(g, DrillType::RollAway, 1.f);
    runScenario(g, groundPickup(1000.f, 950.f, 1000.f, 1040.f, afterGo(g)), 125);
    CHECK(g.mod.drillStatus().lastSuccess);
}

TEST(ft_niveau_1_gives_more_time) {
    Harness h;
    startAt(h, DrillType::RollIn, kDrillStartLevel);
    CHECK_NEAR(h.mod.drillStatus().timeLeft, 9.f, 0.02f);
    Harness g;
    startAt(g, DrillType::RollIn, 1.f);
    CHECK_NEAR(g.mod.drillStatus().timeLeft, 6.f, 0.02f);
    CHECK(g.mod.drillStatus().playerLevel == 10);
}

// ---- Compact HUD ------------------------------------------------------------------------------------------

TEST(ft_compact_stats_panel_keeps_result_and_one_tip) {
    Harness h;
    h.compact = true;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 1000));
    runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 3000));  // rammed: tip shown
    const DrawList dl = h.draw();
    CHECK(dl.panels.size() == 1);
    if (dl.panels.empty()) return;
    const auto main = mainLines(dl.panels.front());
    CHECK(main.size() == 2);
    if (main.size() == 2) {
        CHECK(contains(main[0], "/100"));
        CHECK(contains(main[1], "trop vite"));
    }
    checkCompactPanels(dl);
    CHECK(no2DExtras(dl));     // no heatmap in compact mode
    CHECK(!dl.lines3D.empty());  // the contact marker on the ball stays
    // The secondary lines are still there for the compact HUD off.
    CHECK(dl.panels.front().lines.size() >= 6);

    h.compact = false;
    const DrawList full = h.draw();
    CHECK(!full.rects2D.empty() && !full.lines2D.empty());  // heatmap back
}

TEST(ft_compact_dribble_map_panel_starts_with_the_dribble) {
    Harness h;
    h.compact = true;
    h.game.category = MapCategory::Dribble;
    runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 1000));
    Snapshot s = advance(h.latest);
    for (int k = 0; k < 3; ++k) {
        TouchEvent t;
        t.frame = s.frame;
        t.pre = s;
        t.post1 = advance(s);
        t.post = advance(t.post1);
        t.newContact = true;
        t.ticksSincePrevTouch = 20;
        h.tick(t.post1);
        h.tickWithTouch(t);
        s = advance(t.post, 20);
        h.tick(s);
    }
    const DrawList dl = h.draw();
    CHECK(dl.panels.size() == 1);
    if (dl.panels.empty()) return;
    const auto main = mainLines(dl.panels.front());
    CHECK(main.size() <= 3);
    CHECK(!main.empty() && main.front().rfind("Dribble : ", 0) == 0);
    checkCompactPanels(dl);
}

TEST(ft_compact_drill_shows_only_the_drill_panel) {
    Harness h;
    runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 20));  // stats and a tip exist
    startAt(h, DrillType::RollAway, kDrillStartLevel);
    h.compact = false;
    CHECK(h.draw().panels.size() == 2);
    h.compact = true;
    DrawList dl = h.draw();
    CHECK(dl.panels.size() == 1);
    if (!dl.panels.empty()) CHECK(contains(dl.panels.front().title, "niveau 1/10"));
    checkCompactPanels(dl);
    CHECK(no2DExtras(dl));

    // Evaluate: one line telling what to do.
    runScenario(h, groundPickup(1700.f, 800.f, 1500.f, 2200.f, afterGo(h)), 60);
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Evaluate);
    dl = h.draw();
    checkCompactPanels(dl);
    CHECK(dl.panels.size() == 1 && mainLines(dl.panels.front()).size() == 1);

    // Pause after a miss: verdict with the score, the one tip, the countdown.
    for (int i = 0; i < 70; ++i) h.tick(advance(h.latest));
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Pause);
    dl = h.draw();
    checkCompactPanels(dl);
    CHECK(dl.panels.size() == 1);
    if (dl.panels.empty()) return;
    const auto main = mainLines(dl.panels.front());
    CHECK(main.size() == 3);
    if (main.size() == 3) {
        CHECK(main[0].rfind("Raté : ", 0) == 0 && contains(main[0], "/100"));
        CHECK(contains(main[1], "trop vite"));
        CHECK(main[2].rfind("Balle suivante dans ", 0) == 0);
    }

    // Next ball (after the pause and its 1 s hold): the tip stays as a reminder under the step and the timer.
    for (int i = 0; i < 400 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::WaitTouch; ++i)
        h.tick(advance(h.latest));
    dl = h.draw();
    checkCompactPanels(dl);
    if (!dl.panels.empty()) {
        const auto wait = mainLines(dl.panels.front());
        CHECK(wait.size() == 3);
        if (wait.size() == 3) {
            CHECK(contains(wait[1], "Temps : ") && contains(wait[1], "Réussis : 0/1"));
            CHECK(contains(wait[2], "trop vite"));
        }
    }
}

// ---- Catalogue --------------------------------------------------------------------------------------------

TEST(ft_drill_catalogue_is_tagged_and_ordered_basics_first) {
    CHECK(drillTier(DrillType::RollIn) == Tier::Beginner);
    CHECK(drillTier(DrillType::RollAway) == Tier::Beginner);
    CHECK(drillTier(DrillType::High) == Tier::Advanced);
    CHECK(std::string(tierLabel(Tier::Beginner)) == "Débutant");
    CHECK(std::string(tierLabel(Tier::Intermediate)) == "Intermédiaire");
    CHECK(std::string(tierLabel(Tier::Advanced)) == "Avancé");
    int prev = 0;
    for (int i = 0; i <= static_cast<int>(DrillType::Mix); ++i) {
        const auto t = static_cast<DrillType>(i);
        CHECK(static_cast<int>(drillTier(t)) >= prev);  // the tab lists the catalogue order: basics first
        prev = static_cast<int>(drillTier(t));
        const auto steps = drillSteps(t);
        CHECK(steps.size() >= 3 && steps.size() <= 5);
        for (const auto& st : steps) {
            CHECK_MSG(st.find_first_of("0123456789") == std::string::npos, st);
            CHECK_MSG(noEmDash(st) && noEnDash(st), st);
        }
        const std::string instr = drillInstruction(t, gamepadProfile(), KeyStyle::Auto);
        CHECK_MSG(instr.find_first_of("0123456789") == std::string::npos, instr);
        CHECK(noEnDash(drillName(t)) && noEnDash(drillDescription(t)) && noEnDash(instr));
    }
    const std::string advice = drillAdvice();
    CHECK(advice.rfind("Débutant : ", 0) == 0);
    CHECK(contains(advice, drillName(DrillType::RollIn)));
    CHECK(noEmDash(advice) && noEnDash(advice));
    CHECK(std::string(drillGoal(0.f)) != drillGoal(1.f));
}

TEST(ft_drill_start_toast_names_the_level_and_its_goal) {
    Harness h;
    h.tick(restSnap(100));
    const Actions a = h.command("drill", {"roll_in"});
    bool found = false;
    for (const auto& t : a.toasts)
        if (contains(t.text, "niveau 1/10") && contains(t.text, drillGoal(0.f))) found = true;
    CHECK(found);
}
