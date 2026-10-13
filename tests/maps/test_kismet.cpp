// Kismet profiles (id / signature matching), timer parsing and generic heuristics.
#include "tests/TestFramework.h"

#include "core/modules/maps/KismetProfiles.h"
#include "tests/maps/MapsTestUtil.h"

using namespace rlt;
using namespace maps_test;

TEST(maps_profile_by_workshop_id) {
    const KismetProfile* p = kismetProfileById("2025564083");
    CHECK(p != nullptr && p->key == "leth_rings");
    CHECK(kismetProfileById("2468375059") == p);  // Neon Heights: same family
    CHECK(kismetProfileById("1694288506")->key == "dribble2_overhaul");
    CHECK(kismetProfileById("2100268170")->key == "ad_hoops");
    CHECK(kismetProfileById("1768760178")->key == "sjr1");
    CHECK(kismetProfileById("2779629941")->key == "sjr3");
    CHECK(kismetProfileById("") == nullptr);
    CHECK(kismetProfileById("964271505") == nullptr);  // Dribbling Challenge #2: no Kismet profile known
    CHECK(findKismetProfile("timing_level") != nullptr);
    CHECK(findKismetProfile("nope") == nullptr);
    for (const auto& prof : kismetProfiles()) {
        CHECK(prof.usable());
        CHECK(!prof.label.empty());
        CHECK(!prof.signature().empty());
    }
}

TEST(maps_profile_by_signature) {
    // Leth family: Timing + Level + Deaths.
    const KismetProfile* p = kismetProfileBySignature(lethVars(false, 1));
    CHECK(p != nullptr && p->key == "leth_rings");
    // Timing + Level only: generic family, never Dribble 2 Overhaul (id only).
    p = kismetProfileBySignature({kBool("Timing", false), kInt("Level", 1)});
    CHECK(p != nullptr && p->key == "timing_level");
    CHECK(p->finalLevel == 0);
    // Air Dribble Hoops.
    p = kismetProfileBySignature({kBool("Timer", false), kInt("Level", 1), kInt("Other", 3)});
    CHECK(p != nullptr && p->key == "ad_hoops");
    // SJR3 is more specific than SJR2.
    p = kismetProfileBySignature({kStr("displaytimer", "00:00"), kInt("Level", 1)});
    CHECK(p != nullptr && p->key == "sjr3");
    p = kismetProfileBySignature({kStr("displaytimer", "00:00")});
    CHECK(p != nullptr && p->key == "sjr2");
    p = kismetProfileBySignature({kInt("Rings", 0)});
    CHECK(p != nullptr && p->key == "sjr1");
    p = kismetProfileBySignature({kInt("Player1CPCount", 0), kInt("Player1Count", 0)});
    CHECK(p != nullptr && p->key == "panic_air_race");
    p = kismetProfileBySignature({kInt("ankh", 0)});
    CHECK(p != nullptr && p->key == "egyptian_tomb");
}

TEST(maps_profile_signature_rejects_wrong_types_and_names) {
    CHECK(kismetProfileBySignature({}) == nullptr);
    CHECK(kismetProfileBySignature({kInt("Timing", 1), kInt("Level", 1)}) == nullptr);   // Timing must be a bool
    CHECK(kismetProfileBySignature({kBool("Timing", true), kFloat("Level", 1.f)}) == nullptr);
    CHECK(kismetProfileBySignature({kBool("timing", true), kInt("level", 1)}) == nullptr);  // names are exact
    CHECK(kismetProfileBySignature({kStr("Rings", "3")}) == nullptr);
    CHECK(kismetProfileBySignature({kInt("Score", 3), kBool("Started", true)}) == nullptr);
    // Leth needs Deaths as int.
    const KismetProfile* p = kismetProfileBySignature({kBool("Timing", true), kInt("Level", 1), kStr("Deaths", "0")});
    CHECK(p != nullptr && p->key == "timing_level");
    CHECK(kismetSignatureMatches(*findKismetProfile("leth_rings"), lethVars(true, 3)));
    CHECK(!kismetSignatureMatches(*findKismetProfile("sjr1"), lethVars(true, 3)));
}

TEST(maps_kismet_value_access) {
    const auto vars = std::vector<KismetVar>{kInt("A", 4), kBool("B", true), kStr("C", "x"), kFloat("D", 2.5f)};
    int i = 0;
    bool b = false;
    std::string s;
    float f = 0.f;
    CHECK(kismetInt(vars, "A", i) && i == 4);
    CHECK(!kismetInt(vars, "B", i));
    CHECK(kismetBool(vars, "B", b) && b);
    CHECK(kismetString(vars, "C", s) && s == "x");
    CHECK(kismetNumber(vars, "D", f) && f == 2.5f);
    CHECK(kismetNumber(vars, "A", f) && f == 4.f);
    CHECK(!kismetNumber(vars, "C", f));
    CHECK(findKismetVar(vars, "") == nullptr);
    CHECK(findKismetVar(vars, "Z") == nullptr);
    CHECK(kismetValueText(vars[1]) == "vrai");
    CHECK(kismetValueText(vars[3]) == "2.50");
    KismetVar longStr = kStr("log", std::string(300, 'x'));
    CHECK(kismetValueText(longStr).size() == 64);
    CHECK(std::string(kismetTypeLabel(KismetVar::Type::String)) == "texte");
}

