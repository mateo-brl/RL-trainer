// CoachModule end to end: results -> stats, card (timing, fade, fading rule), records, sessions, streak,
// goals, plan, commands, persistence, settings schema and the "never controls the world" guarantee.

#include "tests/coach/CoachTestUtil.h"

#include "core/modules/coach/CoachCatalog.h"
#include "core/common/Text.h"
#include "core/modules/coach/CoachUtil.h"

using namespace rlt;
using namespace coachtest;

namespace {

void sendN(CoachModule& m, const Env& env, int n, const std::string& skill, float score, bool success,
           const std::string& title = "") {
    for (int i = 0; i < n; ++i) send(m, env, R(skill, score, success).title(title));
}

bool noEmDash(const std::string& s) { return s.find("\xE2\x80\x94") == std::string::npos; }

} // namespace

// ---- Identity and settings ---------------------------------------------------------------------------------
TEST(coach_module_identity) {
    CoachModule m;
    CHECK_EQ(std::string(m.id()), std::string("coach"));
    CHECK_EQ(std::string(m.title()), std::string("Coach & progression"));
}

TEST(coach_schema_defaults_parse_and_match_struct) {
    SettingsSchema schema;
    CoachModule::describeSettings(schema);
    const auto defs = schema.forModule("coach");
    CHECK(defs.size() >= 19);
    const Settings plain;  // struct defaults
    Settings applied;
    schema.applyDefaults(applied);
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_coach_", 0) == 0, d->cvar);
        CHECK(!d->label.empty());
        CHECK(!d->tooltip.empty());
        CHECK(!d->section.empty());
        CHECK(noEmDash(d->label) && noEmDash(d->tooltip) && noEmDash(d->section));
        Settings s;
        CHECK_MSG(d->apply(s, d->defaultValue), d->cvar);
        CHECK_MSG(d->read(plain) == d->defaultValue, d->cvar + " struct default differs from schema default");
        CHECK_MSG(d->read(applied) == d->defaultValue, d->cvar);
    }
    const auto sections = schema.sections("coach");
    CHECK_EQ(sections.size(), static_cast<size_t>(6));
    CHECK_EQ(sections[1], std::string("Carte après chaque essai"));
    // Bounds are enforced.
    Settings s;
    CHECK(schema.apply(s, "rlt_coach_card_seconds", "999"));
    CHECK_NEAR(s.coach.cardSeconds, 15.f, 1e-5f);
    CHECK(schema.apply(s, "rlt_coach_plan_size", "-4"));
    CHECK_EQ(s.coach.planSize, 1);
    CHECK(!schema.apply(s, "rlt_coach_card_x", "abc"));
    // Expert knobs are advanced.
    CHECK(schema.find("rlt_coach_fading_rate")->advanced);
    CHECK(schema.find("rlt_coach_plan_spacing")->advanced);
    CHECK(!schema.find("rlt_coach_fading")->advanced);
    CHECK(!schema.find("rlt_coach_card")->advanced);
}

// ---- Results, stats, card ----------------------------------------------------------------------------------
TEST(coach_result_updates_stats_session_and_card) {
    Env env;
    CoachModule m;
    setup(m, env);
    const Actions a = send(m, env,
                           R("mech.speedflip", 82.f, true)
                               .title("Speedflip")
                               .module("mechanics")
                               .metric("time", "Temps au ballon", 2.05f, "s", 1.95f)
                               .metric("angle", "Angle du flip", 38.f, "°", 30.f)
                               .metric("boost", "Boost utilisé", 33.f, "%", 0.f, false)
                               .tip("me.sf.cancel", "Annule le flip plus tôt.", 1.f)
                               .tip("me.sf.angle", "Flip plus en diagonale.", 3.f));
    CHECK(!hasWorldControl(a));
    const CoachOverview o = m.overview();
    CHECK_EQ(o.totalAttempts, 1);
    CHECK(o.sessionActive);
    CHECK_EQ(o.sessionIndex, 1);
    CHECK_EQ(o.sessionAttempts, 1);
    CHECK_EQ(o.todayAttempts, 1);
    CHECK_EQ(o.today, std::string("2026-10-13"));
    CHECK_EQ(o.skillCount, 1);
    SkillView v;
    CHECK(m.skill("mech.speedflip", v));
    CHECK_EQ(v.attempts, 1);
    CHECK_EQ(v.title, std::string("Speedflip"));
    CHECK_EQ(v.familyLabel, std::string("Mécas mouvement"));
    CHECK(v.launchable);
    CHECK_EQ(v.launch.module, std::string("mechanics"));
    CHECK_EQ(v.daysSince, 0);
    CHECK_EQ(v.mainMistake.code, std::string("me.sf.angle"));  // 1-1 tie in the same attempt: the heavier tip
    const FeedbackCard& c = m.card();
    CHECK(c.active);
    CHECK_EQ(c.title, std::string("Speedflip"));
    CHECK(c.verdict == Verdict::Good);
    CHECK_EQ(c.metricLines.size(), static_cast<size_t>(2));  // default: 2 metrics
    CHECK_EQ(c.metricLines[0], std::string("Temps au ballon : 2.05 s (idéal 1.95 s)"));
    CHECK_EQ(c.metricLines[1], std::string("Angle du flip : 38° (idéal 30°)"));
    CHECK_EQ(c.tip, std::string("Flip plus en diagonale."));  // highest weight
    CHECK(!c.tipRecurring);
    CHECK(!c.newRecord);
}

