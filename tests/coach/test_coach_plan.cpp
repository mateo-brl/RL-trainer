// Catalog (families, titles, launch commands) and the training plan ranking.

#include "tests/coach/CoachTestUtil.h"

#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachPlan.h"

using namespace rlt;
using namespace rlt::coach;

namespace {

PlanCandidate cand(const std::string& skill, float avg, int attempts = 30, int days = 0) {
    PlanCandidate c;
    c.skill = skill;
    c.title = defaultSkillTitle(skill);
    c.attempts = attempts;
    c.recentAverage = avg;
    c.recentSuccess = avg / 100.f;
    c.daysSince = days;
    c.trend = Trend::Stable;
    return c;
}

bool argsAre(const LaunchSpec& l, const std::string& module, const std::string& command, const std::string& arg) {
    return l.module == module && l.command == command && l.args.size() == 1 && l.args[0] == arg;
}

} // namespace

TEST(coach_catalog_families_and_titles) {
    CHECK_EQ(skillFamily("mech.speedflip"), std::string("mech"));
    CHECK_EQ(skillFamily("course.rings.air"), std::string("course"));
    CHECK_EQ(skillFamily("kickoff"), std::string("kickoff"));
    CHECK_EQ(skillFamily(""), std::string("autre"));
    CHECK_EQ(skillFamily(".weird"), std::string("autre"));
    CHECK_EQ(familyLabel("first_touch"), std::string("Premières touches"));
    CHECK_EQ(familyLabel("mech"), std::string("Mécas mouvement"));
    CHECK_EQ(familyLabel("ball"), std::string("Mécas balle"));
    CHECK_EQ(familyLabel("brand_new"), std::string("Brand new"));
    CHECK(familyOrder("first_touch") < familyOrder("mech"));
    CHECK(familyOrder("mech") < familyOrder("zzz"));
    CHECK_EQ(defaultSkillTitle("mech.halfflip"), std::string("Half-flip"));
    CHECK_EQ(defaultSkillTitle("ball.aerial"), std::string("Tir aérien"));
    CHECK_EQ(defaultSkillTitle("first_touch.roll_in"), std::string("Première touche (balle qui roule)"));
    CHECK_EQ(defaultSkillTitle("first_touch.new_type"), std::string("Première touche (new type)"));
    CHECK_EQ(defaultSkillTitle("kickoff.diagonal"), std::string("Kickoff (diagonale)"));
    CHECK_EQ(defaultSkillTitle("course.rings.airdribble"), std::string("Anneaux (air dribble)"));
    CHECK_EQ(defaultSkillTitle("course.control.slalom"), std::string("Contrôle (slalom)"));
    CHECK_EQ(defaultSkillTitle("map.12345"), std::string("Map 12345"));
    CHECK_EQ(defaultSkillTitle("future.thing_x"), std::string("Thing x"));
    // Ids actually emitted by the other modules have stable catalog names.
    CHECK_EQ(defaultSkillTitle("kickoff.decale"), std::string("Kickoff (décalé)"));
    CHECK_EQ(defaultSkillTitle("kickoff.centre"), std::string("Kickoff (centre)"));
    CHECK_EQ(defaultSkillTitle("mech.doublejump_aerial"), std::string("Double jump aerial"));
    CHECK_EQ(defaultSkillTitle("mech.flipcancel"), std::string("Flip cancel"));
    CHECK_EQ(defaultSkillTitle("shoot.flick.45"), std::string("Flick (45)"));
    CHECK_EQ(defaultSkillTitle("shoot.flick.any"), std::string("Flick (tous types)"));
    CHECK_EQ(defaultSkillTitle("shoot.target.tl"), std::string("Tir cadré (lucarne gauche)"));
    CHECK_EQ(defaultSkillTitle("shoot.power"), std::string("Puissance de frappe"));
    std::string name;
    CHECK(catalogSkillTitle("ball.flick", name) && name == "Flick");
    CHECK(catalogSkillTitle("first_touch.whatever", name));
    CHECK(!catalogSkillTitle("map.123", name));
    CHECK(!catalogSkillTitle("mech.brand_new", name));
    CHECK(!catalogSkillTitle("future.thing", name));
}

