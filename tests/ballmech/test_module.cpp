// Module-level tests through a fake engine (Harness): touches resolved 2 ticks late, game events, settings,
// arbitration, flip reset counting and event emission, HUD output, robustness, persistence.
#include <set>

#include "core/modules/ballmech/Tips.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace bmtest;

namespace {
bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

int64_t contactFrame(const std::vector<SimTick>& ticks) {
    for (const auto& t : ticks)
        if (t.s.car.wheelContacts == 4 && t.touched) return t.s.frame;
    return -1;
}

ViewContext view1080() {
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.carValid = true;
    v.ballValid = true;
    return v;
}

DrawList drawNow(Harness& h, bool compact = false) {
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, h.hasSnap, h.host.ownsActivity(&h.mod), 1.f, compact};
    h.mod.draw(view1080(), dc, dl);
    return dl;
}

// Lines the player sees with the compact HUD (the default): detail lines are dropped by the engine.
std::vector<PanelLine> mainLines(const DrawPanel& p) {
    std::vector<PanelLine> v;
    for (const auto& l : p.lines)
        if (!l.detail) v.push_back(l);
    return v;
}

// No measures in milliseconds, degrees or game units in the lines the compact HUD shows.
bool technical(const std::string& s) {
    return s.find("°") != std::string::npos || s.find(" ms") != std::string::npos || s.find(" uu") != std::string::npos ||
           s.find("km/h") != std::string::npos || s.find("idéal") != std::string::npos;
}

// Renumbers a scenario so it continues right after the harness' latest snapshot.
std::vector<SimTick> continueFrom(const Harness& h, std::vector<SimTick> ticks) {
    if (ticks.empty()) return ticks;
    const int64_t offset = h.latest.frame + 1 - ticks.front().s.frame;
    for (auto& t : ticks) {
        t.s.frame += offset;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    return ticks;
}
} // namespace

// ---- Settings schema --------------------------------------------------------------------------------------------
TEST(bm_module_schema) {
    SettingsSchema schema;
    BallMechModule::describeSettings(schema);
    std::set<std::string> cvars;
    int count = 0;
    for (const auto& d : schema.all()) {
        ++count;
        CHECK_MSG(d.cvar.rfind("rlt_bm_", 0) == 0, d.cvar);
        CHECK(d.module == "ballmech");
        CHECK_MSG(!d.label.empty() && !d.tooltip.empty() && !d.section.empty(), d.cvar);
        CHECK_MSG(!hasEmDash(d.label) && !hasEmDash(d.tooltip), d.cvar);
        CHECK_MSG(cvars.insert(d.cvar).second, "duplicate " + d.cvar);
        Settings s;
        CHECK_MSG(schema.apply(s, d.cvar, d.defaultValue), "default parses " + d.cvar);
        CHECK_MSG(d.read && d.read(s) == d.defaultValue, "read back " + d.cvar);
    }
    CHECK(count >= 30);
    const auto sections = schema.sections("ballmech");
    CHECK_EQ(sections.size(), 4u);
    // Defaults of the schema equal the struct defaults.
    Settings s;
    s.ballmech.enabled = false;
    s.ballmech.hudY = 0.f;
    s.ballmech.pinchSpeed = 0.f;
    schema.applyDefaults(s);
    const BallMechConfig def;
    CHECK(s.ballmech.enabled == def.enabled);
    CHECK_NEAR(s.ballmech.hudX, def.hudX, 1e-4f);
    CHECK_NEAR(s.ballmech.hudY, def.hudY, 1e-4f);
    CHECK_NEAR(s.ballmech.pinchSpeed, def.pinchSpeed, 1e-2f);
    CHECK_EQ(s.ballmech.successScore, def.successScore);
    CHECK_EQ(s.ballmech.chatMode, def.chatMode);
    CHECK(s.ballmech.detFlipReset && s.ballmech.detPowerShot);
    // Bounds are enforced.
    CHECK(schema.apply(s, "rlt_bm_success_score", "250"));
    CHECK_EQ(s.ballmech.successScore, 100);
    CHECK(!schema.apply(s, "rlt_bm_hud_x", "abc"));
}

