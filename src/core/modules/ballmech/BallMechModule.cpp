#include "core/modules/ballmech/BallMechModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/common/Text.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Pipeline.h"
#include "core/modules/ballmech/Stats.h"
#include "core/modules/ballmech/Tips.h"

namespace rlt {

using bm::Mech;

static_assert(bm::kMechCount == 11, "stairs_ in BallMechModule.h holds one Staircase per mechanic");

struct BallMechModule::Impl {
    bm::Pipeline pipeline;
    bm::Stats stats;
    bm::DrillSetup setup;
    bm::Pipeline::Output out;
};

namespace {

constexpr int64_t kResetDedupFrames = 36;  // 0.3 s
constexpr float kLandGrace = 0.6f;         // seconds a held grade may still resolve a landed drill attempt
constexpr float kStairStep = 0.04f;
constexpr float kStairStart = 0.15f;

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return {60, 230, 90, 255};
    case Verdict::Good: return {150, 230, 80, 255};
    case Verdict::Ok: return {255, 200, 60, 255};
    case Verdict::Fail: return {255, 80, 60, 255};
    default: return colors::White;
    }
}

std::string fmt0(float v) { return formatNumber(v, 0); }

std::string formatValue(float v, const std::string& unit, SpeedUnit speedUnit) {
    if (unit == "uu/s") return formatSpeed(v, speedUnit, 0);
    if (unit == "s") return formatNumber(v, 2) + " s";
    if (unit == "°") return fmt0(v) + "°";
    if (unit == "%") return fmt0(v) + " %";
    if (unit == "uu") return fmt0(v) + " uu";
    if (unit.empty()) return std::fabs(v - std::round(v)) < 1e-3f ? fmt0(v) : formatNumber(v, 2);
    return formatNumber(v, 1) + " " + unit;
}

std::string metricLine(const Metric& m, SpeedUnit unit) {
    std::string s = m.label + " : " + formatValue(m.value, m.unit, unit);
    if (m.hasIdeal && m.unit != "") s += " (idéal " + formatValue(m.ideal, m.unit, unit) + ")";
    return s;
}

// Splits a sentence into lines of at most `width` characters (bytes), on spaces.
std::vector<std::string> wrapText(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string cur;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (!cur.empty() && cur.size() + 1 + word.size() > width) {
            lines.push_back(cur);
            cur.clear();
        }
        if (!word.empty()) cur += (cur.empty() ? "" : " ") + word;
        start = end + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

const char* phaseLabel(int phase) {
    switch (phase) {
    case 1: return "Prépare-toi";
    case 2: return "À toi !";
    case 3: return "Résultat";
    default: return "";
    }
}

// Error code used when a drill attempt ends without a grade of the drill's mechanic.
const char* primaryFailCode(Mech m) {
    switch (m) {
    case Mech::FlipReset: return "FR_NO_RESET";
    case Mech::Ceiling: return "CEIL_MISSED";
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi: return "FL_NO_CONTACT";
    case Mech::DoubleTap: return "DT_NO_SECOND_TOUCH";
    case Mech::AirDribble: return "AD_SHORT";
    case Mech::AirRoll: return "AE_NO_AIR_ROLL";
    case Mech::Aerial: return "AE_OFF_TARGET";
    case Mech::Pinch: return "PI_NOT_PINCHED";
    case Mech::PowerShot: return "PS_WEAK";
    default: return "DRILL_TIMEOUT";
    }
}

bool flickVariantValid(const std::string& v) {
    return v.empty() || v == "front" || v == "45" || v == "90" || v == "backflip" || v == "side" || v == "reverse";
}

} // namespace

// ---- Construction ------------------------------------------------------------------------------------------
BallMechModule::BallMechModule() : impl_(std::make_unique<Impl>()) {
    for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, kStairStep, kStairStart);
    rng_.reseed(0x62616c6c6d656368ULL);
}

BallMechModule::~BallMechModule() = default;

