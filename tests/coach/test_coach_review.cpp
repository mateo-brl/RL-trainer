// Regressions found in review: stable skill names (result titles are per-attempt headlines), launchable plan
// commands for every mechanics id, record precision, records on non-successful attempts, ASCII-safe texts.

#include "tests/coach/CoachTestUtil.h"

#include "core/common/Text.h"
#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachUtil.h"

using namespace rlt;
using namespace coachtest;

namespace {

void sendN(CoachModule& m, const Env& env, int n, const AttemptResult& r) {
    for (int i = 0; i < n; ++i) send(m, env, r);
}

bool panelHas(const DrawList& dl, const std::string& needle) {
    for (const auto& p : dl.panels) {
        if (p.title.find(needle) != std::string::npos) return true;
        for (const auto& l : p.lines)
            if (l.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST(coach_fail_headline_never_renames_the_skill) {
    Env env;
    CoachModule m;
    setup(m, env);
    event(m, env, GameEventKind::ContextChanged);
    // ballmech failDrill: "<mech> : raté" (verdict Fail), then a few graded flicks.
    sendN(m, env, 6, R("ball.flick", 0.f, false).module("ballmech").title("Flick : raté").verdict(Verdict::Fail));
    sendN(m, env, 6, R("mech.speedflip", 92.f, true).module("mechanics").title("Speedflip"));
    // The card header is the attempt headline...
    send(m, env, R("ball.flick", 0.f, false).module("ballmech").title("Flick : raté").verdict(Verdict::Fail));
    CHECK_EQ(m.card().title, std::string("Flick : raté"));
    // ...but the skill keeps its name everywhere else.
    SkillView v;
    CHECK(m.skill("ball.flick", v));
    CHECK_EQ(v.title, std::string("Flick"));
    CHECK(!m.plan().empty());
    CHECK_EQ(m.plan()[0].skill, std::string("ball.flick"));
    CHECK_EQ(m.plan()[0].title, std::string("Flick"));
    for (const auto& line : m.planLines()) CHECK(line.find("raté") == std::string::npos);
    command(m, env, "goal_add", {"focus"});
    CHECK_EQ(m.goals().size(), static_cast<size_t>(1));
    CHECK_EQ(m.goals()[0].label, std::string("10 réussites : Flick"));
    const std::string summary = m.currentSessionSummary();
    CHECK(summary.find("à travailler Flick,") != std::string::npos ||
          summary.find("à travailler Flick.") != std::string::npos);
    CHECK(summary.find("raté") == std::string::npos);
    const Actions a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(chatContains(a, "point fort Speedflip"));
    CHECK(chatContains(a, "à travailler Flick"));
    CHECK(!chatContains(a, "raté"));
    CHECK_EQ(m.sessionHistory().back().weak, std::string("Flick"));

    // Persisted name and goal label survive a reload.
    Store st;
    m.save(st);
    CHECK(st.getString("coach.s.0.title").find("raté") == std::string::npos);
    CoachModule l;
    setup(l, env);
    l.load(st);
    CHECK(l.skill("ball.flick", v));
    CHECK_EQ(v.title, std::string("Flick"));
    CHECK_EQ(l.goals()[0].label, std::string("10 réussites : Flick"));
}

TEST(coach_kickoff_sides_and_shooting_headlines_share_one_name) {
    Env env;
    CoachModule m;
    setup(m, env);
    send(m, env, R("kickoff.diagonal", 80.f, true).module("kickoff").title("Kickoff (diagonale gauche)"));
    send(m, env, R("kickoff.diagonal", 70.f, true).module("kickoff").title("Kickoff (diagonale droite)"));
    send(m, env, R("shoot.flick.musty", 0.f, false).module("shooting").title("Balle perdue avant le flick"));
    send(m, env, R("shoot.flick.musty", 75.f, true).module("shooting").title("Musty flick, demandé : Flick avant"));
    send(m, env, R("shoot.target.tl", 75.f, true).module("shooting").title("Cible lucarne gauche (Musty flick)"));
    SkillView v;
    CHECK(m.skill("kickoff.diagonal", v));
    CHECK_EQ(v.title, std::string("Kickoff (diagonale)"));
    CHECK(m.skill("shoot.flick.musty", v));
    CHECK_EQ(v.title, std::string("Flick (musty)"));
    CHECK(m.skill("shoot.target.tl", v));
    CHECK_EQ(v.title, std::string("Tir cadré (lucarne gauche)"));
    // The skill table order no longer depends on the latest headline.
    const auto before = m.skills();
    send(m, env, R("kickoff.diagonal", 60.f, true).module("kickoff").title("Kickoff (diagonale gauche)"));
    const auto after = m.skills();
    CHECK_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size() && i < after.size(); ++i) CHECK_EQ(before[i].id, after[i].id);
    // Workshop maps (unknown to the catalog) are named by the maps module.
    send(m, env, R("map.lethamyr_rings", 80.f, true).module("maps").title("Lethamyr Rings"));
    CHECK(m.skill("map.lethamyr_rings", v));
    CHECK_EQ(v.title, std::string("Lethamyr Rings"));
}

TEST(coach_plan_launches_compound_mechanics) {
    Env env;
    CoachModule m;
    setup(m, env);
    // Detected in freeplay by the mechanics module, no drill of its own.
    sendN(m, env, 12, R("mech.doublejump_aerial", 20.f, false).module("mechanics").title("Double jump aerial"));
    sendN(m, env, 12, R("mech.speedflip", 95.f, true).module("mechanics").title("Speedflip"));
    CHECK(!m.plan().empty());
    CHECK_EQ(m.plan()[0].skill, std::string("mech.doublejump_aerial"));
    CHECK_EQ(m.plan()[0].launch.consoleLine(), std::string("rlt mechanics drill fastaerial"));
    SkillView v;
    CHECK(m.skill("mech.doublejump_aerial", v));
    CHECK(v.launchable);
    // Unknown mechanics stay in the stats but are never recommended.
    sendN(m, env, 12, R("mech.brand_new", 5.f, false).module("mechanics").title("Nouvelle méca"));
    CHECK(m.skill("mech.brand_new", v));
    CHECK(!v.launchable);
    CHECK_EQ(v.title, std::string("Brand new"));  // only failed attempts so far: name from the id
    send(m, env, R("mech.brand_new", 80.f, true).module("mechanics").title("Nouvelle méca"));
    CHECK(m.skill("mech.brand_new", v));
    CHECK_EQ(v.title, std::string("Nouvelle méca"));
    for (const auto& it : m.plan()) CHECK(it.skill != "mech.brand_new");
}

TEST(coach_map_record_keeps_centiseconds) {
    Env env;
    env.settings.coach.recordAnnounceAll = true;  // the maps module announces its own records by default
    CoachModule m;
    setup(m, env);
    auto run = [](float seconds) {
        return R("map.lethamyr_rings", 80.f, true)
            .module("maps")
            .title("Lethamyr Rings")
            .metric("time", "Temps", seconds, "s", 0.f, false)
            .record(seconds, true);
    };
    Actions a = send(m, env, run(124.80f));
    CHECK(a.toasts.empty());
    a = send(m, env, run(124.62f));
    CHECK(toastContains(a, "Lethamyr Rings : 2:04.62 (avant 2:04.80)"));
    CHECK(chatContains(a, "Nouveau record : Lethamyr Rings 2:04.62 (avant 2:04.80)"));
    CHECK_EQ(m.card().recordLine, std::string("Nouveau record ! 2:04.62 (avant 2:04.80)"));
    SkillView v;
    CHECK(m.skill("map.lethamyr_rings", v));
    CHECK_EQ(v.recordText, std::string("2:04.62"));
    // A record beaten by less than a centisecond still shows two different values.
    a = send(m, env, run(124.617f));
    CHECK(chatContains(a, "2:04.617 (avant 2:04.620)"));
    // Short times keep two decimals.
    a = send(m, env, R("mech.speedflip", 80.f, true).record(12.38f, true));
    a = send(m, env, R("mech.speedflip", 80.f, true).record(12.34f, true));
    CHECK(chatContains(a, "12.34 s (avant 12.38 s)"));
}

TEST(coach_record_counts_without_success) {
    Env env;
    env.settings.coach.recordAnnounceAll = true;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"kickoff_pb"});
    Actions a = send(m, env, R("kickoff.diagonal", 80.f, true).module("kickoff").record(2.45f, true));
    CHECK(a.toasts.empty() || !toastContains(a, "Nouveau record"));
    // Faster but graded 65 (not a success for the kickoff module): still the record, as in the kickoff tab.
    a = send(m, env, R("kickoff.diagonal", 65.f, false).module("kickoff").record(2.20f, true));
    CHECK(chatContains(a, "Nouveau record : Kickoff (diagonale) 2.20 s (avant 2.45 s)"));
    CHECK(toastContains(a, "Objectif atteint !"));
    CHECK(m.goals()[0].done);
    SkillView v;
    CHECK(m.skill("kickoff.diagonal", v));
    CHECK_EQ(v.recordText, std::string("2.20 s"));
    // ballmech failDrill placeholder (failed, value 0): ignored.
    send(m, env, R("ball.flick", 0.f, false).module("ballmech").verdict(Verdict::Fail).record(0.f, false));
    CHECK(m.skill("ball.flick", v));
    CHECK(!v.hasRecord);
    send(m, env, R("ball.flick", 70.f, true).module("ballmech").record(70.f, false));
    a = send(m, env, R("ball.flick", 0.f, false).module("ballmech").verdict(Verdict::Fail).record(0.f, false));
    CHECK(!chatContains(a, "Nouveau record"));
    CHECK(m.skill("ball.flick", v));
    CHECK(v.hasRecord);
    CHECK_NEAR(v.record, 70.f, 1e-4f);
}

TEST(coach_hud_and_chat_texts_fold_cleanly) {
    Env env;
    CoachModule m;
    setup(m, env);
    send(m, env,
         R("mech.speedflip", 82.f, true)
             .title("Speedflip")
             .metric("angle", "Angle du flip", 38.f, "°", 30.f)
             .metric("time", "Temps au ballon", 2.05f, "s", 1.95f));
    CHECK_EQ(m.card().metricLines[0], std::string("Angle du flip : 38° (idéal 30°)"));
    CHECK_EQ(hudText(m.card().metricLines[0], false), std::string("Angle du flip : 38 deg (ideal 30 deg)"));
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK(panelHas(dl, "38° (idéal 30°)"));
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines) CHECK(hudText(l.text, false).find("  ") == std::string::npos);
    // Preview card uses the same layout.
    command(m, env, "preview");
    for (const auto& line : m.card().metricLines) CHECK(hudText(line, false).find("  ") == std::string::npos);
    // Commands never print a degree-like sign in chat.
    for (const char* c : {"new_session", "plan", "summary", "goal_add"}) {
        const Actions a = command(m, env, c);
        for (const auto& msg : a.chat) {
            const std::string folded = hudText(msg.text, false);
            CHECK(folded.find(" deg") == std::string::npos);
            CHECK(folded.find("  ") == std::string::npos);
        }
    }
}