TEST(coach_card_timing_and_fade_out) {
    Env env;
    CoachModule m;
    setup(m, env);
    send(m, env, R("ball.aerial", 95.f, true).title("Aérienne"));
    const float t0 = env.snap.time;
    CHECK_NEAR(m.cardAlpha(t0), 1.f, 1e-5f);
    CHECK_NEAR(m.cardAlpha(t0 + 3.f), 1.f, 1e-5f);
    CHECK_NEAR(m.cardAlpha(t0 + 3.7f), 0.5f, 1e-3f);  // last 0.6 s fade linearly
    CHECK_NEAR(m.cardAlpha(t0 + 4.f), 0.f, 1e-5f);
    CHECK_NEAR(m.cardAlpha(t0 + 40.f), 0.f, 1e-5f);
    CHECK_NEAR(m.cardAlpha(t0 - 2.f), 0.f, 1e-5f);  // clock went back (new map)
    CHECK_NEAR(m.cardAlpha(std::nanf("")), 0.f, 1e-5f);

    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    CHECK(dl.lines3D.empty());
    const DrawPanel& p = dl.panels[0];
    CHECK_NEAR(p.pos.x, 0.99f * 1920.f, 0.01f);
    CHECK_NEAR(p.pos.y, 0.30f * 1080.f, 0.01f);
    CHECK(p.anchorX == Align::Right);
    CHECK_EQ(p.title, std::string("Aérienne"));
    CHECK_NEAR(p.background.a, 160.f, 1e-3f);
    CHECK(p.lines.size() >= 1);
    CHECK_EQ(p.lines[0].text, std::string("Parfait : 95/100"));
    CHECK_NEAR(p.lines[0].bar, 0.95f, 1e-5f);

    env.advance(3.7f);
    DrawList fading;
    m.draw(env.view(), env.drawCtx(), fading);
    CHECK_EQ(fading.panels.size(), static_cast<size_t>(1));
    CHECK_NEAR(fading.panels[0].background.a, 80.f, 1.f);
    CHECK_NEAR(fading.panels[0].lines[0].color.a, 127.5f, 1.5f);
    CHECK_NEAR(fading.panels[0].titleColor.a, 127.5f, 1.5f);

    env.advance(0.5f);
    DrawList gone;
    m.draw(env.view(), env.drawCtx(), gone);
    CHECK(gone.empty());

    // Longer duration from the settings.
    env.settings.coach.cardSeconds = 8.f;
    setup(m, env);
    send(m, env, R("ball.aerial", 95.f, true));
    CHECK_NEAR(m.cardAlpha(env.snap.time + 7.f), 1.f, 1e-5f);
    CHECK(m.cardAlpha(env.snap.time + 7.6f) < 1.f);
}

TEST(coach_card_draw_gating_position_and_scale) {
    Env env;
    CoachModule m;
    setup(m, env);
    send(m, env, R("ball.aerial", 30.f, false));
    // No snapshot (replay, loading): nothing.
    env.hasSnapshot = false;
    DrawList none;
    m.draw(env.view(), env.drawCtx(), none);
    CHECK(none.empty());
    env.hasSnapshot = true;
    // Position and scale from settings and the HUD scale.
    env.settings.coach.cardX = 0.5f;
    env.settings.coach.cardY = 0.1f;
    env.settings.coach.cardScale = 1.5f;
    setup(m, env);
    DrawContext dc{env.settings, env.game, env.profile, env.snap, true, false, 2.f};
    DrawList dl;
    m.draw(env.view(), dc, dl);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    CHECK_NEAR(dl.panels[0].pos.x, 960.f, 0.01f);
    CHECK_NEAR(dl.panels[0].pos.y, 108.f, 0.01f);
    CHECK_NEAR(dl.panels[0].scale, 3.f, 1e-5f);
    // Card switched off.
    env.settings.coach.cardEnabled = false;
    setup(m, env);
    DrawList off;
    m.draw(env.view(), env.drawCtx(), off);
    CHECK(off.empty());
    send(m, env, R("ball.aerial", 30.f, false));
    CHECK(!m.card().active);
}

TEST(coach_card_verdict_colors_and_options) {
    Env env;
    CoachModule m;
    setup(m, env);
    auto lineOf = [&](const AttemptResult& r) {
        send(m, env, r);
        DrawList dl;
        m.draw(env.view(), env.drawCtx(), dl);
        return dl.panels.empty() ? PanelLine{} : dl.panels[0].lines[0];
    };
    const PanelLine perfect = lineOf(R("x.a", 95.f, true));
    CHECK_EQ(perfect.text, std::string("Parfait : 95/100"));
    CHECK(perfect.color.g > 200.f && perfect.color.r < 100.f);
    const PanelLine good = lineOf(R("x.a", 75.f, true));
    CHECK_EQ(good.text, std::string("Bien : 75/100"));
    CHECK(good.color.r > 100.f && good.color.g > 200.f);
    const PanelLine ok = lineOf(R("x.a", 50.f, true));
    CHECK_EQ(ok.text, std::string("Correct : 50/100"));
    CHECK(ok.color.r > 200.f && ok.color.g > 200.f && ok.color.b < 50.f);
    const PanelLine fail = lineOf(R("x.a", 20.f, false));
    CHECK_EQ(fail.text, std::string("Raté : 20/100"));
    CHECK(fail.color.r > 200.f && fail.color.g < 100.f);
    // The producer's verdict wins over the score; None is derived from the score.
    const PanelLine forced = lineOf(R("x.a", 95.f, false).verdict(Verdict::Fail));
    CHECK_EQ(forced.text, std::string("Raté : 95/100"));
    const PanelLine derived = lineOf(R("x.a", 72.f, true).verdict(Verdict::None));
    CHECK_EQ(derived.text, std::string("Bien : 72/100"));

    // Metrics count and tip switch.
    env.settings.coach.cardMetrics = 1;
    env.settings.coach.cardTip = false;
    setup(m, env);
    send(m, env,
         R("first_touch.ground", 60.f, true)
             .metric("speed_diff", "Écart de vitesse", 120.f, "uu/s", 0.f)
             .metric("height", "Hauteur", 140.f, "uu", 0.f, false)
             .tip("ft.too_fast", "Ralentis."));
    CHECK_EQ(m.card().metricLines.size(), static_cast<size_t>(1));
    CHECK_EQ(m.card().metricLines[0], std::string("Écart de vitesse : +120 uu/s (idéal 0 uu/s)"));
    CHECK(m.card().tip.empty());
    env.settings.coach.cardMetrics = 3;
    env.settings.coach.cardTip = true;
    setup(m, env);
    send(m, env,
         R("first_touch.ground", 60.f, true)
             .metric("speed_diff", "Écart de vitesse", 120.f, "uu/s", 0.f)
             .metric("height", "Hauteur", 140.f, "uu", 0.f, false)
             .metric("bad", "NaN", std::nanf(""), "uu", 0.f, false));
    CHECK_EQ(m.card().metricLines.size(), static_cast<size_t>(2));
    CHECK_EQ(m.card().metricLines[1], std::string("Hauteur : 140 uu"));
}