// ---- Settings ----------------------------------------------------------------------------------------------
void BallMechModule::describeSettings(SettingsSchema& schema) {
    {
        auto w = schema.section(&Settings::ballmech, "ballmech", "Mécas balle");
        w.toggle("rlt_bm_enabled", &BallMechConfig::enabled, true, "Activer",
                 "Détecte et note automatiquement tes mécas avec balle (flip reset, ceiling shot, flicks, double tap, "
                 "air dribble, air roll shots, pinchs, frappes).");
        w.toggle("rlt_bm_in_matches", &BallMechConfig::inMatches, true, "Analyser aussi en match hors ligne",
                 "Note aussi tes mécas pendant les matchs contre les bots (jamais en ligne).");
        w.integer("rlt_bm_success_score", &BallMechConfig::successScore, 50, 0, 100, "Score minimum d'une réussite",
                  "Une tentative compte comme réussie si la méca est validée ET si son score atteint ce seuil.");
        w.choice("rlt_bm_chat", &BallMechConfig::chatMode, 1, {"Jamais", "Réussites seulement", "Toutes les tentatives"},
                 "Résumé dans le chat", "Écrit un court résumé de chaque méca détectée dans le chat du jeu.");
    }
    {
        auto w = schema.section(&Settings::ballmech, "ballmech", "Affichage");
        w.toggle("rlt_bm_hud", &BallMechConfig::hudEnabled, true, "Afficher le panneau",
                 "Panneau à gauche de l'écran : dernière méca, score, mesures clés et conseil.");
        w.number("rlt_bm_hud_x", &BallMechConfig::hudX, 0.01f, 0.f, 1.f, "Position X du panneau",
                 "Position horizontale (fraction de la largeur de l'écran).", 3);
        w.number("rlt_bm_hud_y", &BallMechConfig::hudY, 0.60f, 0.f, 1.f, "Position Y du panneau",
                 "Position verticale (fraction de la hauteur de l'écran).", 3);
        w.number("rlt_bm_hud_seconds", &BallMechConfig::hudSeconds, 8.f, 0.f, 60.f, "Durée d'affichage (s)",
                 "Temps pendant lequel le dernier résultat reste affiché. 0 = toujours.", 1);
        w.integer("rlt_bm_hud_metrics", &BallMechConfig::hudMetrics, 3, 0, 4, "Mesures affichées",
                  "Nombre de mesures clés affichées sous le score.");
        w.toggle("rlt_bm_hud_tip", &BallMechConfig::hudTip, true, "Afficher le conseil",
                 "Affiche le conseil le plus important de la dernière tentative.");
        w.toggle("rlt_bm_hud_resets", &BallMechConfig::hudResetCounter, true, "Compteur de flip resets",
                 "Affiche le nombre de flip resets obtenus pendant la session.");
        w.toggle("rlt_bm_draw_trajectory", &BallMechConfig::drawTrajectory, true, "Trajectoire du dernier tir",
                 "Dessine en 3D la trajectoire prévue du dernier tir noté (vert = cadré).");
        w.toggle("rlt_bm_draw_reset", &BallMechConfig::drawResetIndicator, true, "Cercle de reset disponible",
                 "Dessine un cercle vert autour de ta voiture tant que ton flip reset est disponible.");
        w.advanced();
        w.number("rlt_bm_drill_x", &BallMechConfig::drillHudX, 0.01f, 0.f, 1.f, "Position X du panneau d'exercice",
                 "Position horizontale du panneau des exercices (fraction de l'écran).", 3);
        w.number("rlt_bm_drill_y", &BallMechConfig::drillHudY, 0.12f, 0.f, 1.f, "Position Y du panneau d'exercice",
                 "Position verticale du panneau des exercices (fraction de l'écran).", 3);
    }
    {
        auto w = schema.section(&Settings::ballmech, "ballmech", "Détection");
        w.toggle("rlt_bm_det_flipreset", &BallMechConfig::detFlipReset, true, "Flip reset",
                 "Détecte les flip resets (4 roues sur la balle) et note leur utilisation.");
        w.toggle("rlt_bm_det_ceiling", &BallMechConfig::detCeiling, true, "Ceiling shot",
                 "Détecte les tirs après une chute du plafond.");
        w.toggle("rlt_bm_det_flick", &BallMechConfig::detFlick, true, "Flicks (musty, breezi...)",
                 "Détecte les flicks : avant, 45°, 90°, arrière, latéral, inversé, musty, breezi.");
        w.toggle("rlt_bm_det_doubletap", &BallMechConfig::detDoubleTap, true, "Double tap",
                 "Détecte les doubles touches après un rebond sur le mur du fond.");
        w.toggle("rlt_bm_det_airdribble", &BallMechConfig::detAirDribble, true, "Air dribble",
                 "Détecte les air dribbles depuis le sol ou le mur.");
        w.toggle("rlt_bm_det_aerial", &BallMechConfig::detAerial, true, "Tirs aériens et air roll shots",
                 "Note la précision de tes touches aériennes et ton contrôle de l'air roll.");
        w.toggle("rlt_bm_det_pinch", &BallMechConfig::detPinch, true, "Pinchs",
                 "Détecte les pinchs au sol, au mur (Kuxir), au plafond et dans les coins.");
        w.toggle("rlt_bm_det_powershot", &BallMechConfig::detPowerShot, true, "Frappes puissantes",
                 "Note les grosses frappes au sol (flip shots, frappes en glissade).");
        w.advanced();
        w.number("rlt_bm_pinch_speed", &BallMechConfig::pinchSpeed, 4000.f, 3000.f, 6000.f, "Vitesse d'un pinch (uu/s)",
                 "Vitesse de sortie minimale pour valider un pinch (4000 uu/s = 144 km/h).", 0);
        w.number("rlt_bm_aerial_min_speed", &BallMechConfig::aerialMinSpeed, 1200.f, 500.f, 3000.f,
                 "Vitesse min. d'un tir aérien (uu/s)", "Les touches aériennes plus douces sont des touches de "
                 "contrôle, pas des tirs.", 0);
        w.number("rlt_bm_power_min_speed", &BallMechConfig::powerMinSpeed, 1500.f, 800.f, 3000.f,
                 "Vitesse min. d'une frappe (uu/s)", "Les frappes au sol plus douces ne sont pas notées (sauf en flip).",
                 0);
    }
    {
        auto w = schema.section(&Settings::ballmech, "ballmech", "Exercices");
        w.number("rlt_bm_drill_ready", &BallMechConfig::drillReady, 0.8f, 0.f, 3.f, "Temps de préparation (s)",
                 "La voiture et la balle restent figées ce temps avant chaque essai.", 1);
        w.number("rlt_bm_drill_pause", &BallMechConfig::drillPause, 1.5f, 0.3f, 5.f, "Pause entre deux essais (s)",
                 "Temps pour lire le résultat avant l'essai suivant.", 1);
        w.number("rlt_bm_drill_timeout_scale", &BallMechConfig::drillTimeoutScale, 1.f, 0.5f, 3.f,
                 "Durée max d'un essai (x)", "Multiplie le temps accordé à chaque essai.", 2);
        w.toggle("rlt_bm_drill_adaptive", &BallMechConfig::drillAdaptive, true, "Difficulté adaptative",
                 "Le niveau monte quand tu réussis et baisse quand tu rates, pour rester dans la zone de progrès.");
        w.toggle("rlt_bm_drill_go", &BallMechConfig::drillGo, true, "Message GO! au départ",
                 "Affiche le GO! du jeu au début de chaque essai.");
        w.advanced();
        w.number("rlt_bm_drill_target", &BallMechConfig::drillTarget, 0.75f, 0.5f, 0.95f, "Taux de réussite visé",
                 "La difficulté adaptative se règle pour que tu réussisses environ cette part des essais.", 2);
    }
}

void BallMechModule::onSettingsChanged(const Settings& settings) {
    cfg_ = settings.ballmech;
    keyStyle_ = keyStyleFromIndex(settings.profile.buttonStyle);
    for (auto& s : stairs_) s.setTarget(cfg_.drillTarget);
    bm::DetectParams p;
    p.pinchSpeed = cfg_.pinchSpeed;
    p.aerialMinSpeed = cfg_.aerialMinSpeed;
    p.powerMinSpeed = cfg_.powerMinSpeed;
    impl_->pipeline.setParams(p);
}

