// Module level: touch -> control window -> AttemptResult, dribble contacts, cancellation paths, chat log and
// flip reset de-duplication, session stats, settings schema, persistence and draw output.

#include <cmath>
#include <set>

#include "tests/firsttouch/ft_test_util.h"

using namespace rlt;
using namespace rlt::ft;
using namespace fttest;

namespace {

// Ticks `pre`, then a touch built from it (post velocities given), returns the post snapshot.
Snapshot touchFrom(Harness& h, const Snapshot& pre, bool newContact, int ticksSince, Vec3 postCarVel, Vec3 postBallVel) {
    TouchEvent t;
    t.frame = pre.frame;
    t.pre = pre;
    Snapshot p1 = advance(pre);
    p1.car.vel = postCarVel;
    p1.ball.vel = postBallVel;
    t.post1 = p1;
    t.post = advance(p1);
    t.newContact = newContact;
    t.ticksSincePrevTouch = ticksSince;
    h.tick(pre);
    h.tick(t.post1);
    h.tickWithTouch(t);
    return t.post;
}

Snapshot tickLinear(Harness& h, Snapshot s, int ticks, Actions* acc = nullptr) {
    for (int i = 0; i < ticks; ++i) {
        s = advance(s);
        Actions a = h.tick(s);
        if (acc) acc->merge(std::move(a));
    }
    return s;
}

const Metric* findMetric(const AttemptResult& r, const std::string& key) {
    for (const auto& m : r.metrics)
        if (m.key == key) return &m;
    return nullptr;
}

// Ball dropping onto the roof of a car driving at 1000 uu/s: a catch ("rebond").
Scenario roofCatch(int64_t frame) {
    Scenario sc;
    sc.frame = frame;
    sc.car = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{1000.f, 0.f, 0.f});
    sc.ball = makeBall(Vec3{13.88f, 0.f, 148.f}, Vec3{1000.f, 0.f, -300.f});
    sc.postCarVel = Vec3{1000.f, 0.f, 0.f};
    sc.postBallVel = Vec3{1000.f, 0.f, 20.f};
    return sc;
}

} // namespace

// ---- Identity and settings --------------------------------------------------------------------------------

TEST(ft_module_identity) {
    FirstTouchModule m;
    CHECK(std::string(m.id()) == "firsttouch");
    CHECK(std::string(m.title()) == "Première touche");
    CHECK(!m.drillStatus().active);
    CHECK(!m.lastTouch().valid);
}

TEST(ft_schema_defaults_match_config_and_are_french) {
    SettingsSchema schema;
    FirstTouchModule::describeSettings(schema);
    Settings fromSchema;
    schema.applyDefaults(fromSchema);
    const Settings fromStruct;
    CHECK(schema.all().size() >= 40);
    std::set<std::string> cvars;
    for (const auto& d : schema.all()) {
        CHECK(d.module == "firsttouch");
        CHECK_MSG(d.cvar.rfind("rlt_ft_", 0) == 0, d.cvar);
        CHECK_MSG(!d.label.empty() && !d.tooltip.empty(), d.cvar);
        CHECK_MSG(noEmDash(d.label) && noEmDash(d.tooltip) && noEmDash(d.section), d.cvar);
        Settings tmp;
        CHECK_MSG(d.apply && d.apply(tmp, d.defaultValue), d.cvar);
        CHECK_MSG(d.read(fromSchema) == d.read(fromStruct), d.cvar + " default differs from FirstTouchConfig");
        cvars.insert(d.cvar);
        if (d.section == "Note (expert)" || d.section == "Exercices (expert)") CHECK_MSG(d.advanced, d.cvar);
    }
    CHECK(cvars.size() == schema.all().size());
    CHECK(schema.find("rlt_ft_enabled") != nullptr);
    CHECK(schema.find("rlt_ft_chat")->defaultValue == "0");
    CHECK(schema.find("rlt_ft_chat_flip")->defaultValue == "1");
    CHECK_NEAR(fromSchema.firstTouch.hudX, 0.99f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.hudY, 0.70f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.drillHudX, 0.01f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.drillHudY, 0.12f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.controlWindow, 1.0f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.controlRadius, 350.f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.successScore, 60.f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.drillTimeout, 6.f, 1e-6f);
    CHECK_NEAR(fromSchema.firstTouch.drillPause, 1.5f, 1e-6f);
    const auto sections = schema.sections("firsttouch");
    CHECK(sections.size() == 6);
    CHECK(sections.front() == "Analyse des touches");
}

TEST(ft_settings_are_clamped) {
    SettingsSchema schema;
    FirstTouchModule::describeSettings(schema);
    Settings s;
    CHECK(schema.apply(s, "rlt_ft_unit", "7"));
    CHECK(s.firstTouch.unit == 3);
    CHECK(schema.apply(s, "rlt_ft_control_window", "10"));
    CHECK_NEAR(s.firstTouch.controlWindow, 3.f, 1e-6f);
    CHECK(!schema.apply(s, "rlt_ft_control_window", "abc"));
    CHECK(schema.apply(s, "rlt_ft_color_good", "#102030FF"));
    CHECK(s.firstTouch.goodColor == Color(16, 32, 48, 255));
}

