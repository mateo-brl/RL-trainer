#include "core/modules/shooting/ShootingModule.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/common/Units.h"
#include "core/modules/shooting/ShootingGrading.h"

namespace rlt {

using namespace shoot;

namespace {

constexpr float kTeleportDist = 400.f; // uu in one tick (car max 19 uu/tick, ball max 50 uu/tick)
constexpr float kFixedFlickLevel = 0.3f; // flick level when the adaptive difficulty is off
const char* const kToastTitle = "Tirs & flicks";

const char* activityName(ShootMode m) {
    switch (m) {
    case ShootMode::Flick: return "Flicks";
    case ShootMode::Targets: return "Cibles";
    case ShootMode::Power: return "Puissance";
    default: return "";
    }
}

std::string zoneWithArticle(Zone z) {
    switch (z) {
    case Zone::TopLeft: return "la lucarne gauche";
    case Zone::TopRight: return "la lucarne droite";
    case Zone::LowLeft: return "le poteau bas gauche";
    case Zone::LowRight: return "le poteau bas droit";
    default: return "le centre du but";
    }
}

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return colors::Cyan;
    case Verdict::Good: return colors::Green;
    case Verdict::Ok: return colors::Yellow;
    case Verdict::Fail: return colors::Red;
    default: return colors::White;
    }
}

float metricValue(const AttemptResult& r, const char* key, float def) {
    for (const auto& m : r.metrics)
        if (m.key == key) return m.value;
    return def;
}

int setupIndex(TargetSetup s) { return std::clamp(static_cast<int>(s), 1, kTargetSetupCount - 1); }

// Console commands save the zone / setup choice like the tab does (ctx.setCvar): through the module's own cvars,
// whose choice indexes are the selections (zone 0..4 or kZoneRandom, setup 0 mix..4).
void persistZone(Actions& out, int zoneSel) {
    out.setCvars.emplace_back("rlt_sh_target_zone", std::to_string(zoneSel));
}
void persistSetup(Actions& out, int setupSel) {
    out.setCvars.emplace_back("rlt_sh_target_setup", std::to_string(setupSel));
}

} // namespace

const char* shootModeLabel(ShootMode m) {
    switch (m) {
    case ShootMode::Flick: return "Flicks";
    case ShootMode::Targets: return "Cibles";
    case ShootMode::Power: return "Puissance";
    default: return "Aucun exercice";
    }
}

const char* shootPhaseLabel(ShootPhase p) {
    switch (p) {
    case ShootPhase::Setup: return "Mise en place";
    case ShootPhase::Countdown: return "Prépare-toi";
    case ShootPhase::Live: return "À toi";
    case ShootPhase::Result: return "Résultat";
    default: return "En attente";
    }
}

// ---- Construction / settings ------------------------------------------------------------------------------
ShootingModule::ShootingModule() { flick_.setThresholds(thresholds_); }