// ---- Flip reset counting, de-duplication and events ---------------------------------------------------------------
TEST(bm_module_flipreset_with_game_event) {
    Harness h;
    h.run(flipResetScenario(FR::Good, true));
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.flipreset"), 1);
    CHECK_EQ(h.results.size(), 1u);  // the conversion is also an aerial touch: arbitration keeps the flip reset
    if (!h.results.empty()) {
        const AttemptResult& r = h.results[0];
        CHECK(r.module == "ballmech");
        CHECK(r.success);
        CHECK(r.title == "Flip reset : double touche");
        CHECK(r.score >= 75.f);
        CHECK(r.verdict >= Verdict::Good);
        CHECK(r.hasRecordValue);
        CHECK(hasMetric(r, "time_to_use"));
    }
    CHECK_EQ(h.mod.sessionResets(), 1);
    CHECK_EQ(h.mod.totalResets(), 1);
    CHECK_EQ(h.countEvents("detecteur"), 0);  // the game already announced it
}

TEST(bm_module_flipreset_detector_emits_event) {
    Harness h;
    const auto seq = flipResetScenario(FR::Good, false);
    h.run(seq);
    h.idle(30);
    CHECK_EQ(h.mod.sessionResets(), 1);
    CHECK_EQ(h.countEvents("detecteur"), 1);
    const int64_t contact = contactFrame(seq);
    for (const auto& e : h.events)
        if (e.kind == GameEventKind::FlipReset) CHECK(std::llabs(e.frame - contact) <= 2);
    CHECK_EQ(h.countSkill("ball.flipreset"), 1);
}

TEST(bm_module_flipreset_dedupe_window) {
    Harness h;
    const auto seq = flipResetScenario(FR::Good, false);
    const int64_t contact = contactFrame(seq);
    for (const auto& t : seq) {
        h.tick(t);
        // The game event arrives late (10 ticks) for the same reset: not counted twice.
        if (t.s.frame == contact + 10) h.event(GameEventKind::FlipReset, "sdk", contact + 10);
    }
    CHECK_EQ(h.mod.sessionResets(), 1);
    CHECK_EQ(h.countEvents("detecteur"), 1);
    // A game event 2 s later is a new reset.
    h.event(GameEventKind::FlipReset, "sdk", contact + 240);
    CHECK_EQ(h.mod.sessionResets(), 2);
    // Events from other modules are ignored (they would double count).
    h.event(GameEventKind::FlipReset, "detecteur", contact + 600);
    h.event(GameEventKind::FlipReset, "mechanics", contact + 700);
    CHECK_EQ(h.mod.sessionResets(), 2);
    // Two game events within 0.3 s are one reset.
    h.event(GameEventKind::FlipReset, "sdk", contact + 1000);
    h.event(GameEventKind::FlipReset, "sdk", contact + 1020);
    CHECK_EQ(h.mod.sessionResets(), 3);
    CHECK_EQ(h.mod.totalResets(), 3);
}

TEST(bm_module_flipreset_chain_counts_each_reset) {
    Harness h;
    h.run(flipResetChain());
    h.idle(30);
    CHECK_EQ(h.mod.sessionResets(), 2);
    CHECK_EQ(h.countSkill("ball.flipreset"), 1);
    if (!h.results.empty()) CHECK_NEAR(metric(h.results[0], "resets_in_chain"), 2.f, 1e-4f);
}

