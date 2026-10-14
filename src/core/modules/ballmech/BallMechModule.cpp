#include "core/modules/ballmech/BallMechModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/common/HangingBall.h"
#include "core/common/Hold.h"
#include "core/common/Pickup.h"
#include "core/common/Steps.h"
#include "core/common/Text.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Parcours.h"
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
    ReadyPhase ready;            // get-ready hold before each attempt (fix 1)
    HoldPose hold;               // what the hold re-sends every tick
    StepTracker steps;           // full sequence steps (fix 2)
    bm::ParcoursState parcours;  // what the step checks read (refreshed every Live tick, both starts)
    HangingBall hang;            // ball waiting in the air (fix 3)
    pickup::Progress pickup;     // the shared "Ramassage" start has its own staircase (fix 4)
    bool swallowFamily = false;  // a success found by the step checks: the late grade of the same attempt is dropped
    bool readyNative = false;    // the get-ready phase sends the native 3, 2, 1 and GO itself
    float readySeconds = 0.f;    // its countdown
};

namespace {

constexpr int64_t kResetDedupFrames = 36;  // 0.3 s
constexpr float kLandGrace = 0.6f;         // seconds a held grade may still resolve a landed drill attempt
constexpr float kStairStep = 0.04f;
constexpr float kStairStart = 0.f;         // a new player starts every drill at level 1 (the easiest)
constexpr float kAwaitGrade = 8.f;         // full sequence done on its checks: seconds to wait for the detector's grade
constexpr Color kStepAccent{255, 199, 64, 255};

// Breezi tornado (module check, no grade exists for it): roll >= 230 deg with the air roll held >= 30 % of the air
// time and the ball within reach >= 70 % of it (FlickDetector's breezi gate plus the ball kept).
bool tornadoOk(const bm::ParcoursState& st) {
    if (st.airTicks <= 0) return false;
    const float airRoll = static_cast<float>(st.airRollTicks) / static_cast<float>(st.airTicks);
    const float kept = static_cast<float>(st.keptTicks) / static_cast<float>(st.airTicks);
    return std::fabs(st.rollAccum) * kRadToDeg >= 230.f && airRoll >= 0.3f && kept >= 0.7f;
}

bool isCriteriaCode(const std::string& c) {
    return c == "DRILL_NEED_POWER" || c == "DRILL_NEED_TARGET" || c == "DRILL_NEED_DURATION" || c == "DRILL_NEED_SCORE";
}

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

constexpr Color kGreen{60, 230, 90, 255};
constexpr Color kTipColor{255, 220, 120, 255};
constexpr const char* kWellDone = "Bien joué, continue comme ça !";

PanelLine hudLine(std::string text, Color color, bool detail = false, float scale = 1.f) {
    PanelLine l;
    l.text = std::move(text);
    l.color = color;
    l.detail = detail;
    l.scale = scale;
    return l;
}

// Detail lines: the key measures of a result (count from rlt_bm_hud_metrics), then on target / off target.
void addMetricLines(DrawPanel& p, const AttemptResult& r, int mech, int maxMetrics, SpeedUnit unit) {
    int shown = 0;
    for (const char* k : bm::mechInfo(mech).hudKeys) {
        if (shown >= maxMetrics || !k || !*k) continue;
        for (const auto& m : r.metrics) {
            if (m.key != k) continue;
            p.lines.push_back(hudLine(metricLine(m, unit), {220, 220, 220, 255}, true, 0.9f));
            ++shown;
            break;
        }
    }
    for (const auto& m : r.metrics)
        if (m.key == "on_target")
            p.lines.push_back(hudLine(m.value >= 0.5f ? "Tir cadré" : "Tir non cadré",
                                      m.value >= 0.5f ? kGreen : Color{255, 140, 0, 255}, true, 0.9f));
}

// A panel is drawn when it has a line the player will see: in compact mode the detail lines are dropped.
bool panelHasContent(const DrawPanel& p, bool compact) {
    for (const auto& l : p.lines)
        if (!compact || !l.detail) return true;
    return false;
}

std::string levelText(float level) { return "niveau " + std::to_string(bm::playerLevel(level)) + "/10"; }

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

// Short start where a throttle held through the countdown spoils the attempt at GO: ball balanced on a stopped car
// (it slides off the roof) or car on the ceiling (it keeps to the ceiling instead of dropping).
bool throttleSpoilsStart(Mech m, const bm::DrillSetup& d) {
    if (d.mode == bm::DrillMode::Full) return false;
    const bool roofStart = length(d.car.vel) < 1.f &&
                           (m == Mech::Flick || m == Mech::Musty || m == Mech::Breezi || m == Mech::AirDribble);
    return roofStart || d.armCeiling;
}

} // namespace

// ---- Construction ------------------------------------------------------------------------------------------
BallMechModule::BallMechModule() : impl_(std::make_unique<Impl>()) {
    for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, kStairStep, kStairStart);
    for (int i = 0; i < bm::kMechCount; ++i) modes_[static_cast<size_t>(i)] = static_cast<int>(bm::defaultMode(static_cast<Mech>(i)));
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
                  "Une tentative compte comme réussie si la méca est validée ET si son score atteint ce seuil. En "
                  "exercice, le seuil est 25 points plus bas au niveau 1 et 20 points plus haut au niveau 10.");
        w.choice("rlt_bm_chat", &BallMechConfig::chatMode, 1, {"Jamais", "Réussites seulement", "Toutes les tentatives"},
                 "Résumé dans le chat", "Écrit un court résumé de chaque méca détectée dans le chat du jeu.");
    }
    {
        auto w = schema.section(&Settings::ballmech, "ballmech", "Affichage");
        w.toggle("rlt_bm_hud", &BallMechConfig::hudEnabled, true, "Afficher le panneau",
                 "Panneau à gauche de l'écran : dernière méca, score et conseil.");
        w.number("rlt_bm_hud_x", &BallMechConfig::hudX, 0.01f, 0.f, 1.f, "Position X du panneau",
                 "Position horizontale (fraction de la largeur de l'écran).", 3);
        w.number("rlt_bm_hud_y", &BallMechConfig::hudY, 0.60f, 0.f, 1.f, "Position Y du panneau",
                 "Position verticale (fraction de la hauteur de l'écran).", 3);
        w.number("rlt_bm_hud_scale", &BallMechConfig::hudScale, 1.f, 0.5f, 2.f, "Taille du panneau",
                 "Multiplie la taille du panneau et de celui des exercices, en plus de la taille générale du HUD.", 2);
        w.number("rlt_bm_hud_seconds", &BallMechConfig::hudSeconds, 8.f, 0.f, 60.f, "Durée d'affichage (s)",
                 "Temps pendant lequel le dernier résultat reste affiché. 0 = toujours.", 1);
        w.integer("rlt_bm_hud_metrics", &BallMechConfig::hudMetrics, 3, 0, 4, "Mesures affichées",
                  "Nombre de mesures détaillées sous le score, visibles quand le HUD compact est désactivé.");
        w.toggle("rlt_bm_hud_tip", &BallMechConfig::hudTip, true, "Afficher le conseil",
                 "Affiche le conseil le plus important de la dernière tentative.");
        w.toggle("rlt_bm_hud_resets", &BallMechConfig::hudResetCounter, true, "Compteur de flip resets",
                 "Affiche le nombre de flip resets obtenus pendant la session (HUD compact désactivé).");
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
                 "Durée max d'un essai (x)",
                 "Multiplie le temps accordé à chaque essai. Les premiers niveaux donnent déjà plus de temps.", 2);
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
    // Air roll drill niveau 1 asks 1000 uu/s: the aerial detector must grade softer touches during it.
    bm::DetectParams p;
    p.pinchSpeed = cfg_.pinchSpeed;
    p.aerialMinSpeed = cfg_.aerialMinSpeed;
    p.powerMinSpeed = cfg_.powerMinSpeed;
    if (drill_.phase != DrillPhase::Off && static_cast<Mech>(drill_.mech) == Mech::AirRoll)
        p.aerialMinSpeed = std::min(p.aerialMinSpeed, bm::airRollSpeedNeeded(stairs_[static_cast<size_t>(drill_.mech)].level()));
    pl.setParams(p);
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