TEST(coach_card_flags_recurring_mistake) {
    Env env;
    CoachModule m;
    setup(m, env);
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    CHECK(!m.card().tipRecurring);
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.angle", "Aligne-toi."));
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    CHECK(!m.card().tipRecurring);  // 2 of the last 5
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    CHECK(m.card().tipRecurring);  // 3 of the last 5
    CHECK_EQ(m.card().recurringNote, std::string("Erreur fréquente : 3 des 5 derniers essais"));
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    bool orange = false;
    for (const auto& l : dl.panels[0].lines)
        if (l.text == "Laisse la balle venir." && l.color.r == 255.f && l.color.g == 140.f) orange = true;
    CHECK(orange);
}

TEST(coach_card_long_tip_is_wrapped_and_bounded) {
    Env env;
    CoachModule m;
    setup(m, env);
    const std::string tip(400, 'a');  // sanitized to 160 chars, single "word"
    send(m, env, R("x.y", 50.f, true).tip("c", tip + " " + tip));
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    CHECK(dl.panels[0].lines.size() <= 12);
    const std::string words =
        "Garde ta voiture sous la balle et dose le boost pour rester collé à elle pendant tout le dribble.";
    send(m, env, R("x.y", 50.f, true).tip("c", words));
    DrawList dl2;
    m.draw(env.view(), env.drawCtx(), dl2);
    size_t tipLines = 0;
    for (const auto& l : dl2.panels[0].lines) {
        CHECK(coach::utf8Length(l.text) <= 60);
        if (l.color == Color(215, 230, 255)) ++tipLines;
    }
    CHECK(tipLines >= 2);
}

// Compact HUD: the card does not repeat the module panel (verdict, score, metrics, tip). It shows a new record
// and a goal reached, else the recurring mistake, in at most 2 lines, and nothing at all otherwise.
namespace {

DrawList drawCompact(CoachModule& m, const Env& env) {
    DrawContext dc{env.settings, env.game, env.profile, env.snap, env.hasSnapshot, false, 1.f, true};
    DrawList dl;
    m.draw(env.view(), dc, dl);
    return dl;
}

} // namespace

TEST(coach_card_compact_hides_what_the_module_panel_says) {
    Env env;
    env.settings.coach.feedbackFading = false;
    CoachModule m;
    setup(m, env);
    send(m, env,
         R("mech.speedflip", 62.f, true)
             .title("Speedflip")
             .metric("angle", "Angle du flip", 2.f, "°", 30.f)
             .tip("sf.angle", "Flip plus en diagonale."));
    CHECK(m.card().active);
    DrawList full;
    m.draw(env.view(), env.drawCtx(), full);
    CHECK_EQ(full.panels.size(), static_cast<size_t>(1));
    CHECK(drawCompact(m, env).empty());  // same verdict and tip as the module panel: no card
}

TEST(coach_card_compact_record_and_goal) {
    Env env;
    env.settings.coach.feedbackFading = false;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"custom", "attempts", "any", "2"});
    send(m, env, R("mech.speedflip", 80.f, true).title("Speedflip").record(2.05f, true));  // first record: silent
    send(m, env, R("mech.speedflip", 92.f, true).title("Speedflip").record(1.92f, true));  // record and goal 2/2
    CHECK(m.card().newRecord);
    CHECK(m.card().goalReached);
    DrawList dl = drawCompact(m, env);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    if (dl.panels.empty()) return;
    const DrawPanel& p = dl.panels[0];
    CHECK_EQ(p.title, std::string("Speedflip"));
    CHECK_EQ(p.lines.size(), static_cast<size_t>(2));
    CHECK(p.lines[0].text.find("Nouveau record ! 1.92 s") == 0);
    CHECK(p.lines[1].text.find("Objectif atteint") == 0);
    for (const auto& l : p.lines) CHECK(l.text.find("/100") == std::string::npos);
    // A record in milliseconds keeps its number in the full card only.
    send(m, env, R("mech.flipcancel", 70.f, true).metric("cancel", "Annulation", 60.f, "ms", 0.f).record(60.f, true));
    send(m, env, R("mech.flipcancel", 75.f, true).metric("cancel", "Annulation", 45.f, "ms", 0.f).record(45.f, true));
    CHECK(m.card().newRecord);
    dl = drawCompact(m, env);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    if (dl.panels.empty()) return;
    CHECK_EQ(dl.panels[0].lines.size(), static_cast<size_t>(1));
    CHECK_EQ(dl.panels[0].lines[0].text, std::string("Nouveau record !"));
    CHECK(m.card().recordLine.find(" ms") != std::string::npos);
}

TEST(coach_card_compact_recurring_mistake_gives_the_count_not_the_tip) {
    Env env;
    env.settings.coach.feedbackFading = false;
    CoachModule m;
    setup(m, env);
    const std::string tip = "Garde ta voiture sous la balle et dose le boost pour rester collé à elle pendant tout le dribble.";
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    CHECK(drawCompact(m, env).empty());
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse la balle venir."));
    CHECK(m.card().tipRecurring);
    DrawList dl = drawCompact(m, env);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    if (dl.panels.empty()) return;
    // The module panel already shows the tip: the card only says how often it came back.
    CHECK_EQ(dl.panels[0].lines.size(), static_cast<size_t>(1));
    CHECK_EQ(dl.panels[0].lines[0].text, std::string("Erreur fréquente : 3 des 5 derniers essais"));
    CHECK(dl.panels[0].lines[0].color == colors::Orange);
    // A long tip is never repeated either: still one short line.
    for (int i = 0; i < 3; ++i) send(m, env, R("x.y", 40.f, false).tip("c", tip));
    dl = drawCompact(m, env);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    if (dl.panels.empty()) return;
    CHECK_EQ(dl.panels[0].lines.size(), static_cast<size_t>(1));
    for (const auto& l : dl.panels[0].lines) CHECK(l.text.find("dribble") == std::string::npos);
    // The preview shows the compact card too (placement from the settings).
    command(m, env, "preview");
    dl = drawCompact(m, env);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    if (!dl.panels.empty()) CHECK(dl.panels[0].lines.size() == 1);
}