TEST(coach_catalog_launch_table) {
    CHECK(argsAre(launchForSkill("first_touch.ground"), "firsttouch", "drill", "roll_in"));
    CHECK(argsAre(launchForSkill("first_touch.dribble"), "firsttouch", "drill", "roll_in"));
    CHECK(argsAre(launchForSkill("first_touch.bounce"), "firsttouch", "drill", "lob"));
    CHECK(argsAre(launchForSkill("first_touch.air"), "firsttouch", "drill", "high"));
    CHECK(argsAre(launchForSkill("first_touch.wall"), "firsttouch", "drill", "wall"));
    CHECK(argsAre(launchForSkill("first_touch.whatever"), "firsttouch", "drill", "roll_in"));  // never the Mix
    CHECK(argsAre(launchForSkill("mech.speedflip"), "mechanics", "drill", "speedflip"));
    CHECK(argsAre(launchForSkill("mech.wavedash"), "mechanics", "drill", "wavedash"));
    CHECK(argsAre(launchForSkill("ball.flipreset"), "ballmech", "drill", "flipreset"));
    CHECK(argsAre(launchForSkill("ball.musty.left"), "ballmech", "drill", "musty"));
    CHECK(argsAre(launchForSkill("kickoff.diagonal"), "kickoff", "start", "weakness"));
    CHECK(argsAre(launchForSkill("course.rings.air"), "courses", "rings", "air"));
    CHECK(argsAre(launchForSkill("course.rings.ground"), "courses", "rings", "ground"));
    CHECK(argsAre(launchForSkill("course.rings.airdribble"), "courses", "rings", "airdribble"));
    CHECK(argsAre(launchForSkill("course.rings.weird"), "courses", "rings", "ground"));
    CHECK(argsAre(launchForSkill("course.control.slalom"), "courses", "control", "slalom"));
    CHECK(argsAre(launchForSkill("course.control.catch"), "courses", "control", "catch"));
    CHECK(argsAre(launchForSkill("course.control.x"), "courses", "control", "dribble"));
    CHECK(argsAre(launchForSkill("shoot.flick.musty"), "shooting", "flick", "any"));
    CHECK(argsAre(launchForSkill("shoot.target.top_left"), "shooting", "targets", "random"));
    // Every mechanics drill id, and the compound mechanics detected in freeplay mapped to the closest drill
    // (ids accepted by "rlt mechanics drill").
    for (const char* id : {"speedflip", "halfflip", "flipcancel", "wavedash", "chaindash", "zapdash", "walldash",
                           "fastaerial", "stall", "helicopter", "recovery", "powerslide", "boost"})
        CHECK(argsAre(launchForSkill(std::string("mech.") + id), "mechanics", "drill", id));
    CHECK(argsAre(launchForSkill("mech.ceilingdash"), "mechanics", "drill", "walldash"));
    CHECK(argsAre(launchForSkill("mech.walldash_chain"), "mechanics", "drill", "walldash"));
    CHECK(argsAre(launchForSkill("mech.speedzap"), "mechanics", "drill", "zapdash"));
    CHECK(argsAre(launchForSkill("mech.halfflip_wavedash"), "mechanics", "drill", "halfflip"));
    CHECK(argsAre(launchForSkill("mech.doublejump_aerial"), "mechanics", "drill", "fastaerial"));
    CHECK_EQ(launchForSkill("mech.doublejump_aerial").label, std::string("Exercice Fast aerial"));
    CHECK(!launchForSkill("mech.brand_new").valid());  // unknown mechanics are not launchable
    for (const char* id : {"flipreset", "ceiling", "flick", "musty", "breezi", "doubletap", "airdribble", "airroll",
                           "aerial", "pinch", "powershot"})
        CHECK(argsAre(launchForSkill(std::string("ball.") + id), "ballmech", "drill", id));
    CHECK(!launchForSkill("ball.brand_new").valid());
    const LaunchSpec power = launchForSkill("shoot.power");
    CHECK(power.valid());
    CHECK_EQ(power.key(), std::string("shooting power"));
    CHECK(!launchForSkill("map.123").valid());
    CHECK(!launchForSkill("unknown.skill").valid());
    CHECK(!launchForSkill("mech").valid());
    CHECK(!launchForSkill("").valid());
    const LaunchSpec l = launchForSkill("mech.speedflip");
    CHECK_EQ(l.key(), std::string("mechanics drill speedflip"));
    CHECK_EQ(l.consoleLine(), std::string("rlt mechanics drill speedflip"));
    CHECK(!l.label.empty());
}

