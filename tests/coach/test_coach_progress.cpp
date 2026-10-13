// Sessions, daily log / streak, session history encoding and session goals.

#include "tests/coach/CoachTestUtil.h"

#include "core/common/Text.h"
#include "core/modules/coach/CoachGoals.h"
#include "core/modules/coach/CoachProgress.h"
#include "core/modules/coach/CoachUtil.h"

using namespace rlt;
using namespace rlt::coach;

TEST(coach_session_tracker_numbers) {
    SessionTracker s;
    CHECK(!s.active());
    s.add("mech.speedflip", "Speedflip", true, 90.f, false);  // ignored while inactive
    CHECK_EQ(s.attempts(), 0);
    s.start(4, "2026-10-13");
    CHECK(s.active());
    CHECK_EQ(s.index(), 4);
    s.add("mech.speedflip", "Speedflip", true, 90.f, true);
    s.add("mech.speedflip", "Speedflip", false, 50.f, false);
    s.addTime(1.5f);
    s.addTime(-3.f);           // ignored
    s.addTime(std::nanf(""));  // ignored
    CHECK_EQ(s.attempts(), 2);
    CHECK_EQ(s.successes(), 1);
    CHECK_EQ(s.records(), 1);
    CHECK_NEAR(s.average(), 70.f, 1e-4f);
    CHECK_NEAR(s.seconds(), 1.5f, 1e-5f);
    s.stop();
    CHECK(!s.active());
    CHECK_EQ(s.attempts(), 0);
}

TEST(coach_session_strong_and_weak_skills) {
    SessionTracker s;
    s.start(1, "2026-10-13");
    for (int i = 0; i < 4; ++i) s.add("kickoff.diagonal", "Kickoff", true, 85.f, false);
    for (int i = 0; i < 4; ++i) s.add("mech.wavedash", "Wavedash", false, 40.f, false);
    for (int i = 0; i < 4; ++i) s.add("ball.aerial", "Aérienne", true, 60.f, false);
    s.add("ball.pinch", "Pinch", true, 100.f, false);  // only one attempt: not enough to judge
    std::string strong, weak;
    s.strongWeak(3, strong, weak);
    CHECK_EQ(strong, std::string("Kickoff"));
    CHECK_EQ(weak, std::string("Wavedash"));
    // A single qualifying skill is either a strength or a weakness, not both.
    SessionTracker one;
    one.start(1, "");
    for (int i = 0; i < 3; ++i) one.add("ball.aerial", "Aérienne", true, 75.f, false);
    one.strongWeak(3, strong, weak);
    CHECK_EQ(strong, std::string("Aérienne"));
    CHECK(weak.empty());
    SessionTracker low;
    low.start(1, "");
    for (int i = 0; i < 3; ++i) low.add("ball.aerial", "Aérienne", false, 35.f, false);
    low.strongWeak(3, strong, weak);
    CHECK(strong.empty());
    CHECK_EQ(weak, std::string("Aérienne"));
    SessionTracker none;
    none.start(1, "");
    none.add("x", "X", true, 50.f, false);
    none.strongWeak(3, strong, weak);
    CHECK(strong.empty() && weak.empty());
}

TEST(coach_session_summary_text) {
    SessionRecord r;
    r.attempts = 42;
    r.average = 71.2f;
    r.records = 2;
    r.seconds = 25 * 60 + 10;
    r.strong = "Kickoff";
    r.weak = "Wavedash";
    CHECK_EQ(
        sessionSummaryText(r, 3),
        std::string("Session : 42 essais en 25:10, moyenne 71, 2 records, point fort Kickoff, à travailler Wavedash. "
                    "Série : 3 jours."));
    SessionRecord s;
    s.attempts = 1;
    s.average = 55.f;
    s.records = 1;
    s.seconds = 30.f;
    CHECK_EQ(sessionSummaryText(s, 1), std::string("Session : 1 essai, moyenne 55, 1 record. Série : 1 jour."));
    s.records = 0;
    CHECK_EQ(sessionSummaryText(s, 0), std::string("Session : 1 essai, moyenne 55."));
    CHECK_EQ(sessionSummaryText(s, 0, 2, 3), std::string("Session : 1 essai, moyenne 55, objectifs 2/3."));
    // No em dash and no characters outside of what the HUD folding handles.
    const std::string t = sessionSummaryText(r, 3);
    CHECK(t.find("\xE2\x80\x94") == std::string::npos);
    CHECK(foldAccents(t).find("a travailler") != std::string::npos);
}