// ---- Helpers -----------------------------------------------------------------------------------------------
bool BallMechModule::analysing(const GameContext& game) const {
    switch (game.mode) {
    case GameMode::Freeplay:
    case GameMode::CustomTraining:
    case GameMode::Workshop: return true;
    case GameMode::OfflineMatch: return cfg_.inMatches;
    default: return false;
    }
}

void BallMechModule::syncPipeline(const TickContext& ctx) {
    bm::Pipeline& pl = impl_->pipeline;
    bm::BuildOptions o;
    o.standardArena = !ctx.game.isCustomMap;
    const float dz = ctx.profile.dodgeDeadzone;
    o.dodgeDeadzone = (ctx.profile.valid && dz >= 0.05f && dz <= 1.f) ? dz : 0.5f;
    pl.setBuildOptions(o);

    std::array<bool, bm::kFamilyCount> en{};
    en[static_cast<size_t>(bm::Family::FlipReset)] = cfg_.detFlipReset;
    en[static_cast<size_t>(bm::Family::Ceiling)] = cfg_.detCeiling;
    en[static_cast<size_t>(bm::Family::DoubleTap)] = cfg_.detDoubleTap;
    en[static_cast<size_t>(bm::Family::Pinch)] = cfg_.detPinch;
    en[static_cast<size_t>(bm::Family::Flick)] = cfg_.detFlick;
    en[static_cast<size_t>(bm::Family::AirDribble)] = cfg_.detAirDribble;
    en[static_cast<size_t>(bm::Family::Aerial)] = cfg_.detAerial;
    en[static_cast<size_t>(bm::Family::PowerShot)] = cfg_.detPowerShot;
    if (ctx.game.isCustomMap) {
        // Unknown arena geometry: ceiling, backboard and surfaces cannot be trusted.
        en[static_cast<size_t>(bm::Family::Ceiling)] = false;
        en[static_cast<size_t>(bm::Family::DoubleTap)] = false;
        en[static_cast<size_t>(bm::Family::Pinch)] = false;
        switch (ctx.game.category) {
        case MapCategory::Rings:
        case MapCategory::Obstacle:
            en[static_cast<size_t>(bm::Family::Flick)] = false;
            en[static_cast<size_t>(bm::Family::Aerial)] = false;
            en[static_cast<size_t>(bm::Family::PowerShot)] = false;
            break;
        case MapCategory::AirDribble: en[static_cast<size_t>(bm::Family::PowerShot)] = false; break;
        default: break;
        }
    }
    if (drill_.phase != DrillPhase::Off) {
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        en[static_cast<size_t>(mi.family)] = true;
        if (static_cast<Mech>(drill_.mech) == Mech::PowerShot) en[static_cast<size_t>(bm::Family::Pinch)] = true;
    }
    for (int i = 0; i < bm::kFamilyCount; ++i) pl.setEnabled(static_cast<bm::Family>(i), en[static_cast<size_t>(i)]);
}

bool BallMechModule::countReset(int64_t frame) {
    for (int i = 0; i < ledgerCount_; ++i)
        if (std::llabs(ledger_[static_cast<size_t>(i)] - frame) <= kResetDedupFrames) return false;
    ledger_[static_cast<size_t>(ledgerHead_)] = frame;
    ledgerHead_ = (ledgerHead_ + 1) % static_cast<int>(ledger_.size());
    ledgerCount_ = std::min(ledgerCount_ + 1, static_cast<int>(ledger_.size()));
    ++impl_->stats.sessionResets;
    ++impl_->stats.records.flipResets;
    return true;
}

void BallMechModule::handleAcquisition(int64_t frame, int source, Actions& out) {
    if (source == 1) return;  // the game event was counted when it arrived
    if (!countReset(frame)) return;
    GameEvent ev;
    ev.kind = GameEventKind::FlipReset;
    ev.frame = frame;
    ev.detail = "detecteur";
    out.events.push_back(std::move(ev));
}

int BallMechModule::sessionResets() const { return impl_->stats.sessionResets; }
int BallMechModule::totalResets() const { return impl_->stats.records.flipResets; }

float BallMechModule::drillLevel(const std::string& mechId) const {
    Mech m;
    if (!bm::mechFromId(mechId, m)) return 0.f;
    return stairs_[static_cast<size_t>(m)].level();
}

float BallMechModule::drillTimeout() const {
    return bm::mechInfo(drill_.mech).drillTimeout * std::max(0.5f, cfg_.drillTimeoutScale);
}

// ---- Results -----------------------------------------------------------------------------------------------
AttemptResult BallMechModule::makeResult(const bm::MechGrade& g, int mech, bool success,
                                         const std::vector<std::string>& extra, const PlayerProfile& profile) const {
    const bm::MechInfo& mi = bm::mechInfo(mech);
    AttemptResult r;
    r.module = "ballmech";
    r.skill = mi.skill;
    r.title = bm::gradeTitle(g);
    r.score = std::round(clamp01(g.score) * 100.f);
    r.success = success;
    r.verdict = verdictFromScore(r.score);
    if (!success && r.verdict > Verdict::Ok) r.verdict = Verdict::Ok;  // a failed attempt is never "Bien"
    if (success && r.verdict < Verdict::Ok) r.verdict = Verdict::Ok;   // nor a success "Raté" (low threshold)
    r.metrics = g.metrics;
    r.time = g.tEnd;
    std::vector<std::string> errors = g.errors;
    for (const auto& e : extra)
        if (std::find(errors.begin(), errors.end(), e) == errors.end()) errors.push_back(e);
    r.tips = bm::buildTips(errors, profile, keyStyle_);
    switch (static_cast<Mech>(mech)) {
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi: r.valueForRecord = g.ballSpeed; break;
    case Mech::AirDribble: r.valueForRecord = g.value("duration"); break;
    case Mech::Pinch:
    case Mech::PowerShot: r.valueForRecord = g.value("exit_speed", g.ballSpeed); break;
    default: r.valueForRecord = r.score; break;
    }
    r.hasRecordValue = true;
    r.lowerIsBetter = false;
    return r;
}

