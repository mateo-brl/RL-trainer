// SkillTracker: per-skill statistics, records, recurring mistakes, metric analysis and persistence.

#include "tests/coach/CoachTestUtil.h"

#include "core/modules/coach/SkillTracker.h"

using namespace rlt;
using coachtest::R;

namespace {
const std::string kDay = "2026-10-13";
}

TEST(coach_tracker_basic_stats) {
    SkillTracker t("mech.speedflip");
    CHECK_EQ(t.title(), std::string("Speedflip"));  // default title before any result
    t.add(R("mech.speedflip", 40.f, false).title("Speedflip"), kDay, 1);
    t.add(R("mech.speedflip", 80.f, true), kDay, 1);
    t.add(R("mech.speedflip", 90.f, true), kDay, 2);
    CHECK_EQ(t.attempts(), 3);
    CHECK_EQ(t.successes(), 2);
    CHECK_NEAR(t.successRate(), 2.f / 3.f, 1e-5f);
    CHECK_NEAR(t.averageAll(), 70.f, 1e-4f);
    CHECK_NEAR(t.averageRecent(), 70.f, 1e-4f);
    CHECK_NEAR(t.bestScore(), 90.f, 1e-5f);
    CHECK_EQ(t.lastDate(), kDay);
    CHECK_EQ(t.lastSession(), 2);
    int n = 0;
    CHECK_NEAR(t.recentSuccessRate(10, &n), 2.f / 3.f, 1e-5f);
    CHECK_EQ(n, 3);
    CHECK(t.trend(0.5f) == Trend::Unknown);  // fewer than 5 scores
}

TEST(coach_tracker_scores_clamped_and_bounded) {
    SkillTracker t("ball.aerial");
    t.add(R("ball.aerial", 150.f, true), kDay, 1);
    CHECK_NEAR(t.bestScore(), 100.f, 1e-5f);
    t.add(R("ball.aerial", -20.f, false), kDay, 1);
    CHECK_NEAR(t.scores().back(), 0.f, 1e-5f);
    for (int i = 0; i < 120; ++i) t.add(R("ball.aerial", static_cast<float>(i % 100), i % 2 == 0), kDay, 1);
    CHECK_EQ(t.attempts(), 122);
    CHECK_EQ(t.scores().size(), SkillTracker::kScoreHistory);
    // Recent average = last 20 scores only (100..119 -> i%100 = 0..19 -> mean 9.5).
    CHECK_NEAR(t.averageRecent(), 9.5f, 1e-4f);
    int n = 0;
    t.recentSuccessRate(100, &n);
    CHECK_EQ(n, static_cast<int>(SkillTracker::kScoreHistory));
}

TEST(coach_tracker_trend_up_down_stable) {
    SkillTracker up("a.up"), down("a.down"), flat("a.flat");
    for (int i = 0; i < 20; ++i) {
        up.add(R("a.up", 40.f + 2.f * static_cast<float>(i), true), kDay, 1);
        down.add(R("a.down", 90.f - 2.f * static_cast<float>(i), true), kDay, 1);
        flat.add(R("a.flat", i % 2 ? 72.f : 68.f, true), kDay, 1);
    }
    CHECK(up.trend(0.5f) == Trend::Up);
    CHECK(down.trend(0.5f) == Trend::Down);
    CHECK(flat.trend(0.5f) == Trend::Stable);
    CHECK_NEAR(up.trendSlope(), 2.f, 1e-3f);
    // The trend only looks at the last 20: an old slump does not count.
    SkillTracker recov("a.recov");
    for (int i = 0; i < 20; ++i) recov.add(R("a.recov", 20.f, false), kDay, 1);
    for (int i = 0; i < 20; ++i) recov.add(R("a.recov", 80.f, true), kDay, 1);
    CHECK(recov.trend(0.5f) == Trend::Stable);
}