TEST(coach_session_record_encoding) {
    SessionRecord r;
    r.index = 12;
    r.date = "2026-10-13";
    r.attempts = 42;
    r.successes = 30;
    r.average = 71.25f;
    r.records = 2;
    r.seconds = 1510.f;
    r.strong = "Kickoff\tdiagonal";  // tabs are sanitized
    r.weak = "Wavedash";
    SessionRecord d;
    CHECK(decodeSession(encodeSession(r), d));
    CHECK_EQ(d.index, 12);
    CHECK_EQ(d.date, r.date);
    CHECK_EQ(d.attempts, 42);
    CHECK_EQ(d.successes, 30);
    CHECK_NEAR(d.average, 71.25f, 0.06f);
    CHECK_EQ(d.records, 2);
    CHECK_NEAR(d.seconds, 1510.f, 0.5f);
    CHECK_EQ(d.strong, std::string("Kickoff diagonal"));
    CHECK_EQ(d.weak, std::string("Wavedash"));
    CHECK(!decodeSession("", d));
    CHECK(!decodeSession("1\t2\t3", d));
    CHECK(!decodeSession("x\t2026-10-13\t1\t1\t50\t0\t10\ta\tb", d));
    CHECK(decodeSession("1\tbad-date\t5\t9\t500\t9\t10\ta\tb", d));
    CHECK(d.date.empty());
    CHECK_EQ(d.successes, 5);  // clamped to attempts
    CHECK_NEAR(d.average, 100.f, 1e-4f);
    CHECK_EQ(d.records, 5);
}

TEST(coach_daily_streak_across_dates) {
    DailyLog log;
    auto play = [&](const std::string& day, int n) {
        for (int i = 0; i < n; ++i) log.add(day, true, 70.f);
    };
    play("2026-09-28", 12);  // older streak, broken on 09-29
    play("2026-09-30", 10);
    play("2026-10-01", 15);  // month boundary
    play("2026-10-02", 10);
    CHECK_EQ(log.streak("2026-10-02", 10), 3);
    // Today not done yet: the streak is still alive (counted until yesterday).
    play("2026-10-03", 4);
    CHECK_EQ(log.streak("2026-10-03", 10), 3);
    play("2026-10-03", 6);
    CHECK_EQ(log.streak("2026-10-03", 10), 4);
    // A missed day breaks it.
    CHECK_EQ(log.streak("2026-10-05", 10), 0);
    // A lower requirement joins the older days... except the 09-29 gap.
    CHECK_EQ(log.streak("2026-10-03", 1), 4);
    CHECK_EQ(log.streak("bad", 10), 0);
    CHECK_EQ(log.streak("2026-10-03", 0), 4);  // requirement clamped to 1
    // Invalid dates are not logged.
    log.add("", true, 50.f);
    log.add("2026-02-30", true, 50.f);
    CHECK_EQ(log.size(), static_cast<size_t>(5));
}

TEST(coach_daily_log_points_and_persistence) {
    DailyLog log;
    log.add("2026-10-11", true, 80.f);
    log.add("2026-10-11", false, 40.f);
    log.add("2026-10-13", true, 90.f);
    const auto pts = log.lastDays("2026-10-13", 4);
    CHECK_EQ(pts.size(), static_cast<size_t>(4));
    CHECK_EQ(pts[0].date, std::string("2026-10-10"));
    CHECK_EQ(pts[0].attempts, 0);
    CHECK_EQ(pts[1].attempts, 2);
    CHECK_NEAR(pts[1].average, 60.f, 1e-4f);
    CHECK_EQ(pts[2].attempts, 0);
    CHECK_EQ(pts[3].attempts, 1);
    CHECK(log.lastDays("", 4).empty());

    Store st;
    log.save(st, "coach.days");
    Store st2;
    st2.parse(st.serialize());
    DailyLog back;
    back.load(st2, "coach.days");
    CHECK_EQ(back.size(), log.size());
    const DayStats* d = back.day("2026-10-11");
    CHECK(d != nullptr);
    if (d) {
        CHECK_EQ(d->attempts, 2);
        CHECK_EQ(d->successes, 1);
        CHECK_NEAR(static_cast<float>(d->scoreSum), 120.f, 0.1f);
    }
    // Garbage lines are skipped.
    Store bad;
    bad.setList("k", {"2026-10-11\t3\t9\t100", "nope", "2026-13-01\t1\t1\t1", "2026-10-12\t-1\t0\t0"});
    back.load(bad, "k");
    CHECK_EQ(back.size(), static_cast<size_t>(1));
    CHECK_EQ(back.day("2026-10-11")->successes, 3);
}