TEST(coach_feedback_fading_in_module) {
    Env env;
    CoachModule m;
    setup(m, env);
    for (int i = 0; i < 10; ++i) {
        send(m, env, R("mech.speedflip", 90.f, true));
        CHECK(m.card().active);  // learning phase: every card
    }
    send(m, env, R("mech.speedflip", 90.f, true));
    CHECK(!m.card().active);  // mastered: 1st success hidden
    send(m, env, R("mech.speedflip", 90.f, true));
    CHECK(!m.card().active);
    send(m, env, R("mech.speedflip", 90.f, true));
    CHECK(m.card().active);  // every 3rd success
    send(m, env, R("mech.speedflip", 30.f, false));
    CHECK(m.card().active);  // failure always shown
    // Another skill is still new: always shown.
    send(m, env, R("mech.halfflip", 90.f, true));
    CHECK(m.card().active);
    // A record is always shown.
    send(m, env, R("mech.speedflip", 90.f, true).record(2.1f, true));  // first record (silent)
    CHECK(!m.card().active);
    send(m, env, R("mech.speedflip", 90.f, true).record(2.0f, true));
    CHECK(m.card().active);
    CHECK(m.card().newRecord);
    // Fading off: every card.
    env.settings.coach.feedbackFading = false;
    setup(m, env);
    for (int i = 0; i < 4; ++i) {
        send(m, env, R("mech.speedflip", 90.f, true));
        CHECK(m.card().active);
    }
}

// ---- Records -------------------------------------------------------------------------------------------
TEST(coach_records_toast_chat_and_save) {
    Env env;
    CoachModule m;
    setup(m, env);
    Actions a = send(m, env, R("mech.speedflip", 80.f, true).title("Speedflip").record(2.05f, true));
    CHECK(a.toasts.empty());  // first value: no celebration
    CHECK(a.chat.empty());
    CHECK(a.requestSave);
    a = send(m, env, R("mech.speedflip", 92.f, true).title("Speedflip").record(1.92f, true));
    CHECK(toastContains(a, "Nouveau record !"));
    CHECK(toastContains(a, "Speedflip : 1.92 s (avant 2.05 s)"));
    CHECK(chatContains(a, "Nouveau record : Speedflip 1.92 s (avant 2.05 s)"));
    CHECK(a.requestSave);
    CHECK(m.card().newRecord);
    CHECK_EQ(m.card().recordLine, std::string("Nouveau record ! 1.92 s (avant 2.05 s)"));
    CHECK_EQ(m.overview().sessionRecords, 1);
    SkillView v;
    CHECK(m.skill("mech.speedflip", v));
    CHECK(v.hasRecord && v.lowerIsBetter);
    CHECK_EQ(v.recordText, std::string("1.92 s"));
    // Slower: no record.
    a = send(m, env, R("mech.speedflip", 92.f, true).record(1.95f, true));
    CHECK(a.toasts.empty());
    // Settings switch both channels off.
    env.settings.coach.recordChat = false;
    env.settings.coach.recordToast = false;
    setup(m, env);
    a = send(m, env, R("mech.speedflip", 95.f, true).record(1.80f, true));
    CHECK(a.toasts.empty());
    CHECK(a.chat.empty());
    CHECK(a.requestSave);
    CHECK_EQ(m.overview().sessionRecords, 2);
}

// ---- Sessions --------------------------------------------------------------------------------------------
TEST(coach_session_summary_on_map_unload) {
    Env env;
    CoachModule m;
    setup(m, env);
    event(m, env, GameEventKind::ContextChanged);
    sendN(m, env, 3, "kickoff.diagonal", 85.f, true, "Kickoff");
    sendN(m, env, 3, "mech.wavedash", 40.f, false, "Wavedash");
    const Actions a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(chatContains(a, "Session : 6 essais, moyenne 63, point fort Kickoff (diagonale), à travailler Wavedash."));
    CHECK(a.requestSave);
    CHECK(!m.overview().sessionActive);
    CHECK(!m.card().active);
    CHECK_EQ(m.sessionHistory().size(), static_cast<size_t>(1));
    CHECK_EQ(m.sessionHistory()[0].index, 1);
    CHECK_EQ(m.sessionHistory()[0].attempts, 6);
    CHECK_EQ(m.sessionHistory()[0].strong, std::string("Kickoff (diagonale)"));
    // A second unload does not repeat the summary.
    CHECK(event(m, env, GameEventKind::MapUnloaded).chat.empty());
    // Next result starts session 2.
    send(m, env, R("ball.aerial", 50.f, true));
    CHECK_EQ(m.overview().sessionIndex, 2);
    CHECK_EQ(m.overview().sessionAttempts, 1);
}

TEST(coach_session_survives_date_and_category_refresh) {
    Env env;
    CoachModule m;
    setup(m, env);
    event(m, env, GameEventKind::ContextChanged);  // first context: just remembered
    sendN(m, env, 4, "ball.aerial", 70.f, true);
    env.game.localDate = "2026-10-14";
    env.game.category = MapCategory::Rings;
    Actions a = event(m, env, GameEventKind::ContextChanged);
    CHECK(a.chat.empty());
    CHECK(m.overview().sessionActive);
    CHECK_EQ(m.overview().sessionAttempts, 4);
    CHECK_EQ(m.today(), std::string("2026-10-14"));
    // A real map change closes it (summary because >= 3 attempts).
    env.game.mapName = "Labs_Underpass_P";
    a = event(m, env, GameEventKind::ContextChanged);
    CHECK(chatContains(a, "Session : 4 essais"));
    CHECK(!m.overview().sessionActive);
    // Mode change too.
    sendN(m, env, 3, "ball.aerial", 70.f, true);
    env.game.mode = GameMode::Menu;
    a = event(m, env, GameEventKind::ContextChanged);
    CHECK(chatContains(a, "Session : 3 essais"));
}

TEST(coach_session_summary_settings) {
    Env env;
    CoachModule m;
    setup(m, env);
    sendN(m, env, 2, "ball.aerial", 70.f, true);
    Actions a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(a.chat.empty());  // shorter than the minimum (3)
    CHECK_EQ(m.sessionHistory().size(), static_cast<size_t>(1));
    env.settings.coach.summaryChat = false;
    setup(m, env);
    sendN(m, env, 5, "ball.aerial", 70.f, true);
    a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(a.chat.empty());
    CHECK_EQ(m.sessionHistory().size(), static_cast<size_t>(2));
    // Summary mentions the streak once the day is validated.
    env.settings.coach.summaryChat = true;
    env.settings.coach.streakMinAttempts = 3;
    setup(m, env);
    sendN(m, env, 3, "ball.aerial", 70.f, true);
    a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(chatContains(a, "Série : 1 jour."));
}