TEST(coach_plan_starter_for_new_player) {
    const auto plan = buildPlan({}, 3, PlanWeights{});
    CHECK_EQ(plan.size(), static_cast<size_t>(3));
    CHECK(argsAre(plan[0].launch, "firsttouch", "drill", "roll_in"));
    CHECK(argsAre(plan[1].launch, "kickoff", "start", "random"));
    CHECK(argsAre(plan[2].launch, "mechanics", "drill", "recovery"));  // a Débutant drill, not the speedflip
    for (const auto& it : plan) {
        CHECK(it.starter);
        CHECK(!it.reason.empty());
        CHECK(!it.title.empty());
    }
    CHECK_EQ(buildPlan({}, 1, PlanWeights{}).size(), static_cast<size_t>(1));
    CHECK(buildPlan({}, 0, PlanWeights{}).empty());
}

TEST(coach_plan_weakness_ranks_first) {
    std::vector<PlanCandidate> c = {cand("mech.speedflip", 85.f), cand("ball.aerial", 40.f),
                                    cand("kickoff.diagonal", 65.f)};
    const auto plan = buildPlan(c, 3, PlanWeights{});
    CHECK_EQ(plan.size(), static_cast<size_t>(3));
    CHECK_EQ(plan[0].skill, std::string("ball.aerial"));
    CHECK_EQ(plan[1].skill, std::string("kickoff.diagonal"));
    CHECK_EQ(plan[2].skill, std::string("mech.speedflip"));
    CHECK(plan[0].need > plan[1].need && plan[1].need > plan[2].need);
    CHECK_MSG(plan[0].reason.find("Moyenne récente 40/100") == 0, plan[0].reason);
    CHECK_MSG(plan[0].reason.find("réussite 40 %") != std::string::npos, plan[0].reason);
    CHECK(!plan[0].starter);
}

TEST(coach_plan_spacing_beats_small_weakness) {
    // Same weakness, but speedflip was not practised for 5 days.
    std::vector<PlanCandidate> c = {cand("ball.aerial", 75.f, 30, 0), cand("mech.speedflip", 75.f, 30, 5)};
    auto plan = buildPlan(c, 2, PlanWeights{});
    CHECK_EQ(plan[0].skill, std::string("mech.speedflip"));
    CHECK_MSG(plan[0].reason.find("Pas pratiqué depuis 5 jours") == 0, plan[0].reason);
    // A clearly weaker skill still wins over a short break.
    c = {cand("ball.aerial", 30.f, 30, 0), cand("mech.speedflip", 80.f, 30, 2)};
    plan = buildPlan(c, 2, PlanWeights{});
    CHECK_EQ(plan[0].skill, std::string("ball.aerial"));
    // Spacing is capped (10 days) and can be switched off.
    PlanWeights noSpacing;
    noSpacing.spacing = 0.f;
    c = {cand("ball.aerial", 60.f, 30, 0), cand("mech.speedflip", 75.f, 30, 30)};
    plan = buildPlan(c, 2, noSpacing);
    CHECK_EQ(plan[0].skill, std::string("ball.aerial"));
    const NeedParts capped = computeNeed(cand("mech.speedflip", 75.f, 30, 300), PlanWeights{});
    CHECK_NEAR(capped.spacing, 0.8f, 1e-5f);
    // A weakness-led reason mentions a long break as a side note.
    PlanCandidate w = cand("ball.aerial", 20.f, 30, 4);
    CHECK_MSG(planReason(w, computeNeed(w, PlanWeights{})).find("(pas pratiqué depuis 4 jours)") != std::string::npos,
              planReason(w, computeNeed(w, PlanWeights{})));
}