void BallMechModule::publish(AttemptResult r, int mech, const bm::MechGrade* g, const TickContext& ctx, Actions& out) {
    bm::Stats& st = impl_->stats;
    st.at(static_cast<Mech>(mech)).record(r.score, r.success);
    bool record = false;
    if (g) {
        auto bump = [&record](float& slot, float v) {
            if (std::isfinite(v) && v > slot) {
                slot = v;
                record = true;
            }
        };
        const Mech m = static_cast<Mech>(mech);
        if ((m == Mech::Flick || m == Mech::Musty || m == Mech::Breezi) && g->hasShot && r.success)
            bump(st.records.flickSpeed, g->ballSpeed);
        if (m == Mech::AirDribble && r.success) bump(st.records.airDribbleSeconds, g->value("duration"));
        if (m == Mech::Pinch && r.success) bump(st.records.pinchSpeed, g->value("exit_speed"));
        if (m == Mech::PowerShot && r.success) bump(st.records.powerShotSpeed, g->value("exit_speed"));
        lastHasPath_ = g->hasShot && g->projection.valid && g->projection.pointCount >= 2;
        if (lastHasPath_) {
            lastPathCount_ = std::min(g->projection.pointCount, static_cast<int>(lastPath_.size()));
            for (int i = 0; i < lastPathCount_; ++i) lastPath_[static_cast<size_t>(i)] = g->projection.points[static_cast<size_t>(i)];
            lastPathOnTarget_ = g->projection.onTarget;
        }
    } else {
        lastHasPath_ = false;
    }
    if (record) out.requestSave = true;

    if (cfg_.chatMode == 2 || (cfg_.chatMode == 1 && r.success)) {
        const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
        std::string text = r.title + " : " + verdictLabel(r.verdict) + " (" + fmt0(r.score) + "/100)";
        if (g && g->hasShot && g->ballSpeed > 0.f) text += " | " + formatSpeed(g->ballSpeed, unit, 0);
        if (g && g->projection.valid && g->hasShot) text += g->projection.onTarget ? ", cadré" : ", non cadré";
        if (!r.success && !r.tips.empty()) text += " | " + r.tips.front().text;
        ChatMessage msg;
        msg.text = text;
        out.chat.push_back(std::move(msg));
    }
    last_ = r;
    hasLast_ = true;
    lastMech_ = mech;
    lastShownAt_ = ctx.snap.time;
    out.results.push_back(std::move(r));
}

void BallMechModule::handleGrade(bm::MechGrade& g, const TickContext& ctx, Actions& out) {
    if (drill_.phase == DrillPhase::Live && !drill_.resolved) {
        const Mech dm = static_cast<Mech>(drill_.mech);
        const float level = stairs_[static_cast<size_t>(drill_.mech)].level();
        const bool matches = bm::gradeMatchesDrill(dm, g) || (dm == Mech::PowerShot && g.family == bm::Family::Pinch);
        if (matches) {
            bm::DrillVerdict v = bm::judgeDrill(dm, g, level, cfg_.successScore, cfg_.pinchSpeed);
            if (dm == Mech::PowerShot && g.family == bm::Family::Pinch) {
                const bool onTarget = !g.projection.valid || g.projection.onTarget;
                v.success = g.value("exit_speed") >= 2000.f + 1200.f * level && onTarget &&
                            g.score * 100.f >= static_cast<float>(cfg_.successScore);
            }
            AttemptResult r = makeResult(g, drill_.mech, v.success, v.extraErrors, ctx.profile);
            drill_.lastVerdict = r.title + " : " + verdictLabel(r.verdict) + " " + fmt0(r.score) + "/100";
            publish(std::move(r), drill_.mech, &g, ctx, out);
            resolveDrill(v.success, ctx);
            return;
        }
        // Another mechanic resolved the attempt: it failed for the drill (main tip: wrong mechanic, then the drill's).
        const std::string what = bm::gradeTitle(g);
        failDrill("DRILL_WRONG_MECH", ctx, out);
        drill_.lastVerdict = std::string("Raté (") + what + ")";
        return;
    }
    if (g.drillOnly) return;
    const int mech = static_cast<int>(bm::mechOf(g));
    const bool success = g.success && g.score * 100.f + 1e-3f >= static_cast<float>(cfg_.successScore);
    std::vector<std::string> extra;
    if (g.success && !success) extra.push_back("DRILL_NEED_SCORE");
    publish(makeResult(g, mech, success, extra, ctx.profile), mech, &g, ctx, out);
}

// ---- Callbacks ---------------------------------------------------------------------------------------------
void BallMechModule::onTick(const TickContext& ctx, Actions& out) {
    if (ctx.hasSnapshot) lastTickTime_ = ctx.snap.time;
    if (!cfg_.enabled || !analysing(ctx.game)) {
        detecting_ = false;
        resetAvailable_ = false;
        if (drill_.phase != DrillPhase::Off) stopDrill(&out, true);
        return;
    }
    detecting_ = true;
    syncPipeline(ctx);
    bm::Pipeline::Output& po = impl_->out;
    po.clear();
    impl_->pipeline.process(ctx.history, ctx.hasSnapshot, po);
    for (const auto& a : po.acquisitions) handleAcquisition(a.frame, a.source, out);
    for (auto& g : po.released) handleGrade(g, ctx, out);
    resetAvailable_ = impl_->pipeline.resetAvailable();
    drillTick(ctx, out);
}

void BallMechModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    if (!cfg_.enabled) return;
    bm::TouchMark m;
    m.frame = touch.pre.frame;
    m.newContact = touch.newContact;
    m.hasHit = touch.hasHitInfo;
    m.hitLocation = touch.hitLocation;
    m.hitNormal = touch.hitNormal;
    impl_->pipeline.addTouch(m);
    if (drill_.phase == DrillPhase::Live) drill_.activity = true;
}

void BallMechModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    (void)out;
    switch (ev.kind) {
    case GameEventKind::ContextChanged:
    case GameEventKind::MapUnloaded:
        impl_->pipeline.restart();
        ledgerCount_ = 0;
        ledgerHead_ = 0;
        lastHasPath_ = false;
        resetAvailable_ = false;
        if (drill_.phase != DrillPhase::Off && ev.kind == GameEventKind::ContextChanged) drill_.phase = DrillPhase::Result;
        break;
    case GameEventKind::ResetShot:
        impl_->pipeline.resetDetectors();
        if (drill_.phase == DrillPhase::Live || drill_.phase == DrillPhase::Result) beginAttempt(ctx);
        break;
    case GameEventKind::Goal: impl_->pipeline.addGoal(); break;
    case GameEventKind::FlipReset: {
        // Counted only where our own detection runs: module and flip reset detection on (or the flip reset drill),
        // analysed game mode (never replays, online or offline matches when they are not analysed).
        const bool frDrill = drill_.phase != DrillPhase::Off && static_cast<Mech>(drill_.mech) == Mech::FlipReset;
        if (!cfg_.enabled || !(cfg_.detFlipReset || frDrill) || !analysing(ctx.game)) break;
        if (ev.detail == "sdk" || ev.detail.empty()) {
            const int64_t frame = ev.frame >= 0 ? ev.frame : ctx.snap.frame;
            impl_->pipeline.addSdkReset(frame);
            countReset(frame);
        }
        break;
    }
    default: break;
    }
}

void BallMechModule::onActivityStopped() {
    drill_.phase = DrillPhase::Off;
    drill_.burnStep = -1;
    impl_->pipeline.flick().setTarget("");
}

// ---- Drills ------------------------------------------------------------------------------------------------
bool BallMechModule::startDrill(int mech, const TickContext& ctx, Actions& out) {
    const bm::MechInfo& mi = bm::mechInfo(mech);
    ToastMessage t;
    t.title = std::string("Exercice : ") + mi.title;
    if (!cfg_.enabled) {
        t.text = "Active d'abord le module Mécas balle.";
        t.kind = ToastKind::Warning;
        out.toasts.push_back(t);
        return false;
    }
    if (ctx.game.isCustomMap) {
        t.text = "Les exercices Mécas balle demandent une arène standard : lance le freeplay.";
        t.kind = ToastKind::Warning;
        out.toasts.push_back(t);
        return false;
    }
    if (!ctx.worldControlAllowed || !requestActivity(std::string("Mécas balle : ") + mi.title)) {
        t.text = "Exercice indisponible ici : lance le freeplay (ou autorise les exercices dans les réglages généraux).";
        t.kind = ToastKind::Warning;
        out.toasts.push_back(t);
        return false;
    }
    const int attempts = drill_.mech == mech ? drill_.attempts : 0;
    const int successes = drill_.mech == mech ? drill_.successes : 0;
    drill_ = Drill{};
    drill_.mech = mech;
    drill_.attempts = attempts;
    drill_.successes = successes;
    beginAttempt(ctx);
    t.text = bm::fillButtons(mi.drillHint, ctx.profile, keyStyle_);
    t.kind = ToastKind::Info;
    out.toasts.push_back(t);
    return true;
}

void BallMechModule::stopDrill(Actions* out, bool release) {
    if (drill_.phase == DrillPhase::Off) return;
    drill_.phase = DrillPhase::Off;
    drill_.burnStep = -1;
    impl_->pipeline.flick().setTarget("");
    if (release) releaseActivity();
    if (out) {
        ToastMessage t;
        t.title = "Mécas balle";
        t.text = "Exercice arrêté.";
        out->toasts.push_back(t);
    }
}

void BallMechModule::beginAttempt(const TickContext& ctx) {
    const size_t mi = static_cast<size_t>(drill_.mech);
    const float level = stairs_[mi].level();
    rng_.reseed(mix64(static_cast<uint64_t>(ctx.snap.frame) ^ (++rngCounter_ * 0x9e3779b97f4a7c15ULL)));
    const int team = ctx.snap.car.valid ? ctx.snap.car.team : 0;
    impl_->setup = bm::makeDrillSetup(static_cast<Mech>(drill_.mech), level, team, ctx.snap.car,
                                      ctx.snap.ball.radius > 10.f ? ctx.snap.ball.radius : 91.25f, rng_, flickVariant_);
    impl_->pipeline.flick().setTarget(impl_->setup.flickTarget);
    drill_.phase = DrillPhase::Ready;
    drill_.phaseStart = ctx.snap.time;
    drill_.burnStep = -1;
    drill_.airborneSeen = false;
    drill_.activity = false;
    drill_.failAt = -1.f;
    drill_.failCode.clear();
    drill_.resolved = false;
}

void BallMechModule::applySetup(const TickContext& ctx, Actions& out, bool launch) {
    (void)ctx;
    const bm::DrillSetup& d = impl_->setup;
    CarPlacement c = d.car;
    BallPlacement b = d.ball;
    if (!launch) {
        c.vel = {};
        c.angVel = {};
        b.vel = {};
        b.angVel = {};
        out.freezeCar = true;
    }
    out.setCar = c;
    out.setBall = b;
}

void BallMechModule::resolveDrill(bool success, const TickContext& ctx) {
    if (cfg_.drillAdaptive) stairs_[static_cast<size_t>(drill_.mech)].record(success);
    ++drill_.attempts;
    if (success) ++drill_.successes;
    drill_.lastSuccess = success;
    drill_.resolved = true;
    drill_.idleTimeouts = 0;
    drill_.phase = DrillPhase::Result;
    drill_.phaseStart = ctx.snap.time;
}

void BallMechModule::failDrill(const char* code, const TickContext& ctx, Actions& out) {
    const Mech dm = static_cast<Mech>(drill_.mech);
    const bm::MechInfo& mi = bm::mechInfo(dm);
    AttemptResult r;
    r.module = "ballmech";
    r.skill = mi.skill;
    r.title = std::string(mi.title) + " : raté";
    r.score = 0.f;
    r.success = false;
    r.verdict = Verdict::Fail;
    r.time = ctx.snap.time;
    r.metrics.push_back(bm::makeMetric("attempt_time", "Durée de l'essai", ctx.snap.time - drill_.phaseStart, "s"));
    std::vector<std::string> codes{code};
    if (std::string(code) != primaryFailCode(dm)) codes.push_back(primaryFailCode(dm));
    r.tips = bm::buildTips(codes, ctx.profile, keyStyle_);
    r.valueForRecord = 0.f;
    r.hasRecordValue = true;
    drill_.lastVerdict = r.title;
    publish(std::move(r), drill_.mech, nullptr, ctx, out);
    resolveDrill(false, ctx);
}