TEST(bm_module_flipreset_wasted_and_near_miss) {
    Harness h;
    h.run(flipResetScenario(FR::Wasted, true));
    h.idle(20);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.fr_reset_wasted"));
        CHECK(h.results[0].verdict <= Verdict::Ok);
    }
    Harness n;
    n.run(flipResetScenario(FR::NoReset));
    n.idle(20);
    CHECK_EQ(n.results.size(), 1u);
    if (!n.results.empty()) {
        CHECK(n.results[0].title == "Flip reset manqué");
        CHECK(hasTip(n.results[0], "bm.fr_no_reset"));
        CHECK(n.results[0].verdict == Verdict::Fail);
    }
    CHECK_EQ(n.mod.sessionResets(), 0);
}

TEST(bm_module_reset_available_indicator) {
    Harness h;
    const auto seq = flipResetScenario(FR::Good, true);
    const int64_t contact = contactFrame(seq);
    bool sawAvailable = false;
    for (const auto& t : seq) {
        h.tick(t);
        if (t.s.frame == contact + 20) {
            sawAvailable = h.mod.uiState().resetAvailable;
            const DrawList dl = drawNow(h);
            bool line = false;
            for (const auto& p : dl.panels)
                for (const auto& l : p.lines) line = line || l.text == "Flip reset disponible !";
            CHECK(line);
            CHECK(!dl.lines3D.empty());  // ring around the car
        }
    }
    CHECK(sawAvailable);
    h.idle(10);
    CHECK(!h.mod.uiState().resetAvailable);
}

// ---- Arbitration through the module ----------------------------------------------------------------------------
TEST(bm_module_one_result_per_attempt) {
    Harness h;
    h.run(ceilingScenario(true));
    h.idle(30);
    CHECK_EQ(h.results.size(), 1u);
    CHECK_EQ(h.countSkill("ball.ceiling"), 1);
    Harness p;
    p.run(pinchScenario(PI_::Ground));
    p.idle(30);
    CHECK_EQ(p.results.size(), 1u);
    CHECK_EQ(p.countSkill("ball.pinch"), 1);
    Harness f;
    f.run(flickScenario(FK::Musty));
    f.idle(30);
    CHECK_EQ(f.results.size(), 1u);
    CHECK_EQ(f.countSkill("ball.musty"), 1);
    if (!f.results.empty()) CHECK(f.results[0].title == "Musty flick");
}

TEST(bm_module_skills_of_each_mechanic) {
    struct Case {
        std::vector<SimTick> seq;
        const char* skill;
    };
    std::vector<Case> cases;
    cases.push_back({flickScenario(FK::F45), "ball.flick"});
    cases.push_back({flickScenario(FK::Breezi), "ball.breezi"});
    cases.push_back({aerialScenario(AE::AirRoll), "ball.airroll"});
    cases.push_back({aerialScenario(AE::Redirect), "ball.aerial"});
    cases.push_back({powerShotScenario(PS::FlipShot), "ball.powershot"});
    for (auto& c : cases) {
        Harness h;
        h.run(c.seq);
        h.idle(30);
        CHECK_MSG(h.countSkill(c.skill) == 1, c.skill);
        CHECK_MSG(h.results.size() == 1u, c.skill);
    }
}

// ---- Team / attack direction -----------------------------------------------------------------------------------
TEST(bm_module_attack_direction_follows_team) {
    Harness orange;
    orange.run(powerShotScenario(PS::FlipShot, 1));  // mirrored: orange shoots toward -Y
    orange.idle(20);
    CHECK_EQ(orange.results.size(), 1u);
    if (!orange.results.empty()) {
        CHECK(orange.results[0].success);
        CHECK_EQ(metric(orange.results[0], "on_target"), 1.f);
    }
    // The blue shot (+Y) by an orange car goes toward its own goal: not on target.
    auto seq = powerShotScenario(PS::FlipShot, 0);
    for (auto& t : seq) t.s.car.team = 1;
    Harness own;
    own.run(seq);
    own.idle(20);
    CHECK_EQ(own.results.size(), 1u);
    if (!own.results.empty()) {
        CHECK(!own.results[0].success);
        CHECK_EQ(metric(own.results[0], "on_target"), 0.f);
        CHECK(hasTip(own.results[0], "bm.ps_off_target"));
    }
    Harness ae;
    ae.run(aerialScenario(AE::AirRoll, 1));
    ae.idle(20);
    CHECK_EQ(ae.countSkill("ball.airroll"), 1);
    if (!ae.results.empty()) CHECK(ae.results[0].success);
}

