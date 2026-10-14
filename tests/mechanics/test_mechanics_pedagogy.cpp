// Pedagogy and compact HUD of the movement mechanics: drill levels a beginner can pass (speedflip first step
// without the kickoff), demanding top levels, one tip in words per attempt, live step of the exercise, at most three
// main HUD lines without milliseconds or degrees, difficulty tags and plain texts of the catalogue.
#include <iterator>
#include <memory>
#include <string>

#include "tests/mechanics/MechRig.h"

#include "core/modules/mechanics/Presentation.h"

using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;

namespace {

// Speedflip from a standstill: boost, jump held 11 ticks, dodge `jumpToFlip` ticks after the jump at `angleDeg` from
// the nose (negative: left), stick pulled back `cancelAfter` ticks after the dodge, then back on the wheels.
Script speedflipAttempt(float angleDeg, int jumpToFlip, int cancelAfter) {
    auto passedInverted = std::make_shared<bool>(false);
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        const int J = 40, D = J + jumpToFlip;
        if (t >= J && t < J + 11) in.jump = true;
        if (t >= D && t < D + cancelAfter) {
            in.jump = t < D + 3;
            const float a = angleDeg * kDegToRad;
            in.yaw = std::sin(a);
            in.pitch = -std::cos(a);
        } else if (t >= D + cancelAfter) {
            in.pitch = 1.f;
            if (c.up().z < -0.3f) *passedInverted = true;
            in.roll = sgn(angleDeg);
            if (*passedInverted && c.up().z > 0.3f) {
                Inputs pd;
                orientPD(c, Vec3{1.f, 0.f, 0.f}, pd);
                in.roll = pd.roll;
                in.yaw = pd.yaw;
                in.pitch = pd.pitch;
            }
            if (c.wheelCount() >= 3) {
                in.roll = 0.f;
                in.pitch = 0.f;
                in.yaw = 0.f;
            }
        }
        return in;
    };
}

// Starts the drill of `id` at `level` and runs until GO (get-ready phase included).
void startDrill(Rig& rig, const std::string& id, float level) { CHECK(rig.startDrill(id, level)); }

std::string describe(const Rig& rig) {
    std::string s;
    for (const auto& r : rig.results) {
        s += " [" + r.skill + " " + std::to_string(static_cast<int>(r.score)) + (r.success ? " OK" : " FAIL");
        for (const auto& t : r.tips) s += " " + t.code;
        s += "]";
    }
    return s.empty() ? std::string(" (no result)") : s;
}

DrawList drawHud(Rig& rig, bool compact = true) {
    DrawList dl;
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    DrawContext dctx{rig.settings, rig.game, rig.profile, rig.last, true, rig.host.ownsActivity(&rig.mod), 1.f, compact};
    rig.mod.draw(view, dctx, dl);
    return dl;
}

std::vector<const PanelLine*> mainLines(const DrawPanel& p) {
    std::vector<const PanelLine*> v;
    for (const auto& l : p.lines)
        if (!l.detail) v.push_back(&l);
    return v;
}

bool hasEnDash(const std::string& s) { return s.find("\xE2\x80\x93") != std::string::npos; }
// Milliseconds or degrees: only for detail lines and the tab.
bool technical(const std::string& s) { return s.find(" ms") != std::string::npos || s.find("\xC2\xB0") != std::string::npos; }

// Compact HUD contract: at most three main lines (title apart), plain words, no ms or degrees.
void checkCompact(const DrawList& dl, const std::string& what) {
    CHECK_MSG(dl.panels.size() <= 1, what);
    for (const auto& p : dl.panels) {
        const auto lines = mainLines(p);
        CHECK_MSG(lines.size() <= 3, what + " : " + std::to_string(lines.size()) + " lignes");
        CHECK_MSG(!technical(p.title) && !hasEmDash(p.title) && !hasEnDash(p.title), what + " : " + p.title);
        for (const PanelLine* l : lines) {
            CHECK_MSG(!technical(l->text), what + " : " + l->text);
            CHECK_MSG(!hasEmDash(l->text) && !hasEnDash(l->text), what + " : " + l->text);
        }
    }
}

const char* kFamilies[] = {"speedflip", "halfflip", "flipcancel", "wavedash", "chaindash", "zapdash", "walldash",
                           "fastaerial", "stall", "helicopter", "recovery", "powerslide", "boost"};

} // namespace

// ---- Speedflip progression -----------------------------------------------------------------------------------
TEST(mechanics_new_player_starts_at_level_one) {
    MechanicsModule m;
    for (int i = 0; i < kFocusCount; ++i) CHECK_NEAR(m.level(static_cast<Focus>(i)), 0.f, 1e-6f);
    CHECK(displayLevel(0.f) == 1);
    CHECK(displayLevel(0.2f) == 3);
    CHECK(displayLevel(1.f) == 10);
    const auto rows = m.statRows();
    for (const auto& r : rows) CHECK(r.levelNumber == 1);
    // A saved level is kept.
    Store st;
    st.setFloat("mechanics.level.halfflip.level", 0.6f);
    st.setInt("mechanics.level.halfflip.attempts", 1);
    m.load(st);
    CHECK_NEAR(m.level(Focus::HalfFlip), 0.6f, 1e-6f);
    CHECK_NEAR(m.level(Focus::SpeedFlip), 0.f, 1e-6f);
}