void BallMechModule::drillTick(const TickContext& ctx, Actions& out) {
    if (drill_.phase == DrillPhase::Off) return;
    if (!ctx.ownsActivity || !ctx.worldControlAllowed) {
        stopDrill(nullptr, ctx.ownsActivity);
        return;
    }
    const Snapshot& s = ctx.snap;
    if (!ctx.hasSnapshot || !s.car.valid || !s.ball.valid || s.paused || ctx.game.paused) return;
    const float now = s.time;
    if (now < drill_.phaseStart - 0.5f) drill_.phaseStart = now;  // game time went backwards (map reload)
    switch (drill_.phase) {
    case DrillPhase::Ready:
        if (now - drill_.phaseStart >= cfg_.drillReady) {
            applySetup(ctx, out, true);
            if (cfg_.drillGo && cfg_.drillReady >= 0.3f) out.goMessage = true;
            drill_.launchFrame = s.frame;
            impl_->pipeline.ignoreUntil(s.frame + 2);
            impl_->pipeline.resetDetectors();
            impl_->pipeline.clearHeld();
            if (impl_->setup.doubleTapRead) impl_->pipeline.armDoubleTapRead();
            drill_.burnStep = impl_->setup.burnFlip ? 0 : -1;
            drill_.phase = DrillPhase::Live;
            drill_.phaseStart = now;
        } else {
            applySetup(ctx, out, false);
        }
        break;
    case DrillPhase::Live: {
        if (drill_.burnStep >= 0) {
            // Scripted double jump right after the launch: the car starts without its flip (flip reset drill).
            Inputs in;
            in.jump = drill_.burnStep == 1 || drill_.burnStep == 2;
            out.overrideInputs = in;
            if (++drill_.burnStep > 3) drill_.burnStep = -1;
        } else if (s.in.jump || s.in.boost || std::fabs(s.in.throttle) > 0.1f || bm::stickMagnitude(s.in) > 0.3f) {
            drill_.activity = true;
        }
        const int world = bm::worldWheelContacts(s);
        if (world == 0 && s.frame > drill_.launchFrame + 3) drill_.airborneSeen = true;
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        if (!drill_.resolved && drill_.failAt < 0.f) {
            const Mech dm = static_cast<Mech>(drill_.mech);
            const bool flickFamily = dm == Mech::Flick || dm == Mech::Musty || dm == Mech::Breezi;
            if (mi.airborneDrill && drill_.airborneSeen && world >= 3 && s.car.pos.z < 300.f) {
                drill_.failAt = now + kLandGrace;
                drill_.failCode = "DRILL_LANDED";
            } else if (flickFamily && now - drill_.phaseStart > 0.3f && s.ball.pos.z < s.ball.radius + 40.f &&
                       distance(s.car.pos, s.ball.pos) > 300.f) {
                drill_.failAt = now + kLandGrace;
                drill_.failCode = "DRILL_BALL_LOST";
            } else if (now - drill_.phaseStart > drillTimeout()) {
                if (drill_.activity) {
                    failDrill("DRILL_TIMEOUT", ctx, out);
                } else if (++drill_.idleTimeouts >= 3) {
                    stopDrill(&out, true);
                    ToastMessage t;
                    t.title = "Mécas balle";
                    t.text = "Exercice mis en pause : tu n'as pas joué depuis 3 essais.";
                    out.toasts.push_back(t);
                } else {
                    beginAttempt(ctx);
                }
                break;
            }
        }
        if (!drill_.resolved && drill_.failAt >= 0.f) {
            // A pending fail is cancelled when the situation recovers during the grace delay: the car is airborne
            // again (hop, pop and follow; not a landing bounce) or the ball came back within reach.
            const bool recovered = (drill_.failCode == "DRILL_LANDED" && world == 0 && s.car.pos.z > 50.f) ||
                                   (drill_.failCode == "DRILL_BALL_LOST" && distance(s.car.pos, s.ball.pos) <= 300.f);
            if (recovered) {
                drill_.failAt = -1.f;
                drill_.failCode.clear();
            } else if (now >= drill_.failAt) {
                failDrill(drill_.failCode.c_str(), ctx, out);
            }
        }
        break;
    }
    case DrillPhase::Result:
        if (now - drill_.phaseStart >= cfg_.drillPause) beginAttempt(ctx);
        break;
    default: break;
    }
}

// ---- Commands ----------------------------------------------------------------------------------------------
void BallMechModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    auto toast = [&out](const std::string& text, ToastKind kind = ToastKind::Info) {
        ToastMessage t;
        t.title = "Mécas balle";
        t.text = text;
        t.kind = kind;
        out.toasts.push_back(std::move(t));
    };
    const std::string& n = cmd.name;
    if (n == "drill" || n == "start") {
        Mech m = Mech::FlipReset;
        if (cmd.args.empty() || !bm::mechFromId(cmd.args[0], m)) {
            toast("Méca inconnue. Choix : flipreset, ceiling, flick, musty, breezi, doubletap, airdribble, airroll, "
                  "aerial, pinch, powershot.",
                  ToastKind::Warning);
            return;
        }
        if (m == Mech::Flick && cmd.args.size() >= 2) {
            const std::string v = cmd.args[1] == "any" || cmd.args[1] == "tous" ? "" : cmd.args[1];
            if (flickVariantValid(v)) flickVariant_ = v;
        }
        startDrill(static_cast<int>(m), ctx, out);
    } else if (n == "stop") {
        if (drill_.phase == DrillPhase::Off) return;
        stopDrill(&out, true);
    } else if (n == "next" || n == "restart") {
        if (drill_.phase != DrillPhase::Off) beginAttempt(ctx);
    } else if (n == "level" && cmd.args.size() >= 2) {
        Mech m;
        char* end = nullptr;
        const float v = std::strtof(cmd.args[1].c_str(), &end);
        if (!bm::mechFromId(cmd.args[0], m) || end == cmd.args[1].c_str() || !std::isfinite(v)) {
            toast("Usage : level <méca> <0-100>", ToastKind::Warning);
            return;
        }
        // Percent (0-100); a decimal value up to 1 ("0.25") is read as a fraction.
        const bool fraction = cmd.args[1].find('.') != std::string::npos && v <= 1.f;
        stairs_[static_cast<size_t>(m)].setLevel(fraction ? v : v / 100.f);
        out.requestSave = true;
    } else if (n == "variant") {
        const std::string v = cmd.args.empty() || cmd.args[0] == "any" || cmd.args[0] == "tous" ? "" : cmd.args[0];
        if (!flickVariantValid(v)) {
            toast("Variante inconnue : front, 45, 90, backflip, side, reverse ou any.", ToastKind::Warning);
            return;
        }
        flickVariant_ = v;
        if (drill_.phase != DrillPhase::Off && static_cast<Mech>(drill_.mech) == Mech::Flick)
            impl_->pipeline.flick().setTarget(v);
        out.requestSave = true;
    } else if (n == "reset_stats") {
        impl_->stats.clear();
        for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, kStairStep, kStairStart);
        drill_.attempts = drill_.successes = 0;
        hasLast_ = false;
        lastHasPath_ = false;
        out.requestSave = true;
        toast("Statistiques des mécas balle remises à zéro.");
    } else if (n == "reset_session") {
        impl_->stats.sessionResets = 0;
        for (int i = 0; i < bm::kMechCount; ++i) {
            auto& m = impl_->stats.at(static_cast<Mech>(i));
            m.sessionAttempts = m.sessionSuccesses = 0;
        }
    } else {
        toast("Commande inconnue : " + n, ToastKind::Warning);
    }
}