TEST(maps_timer_text_parsing) {
    float s = -1.f;
    CHECK(parseTimerText("00:00", s) && s == 0.f);
    CHECK(parseTimerText("1:05", s) && s == 65.f);
    CHECK(parseTimerText("12:34", s) && s == 754.f);
    CHECK(parseTimerText(" 2:03.5 ", s) && std::fabs(s - 123.5f) < 1e-4f);
    CHECK(parseTimerText("1:02:03", s) && s == 3723.f);
    CHECK(parseTimerText("0:59.99", s) && std::fabs(s - 59.99f) < 1e-3f);
    CHECK(!parseTimerText("", s));
    CHECK(!parseTimerText("12", s));
    CHECK(!parseTimerText("1:5", s));
    CHECK(!parseTimerText("1:60", s));
    CHECK(!parseTimerText("a:00", s));
    CHECK(!parseTimerText("1:00:", s));
    CHECK(!parseTimerText(":30", s));
    CHECK(!parseTimerText("1:00.", s));
    CHECK(!parseTimerText("Time 1:00", s));
    CHECK(looksLikeTimer("3:07"));
    CHECK(!looksLikeTimer("Level 3"));
}

TEST(maps_level_like_names) {
    CHECK(isLevelLikeName("Level"));
    CHECK(isLevelLikeName("CurrentCheckpoint"));
    CHECK(isLevelLikeName("Player1CPCount"));
    CHECK(isLevelLikeName("Rings"));
    CHECK(isLevelLikeName("NIVEAU"));
    CHECK(!isLevelLikeName("Deaths"));
    CHECK(!isLevelLikeName("Seconds"));
}

TEST(maps_watcher_counts_patterns) {
    KismetWatcher w;
    w.update({kInt("Level", 1), kBool("Go", false), kStr("clock", "0:00"), kInt("Seconds", 0), kInt("Noise", 5)}, 0.f);
    CHECK(w.variableCount() == 5);
    CHECK(w.bestLevelCandidate() == nullptr);  // nothing moved yet
    w.update({kInt("Level", 2), kBool("Go", true), kStr("clock", "0:01"), kInt("Seconds", 1), kInt("Noise", 9)}, 1.f);
    w.update({kInt("Level", 3), kBool("Go", true), kStr("clock", "0:02"), kInt("Seconds", 2), kInt("Noise", 2)}, 2.f);
    w.update({kInt("Level", 1), kBool("Go", false), kStr("clock", "0:03"), kInt("Seconds", 3), kInt("Noise", 7)}, 3.f);
    const KismetCandidate* lvl = w.find("Level");
    CHECK(lvl != nullptr && lvl->plusOneSteps == 2 && lvl->resets == 1 && lvl->otherChanges == 0);
    CHECK(lvl->value == "1");
    CHECK(lvl->lastChange == 3.f);
    const KismetCandidate* go = w.find("Go");
    CHECK(go != nullptr && go->risingEdges == 1 && go->fallingEdges == 1);
    const KismetCandidate* best = w.bestLevelCandidate();
    CHECK(best != nullptr && best->name == "Level");
    // "Seconds" steps +1 too but is a time counter: never a checkpoint, even with anyName.
    const KismetCandidate* anyBest = w.bestLevelCandidate(true);
    CHECK(anyBest != nullptr && anyBest->name == "Level");
    const KismetCandidate* timer = w.timerCandidate();
    CHECK(timer != nullptr && timer->name == "clock" && timer->timerTicks == 3);
    const KismetCandidate* start = w.startCandidate();
    CHECK(start != nullptr && start->name == "Go");
    CHECK(w.find("Noise")->plusOneSteps == 0);
    CHECK(w.samples() == 4);
    w.reset();
    CHECK(w.variableCount() == 0 && w.find("Level") == nullptr);
}

TEST(maps_watcher_handles_type_change_and_missing_vars) {
    KismetWatcher w;
    w.update({kInt("X", 1)}, 0.f);
    w.update({kInt("X", 2)}, 1.f);
    CHECK(w.find("X")->plusOneSteps == 1);
    w.update({kStr("X", "hello")}, 2.f);  // reused name with another type: counters restart
    CHECK(w.find("X")->type == KismetVar::Type::String);
    CHECK(w.find("X")->plusOneSteps == 0);
    w.update({}, 3.f);  // missing variables keep their last value
    CHECK(w.find("X")->value == "hello");
    // An int that jumps around is not a good checkpoint candidate.
    KismetWatcher j;
    j.update({kInt("LevelScore", 0)}, 0.f);
    j.update({kInt("LevelScore", 1)}, 0.1f);
    j.update({kInt("LevelScore", 7)}, 0.2f);
    j.update({kInt("LevelScore", 3)}, 0.3f);
    CHECK(j.bestLevelCandidate() == nullptr);
}

TEST(maps_watcher_float_changes) {
    KismetWatcher w;
    w.update({kFloat("Speed", 1.f)}, 0.f);
    w.update({kFloat("Speed", 1.f)}, 0.1f);
    CHECK(w.find("Speed")->otherChanges == 0);
    w.update({kFloat("Speed", 2.f)}, 0.2f);
    CHECK(w.find("Speed")->otherChanges == 1);
    CHECK(w.find("Speed")->value == "2.00");
}