TEST(mechanics_unplayed_old_save_starts_at_niveau_1) {
    // The previous version saved every drill at its old start level (0.2) even when never played.
    Store st;
    st.setFloat("mechanics.level.wavedash.level", 0.2f);
    st.setInt("mechanics.level.wavedash.attempts", 0);
    MechanicsModule m;
    m.load(st);
    CHECK_NEAR(m.level(Focus::Wavedash), 0.f, 1e-6f);
    CHECK(displayLevel(m.level(Focus::Wavedash)) == 1);
}

TEST(mechanics_speedflip_level_one_passes_a_beginner_attempt) {
    // Jump -> flip 150 ms, flip 25 degrees off the 30 target, cancel 125 ms after the flip: what a beginner does.
    for (float angle : {55.f, 5.f, -55.f}) {
        Rig rig;
        startDrill(rig, "speedflip", 0.f);
        CHECK(rig.mod.drillSetup().stage == 1);
        auto script = speedflipAttempt(angle, 18, 15);
        for (int i = 0; i < 4 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
        const AttemptResult* r = rig.result("mech.speedflip");
        CHECK_MSG(r != nullptr, describe(rig));
        if (!r) continue;
        CHECK_MSG(r->success, describe(rig));
        CHECK(r->score < 60.f);  // the old pass mark (60/100 and a clean grading) refused it
        CHECK(rig.mod.status().drillSuccesses == 1);
        CHECK(rig.mod.level(Focus::SpeedFlip) > 0.f);  // the staircase goes up
        // HUD: verdict and score, then the one tip in words, then the next action.
        const DrawList dl = drawHud(rig);
        checkCompact(dl, "speedflip niveau 1");
        if (dl.panels.empty()) continue;
        const auto lines = mainLines(dl.panels[0]);
        CHECK(lines.size() == 3);
        if (lines.size() < 3) continue;
        CHECK(lines[0]->text.find("Réussi") == 0 && lines[0]->text.find("/100") != std::string::npos);
        CHECK(lines[1]->text == rig.mod.lastAttempt().headline && !rig.result("mech.speedflip")->tips.empty());
        CHECK(lines[2]->text.find("Essai suivant") == 0);
        CHECK(dl.panels[0].title.find("niveau 1/10") != std::string::npos);
    }
}

TEST(mechanics_speedflip_top_level_stays_demanding) {
    // The same beginner attempt at the top level: kickoff from any spot, narrow margins.
    {
        Rig rig;
        startDrill(rig, "speedflip", 1.f);
        CHECK(rig.mod.drillSetup().stage == 3 && rig.mod.drillSetup().kickoff);
        CHECK_NEAR(rig.mod.drillSetup().passScore, 85.f, 1e-3f);
        CHECK(rig.mod.drillSetup().requireDetector);
        auto script = speedflipAttempt(55.f, 18, 15);
        for (int i = 0; i < 6 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
        const AttemptResult* r = rig.result("mech.speedflip");
        CHECK_MSG(r != nullptr, describe(rig));
        if (r) {
            CHECK(!r->success);
            CHECK(!r->tips.empty() && r->tips.front().code != "me.drill_below_level");
        }
        CHECK(rig.mod.level(Focus::SpeedFlip) < 1.f);
    }
    // Pass conditions alone: the beginner grade passes level 1 and fails level 10; a clean one passes level 10.
    Rng rng(1);
    MechGrade beginner;
    beginner.mech = Mech::SpeedFlip;
    beginner.add("jumpToDodgeTicks", 18.f);
    beginner.add("angleErrorDeg", 25.f);
    beginner.add("cancelTicks", 15.f);
    beginner.add("headingErrorDeg", 30.f);
    beginner.add("boostRatio", 0.6f);
    beginner.add("kickoffFirstJumpTicks", 62.f);
    CHECK(judgeDrill(makeDrill(Focus::SpeedFlip, 0.f, 0, 0, rng), beginner).pass);
    // Level 1 still wants a diagonal flip: 2 degrees off the nose is a straight front flip, 5 degrees is enough.
    MechGrade straight = beginner;
    straight.metrics.clear();
    straight.add("jumpToDodgeTicks", 18.f);
    straight.add("angleErrorDeg", 2.f - 30.f);
    straight.add("cancelTicks", 15.f);
    const DrillJudgement front = judgeDrill(makeDrill(Focus::SpeedFlip, 0.f, 0, 0, rng), straight);
    CHECK(!front.pass);
    CHECK(front.code && std::string(front.code) == "SF_ANGLE_STRAIGHT");
    MechGrade diagonal = straight;
    diagonal.metrics.clear();
    diagonal.add("jumpToDodgeTicks", 18.f);
    diagonal.add("angleErrorDeg", 5.f - 30.f);
    diagonal.add("cancelTicks", 15.f);
    CHECK(judgeDrill(makeDrill(Focus::SpeedFlip, 0.f, 0, 0, rng), diagonal).pass);
    const DrillJudgement top = judgeDrill(makeDrill(Focus::SpeedFlip, 1.f, 0, 0, rng), beginner);
    CHECK(!top.pass);
    CHECK(top.code && std::string(top.code) == "SF_JUMP_LONG");  // the first thing of the move that goes wrong
    MechGrade clean = beginner;
    clean.metrics.clear();
    clean.add("jumpToDodgeTicks", 12.f);
    clean.add("angleErrorDeg", -4.f);
    clean.add("cancelTicks", 4.f);
    clean.add("headingErrorDeg", 6.f);
    clean.add("boostRatio", 1.f);
    clean.add("kickoffFirstJumpTicks", 62.f);
    CHECK(judgeDrill(makeDrill(Focus::SpeedFlip, 1.f, 0, 0, rng), clean).pass);
    // A crash landing fails every level; a missing touch is the first thing said.
    MechGrade crash = beginner;
    crash.err("SF_BAD_LANDING", 20.f);
    CHECK(!judgeDrill(makeDrill(Focus::SpeedFlip, 0.f, 0, 0, rng), crash).pass);
    crash.err("DRILL_NO_TOUCH", 30.f);
    CHECK(std::string(judgeDrill(makeDrill(Focus::SpeedFlip, 0.6f, 0, 0, rng), crash).code) == "DRILL_NO_TOUCH");
}

TEST(mechanics_speedflip_late_flip_gets_one_tip) {
    // Flip 0.4 s after the jump: graded in practice (it used to be ignored), the single tip says why.
    Rig rig;
    startDrill(rig, "speedflip", 0.f);
    auto script = speedflipAttempt(30.f, 48, 4);
    for (int i = 0; i < 4 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r != nullptr, describe(rig));
    if (!r) return;
    CHECK(!r->success);
    CHECK(!r->tips.empty() && r->tips.front().code == "me.sf_jump_long");
    if (!r->tips.empty()) CHECK(r->tips.front().text == "Tu flippes trop tard : flippe juste après le saut.");
    CHECK(rig.mod.lastAttempt().headline == r->tips.front().text);
    // The measures stay available as detail lines (detailed HUD).
    const DrawList full = drawHud(rig, false);
    bool measure = false;
    if (!full.panels.empty())
        for (const auto& l : full.panels[0].lines) measure = measure || (l.detail && technical(l.text));
    CHECK(measure);
}

TEST(mechanics_speedflip_very_late_flip_still_gets_the_tip) {
    // A hesitant flip 70 ticks (0.6 s) after the jump: one graded result with the "too late" tip, not a time-out.
    Rig rig;
    startDrill(rig, "speedflip", 0.f);
    auto script = speedflipAttempt(30.f, 70, 4);
    for (int i = 0; i < 5 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
    int graded = 0;
    for (const auto& res : rig.results) graded += res.skill == "mech.speedflip" ? 1 : 0;
    CHECK_EQ(graded, 1);
    const AttemptResult* r = rig.result("mech.speedflip");
    CHECK_MSG(r != nullptr, describe(rig));
    if (!r) return;
    CHECK(!r->success);
    CHECK(!r->tips.empty() && r->tips.front().code == "me.sf_jump_long");
}

TEST(mechanics_drill_live_step_and_timeout_hint) {
    Rig rig;
    startDrill(rig, "speedflip", 0.f);
    auto action = [&]() -> std::string {
        const DrawList dl = drawHud(rig);
        if (dl.panels.empty()) return {};
        const auto lines = mainLines(dl.panels[0]);
        return lines.empty() ? std::string() : lines.back()->text;
    };
    CHECK(rig.mod.status().currentStep == 0);
    CHECK(action() == "1/5 Boost et accélérateur");
    Inputs in;
    in.throttle = 1.f;
    in.boost = true;
    for (int i = 0; i < 20; ++i) rig.tick(in);
    CHECK(rig.mod.status().currentStep == 1);
    CHECK(action() == "2/5 Petit saut");
    in.jump = true;
    for (int i = 0; i < 8; ++i) rig.tick(in);
    CHECK(rig.mod.status().currentStep == 2);
    CHECK(action() == "3/5 Flip en diagonale");
    // No flip until the time limit: a failed attempt with the tip of the missing step (never silent).
    in.jump = false;
    for (int i = 0; i < 6 * 120 && rig.mod.status().phase == "Essai en cours"; ++i) rig.tick(in);
    CHECK(rig.count("mech.speedflip") == 1);
    const AttemptResult* r = rig.result("mech.speedflip");
    if (r) {
        CHECK(!r->success);
        CHECK(!r->tips.empty() && r->tips.front().code == "me.sf_no_flip");
    }
    CHECK(rig.mod.status().message.find("Il manquait : Flip en diagonale") != std::string::npos);
    const DrawList dl = drawHud(rig);
    checkCompact(dl, "temps écoulé");
    if (!dl.panels.empty()) {
        const auto lines = mainLines(dl.panels[0]);
        CHECK(lines.size() == 3);
        if (lines.size() == 3) {
            CHECK(lines[0]->text == "Temps écoulé");
            CHECK(lines[1]->text == "Il manquait : Flip en diagonale");
        }
    }
    CHECK_NEAR(rig.mod.level(Focus::SpeedFlip), 0.f, 1e-6f);
}

// ---- Every drill: beginner first level, demanding top level, compact HUD ----------------------------------
TEST(mechanics_every_drill_has_a_beginner_level) {
    Rng rng(7);
    for (int i = 0; i < kFocusCount; ++i) {
        const Focus f = static_cast<Focus>(i);
        const std::string id = focusInfo(f).id;
        const DrillSetup easy = makeDrill(f, 0.f, 0, 0, rng);
        const DrillSetup hard = makeDrill(f, 1.f, 0, 0, rng);
        CHECK_MSG(easy.passScore == 0.f && !easy.requireDetector, id);
        CHECK_MSG(!easy.checks.empty() && !hard.checks.empty(), id);
        CHECK_MSG(easy.timeLimit >= hard.timeLimit, id);
        if (f != Focus::Boost) CHECK_MSG(hard.passScore >= 75.f && hard.requireDetector, id);
        for (const DrillSetup* d : {&easy, &hard}) {
            CHECK_MSG(!d->goal.empty() && !d->memo.empty() && d->steps.size() >= 2, id);
            for (const std::string& t : {d->goal, d->memo}) {
                CHECK_MSG(!technical(t) && !hasEmDash(t) && !hasEnDash(t), id + " : " + t);
            }
            for (const DrillStep& st : d->steps) CHECK_MSG(!technical(st.text) && !hasEmDash(st.text), id + " : " + st.text);
        }
        // Each band narrows (or stays) from level 1 to level 10.
        for (const DrillCheck& e : easy.checks) {
            if (!e.key) continue;
            for (const DrillCheck& h : hard.checks) {
                if (!h.key || std::string(h.key) != e.key || std::string(h.lowCode) != e.lowCode) continue;
                CHECK_MSG(h.lo >= e.lo - 1e-3f && h.hi <= e.hi + 1e-3f, id + " " + e.key);
            }
        }
    }
}

TEST(mechanics_compact_hud_for_every_drill) {
    for (const char* id : kFamilies) {
        for (float level : {0.f, 1.f}) {
            Rig rig;
            Store st;
            st.setFloat(std::string("mechanics.level.") + id + ".level", level);
            st.setInt(std::string("mechanics.level.") + id + ".attempts", 1);
            rig.mod.load(st);
            rig.command("drill", {id});
            rig.tick(Inputs{});
            const std::string what = std::string(id) + (level > 0.5f ? " niveau 10" : " niveau 1");
            checkCompact(drawHud(rig), what + " début");
            const DrawList first = drawHud(rig);
            CHECK_MSG(!first.panels.empty() && !mainLines(first.panels[0]).empty(), what);
            for (int i = 0; i < 6 * 120; ++i) {
                rig.tick(Inputs{});
                if (i % 40 == 0) checkCompact(drawHud(rig), what + " pendant");
            }
            // The detailed HUD keeps the level and the steps.
            const DrawList full = drawHud(rig, false);
            bool levelLine = false;
            if (!full.panels.empty())
                for (const auto& l : full.panels[0].lines) levelLine = levelLine || (l.detail && l.text.find("Niveau ") == 0);
            CHECK_MSG(levelLine, what);
        }
    }
}

TEST(mechanics_compact_hud_free_and_focus) {
    Rig rig;
    rig.set("rlt_me_hud_counters", "1");
    rig.sim.vel = {1000.f, 0.f, 0.f};
    rig.run(160, groundWavedash());
    CHECK(rig.count("mech.wavedash") == 1);
    const DrawList dl = drawHud(rig);
    checkCompact(dl, "libre");
    if (!dl.panels.empty()) {
        const auto lines = mainLines(dl.panels[0]);
        CHECK(lines.size() == 2);  // result, tip
        if (!lines.empty()) CHECK(lines[0]->text.find("/100") != std::string::npos && lines[0]->bar >= 0.f);
    }
    // Free mode once the result faded: the counters alone are a detail line, no empty box in the compact HUD.
    rig.run(9 * 120, idle());
    CHECK(drawHud(rig).panels.empty());
    CHECK(drawHud(rig, false).panels.size() == 1);

    Rig focus;
    focus.command("focus", {"halfflip"});
    const DrawList f = drawHud(focus);
    checkCompact(f, "focus");
    if (!f.panels.empty()) {
        const auto lines = mainLines(f.panels[0]);
        CHECK(lines.size() == 1);
        if (!lines.empty()) CHECK(lines[0]->text == std::string("À faire : ") + focusInfo(Focus::HalfFlip).memo);
    }
}

// ---- Texts: tiers, plain words first, numbers in the details ----------------------------------------------
TEST(mechanics_catalogue_tiers_and_plain_texts) {
    CHECK(focusInfo(Focus::SpeedFlip).tier == Tier::Advanced);
    CHECK(std::string(tierLabel(Tier::Beginner)) == "Débutant");
    CHECK(std::string(tierLabel(Tier::Intermediate)) == "Intermédiaire");
    CHECK(std::string(tierLabel(Tier::Advanced)) == "Avancé");
    CHECK(std::string(tierAdvice()).find("Débutant : commence par") == 0);
    int perTier[3] = {0, 0, 0};
    for (int i = 0; i < kFocusCount; ++i) {
        const FocusInfo& fi = focusInfo(static_cast<Focus>(i));
        perTier[static_cast<int>(fi.tier)]++;
        CHECK_MSG(fi.memo && fi.tech && fi.progression && fi.steps[0] && fi.steps[1], fi.id);
        for (const char* t : {fi.what, fi.key, fi.memo}) CHECK_MSG(!technical(t) && !hasEmDash(t) && !hasEnDash(t), t);
        for (const char* st : fi.steps)
            if (st) CHECK_MSG(!technical(st) && !hasEmDash(st) && !hasEnDash(st), st);
        for (const char* t : {fi.tech, fi.progression}) CHECK_MSG(!hasEmDash(t) && !hasEnDash(t), t);
    }
    for (int n : perTier) CHECK(n >= 3);
    CHECK(!hasEmDash(tierAdvice()) && !hasEnDash(tierAdvice()));
}

TEST(mechanics_tips_in_plain_words) {
    PlayerProfile p;
    MechGrade g;
    g.add("passScore", 40.f);
    for (const char* code :
         {"WD_CRASH_LANDING", "WD_NOSE_DOWN", "WD_NOSE_UP", "WD_ROLL_TILT", "WD_LOW_GAIN", "WD_LATE_JUMPED", "WD_TOO_EARLY",
          "WD_SIDE_NO_POWERSLIDE", "WD_HIGH_HOP", "WALL_HOLD_LONG", "WALL_TAP_SLOW", "WALL_FELL_OFF", "WALL_LOW_GAIN",
          "CHAIN_GROUND_TIME", "CHAIN_BROKEN", "CHAIN_NOT_SUPERSONIC", "CHAIN_NO_POWERSLIDE", "CHAIN_BOOST_USED",
          "SF_NO_CANCEL", "SF_LATE_CANCEL", "SF_JUMP_LONG", "SF_HEADING_OFF", "SF_DRIFT", "SF_ANGLE_STRAIGHT",
          "SF_ANGLE_WIDE", "SF_BOOST_RELEASED", "SF_THROTTLE_RELEASED", "SF_BAD_LANDING", "KO_STARTED_IN_AIR",
          "KO_FIRST_JUMP_EARLY", "KO_FIRST_JUMP_LATE", "KO_NO_BOOST_AT_START", "HF_NO_CANCEL", "HF_EARLY_CANCEL",
          "HF_LATE_CANCEL", "HF_WEAK_CANCEL", "HF_NOT_STRAIGHT", "HF_EARLY_ROLL", "HF_LATE_ROLL", "HF_HEADING_OFF",
          "HF_BAD_LANDING", "HF_HIGH_JUMP", "HF_NO_ROLL", "FC_NO_CANCEL", "FC_LATE", "FC_WEAK", "FC_BAD_LANDING",
          "FA_NO_DOUBLE_JUMP", "FA_HOLD_SHORT", "FA_HOLD_LONG", "FA_NO_TILT", "FA_ACCIDENTAL_DODGE", "FA_GAP_SLOW",
          "FA_BOOST_LATE", "FA_STICK_NEAR_DEADZONE", "DJA_BOOST_LATE", "ST_PITCH_LEAK", "ST_NO_FLIP", "ST_SHORT_HOVER",
          "ST_YAW_ROLL_MISMATCH", "AS_UNSTABLE_AXIS", "AS_SLOW_SPIN", "AS_SHORT", "RC_TILTED", "RC_MISALIGNED",
          "RC_SPEED_LOST", "RC_SLOW_SETTLE", "RC_MISSED_WAVEDASH", "RC_WEAK_WAVEDASH", "RC_CRASHED", "RC_ON_ROOF",
          "PS_SPEED_LOSS", "PS_EXIT_SLIP", "PS_NO_THROTTLE", "SM_LONG_HOLDS", "SM_BOOST_AT_MAX", "SM_NO_THROTTLE",
          "SM_NOT_SUPERSONIC", "SM_TOO_MUCH_BOOST", "COMBO_SLOW_LINK", "ZAP_NO_BOUNCE", "WCHAIN_SLOW", "DRILL_NO_TOUCH",
          "DRILL_BELOW_LEVEL", "DRILL_CHAIN_SHORT", "DRILL_SLOW_TO_BALL",
          // Never-silent timeouts and the "parcours complet" steps.
          "SF_NO_BOOST", "SF_NO_JUMP", "SF_NO_FLIP", "SF_SIDE_FLIP", "SF_BACK_FLIP", "SF_JUMP_HELD", "SF_NOT_AIMED",
          "SF_SLOW_TO_BALL", "HF_NO_FLIP", "HF_WRONG_FLIP", "HF_NO_REVERSE", "HF_SLOW_TO_BALL", "FC_NO_FLIP",
          "FC_WRONG_FLIP", "FC_JUMP_LONG", "DRILL_NO_SPEED", "WD_NO_FLIP", "WD_NO_HOP", "WD_NOT_FORWARD", "ZAP_NO_FLIP",
          "ZAP_NOSE", "ZAP_LATE", "CHAIN_SLOW", "WALL_NO_DASH", "WALL_NO_CLIMB", "WALL_NOT_RIDING", "FA_NO_JUMP",
          "FA_TOUCH_LOW", "FA_NOT_PLACED", "ST_NO_STALL", "ST_DOUBLE_JUMP", "ST_TOO_LOW", "AS_NO_SPIN", "AS_NO_TAKEOFF",
          "RC_NO_CLIMB", "RC_NO_WALL_JUMP", "RC_SHORT_AIR", "PS_NO_SLIDE", "PS_NOT_AIMED", "SM_SLOW_TO_SUPERSONIC",
          "SM_EARLY_BOOST"}) {
        const std::string t = tipText(code, g, p, 0.5f);
        CHECK_MSG(!t.empty() && !technical(t) && !hasEmDash(t) && !hasEnDash(t), std::string(code) + " : " + t);
        CHECK_MSG(t.find("Recommence en te concentrant") == std::string::npos, code);
    }
    // The flip direction is said as a clock position of the stick.
    MechGrade sf;
    sf.add("targetAngleDeg", 30.f);
    CHECK(tipText("SF_ANGLE_STRAIGHT", sf, p, 0.5f).find("11 h ou 1 h") != std::string::npos);
    sf.metrics.clear();
    sf.add("targetAngleDeg", 45.f);
    CHECK(tipText("SF_ANGLE_WIDE", sf, p, 0.5f).find("10 h 30 ou 1 h 30") != std::string::npos);
}

TEST(mechanics_drill_numbers_for_the_tab) {
    Rng rng(3);
    const auto lines = drillCheckLines(makeDrill(Focus::SpeedFlip, 0.f, 0, 0, rng), DetectConfig{}, SpeedUnit::Uus);
    auto has = [&](const std::string& s) {
        for (const auto& l : lines)
            if (l == s) return true;
        return false;
    };
    CHECK(has("Saut -> flip : <= 300 ms"));
    CHECK(has("Angle du flip : 30° +/-28°"));
    CHECK(has("Annulation : <= 167 ms"));
    CHECK(has("Retomber sur les roues"));
    CHECK(has("Temps par essai : 4,5 s"));
    for (const auto& l : lines) CHECK_MSG(l.find("Score minimum") == std::string::npos, l);  // none at level 1
    const auto top = drillCheckLines(makeDrill(Focus::SpeedFlip, 1.f, 0, 0, rng), DetectConfig{}, SpeedUnit::Uus);
    bool score = false, touch = false;
    for (const auto& l : top) {
        score = score || l == "Score minimum : 85/100";
        touch = touch || l == "Toucher la balle";
        CHECK_MSG(!hasEmDash(l) && !hasEnDash(l), l);
    }
    CHECK(score && touch);
    // Every family describes its conditions with real labels.
    for (int i = 0; i < kFocusCount; ++i) {
        const DrillSetup d = makeDrill(static_cast<Focus>(i), 0.5f, 0, 0, rng);
        for (const auto& l : drillCheckLines(d, DetectConfig{}, SpeedUnit::Uus)) {
            CHECK_MSG(l.find("Ticks") == std::string::npos && l.find("Ratio") == std::string::npos, l);
            CHECK_MSG(l.find('_') == std::string::npos, l);
        }
    }
}

TEST(mechanics_speedflip_next_step_is_announced) {
    // Level 3, one success: level 4, the kickoff step. The change is said before the next attempt starts.
    Rig rig;
    startDrill(rig, "speedflip", 0.27f);
    CHECK(rig.mod.drillSetup().stage == 1);
    auto script = speedflipAttempt(30.f, 14, 4);
    for (int i = 0; i < 4 * 120 && rig.results.empty(); ++i) rig.tick(script(i, rig.sim));
    CHECK_MSG(!rig.results.empty() && rig.results[0].success, describe(rig));
    CHECK(displayLevel(rig.mod.level(Focus::SpeedFlip)) == 4);
    for (int i = 0; i < 300 && rig.mod.status().phase != "Compte à rebours"; ++i) rig.tick(Inputs{});
    CHECK(rig.mod.drillSetup().stage == 2 && rig.mod.drillSetup().kickoff);
    CHECK(rig.toastContains("Étape suivante"));
    const DrawList dl = drawHud(rig);
    checkCompact(dl, "nouvelle étape");
    if (!dl.panels.empty()) {
        const auto lines = mainLines(dl.panels[0]);
        CHECK(lines.size() == 3);
        if (lines.size() == 3) CHECK(lines[0]->text == rig.mod.drillSetup().goal);
    }
}

// ---- Tutorials, beginner path, lexicon --------------------------------------------------------------------------
TEST(mechanics_tutorials_for_every_drill) {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"}};
    for (int i = 0; i < kFocusCount; ++i) {
        const Focus f = static_cast<Focus>(i);
        const Tutorial& t = tutorial(f);
        const std::string id = focusInfo(f).id;
        CHECK_MSG(t.focus == f, id);
        CHECK_MSG(t.intro && *t.intro && t.full && *t.full && t.level1 && *t.level1, id);
        int steps = 0, panel = 0;
        for (const TutorialStep& s : t.steps) {
            if (!s.action) continue;
            ++steps;
            CHECK_MSG(s.why && *s.why && s.ok && *s.ok, id);
            for (const char* x : {s.action, s.why, s.ok}) {
                const std::string filled = fillButtons(x, p);
                CHECK_MSG(!technical(filled) && !hasEmDash(filled) && !hasEnDash(filled), id + " : " + filled);
                CHECK_MSG(filled.find('{') == std::string::npos, id + " : " + filled);  // every placeholder filled
            }
        }
        for (const char* l : t.panel) panel += l ? 1 : 0;
        CHECK_MSG(steps >= 3 && panel >= 3, id);
        for (const char* x : {t.intro, t.full, t.level1})
            CHECK_MSG(!technical(x) && !hasEmDash(x) && !hasEnDash(x), id + " : " + x);
        // The HUD panel lines of the tab are the steps of the exercise.
        const FocusInfo& fi = focusInfo(f);
        for (int k = 0; k < 6 && fi.steps[k]; ++k) CHECK_MSG(t.panel[k] && std::string(t.panel[k]) == fi.steps[k], id);
        CHECK_MSG(!hasEmDash(fi.tech) && !hasEnDash(fi.tech), id);
    }
    // Buttons are the player's own.
    CHECK(fillButtons("Saute avec {jump}, puis {boost} et {slide}.", p) == "Saute avec Saut (A), puis Boost (B) et Glissade (X).");
    // The read model gives the tab the filled tutorial.
    Rig rig;
    rig.setProfile(p);
    for (const auto& row : rig.mod.statRows()) {
        CHECK(!row.intro.empty() && row.tutorial.size() >= 3 && row.panel.size() >= 3);
        for (const auto& s : row.tutorial) CHECK(s.action.find('{') == std::string::npos);
    }
}

TEST(mechanics_beginner_path_and_lexicon) {
    const auto& path = mechanicsPath();
    CHECK(path.size() == static_cast<size_t>(kFocusCount) + 1);  // the speedflip twice (step 1, then the kickoff)
    for (int i = 0; i < kFocusCount; ++i) CHECK(pathIndex(static_cast<Focus>(i)) < static_cast<int>(path.size()));
    // Spec order of the tab: Powerslide, Boost, Réception, Hélico, Flip cancel, Speedflip étape 1, Half-flip, Fast aerial,
    // Wavedash, Stall, Speedflip kickoff, Chain, Zap, Wall dash.
    const Focus order[] = {Focus::Powerslide, Focus::Boost,      Focus::Recovery,  Focus::Helicopter, Focus::FlipCancel,
                           Focus::SpeedFlip,  Focus::HalfFlip,   Focus::FastAerial, Focus::Wavedash,  Focus::Stall,
                           Focus::SpeedFlip,  Focus::ChainDash,  Focus::ZapDash,   Focus::WallDash};
    CHECK(path.size() == std::size(order));
    for (size_t i = 0; i < path.size() && i < std::size(order); ++i) CHECK(path[i].focus == order[i]);
    CHECK(std::string(path[10].drillArg) == "kickoff");
    CHECK(std::string(pathRule()) ==
          "Passe à l'exercice suivant quand tu réussis environ 3 essais sur 4 au niveau 4. Tu peux toujours revenir en "
          "arrière.");
    const std::string lex = lexicon();
    for (const char* w : {"Flip :", "Annuler un flip :", "Air roll :", "Powerslide :", "Supersonique :", "Kickoff :"})
        CHECK_MSG(lex.find(w) != std::string::npos, w);
    for (const auto& e : path) CHECK(!hasEmDash(e.label) && !hasEnDash(e.label) && !hasEmDash(e.block) && !hasEmDash(e.note));
    CHECK(!hasEmDash(lex) && !hasEnDash(lex));
    // Difficulty of the spec: Hélico is a beginner drill, Stall an intermediate one.
    CHECK(focusInfo(Focus::Helicopter).tier == Tier::Beginner);
    CHECK(focusInfo(Focus::Stall).tier == Tier::Intermediate);
}

TEST(mechanics_level_commands) {
    // "Revenir au niveau 1", the speedflip kickoff steps, the "parcours complet".
    Rig rig;
    Store st;
    st.setFloat("mechanics.level.halfflip.level", 0.6f);
    st.setInt("mechanics.level.halfflip.attempts", 3);
    st.setFloat("mechanics.full.halfflip.level", 0.4f);
    st.setInt("mechanics.full.halfflip.attempts", 2);
    rig.mod.load(st);
    CHECK_NEAR(rig.mod.level(Focus::HalfFlip), 0.6f, 1e-6f);
    CHECK_NEAR(rig.mod.fullLevel(Focus::HalfFlip), 0.4f, 1e-6f);
    Actions a = rig.command("reset_level", {"halfflip"});
    CHECK_NEAR(rig.mod.level(Focus::HalfFlip), 0.f, 1e-6f);
    CHECK_NEAR(rig.mod.fullLevel(Focus::HalfFlip), 0.4f, 1e-6f);
    CHECK(a.requestSave && rig.toastContains("retour au niveau 1"));
    rig.command("reset_level", {"halfflip", "full"});
    CHECK_NEAR(rig.mod.fullLevel(Focus::HalfFlip), 0.f, 1e-6f);
    rig.command("reset_level", {"nope"});
    CHECK(rig.toastContains("Méca inconnue"));
    rig.mod.load(st);
    rig.command("reset_level", {"halfflip", "all"});  // the tab button: both at once
    CHECK_NEAR(rig.mod.level(Focus::HalfFlip), 0.f, 1e-6f);
    CHECK_NEAR(rig.mod.fullLevel(Focus::HalfFlip), 0.f, 1e-6f);
    // Straight to the kickoff steps of the speedflip.
    rig.command("drill", {"speedflip", "kickoff"});
    CHECK(displayLevel(rig.mod.level(Focus::SpeedFlip)) == 4);
    rig.tick(Inputs{});
    CHECK(rig.mod.drillSetup().stage == 2 && rig.mod.drillSetup().kickoff);
    rig.command("stop");
    rig.tick(Inputs{});
    rig.command("drill", {"wavedash", "full"});
    CHECK(rig.mod.drillFull());
    rig.tick(Inputs{});
    CHECK(rig.mod.drillSetup().full && rig.mod.fullTracker().size() == 4);
    CHECK(rig.mod.status().full && rig.mod.status().steps.size() == 4);
}

TEST(mechanics_compact_hud_full_sequences) {
    for (const char* id : kFamilies) {
        Rig rig;
        rig.command("drill", {id, "full"});
        Inputs in;
        rig.tick(in);
        checkCompact(drawHud(rig), std::string(id) + " complet début");
        rig.tick(in);
        in.throttle = 1.f;
        for (int i = 0; i < 12 * 120; ++i) {  // get-ready phase (3 s) and the longest time limit (8 s)
            rig.tick(in);
            if (i % 30 == 0) checkCompact(drawHud(rig), std::string(id) + " complet pendant");
        }
        CHECK_MSG(rig.mod.status().drillAttempts >= 1, id);  // never silent: the time limit records the attempt
    }
}

TEST(mechanics_slow_motion_offered_after_three_misses) {
    // Three failed attempts in a row: during the next get-ready phase the HUD suggests the slow motion (in words), the
    // tab gets the streak to show its button.
    Rig rig;
    CHECK(rig.startDrill("speedflip", 0.f));
    auto noFlip = [](int t, MiniSim&) {
        Inputs in;
        in.throttle = 1.f;
        in.boost = true;
        in.jump = t >= 50 && t < 58;
        return in;
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
        const size_t before = rig.results.size();
        for (int i = 0; i < 6 * 120 && rig.results.size() == before; ++i) rig.tick(noFlip(i, rig.sim));
        CHECK(rig.results.size() == before + 1);
        if (attempt < 2) CHECK(rig.waitGo());
    }
    CHECK(rig.mod.status().failStreak == 3);
    for (int i = 0; i < 300 && rig.mod.status().phase != "Compte à rebours"; ++i) rig.tick(Inputs{});
    const DrawList dl = drawHud(rig);
    checkCompact(dl, "ralenti");
    bool offer = false;
    if (!dl.panels.empty())
        for (const auto* l : mainLines(dl.panels[0])) offer = offer || l->text.find("ralenti 0,75") != std::string::npos;
    CHECK(offer);
}