// ---- Graded touches ---------------------------------------------------------------------------------------

TEST(ft_good_ground_touch_produces_one_graded_result) {
    Harness h;
    const Actions run = runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f));
    CHECK(run.results.size() == 1);
    if (run.results.empty()) return;
    const AttemptResult& r = run.results.front();
    CHECK(r.module == "firsttouch");
    CHECK(r.skill == "first_touch.ground");
    CHECK(r.title == "Première touche (sol)");
    CHECK(r.success);
    CHECK(r.score >= 90.f);
    CHECK(r.verdict == Verdict::Perfect);
    CHECK(r.hasRecordValue && !r.lowerIsBetter);
    CHECK_NEAR(r.valueForRecord, r.score, 1e-6f);
    CHECK_NEAR(r.time, 1000.f / 120.f, 1e-4f);
    CHECK(r.tips.empty());
    const Metric* diff = findMetric(r, "speed_diff");
    CHECK(diff != nullptr && std::fabs(diff->value - 50.f) < 0.01f);
    CHECK(diff && diff->unit == "uu/s" && diff->label == "Écart de vitesse");
    const Metric* control = findMetric(r, "control");
    CHECK(control != nullptr && std::fabs(control->value - 100.f) < 1e-3f);
    CHECK(findMetric(r, "contact_height") && findMetric(r, "separation") && findMetric(r, "ball_pop"));
    CHECK(findMetric(r, "drill_level") == nullptr);
    for (const auto& m : r.metrics) CHECK(noEmDash(m.label));

    const auto& last = h.mod.lastTouch();
    CHECK(last.valid);
    CHECK(last.type == TouchType::Ground);
    CHECK(last.part == CarPart::Nose);
    CHECK(last.height == ContactHeight::Low);
    CHECK(last.zone == SpeedZone::Good);
    CHECK(last.controlled);
    const auto& st = h.mod.sessionStats();
    CHECK(st.graded == 1 && st.successes == 1 && st.goodZone == 1 && st.streak == 1);
    CHECK(st.typeCount[static_cast<size_t>(TouchType::Ground)] == 1);
    CHECK(h.mod.totalStats().graded == 1);
    CHECK(h.mod.recentScores().size() == 1);
    CHECK(h.mod.heatmap().size() == 1);
    CHECK(!h.mod.analysisPending());
}

TEST(ft_rammed_touch_is_graded_bad_with_tips) {
    Harness h;
    const Actions run = runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f));
    CHECK(run.results.size() == 1);
    if (run.results.empty()) return;
    const AttemptResult& r = run.results.front();
    CHECK(!r.success);
    CHECK(r.score <= 40.f);
    CHECK(r.verdict == Verdict::Fail);
    CHECK(hasTip(r.tips, "ft.too_fast"));
    CHECK(hasTip(r.tips, "ft.lost_control"));
    const auto& last = h.mod.lastTouch();
    CHECK(last.zone == SpeedZone::Bad);
    CHECK(!last.controlled);
    CHECK(h.mod.sessionStats().successes == 0);
    CHECK(h.mod.sessionStats().goodZone == 0);
}

TEST(ft_result_waits_for_end_of_control_window) {
    Harness h;
    Actions early = runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 60);
    CHECK(early.results.empty());
    CHECK(h.mod.analysisPending());
    Actions rest;
    tickLinear(h, h.latest, 70, &rest);
    CHECK(rest.results.size() == 1);
    // Longer window setting: no result after 1 s.
    Harness h2;
    h2.set("rlt_ft_control_window", "2.0");
    CHECK(runScenario(h2, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 130).results.empty());
    Actions more;
    tickLinear(h2, h2.latest, 120, &more);
    CHECK(more.results.size() == 1);
}