int BallMechModule::drillMode(const std::string& mechId) const {
    Mech m;
    if (!bm::mechFromId(mechId, m)) return 0;
    return modes_[static_cast<size_t>(m)];
}

float BallMechModule::pickupLevel() const { return impl_->pickup.staircase().level(); }

float BallMechModule::drillTimeout() const {
    const float level = stairs_[static_cast<size_t>(drill_.mech)].level();
    return bm::mechInfo(drill_.mech).drillTimeout * std::max(0.5f, cfg_.drillTimeoutScale) * bm::drillTimeFactor(level);
}

const Tip* BallMechModule::drillTip() const {
    if (!drill_.hasResult) return nullptr;
    const auto& tips = drill_.result.tips;
    // "Not the requested mechanic" is already in the verdict line: the drill's own advice says what to change.
    for (const auto& t : tips)
        if (t.code != "bm.drill_wrong_mech" || tips.size() == 1) return &t;
    return nullptr;
}

std::string BallMechModule::drillActionNow() const {
    const Mech m = static_cast<Mech>(drill_.mech);
    const float level = stairs_[static_cast<size_t>(drill_.mech)].level();
    const int n = bm::playerLevel(level);
    if (m == Mech::FlipReset && drill_.phase == DrillPhase::Live && resetAvailable_)
        return n < bm::kFrConvertLevel ? "Reset obtenu ! Tu peux flipper dedans" : "Reset obtenu : flippe dans la balle !";
    const StepTracker& st = impl_->steps;
    if (drill_.full && !st.empty()) {
        if (st.running()) return st.currentLine();
        return st.number(0) + st.steps().front().label;  // before GO: the first step
    }
    if (m == Mech::FlipReset && n < bm::kFrConvertLevel && impl_->setup.airRollHint != 0)
        return std::string("1. Air roll à ") + (impl_->setup.airRollHint < 0 ? "gauche" : "droite") +
               ", roues vers la balle  2. Pose les 4 roues dessus";
    return bm::drillAction(m, level, impl_->setup.flickTarget);
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
    // Only a value the grade really measured (finite) is a record candidate, see bm::recordValueOf.
    r.hasRecordValue = bm::recordValueOf(static_cast<Mech>(mech), g, r.score, r.valueForRecord);
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
        const int n = bm::playerLevel(level);
        StepTracker& steps = impl_->steps;
        const float now = ctx.snap.time;
        const bool matches = bm::gradeMatchesDrill(dm, g) || (dm == Mech::PowerShot && g.family == bm::Family::Pinch);
        if (matches) {
            const int minScore = bm::drillMinScore(cfg_.successScore, level);
            bm::DrillVerdict v = bm::judgeDrill(dm, g, level, minScore, cfg_.pinchSpeed);
            if (dm == Mech::PowerShot && g.family == bm::Family::Pinch) {
                const bool onTarget = !g.projection.valid || g.projection.onTarget;
                v.success = g.value("exit_speed") >= bm::powerShotSpeedNeeded(level) && onTarget &&
                            g.score * 100.f >= static_cast<float>(minScore);
            }
            // Breezi niveaux 1-2: the tornado alone, checked by the module (a flip that missed does not undo it).
            if (dm == Mech::Breezi && n < bm::kBzTornadoLevel && !v.success && tornadoOk(impl_->parcours)) {
                v.success = true;
                v.extraErrors.clear();
            }
            if (drill_.full) {
                // The full sequence asks a little more than the short drill where a step names it.
                if (dm == Mech::PowerShot && g.family == bm::Family::PowerShot && g.variant != "flip_shot" && v.success) {
                    v.success = false;
                    v.extraErrors.insert(v.extraErrors.begin(), "PS_NO_FLIP");
                }
                if (dm == Mech::AirRoll && g.value("off_axis") > 90.f && v.success) {
                    v.success = false;
                    v.extraErrors.insert(v.extraErrors.begin(), "AE_OFF_CENTER");
                }
                if (steps.isComplete()) {
                    // Done on the step checks (reset obtained, first air touch): the grade brings the measures.
                    v.success = true;
                    v.extraErrors.clear();
                } else if (steps.running()) {
                    if (v.success) {
                        while (steps.running()) steps.complete(now);
                    } else {
                        const std::string crit = bm::criteriaStep(dm, n);
                        bool criteria = !v.extraErrors.empty();
                        for (const auto& e : v.extraErrors) criteria = criteria && isCriteriaCode(e);
                        if (criteria && !crit.empty() && steps.indexOf(crit) > steps.currentRequired()) {
                            while (steps.running() && steps.currentRequired() < steps.indexOf(crit)) steps.complete(now);
                        }
                        std::string code = !v.extraErrors.empty() ? v.extraErrors.front()
                                                                  : (!g.errors.empty() ? g.errors.front() : std::string());
                        steps.fail(now, code);
                    }
                }
                if (!v.success && dm == Mech::Ceiling && impl_->parcours.tooFast) v.extraErrors.push_back("CEIL_TOO_FAST");
            }
            AttemptResult r = makeResult(g, drill_.mech, v.success, v.extraErrors, ctx.profile);
            drill_.lastVerdict = v.success ? std::string("Réussi ! ") + verdictLabel(r.verdict) + " " + fmt0(r.score) +
                                                 "/100"
                                           : "Raté : " + fmt0(r.score) + "/100";
            drill_.result = r;
            drill_.hasResult = true;
            publish(std::move(r), drill_.mech, &g, ctx, out);
            resolveDrill(v.success, ctx);
            return;
        }
        // Full sequence: grades of other mechanics before the last step are part of the way there (a touch while
        // climbing the wall, a pop on the way to the ball): ignored (fix 2).
        if (drill_.full && !steps.onLastStep()) return;
        // Another mechanic resolved the attempt: it failed for the drill (main tip: wrong mechanic, then the drill's).
        const std::string what = bm::gradeTitle(g);
        if (drill_.full) steps.fail(now, "DRILL_WRONG_MECH");
        failDrill("DRILL_WRONG_MECH", ctx, out);
        drill_.lastVerdict = std::string("Raté (") + what + ")";
        return;
    }
    // A success found by the step checks already ended this attempt: its late grade is the same attempt.
    if (drill_.phase == DrillPhase::Result && impl_->swallowFamily && bm::gradeMatchesDrill(static_cast<Mech>(drill_.mech), g))
        return;
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
    if (drill_.phase == DrillPhase::Live) {
        drill_.activity = true;
        touchThisTick_ = true;
    }
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
            if (drill_.phase == DrillPhase::Live) resetThisTick_ = true;
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
    impl_->ready.stop();
    impl_->hang.stop();
    impl_->steps.reset();
    gameSpeedSet_ = false;  // the engine puts the game speed back when the activity ends
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
    beginAttempt(ctx, ReadyKind::First);
    const float level = stairs_[static_cast<size_t>(mech)].level();
    const bm::DrillMode mode = static_cast<bm::DrillMode>(modes_[static_cast<size_t>(mech)]);
    const std::string note = bm::drillLevelNote(static_cast<Mech>(mech), level, mode);
    t.text = bm::fillButtons(mi.drillHint, ctx.profile, keyStyle_) + " " + bm::modeLabel(static_cast<Mech>(mech), mode) +
             ", niveau " + std::to_string(bm::playerLevel(level)) + "/10" + (note.empty() ? "." : " : " + note + ".");
    t.kind = ToastKind::Info;
    out.toasts.push_back(t);
    return true;
}