void ShootingModule::describeSettings(SettingsSchema& schema) {
    auto g = schema.section(&Settings::shooting, "shooting", "Exercices");
    g.toggle("rlt_sh_enabled", &ShootingConfig::enabled, true, "Activer",
             "Active le module Tirs & flicks : exercices de flicks, cibles dans le but et puissance.");
    g.choice("rlt_sh_unit", &ShootingConfig::unit, 0, {"Auto (réglage du jeu)", "km/h", "mph", "uu/s"},
             "Unité de vitesse", "Unité des vitesses affichées. Auto suit le réglage métrique de Rocket League.");
    g.toggle("rlt_sh_auto_repeat", &ShootingConfig::autoRepeat, true, "Enchaîner les essais",
             "Remet la voiture et la balle en place automatiquement après chaque essai.");
    g.number("rlt_sh_countdown", &ShootingConfig::countdown, 1.f, 0.f, 3.f, "Délai avant l'essai (s)",
             "Temps pendant lequel la voiture et la balle restent immobiles avant chaque essai.", 1);
    g.number("rlt_sh_result_hold", &ShootingConfig::resultHold, 1.5f, 0.5f, 5.f, "Pause après l'essai (s)",
             "Temps d'affichage du résultat avant l'essai suivant.", 1);
    g.toggle("rlt_sh_native_countdown", &ShootingConfig::nativeCountdown, true, "Compte à rebours du jeu",
             "Affiche le compte à rebours et le GO de Rocket League au début de chaque essai.");
    g.number("rlt_sh_start_boost", &ShootingConfig::startBoost, 100.f, 0.f, 100.f, "Boost au départ",
             "Quantité de boost donnée au début de chaque essai.", 0);
    g.toggle("rlt_sh_chat_results", &ShootingConfig::chatResults, false, "Résultats dans le chat",
             "Écrit une ligne dans le chat après chaque essai noté.");
    g.toggle("rlt_sh_toast_records", &ShootingConfig::toastRecords, true, "Notifier les records",
             "Affiche une notification quand tu bats un record.");

    auto f = schema.section(&Settings::shooting, "shooting", "Flicks");
    f.choice("rlt_sh_flick_type", &ShootingConfig::flickRequested, 0,
             {"N'importe lequel", "Flick avant", "Flick 45°", "Flick 90°", "Back flick", "Flick retardé", "Musty flick",
              "Breezi flick", "Reverse 45"},
             "Flick demandé", "Le flick à travailler. Un autre flick reste analysé mais compte comme raté.");
    f.choice("rlt_sh_flick_start", &ShootingConfig::flickStart, 0,
             {"Voiture à l'arrêt", "Voiture en mouvement", "Aléatoire"}, "Départ",
             "À l'arrêt pour apprendre le geste, en mouvement vers le but pour le jeu réel.");
    f.toggle("rlt_sh_flick_adaptive", &ShootingConfig::flickAdaptive, true, "Difficulté adaptative",
             "La distance au but, la vitesse de départ et la puissance demandée suivent ton taux de réussite.");
    f.number("rlt_sh_flick_speed", &ShootingConfig::flickCarSpeed, 800.f, 0.f, 2000.f, "Vitesse de départ (uu/s)",
             "Vitesse de la voiture au départ en mouvement quand la difficulté adaptative est coupée.", 0);
    f.choice("rlt_sh_flick_target", &ShootingConfig::flickTarget, 0,
             {"But entier", "Zone choisie dans Cibles", "Zone aléatoire"}, "Cible des flicks",
             "Où viser : la précision de chaque flick est mesurée par rapport à cette cible.");
    f.toggle("rlt_sh_flick_on_target", &ShootingConfig::flickRequireOnTarget, true, "Exiger un tir cadré",
             "Un flick réussi doit partir vers le but (impact prévu dans le cadre).");
    f.toggle("rlt_sh_roof_guide", &ShootingConfig::roofGuide, true, "Guide sur le toit",
             "Petit cercle au centre du toit : vert quand la balle est bien centrée, jaune sinon.");

    auto t = schema.section(&Settings::shooting, "shooting", "Cibles dans le but");
    t.choice("rlt_sh_target_zone", &ShootingConfig::targetZone, kZoneRandom,
             {"Lucarne gauche", "Lucarne droite", "Poteau bas gauche", "Poteau bas droit", "Centre", "Aléatoire"},
             "Zone visée",
             "La zone du but à atteindre (vue de face, depuis le terrain). Aléatoire en change à chaque essai.");
    t.choice("rlt_sh_target_setup", &ShootingConfig::targetSetup, 0,
             {"Mélange", "Balle roulante", "Balle qui rebondit", "Centre en travers", "Balle sur le toit"},
             "Mise en place", "Comment la balle arrive. Mélange alterne les quatre situations.");
    t.toggle("rlt_sh_target_adaptive", &ShootingConfig::targetAdaptive, true, "Difficulté adaptative",
             "La taille des zones et la vitesse de la balle suivent ton taux de réussite (objectif 75 %).");
    t.number("rlt_sh_target_level", &ShootingConfig::targetLevel, 0.3f, 0.f, 1.f, "Niveau fixe",
             "Difficulté utilisée quand l'adaptatif est coupé (0 facile, 1 difficile).", 2);
    t.toggle("rlt_sh_show_zones", &ShootingConfig::showAllZones, true, "Afficher toutes les zones",
             "Dessine aussi les autres zones du but, en discret.");
    t.toggle("rlt_sh_show_prediction", &ShootingConfig::showPrediction, true, "Marqueur d'impact prévu",
             "Pendant que la balle vole, montre où elle va croiser la ligne de but.");

    auto p = schema.section(&Settings::shooting, "shooting", "Puissance");
    p.number("rlt_sh_power_ball_speed", &ShootingConfig::powerBallSpeedKph, 25.f, 0.f, 80.f,
             "Vitesse de la balle (km/h)", "Vitesse de la balle qui roule vers toi.", 0);
    p.number("rlt_sh_power_goal", &ShootingConfig::powerGoalKph, 90.f, 40.f, 180.f, "Objectif (km/h)",
             "Vitesse du tir à atteindre pour réussir l'essai.", 0);

    auto h = schema.section(&Settings::shooting, "shooting", "Affichage");
    h.choice("rlt_sh_hud_mode", &ShootingConfig::hudMode, 0, {"Pendant les exercices", "Toujours", "Jamais"}, "Panneau",
             "Quand afficher le panneau Tirs & flicks (en haut à gauche).");
    h.number("rlt_sh_hud_x", &ShootingConfig::hudX, 0.01f, 0.f, 1.f, "Position X",
             "Position horizontale du panneau (fraction de l'écran).", 3);
    h.number("rlt_sh_hud_y", &ShootingConfig::hudY, 0.12f, 0.f, 1.f, "Position Y",
             "Position verticale du panneau (fraction de l'écran).", 3);
    h.number("rlt_sh_hud_scale", &ShootingConfig::hudScale, 1.f, 0.5f, 3.f, "Taille du panneau",
             "Multiplie la taille du texte du panneau.", 2);
    h.toggle("rlt_sh_free_analysis", &ShootingConfig::freeAnalysis, false, "Impact prévu hors exercice",
             "Montre aussi l'impact prévu de tes tirs en jeu libre, sur les maps workshop et dans les replays.");
    h.color("rlt_sh_zone_color", &ShootingConfig::zoneColor, Color{200.f, 200.f, 200.f, 140.f}, "Couleur des zones",
            "Couleur des zones du but qui ne sont pas visées.");
    h.color("rlt_sh_target_color", &ShootingConfig::targetColor, Color{255.f, 200.f, 40.f, 255.f},
            "Couleur de la cible", "Couleur de la zone à atteindre.");
    h.color("rlt_sh_marker_color", &ShootingConfig::markerColor, Color{0.f, 220.f, 255.f, 255.f}, "Couleur du marqueur",
            "Couleur du marqueur d'impact prévu quand le tir est hors de la cible.");

    auto a = schema.section(&Settings::shooting, "shooting", "Avancé");
    a.advanced();
    a.number("rlt_sh_target_min_speed", &ShootingConfig::targetMinSpeedKph, 0.f, 0.f, 150.f,
             "Vitesse minimale des tirs (km/h)",
             "Mode Cibles : un tir plus lent ne compte pas comme réussi. 0 = pas de minimum.", 0);
    a.number("rlt_sh_attempt_timeout", &ShootingConfig::attemptTimeout, 10.f, 3.f, 30.f, "Durée max d'un essai (s)",
             "Au-delà, la balle est remise en place sans note.", 0);
    a.number("rlt_sh_roof_gap", &ShootingConfig::roofGap, 2.f, 0.f, 20.f, "Écart balle / toit (uu)",
             "Espace laissé sous la balle quand elle est posée sur le toit.", 1);
    a.number("rlt_sh_musty_pitch", &ShootingConfig::mustyPitchDeg, -35.f, -90.f, 0.f, "Seuil musty (inclinaison, °)",
             "Le nez doit être au moins aussi bas au moment du backflip pour compter un musty. À calibrer.", 0);
    a.number("rlt_sh_breezi_roll", &ShootingConfig::breeziRollDeg, 230.f, 90.f, 720.f, "Seuil breezi (rotation, °)",
             "Rotation en roulis cumulée avant le backflip pour compter un breezi. À calibrer.", 0);
    a.number("rlt_sh_delayed_sec", &ShootingConfig::delayedSec, 0.6f, 0.3f, 1.2f, "Seuil flick retardé (s)",
             "Délai minimum entre le saut et le dodge pour compter un flick retardé.", 2);
    a.number("rlt_sh_roof_reach", &ShootingConfig::roofReach, 260.f, 150.f, 400.f, "Portée balle / voiture (uu)",
             "Distance maximale entre la balle et la voiture pour considérer que la balle est encore sur la voiture.",
             0);
}

void ShootingModule::onSettingsChanged(const Settings& settings) {
    // Zone / setup selections follow their cvars when those change (any module's change calls this). The
    // "targets", "zone" and "setup" commands set the selection at once and save it through Actions::setCvars,
    // so the cvar catches up a moment later with the same value.
    if (!settingsSeen_ || settings.shooting.targetZone != cfg_.targetZone)
        zoneSel_ = std::clamp(settings.shooting.targetZone, 0, kZoneRandom);
    if (!settingsSeen_ || settings.shooting.targetSetup != cfg_.targetSetup)
        setupSel_ = std::clamp(settings.shooting.targetSetup, 0, kTargetSetupCount - 1);
    settingsSeen_ = true;
    cfg_ = settings.shooting;
    thresholds_.mustyPitchDeg = cfg_.mustyPitchDeg;
    thresholds_.breeziRollDeg = cfg_.breeziRollDeg;
    thresholds_.delayedSec = cfg_.delayedSec;
    thresholds_.roofReach = cfg_.roofReach;
    flick_.setThresholds(thresholds_);
    if (!cfg_.enabled && mode_ != ShootMode::None) stopDrill(true);
}

void ShootingModule::setSeed(uint64_t seed) {
    rng_.reseed(seed);
    fixedSeed_ = true;
}

// ---- Accessors --------------------------------------------------------------------------------------------
const Staircase& ShootingModule::targetStaircase(TargetSetup s) const {
    return targetStairs_[static_cast<size_t>(setupIndex(s))];
}