// ---- Robustness --------------------------------------------------------------------------------------------------
TEST(bm_module_teleport_aborts_attempt) {
    Harness h;
    auto seq = flipResetScenario(FR::Good, true);
    const int64_t contact = contactFrame(seq);
    for (auto& t : seq)
        if (t.s.frame > contact + 30) t.s.car.pos += Vec3{0, 0, 1200.f};  // reset to another spot
    h.run(seq);
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.flipreset"), 0);
    CHECK_EQ(h.mod.sessionResets(), 1);  // the reset itself was real
}

TEST(bm_module_context_change_resets_state) {
    Harness h;
    const auto seq = flipResetScenario(FR::Good, true);
    const int64_t contact = contactFrame(seq);
    for (const auto& t : seq) {
        h.tick(t);
        if (t.s.frame == contact + 20) h.event(GameEventKind::ContextChanged, "Park_P");
    }
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.flipreset"), 0);
    CHECK(!h.mod.uiState().resetAvailable);
}

TEST(bm_module_invalid_and_paused_snapshots) {
    Harness h;
    auto seq = flipResetScenario(FR::Good, true);
    const int64_t contact = contactFrame(seq);
    for (auto& t : seq)
        if (t.s.frame > contact + 10 && t.s.frame < contact + 20) t.s.car.valid = false;
    h.run(seq);
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.flipreset"), 0);  // attempt aborted, no crash

    Harness p;
    auto paused = powerShotScenario(PS::FlipShot);
    for (auto& t : paused) t.s.paused = true;
    p.run(paused);
    CHECK(p.results.empty());

    Harness r;
    r.game.mode = GameMode::Replay;
    r.run(powerShotScenario(PS::FlipShot));
    CHECK(r.results.empty());
    CHECK(drawNow(r).empty());
}