void BallMechModule::stopDrill(Actions* out, bool release) {
    if (drill_.phase == DrillPhase::Off) return;
    drill_.phase = DrillPhase::Off;
    drill_.burnStep = -1;
    impl_->pipeline.flick().setTarget("");
    impl_->ready.stop();
    impl_->hang.stop();
    impl_->steps.reset();
    if (release) releaseActivity();
    gameSpeedSet_ = false;  // the engine restores the game speed with the activity
    if (out) {
        ToastMessage t;
        t.title = "Mécas balle";
        t.text = "Exercice arrêté.";
        out->toasts.push_back(t);
    }
}

void BallMechModule::beginAttempt(const TickContext& ctx, ReadyKind kind) {
    const size_t mi = static_cast<size_t>(drill_.mech);
    const Mech m = static_cast<Mech>(drill_.mech);
    const float level = stairs_[mi].level();
    const int n = bm::playerLevel(level);
    rng_.reseed(mix64(static_cast<uint64_t>(ctx.snap.frame) ^ (++rngCounter_ * 0x9e3779b97f4a7c15ULL)));
    const int team = ctx.snap.car.valid ? ctx.snap.car.team : 0;
    bm::SetupOptions o;
    o.mode = static_cast<bm::DrillMode>(modes_[mi]);
    o.pickupNiveau = impl_->pickup.niveau();
    o.flickVariant = flickVariant_;
    impl_->setup = bm::makeDrillSetup(m, level, team, ctx.snap.car, ctx.snap.ball.radius > 10.f ? ctx.snap.ball.radius : 91.25f,
                                      rng_, o);
    const bm::DrillSetup& d = impl_->setup;
    impl_->pipeline.flick().setTarget(d.flickTarget);
    drill_.full = d.mode == bm::DrillMode::Full;
    drill_.phase = DrillPhase::Ready;
    drill_.phaseStart = ctx.snap.time;
    drill_.burnStep = -1;
    drill_.airborneSeen = false;
    drill_.activity = false;
    drill_.failAt = -1.f;
    drill_.failCode.clear();
    drill_.resolved = false;
    drill_.completeAt = -1.f;
    drill_.flickLanded = false;
    impl_->swallowFamily = false;
    impl_->hang.stop();
    // Steps of the full sequence (shown before GO: the first one is the instruction).
    if (drill_.full) impl_->steps.setSteps(bm::parcoursSteps(m, n, d, impl_->parcours));
    else impl_->steps.clear();
    // Get-ready hold (fix 1): car and ball re-placed with zero velocity on every tick until GO. The first attempt
    // waits for the throttle then counts 3, 2, 1; the next ones hold 1.5 s at niveaux 1-3, the setting above.
    impl_->hold = HoldPose{};
    impl_->hold.car = d.car;
    BallPlacement held = d.ball;
    held.vel = {};
    held.angVel = {};
    impl_->hold.ball = held;
    impl_->hold.boost = d.car.boost;
    ReadyPhase::Options ro;
    if (kind == ReadyKind::First) {
        ro = ReadyPhase::firstAttempt();
        ro.nativeMessages = cfg_.drillGo;
    } else {
        ro = ReadyPhase::timed(bm::drillReadyTime(level, cfg_.drillReady));
    }
    // Ball balanced on a stopped car, or car on the ceiling: a throttle still held at GO would throw the ball off the
    // roof (keep the car on the ceiling), so GO waits until it is let go, on every hold.
    ro.waitThrottleRelease = throttleSpoilsStart(m, d);
    impl_->readyNative = ro.nativeMessages;
    impl_->readySeconds = ro.countdown;
    impl_->ready.start(ctx.snap.time, ro);
}