float ShootingModule::flickLevel() const { return cfg_.flickAdaptive ? flickStair_.level() : kFixedFlickLevel; }

float ShootingModule::targetLevel(TargetSetup s) const {
    return cfg_.targetAdaptive ? targetStaircase(s).level() : clamp01(cfg_.targetLevel);
}

ShootingStatus ShootingModule::status() const {
    ShootingStatus st;
    st.mode = mode_;
    st.phase = phase_;
    st.requestedFlick = mode_ == ShootMode::Flick ? requested_ : cfg_.flickRequested - 1;
    st.zoneSelection = zoneSel_;
    st.currentZone = static_cast<int>(zone_);
    st.setupSelection = setupSel_;
    st.currentSetup = static_cast<int>(setup_);
    st.flickZoneActive = flickZoneActive_;
    st.flickZone = static_cast<int>(flickZone_);
    st.flickLevel = flickLevel();
    for (int i = 1; i < kTargetSetupCount; ++i)
        st.setupLevels[static_cast<size_t>(i)] = targetLevel(static_cast<TargetSetup>(i));
    st.hasLastShot = hasLastShot_;
    st.lastShotSpeed = lastShotSpeed_;
    st.hasLastResult = hasLastResult_;
    st.lastResult = lastResult_;
    st.lastFlick = lastFlick_;
    st.message = message_;
    st.attempts = attempts_;
    st.settled = flick_.phase() == FlickPhase::Carry;
    st.settledTime = flick_.settledTime(prev_.frame);
    return st;
}

// ---- Commands ---------------------------------------------------------------------------------------------
void ShootingModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    const std::string& n = cmd.name;
    auto warn = [&](const std::string& text) {
        out.toasts.push_back(ToastMessage{kToastTitle, text, ToastKind::Warning, 4.f});
    };

    if (n == "stop") {
        if (mode_ != ShootMode::None) {
            stopDrill(true);
            message_ = "Exercice arrêté.";
        }
        return;
    }
    if (n == "reset_stats") {
        const bool sessionOnly = !cmd.args.empty() && cmd.args[0] == "session";
        if (sessionOnly) {
            stats_.resetSession();
        } else {
            stats_.resetAll();
            flickStair_ = Staircase();
            for (auto& st : targetStairs_) st = Staircase();
        }
        out.requestSave = true;
        out.toasts.push_back(ToastMessage{
            kToastTitle, sessionOnly ? "Statistiques de la session effacées." : "Statistiques et records effacés.",
            ToastKind::Info, 3.f});
        return;
    }
    if (!cfg_.enabled) {
        warn("Active d'abord le module Tirs & flicks.");
        return;
    }
    if (n == "flick" || n == "flicks") {
        int req = cfg_.flickRequested - 1;
        if (!cmd.args.empty()) {
            int t = -1;
            // Diagonal / side are recognised flicks but not drills: refuse them rather than grade anything.
            if (!parseFlickType(cmd.args[0], t) || t >= kRequestableFlickCount) {
                warn("Flick inconnu : " + cmd.args[0] +
                     ". Choix : front, 45, 90, backflip, delayed, musty, breezi, reverse, any.");
                return;
            }
            req = t;
        }
        requested_ = std::clamp(req, -1, kRequestableFlickCount - 1);
        startDrill(ShootMode::Flick, ctx, out);
        return;
    }
    if (n == "targets" || n == "target" || n == "cibles") {
        int zsel = zoneSel_, ssel = setupSel_;
        bool zoneGiven = false, setupGiven = false;
        for (const auto& arg : cmd.args) {
            int v = 0;
            if (parseZone(arg, v)) {
                zsel = v;
                zoneGiven = true;
            } else if (parseSetup(arg, v)) {
                ssel = v;
                setupGiven = true;
            } else {
                warn("Zone inconnue : " + arg + ". Choix : tl, tr, bl, br, center, random.");
                return;
            }
        }
        zoneSel_ = std::clamp(zsel, 0, kZoneRandom);
        setupSel_ = std::clamp(ssel, 0, kTargetSetupCount - 1);
        // Saved even when the drill cannot start here, like the tab's zone buttons.
        if (zoneGiven) persistZone(out, zoneSel_);
        if (setupGiven) persistSetup(out, setupSel_);
        startDrill(ShootMode::Targets, ctx, out);
        return;
    }
    if (n == "power" || n == "puissance") {
        startDrill(ShootMode::Power, ctx, out);
        return;
    }
    if (n == "next") {
        if (mode_ != ShootMode::None) newAttemptLater("Nouvel essai.");
        return;
    }
    if (n == "setup") {
        int v = 0;
        if (cmd.args.empty() || !parseSetup(cmd.args[0], v)) {
            warn("Mise en place inconnue. Choix : mix, rolling, bouncing, pass, roof.");
            return;
        }
        setupSel_ = std::clamp(v, 0, kTargetSetupCount - 1);
        persistSetup(out, setupSel_);
        const std::string text = std::string("Mise en place : ") + setupLabel(static_cast<TargetSetup>(setupSel_));
        if (mode_ == ShootMode::Targets) {
            newAttemptLater(text + ".");
        } else {
            message_ = text + " (pour le prochain exercice Cibles).";
            out.toasts.push_back(ToastMessage{kToastTitle, message_, ToastKind::Info, 3.f});
        }
        return;
    }
    if (n == "zone") {
        int v = 0;
        if (cmd.args.empty() || !parseZone(cmd.args[0], v)) {
            warn("Zone inconnue. Choix : tl, tr, bl, br, center, random.");
            return;
        }
        zoneSel_ = std::clamp(v, 0, kZoneRandom);
        persistZone(out, zoneSel_);
        const std::string label = zoneSel_ == kZoneRandom ? std::string("aléatoire")
                                                          : std::string(zoneLabel(static_cast<Zone>(zoneSel_)));
        if (mode_ == ShootMode::Targets) {
            newAttemptLater("Nouvelle cible : " + label + ".");
        } else {
            // Also the flick target when "Cible des flicks" is "Zone choisie dans Cibles".
            message_ = "Cible choisie : " + label + " (pour le prochain exercice Cibles).";
            out.toasts.push_back(ToastMessage{kToastTitle, message_, ToastKind::Info, 3.f});
        }
        return;
    }
    warn("Commande inconnue : " + n + ". Essaie flick, targets, power, stop ou reset_stats.");
}

bool ShootingModule::startDrill(ShootMode m, const TickContext& ctx, Actions& out) {
    const char* unavailable =
        "Exercice indisponible ici : lance le freeplay (ou autorise les maps workshop dans Réglages, Avancé).";
    if (!ctx.worldControlAllowed || !requestActivity(activityName(m))) {
        out.toasts.push_back(ToastMessage{kToastTitle, unavailable, ToastKind::Warning, 4.5f});
        message_ = "Exercice indisponible ici.";
        return false;
    }
    const bool sameMode = mode_ == m;
    mode_ = m;
    phase_ = ShootPhase::Setup;
    phaseStart_ = ctx.snap.frame;
    if (!fixedSeed_) rng_.reseed(mix64(static_cast<uint64_t>(ctx.snap.frame) + 0x9E3779B97F4A7C15ULL * ++seedCounter_));
    flick_.reset();
    shot_.reset();
    hasLastHit_ = false;
    if (!sameMode) {
        hasLastResult_ = false;
        lastFlick_.clear();
    }
    switch (m) {
    case ShootMode::Flick:
        message_ = requested_ >= 0 ? std::string("Flicks : ") + flickTypeLabel(static_cast<FlickType>(requested_))
                                   : "Flicks libres";
        break;
    case ShootMode::Targets:
        message_ = zoneSel_ == kZoneRandom ? std::string("Cibles aléatoires")
                                           : "Cible : " + std::string(zoneLabel(static_cast<Zone>(zoneSel_)));
        break;
    default: message_ = "Puissance : frappe le plus fort possible"; break;
    }
    return true;
}