TEST(coach_tracker_record_higher_is_better) {
    SkillTracker t("shoot.target.top");
    auto u = t.add(R("shoot.target.top", 70.f, true).record(70.f, false), kDay, 1);
    CHECK(u.firstRecord);
    CHECK(!u.newRecord);  // first value is not celebrated
    CHECK(t.hasRecord());
    u = t.add(R("shoot.target.top", 65.f, true).record(65.f, false), kDay, 1);
    CHECK(!u.newRecord);
    u = t.add(R("shoot.target.top", 70.f, true).record(70.f, false), kDay, 1);
    CHECK(!u.newRecord);  // equal is not a record
    u = t.add(R("shoot.target.top", 85.f, true).record(85.f, false), kDay, 1);
    CHECK(u.newRecord);
    CHECK_NEAR(u.previous, 70.f, 1e-5f);
    CHECK_NEAR(t.record(), 85.f, 1e-5f);
    CHECK(!t.lowerIsBetter());
}

TEST(coach_tracker_record_lower_is_better_and_unit) {
    SkillTracker t("mech.speedflip");
    t.add(R("mech.speedflip", 70.f, true).metric("time", "Temps au ballon", 2.05f, "s", 1.95f).record(2.05f, true),
          kDay, 1);
    CHECK_EQ(t.recordUnit(), std::string("s"));
    CHECK_EQ(t.recordText(), std::string("2.05 s"));
    auto u = t.add(R("mech.speedflip", 90.f, true).record(1.92f, true), kDay, 1);
    CHECK(u.newRecord);
    CHECK_NEAR(u.previous, 2.05f, 1e-5f);
    CHECK_EQ(t.recordText(), std::string("1.92 s"));  // no metric: times default to seconds
    u = t.add(R("mech.speedflip", 95.f, true).record(2.0f, true), kDay, 1);
    CHECK(!u.newRecord);
    CHECK_NEAR(t.record(), 1.92f, 1e-5f);
    // Unit found from a matching metric.
    SkillTracker d("course.rings.air");
    d.add(R("course.rings.air", 80.f, true).metric("dist", "Distance", 4200.f, "uu", 0.f, false).record(4200.f, false),
          kDay, 1);
    CHECK_EQ(d.recordUnit(), std::string("uu"));
}

TEST(coach_tracker_record_needs_value_not_success) {
    SkillTracker t("kickoff.diagonal");
    t.add(R("kickoff.diagonal", 90.f, true), kDay, 1);  // no record value
    CHECK(!t.hasRecord());
    t.add(R("kickoff.diagonal", 80.f, true).record(std::nanf(""), true), kDay, 1);
    CHECK(!t.hasRecord());
    t.add(R("kickoff.diagonal", 80.f, true).record(2.45f, true), kDay, 1);
    CHECK(t.hasRecord());
    // The contract is hasRecordValue: a faster kickoff graded 65 (not a success) is still the record, as in the
    // kickoff and mechanics tabs.
    auto u = t.add(R("kickoff.diagonal", 65.f, false).record(2.20f, true), kDay, 1);
    CHECK(u.newRecord);
    CHECK_NEAR(u.previous, 2.45f, 1e-5f);
    CHECK_NEAR(t.record(), 2.20f, 1e-5f);
    // A producer switching the comparison direction restarts the record.
    u = t.add(R("kickoff.diagonal", 80.f, true).record(70.f, false), kDay, 1);
    CHECK(u.firstRecord);
    CHECK(!t.lowerIsBetter());
    CHECK_NEAR(t.record(), 70.f, 1e-5f);
}