TEST(coach_new_session_command) {
    Env env;
    CoachModule m;
    setup(m, env);
    sendN(m, env, 3, "ball.aerial", 70.f, true, "Aérienne");
    Actions a = command(m, env, "new_session");
    CHECK(chatContains(a, "Session : 3 essais"));
    CHECK(chatContains(a, "Session 2 lancée. Bon entraînement !"));
    for (const auto& c : a.chat) CHECK(hudText(c.text, false).find("deg") == std::string::npos);
    CHECK(m.overview().sessionActive);
    CHECK_EQ(m.overview().sessionIndex, 2);
    CHECK_EQ(m.overview().sessionAttempts, 0);
    // An empty session gives its number back.
    a = command(m, env, "new_session");
    CHECK(chatContains(a, "Session 2 lancée"));
    CHECK_EQ(m.sessionHistory().size(), static_cast<size_t>(1));
    // Summary command.
    a = command(m, env, "summary");
    CHECK(chatContains(a, "Pas encore d'essai"));
    send(m, env, R("ball.aerial", 70.f, true));
    a = command(m, env, "summary");
    CHECK(chatContains(a, "Session : 1 essai, moyenne 70."));
    CHECK_EQ(m.currentSessionSummary(), std::string("Session : 1 essai, moyenne 70."));
}

TEST(coach_session_time_from_ticks) {
    Env env;
    CoachModule m;
    setup(m, env);
    Actions out;
    m.onTick(env.tick(), out);  // no session yet: nothing counted
    send(m, env, R("ball.aerial", 70.f, true));
    for (int i = 0; i < 240; ++i) {
        env.advance(kTickDt);
        m.onTick(env.tick(), out);
    }
    CHECK_NEAR(m.overview().sessionSeconds, 2.f, 0.02f);
    // Paused ticks do not count, a big frame jump counts at most 0.25 s.
    env.snap.paused = true;
    for (int i = 0; i < 120; ++i) {
        env.advance(kTickDt);
        m.onTick(env.tick(), out);
    }
    env.snap.paused = false;
    env.advance(30.f);
    m.onTick(env.tick(), out);
    CHECK_NEAR(m.overview().sessionSeconds, 2.25f, 0.02f);
    // Frames going backwards (new map) are ignored.
    env.snap.frame = 10;
    m.onTick(env.tick(), out);
    CHECK_NEAR(m.overview().sessionSeconds, 2.25f, 0.02f);
    CHECK(out.empty());
}

TEST(coach_break_reminder) {
    Env env;
    env.settings.coach.breakMinutes = 1;
    CoachModule m;
    setup(m, env);
    send(m, env, R("ball.aerial", 70.f, true));
    int toasts = 0;
    auto run = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) {
            env.advance(kTickDt);
            Actions a;
            m.onTick(env.tick(), a);
            if (toastContains(a, "Petite pause ?")) ++toasts;
            CHECK(!hasWorldControl(a));
        }
    };
    run(60 * 120 - 10);
    CHECK_EQ(toasts, 0);
    run(20);
    CHECK_EQ(toasts, 1);  // after one minute of session
    run(50 * 120);
    CHECK_EQ(toasts, 1);  // not again before the next minute
    run(15 * 120);
    CHECK_EQ(toasts, 2);
    // New session: counter restarts. 0 = never.
    command(m, env, "new_session");
    env.settings.coach.breakMinutes = 0;
    setup(m, env);
    run(200 * 120);
    CHECK_EQ(toasts, 2);
}

TEST(coach_summary_mentions_goals) {
    Env env;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"custom", "attempts", "any", "2"});
    command(m, env, "goal_add", {"speedflip10"});
    sendN(m, env, 3, "ball.aerial", 70.f, true, "Aérienne");
    CHECK(m.currentSessionSummary().find("objectifs 1/2") != std::string::npos);
    const Actions a = event(m, env, GameEventKind::MapUnloaded);
    CHECK(chatContains(a, "objectifs 1/2."));
}

// ---- Streak ------------------------------------------------------------------------------------------------
TEST(coach_streak_toasts_and_overview) {
    Env env;
    CoachModule m;
    setup(m, env);
    env.game.localDate = "2026-10-12";
    for (int i = 0; i < 9; ++i) CHECK(!toastContains(send(m, env, R("ball.aerial", 70.f, true)), "Série"));
    Actions a = send(m, env, R("ball.aerial", 70.f, true));
    CHECK(toastContains(a, "Série lancée !"));
    CHECK(a.requestSave);
    CHECK_EQ(m.overview().streak, 1);
    // Only once per day.
    CHECK(!toastContains(send(m, env, R("ball.aerial", 70.f, true)), "Série"));
    env.game.localDate = "2026-10-13";
    for (int i = 0; i < 9; ++i) send(m, env, R("ball.aerial", 70.f, true));
    CHECK_EQ(m.overview().streak, 1);  // still alive (yesterday), today not validated yet
    a = send(m, env, R("ball.aerial", 70.f, true));
    CHECK(toastContains(a, "Série : 2 jours !"));
    CHECK_EQ(m.overview().streak, 2);
    CHECK_EQ(m.overview().bestStreak, 2);
    // Two days later: broken, best kept.
    env.game.localDate = "2026-10-15";
    event(m, env, GameEventKind::ContextChanged);
    CHECK_EQ(m.overview().streak, 0);
    CHECK_EQ(m.overview().bestStreak, 2);
    // Toasts can be switched off.
    env.settings.coach.goalToasts = false;
    setup(m, env);
    for (int i = 0; i < 10; ++i) CHECK(!toastContains(send(m, env, R("ball.aerial", 70.f, true)), "Série"));
    const auto days = m.recentDays(5);
    CHECK_EQ(days.size(), static_cast<size_t>(5));
    CHECK_EQ(days.back().date, std::string("2026-10-15"));
    CHECK_EQ(days.back().attempts, 10);
    CHECK_EQ(days[0].attempts, 0);   // 10-11
    CHECK_EQ(days[1].attempts, 11);  // 10-12
    CHECK_EQ(days[3].attempts, 0);   // 10-14
}