void ShootingModule::stopDrill(bool release) {
    const bool wasActive = mode_ != ShootMode::None;
    mode_ = ShootMode::None;
    phase_ = ShootPhase::Idle;
    flick_.reset();
    shot_.reset();
    touched_ = playerMoved_ = powerTouched_ = false;
    lastFlickReport_.reset();
    hasLastHit_ = false;
    expectJumpUntil_ = -1;
    if (release && wasActive) releaseActivity();
}

void ShootingModule::onActivityStopped() { stopDrill(false); }

void ShootingModule::newAttemptLater(const std::string& message) {
    message_ = message;
    phase_ = ShootPhase::Setup;
    flick_.reset();
    shot_.reset();
}

// ---- Tick -------------------------------------------------------------------------------------------------
void ShootingModule::onTick(const TickContext& ctx, Actions& out) {
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    if (!cfg_.enabled) {
        if (mode_ != ShootMode::None) stopDrill(true);
        shot_.reset();
        hasPrev_ = false;
        return;
    }
    flick_.setDodgeDeadzone(ctx.profile.valid ? ctx.profile.dodgeDeadzone : 0.5f);
    bool teleported = false;
    if (hasPrev_ && s.frame > expectJumpUntil_) {
        // Tolerance grows with skipped frames (hitch): the ball moves at most 50 uu per tick.
        const int64_t gap = std::clamp<int64_t>(s.frame - prev_.frame, 1, 120);
        const float tol = kTeleportDist + 55.f * static_cast<float>(gap - 1);
        if (s.car.valid && prev_.car.valid && distance(s.car.pos, prev_.car.pos) > tol) teleported = true;
        if (s.ball.valid && prev_.ball.valid && distance(s.ball.pos, prev_.ball.pos) > tol) teleported = true;
    }
    if (s.paused || ctx.game.paused) {
        prev_ = s;
        hasPrev_ = true;
        return;
    }
    if (hasPrev_ && s.car.valid && s.car.dodging && !prev_.car.dodging) lastDodgeFrame_ = s.frame;
    if (mode_ != ShootMode::None) {
        if (!ctx.ownsActivity || !ctx.worldControlAllowed)
            stopDrill(ctx.ownsActivity);
        else
            tickDrill(ctx, out, teleported);
    } else {
        tickFree(ctx, teleported);
    }
    prev_ = s;
    hasPrev_ = true;
}

void ShootingModule::tickDrill(const TickContext& ctx, Actions& out, bool teleported) {
    const Snapshot& s = ctx.snap;
    switch (phase_) {
    case ShootPhase::Idle: phase_ = ShootPhase::Setup; return;
    case ShootPhase::Setup:
        if (!s.car.valid) return; // spectating / respawning: wait
        prepareAttempt(ctx);
        applyHeld(out, s.frame);
        phase_ = ShootPhase::Countdown;
        phaseStart_ = s.frame;
        lastCountdownShown_ = -1;
        if (cfg_.countdown <= 0.f) release(out, s.frame);
        return;
    case ShootPhase::Countdown: {
        const float remaining = cfg_.countdown - elapsed(s.frame);
        if (remaining > 1e-4f) {
            applyHeld(out, s.frame);
            out.freezeCar = true;
            const int n = static_cast<int>(std::ceil(remaining - 1e-4f));
            if (cfg_.nativeCountdown && n >= 1 && n <= 3 && n != lastCountdownShown_) {
                out.countdownMessage = n;
                lastCountdownShown_ = n;
            }
        } else {
            release(out, s.frame);
        }
        return;
    }
    case ShootPhase::Live:
        if (teleported) {
            newAttemptLater("Position changée : nouvel essai.");
            return;
        }
        if (std::fabs(s.in.throttle) > 0.2f || s.in.jump || s.in.boost) playerMoved_ = true;
        if (mode_ == ShootMode::Flick)
            tickFlick(ctx, out);
        else if (mode_ == ShootMode::Targets)
            tickTargets(ctx, out);
        else
            tickPower(ctx, out);
        return;
    case ShootPhase::Result:
        if (shot_.active()) {
            if (const auto o = shot_.update(s)) {
                const bool good =
                    mode_ == ShootMode::Flick ? flickTargetRect().contains(o->hit) : shoot::onGoal(o->hit);
                if (o->reached) noteHit(*o, s.frame, good);
            }
        }
        if (elapsed(s.frame) >= cfg_.resultHold) {
            if (cfg_.autoRepeat) {
                phase_ = ShootPhase::Setup;
            } else {
                stopDrill(true);
                message_ = "Exercice terminé. Relance-le quand tu veux.";
            }
        }
        return;
    }
}

Zone ShootingModule::pickZone(int selection) {
    if (selection >= 0 && selection < kZoneCount) return static_cast<Zone>(selection);
    int z = rng_.rangeInt(0, kZoneCount - 1);
    if (z == static_cast<int>(zone_)) z = rng_.rangeInt(0, kZoneCount - 1); // fewer repeats
    return static_cast<Zone>(z);
}

ZoneRect ShootingModule::flickTargetRect() const {
    return flickZoneActive_ ? zoneRect(flickZone_, attemptLevel_) : goalMouthRect();
}

void ShootingModule::prepareAttempt(const TickContext& ctx) {
    const Snapshot& s = ctx.snap;
    goal_ = GoalFrame::forTeam(s.car.team);
    RoofParams roof;
    roof.hb = hitboxOf(s.car);
    roof.ballRadius = s.ball.valid && s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    roof.gap = cfg_.roofGap;
    switch (mode_) {
    case ShootMode::Flick: {
        attemptLevel_ = flickLevel();
        const bool moving = cfg_.flickStart == 1 || (cfg_.flickStart == 2 && rng_.chance(0.5f));
        spawn_ = flickSpawn(attemptLevel_, moving, cfg_.flickAdaptive ? -1.f : cfg_.flickCarSpeed, goal_, roof, rng_);
        flickZoneActive_ = cfg_.flickTarget != 0;
        if (cfg_.flickTarget == 1)
            flickZone_ = pickZone(zoneSel_);
        else if (cfg_.flickTarget == 2)
            flickZone_ = pickZone(kZoneRandom);
        break;
    }
    case ShootMode::Targets: {
        zone_ = pickZone(zoneSel_);
        setup_ = setupSel_ >= 1 && setupSel_ < kTargetSetupCount
                     ? static_cast<TargetSetup>(setupSel_)
                     : static_cast<TargetSetup>(rng_.rangeInt(1, kTargetSetupCount - 1));
        attemptLevel_ = targetLevel(setup_);
        spawn_ = targetSpawn(setup_, attemptLevel_, goal_, roof, rng_);
        break;
    }
    case ShootMode::Power:
        attemptLevel_ = 0.f;
        spawn_ = powerSpawn(cfg_.powerBallSpeedKph / kUusToKph, goal_, rng_);
        break;
    default: break;
    }
    touched_ = playerMoved_ = powerTouched_ = false;
    powerShot_ = PowerShot{};
    lastFlickReport_.reset();
    flick_.reset();
    shot_.reset();
    hasLastHit_ = false;
}