TEST(coach_tracker_record_ignores_failed_placeholder) {
    // ballmech reports failed drills with hasRecordValue and a value of 0: never a record (first or new).
    SkillTracker t("ball.flick");
    auto u = t.add(R("ball.flick", 0.f, false).record(0.f, false), kDay, 1);
    CHECK(!t.hasRecord());
    CHECK(!u.firstRecord && !u.newRecord);
    u = t.add(R("ball.flick", 72.f, true).record(72.f, false), kDay, 1);
    CHECK(u.firstRecord);
    CHECK(!u.newRecord);  // the first real value is not "beaten" from 0
    u = t.add(R("ball.flick", 0.f, false).record(0.f, false), kDay, 1);
    CHECK(!u.newRecord);
    CHECK_NEAR(t.record(), 72.f, 1e-5f);
    // A failed attempt with a real value still counts.
    u = t.add(R("ball.flick", 40.f, false).record(80.f, false), kDay, 1);
    CHECK(u.newRecord);
    CHECK_NEAR(t.record(), 80.f, 1e-5f);
    // A successful 0 is a real value (e.g. a perfect zero error), kept as a lower-is-better record.
    SkillTracker z("future.error");
    u = z.add(R("future.error", 100.f, true).record(0.f, true), kDay, 1);
    CHECK(u.firstRecord);
    CHECK(z.hasRecord());
}

TEST(coach_tracker_title_is_stable) {
    // Known ids: always the catalog name, whatever headline the attempt carried.
    SkillTracker f("ball.flick");
    f.add(R("ball.flick", 0.f, false).title("Flick : raté"), kDay, 1);
    CHECK_EQ(f.title(), std::string("Flick"));
    f.add(R("ball.flick", 90.f, true).title("Flick (avant)"), kDay, 1);
    CHECK_EQ(f.title(), std::string("Flick"));
    SkillTracker k("kickoff.diagonal");
    k.add(R("kickoff.diagonal", 80.f, true).title("Kickoff (diagonale gauche)"), kDay, 1);
    k.add(R("kickoff.diagonal", 80.f, true).title("Kickoff (diagonale droite)"), kDay, 1);
    CHECK_EQ(k.title(), std::string("Kickoff (diagonale)"));
    SkillTracker s("shoot.flick.musty");
    s.add(R("shoot.flick.musty", 0.f, false).verdict(Verdict::Fail).title("Balle perdue avant le flick"), kDay, 1);
    CHECK_EQ(s.title(), std::string("Flick (musty)"));
    SkillTracker c("mech.ceilingdash");
    c.add(R("mech.ceilingdash", 80.f, true).title("Ceiling dash (mur)"), kDay, 1);
    CHECK_EQ(c.title(), std::string("Ceiling dash"));

    // Unknown ids (workshop maps, future modules): never named by a failed attempt...
    SkillTracker m("map.lethamyr_rings");
    m.add(R("map.lethamyr_rings", 10.f, false).title("Lethamyr Rings : raté"), kDay, 1);
    CHECK_EQ(m.title(), std::string("Map lethamyr rings"));
    // ...a non-failed attempt gives a provisional name, the first success the final one.
    m.add(R("map.lethamyr_rings", 55.f, false).title("Lethamyr Rings (abandon)"), kDay, 1);
    CHECK_EQ(m.title(), std::string("Lethamyr Rings (abandon)"));
    m.add(R("map.lethamyr_rings", 80.f, true).title("Lethamyr Rings"), kDay, 1);
    CHECK_EQ(m.title(), std::string("Lethamyr Rings"));
    m.add(R("map.lethamyr_rings", 95.f, true).title("Lethamyr Rings (PB)"), kDay, 1);
    m.add(R("map.lethamyr_rings", 0.f, false).title("Lethamyr Rings : raté"), kDay, 1);
    CHECK_EQ(m.title(), std::string("Lethamyr Rings"));

    // Persisted with its state: a reload keeps the final name.
    Store st;
    m.save(st, "coach.s.0.");
    SkillTracker l;
    CHECK(l.load(st, "coach.s.0."));
    CHECK_EQ(l.title(), std::string("Lethamyr Rings"));
    l.add(R("map.lethamyr_rings", 90.f, true).title("Autre titre"), kDay, 1);
    CHECK_EQ(l.title(), std::string("Lethamyr Rings"));
}