// ---- Goals -------------------------------------------------------------------------------------------------
TEST(coach_goal_commands_and_progress) {
    Env env;
    CoachModule m;
    setup(m, env);
    Actions a = command(m, env, "goal_add", {"speedflip10"});
    CHECK(toastContains(a, "Objectif ajouté"));
    CHECK(a.requestSave);
    CHECK_EQ(m.goals().size(), static_cast<size_t>(1));
    a = command(m, env, "goal_add", {"speedflip10"});
    CHECK(chatContains(a, "déjà actif"));
    a = command(m, env, "goal_add", {"nope"});
    CHECK(chatContains(a, "Objectif inconnu : nope"));
    a = command(m, env, "goal_add");
    CHECK(chatContains(a, "speedflip10"));
    a = command(m, env, "goal_add", {"custom", "success", "mech.halfflip", "2"});
    CHECK_EQ(m.goals().size(), static_cast<size_t>(2));
    CHECK_EQ(m.goals()[1].label, std::string("2 réussites : Half-flip"));
    a = command(m, env, "goal_add", {"custom", "success"});
    CHECK(chatContains(a, "Format"));
    command(m, env, "goal_add", {"ft70"});
    a = command(m, env, "goal_add", {"score80"});
    CHECK(chatContains(a, "déjà 3 objectifs"));
    CHECK_EQ(m.goals().size(), static_cast<size_t>(3));

    send(m, env, R("mech.speedflip", 80.f, true));
    CHECK_EQ(m.card().goalLine, std::string("Objectif 1/10 : 10 speedflips réussis"));
    send(m, env, R("mech.halfflip", 80.f, true));
    a = send(m, env, R("mech.halfflip", 80.f, true));
    CHECK(toastContains(a, "Objectif atteint !"));
    CHECK(toastContains(a, "2 réussites : Half-flip"));
    CHECK_EQ(m.card().goalLine, std::string("Objectif atteint : 2 réussites : Half-flip"));
    CHECK(m.goals()[1].done);
    // Done goals no longer move.
    a = send(m, env, R("mech.halfflip", 80.f, true));
    CHECK(!toastContains(a, "Objectif atteint"));

    a = command(m, env, "goal_remove", {"9"});
    CHECK(chatContains(a, "invalide"));
    command(m, env, "goal_remove", {"1"});
    CHECK_EQ(m.goals().size(), static_cast<size_t>(2));
    CHECK_EQ(m.goals()[0].label, std::string("2 réussites : Half-flip"));
    command(m, env, "goal_clear");
    CHECK(m.goals().empty());
}

TEST(coach_goals_reset_each_session_and_all_done_toast) {
    Env env;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"custom", "attempts", "any", "2"});
    command(m, env, "goal_add", {"custom", "success", "ball.aerial", "1"});
    send(m, env, R("ball.aerial", 50.f, false));
    Actions a = send(m, env, R("ball.aerial", 80.f, true));
    CHECK(toastContains(a, "Session réussie !"));
    CHECK(m.goals()[0].done && m.goals()[1].done);
    command(m, env, "new_session");
    CHECK(!m.goals()[0].done);
    CHECK_EQ(m.goals()[0].progress, 0);
    // Toasts off: no goal toast.
    env.settings.coach.goalToasts = false;
    setup(m, env);
    send(m, env, R("ball.aerial", 80.f, true));
    a = send(m, env, R("ball.aerial", 80.f, true));
    CHECK(a.toasts.empty());
    CHECK(m.goals()[0].done);
}

TEST(coach_goal_focus_follows_the_plan) {
    Env env;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"focus"});
    CHECK_EQ(m.goals().size(), static_cast<size_t>(1));
    CHECK_EQ(m.goals()[0].scope, std::string("first_touch"));  // starter plan: first touch
    CHECK_EQ(m.goals()[0].label, std::string("10 réussites : Premières touches"));
    command(m, env, "goal_clear");
    sendN(m, env, 12, "ball.aerial", 25.f, false, "Aérienne");
    sendN(m, env, 12, "mech.speedflip", 90.f, true, "Speedflip");
    command(m, env, "goal_add", {"focus"});
    CHECK_EQ(m.goals()[0].scope, std::string("ball.aerial"));
    CHECK_EQ(m.goals()[0].label, std::string("10 réussites : Tir aérien"));
    CHECK_EQ(m.goals()[0].preset, std::string("focus"));
}

// ---- Plan ----------------------------------------------------------------------------------------------
TEST(coach_plan_live_and_command) {
    Env env;
    CoachModule m;
    setup(m, env);
    CHECK_EQ(m.plan().size(), static_cast<size_t>(3));
    CHECK(m.plan()[0].starter);
    Actions a = command(m, env, "plan");
    CHECK_EQ(a.chat.size(), static_cast<size_t>(4));
    CHECK(chatContains(a, "Plan conseillé :"));
    CHECK(chatContains(a, "-> rlt firsttouch drill roll_in"));
    sendN(m, env, 12, "mech.wavedash", 30.f, false, "Wavedash");
    sendN(m, env, 12, "kickoff.diagonal", 80.f, true, "Kickoff diagonale");
    CHECK_EQ(m.plan()[0].skill, std::string("mech.wavedash"));
    CHECK_EQ(m.plan()[0].launch.consoleLine(), std::string("rlt mechanics drill wavedash"));
    CHECK(!m.plan()[0].starter);
    env.settings.coach.planSize = 5;
    setup(m, env);
    CHECK(m.plan().size() <= 5);
    CHECK(m.plan().size() >= 3);
    env.settings.coach.planSize = 1;
    setup(m, env);
    CHECK_EQ(m.plan().size(), static_cast<size_t>(1));
}

TEST(coach_plan_spaced_practice_with_dates) {
    Env env;
    CoachModule m;
    setup(m, env);
    env.game.localDate = "2026-10-09";
    sendN(m, env, 20, "mech.speedflip", 75.f, true, "Speedflip");
    env.game.localDate = "2026-10-13";
    sendN(m, env, 20, "ball.aerial", 70.f, true, "Aérienne");
    CHECK_EQ(m.plan()[0].skill, std::string("mech.speedflip"));
    CHECK_MSG(m.plan()[0].reason.find("Pas pratiqué depuis 4 jours") == 0, m.plan()[0].reason);
    SkillView v;
    CHECK(m.skill("mech.speedflip", v));
    CHECK_EQ(v.daysSince, 4);
    CHECK_EQ(v.lastDate, std::string("2026-10-09"));
}