void ShootingModule::applyHeld(Actions& out, int64_t frame) {
    CarPlacement c = spawn_.car;
    c.vel = {};
    c.angVel = {};
    c.boost = cfg_.startBoost;
    BallPlacement b = spawn_.ball;
    b.vel = {};
    b.angVel = {};
    out.setCar = c;
    out.setBall = b;
    expectJumpUntil_ = frame + 3;
}

void ShootingModule::release(Actions& out, int64_t frame) {
    CarPlacement c = spawn_.car;
    c.vel = spawn_.carReleaseVel;
    c.angVel = {};
    c.boost = cfg_.startBoost;
    BallPlacement b = spawn_.ball;
    b.vel = spawn_.ballReleaseVel;
    b.angVel = spawn_.ballReleaseAngVel;
    out.setCar = c;
    out.setBall = b;
    if (cfg_.nativeCountdown && cfg_.countdown > 0.f) out.goMessage = true;
    expectJumpUntil_ = frame + 3;
    phase_ = ShootPhase::Live;
    phaseStart_ = frame;
    message_.clear();
    flick_.reset();
    shot_.reset();
    touched_ = playerMoved_ = powerTouched_ = false;
}

// ---- Flick drill ------------------------------------------------------------------------------------------
void ShootingModule::tickFlick(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (const auto rep = flick_.update(s)) {
        onFlickReport(*rep, ctx, out);
        return;
    }
    const FlickPhase ph = flick_.phase();
    if (s.ball.valid && s.car.valid && ph != FlickPhase::Air && ph != FlickPhase::Dodged && elapsed(s.frame) > 0.3f) {
        const bool onFloor = s.ball.pos.z < kBallRestZ + 25.f;
        const bool away = distance(s.ball.pos, hitboxCenter(s.car)) > 250.f;
        if (onFloor && away) {
            if (cfg_.flickAdaptive) flickStair_.record(false);
            lastFlick_ = "Balle perdue";
            finishAttempt(gradeLostBall(requested_, s.time), ctx, out);
            return;
        }
    }
    // Never cut a flick in progress (jump or dodge already started).
    if (elapsed(s.frame) > cfg_.attemptTimeout && ph != FlickPhase::Air && ph != FlickPhase::Dodged)
        newAttemptLater("Temps écoulé : nouvel essai.");
}

void ShootingModule::onFlickReport(const FlickReport& rep, const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
    const ZoneRect rect = flickTargetRect();
    const float radius = s.ball.valid && s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    std::optional<PlaneCrossing> cross;
    if (rep.contact) cross = predictToPlaneY(rep.ballPos, rep.ballVel, rep.ballAngVel, goal_.planeY(), 4.f, radius);
    const AimResult aim = scoreAim(rect, cross.has_value(), cross ? goal_.toPlane(cross->pos) : Vec2{});
    const Vec3 targetWorld = goal_.toWorld(rect.u, rect.z);

    FlickGradeParams p;
    p.requested = requested_;
    p.level = attemptLevel_;
    p.minPower = flickMinPower(attemptLevel_);
    p.requireOnTarget = cfg_.flickRequireOnTarget;
    p.aimErrorDeg = angleBetweenDeg(flat(rep.launchVel), flat(targetWorld - rep.ballPos));
    p.delayedSec = cfg_.delayedSec;
    p.time = s.time;
    AttemptResult res = gradeFlick(rep, aim, p, ctx.profile);

    const float acc = metricValue(res, "accuracy", 0.f);
    const auto flags = stats_.addFlick(rep.category, rep.power, acc, res.success);
    if (cfg_.flickAdaptive) flickStair_.record(res.success);
    lastFlick_ = std::string(flickTypeLabel(rep.category)) + (rep.cls.uncertain ? " (?)" : "");
    if (rep.contact) {
        hasLastShot_ = true;
        lastShotSpeed_ = rep.power;
        shot_.start(s.ball, goal_, s.frame, rep.power); // keeps the impact marker during the result
    }
    if (flags.powerAll)
        toastRecord(out,
                    std::string("Record ") + flickTypeLabel(rep.category) + " : " + formatSpeed(rep.power, unit, 1));
    if (flags.accuracyAll) toastRecord(out, "Record de précision en flick : " + formatNumber(acc, 0) + " %");
    finishAttempt(std::move(res), ctx, out);
}

// ---- Target drill -----------------------------------------------------------------------------------------
void ShootingModule::tickTargets(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (setup_ == TargetSetup::Roof) {
        if (const auto rep = flick_.update(s)) {
            lastFlickReport_ = *rep;
            lastFlick_ = std::string(flickTypeLabel(rep->category)) + (rep->cls.uncertain ? " (?)" : "");
            touched_ = true;
            if (!rep->contact) { // dodge into the void: the shot never happened
                ShotOutcome o;
                o.end = ShotEnd::Short;
                onTargetOutcome(o, ctx, out);
                return;
            }
            hasLastShot_ = true;
            lastShotSpeed_ = rep->power;
            shot_.start(s.ball, goal_, s.frame, rep->power);
        }
    }
    if (shot_.active()) {
        if (const auto o = shot_.update(s)) {
            onTargetOutcome(*o, ctx, out);
            return;
        }
    }
    // A shot in flight always resolves (the tracker has its own flight limit).
    if (elapsed(s.frame) > cfg_.attemptTimeout && !shot_.active()) {
        if (!touched_) {
            if (playerMoved_ && cfg_.targetAdaptive)
                targetStairs_[static_cast<size_t>(setupIndex(setup_))].record(false);
            newAttemptLater("Balle non touchée : nouvel essai.");
        } else {
            newAttemptLater("Temps écoulé : nouvel essai.");
        }
    }
}

void ShootingModule::onTargetOutcome(const ShotOutcome& o, const TickContext& ctx, Actions& out) {
    if (o.end == ShotEnd::Aborted) {
        newAttemptLater("Position changée : nouvel essai.");
        return;
    }
    const Snapshot& s = ctx.snap;
    const ZoneRect rect = zoneRect(zone_, attemptLevel_);
    const AimResult aim = scoreAim(rect, o.reached, o.hit);
    TargetGradeParams p;
    p.zone = zone_;
    p.rect = rect;
    p.setup = setup_;
    p.minSpeed = cfg_.targetMinSpeedKph / kUusToKph;
    p.time = s.time;
    p.flick = lastFlickReport_ ? &*lastFlickReport_ : nullptr;
    AttemptResult res = gradeTarget(aim, o.shotSpeed, p);
    const bool streakRecord = stats_.addTarget(zone_, res.success, res.score);
    if (cfg_.targetAdaptive) targetStairs_[static_cast<size_t>(setupIndex(setup_))].record(res.success);
    if (o.reached) noteHit(o, s.frame, aim.inZone);
    if (streakRecord)
        toastRecord(out, "Série record : " + std::to_string(stats_.bestStreakAll) + " cibles d'affilée !");
    finishAttempt(std::move(res), ctx, out);
}