TEST(coach_tracker_record_text_keeps_time_precision) {
    // Workshop map run times are over 100 s: the record keeps its centiseconds (m:ss.cc).
    SkillTracker t("map.lethamyr_rings");
    t.add(R("map.lethamyr_rings", 80.f, true).metric("time", "Temps", 124.80f, "s", 0.f, false).record(124.80f, true),
          kDay, 1);
    CHECK_EQ(t.recordText(), std::string("2:04.80"));
    auto u = t.add(
        R("map.lethamyr_rings", 85.f, true).metric("time", "Temps", 124.62f, "s", 0.f, false).record(124.62f, true),
        kDay, 1);
    CHECK(u.newRecord);
    std::string now, before;
    t.formatRecordPair(u.value, u.previous, now, before);
    CHECK_EQ(now, std::string("2:04.62"));
    CHECK_EQ(before, std::string("2:04.80"));
    CHECK(now != before);
    CHECK_EQ(t.recordText(), std::string("2:04.62"));
}

TEST(coach_tracker_recurring_mistakes_ranking) {
    SkillTracker t("first_touch.ground");
    // too_fast x3, angle x3 (more recent), boost x1.
    t.add(R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Ralentis avant le contact."), kDay, 1);
    t.add(R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."), kDay, 1);
    t.add(R("first_touch.ground", 40.f, false).tip("ft.angle", "Aligne-toi sur la balle."), kDay, 1);
    t.add(R("first_touch.ground", 40.f, false)
              .tip("ft.too_fast", "Laisse venir la balle.")
              .tip("ft.boost", "Lâche le boost."),
          kDay, 1);
    t.add(R("first_touch.ground", 40.f, false).tip("ft.angle", "Aligne-toi."), kDay, 1);
    t.add(R("first_touch.ground", 90.f, true), kDay, 1);  // no tip, still in the window
    t.add(R("first_touch.ground", 40.f, false).tip("ft.angle", "Aligne ta trajectoire."), kDay, 1);
    const auto all = t.mistakes();
    CHECK_EQ(all.size(), static_cast<size_t>(3));
    // Tie 3-3: the most recent code wins.
    CHECK_EQ(all[0].code, std::string("ft.angle"));
    CHECK_EQ(all[0].count, 3);
    CHECK_EQ(all[0].window, 7);
    CHECK_EQ(all[0].text, std::string("Aligne ta trajectoire."));  // latest text
    CHECK_EQ(all[1].code, std::string("ft.too_fast"));
    CHECK_EQ(all[1].text, std::string("Laisse venir la balle."));
    CHECK_EQ(all[2].code, std::string("ft.boost"));
    CHECK_EQ(t.mainMistake().code, std::string("ft.angle"));
    CHECK_EQ(t.codeCount("ft.angle", 5), 3);  // attempts 3, 5 and 7
    CHECK_EQ(t.codeCount("ft.too_fast", 5), 1);
    CHECK_EQ(t.codeCount("ft.angle", 100), 3);
}

TEST(coach_tracker_mistakes_window_and_caps) {
    SkillTracker t("mech.wavedash");
    for (int i = 0; i < 10; ++i) t.add(R("mech.wavedash", 30.f, false).tip("me.old", "Ancienne erreur."), kDay, 1);
    for (int i = 0; i < 30; ++i) t.add(R("mech.wavedash", 30.f, false).tip("me.new", "Nouvelle erreur."), kDay, 1);
    // The 10 old attempts left the 30-attempt window.
    const auto all = t.mistakes();
    CHECK_EQ(all.size(), static_cast<size_t>(1));
    CHECK_EQ(all[0].code, std::string("me.new"));
    CHECK_EQ(all[0].count, 30);
    CHECK_EQ(all[0].window, 30);
    // Same code twice in one attempt counts once; at most 3 codes per attempt, by weight.
    SkillTracker u("mech.halfflip");
    u.add(R("mech.halfflip", 30.f, false)
              .tip("a", "A", 1.f)
              .tip("a", "A bis", 5.f)
              .tip("b", "B", 4.f)
              .tip("c", "C", 3.f)
              .tip("d", "D", 0.5f)
              .tip("", "sans code", 9.f),
          kDay, 1);
    const auto m = u.mistakes();
    CHECK_EQ(m.size(), static_cast<size_t>(3));
    CHECK_EQ(u.codeCount("a", 1), 1);
    CHECK_EQ(u.codeCount("d", 1), 0);  // lowest weight dropped
    CHECK_EQ(u.mainMistake().text, std::string("A bis"));
    CHECK(!SkillTracker("x.y").mainMistake().valid());
}

TEST(coach_tracker_metric_reason_biased_and_discriminative) {
    SkillTracker t("first_touch.ground");
    // Failures: speed difference around +300; successes around +60. Ideal 0.
    for (int i = 0; i < 6; ++i)
        t.add(R("first_touch.ground", 35.f, false)
                  .metric("speed_diff", "Écart de vitesse", 280.f + 10.f * static_cast<float>(i), "uu/s", 0.f),
              kDay, 1);
    for (int i = 0; i < 4; ++i)
        t.add(R("first_touch.ground", 85.f, true).metric("speed_diff", "Écart de vitesse", 60.f, "uu/s", 0.f), kDay, 1);
    const std::string reason = t.metricReason();
    CHECK_MSG(reason.find("Écart de vitesse") == 0, reason);
    CHECK_MSG(reason.find("+") != std::string::npos, reason);
    CHECK_MSG(reason.find("uu/s en moyenne, vise 0 uu/s") != std::string::npos, reason);
    CHECK_MSG(reason.find("+207 uu/s") != std::string::npos, reason);  // mean of 280..330 (x6) and 60 (x4) = 207
}

TEST(coach_tracker_metric_reason_rejects_noise) {
    // Values on both sides of the ideal: no bias, no reason.
    SkillTracker t("ball.flick");
    for (int i = 0; i < 10; ++i)
        t.add(R("ball.flick", 50.f, i % 2 == 0).metric("angle", "Angle", i % 2 ? 40.f : 20.f, "°", 30.f), kDay, 1);
    CHECK(t.metricReason().empty());
    // Biased but identical for successes and failures: not what makes the difference.
    SkillTracker u("ball.musty");
    for (int i = 0; i < 10; ++i)
        u.add(R("ball.musty", 50.f, i % 2 == 0).metric("height", "Hauteur", 200.f, "uu", 100.f), kDay, 1);
    CHECK(u.metricReason().empty());
    // Not enough samples.
    SkillTracker v("ball.pinch");
    for (int i = 0; i < 4; ++i) v.add(R("ball.pinch", 20.f, false).metric("h", "Hauteur", 500.f, "uu", 100.f), kDay, 1);
    CHECK(v.metricReason().empty());
    // No ideal: never a reason.
    SkillTracker w("ball.ceiling");
    for (int i = 0; i < 10; ++i)
        w.add(R("ball.ceiling", 20.f, false).metric("h", "Hauteur", 500.f, "uu", 0.f, false), kDay, 1);
    CHECK(w.metricReason().empty());
    // Only failures and a strong bias: reason with the mean value.
    SkillTracker x("mech.halfflip");
    for (int i = 0; i < 6; ++i)
        x.add(R("mech.halfflip", 20.f, false).metric("cancel", "Délai d'annulation", 0.30f, "s", 0.12f), kDay, 1);
    CHECK_EQ(x.metricReason(), std::string("Délai d'annulation : 0.30 s en moyenne, vise 0.12 s"));
}

TEST(coach_tracker_save_load_roundtrip) {
    SkillTracker t("mech.speedflip");
    for (int i = 0; i < 60; ++i) {
        R r("mech.speedflip", static_cast<float>(40 + i % 50), i % 3 != 0);
        r.title("Speedflip")
            .module("mechanics")
            .metric("time", "Temps", 2.f + 0.01f * static_cast<float>(i % 7), "s", 1.95f);
        if (i % 4 == 0) r.tip("me.sf.cancel", "Annule le flip plus tôt.", 2.f);
        if (i % 5 == 0) r.tip("me.sf.angle", "Flip plus en diagonale.", 1.f);
        if (r.r.success) r.record(2.f + 0.01f * static_cast<float>(i % 7), true);
        t.add(r, i < 30 ? "2026-10-12" : kDay, i / 20 + 1);
    }
    Store st;
    t.save(st, "coach.s.0.");
    // The store text format survives a serialize/parse cycle.
    Store st2;
    st2.parse(st.serialize());
    SkillTracker l;
    CHECK(l.load(st2, "coach.s.0."));
    CHECK_EQ(l.id(), t.id());
    CHECK_EQ(l.title(), t.title());
    CHECK_EQ(l.module(), std::string("mechanics"));
    CHECK_EQ(l.attempts(), t.attempts());
    CHECK_EQ(l.successes(), t.successes());
    CHECK_NEAR(l.averageAll(), t.averageAll(), 0.01f);
    CHECK_NEAR(l.averageRecent(), t.averageRecent(), 0.01f);
    CHECK_NEAR(l.bestScore(), t.bestScore(), 1e-4f);
    CHECK_EQ(l.scores().size(), t.scores().size());
    CHECK(l.trend(0.5f) == t.trend(0.5f));
    CHECK_EQ(l.hasRecord(), t.hasRecord());
    CHECK_NEAR(l.record(), t.record(), 1e-4f);
    CHECK_EQ(l.lowerIsBetter(), t.lowerIsBetter());
    CHECK_EQ(l.recordText(), t.recordText());
    CHECK_EQ(l.lastDate(), kDay);
    CHECK_EQ(l.lastSession(), t.lastSession());
    CHECK_EQ(l.mainMistake().code, t.mainMistake().code);
    CHECK_EQ(l.mainMistake().count, t.mainMistake().count);
    CHECK_EQ(l.mainMistake().text, t.mainMistake().text);
    CHECK_EQ(l.mistakes().size(), t.mistakes().size());
    CHECK_EQ(l.metricReason(), t.metricReason());
    int a = 0, b = 0;
    CHECK_NEAR(l.recentSuccessRate(10, &a), t.recentSuccessRate(10, &b), 1e-5f);
    CHECK_EQ(a, b);
}

TEST(coach_tracker_load_rejects_garbage_and_bounds) {
    Store st;
    SkillTracker t;
    CHECK(!t.load(st, "coach.s.0."));  // no id
    st.setString("p.id", "mech.zapdash");
    st.setInt("p.att", -5);
    st.setInt("p.succ", 99);
    st.setString("p.sum", "nope");
    std::string many;
    for (int i = 0; i < 500; ++i) many += (i ? "," : "") + std::to_string(i % 120);
    many += ",abc,,nan";
    st.setString("p.scores", many);
    st.setString("p.outcomes", std::string(300, '1') + "xyz");
    st.setInt("p.tipn", 3);
    st.setString("p.tips", "a,b,c,d,e;;" + std::string(80, 'z'));
    st.setString("p.date", "2026-99-99");
    st.setList("p.m", {"bad", "k\tLabel\tuu\t1\t1\t3\t1,2,3,4,5\t10101", "k2\tL\t\tx\t1\t0\t1\t1"});
    CHECK(t.load(st, "p."));
    CHECK_EQ(t.id(), std::string("mech.zapdash"));
    CHECK_EQ(t.scores().size(), SkillTracker::kScoreHistory);
    for (float s : t.scores()) CHECK(s >= 0.f && s <= 100.f);
    CHECK(t.attempts() >= static_cast<int>(t.scores().size()));
    CHECK(t.successes() <= t.attempts());
    CHECK(t.lastDate().empty());
    CHECK(t.mainMistake().valid());
    int n = 0;
    t.recentSuccessRate(1000, &n);
    CHECK(n <= static_cast<int>(SkillTracker::kScoreHistory));
}