TEST(ft_dribble_contacts_counted_not_graded) {
    Harness h;
    h.game.category = MapCategory::Dribble;
    Actions acc = runScenario(h, roofCatch(1000), 0);
    Snapshot s = tickLinear(h, h.latest, 36);                          // up to frame 1038
    s = advance(s);                                                   // pre at 1039
    s = touchFrom(h, s, false, 2, Vec3{1000.f, 0.f, 0.f}, s.ball.vel);  // continued contact
    s = tickLinear(h, s, 30);
    s = advance(s);
    s = touchFrom(h, s, true, 40, Vec3{1000.f, 0.f, 0.f}, s.ball.vel);  // new contact, ball on the roof
    s = tickLinear(h, s, 30);
    s = advance(s);
    s = touchFrom(h, s, true, 34, Vec3{1000.f, 0.f, 0.f}, s.ball.vel);
    tickLinear(h, s, 60, &acc);
    CHECK(h.all.results.size() == 1);
    if (!h.all.results.empty()) CHECK(h.all.results.front().skill == "first_touch.bounce");
    CHECK(h.mod.sessionStats().dribbleContacts == 2);
    CHECK(h.mod.sessionStats().typeCount[static_cast<size_t>(TouchType::Dribble)] == 2);
    CHECK(h.mod.sessionStats().graded == 1);
    const auto d = h.mod.dribble();
    CHECK(d.contacts == 3);
    CHECK(d.cadence > 2.f && d.cadence < 4.f);

    // Dribble map: the panel starts with the cadence.
    const DrawList dl = h.draw();
    CHECK(!dl.panels.empty());
    if (!dl.panels.empty() && !dl.panels.front().lines.empty())
        CHECK(dl.panels.front().lines.front().text.rfind("Dribble : 3 touches", 0) == 0);

    // The chain ends after 1 s without contact and is recorded.
    tickLinear(h, h.latest, 130);
    CHECK(h.mod.sessionStats().bestChainContacts == 3);
    CHECK(h.mod.sessionStats().dribbleChains == 1);
}

TEST(ft_quick_second_contact_is_part_of_the_same_touch) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 6);
    Snapshot s = advance(h.latest);
    s = touchFrom(h, s, true, 10, Vec3{1000.f, 0.f, 0.f}, Vec3{1000.f, 0.f, 0.f});
    tickLinear(h, s, 130);
    CHECK(h.all.results.size() == 1);
    CHECK(h.mod.sessionStats().graded == 1);
}

TEST(ft_new_first_touch_closes_previous_window_early) {
    Harness h;
    h.set("rlt_ft_control_window", "2.0");
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 146);  // last frame 1148
    CHECK(h.all.results.empty());
    Snapshot s = advance(h.latest);                                     // pre at 1149
    s = touchFrom(h, s, true, 149, Vec3{1000.f, 0.f, 0.f}, Vec3{1000.f, 0.f, 0.f});
    CHECK(h.all.results.size() == 1);   // first touch graded on the elapsed part of its window
    CHECK(h.mod.analysisPending());
    tickLinear(h, s, 245);
    CHECK(h.all.results.size() == 2);
}

TEST(ft_follow_up_contacts_are_dribble_not_first_touches) {
    // Air-dribble style: a new contact every 0.5 s. Only the first one is graded.
    Harness h;
    Scenario sc;
    sc.car = airCar(Vec3{0.f, 0.f, 500.f}, Vec3{0.f, 0.f, 300.f});
    sc.ball = makeBall(Vec3{13.88f, 0.f, 630.1f}, Vec3{0.f, 0.f, 200.f});
    sc.postCarVel = Vec3{0.f, 0.f, 300.f};
    sc.postBallVel = Vec3{0.f, 0.f, 300.f};
    runScenario(h, sc, 57);
    Snapshot s = h.latest;
    for (int k = 0; k < 4; ++k) {
        s = advance(s);
        s = touchFrom(h, s, true, 60, s.car.vel, s.ball.vel);
        s = tickLinear(h, s, 57);
    }
    tickLinear(h, s, 30);
    CHECK(h.all.results.size() == 1);
    CHECK(h.mod.sessionStats().graded == 1);
    CHECK(h.mod.sessionStats().dribbleContacts == 4);
    CHECK(h.mod.dribble().contacts == 5);

    // Raising the gap setting makes slower juggles count as follow-ups too; lowering it grades them.
    Harness g;
    g.set("rlt_ft_first_touch_gap", "0.3");
    runScenario(g, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 57);
    Snapshot q = advance(g.latest);
    q = touchFrom(g, q, true, 60, q.car.vel, q.ball.vel);
    tickLinear(g, q, 130);
    CHECK(g.all.results.size() == 2);
}

TEST(ft_contact_after_teleport_is_a_fresh_first_touch) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 20);  // touch at 1000, last frame 1022
    // Reset shot: the previous contact (40 ticks before the next one) no longer counts.
    h.event(GameEventKind::ResetShot);
    Scenario sc = groundPickup(800.f, 760.f, 800.f, 800.f, 1040);
    sc.ticksSince = 40;
    runScenario(h, sc, 130);
    CHECK(h.all.results.size() == 1);
    if (!h.all.results.empty()) CHECK(h.all.results.front().skill == "first_touch.ground");
}