TEST(coach_plan_recurring_mistake_reason) {
    PlanCandidate a = cand("first_touch.ground", 60.f);
    a.mistake.code = "ft.too_fast";
    a.mistake.text = "Laisse la balle venir.";
    a.mistake.count = 25;
    a.mistake.window = 30;
    // Make the mistake dominant: lower its weakness contribution.
    a.recentAverage = 70.f;
    PlanCandidate b = cand("mech.speedflip", 72.f);
    const auto plan = buildPlan({b, a}, 2, PlanWeights{});
    CHECK_EQ(plan[0].skill, std::string("first_touch.ground"));
    CHECK_MSG(plan[0].reason.find("Erreur fréquente (25 essais sur 30) : Laisse la balle venir.") == 0, plan[0].reason);
    // A one-off mistake does not count.
    PlanCandidate once = cand("first_touch.ground", 70.f);
    once.mistake = a.mistake;
    once.mistake.count = 1;
    CHECK_NEAR(computeNeed(once, PlanWeights{}).mistake, 0.f, 1e-6f);
}

TEST(coach_plan_metric_reason_when_weak) {
    PlanCandidate a = cand("first_touch.ground", 35.f);
    a.metricReason = "Écart de vitesse : +260 uu/s en moyenne, vise 0 uu/s";
    const auto plan = buildPlan({a}, 1, PlanWeights{});
    CHECK_EQ(plan[0].reason, a.metricReason);
}

TEST(coach_plan_low_sample_bonus) {
    PlanCandidate few = cand("ball.ceiling", 80.f, 2);
    PlanCandidate many = cand("ball.pinch", 66.f, 50);
    const auto plan = buildPlan({many, few}, 1, PlanWeights{});
    CHECK_EQ(plan[0].skill, std::string("ball.ceiling"));
    CHECK_MSG(plan[0].reason.find("Seulement 2 essais") == 0, plan[0].reason);
    CHECK_NEAR(computeNeed(cand("x.y", 50.f, 10), PlanWeights{}).sample, 0.f, 1e-6f);
}

TEST(coach_plan_variety_across_families) {
    // Two weak movement mechanics, one average kickoff: one family each before a second mech.
    std::vector<PlanCandidate> c = {cand("mech.wavedash", 30.f), cand("mech.halfflip", 35.f),
                                    cand("kickoff.diagonal", 70.f)};
    auto plan = buildPlan(c, 3, PlanWeights{});
    CHECK_EQ(plan.size(), static_cast<size_t>(3));
    CHECK_EQ(plan[0].skill, std::string("mech.wavedash"));
    CHECK_EQ(plan[1].skill, std::string("kickoff.diagonal"));
    CHECK_EQ(plan[2].skill, std::string("mech.halfflip"));
    // Plan of 2: no second mech, the variety pass wins.
    plan = buildPlan(c, 2, PlanWeights{});
    CHECK_EQ(plan[1].skill, std::string("kickoff.diagonal"));
}

TEST(coach_plan_never_same_exercise_twice_and_fills_with_starter) {
    // Sol and dribble both launch "firsttouch drill roll_in" (also the starter's first touch): only one of them.
    std::vector<PlanCandidate> c = {cand("first_touch.ground", 20.f), cand("first_touch.dribble", 25.f),
                                    cand("map.999", 5.f)};
    const auto plan = buildPlan(c, 3, PlanWeights{});
    CHECK_EQ(plan.size(), static_cast<size_t>(3));
    CHECK_EQ(plan[0].skill, std::string("first_touch.ground"));
    std::vector<std::string> keys;
    for (const auto& it : plan) {
        CHECK(it.skill != "map.999");  // not launchable
        for (const auto& k : keys) CHECK(k != it.launch.key());
        keys.push_back(it.launch.key());
    }
    // The other slots come from the starter plan, new families first.
    CHECK(plan[1].starter && plan[1].family == "kickoff");
    CHECK(plan[2].starter && plan[2].family == "mech");
}

TEST(coach_plan_deterministic_ties) {
    std::vector<PlanCandidate> c = {cand("mech.stall", 50.f, 30), cand("ball.pinch", 50.f, 20),
                                    cand("kickoff.center", 50.f, 20)};
    const auto plan = buildPlan(c, 3, PlanWeights{});
    // Equal need: fewer attempts first, then id order.
    CHECK_EQ(plan[0].skill, std::string("ball.pinch"));
    CHECK_EQ(plan[1].skill, std::string("kickoff.center"));
    CHECK_EQ(plan[2].skill, std::string("mech.stall"));
}