TEST(coach_records_not_repeated_for_self_announcing_modules) {
    Env env;
    CoachModule m;
    setup(m, env);
    CHECK(!m.config().recordAnnounceAll);
    for (const char* mod : {"mechanics", "kickoff", "courses", "shooting", "maps"}) CHECK(coach::moduleAnnouncesRecords(mod));
    CHECK(!coach::moduleAnnouncesRecords("firsttouch"));
    CHECK(!coach::moduleAnnouncesRecords("ballmech"));
    // The mechanics module toasts its own speedflip record: the coach stays quiet but still counts and shows it.
    send(m, env, R("mech.speedflip", 80.f, true).module("mechanics").record(2.05f, true));
    Actions a = send(m, env, R("mech.speedflip", 90.f, true).module("mechanics").record(1.92f, true));
    CHECK(!toastContains(a, "Nouveau record"));
    CHECK(!chatContains(a, "Nouveau record"));
    CHECK(a.requestSave);
    CHECK(m.card().newRecord);
    CHECK_EQ(m.card().recordLine, std::string("Nouveau record ! 1.92 s (avant 2.05 s)"));
    CHECK_EQ(m.overview().sessionRecords, 1);
    // Ball mechanics do not announce theirs: the coach does.
    send(m, env, R("ball.aerial", 60.f, true).module("ballmech").record(60.f, false));
    a = send(m, env, R("ball.aerial", 75.f, true).module("ballmech").record(75.f, false));
    CHECK(toastContains(a, "Tir aérien : 75 (avant 60)"));
    CHECK(chatContains(a, "Nouveau record : Tir aérien 75 (avant 60)"));
    // Opt-in: announce everything.
    env.settings.coach.recordAnnounceAll = true;
    setup(m, env);
    a = send(m, env, R("mech.speedflip", 95.f, true).module("mechanics").record(1.90f, true));
    CHECK(toastContains(a, "Speedflip : 1.90 s (avant 1.92 s)"));
    CHECK(chatContains(a, "Nouveau record : Speedflip 1.90 s (avant 1.92 s)"));
}