void BallMechModule::launch(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const bm::DrillSetup& d = impl_->setup;
    const Mech m = static_cast<Mech>(drill_.mech);
    const float level = stairs_[static_cast<size_t>(drill_.mech)].level();
    CarPlacement c = d.car;
    drill_.burnStep = -1;
    if (d.burnFlip) {
        // The scripted double jump gives back the impulse the setup took off. A car without its flip (the previous
        // attempt ended in the air) cannot jump: no script, and the velocity keeps the impulse.
        if (s.car.hasFlip) drill_.burnStep = 0;
        else c.vel += basisFromRot(c.rot).up * bm::arena::kJumpImpulse;
    }
    out.setCar = c;
    if (d.hangBall) {
        impl_->hang.hang(d.ball.pos);
        impl_->hang.update(s, out);
    } else {
        out.setBall = d.ball;
    }
    if (d.gameSpeed < 0.999f || gameSpeedSet_) {
        out.setGameSpeed = d.gameSpeed;
        gameSpeedSet_ = d.gameSpeed < 0.999f;
    }
    // The first attempt's countdown sent its own GO; the short holds get the native GO only (no 3, 2, 1).
    if (cfg_.drillGo && !impl_->readyNative && impl_->readySeconds >= 0.3f) out.goMessage = true;
    drill_.launchFrame = s.frame;
    impl_->pipeline.ignoreUntil(s.frame + 2);
    impl_->pipeline.resetDetectors();
    impl_->pipeline.clearHeld();
    if (d.doubleTapRead) impl_->pipeline.armDoubleTapRead();
    if (d.armCeiling) impl_->pipeline.armCeiling();
    bm::parcoursBegin(impl_->parcours, m, bm::playerLevel(level), d, s.car.valid ? s.car.team : 0, !ctx.game.isCustomMap);
    if (drill_.full) impl_->steps.start(s.time);
    impl_->pickup.beginAttempt();
    touchThisTick_ = false;
    resetThisTick_ = false;
    drill_.phase = DrillPhase::Live;
    drill_.phaseStart = s.time;
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

void BallMechModule::failDrill(const char* code, const TickContext& ctx, Actions& out, bool withPrimary) {
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
    if (dm == Mech::Ceiling && impl_->parcours.tooFast) codes.push_back("CEIL_TOO_FAST");
    if (withPrimary && std::string(code) != primaryFailCode(dm)) codes.push_back(primaryFailCode(dm));
    r.tips = bm::buildTips(codes, ctx.profile, keyStyle_);
    // A step's own tip when the code has none in the table (never a failure without advice, fix 5).
    if (r.tips.empty() && drill_.full && impl_->steps.failed() && !impl_->steps.failure().tip.empty()) {
        Tip t;
        t.code = bm::tipCode(code);
        t.text = impl_->steps.failure().tip;
        t.weight = 30.f;
        r.tips.push_back(std::move(t));
    }
    // Nothing was measured: no record value (a 0 placeholder would read as a real, if poor, value).
    r.valueForRecord = 0.f;
    r.hasRecordValue = false;
    drill_.lastVerdict = "Raté";
    drill_.result = r;
    drill_.hasResult = true;
    publish(std::move(r), drill_.mech, nullptr, ctx, out);
    resolveDrill(false, ctx);
}

bool BallMechModule::idleTimeout(const TickContext& ctx, Actions& out) {
    if (drill_.activity) return false;
    if (++drill_.idleTimeouts >= 3) {
        stopDrill(&out, true);
        ToastMessage t;
        t.title = "Mécas balle";
        t.text = "Exercice mis en pause : tu n'as pas joué depuis 3 essais.";
        out.toasts.push_back(t);
    } else {
        beginAttempt(ctx);
    }
    return true;
}

void BallMechModule::stepFailed(const TickContext& ctx, Actions& out) {
    const StepTracker::Failure& f = impl_->steps.failure();
    if (f.timedOut && idleTimeout(ctx, out)) return;
    if (f.group == pickup::kGroup) {
        // A missed pickup is not an attempt of the mechanic: it only feeds the pickup's own staircase (fix 4).
        AttemptResult r;
        r.module = "ballmech";
        r.skill = bm::mechInfo(drill_.mech).skill;
        r.title = "Ramassage : raté";
        r.success = false;
        r.verdict = Verdict::Fail;
        r.time = ctx.snap.time;
        // The step's own text (it knows whether the ball was resting or rolling); the code gives the HUD line.
        Tip t;
        t.code = bm::tipCode(f.code);
        t.text = f.tip.empty() ? impl_->steps.missingText() : bm::fillButtons(f.tip, ctx.profile, keyStyle_);
        t.weight = 30.f;
        r.tips.push_back(std::move(t));
        drill_.lastVerdict = "Ramassage raté";
        drill_.lastSuccess = false;
        drill_.result = r;
        drill_.hasResult = true;
        drill_.resolved = true;
        drill_.idleTimeouts = 0;
        drill_.phase = DrillPhase::Result;
        drill_.phaseStart = ctx.snap.time;
        out.requestSave = true;
        return;
    }
    const std::string code = f.code.empty() ? std::string(primaryFailCode(static_cast<Mech>(drill_.mech))) : f.code;
    failDrill(code.c_str(), ctx, out, false);
}

void BallMechModule::checksSucceeded(const TickContext& ctx, Actions& out) {
    // The steps alone prove the success (breezi tornado at niveaux 1-2), or the grade did not come in time.
    const Mech dm = static_cast<Mech>(drill_.mech);
    const bm::ParcoursState& st = impl_->parcours;
    bm::MechGrade g;
    g.family = bm::mechInfo(dm).family;
    g.success = true;
    g.tStart = drill_.phaseStart;
    g.tEnd = ctx.snap.time;
    g.drillOnly = true;
    if (dm == Mech::Breezi) {
        g.variant = "tornado";
        const float airRoll = st.airTicks ? static_cast<float>(st.airRollTicks) / static_cast<float>(st.airTicks) : 0.f;
        const float kept = st.airTicks ? static_cast<float>(st.keptTicks) / static_cast<float>(st.airTicks) : 0.f;
        const float rollDeg = std::fabs(st.rollAccum) * kRadToDeg;
        g.metrics.push_back(bm::makeMetric("roll_deg", "Roulis accumulé", rollDeg, "°", 360.f));
        g.metrics.push_back(bm::makeMetric("air_roll_frac", "Air roll tenu", airRoll * 100.f, "%"));
        g.metrics.push_back(bm::makeMetric("kept_frac", "Balle près de toi", kept * 100.f, "%", 100.f));
        g.score = clamp01(0.5f + 0.25f * bm::score01(rollDeg, 230.f, 360.f) + 0.25f * kept);
    } else {
        g.variant = dm == Mech::FlipReset ? "obtained" : "steps";
        g.score = 0.6f;
    }
    AttemptResult r = makeResult(g, drill_.mech, true, {}, ctx.profile);
    if (dm == Mech::FlipReset) r.title = "Flip reset : reset obtenu";
    else if (dm == Mech::AirDribble) r.title = "Air dribble : pop et touche en l'air";
    drill_.lastVerdict = std::string("Réussi ! ") + verdictLabel(r.verdict) + " " + fmt0(r.score) + "/100";
    drill_.result = r;
    drill_.hasResult = true;
    publish(std::move(r), drill_.mech, &g, ctx, out);
    impl_->swallowFamily = true;
    resolveDrill(true, ctx);
}

void BallMechModule::tornadoLanded(const TickContext& ctx, Actions& out) {
    // Breezi (short start): a carried jump that came back down without a flick grade. Niveaux 1-2: the tornado alone
    // is the goal; from niveau 3 the flip was missing. Never a silent timeout (fix 5).
    const int n = bm::playerLevel(stairs_[static_cast<size_t>(drill_.mech)].level());
    const bool ok = tornadoOk(impl_->parcours);
    if (n < bm::kBzTornadoLevel && ok) {
        checksSucceeded(ctx, out);
        return;
    }
    failDrill(ok ? "FL_NO_CONTACT" : "FL_BREEZI_NO_SPIN", ctx, out, false);
}

void BallMechModule::fullTick(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const float now = s.time;
    const Mech dm = static_cast<Mech>(drill_.mech);
    StepTracker& steps = impl_->steps;
    const bm::DrillSetup& d = impl_->setup;
    if (steps.running()) steps.update(s, &ctx);
    // The pickup's staircase records its result (an idle player's timeout is no attempt: silent restart below).
    const bool idleTimedOut = steps.failed() && steps.failure().timedOut && !drill_.activity;
    if (d.pickup && !idleTimedOut && impl_->pickup.update(steps) != 0) out.requestSave = true;
    // Hanging ball: let go at the release step (or when the car comes close); a touch frees it by itself.
    HangingBall& hang = impl_->hang;
    if (hang.hanging()) {
        const bool byStep = !d.releaseStep.empty() && steps.done(d.releaseStep);
        const bool byDist = d.releaseDist > 0.f && distance(s.car.pos, hang.position()) < d.releaseDist;
        if ((byStep || byDist) && d.releaseApex > 0.f) {
            // Ceiling parcours, niveaux 1-3: where the car drops depends on its heading on the wall, so the ball is
            // relaunched from the car's real fall, right in front of its nose (one setBall: moved, then let go).
            const BallPlacement b = bm::ceilingReleaseAim(s.car, d.releaseApex);
            hang.hang(b.pos);
            hang.release(b.vel);
            // The ball jumps on the next snapshot: not a reset, the ceiling detector keeps the drop it is following.
            impl_->pipeline.allowBallMoveUntil(s.frame + 4);
        } else if (byStep || byDist) {
            hang.release(d.releaseVel);
        }
    }
    hang.update(s, out, impl_->parcours.touch);

    if (steps.failed()) {
        stepFailed(ctx, out);
        return;
    }
    if (steps.isComplete()) {
        if (bm::onChecksComplete(dm) == bm::CompleteAction::Resolve) {
            checksSucceeded(ctx, out);
            return;
        }
        if (drill_.completeAt < 0.f) drill_.completeAt = now;
        if (now - drill_.completeAt > kAwaitGrade) checksSucceeded(ctx, out);
        return;  // waiting for the grade: no generic check can fail a done sequence
    }

    // Generic checks, on only from the step that needs them (fix 2).
    const int world = impl_->parcours.worldWheels;
    const bool landOn = bm::landingCheckOn(dm, steps);
    const bool lostOn = bm::ballLostCheckOn(dm, steps);
    if (!landOn) drill_.airborneSeen = false;
    else if (world == 0) drill_.airborneSeen = true;
    if (drill_.failAt < 0.f) {
        if (landOn && drill_.airborneSeen && world >= 3 && s.car.pos.z < 300.f) {
            drill_.failAt = now + kLandGrace;
            drill_.failCode = bm::landingCode(dm);
        } else if (landOn && dm == Mech::AirDribble && s.ball.pos.z <= 110.f) {
            drill_.failAt = now + kLandGrace;
            drill_.failCode = "AD_SHORT";
        } else if (lostOn && s.ball.pos.z < s.ball.radius + 40.f && distance(s.car.pos, s.ball.pos) > 300.f) {
            drill_.failAt = now + kLandGrace;
            drill_.failCode = "DRILL_BALL_LOST";
        }
    }
    if (drill_.failAt >= 0.f) {
        const bool recovered = (drill_.failCode != "DRILL_BALL_LOST" && world == 0 && s.car.pos.z > 50.f &&
                                (dm != Mech::AirDribble || s.ball.pos.z > 110.f)) ||
                               (drill_.failCode == "DRILL_BALL_LOST" && distance(s.car.pos, s.ball.pos) <= 300.f);
        if (recovered) {
            drill_.failAt = -1.f;
            drill_.failCode.clear();
        } else if (now >= drill_.failAt) {
            const std::string code = drill_.failCode;
            steps.fail(now, code);
            failDrill(code.c_str(), ctx, out, false);
            return;
        }
    }
    // Safety net: the sum of the step limits (a step without a limit waits for its grade).
    float total = 3.f;
    for (const Step& st : steps.steps()) total += st.timeout;
    if (now - drill_.phaseStart > total) {
        steps.failOnTimeout(now);
        stepFailed(ctx, out);
    }
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
        if (gameSpeedSet_ && impl_->setup.gameSpeed > 0.999f) {  // next attempt not slowed: normal speed already for its hold
            out.setGameSpeed = 1.f;
            gameSpeedSet_ = false;
        }
        // GO tick: launch without holding (the hold's freeze would swallow the inputs the player already gives).
        if (impl_->ready.update(s, out)) launch(ctx, out);
        else applyHold(impl_->hold, &s, out);
        break;
    case DrillPhase::Live: {
        if (drill_.burnStep >= 0) {
            // Scripted double jump right after the launch: the car starts without its flip (flip reset drill). If the
            // flip is still there 6 ticks later (the first press was lost), press once more.
            Inputs in;
            const int k = drill_.burnStep;
            if (k == 6 && !s.car.hasFlip) {
                drill_.burnStep = -1;
            } else {
                in.jump = k == 1 || k == 2 || k == 6 || k == 7;
                out.overrideInputs = in;
                if (++drill_.burnStep > 8) drill_.burnStep = -1;
            }
        }
        if (drill_.burnStep < 0 &&
            (s.in.jump || s.in.boost || std::fabs(s.in.throttle) > 0.1f || bm::stickMagnitude(s.in) > 0.3f)) {
            drill_.activity = true;
        }
        const Mech dm = static_cast<Mech>(drill_.mech);
        bm::parcoursTick(impl_->parcours, s, touchThisTick_, resetThisTick_);
        touchThisTick_ = false;
        resetThisTick_ = false;
        if (drill_.full) {
            if (!drill_.resolved) fullTick(ctx, out);
            break;
        }
        const int world = impl_->parcours.worldWheels;
        if (world == 0 && s.frame > drill_.launchFrame + 3) drill_.airborneSeen = true;
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        if (!drill_.resolved && dm == Mech::Breezi && impl_->parcours.landedAfterJump && !drill_.flickLanded) {
            drill_.flickLanded = true;
            tornadoLanded(ctx, out);
            break;
        }
        if (!drill_.resolved && drill_.failAt < 0.f) {
            const bool flickFamily = dm == Mech::Flick || dm == Mech::Musty || dm == Mech::Breezi;
            if (mi.airborneDrill && drill_.airborneSeen && world >= 3 && s.car.pos.z < 300.f) {
                drill_.failAt = now + kLandGrace;
                drill_.failCode = "DRILL_LANDED";
            } else if (flickFamily && now - drill_.phaseStart > 0.3f && s.ball.pos.z < s.ball.radius + 40.f &&
                       distance(s.car.pos, s.ball.pos) > 300.f) {
                drill_.failAt = now + kLandGrace;
                drill_.failCode = "DRILL_BALL_LOST";
            } else if (now - drill_.phaseStart > drillTimeout()) {
                if (!idleTimeout(ctx, out)) failDrill("DRILL_TIMEOUT", ctx, out);
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
        for (size_t i = 1; i < cmd.args.size(); ++i) {
            bm::DrillMode mode;
            if (bm::modeFromId(cmd.args[i], mode)) {
                modes_[static_cast<size_t>(m)] = static_cast<int>(mode);
                modeChosen_[static_cast<size_t>(m)] = true;
                out.requestSave = true;
            } else if (m == Mech::Flick) {
                const std::string v = cmd.args[i] == "any" || cmd.args[i] == "tous" ? "" : cmd.args[i];
                if (flickVariantValid(v)) flickVariant_ = v;
            }
        }
        startDrill(static_cast<int>(m), ctx, out);
    } else if (n == "stop") {
        if (drill_.phase == DrillPhase::Off) return;
        stopDrill(&out, true);
    } else if (n == "next" || n == "restart") {
        // Posted from the menu: wait for the throttle like a first attempt, the player is not in the car yet.
        if (drill_.phase != DrillPhase::Off) beginAttempt(ctx, ReadyKind::First);
    } else if (n == "mode" && cmd.args.size() >= 2) {
        Mech m;
        bm::DrillMode mode;
        if (!bm::mechFromId(cmd.args[0], m) || !bm::modeFromId(cmd.args[1], mode)) {
            toast("Usage : mode <méca> <court|parcours>", ToastKind::Warning);
            return;
        }
        modes_[static_cast<size_t>(m)] = static_cast<int>(mode);
        modeChosen_[static_cast<size_t>(m)] = true;
        out.requestSave = true;
        // The running drill of that mechanic switches at its next attempt.
        if (drill_.phase != DrillPhase::Off && drill_.mech == static_cast<int>(m) && drill_.phase != DrillPhase::Live)
            beginAttempt(ctx, ReadyKind::First);  // posted from the menu: wait for the throttle
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
        levelChosen_[static_cast<size_t>(m)] = true;
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
        levelChosen_.fill(false);
        impl_->pickup.reset();
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
    const float scale = (ctx.hudScale > 0.f ? ctx.hudScale : 1.f) * cfg_.hudScale;
    const float now = ctx.hasSnapshot ? ctx.snap.time : lastTickTime_;
    const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
    const Occluder occ{view.ballValid, view.ballVisualPos, view.ballRadius};

    // Compact HUD: at most 3 lines per panel (result, one tip, flip reset or current action). Measures, counters
    // and help are detail lines, shown only when the compact HUD is off. During a drill only the drill panel is
    // drawn: it carries the attempt's result, so the main panel would repeat it.
    const bool compact = ctx.compactHud;

    // ---- Main panel (free play)
    if (cfg_.hudEnabled && drill_.phase == DrillPhase::Off) {
        DrawPanel p;
        p.pos = {clamp01(cfg_.hudX) * W, clamp01(cfg_.hudY) * H};
        p.title = "Mécas balle";
        p.scale = scale;
        p.minWidth = 250.f * scale;
        const bool showResult = hasLast_ && (cfg_.hudSeconds <= 0.f || (now - lastShownAt_ >= 0.f && now - lastShownAt_ <= cfg_.hudSeconds));
        if (showResult) {
            const Color vc = verdictColor(last_.verdict);
            p.title = last_.title;
            PanelLine score = hudLine(std::string(verdictLabel(last_.verdict)) + " : " + fmt0(last_.score) + "/100" +
                                          (last_.success ? "  (réussi)" : ""),
                                      vc);
            score.bar = clamp01(last_.score / 100.f);
            score.barColor = vc;
            p.lines.push_back(score);
            if (cfg_.hudTip) {
                if (!last_.tips.empty()) p.lines.push_back(hudLine(bm::hudTipText(last_.tips.front()), kTipColor));
                else if (last_.success) p.lines.push_back(hudLine(kWellDone, kGreen));
            }
        }
        if (resetAvailable_ && detecting_) p.lines.push_back(hudLine("Flip reset disponible !", kGreen));
        if (showResult) addMetricLines(p, last_, lastMech_, cfg_.hudMetrics, unit);
        if (cfg_.hudResetCounter && impl_->stats.sessionResets > 0)
            p.lines.push_back(hudLine("Flip resets (session) : " + std::to_string(impl_->stats.sessionResets),
                                      {180, 220, 255, 255}, true, 0.9f));
        if (panelHasContent(p, compact)) out.panel(std::move(p));
    }

    // ---- Drill panel (top-left exercise zone): last result, its main tip, then what to do now. During a full
    // sequence the steps replace the action line (validated steps turn green, the current one is highlighted) and
    // the previous result becomes detail lines, so the compact HUD keeps at most 3 main lines.
    if (drill_.phase != DrillPhase::Off) {
        const bm::MechInfo& mi = bm::mechInfo(drill_.mech);
        const Mech dm = static_cast<Mech>(drill_.mech);
        const float level = stairs_[static_cast<size_t>(drill_.mech)].level();
        const StepTracker& steps = impl_->steps;
        const bm::ParcoursState& ps = impl_->parcours;
        DrawPanel d;
        d.pos = {clamp01(cfg_.drillHudX) * W, clamp01(cfg_.drillHudY) * H};
        d.title = std::string("Exercice : ") + mi.title + " (" + levelText(level) + ")";
        d.scale = scale;
        d.minWidth = 260.f * scale;
        const bool liveSteps = drill_.full && drill_.phase == DrillPhase::Live && !steps.idle();
        const bool resultSteps = drill_.full && drill_.phase == DrillPhase::Result && steps.finished();
        // Gauges of the live attempt: wheels toward the ball (flip reset), nose down (musty).
        PanelLine gauge;
        bool hasGauge = false;
        if (drill_.phase == DrillPhase::Live && !drill_.resolved && ctx.hasSnapshot && ctx.snap.car.valid && ctx.snap.ball.valid) {
            const Snapshot& sn = ctx.snap;
            if (dm == Mech::FlipReset && !resetAvailable_ && distance(sn.car.pos, sn.ball.pos) < 1200.f && ps.worldWheels == 0) {
                const float a = bm::undersideAlignment(sn.car, sn.ball);
                gauge = hudLine("Roues vers la balle", a > 0.8f ? kGreen : Color{255, 110, 70, 255});
                gauge.bar = clamp01((a + 1.f) * 0.5f);
                gauge.barColor = gauge.color;
                hasGauge = true;
            } else if (dm == Mech::Musty && ps.airPhase) {
                const float pitch = std::asin(clampf(sn.car.basis.fwd.z, -1.f, 1.f)) * kRadToDeg;
                gauge = hudLine("Nez vers le bas", pitch <= -35.f ? kGreen : Color{255, 110, 70, 255});
                gauge.bar = clamp01(-pitch / 60.f);
                gauge.barColor = gauge.color;
                hasGauge = true;
            }
        }
        const bool demoteResult = drill_.phase == DrillPhase::Live && (liveSteps || hasGauge);
        if (drill_.hasResult && !drill_.lastVerdict.empty() && (drill_.attempts > 0 || drill_.full)) {
            const std::string counts = drill_.attempts > 0 ? "   (" + std::to_string(drill_.successes) + "/" +
                                                                 std::to_string(drill_.attempts) + " réussis)"
                                                           : std::string();
            d.lines.push_back(hudLine(drill_.lastVerdict + counts, drill_.lastSuccess ? kGreen : Color{255, 100, 70, 255},
                                      demoteResult, demoteResult ? 0.9f : 1.f));
            if (cfg_.hudTip) {
                if (const Tip* t = drillTip()) d.lines.push_back(hudLine(bm::hudTipText(*t), kTipColor, demoteResult, demoteResult ? 0.9f : 1.f));
                else if (drill_.lastSuccess) d.lines.push_back(hudLine(kWellDone, kGreen, demoteResult, demoteResult ? 0.9f : 1.f));
            }
        }
        if (liveSteps || resultSteps) {
            StepHudStyle style;
            style.flashSeconds = resultSteps ? 0.f : 1.f;
            std::vector<PanelLine> lines;
            appendStepLines(lines, steps, now, style);
            // Time left on the step to do now.
            const int req = steps.currentRequired();
            if (liveSteps && req < steps.size()) {
                const float left = steps.timeLeft(now);
                const std::string prefix = steps.number(req);
                for (auto& l : lines)
                    if (!l.detail && l.text.rfind(prefix, 0) == 0 && left < 1e8f)
                        l.text += "   " + fmt0(std::ceil(std::max(0.f, left))) + " s";
            }
            for (auto& l : lines) d.lines.push_back(std::move(l));
        } else {
            PanelLine act = hudLine(drillActionNow(), drill_.full ? kStepAccent : colors::White);
            if (drill_.phase == DrillPhase::Ready) {
                const ReadyPhase& rp = impl_->ready;
                if (rp.counting()) {
                    const float total = std::max(0.1f, impl_->readySeconds);
                    act.bar = clamp01(1.f - rp.remaining(now) / total);
                    act.barColor = {255, 220, 120, 255};
                }
            } else if (drill_.phase == DrillPhase::Live) {
                const float left = std::max(0.f, drillTimeout() - (now - drill_.phaseStart));
                act.text += "   " + fmt0(std::ceil(left)) + " s";
                act.bar = clamp01(left / std::max(0.1f, drillTimeout()));
                act.barColor = {90, 170, 255, 255};
            }
            d.lines.push_back(act);
            // First attempt: wait for the throttle, then "Départ dans 3" (the later holds are short: the bar only).
            // A start where the throttle spoils everything (ball balanced on a stopped car, car on the ceiling) says
            // to let go of it during the countdown, and on every hold GO waits for that (line shown then too).
            const bool releaseWait = impl_->ready.waitingRelease(now);  // "Lâche l'accélérateur pour partir"
            if (drill_.phase == DrillPhase::Ready && (impl_->ready.waiting() || impl_->readyNative || releaseWait)) {
                std::string line = impl_->ready.hudLine(now);
                if (impl_->ready.counting() && !releaseWait && throttleSpoilsStart(dm, impl_->setup))
                    line += " : lâche l'accélérateur";
                d.lines.push_back(hudLine(line, kStepAccent));
            }
        }
        if (hasGauge) d.lines.push_back(gauge);
        // Details (full HUD only): situation, counters, the last attempt's measures, how to stop.
        d.lines.push_back(hudLine("Situation : " + impl_->setup.label, {220, 220, 220, 255}, true, 0.9f));
        const float rate = drill_.attempts > 0 ? static_cast<float>(drill_.successes) / static_cast<float>(drill_.attempts) : 0.f;
        d.lines.push_back(hudLine("Essais : " + std::to_string(drill_.attempts) + " | Réussis : " +
                                      std::to_string(drill_.successes) +
                                      (drill_.attempts > 0 ? " (" + formatPercent(rate) + ")" : ""),
                                  {180, 220, 255, 255}, true, 0.9f));
        if (drill_.hasResult) {
            d.lines.push_back(hudLine(drill_.result.title, {220, 220, 220, 255}, true, 0.9f));
            addMetricLines(d, drill_.result, drill_.mech, cfg_.hudMetrics, unit);
        }
        d.lines.push_back(hudLine("Arrêter : rlt ballmech stop", {220, 220, 220, 255}, true, 0.9f));
        out.panel(std::move(d));

        // 3D guide of the first levels (ceiling start): the ball's path to the meeting point and a ring there.
        const bm::DrillSetup& su = impl_->setup;
        if (!drill_.resolved && (drill_.phase == DrillPhase::Ready || drill_.phase == DrillPhase::Live) && su.hasMeet) {
            const Color gc{120, 200, 255, 200};
            for (int i = 0; i + 1 < su.guideCount; ++i)
                out.line3D(su.guide[static_cast<size_t>(i)], su.guide[static_cast<size_t>(i + 1)], gc, 2.f, occ);
            out.circle3D(su.meetPoint, {0.f, 0.f, 1.f}, 120.f, {255, 199, 64, 220}, 3.f, 24, occ);
        }
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
    u.successScore = cfg_.successScore;
    u.pinchSpeed = cfg_.pinchSpeed;
    u.timeoutScale = cfg_.drillTimeoutScale;
    std::array<int, bm::kMechCount> order{};  // position of each mechanic in the learning order
    for (size_t i = 0; i < order.size(); ++i) order[static_cast<size_t>(bm::learningOrder()[i])] = static_cast<int>(i);
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
        for (const char* step : mi.steps)
            if (step && *step) v.steps.emplace_back(step);
        v.difficulty = bm::difficultyLabel(mi.difficulty);
        v.order = order[static_cast<size_t>(i)];
        v.progression = bm::drillProgression(static_cast<Mech>(i));
        v.mode = modes_[static_cast<size_t>(i)];
        v.modeShort = bm::modeLabel(static_cast<Mech>(i), bm::DrillMode::Short);
        v.modeFull = bm::modeLabel(static_cast<Mech>(i), bm::DrillMode::Full);
        v.attempts = ms.attempts;
        v.successes = ms.successes;
        v.sessionAttempts = ms.sessionAttempts;
        v.sessionSuccesses = ms.sessionSuccesses;
        v.average = ms.average();
        v.best = ms.best;
        v.recent = ms.recent;
        v.drillLevel = stairs_[static_cast<size_t>(i)].level();
        v.levelNumber = bm::playerLevel(v.drillLevel);
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
        d.action = drillActionNow();
        if (const Tip* t = drillTip()) d.tip = t->text;
        d.level = stairs_[static_cast<size_t>(drill_.mech)].level();
        d.levelNumber = bm::playerLevel(d.level);
        d.attempts = drill_.attempts;
        d.successes = drill_.successes;
        d.timeLeft = drill_.phase == DrillPhase::Live ? std::max(0.f, drillTimeout() - (lastTickTime_ - drill_.phaseStart)) : 0.f;
        d.lastVerdict = drill_.lastVerdict;
        d.lastSuccess = drill_.lastSuccess;
        d.full = drill_.full;
        d.modeLabel = bm::modeLabel(static_cast<Mech>(drill_.mech), static_cast<bm::DrillMode>(modes_[static_cast<size_t>(drill_.mech)]));
        d.waitingThrottle = drill_.phase == DrillPhase::Ready && impl_->ready.waiting();
        const StepTracker& st = impl_->steps;
        if (drill_.full && !st.empty()) {
            for (int i = 0; i < st.size(); ++i) {
                std::string line = st.number(i) + st.steps()[static_cast<size_t>(i)].label;
                const StepState ss = st.state(i);
                if (ss == StepState::Done) line += " : OK";
                else if (ss == StepState::Failed) line += " : raté";
                d.steps.push_back(std::move(line));
            }
            d.currentStep = st.running() ? st.current() : -1;
            if (st.failed()) d.missing = st.missingText();
            if (drill_.phase == DrillPhase::Live && st.running()) {
                const float left = st.timeLeft(lastTickTime_);
                d.timeLeft = left < 1e8f ? std::max(0.f, left) : 0.f;
            }
        }
        if (drill_.phase == DrillPhase::Ready) d.timeLeft = 0.f;
    }
    u.pickupLevel = impl_->pickup.niveau();
    return u;
}

// ---- Persistence -------------------------------------------------------------------------------------------
void BallMechModule::save(Store& store) const {
    impl_->stats.save(store);
    for (int i = 0; i < bm::kMechCount; ++i) {
        const std::string key = std::string("ballmech.drill.") + bm::mechInfo(i).id;
        stairs_[static_cast<size_t>(i)].save(store, key);
        store.setInt(key + ".chosen", levelChosen_[static_cast<size_t>(i)] ? 1 : 0);
    }
    store.setString("ballmech.flick_variant", flickVariant_);
    for (int i = 0; i < bm::kMechCount; ++i) {
        if (!modeChosen_[static_cast<size_t>(i)]) continue;
        store.setInt(std::string("ballmech.mode.") + bm::mechInfo(i).id, modes_[static_cast<size_t>(i)]);
    }
    impl_->pickup.save(store, "ballmech.pickup");
}

void BallMechModule::load(const Store& store) {
    impl_->stats.load(store);
    for (int i = 0; i < bm::kMechCount; ++i) {
        Staircase& s = stairs_[static_cast<size_t>(i)];
        s = Staircase(cfg_.drillTarget, kStairStep, kStairStart);
        // Only a played or chosen level is kept: the previous version saved untried drills at its old start value.
        const std::string key = std::string("ballmech.drill.") + bm::mechInfo(i).id;
        levelChosen_[static_cast<size_t>(i)] = store.getInt(key + ".chosen", 0) != 0;
        if (levelChosen_[static_cast<size_t>(i)] || store.getInt(key + ".attempts", 0) > 0) s.load(store, key);
    }
    const std::string v = store.getString("ballmech.flick_variant", "");
    flickVariant_ = flickVariantValid(v) ? v : "";
    for (int i = 0; i < bm::kMechCount; ++i) {
        const std::string key = std::string("ballmech.mode.") + bm::mechInfo(i).id;
        const int mode = static_cast<int>(store.getInt(key, -1));
        modeChosen_[static_cast<size_t>(i)] = mode == 0 || mode == 1;
        modes_[static_cast<size_t>(i)] = modeChosen_[static_cast<size_t>(i)] ? mode : static_cast<int>(bm::defaultMode(static_cast<Mech>(i)));
    }
    impl_->pickup.reset();
    if (store.getInt("ballmech.pickup.attempts", 0) > 0) impl_->pickup.load(store, "ballmech.pickup");
}

} // namespace rlt