TEST(ft_teleport_reset_goal_and_invalid_states_cancel_grading) {
    {
        Harness h;
        runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 20);
        Snapshot s = advance(h.latest);
        s.ball.pos.x += 2500.f;  // ball teleported
        h.tick(s);
        CHECK(!h.mod.analysisPending());
        tickLinear(h, s, 150);
        CHECK(h.all.results.empty());
    }
    {
        Harness h;
        runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 20);
        h.event(GameEventKind::ResetShot);
        CHECK(!h.mod.analysisPending());
        tickLinear(h, h.latest, 150);
        CHECK(h.all.results.empty());
    }
    {
        Harness h;
        runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 20);
        h.event(GameEventKind::Goal);
        tickLinear(h, h.latest, 150);
        CHECK(h.all.results.empty());
    }
    {
        Harness h;  // car invalid (demolished) for the whole window: no grade
        runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 0);
        Snapshot s = h.latest;
        for (int i = 0; i < 150; ++i) {
            s = advance(s);
            Snapshot fed = s;
            fed.car.valid = false;
            h.tick(fed);
        }
        CHECK(h.all.results.empty());
        CHECK(h.mod.sessionStats().graded == 0);
    }
    {
        Harness h;  // touch with an invalid ball is ignored
        Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1000.f);
        TouchEvent t = buildTouch(sc);
        t.pre.ball.valid = false;
        h.tickWithTouch(t);
        CHECK(!h.mod.analysisPending());
    }
}

TEST(ft_context_change_resets_only_on_real_change) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 20);
    h.event(GameEventKind::ContextChanged);  // periodic refresh, same map
    CHECK(h.mod.analysisPending());
    h.game.mapName = "Stadium_P";
    h.event(GameEventKind::ContextChanged);
    CHECK(!h.mod.analysisPending());
    // Frames restart on the new map: touches are accepted again.
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f, 50), 130);
    CHECK(h.all.results.size() == 1);
    h.event(GameEventKind::MapUnloaded);
    CHECK(!h.mod.analysisPending());
}

TEST(ft_air_and_wall_touches_use_their_skill) {
    {
        Harness h;
        Scenario sc;
        sc.car = airCar(Vec3{0.f, 0.f, 500.f}, Vec3{0.f, 0.f, 300.f});
        sc.ball = makeBall(Vec3{13.88f, 0.f, 630.1f}, Vec3{0.f, 0.f, 200.f});
        sc.postCarVel = Vec3{0.f, 0.f, 300.f};
        sc.postBallVel = Vec3{0.f, 0.f, 340.f};
        const Actions a = runScenario(h, sc);
        CHECK(a.results.size() == 1);
        if (!a.results.empty()) {
            CHECK(a.results.front().skill == "first_touch.air");
            CHECK(a.results.front().title == "Première touche (aérien)");
        }
        CHECK(h.mod.lastTouch().part == CarPart::Roof);
    }
    {
        Harness h;
        Scenario sc;
        sc.car = groundCar(Vec3{3950.f, 0.f, 600.f}, Vec3{0.f, 0.f, 600.f});
        sc.car.onWall = true;
        sc.ball = makeBall(Vec3{3800.f, 0.f, 750.f}, Vec3{0.f, 0.f, 550.f});
        sc.postCarVel = Vec3{0.f, 0.f, 600.f};
        sc.postBallVel = Vec3{0.f, 0.f, 620.f};
        const Actions a = runScenario(h, sc);
        CHECK(a.results.size() == 1);
        if (!a.results.empty()) CHECK(a.results.front().skill == "first_touch.wall");
    }
}

TEST(ft_results_can_be_disabled_and_module_turned_off) {
    Harness h;
    h.set("rlt_ft_send_results", "0");
    CHECK(runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f)).results.empty());
    CHECK(h.mod.sessionStats().graded == 1);

    Harness off;
    off.set("rlt_ft_enabled", "0");
    const Actions a = runScenario(off, groundPickup(1000.f, 950.f, 1000.f, 1000.f));
    CHECK(a.results.empty() && a.chat.empty());
    CHECK(off.mod.sessionStats().graded == 0);
    CHECK(off.draw().empty());
}

// ---- Chat log (parity P21) --------------------------------------------------------------------------------

TEST(ft_chat_log_off_by_default_then_formats_lines) {
    Harness h;
    CHECK(runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f)).chat.empty());
    h.set("rlt_ft_chat", "1");
    const Actions a = runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 3000));
    CHECK(a.chat.size() == 1);
    if (!a.chat.empty()) CHECK(a.chat.front().text == "[BON] Écart +1.8 km/h, note 100");
    h.set("rlt_ft_unit", "2");
    h.set("rlt_ft_chat_score", "0");
    const Actions b = runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 5000));
    CHECK(b.chat.size() == 1);
    if (!b.chat.empty()) CHECK(b.chat.front().text == "[MAUVAIS] Écart +17.9 mph");
    // Auto unit follows the game setting (imperial).
    h.set("rlt_ft_unit", "0");
    h.profile.metric = false;
    const Actions c = runScenario(h, groundPickup(1200.f, 1000.f, 1100.f, 1300.f, 7000));
    CHECK(c.chat.size() == 1);
    if (!c.chat.empty()) CHECK(c.chat.front().text == "[MOYEN] Écart +4.5 mph");
}