TEST(coach_daily_log_is_bounded) {
    DailyLog log;
    for (int i = 0; i < 500; ++i) log.add(shiftDate("2025-01-01", i), true, 50.f);
    CHECK_EQ(log.size(), DailyLog::kMaxDays);
    CHECK(log.day("2025-01-01") == nullptr);  // oldest dropped
    CHECK(log.day(shiftDate("2025-01-01", 499)) != nullptr);
}

TEST(coach_goal_presets_are_valid) {
    CHECK(goalPresets().size() >= 10);
    for (const auto& p : goalPresets()) {
        Goal g;
        CHECK(makePresetGoal(p.id, g));
        CHECK(!g.label.empty());
        CHECK(g.target > 0);
        CHECK_EQ(g.preset, std::string(p.id));
        CHECK(std::string(p.label).find("\xE2\x89\xA5") == std::string::npos);  // no "≥": the HUD cannot draw it
    }
    Goal g;
    CHECK(!makePresetGoal("nope", g));
    CHECK(makePresetGoal("ft70", g));
    CHECK(g.kind == GoalKind::ScoreAtLeast);
    CHECK_EQ(g.scope, std::string("first_touch"));
    CHECK_EQ(g.target, 20);
    CHECK_NEAR(g.minScore, 70.f, 1e-5f);
}

TEST(coach_goal_scope_matching) {
    CHECK(scopeMatches("", "anything.at_all"));
    CHECK(scopeMatches("any", "mech.speedflip"));
    CHECK(scopeMatches("mech.speedflip", "mech.speedflip"));
    CHECK(!scopeMatches("mech.speedflip", "mech.speedflipper"));
    CHECK(scopeMatches("first_touch", "first_touch.ground"));
    CHECK(scopeMatches("course.rings", "course.rings.air"));
    CHECK(!scopeMatches("course.rings", "course.ringsX"));
    CHECK(!scopeMatches("course.rings", "course.control.slalom"));
    CHECK(!scopeMatches("kickoff", "first_touch.ground"));
}

TEST(coach_goal_progress_and_completion) {
    Goal flips;
    CHECK(makePresetGoal("speedflip10", flips));
    flips.target = 3;
    CHECK(!applyResultToGoal(flips, "mech.speedflip", 40.f, false, false));  // failure: no progress
    CHECK(applyResultToGoal(flips, "mech.speedflip", 80.f, true, false));
    CHECK(!applyResultToGoal(flips, "mech.halfflip", 80.f, true, false));  // other skill
    CHECK(applyResultToGoal(flips, "mech.speedflip", 80.f, true, false));
    CHECK(!flips.done);
    CHECK_NEAR(flips.ratio(), 2.f / 3.f, 1e-5f);
    CHECK(applyResultToGoal(flips, "mech.speedflip", 80.f, true, false));
    CHECK(flips.done);
    CHECK_EQ(flips.progress, 3);
    CHECK(!applyResultToGoal(flips, "mech.speedflip", 80.f, true, false));  // done: frozen
    CHECK_EQ(flips.progress, 3);
    CHECK_NEAR(flips.ratio(), 1.f, 1e-6f);

    Goal touches;
    CHECK(makePresetGoal("ft70", touches));
    CHECK(!applyResultToGoal(touches, "first_touch.ground", 69.9f, true, false));
    CHECK(applyResultToGoal(touches, "first_touch.air", 70.f, false, false));  // score counts, not success
    CHECK_EQ(touches.progress, 1);

    Goal pb;
    CHECK(makePresetGoal("kickoff_pb", pb));
    CHECK(!applyResultToGoal(pb, "kickoff.diagonal", 99.f, true, false));
    CHECK(applyResultToGoal(pb, "kickoff.diagonal", 99.f, true, true));
    CHECK(pb.done);

    Goal all;
    CHECK(makePresetGoal("attempts50", all));
    CHECK(applyResultToGoal(all, "whatever", 0.f, false, false));
    CHECK_EQ(all.progress, 1);
}