// ---- Power drill ------------------------------------------------------------------------------------------
void ShootingModule::tickPower(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (powerTouched_) {
        if (s.frame >= powerFirst_ + 30) {
            const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
            AttemptResult res = gradePower(powerShot_, cfg_.powerGoalKph / kUusToKph, s.time);
            const auto flags = stats_.addPower(powerShot_.speed, res.success);
            hasLastShot_ = true;
            lastShotSpeed_ = powerShot_.speed;
            if (s.ball.valid && goal_.sign * s.ball.vel.y > 300.f)
                shot_.start(s.ball, goal_, s.frame, powerShot_.speed);
            if (flags.all) toastRecord(out, "Record de puissance : " + formatSpeed(powerShot_.speed, unit, 1));
            finishAttempt(std::move(res), ctx, out);
        }
        return;
    }
    bool missed = elapsed(s.frame) > cfg_.attemptTimeout;
    if (s.ball.valid && s.car.valid && elapsed(s.frame) > 1.f) {
        const float behind = goal_.sign * (s.car.pos.y - s.ball.pos.y); // > 0: the ball went past the car
        if (behind > 1500.f) missed = true;
    }
    if (missed) newAttemptLater("Balle manquée : nouvel essai.");
}

// ---- Free analysis ----------------------------------------------------------------------------------------
void ShootingModule::tickFree(const TickContext& ctx, bool teleported) {
    const bool analysis = cfg_.freeAnalysis || ctx.game.category == MapCategory::Shooting;
    if (!analysis || teleported) {
        shot_.reset();
        return;
    }
    if (shot_.active()) {
        if (const auto o = shot_.update(ctx.snap)) {
            if (o->reached) noteHit(*o, ctx.snap.frame, shoot::onGoal(o->hit));
        }
    }
}

// ---- Touches / events -------------------------------------------------------------------------------------
void ShootingModule::onTouch(const TouchEvent& t, const TickContext& ctx, Actions& out) {
    (void)out;
    if (!cfg_.enabled || !t.post.ball.valid) return;
    const float post = length(t.post.ball.vel);
    const float pre = t.pre.ball.valid ? length(t.pre.ball.vel) : 0.f;
    // Contacts of a ball carried on the roof are not shots.
    const bool stillOnRoof = t.post.car.valid && ballOnRoof(t.post.ball.pos, t.post.car.pos, t.post.car.basis,
                                                            hitboxOf(t.post.car), t.post.ball.radius);
    if (!stillOnRoof && (t.newContact || post - pre > 300.f)) {
        hasLastShot_ = true;
        lastShotSpeed_ = post;
    }
    if (mode_ == ShootMode::None) {
        const bool analysis = cfg_.freeAnalysis || ctx.game.category == MapCategory::Shooting;
        if (analysis && t.post.car.valid) {
            const GoalFrame g = GoalFrame::forTeam(t.post.car.team);
            if (g.sign * t.post.ball.vel.y > 300.f) shot_.start(t.post.ball, g, t.post.frame, post);
        }
        return;
    }
    if (phase_ != ShootPhase::Live || t.pre.frame < phaseStart_) return;
    switch (mode_) {
    case ShootMode::Flick: flick_.onTouch(t); break;
    case ShootMode::Targets: {
        touched_ = true;
        if (setup_ == TargetSetup::Roof) flick_.onTouch(t);
        if (!stillOnRoof) shot_.start(t.post.ball, goal_, t.post.frame, post);
        break;
    }
    case ShootMode::Power: {
        if (!powerTouched_) {
            powerTouched_ = true;
            powerFirst_ = t.pre.frame;
            powerShot_ = PowerShot{};
        }
        if (t.pre.frame > powerFirst_ + 24 || post <= powerShot_.speed) break;
        powerShot_.speed = post;
        powerShot_.carSpeed = length(t.pre.car.vel);
        const int64_t sinceDodge = t.pre.frame - lastDodgeFrame_;
        const bool recentDodge = sinceDodge >= 0 && sinceDodge <= 72;
        powerShot_.flip = t.pre.car.dodging || recentDodge;
        powerShot_.dodgeToTouch = recentDodge ? static_cast<float>(sinceDodge) * kTickDt : -1.f;
        if (t.pre.car.valid) {
            const Vec3 contact = t.hasHitInfo ? t.hitLocation : t.pre.ball.pos;
            const Vec3 local = t.pre.car.basis.toLocal(contact - hitboxCenter(t.pre.car));
            powerShot_.sideHit = std::fabs(local.y) > std::max(0.f, local.x);
        }
        const auto c = predictToPlaneY(t.post.ball.pos, t.post.ball.vel, t.post.ball.angVel, goal_.planeY(), 4.f,
                                       t.post.ball.radius);
        powerShot_.onGoal = c && shoot::onGoal(goal_.toPlane(c->pos));
        break;
    }
    default: break;
    }
}

void ShootingModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    switch (ev.kind) {
    case GameEventKind::ContextChanged:
        flick_.reset();
        shot_.reset();
        hasPrev_ = false;
        hasLastHit_ = false;
        if (mode_ != ShootMode::None) {
            if (!ctx.worldControlAllowed || !ownsActivity())
                stopDrill(ownsActivity());
            else
                newAttemptLater("Nouvelle map : nouvel essai.");
        }
        break;
    case GameEventKind::MapUnloaded:
        stopDrill(true);
        hasPrev_ = false;
        break;
    case GameEventKind::ResetShot:
        if (mode_ != ShootMode::None)
            newAttemptLater("Nouvel essai.");
        else
            shot_.reset();
        break;
    case GameEventKind::CarSpawned:
        if (mode_ != ShootMode::None && phase_ == ShootPhase::Live) newAttemptLater("Nouvel essai.");
        break;
    case GameEventKind::Goal:
        // The event does not say which goal: only a shot predicted into the attacked goal is resolved by it
        // (an own goal leaves the attempt to the short / timeout rules).
        if (mode_ == ShootMode::Targets && phase_ == ShootPhase::Live) {
            if (shot_.hasPrediction()) {
                if (const auto o = shot_.onGoalEvent(ctx.snap.frame)) onTargetOutcome(*o, ctx, out);
            } else if (!touched_) {
                newAttemptLater("But sans toucher la balle : nouvel essai.");
            }
        } else if (mode_ == ShootMode::None && shot_.hasPrediction()) {
            if (const auto o = shot_.onGoalEvent(ctx.snap.frame)) noteHit(*o, ctx.snap.frame, true);
        }
        break;
    default: break;
    }
}