TEST(ft_flip_reset_chat_and_dedupe) {
    Harness h;
    h.set("rlt_ft_chat", "1");
    Snapshot s = makeSnap(500, airCar(Vec3{0.f, 0.f, 800.f}, Vec3{1000.f, 0.f, 0.f}),
                          makeBall(Vec3{0.f, 0.f, 950.f}, Vec3{900.f, 0.f, 0.f}));
    h.tick(s);
    Actions a = h.event(GameEventKind::FlipReset, 500, "sdk");
    a.merge(h.event(GameEventKind::FlipReset, 500, "mechanics"));  // same reset reported by another source
    CHECK(a.chat.size() == 1);
    if (!a.chat.empty()) CHECK(a.chat.front().text == "[FLIP RESET] Écart +3.6 km/h [BON]");
    s = tickLinear(h, s, 30);
    CHECK(h.event(GameEventKind::FlipReset).chat.empty());  // 0.25 s later: same reset
    s = tickLinear(h, s, 31);
    CHECK(h.event(GameEventKind::FlipReset).chat.size() == 1);  // > 0.5 s: a new one
    CHECK(h.mod.sessionStats().flipResets == 2);
    h.set("rlt_ft_chat_flip", "0");
    s = tickLinear(h, s, 100);
    CHECK(h.event(GameEventKind::FlipReset).chat.empty());
    CHECK(h.mod.sessionStats().flipResets == 3);
    h.set("rlt_ft_chat", "0");
    h.set("rlt_ft_chat_flip", "1");
    tickLinear(h, s, 100);
    CHECK(h.event(GameEventKind::FlipReset).chat.empty());
}

// ---- Stats ------------------------------------------------------------------------------------------------

TEST(ft_session_streaks_and_averages) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 1000));
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 2000));
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 3000));
    runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 4000));
    const auto& st = h.mod.sessionStats();
    CHECK(st.graded == 4);
    CHECK(st.successes == 3);
    CHECK(st.bestStreak == 3);
    CHECK(st.streak == 0);
    CHECK_NEAR(st.goodZoneRate(), 0.75f, 1e-5f);
    CHECK_NEAR(st.meanSignedDiff(), (50.f * 3.f + 800.f) / 4.f, 1e-3f);
    CHECK(st.averageScore() > 70.f && st.averageScore() < 90.f);
    CHECK(h.mod.recentScores().size() == 4);
    CHECK(h.mod.heatmap().size() == 4);
    CHECK(h.all.requestSave == false);  // saved every 10 graded touches only
}

TEST(ft_trend_and_persistent_error_helpers) {
    TouchStats s;
    for (int i = 0; i < 4; ++i) s.addGraded(TouchType::Ground, 60.f, true, 250.f, false);
    CHECK(trendText(s, SpeedUnit::Kph, 99.f).empty());
    s.addGraded(TouchType::Ground, 60.f, true, 250.f, false);
    CHECK(contains(trendText(s, SpeedUnit::Kph, 99.f), "trop vite"));
    CHECK(contains(trendText(s, SpeedUnit::Kph, 99.f), "+9.0 km/h"));
    TouchStats slow;
    for (int i = 0; i < 5; ++i) slow.addGraded(TouchType::Bounce, 40.f, false, -250.f, false);
    CHECK(contains(trendText(slow, SpeedUnit::Kph, 99.f), "trop lentement"));
    TouchStats ok;
    for (int i = 0; i < 5; ++i) ok.addGraded(TouchType::Ground, 80.f, true, 20.f, true);
    CHECK(contains(trendText(ok, SpeedUnit::Kph, 99.f), "bonne gestion"));
    CHECK_NEAR(ok.typeAverage(TouchType::Ground), 80.f, 1e-4f);
    CHECK_NEAR(ok.typeAverage(TouchType::Air), 0.f, 1e-6f);

    CHECK(persistentCode({"ft.a", "ft.b", "ft.a", "", "ft.a"}) == "ft.a");
    CHECK(persistentCode({"ft.a", "ft.b", "ft.c", "", "ft.a"}).empty());
    CHECK(persistentCode({}).empty());
    CHECK(std::string(tipShortLabel("ft.too_fast")).size() > 5);
    CHECK(std::string(tipShortLabel("unknown")).empty());

    Ring<int, 3> r;
    for (int i = 1; i <= 5; ++i) r.push(i);
    CHECK(r.size() == 3);
    CHECK(r.at(0) == 3 && r.newest() == 5);
    CHECK(r.toVector() == std::vector<int>({3, 4, 5}));
    r.clear();
    CHECK(r.empty());

    TouchStats chains;
    chains.addChain(1, 5.f);  // a single contact is not a chain
    chains.addChain(4, 2.f);
    chains.addChain(3, 3.f);
    CHECK(chains.dribbleChains == 2);
    CHECK(chains.bestChainContacts == 4);
    CHECK_NEAR(chains.bestChainSeconds, 3.f, 1e-6f);
}

TEST(ft_persistent_error_shows_in_view) {
    Harness h;
    for (int i = 0; i < 3; ++i) runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 1000 + 1000 * i));
    CHECK(!h.mod.persistentTipCode().empty());
    CHECK(contains(h.mod.trend(), "Tendance") == false);  // fewer than 5 touches
}