TEST(bm_module_frame_counter_restart) {
    // A map reload restarts the physics frame counter: detection keeps working.
    Harness h;
    h.run(powerShotScenario(PS::FlipShot));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    auto again = powerShotScenario(PS::FlipShot);
    for (auto& t : again) {
        t.s.frame -= 900;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    h.run(again);
    h.idle(10);
    CHECK_EQ(h.results.size(), 2u);
}

// ---- Settings effects ---------------------------------------------------------------------------------------------
TEST(bm_module_detection_toggles_and_modes) {
    Harness off;
    off.set("rlt_bm_det_powershot", "0");
    off.run(powerShotScenario(PS::FlipShot));
    off.idle(20);
    CHECK(off.results.empty());

    Harness disabled;
    disabled.set("rlt_bm_enabled", "0");
    disabled.run(flipResetScenario(FR::Good, true));
    disabled.idle(20);
    CHECK(disabled.results.empty());
    CHECK_EQ(disabled.mod.sessionResets(), 0);
    CHECK(drawNow(disabled).empty());

    Harness online;
    online.game.mode = GameMode::Online;
    online.run(powerShotScenario(PS::FlipShot));
    CHECK(online.results.empty());

    Harness match;
    match.game.mode = GameMode::OfflineMatch;
    match.game.worldControlAllowed = false;
    match.run(powerShotScenario(PS::FlipShot));
    match.idle(20);
    CHECK_EQ(match.results.size(), 1u);
    Harness noMatch;
    noMatch.game.mode = GameMode::OfflineMatch;
    noMatch.set("rlt_bm_in_matches", "0");
    noMatch.run(powerShotScenario(PS::FlipShot));
    CHECK(noMatch.results.empty());
}

TEST(bm_module_custom_map_gating) {
    Harness h;
    h.game.mode = GameMode::Workshop;
    h.game.isCustomMap = true;
    h.game.category = MapCategory::Dribble;
    h.run(pinchScenario(PI_::Ground));
    h.idle(20);
    CHECK_EQ(h.countSkill("ball.pinch"), 0);
    h.run(ceilingScenario(true));
    h.idle(20);
    CHECK_EQ(h.countSkill("ball.ceiling"), 0);
    // Flicks still work on a dribble map, without goal accuracy.
    Harness f;
    f.game.mode = GameMode::Workshop;
    f.game.isCustomMap = true;
    f.game.category = MapCategory::Dribble;
    f.run(flickScenario(FK::F45));
    f.idle(20);
    CHECK_EQ(f.countSkill("ball.flick"), 1);
    if (!f.results.empty()) CHECK(!hasMetric(f.results[0], "on_target"));
    // Rings maps: only flip resets and air dribbles.
    Harness r;
    r.game.mode = GameMode::Workshop;
    r.game.isCustomMap = true;
    r.game.category = MapCategory::Rings;
    r.run(flickScenario(FK::F45));
    r.idle(20);
    CHECK(r.results.empty());
}

TEST(bm_module_success_threshold_and_chat) {
    Harness h;
    h.set("rlt_bm_success_score", "100");
    h.set("rlt_bm_chat", "2");
    h.run(powerShotScenario(PS::FlipShot));
    h.idle(20);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.drill_need_score"));
        CHECK(h.results[0].verdict <= Verdict::Ok);
    }
    CHECK_EQ(h.chat.size(), 1u);
    if (!h.chat.empty()) {
        CHECK(h.chat[0].text.find("Frappe en flip") != std::string::npos);
        CHECK(!hasEmDash(h.chat[0].text));
    }
    Harness quiet;
    quiet.set("rlt_bm_chat", "0");
    quiet.run(powerShotScenario(PS::FlipShot));
    quiet.idle(20);
    CHECK(quiet.chat.empty());
    Harness successes;  // default: successes only
    successes.run(powerShotScenario(PS::WeakSide));
    successes.run(powerShotScenario(PS::FlipShot));
    successes.idle(20);
    CHECK_EQ(successes.chat.size(), 1u);
}

// ---- HUD --------------------------------------------------------------------------------------------------------
TEST(bm_module_hud_panel) {
    Harness h;
    CHECK(drawNow(h).panels.empty());  // nothing to show yet
    h.run(powerShotScenario(PS::FlipShot));
    h.idle(10);
    DrawList dl = drawNow(h);
    CHECK_EQ(dl.panels.size(), 1u);
    if (!dl.panels.empty()) {
        const DrawPanel& p = dl.panels[0];
        CHECK_NEAR(p.pos.x, 0.01f * 1920.f, 0.5f);
        CHECK_NEAR(p.pos.y, 0.60f * 1080.f, 0.5f);
        CHECK(p.title == "Frappe en flip");  // the detected mechanic names the panel
        CHECK(p.lines.size() >= 3 && p.lines.size() <= 12);
        CHECK(p.lines[0].text.find("/100") != std::string::npos);
        CHECK(p.lines[0].bar > 0.5f);
        CHECK(!p.lines[0].detail);
        for (const auto& l : p.lines) CHECK(!hasEmDash(l.text));
        bool measures = false;  // the key measures are still there, as detail lines
        for (const auto& l : p.lines) measures = measures || (l.detail && l.text.find("Vitesse de sortie") != std::string::npos);
        CHECK(measures);
    }
    CHECK(dl.lines3D.empty());  // the projected path of the shot is a prediction: off by default
    h.set("rlt_bm_draw_trajectory", "1");
    dl = drawNow(h);
    CHECK(!dl.lines3D.empty() && dl.lines3D.size() <= 100);  // projected path of the shot
    // HUD position follows the settings.
    h.set("rlt_bm_hud_x", "0.5");
    dl = drawNow(h);
    if (!dl.panels.empty()) CHECK_NEAR(dl.panels[0].pos.x, 960.f, 0.5f);
    // The result fades after hud_seconds.
    h.idle(static_cast<int>(9.f * kTickRate));
    dl = drawNow(h);
    CHECK(dl.panels.empty());
    CHECK(dl.lines3D.empty());
    h.set("rlt_bm_hud", "0");
    h.set("rlt_bm_hud_seconds", "0");
    CHECK(drawNow(h).panels.empty());
}