TEST(coach_top_mistakes_across_skills) {
    Env env;
    CoachModule m;
    setup(m, env);
    for (int i = 0; i < 4; ++i)
        send(m, env, R("first_touch.ground", 40.f, false).tip("ft.too_fast", "Laisse venir la balle."));
    for (int i = 0; i < 4; ++i)
        send(m, env, R("mech.halfflip", 40.f, false).tip(i < 2 ? "me.hf.cancel" : "me.hf.slow", "Annule plus tôt."));
    send(m, env, R("ball.aerial", 40.f, false).tip("bm.once", "Une seule fois."));
    const auto top = m.topMistakes(5);
    CHECK_EQ(top.size(), static_cast<size_t>(2));  // the single occurrence is not "recurring"
    CHECK_EQ(top[0].skill, std::string("first_touch.ground"));
    CHECK_EQ(top[0].mistake.count, 4);
    CHECK_EQ(top[1].skill, std::string("mech.halfflip"));
    CHECK_EQ(m.topMistakes(1).size(), static_cast<size_t>(1));
}

// ---- Views -------------------------------------------------------------------------------------------------
TEST(coach_skill_views_grouped_and_trend) {
    Env env;
    CoachModule m;
    setup(m, env);
    for (int i = 0; i < 20; ++i)
        send(m, env, R("mech.speedflip", 30.f + 3.f * static_cast<float>(i), true).title("Speedflip"));
    send(m, env, R("first_touch.air", 50.f, true));
    send(m, env, R("zzz.custom", 50.f, true));
    send(m, env, R("ball.aerial", 50.f, true));
    const auto views = m.skills();
    CHECK_EQ(views.size(), static_cast<size_t>(4));
    CHECK_EQ(views[0].family, std::string("first_touch"));
    CHECK_EQ(views[1].family, std::string("mech"));
    CHECK_EQ(views[2].family, std::string("ball"));
    CHECK_EQ(views[3].family, std::string("zzz"));
    CHECK(!views[3].launchable);
    CHECK(views[1].trend == Trend::Up);
    CHECK_EQ(views[1].scores.size(), static_cast<size_t>(20));
    SkillView missing;
    CHECK(!m.skill("nope", missing));
}

// ---- Commands ----------------------------------------------------------------------------------------------
TEST(coach_reset_all_command) {
    Env env;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"speedflip10"});
    sendN(m, env, 5, "mech.speedflip", 80.f, true);
    event(m, env, GameEventKind::MapUnloaded);
    send(m, env, R("mech.speedflip", 80.f, true));
    const Actions a = command(m, env, "reset_all");
    CHECK(a.requestSave);
    CHECK(chatContains(a, "réinitialisée"));
    const CoachOverview o = m.overview();
    CHECK_EQ(o.totalAttempts, 0);
    CHECK_EQ(o.skillCount, 0);
    CHECK_EQ(o.todayAttempts, 0);
    CHECK(!o.sessionActive);
    CHECK(m.sessionHistory().empty());
    CHECK(m.skills().empty());
    CHECK(!m.card().active);
    CHECK(m.plan()[0].starter);
    CHECK_EQ(m.goals().size(), static_cast<size_t>(1));  // goal definitions are preferences: kept
    CHECK_EQ(m.goals()[0].progress, 0);
    send(m, env, R("mech.speedflip", 80.f, true));
    CHECK_EQ(m.overview().sessionIndex, 1);
}

TEST(coach_unknown_command_and_preview) {
    Env env;
    CoachModule m;
    setup(m, env);
    Actions a = command(m, env, "dance");
    CHECK(chatContains(a, "Commande coach inconnue : dance"));
    a = command(m, env, "preview");
    CHECK(m.card().active);
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK_EQ(dl.panels.size(), static_cast<size_t>(1));
    CHECK(dl.panels[0].lines.size() >= 5);
    CHECK(!hasWorldControl(a));
}

TEST(coach_context_change_clears_card) {
    Env env;
    CoachModule m;
    setup(m, env);
    event(m, env, GameEventKind::ContextChanged);
    send(m, env, R("ball.aerial", 50.f, true));
    CHECK(m.card().active);
    env.game.mode = GameMode::Replay;
    event(m, env, GameEventKind::ContextChanged);
    CHECK(!m.card().active);
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK(dl.empty());
}

TEST(coach_revision_tracks_ui_lists) {
    Env env;
    CoachModule m;
    setup(m, env);
    unsigned r = m.revision();
    send(m, env, R("ball.aerial", 50.f, true));
    CHECK(m.revision() != r);
    r = m.revision();
    Actions out;
    env.advance(kTickDt);
    m.onTick(env.tick(), out);
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK_EQ(m.revision(), r);  // ticks and frames do not invalidate the UI copy
    event(m, env, GameEventKind::MapUnloaded);
    CHECK(m.revision() != r);  // session history changed
    r = m.revision();
    command(m, env, "reset_all");
    CHECK(m.revision() != r);
}