// ---- Commands ---------------------------------------------------------------------------------------------

TEST(ft_reset_commands) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f));
    Actions a = h.command("reset_stats");
    CHECK(h.mod.sessionStats().graded == 0);
    CHECK(h.mod.totalStats().graded == 1);
    CHECK(h.mod.recentScores().empty() && h.mod.heatmap().empty());
    CHECK(!h.mod.lastTouch().valid);
    CHECK(!a.toasts.empty());

    h.set("rlt_ft_drill_seed", "5");
    h.tick(makeSnap(5000, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    h.command("drill", {"pass"});
    Snapshot s = makeSnap(5001, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{}));
    h.tick(s);
    for (int i = 0; i < 730; ++i) {
        s.frame += 1;
        s.time = static_cast<float>(s.frame) / kTickRate;
        s.in.throttle = 1.f;
        h.tick(s);
    }
    CHECK(h.mod.drillLevel(DrillType::Pass).level < 0.2f);
    a = h.command("reset_levels");
    CHECK_NEAR(h.mod.drillLevel(DrillType::Pass).level, 0.2f, 1e-6f);
    CHECK(h.mod.drillLevel(DrillType::Pass).attempts == 0);
    CHECK(a.requestSave);
    h.command("reset_all");
    CHECK(h.mod.totalStats().graded == 0);
    CHECK(hasTip(std::vector<Tip>{}, "x") == false);
    CHECK(!h.command("bogus").toasts.empty());
}

// ---- Persistence ------------------------------------------------------------------------------------------

TEST(ft_save_load_roundtrip) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 1000));
    runScenario(h, groundPickup(1600.f, 800.f, 1500.f, 2100.f, 2000));
    h.event(GameEventKind::FlipReset);
    // One successful drill rep to move a staircase.
    h.set("rlt_ft_drill_seed", "77");
    h.tick(makeSnap(2500, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    h.command("drill", {"lob"});
    h.tick(makeSnap(2501, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f, 2600));
    CHECK_NEAR(h.mod.drillLevel(DrillType::Lob).level, 0.24f, 1e-4f);

    Store st;
    h.mod.save(st);
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("firsttouch.", 0) == 0, kv.first);
    CHECK(st.raw().size() < 400);
    Store parsed;
    CHECK(parsed.parse(st.serialize()) > 0);

    Harness h2;
    h2.mod.load(parsed);
    const auto& a = h.mod.totalStats();
    const auto& b = h2.mod.totalStats();
    CHECK(b.graded == a.graded && a.graded == 3);
    CHECK(b.successes == a.successes);
    CHECK(b.goodZone == a.goodZone);
    CHECK(b.bestStreak == a.bestStreak);
    CHECK(b.flipResets == 1);
    CHECK_NEAR(b.sumScore, a.sumScore, 0.01f);
    CHECK_NEAR(b.meanSignedDiff(), a.meanSignedDiff(), 0.01f);
    CHECK(b.typeCount == a.typeCount);
    CHECK(h2.mod.sessionStats().graded == 0);  // a new session starts
    const auto sa = h.mod.recentScores(), sb = h2.mod.recentScores();
    CHECK(sa.size() == sb.size());
    for (size_t i = 0; i < sa.size() && i < sb.size(); ++i) CHECK_NEAR(sa[i], sb[i], 0.5f);
    const auto ha = h.mod.heatmap(), hb = h2.mod.heatmap();
    CHECK(ha.size() == hb.size());
    for (size_t i = 0; i < ha.size() && i < hb.size(); ++i) {
        CHECK_NEAR(ha[i].x, hb[i].x, 1e-3f);
        CHECK_NEAR(ha[i].z, hb[i].z, 1e-3f);
        CHECK(ha[i].type == hb[i].type);
    }
    CHECK_NEAR(h2.mod.drillLevel(DrillType::Lob).level, 0.24f, 1e-4f);
    CHECK(h2.mod.drillLevel(DrillType::Lob).attempts == 1);
    CHECK_NEAR(h2.mod.drillLevel(DrillType::Lob).best, 0.24f, 1e-4f);
    CHECK_NEAR(h2.mod.drillLevel(DrillType::Pass).level, 0.2f, 1e-6f);
}

TEST(ft_load_tolerates_garbage) {
    Store st;
    st.setString("firsttouch.total.graded", "-5");
    st.setString("firsttouch.total.sum_score", "nan");
    st.setList("firsttouch.scores", {"abc", "150", "42"});
    st.setList("firsttouch.heat", {"1;2;3;4;5", "bad", "0.5;0;-0.5;80;9", "0.1;0.2"});
    st.setString("firsttouch.drill.roll_in.level", "7");
    FirstTouchModule m;
    m.load(st);
    CHECK(m.totalStats().graded == 0);
    CHECK(std::isfinite(m.totalStats().sumScore));
    const auto scores = m.recentScores();
    CHECK(scores.size() == 2);
    if (scores.size() == 2) CHECK(scores[0] == 100.f && scores[1] == 42.f);
    const auto heat = m.heatmap();
    CHECK(heat.size() == 2);
    if (heat.size() == 2) {
        CHECK(heat[0].x == 1.f && heat[0].z == 1.f && heat[0].type == 4);
        CHECK(heat[1].type == 4 && heat[1].score == 80.f);
    }
    CHECK_NEAR(m.drillLevel(DrillType::RollIn).level, 1.f, 1e-6f);
    // Empty store: defaults.
    FirstTouchModule e;
    e.load(Store{});
    CHECK_NEAR(e.drillLevel(DrillType::High).level, 0.2f, 1e-6f);
    CHECK(e.recentScores().empty());
}