// ---- Drawing -----------------------------------------------------------------------------------------------
void BallMechModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled) return;
    const GameMode mode = ctx.game.mode;
    if (mode == GameMode::Replay || mode == GameMode::Menu || mode == GameMode::Online) return;
    const float W = view.screenW > 0.f ? view.screenW : 1920.f;
    const float H = view.screenH > 0.f ? view.screenH : 1080.f;
    const float scale = ctx.hudScale > 0.f ? ctx.hudScale : 1.f;
    const float now = ctx.hasSnapshot ? ctx.snap.time : lastTickTime_;
    const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
    const Occluder occ{view.ballValid, view.ballVisualPos, view.ballRadius};

    // ---- Main panel
    if (cfg_.hudEnabled) {
        DrawPanel p;
        p.pos = {clamp01(cfg_.hudX) * W, clamp01(cfg_.hudY) * H};
        p.title = "Mécas balle";
        p.scale = scale;
        p.minWidth = 250.f * scale;
        const bool showResult = hasLast_ && (cfg_.hudSeconds <= 0.f || (now - lastShownAt_ >= 0.f && now - lastShownAt_ <= cfg_.hudSeconds));
        if (showResult) {
            const Color vc = verdictColor(last_.verdict);
            PanelLine head;
            head.text = last_.title;
            head.color = vc;
            head.scale = 1.1f;
            p.lines.push_back(head);
            PanelLine score;
            score.text = std::string(verdictLabel(last_.verdict)) + " : " + fmt0(last_.score) + "/100" +
                         (last_.success ? "  (réussi)" : "");
            score.color = vc;
            score.bar = clamp01(last_.score / 100.f);
            score.barColor = vc;
            p.lines.push_back(score);
            int shown = 0;
            const auto& keys = bm::mechInfo(lastMech_).hudKeys;
            for (const char* k : keys) {
                if (shown >= cfg_.hudMetrics || !k || !*k) continue;
                for (const auto& m : last_.metrics) {
                    if (m.key != k) continue;
                    PanelLine l;
                    l.text = metricLine(m, unit);
                    l.color = {220, 220, 220, 255};
                    l.scale = 0.9f;
                    p.lines.push_back(l);
                    ++shown;
                    break;
                }
            }
            for (const auto& m : last_.metrics) {
                if (m.key != "on_target") continue;
                PanelLine l;
                l.text = m.value >= 0.5f ? "Tir cadré" : "Tir non cadré";
                l.color = m.value >= 0.5f ? Color{60, 230, 90, 255} : Color{255, 140, 0, 255};
                l.scale = 0.9f;
                p.lines.push_back(l);
            }
            if (cfg_.hudTip) {
                if (!last_.tips.empty()) {
                    int n = 0;
                    for (const auto& line : wrapText(last_.tips.front().text, 46)) {
                        if (++n > 3) break;
                        PanelLine l;
                        l.text = line;
                        l.color = {255, 220, 120, 255};
                        l.scale = 0.9f;
                        p.lines.push_back(l);
                    }
                } else if (last_.success) {
                    PanelLine l;
                    l.text = "Bien joué, continue comme ça !";
                    l.color = {60, 230, 90, 255};
                    l.scale = 0.9f;
                    p.lines.push_back(l);
                }
            }
        }
        if (resetAvailable_ && detecting_) {
            PanelLine l;
            l.text = "Flip reset disponible !";
            l.color = {60, 230, 90, 255};
            p.lines.push_back(l);
        }
        if (cfg_.hudResetCounter && impl_->stats.sessionResets > 0) {
            PanelLine l;
            l.text = "Flip resets (session) : " + std::to_string(impl_->stats.sessionResets);
            l.color = {180, 220, 255, 255};
            l.scale = 0.9f;
            p.lines.push_back(l);
        }
        if (!p.lines.empty()) out.panel(std::move(p));
    }

    // ---- Drill panel (top-left exercise zone)
    if (drill_.phase != DrillPhase::Off) {
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        DrawPanel d;
        d.pos = {clamp01(cfg_.drillHudX) * W, clamp01(cfg_.drillHudY) * H};
        d.title = std::string("Exercice : ") + mi.title;
        d.scale = scale;
        d.minWidth = 260.f * scale;
        PanelLine info;
        info.text = impl_->setup.label + " | Niveau " + fmt0(stairs_[static_cast<size_t>(drill_.mech)].level() * 100.f) + " %";
        info.color = {200, 200, 200, 255};
        info.scale = 0.9f;
        d.lines.push_back(info);
        PanelLine ph;
        if (drill_.phase == DrillPhase::Ready) {
            ph.text = std::string(phaseLabel(1)) + "...";
            ph.color = {255, 220, 120, 255};
            ph.bar = cfg_.drillReady > 0.f ? clamp01((now - drill_.phaseStart) / cfg_.drillReady) : 1.f;
            ph.barColor = {255, 220, 120, 255};
        } else if (drill_.phase == DrillPhase::Live) {
            const float left = std::max(0.f, drillTimeout() - (now - drill_.phaseStart));
            ph.text = std::string(phaseLabel(2)) + " " + formatNumber(left, 1) + " s";
            ph.color = colors::White;
            ph.bar = clamp01(left / std::max(0.1f, drillTimeout()));
            ph.barColor = {90, 170, 255, 255};
        } else {
            ph.text = drill_.lastVerdict.empty() ? phaseLabel(3) : drill_.lastVerdict;
            ph.color = drill_.lastSuccess ? Color{60, 230, 90, 255} : Color{255, 100, 70, 255};
        }
        d.lines.push_back(ph);
        int n = 0;
        for (const auto& line : wrapText(bm::fillButtons(mi.drillHint, ctx.profile, keyStyle_), 48)) {
            if (++n > 3) break;
            PanelLine l;
            l.text = line;
            l.color = {220, 220, 220, 255};
            l.scale = 0.9f;
            d.lines.push_back(l);
        }
        PanelLine count;
        const float rate = drill_.attempts > 0 ? static_cast<float>(drill_.successes) / static_cast<float>(drill_.attempts) : 0.f;
        count.text = "Essais : " + std::to_string(drill_.attempts) + " | Réussis : " + std::to_string(drill_.successes) +
                     (drill_.attempts > 0 ? " (" + formatPercent(rate) + ")" : "");
        count.color = {180, 220, 255, 255};
        count.scale = 0.9f;
        d.lines.push_back(count);
        out.panel(std::move(d));
    }

    // ---- 3D: flip reset ring and last shot path (bounded: <= 24 + 24 segments)
    if (cfg_.drawResetIndicator && resetAvailable_ && detecting_ && view.carValid)
        out.circle3D(view.carVisualPos, view.carVisualBasis.up, 110.f, {60, 230, 90, 200}, 3.f, 24, occ);
    if (cfg_.drawTrajectory && lastHasPath_ && lastPathCount_ >= 2 && now - lastShownAt_ >= 0.f && now - lastShownAt_ <= 2.5f) {
        const Color c = lastPathOnTarget_ ? Color{60, 230, 90, 220} : Color{255, 120, 60, 220};
        for (int i = 0; i + 1 < lastPathCount_; ++i)
            out.line3D(lastPath_[static_cast<size_t>(i)], lastPath_[static_cast<size_t>(i + 1)], c, 2.f, occ);
        out.marker3D(lastPath_[static_cast<size_t>(lastPathCount_ - 1)], 40.f, c, 2.f);
    }
}