// ---- Persistence ---------------------------------------------------------------------------------------
TEST(coach_save_load_roundtrip) {
    Env env;
    CoachModule m;
    setup(m, env);
    command(m, env, "goal_add", {"speedflip10"});
    command(m, env, "goal_add", {"custom", "score", "first_touch", "5", "75"});
    env.game.localDate = "2026-10-12";
    for (int i = 0; i < 12; ++i)
        send(m, env,
             R("mech.speedflip", 60.f + static_cast<float>(i), i % 3 != 0)
                 .title("Speedflip")
                 .record(2.2f - 0.01f * static_cast<float>(i), true)
                 .tip("me.sf.cancel", "Annule plus tôt.")
                 .metric("time", "Temps", 2.1f, "s", 1.95f));
    event(m, env, GameEventKind::MapUnloaded);
    env.game.localDate = "2026-10-13";
    for (int i = 0; i < 11; ++i) send(m, env, R("first_touch.ground", 70.f, true).title("Première touche au sol"));

    Store st;
    st.setString("other.key", "kept");
    m.save(st);
    CHECK_EQ(st.getString("other.key"), std::string("kept"));  // other modules' keys untouched
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("coach.", 0) == 0 || kv.first == "other.key", kv.first);
    Store parsed;
    parsed.parse(st.serialize());

    CoachModule l;
    setup(l, env);
    event(l, env, GameEventKind::ContextChanged);  // gives the date
    l.load(parsed);
    const CoachOverview a = m.overview(), b = l.overview();
    CHECK_EQ(b.totalAttempts, a.totalAttempts);
    CHECK_EQ(b.skillCount, a.skillCount);
    CHECK_EQ(b.todayAttempts, a.todayAttempts);
    CHECK_EQ(b.streak, a.streak);
    CHECK_EQ(b.bestStreak, a.bestStreak);
    CHECK_EQ(l.sessionHistory().size(), m.sessionHistory().size());
    CHECK_EQ(l.sessionHistory()[0].attempts, 12);
    CHECK_EQ(l.goals().size(), static_cast<size_t>(2));
    CHECK_EQ(l.goals()[1].label, m.goals()[1].label);
    CHECK_EQ(l.goals()[0].progress, 0);
    const auto va = m.skills(), vb = l.skills();
    CHECK_EQ(va.size(), vb.size());
    for (size_t i = 0; i < va.size() && i < vb.size(); ++i) {
        CHECK_EQ(va[i].id, vb[i].id);
        CHECK_EQ(va[i].title, vb[i].title);
        CHECK_EQ(va[i].attempts, vb[i].attempts);
        CHECK_NEAR(va[i].averageRecent, vb[i].averageRecent, 0.01f);
        CHECK_EQ(va[i].recordText, vb[i].recordText);
        CHECK_EQ(va[i].mainMistake.code, vb[i].mainMistake.code);
        CHECK_EQ(va[i].lastDate, vb[i].lastDate);
    }
    CHECK_EQ(l.plan().size(), m.plan().size());
    for (size_t i = 0; i < l.plan().size() && i < m.plan().size(); ++i) CHECK_EQ(l.plan()[i].skill, m.plan()[i].skill);
    // The next session number continues (session 2 was still running when saved: its number is used).
    event(l, env, GameEventKind::MapUnloaded);
    send(l, env, R("ball.aerial", 50.f, true));
    CHECK_EQ(l.overview().sessionIndex, 3);
    // Loading an empty store gives a clean coach.
    CoachModule e;
    e.load(Store{});
    CHECK_EQ(e.overview().totalAttempts, 0);
    CHECK(e.plan()[0].starter);
}

TEST(coach_skills_and_history_bounded) {
    Env env;
    CoachModule m;
    setup(m, env);
    for (int i = 0; i < 130; ++i) send(m, env, R("x.skill" + std::to_string(i), 50.f, true));
    CHECK_EQ(m.skills().size(), CoachModule::kMaxSkills);
    for (int s = 0; s < 40; ++s) {
        send(m, env, R("ball.aerial", 50.f, true));
        event(m, env, GameEventKind::MapUnloaded);
    }
    CHECK_EQ(m.sessionHistory().size(), CoachModule::kMaxSessionHistory);
    Store st;
    m.save(st);
    CHECK(st.raw().size() < 4000);
    CoachModule l;
    l.load(st);
    CHECK_EQ(l.skills().size(), CoachModule::kMaxSkills);
    CHECK_EQ(l.sessionHistory().size(), CoachModule::kMaxSessionHistory);
}

// ---- Robustness ------------------------------------------------------------------------------------------
TEST(coach_disabled_ignores_results_and_draws_nothing) {
    Env env;
    env.settings.coach.enabled = false;
    CoachModule m;
    setup(m, env);
    const Actions a = send(m, env, R("ball.aerial", 50.f, true).record(3.f, false));
    CHECK(a.empty());
    CHECK_EQ(m.overview().totalAttempts, 0);
    DrawList dl;
    m.draw(env.view(), env.drawCtx(), dl);
    CHECK(dl.empty());
    Actions t;
    m.onTick(env.tick(), t);
    CHECK(t.empty());
}

TEST(coach_robust_to_bad_results) {
    Env env;
    CoachModule m;
    setup(m, env);
    AttemptResult r;
    r.score = std::nanf("");
    send(m, env, r);  // empty skill and module
    SkillView v;
    CHECK(m.skill("autre", v));
    CHECK_NEAR(v.averageAll, 0.f, 1e-6f);
    CHECK(m.card().verdict == Verdict::Fail);
    r.module = "kickoff";
    send(m, env, r);
    CHECK(m.skill("kickoff", v));
    // Invalid date: stats work, no day accounting, no streak.
    env.game.localDate = "";
    CoachModule n;
    setup(n, env);
    send(n, env, R("ball.aerial", 50.f, true));
    CHECK_EQ(n.overview().totalAttempts, 1);
    CHECK_EQ(n.overview().todayAttempts, 0);
    CHECK(n.recentDays(7).empty());
    CHECK(n.skill("ball.aerial", v));
    CHECK_EQ(v.daysSince, -1);
    // Weird ids are sanitized.
    send(n, env, R("mech speed=flip", 50.f, true));
    CHECK(n.skill("mech_speed_flip", v));
    // Result before any snapshot: the card still has a sane start time.
    env.hasSnapshot = false;
    send(n, env, R("ball.aerial", 50.f, true));
    CHECK(n.card().active);
}

TEST(coach_never_controls_the_world) {
    Env env;
    CoachModule m;
    FakeHost host;
    m.attach(&host);
    setup(m, env);
    std::vector<Actions> all;
    all.push_back(event(m, env, GameEventKind::ContextChanged));
    for (int i = 0; i < 30; ++i) {
        all.push_back(send(m, env,
                           R(i % 2 ? "mech.speedflip" : "ball.aerial", static_cast<float>(i * 3), i % 3 == 0)
                               .record(static_cast<float>(i), false)
                               .tip("c", "Conseil.")));
        Actions t;
        env.advance(kTickDt);
        m.onTick(env.tick(), t);
        all.push_back(t);
    }
    for (const char* c : {"new_session", "plan", "summary", "preview", "goal_add", "goal_clear", "reset_all", "x"})
        all.push_back(command(m, env, c));
    all.push_back(event(m, env, GameEventKind::ResetShot));
    all.push_back(event(m, env, GameEventKind::Paused));
    all.push_back(event(m, env, GameEventKind::MapUnloaded));
    m.onActivityStopped();
    for (const auto& a : all) {
        CHECK(!hasWorldControl(a));
        CHECK(a.results.empty());  // the coach never reports results itself
        for (const auto& c : a.chat) CHECK(noEmDash(c.text));
        for (const auto& t : a.toasts) CHECK(noEmDash(t.title) && noEmDash(t.text));
    }
    CHECK_EQ(host.requests, 0);
    CHECK_EQ(host.releases, 0);
}