// ---- Results ----------------------------------------------------------------------------------------------
void ShootingModule::finishAttempt(AttemptResult r, const TickContext& ctx, Actions& out) {
    if (cfg_.chatResults) {
        ChatMessage m;
        m.text = r.title + " : " + verdictLabel(r.verdict) + " (" + formatNumber(r.score, 0) + "/100)";
        if (!r.tips.empty()) m.text += ". " + r.tips.front().text;
        out.chat.push_back(std::move(m));
    }
    lastResult_ = r;
    hasLastResult_ = true;
    ++attempts_;
    out.results.push_back(std::move(r));
    out.requestSave = true;
    phase_ = ShootPhase::Result;
    phaseStart_ = ctx.snap.frame;
    message_.clear();
}

void ShootingModule::noteHit(const ShotOutcome& o, int64_t frame, bool good) {
    hasLastHit_ = o.reached;
    lastHitGood_ = good;
    lastHitWorld_ = o.hitWorld;
    lastHitGoal_ = shot_.goal();
    lastHitFrame_ = frame;
}

void ShootingModule::toastRecord(Actions& out, const std::string& text) const {
    if (cfg_.toastRecords) out.toasts.push_back(ToastMessage{"Record !", text, ToastKind::Ok, 4.f});
}

// ---- Persistence ------------------------------------------------------------------------------------------
void ShootingModule::save(Store& store) const {
    stats_.save(store);
    flickStair_.save(store, "shooting.stair.flick");
    for (int i = 1; i < kTargetSetupCount; ++i)
        targetStairs_[static_cast<size_t>(i)].save(store, std::string("shooting.stair.") +
                                                              setupId(static_cast<TargetSetup>(i)));
}

void ShootingModule::load(const Store& store) {
    stats_.load(store);
    flickStair_.load(store, "shooting.stair.flick");
    for (int i = 1; i < kTargetSetupCount; ++i)
        targetStairs_[static_cast<size_t>(i)].load(store, std::string("shooting.stair.") +
                                                              setupId(static_cast<TargetSetup>(i)));
}

// ---- Drawing ----------------------------------------------------------------------------------------------
void ShootingModule::drawRect(const GoalFrame& g, const ZoneRect& r, const Color& c, float width, float inset,
                              const Occluder& occ, DrawList& out) const {
    const Vec3 a = g.toWorld(r.u - r.hw, r.z - r.hh, inset);
    const Vec3 b = g.toWorld(r.u + r.hw, r.z - r.hh, inset);
    const Vec3 cc = g.toWorld(r.u + r.hw, r.z + r.hh, inset);
    const Vec3 d = g.toWorld(r.u - r.hw, r.z + r.hh, inset);
    out.line3D(a, b, c, width, occ);
    out.line3D(b, cc, c, width, occ);
    out.line3D(cc, d, c, width, occ);
    out.line3D(d, a, c, width, occ);
}

void ShootingModule::drawZones(const GoalFrame& g, Zone active, float level, const Color& activeColor,
                               const Occluder& occ, DrawList& out) const {
    for (int i = 0; i < kZoneCount; ++i) {
        const Zone z = static_cast<Zone>(i);
        const ZoneRect r = zoneRect(z, level);
        if (z == active) {
            drawRect(g, r, activeColor, 3.f, 6.f, occ, out);
            ZoneRect inner = r;
            inner.hw = std::max(5.f, r.hw - 12.f);
            inner.hh = std::max(5.f, r.hh - 12.f);
            drawRect(g, inner, activeColor.withAlpha(150.f), 1.5f, 6.f, occ, out);
        } else if (cfg_.showAllZones) {
            drawRect(g, r, cfg_.zoneColor, 1.f, 4.f, occ, out);
        }
    }
}

void ShootingModule::drawMarker(const GoalFrame& g, const Vec3& world, float radius, const Color& c,
                                const Occluder& occ, DrawList& out) const {
    const Vec3 center = world - g.normal() * 8.f;
    out.circle3D(center, g.normal(), radius, c, 2.f, 20, occ);
    const Vec3 r = g.right() * (radius * 0.4f);
    const Vec3 up{0.f, 0.f, radius * 0.4f};
    out.line3D(center - r, center + r, c, 2.f, occ);
    out.line3D(center - up, center + up, c, 2.f, occ);
}

void ShootingModule::drawRoofGuide(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    if (!view.carValid) return;
    const Hitbox hb = ctx.hasSnapshot && ctx.snap.car.valid ? hitboxOf(ctx.snap.car) : Hitbox{};
    const Basis& b = view.carVisualBasis;
    const Vec3 centre = roofCenter(view.carVisualPos, b, hb) + b.up * 1.5f;
    Occluder occ;
    Color c = colors::Grey;
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
        if (ballOnRoof(view.ballVisualPos, view.carVisualPos, b, hb, view.ballRadius)) {
            const Vec3 off = roofOffset(view.ballVisualPos, view.carVisualPos, b, hb, view.ballRadius);
            c = std::fabs(off.x) <= 25.f && std::fabs(off.y) <= 25.f ? colors::Green : colors::Yellow;
            out.line3D(centre, centre + b.fwd * off.x + b.right * off.y, c, 2.f, occ);
        }
    }
    out.circle3D(centre, b.up, 22.f, c, 2.f, 14, occ);
}