TEST(bm_module_hud_compact_budget) {
    // Compact HUD: result, one tip in words, nothing technical; at most 3 visible lines.
    Harness h;
    h.run(aerialScenario(AE::OffCenter));
    h.idle(10);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(!h.results[0].tips.empty());
    const DrawList dl = drawNow(h, true);
    CHECK_EQ(dl.panels.size(), 1u);
    if (!dl.panels.empty()) {
        const auto lines = mainLines(dl.panels[0]);
        CHECK(!lines.empty() && lines.size() <= 3);
        if (!lines.empty()) CHECK(lines[0].text.find("/100") != std::string::npos);
        if (lines.size() >= 2 && !h.results.empty() && !h.results[0].tips.empty()) {
            CHECK(lines[1].text == bm::hudTipText(h.results[0].tips.front()));
            CHECK(lines[1].text.size() <= 60);
        }
        for (const auto& l : lines) CHECK_MSG(!technical(l.text), l.text);
    }
    // Only a counter left (detail): the compact HUD draws no empty panel.
    Harness r;
    r.run(flipResetScenario(FR::Good, true));
    r.idle(static_cast<int>(10.f * kTickRate));
    CHECK(r.mod.sessionResets() > 0);
    CHECK(drawNow(r, true).panels.empty());
    CHECK_EQ(drawNow(r, false).panels.size(), 1u);  // the full HUD still shows the session counter
}

TEST(bm_module_hud_drill_single_panel) {
    // During a drill one panel carries everything: last result, its tip, then the action and the time left.
    Harness h;
    Sim warm;
    orient(warm.s.car, PI / 2, 0, 0);
    warm.s.ball.pos = {0, 1000, 93};
    warm.step(10);
    h.run(warm.out);
    h.command("drill", {"powershot"});
    h.skipReady();
    DrawList dl = drawNow(h, true);
    CHECK_EQ(dl.panels.size(), 1u);
    if (!dl.panels.empty()) {
        const DrawPanel& p = dl.panels[0];
        CHECK(p.title == "Exercice : Frappe puissante (niveau 1/10)");
        const auto lines = mainLines(p);
        CHECK_EQ(lines.size(), 1u);  // no attempt yet: only what to do, with the seconds left
        if (!lines.empty()) {
            CHECK(lines[0].text.rfind("1. Boost tout droit", 0) == 0);
            CHECK(lines[0].text.find(" s") != std::string::npos);
            CHECK(lines[0].bar > 0.9f);
        }
        bool stopHelp = false;
        for (const auto& l : p.lines) stopHelp = stopHelp || (l.detail && l.text.find("rlt ballmech stop") != std::string::npos);
        CHECK(stopHelp);
    }
    h.run(continueFrom(h, powerShotScenario(PS::FlipShot)));
    h.idle(5);
    CHECK_EQ(h.results.size(), 1u);
    dl = drawNow(h, true);
    CHECK_EQ(dl.panels.size(), 1u);  // the main result panel is not drawn during the drill
    if (!dl.panels.empty()) {
        const auto lines = mainLines(dl.panels[0]);
        CHECK(lines.size() >= 2 && lines.size() <= 3);
        if (lines.size() >= 2) {
            CHECK(lines[0].text.rfind("Réussi ! ", 0) == 0);
            CHECK(lines[0].text.find("/100") != std::string::npos);
            CHECK(lines[0].text.find("(1/1 réussis)") != std::string::npos);
            CHECK(lines.back().text.rfind("1. Boost tout droit", 0) == 0);
        }
        for (const auto& l : lines) CHECK_MSG(!technical(l.text) && !hasEmDash(l.text), l.text);
    }
    // The full HUD adds the details to the same panel.
    dl = drawNow(h, false);
    CHECK_EQ(dl.panels.size(), 1u);
    if (!dl.panels.empty()) CHECK(dl.panels[0].lines.size() > mainLines(dl.panels[0]).size());
}