// Regression (review): the heat type was cast to int before clamping: "1e30" overflowed (undefined behaviour).
TEST(ft_load_clamps_huge_values_before_int_conversion) {
    Store st;
    st.setList("firsttouch.heat", {"0;0;0;50;1e30", "0;0;0;50;-1e30", "0;0;0;50;2.7", "0;0;0;50;3e38"});
    st.setString("firsttouch.total.graded", "99999999999999");
    st.setString("firsttouch.total.successes", "99999999999999");
    st.setString("firsttouch.total.flip_resets", "-99999999999999");
    FirstTouchModule m;
    m.load(st);
    const auto heat = m.heatmap();
    CHECK(heat.size() == 4);
    if (heat.size() == 4) {
        CHECK(heat[0].type == kTouchTypeCount - 1);
        CHECK(heat[1].type == 0);
        CHECK(heat[2].type == 2);
        CHECK(heat[3].type == kTouchTypeCount - 1);
    }
    CHECK(m.totalStats().graded == 1000000000);
    CHECK(m.totalStats().successes >= 0 && m.totalStats().successes <= m.totalStats().graded);
    CHECK(m.totalStats().flipResets == 0);
}

// A ball put on the roof (ballontop) and carried is a dribble start, not a graded first touch.
TEST(ft_ball_put_on_roof_is_not_graded_as_first_touch) {
    Harness h;
    Scenario sc = roofCatch(1000);
    sc.ball.vel.z = 0.f;
    sc.postBallVel = sc.car.vel;
    const Actions run = runScenario(h, sc, 130);
    CHECK(run.results.empty());
    CHECK(h.mod.sessionStats().graded == 0);
    CHECK(h.mod.sessionStats().dribbleContacts == 1);
    CHECK(!h.mod.analysisPending());
}

// ---- Draw -------------------------------------------------------------------------------------------------

TEST(ft_draw_nothing_before_first_touch) {
    Harness h;
    h.tick(makeSnap(10, groundCar(Vec3{}, Vec3{}), makeBall(Vec3{500.f, 0.f, kBallZ}, Vec3{})));
    CHECK(h.draw().empty());
}

TEST(ft_draw_panel_heatmap_and_marker) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f), 130);
    const DrawList dl = h.draw();
    CHECK(dl.panels.size() == 1);
    if (dl.panels.empty()) return;
    const DrawPanel& p = dl.panels.front();
    CHECK(p.title == "Première touche");
    CHECK(p.anchorX == Align::Right);
    CHECK_NEAR(p.pos.x, 0.99f * 1920.f, 0.01f);
    CHECK_NEAR(p.pos.y, 0.70f * 1080.f, 0.01f);
    CHECK(p.lines.size() >= 5);
    CHECK(!p.lines.empty() && p.lines.front().text.rfind("Sol : 100/100", 0) == 0);
    bool hasBar = false;
    for (const auto& l : p.lines) {
        CHECK(noEmDash(l.text));
        if (l.bar >= 0.f) hasBar = true;
    }
    CHECK(hasBar);
    // Heatmap above the panel, inside the screen.
    CHECK(!dl.rects2D.empty());
    for (const auto& r : dl.rects2D) {
        CHECK(r.pos.x >= 0.f && r.pos.x + r.size.x <= 1920.f + 0.01f);
        CHECK(r.pos.y >= 0.f && r.pos.y + r.size.y <= p.pos.y);
    }
    CHECK(dl.lines2D.size() >= 24 && dl.lines2D.size() < 60);
    CHECK(dl.triangles2D.size() == 1);
    // 3D contact marker (touch 1.1 s ago < 1.5 s).
    CHECK(!dl.lines3D.empty());
    CHECK(dl.lines3D.size() < 1500);
    for (const auto& l : dl.lines3D) CHECK(l.occluder.enabled);

    // 4K: positions follow the screen, scale follows the height.
    const DrawList big = h.draw(3840.f, 2160.f);
    CHECK(!big.panels.empty() && std::fabs(big.panels.front().pos.x - 0.99f * 3840.f) < 0.01f);
    CHECK(!big.panels.empty() && std::fabs(big.panels.front().scale - 2.f) < 1e-4f);

    // Options.
    h.set("rlt_ft_heatmap", "0");
    CHECK(h.draw().rects2D.empty());
    h.set("rlt_ft_marker", "0");
    CHECK(h.draw().lines3D.empty());
    h.set("rlt_ft_marker", "1");
    h.game.mode = GameMode::Replay;
    CHECK(h.draw().lines3D.empty());
    h.game.mode = GameMode::Freeplay;
    h.set("rlt_ft_hud", "0");
    CHECK(h.draw().panels.empty());
    // Marker expires.
    tickLinear(h, h.latest, 60);
    CHECK(h.draw().lines3D.empty());
}