TEST(coach_goal_custom_parsing) {
    Goal g;
    std::string err;
    CHECK(parseCustomGoal({"success", "mech.halfflip", "15"}, g, err, "Half-flip"));
    CHECK(g.kind == GoalKind::Successes);
    CHECK_EQ(g.scope, std::string("mech.halfflip"));
    CHECK_EQ(g.target, 15);
    CHECK_EQ(g.label, std::string("15 réussites : Half-flip"));
    CHECK_EQ(g.preset, std::string("custom"));
    CHECK(parseCustomGoal({"score", "first_touch", "20", "75"}, g, err));
    CHECK_EQ(g.label, std::string("20 essais notés 75 ou plus : Premières touches"));
    CHECK(parseCustomGoal({"score", "any", "5"}, g, err));
    CHECK_NEAR(g.minScore, 70.f, 1e-5f);  // default minimum
    CHECK_EQ(g.label, std::string("5 essais notés 70 ou plus"));
    CHECK(parseCustomGoal({"attempts", "any", "30"}, g, err));
    CHECK_EQ(g.label, std::string("30 essais dans la session"));
    CHECK(parseCustomGoal({"record", "kickoff", "1"}, g, err));
    CHECK_EQ(g.label, std::string("Battre ton record : Kickoffs"));
    CHECK(parseCustomGoal({"record", "any", "2"}, g, err));
    CHECK_EQ(g.label, std::string("Battre 2 records"));
    CHECK(parseCustomGoal({"success", "course.rings", "3"}, g, err));
    CHECK_EQ(g.label, std::string("3 réussites : Parcours d'anneaux"));

    CHECK(!parseCustomGoal({"success", "mech.halfflip"}, g, err));
    CHECK(!err.empty());
    CHECK(!parseCustomGoal({"jump", "any", "3"}, g, err));
    CHECK(!parseCustomGoal({"success", "any", "0"}, g, err));
    CHECK(!parseCustomGoal({"success", "any", "5000"}, g, err));
    CHECK(!parseCustomGoal({"success", "any", "3x"}, g, err));
    CHECK(!parseCustomGoal({"score", "any", "3", "120"}, g, err));
    CHECK(!parseCustomGoal({"success", "   ", "3"}, g, err));
}

TEST(coach_goal_encoding_roundtrip) {
    Goal g;
    CHECK(makePresetGoal("ft70", g));
    g.progress = 7;
    Goal d;
    CHECK(decodeGoal(encodeGoal(g), d));
    CHECK(d.kind == g.kind);
    CHECK_EQ(d.scope, g.scope);
    CHECK_EQ(d.target, g.target);
    CHECK_NEAR(d.minScore, g.minScore, 1e-4f);
    CHECK_EQ(d.label, g.label);
    CHECK_EQ(d.preset, g.preset);
    CHECK_EQ(d.progress, 0);  // progress belongs to the session, not to the file
    Goal any;
    CHECK(makePresetGoal("attempts50", any));
    CHECK(decodeGoal(encodeGoal(any), d));
    CHECK(d.scope.empty());
    CHECK(!decodeGoal("", d));
    CHECK(!decodeGoal("9\t\t5\t0\tx\ty", d));
    CHECK(!decodeGoal("1\t\t0\t0\tx\ty", d));
    CHECK(!decodeGoal("1\t\tfive\t0\tx\ty", d));
    CHECK(decodeGoal("1\tmech.stall\t4\t0\t\tcustom", d));
    CHECK_EQ(d.label, std::string("4 réussites : Stall"));  // label rebuilt when missing
}