// ---- Persistence -------------------------------------------------------------------------------------------------
TEST(bm_module_save_load_roundtrip) {
    Harness h;
    h.run(flipResetScenario(FR::Good, true));
    h.run(powerShotScenario(PS::FlipShot));
    h.idle(20);
    h.command("level", {"pinch", "70"});
    h.command("variant", {"45"});
    Store st;
    h.mod.save(st);
    for (const auto& k : st.keysWithPrefix("")) CHECK_MSG(k.rfind("ballmech.", 0) == 0, k);
    CHECK(st.raw().size() < 400);

    Harness h2;
    h2.mod.load(st);
    const auto a = h.mod.uiState();
    const auto b = h2.mod.uiState();
    CHECK_EQ(b.totalResets, a.totalResets);
    CHECK_EQ(b.totalResets, 1);
    CHECK_EQ(b.sessionResets, 0);
    CHECK(b.flickVariant == "45");
    CHECK_NEAR(h2.mod.drillLevel("pinch"), 0.7f, 1e-3f);
    CHECK_EQ(a.mechs.size(), b.mechs.size());
    for (size_t i = 0; i < a.mechs.size() && i < b.mechs.size(); ++i) {
        CHECK_EQ(b.mechs[i].attempts, a.mechs[i].attempts);
        CHECK_EQ(b.mechs[i].successes, a.mechs[i].successes);
        CHECK_NEAR(b.mechs[i].best, a.mechs[i].best, 0.06f);
        CHECK_EQ(b.mechs[i].recent.size(), a.mechs[i].recent.size());
    }
    CHECK_EQ(b.mechs[0].attempts, 1);  // flipreset

    h.command("reset_stats");
    const auto c = h.mod.uiState();
    CHECK_EQ(c.totalResets, 0);
    CHECK_EQ(c.mechs[0].attempts, 0);
    CHECK(!c.hasLast);
}

TEST(bm_module_ui_state) {
    Harness h;
    h.set("rlt_bm_det_pinch", "0");
    const auto u = h.mod.uiState();
    CHECK_EQ(u.mechs.size(), 11u);
    CHECK(u.enabled);
    CHECK(!u.drill.active);
    for (const auto& m : u.mechs) {
        CHECK(!m.title.empty() && !m.what.empty() && !m.key.empty());
        if (m.id == "pinch") CHECK(!m.detectionEnabled);
        else CHECK(m.detectionEnabled);
    }
    h.command("reset_session");
    h.command("bogus");
    CHECK(!h.toasts.empty());
}

TEST(bm_module_weak_claims_stay_silent_in_free_play) {
    // A pop plus one soft air touch: not reported in free play...
    Harness h;
    h.run(airDribbleScenario(AD::Short));
    h.idle(30);
    CHECK_EQ(h.countSkill("ball.airdribble"), 0);
    // ...but a real air dribble is.
    Harness g;
    g.run(airDribbleScenario(AD::WallBump));
    g.idle(30);
    CHECK_EQ(g.countSkill("ball.airdribble"), 1);
    if (!g.results.empty()) CHECK(g.results[0].title == "Air dribble depuis le mur (en touches)");
}