TEST(ft_draw_drill_panel) {
    Harness h;
    h.set("rlt_ft_drill_seed", "9");
    h.tick(makeSnap(100, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    h.command("drill", {"roll_in"});
    h.tick(makeSnap(101, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    const DrawList dl = h.draw();
    CHECK(dl.panels.size() == 1);  // no stats yet: drill panel only
    if (dl.panels.empty()) return;
    const DrawPanel& p = dl.panels.front();
    CHECK(p.title == "Exercice : Roulante vers toi");
    CHECK(p.anchorX == Align::Left);
    CHECK_NEAR(p.pos.x, 0.01f * 1920.f, 0.01f);
    CHECK_NEAR(p.pos.y, 0.12f * 1080.f, 0.01f);
    CHECK(!p.lines.empty() && p.lines.front().text == "Niveau 20/100");
    bool instruction = false;
    for (const auto& l : p.lines)
        if (contains(l.text, "Boost (B)")) instruction = true;
    CHECK(instruction);
    const auto view = h.mod.drillStatus();
    CHECK(view.active && view.phase == FirstTouchModule::DrillPhase::WaitTouch);
    CHECK(contains(view.instruction, "Boost (B)"));
}

// ---- Contexts ---------------------------------------------------------------------------------------------

TEST(ft_analysis_works_without_world_control) {
    for (GameMode mode : {GameMode::Workshop, GameMode::CustomTraining, GameMode::OfflineMatch}) {
        Harness h;
        h.game.mode = mode;
        h.game.worldControlAllowed = false;
        h.game.mapName = "custom_map";
        h.event(GameEventKind::ContextChanged);
        const Actions a = runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f));
        CHECK(a.results.size() == 1);
        CHECK(!a.setBall && !a.setCar && !a.overrideInputs && !a.freezeCar && !a.setGameSpeed);
    }
}

TEST(ft_paused_ticks_do_not_count_in_the_window) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1000.f), 10);
    Snapshot s = h.latest;
    // While paused the ball "escapes" in the snapshots: ignored by the control window.
    for (int i = 0; i < 40; ++i) {
        s = advance(s);
        Snapshot fed = s;
        fed.paused = true;
        fed.ball.vel.x = 3000.f;
        h.tick(fed);
    }
    tickLinear(h, s, 100);
    CHECK(h.all.results.size() == 1);
    CHECK(h.mod.lastTouch().controlled);
    CHECK_NEAR(h.mod.lastTouch().controlFraction, 1.f, 1e-4f);
}

TEST(ft_replay_draw_uses_view_only) {
    Harness h;
    runScenario(h, groundPickup(1000.f, 950.f, 1000.f, 1040.f));
    h.game.mode = GameMode::Replay;
    h.hasSnap = false;  // no physics ticks in replays
    const DrawList dl = h.draw();
    CHECK(dl.lines3D.empty());
    CHECK(dl.panels.size() == 1);  // session panel from stored stats only
    ViewContext v;               // invalid view: still no crash, no 3D
    DrawContext dc{h.settings, h.game, h.profile, h.latest, false, false, 1.f};
    DrawList out;
    h.mod.draw(v, dc, out);
    CHECK(out.lines3D.empty());
}

TEST(ft_profile_drives_display_unit) {
    FirstTouchModule m;
    SettingsSchema schema;
    FirstTouchModule::describeSettings(schema);
    Settings s;
    schema.applyDefaults(s);
    m.onSettingsChanged(s);
    PlayerProfile p;
    p.metric = false;
    m.onProfileChanged(p);
    CHECK(m.displayUnit() == SpeedUnit::Mph);
    p.metric = true;
    m.onProfileChanged(p);
    CHECK(m.displayUnit() == SpeedUnit::Kph);
    schema.apply(s, "rlt_ft_unit", "3");
    m.onSettingsChanged(s);
    CHECK(m.displayUnit() == SpeedUnit::Uus);
}

TEST(ft_disabling_module_stops_running_drill) {
    Harness h;
    h.tick(makeSnap(100, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 1500.f, kBallZ}, Vec3{})));
    h.command("drill", {"mix"});
    CHECK(h.mod.drillStatus().active);
    h.set("rlt_ft_enabled", "0");
    CHECK(!h.mod.drillStatus().active);
    CHECK(h.host.releases == 1);
    CHECK(!h.tick(makeSnap(101, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{}, Vec3{}))).setBall);
}