DrawPanel ShootingModule::buildPanel(const ViewContext& view, const DrawContext& ctx) const {
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = std::max(0.3f, ctx.hudScale * cfg_.hudScale);
    p.minWidth = 230.f * p.scale;
    p.title = "Tirs & flicks";
    if (mode_ != ShootMode::None) p.title += std::string(" : ") + shootModeLabel(mode_);
    auto line = [&p](const std::string& text, Color c = colors::White, float bar = -1.f, Color barColor = colors::Green,
                     float scale = 1.f) {
        PanelLine l;
        l.text = text;
        l.color = c;
        l.bar = bar;
        l.barColor = barColor;
        l.scale = scale;
        p.lines.push_back(std::move(l));
    };

    switch (phase_) {
    case ShootPhase::Setup:
    case ShootPhase::Countdown: line(message_.empty() ? "Prépare-toi..." : message_, colors::Yellow); break;
    case ShootPhase::Live:
        if (mode_ == ShootMode::Flick)
            line(requested_ >= 0
                     ? std::string("À toi : ") + flickTypeLabel(static_cast<FlickType>(requested_)) + " vers le but !"
                     : "À toi : flick vers le but !");
        else if (mode_ == ShootMode::Targets)
            line("À toi : vise " + zoneWithArticle(zone_) + " !");
        else
            line("À toi : frappe le plus fort possible !");
        break;
    case ShootPhase::Result:
        if (hasLastResult_)
            line(lastResult_.title + " : " + verdictLabel(lastResult_.verdict) + " (" +
                     formatNumber(lastResult_.score, 0) + "/100)",
                 verdictColor(lastResult_.verdict));
        break;
    default:
        if (!message_.empty()) line(message_, colors::Grey);
        break;
    }

    switch (mode_) {
    case ShootMode::Flick: {
        line(requested_ >= 0 ? std::string("Demandé : ") + flickTypeLabel(static_cast<FlickType>(requested_))
                             : "Flick libre (tous types)");
        if (requested_ >= 0 && phase_ != ShootPhase::Result)
            line(flickCue(static_cast<FlickType>(requested_), ctx.profile,
                          keyStyleFromIndex(ctx.settings.profile.buttonStyle), cfg_.delayedSec),
                 colors::Cyan, -1.f, colors::Green, 0.9f);
        line(std::string("Cible : ") + (flickZoneActive_ ? zoneLabel(flickZone_) : "but entier"), cfg_.targetColor);
        if (phase_ == ShootPhase::Live && flick_.phase() == FlickPhase::Carry) {
            const float settled = flick_.settledTime(ctx.snap.frame);
            line("Balle sur le toit : " + formatNumber(settled, 2) + " s",
                 settled >= 0.3f ? colors::Green : colors::Yellow);
        }
        if (!lastFlick_.empty()) line("Dernier flick : " + lastFlick_);
        if (requested_ >= 0) {
            const float best = stats_.flickAll[static_cast<size_t>(requested_)].bestPower;
            if (best > 0.f)
                line(std::string("Record ") + flickTypeLabel(static_cast<FlickType>(requested_)) + " : " +
                     formatSpeed(best, unit, 1));
        } else {
            float best = 0.f;
            for (const auto& r : stats_.flickAll) best = std::max(best, r.bestPower);
            if (best > 0.f) line("Record tous flicks : " + formatSpeed(best, unit, 1));
        }
        break;
    }
    case ShootMode::Targets: {
        line(std::string("Cible : ") + zoneLabel(zone_) + " (" + setupLabel(setup_) + ")", cfg_.targetColor);
        const ZoneStats& z = stats_.zoneSession[static_cast<size_t>(zone_)];
        if (z.attempts > 0)
            line(capitalized(zoneLabel(zone_)) + " : " + std::to_string(z.hits) + "/" + std::to_string(z.attempts) +
                 " (" + formatPercent(z.hitRate()) + ")");
        line("Série : " + std::to_string(stats_.streak) + " (record " + std::to_string(stats_.bestStreakAll) + ")");
        break;
    }
    case ShootMode::Power:
        line("Objectif : " + formatSpeed(cfg_.powerGoalKph / kUusToKph, unit, 0));
        if (stats_.powerAll.best > 0.f)
            line("Record : " + formatSpeed(stats_.powerAll.best, unit, 1) + " (session " +
                 formatSpeed(stats_.powerSession.best, unit, 1) + ")");
        break;
    default:
        if (stats_.powerAll.best > 0.f) line("Record de puissance : " + formatSpeed(stats_.powerAll.best, unit, 1));
        break;
    }

    // Early feedback while the ball travels: where it is predicted to cross the goal line.
    if (shot_.hasPrediction() && (phase_ == ShootPhase::Live || mode_ == ShootMode::None)) {
        const Vec2 pp = shot_.predicted();
        if (mode_ == ShootMode::Targets && zoneRect(zone_, attemptLevel_).contains(pp))
            line("Impact prévu : dans la cible", colors::Green);
        else if (shoot::onGoal(pp))
            line(mode_ == ShootMode::Targets ? "Impact prévu : cadré, hors cible" : "Impact prévu : cadré",
                 colors::Yellow);
        else
            line("Impact prévu : hors cadre", colors::Red);
    }
    if (hasLastShot_) line("Dernier tir : " + formatSpeed(lastShotSpeed_, unit, 1));
    if (hasLastResult_ && mode_ != ShootMode::Power && mode_ != ShootMode::None) {
        const float acc = metricValue(lastResult_, "accuracy", -1.f);
        if (acc >= 0.f) line("Précision : " + formatNumber(acc, 0) + " %");
    }
    if (phase_ == ShootPhase::Result && hasLastResult_ && !lastResult_.tips.empty())
        line(lastResult_.tips.front().text, colors::Yellow, -1.f, colors::Green, 0.9f);
    if (mode_ == ShootMode::Flick || mode_ == ShootMode::Targets) {
        const float level = mode_ == ShootMode::Flick ? flickLevel() : targetLevel(setup_);
        line("Niveau : " + formatPercent(level), colors::Grey, level, colors::Cyan, 0.85f);
    }
    return p;
}

void ShootingModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled) return;
    const bool analysis = cfg_.freeAnalysis || ctx.game.category == MapCategory::Shooting;
    Occluder occ; // goal lines behind the ball are hidden by it
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
    }
    if (ctx.game.mode == GameMode::Replay) {
        // No physics ticks in replays: predict from the visual ball state only.
        if (analysis && view.valid && view.ballValid && std::fabs(view.ballVel.y) > 500.f) {
            const GoalFrame g{view.ballVel.y > 0.f ? 1.f : -1.f};
            if (const auto c =
                    predictToPlaneY(view.ballVisualPos, view.ballVel, Vec3{}, g.planeY(), 3.f, view.ballRadius)) {
                drawRect(g, goalMouthRect(), cfg_.zoneColor, 1.f, 4.f, occ, out);
                drawMarker(g, c->pos, view.ballRadius, shoot::onGoal(g.toPlane(c->pos)) ? colors::Green : colors::Red,
                           occ, out);
            }
        }
        return;
    }
    const bool drill = mode_ != ShootMode::None;
    if (view.valid) {
        const bool resultShown = phase_ == ShootPhase::Result && hasLastResult_;
        const Color active = resultShown ? (lastResult_.success ? colors::Green : colors::Red) : cfg_.targetColor;
        if (drill && mode_ == ShootMode::Targets) {
            drawZones(goal_, zone_, attemptLevel_, active, occ, out);
        } else if (drill && mode_ == ShootMode::Flick) {
            if (flickZoneActive_)
                drawZones(goal_, flickZone_, attemptLevel_, active, occ, out);
            else
                drawRect(goal_, goalMouthRect(), active, 2.f, 4.f, occ, out);
        }
        if (cfg_.showPrediction && shot_.hasPrediction() && (drill || analysis)) {
            const GoalFrame& g = shot_.goal();
            const Vec2 pp = shot_.predicted();
            bool good = shoot::onGoal(pp);
            if (drill && mode_ == ShootMode::Targets)
                good = zoneRect(zone_, attemptLevel_).contains(pp);
            else if (drill && mode_ == ShootMode::Flick)
                good = flickTargetRect().contains(pp);
            drawMarker(g, shot_.predictedWorld(), view.ballRadius, good ? colors::Green : cfg_.markerColor, occ, out);
        }
        if (hasLastHit_ && ctx.snap.frame - lastHitFrame_ < 240)
            drawMarker(lastHitGoal_, lastHitWorld_, view.ballRadius * 0.6f, lastHitGood_ ? colors::Green : colors::Red,
                       occ, out);
        if (drill && cfg_.roofGuide && phase_ != ShootPhase::Result &&
            (mode_ == ShootMode::Flick || (mode_ == ShootMode::Targets && setup_ == TargetSetup::Roof)))
            drawRoofGuide(view, ctx, out);
    }
    if (cfg_.hudMode == 2 || (cfg_.hudMode == 0 && !drill)) return;
    out.panel(buildPanel(view, ctx));
}

} // namespace rlt