// ---- UI ----------------------------------------------------------------------------------------------------
BallMechModule::UiState BallMechModule::uiState() const {
    UiState u;
    const bm::Stats& st = impl_->stats;
    u.enabled = cfg_.enabled;
    u.detecting = detecting_;
    u.sessionResets = st.sessionResets;
    u.totalResets = st.records.flipResets;
    u.bestAirDribble = st.records.airDribbleSeconds;
    u.hardestFlick = st.records.flickSpeed;
    u.bestPinch = st.records.pinchSpeed;
    u.bestPowerShot = st.records.powerShotSpeed;
    u.resetAvailable = resetAvailable_;
    u.hasLast = hasLast_;
    if (hasLast_) u.last = last_;
    u.flickVariant = flickVariant_;
    u.keyStyle = keyStyle_;
    const std::array<bool, bm::kFamilyCount> det = {cfg_.detFlipReset, cfg_.detCeiling, cfg_.detDoubleTap, cfg_.detPinch,
                                                    cfg_.detFlick,     cfg_.detAirDribble, cfg_.detAerial, cfg_.detPowerShot};
    u.mechs.reserve(bm::kMechCount);
    for (int i = 0; i < bm::kMechCount; ++i) {
        const bm::MechInfo& mi = bm::mechInfo(i);
        const bm::MechStats& ms = st.at(static_cast<Mech>(i));
        MechView v;
        v.id = mi.id;
        v.title = mi.title;
        v.what = mi.what;
        v.key = mi.key;
        v.drillHint = mi.drillHint;
        v.attempts = ms.attempts;
        v.successes = ms.successes;
        v.sessionAttempts = ms.sessionAttempts;
        v.sessionSuccesses = ms.sessionSuccesses;
        v.average = ms.average();
        v.best = ms.best;
        v.recent = ms.recent;
        v.drillLevel = stairs_[static_cast<size_t>(i)].level();
        v.drillAttempts = stairs_[static_cast<size_t>(i)].attempts();
        v.detectionEnabled = det[static_cast<size_t>(mi.family)];
        u.mechs.push_back(std::move(v));
    }
    DrillView& d = u.drill;
    d.active = drill_.phase != DrillPhase::Off;
    if (d.active) {
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        d.mechId = mi.id;
        d.title = mi.title;
        d.phase = phaseLabel(static_cast<int>(drill_.phase));
        d.label = impl_->setup.label;
        d.hint = mi.drillHint;
        d.level = stairs_[static_cast<size_t>(drill_.mech)].level();
        d.attempts = drill_.attempts;
        d.successes = drill_.successes;
        d.timeLeft = drill_.phase == DrillPhase::Live ? std::max(0.f, drillTimeout() - (lastTickTime_ - drill_.phaseStart)) : 0.f;
        d.lastVerdict = drill_.lastVerdict;
        d.lastSuccess = drill_.lastSuccess;
    }
    return u;
}

// ---- Persistence -------------------------------------------------------------------------------------------
void BallMechModule::save(Store& store) const {
    impl_->stats.save(store);
    for (int i = 0; i < bm::kMechCount; ++i)
        stairs_[static_cast<size_t>(i)].save(store, std::string("ballmech.drill.") + bm::mechInfo(i).id);
    store.setString("ballmech.flick_variant", flickVariant_);
}

void BallMechModule::load(const Store& store) {
    impl_->stats.load(store);
    for (int i = 0; i < bm::kMechCount; ++i) {
        Staircase& s = stairs_[static_cast<size_t>(i)];
        s = Staircase(cfg_.drillTarget, kStairStep, kStairStart);
        s.load(store, std::string("ballmech.drill.") + bm::mechInfo(i).id);
    }
    const std::string v = store.getString("ballmech.flick_variant", "");
    flickVariant_ = flickVariantValid(v) ? v : "";
}

} // namespace rlt
