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
constexpr Color kDetailColor{205.f, 205.f, 205.f, 255.f}; // secondary HUD lines
constexpr Color kTipColor{255.f, 220.f, 120.f, 255.f};
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
ShootingModule::ShootingModule() {
    flick_.setThresholds(thresholds_);
    resetStaircases();
}

void ShootingModule::resetStaircases() {
    flickStair_ = Staircase(0.75f, 0.03f, kShootStartLevel);
    for (auto& st : targetStairs_) st = Staircase(0.75f, 0.03f, kShootStartLevel);
    powerStair_ = Staircase(0.75f, kPowerStep, kShootStartLevel);
    for (auto& st : fullStairs_) st = Staircase(0.75f, 0.03f, kShootStartLevel);
    pickup_.reset(kShootStartLevel);
}

void ShootingModule::resetLevel(ShootDrill d) {
    switch (d) {
    case ShootDrill::Flick: flickStair_ = Staircase(0.75f, 0.03f, kShootStartLevel); break;
    case ShootDrill::Roof:
        targetStairs_[static_cast<size_t>(TargetSetup::Roof)] = Staircase(0.75f, 0.03f, kShootStartLevel);
        break;
    case ShootDrill::Rolling:
        targetStairs_[static_cast<size_t>(TargetSetup::Rolling)] = Staircase(0.75f, 0.03f, kShootStartLevel);
        break;
    case ShootDrill::Bouncing:
        targetStairs_[static_cast<size_t>(TargetSetup::Bouncing)] = Staircase(0.75f, 0.03f, kShootStartLevel);
        break;
    case ShootDrill::Pass:
        targetStairs_[static_cast<size_t>(TargetSetup::Pass)] = Staircase(0.75f, 0.03f, kShootStartLevel);
        break;
    case ShootDrill::Power: powerStair_ = Staircase(0.75f, kPowerStep, kShootStartLevel); break;
    default: break;
    }
    fullStairs_[static_cast<size_t>(d)] = Staircase(0.75f, 0.03f, kShootStartLevel);
}

float ShootingModule::powerGoal() const {
    return (cfg_.powerAdaptive ? powerGoalKph(full_ && fullKind_ == ShootDrill::Power ? fullLevel() : powerStair_.level())
                               : cfg_.powerGoalKph) /
           kUusToKph;
}

float ShootingModule::fullLevel() const { return fullStairs_[static_cast<size_t>(fullKind_)].level(); }

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
    f.choice("rlt_sh_flick_start", &ShootingConfig::flickStart, 1,
             {"Voiture à l'arrêt", "En dribble vers le but", "Aléatoire"}, "Départ",
             "En dribble : comme en match, la balle est sur ton toit et ta voiture roule déjà vers le but depuis le "
             "milieu du terrain. À l'arrêt : pour apprendre le geste, plus près du but.");
    f.toggle("rlt_sh_flick_adaptive", &ShootingConfig::flickAdaptive, true, "Difficulté adaptative",
             "La vitesse du dribble, la distance au but et la vitesse de balle demandée suivent ton taux de réussite.");
    f.number("rlt_sh_flick_speed", &ShootingConfig::flickCarSpeed, 1100.f, 0.f, 2000.f, "Vitesse du dribble (uu/s)",
             "Vitesse de la voiture au départ en dribble quand la difficulté adaptative est coupée. L'accélérateur seul "
             "la tient jusqu'à 1400 uu/s environ.", 0);
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
    t.toggle("rlt_sh_show_prediction", &ShootingConfig::showPrediction, false, "Marqueur d'impact prévu",
             "Pendant que la balle vole, montre où elle va croiser la ligne de but. Coupé par défaut : le point "
             "d'arrivée s'affiche quand même dans le but après le tir.");

    auto p = schema.section(&Settings::shooting, "shooting", "Puissance");
    p.number("rlt_sh_power_ball_speed", &ShootingConfig::powerBallSpeedKph, 25.f, 0.f, 80.f,
             "Vitesse de la balle (km/h)", "Vitesse de la balle qui roule vers toi.", 0);
    p.toggle("rlt_sh_power_adaptive", &ShootingConfig::powerAdaptive, true, "Objectif adaptatif",
             "L'objectif part de 70 km/h au niveau 1 et monte avec tes réussites (110 km/h au niveau 10).");
    p.number("rlt_sh_power_goal", &ShootingConfig::powerGoalKph, 90.f, 40.f, 180.f, "Objectif fixe (km/h)",
             "Vitesse du tir à atteindre quand l'objectif adaptatif est coupé.", 0);

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
    h.toggle("rlt_sh_floor_marks", &ShootingConfig::floorMarks, false, "Cercles au sol",
             "Dessine au sol les prochains rebonds (balle qui rebondit), le point de rencontre (centre en travers) et "
             "le point d'élan (puissance). Coupé par défaut.");
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
             "Délai minimum entre le saut et le flip pour compter un flick retardé.", 2);
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

// The previous version started the flicks with a stopped car near the goal and drew the predicted impact marker by
// default; BakkesMod saved those values in config.cfg. A player who still has exactly an old default never chose it:
// move it once to the new default (dribble start, 1100 uu/s without the adaptive difficulty, no marker). A value the
// player chose is kept, and the saved flag stops it from running twice.
void ShootingModule::migrateDefaults(Actions& out) {
    defaultsV2_ = true;
    out.requestSave = true;
    const bool start = cfg_.flickStart == 0;
    const bool speed = std::fabs(cfg_.flickCarSpeed - 800.f) < 0.5f;
    const bool marker = cfg_.showPrediction;
    if (start) out.setCvars.emplace_back("rlt_sh_flick_start", "1");
    if (speed) out.setCvars.emplace_back("rlt_sh_flick_speed", floatToCvar(1100.f));
    if (marker) out.setCvars.emplace_back("rlt_sh_show_prediction", "0");
    if (!start && !speed && !marker) return;
    // The text says only what moved: a player who chose his own start keeps it and is not told otherwise.
    std::string text;
    if (start)
        text = "Les flicks partent maintenant en dribble depuis le milieu du terrain. Tu peux le changer dans la page "
               "Tirs & flicks.";
    else if (speed)
        text = "Le dribble des flicks part maintenant à 1100 uu/s. Tu peux le changer dans la page Tirs & flicks.";
    if (marker) text += std::string(text.empty() ? "" : " ") +
                        "Le marqueur d'impact prévu est coupé (page HUD, section Trajectoires).";
    out.toasts.push_back(migrationToast(kToastTitle, text));
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
    st.readyLine = phase_ == ShootPhase::Countdown ? ready_.hudLine(prev_.time) : std::string();
    st.movingStart = movingStart_;
    st.full = full_;
    st.fullKind = static_cast<int>(fullKind_);
    if (full_) {
        if (!steps_.idle()) {
            std::vector<PanelLine> lines;
            StepHudStyle hs;
            hs.flashSeconds = 0.f;
            appendStepLines(lines, steps_, prev_.time, hs);
            for (const auto& l : lines) st.steps.push_back(l.text);
        } else {
            st.steps = shoot::fullLabels(fullKind_, shootPlayerLevel(fullLevel()));
        }
    }
    st.powerLevel = powerStair_.level();
    st.powerGoalKph = powerGoal() * kUusToKph;
    for (int i = 0; i < kShootDrillCount; ++i) {
        st.fullLevels[static_cast<size_t>(i)] = fullStairs_[static_cast<size_t>(i)].level();
        st.fullAttempts[static_cast<size_t>(i)] = fullStairs_[static_cast<size_t>(i)].attempts();
    }
    st.pickupLevel = pickup_.staircase().level();
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
            resetStaircases();
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
    auto isFull = [](const std::string& a) { return a == "full" || a == "parcours" || a == "complet"; };
    if (n == "reset_level") {
        // "Revenir au niveau 1": one drill and its parcours complet start again from the first level.
        ShootDrill d = ShootDrill::Flick;
        if (cmd.args.empty() || !parseShootDrill(cmd.args[0], d)) {
            warn("Exercice inconnu. Choix : flick, roof, rolling, bouncing, pass, power.");
            return;
        }
        resetLevel(d);
        out.requestSave = true;
        out.toasts.push_back(ToastMessage{kToastTitle, std::string(shootDrillTitle(d)) + " : retour au niveau 1.",
                                          ToastKind::Info, 3.f});
        return;
    }
    if (n == "flick" || n == "flicks") {
        int req = cfg_.flickRequested - 1;
        bool full = false;
        for (size_t i = 0; i < cmd.args.size(); ++i) {
            if (isFull(cmd.args[i])) {
                full = true;
                continue;
            }
            int t = -1;
            // Diagonal / side are recognised flicks but not drills: refuse them rather than grade anything.
            if (i > 0 || !parseFlickType(cmd.args[i], t) || t >= kRequestableFlickCount) {
                warn("Flick inconnu : " + cmd.args[i] +
                     ". Choix : front, 45, 90, backflip, delayed, musty, breezi, reverse, any.");
                return;
            }
            req = t;
        }
        requested_ = std::clamp(req, -1, kRequestableFlickCount - 1);
        startDrill(ShootMode::Flick, ctx, out, full);
        return;
    }
    if (n == "targets" || n == "target" || n == "cibles") {
        int zsel = zoneSel_, ssel = setupSel_;
        bool zoneGiven = false, setupGiven = false, full = false;
        for (const auto& arg : cmd.args) {
            int v = 0;
            if (isFull(arg)) {
                full = true;
            } else if (parseZone(arg, v)) {
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
        startDrill(ShootMode::Targets, ctx, out, full);
        return;
    }
    if (n == "power" || n == "puissance") {
        startDrill(ShootMode::Power, ctx, out, !cmd.args.empty() && isFull(cmd.args[0]));
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
    warn("Commande inconnue : " + n + ". Essaie flick, targets, power, stop, reset_level ou reset_stats.");
}

bool ShootingModule::startDrill(ShootMode m, const TickContext& ctx, Actions& out, bool full) {
    const char* unavailable =
        "Exercice indisponible ici : lance le freeplay (ou autorise les maps workshop dans Réglages, Avancé).";
    const std::string activity = std::string(activityName(m)) + (full ? " (parcours complet)" : "");
    if (!ctx.worldControlAllowed || !requestActivity(activity)) {
        out.toasts.push_back(ToastMessage{kToastTitle, unavailable, ToastKind::Warning, 4.5f});
        message_ = "Exercice indisponible ici.";
        return false;
    }
    const bool sameMode = mode_ == m && full_ == full;
    mode_ = m;
    full_ = full;
    firstAttempt_ = true;  // the first attempt waits for the throttle
    earlyReplays_ = 0;
    steps_.clear();
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
        message_ = requested_ >= 0 ? std::string("Flicks : ") + flickTypeHudLabel(static_cast<FlickType>(requested_))
                                   : "Flicks libres";
        break;
    case ShootMode::Targets:
        message_ = zoneSel_ == kZoneRandom ? std::string("Cibles aléatoires")
                                           : "Cible : " + std::string(zoneLabel(static_cast<Zone>(zoneSel_)));
        break;
    default: message_ = "Puissance : frappe le plus fort possible"; break;
    }
    if (full) message_ = "Parcours complet : " + message_;
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
    full_ = false;
    steps_.clear();
    ready_.stop();
    movingStart_ = assistActive_ = false;
    offRoof_ = leftRoofEarly_ = false;
    earlyReplays_ = 0;
    bounceMarks_.clear();
    showMeet_ = showRunUp_ = false;
    if (release && wasActive) releaseActivity();
}

void ShootingModule::onActivityStopped() { stopDrill(false); }

void ShootingModule::newAttemptLater(const std::string& message) {
    message_ = message;
    phase_ = ShootPhase::Setup;
    flick_.reset();
    shot_.reset();
    steps_.reset();
    ready_.stop();
    assistActive_ = false;
}

// ---- Tick -------------------------------------------------------------------------------------------------
void ShootingModule::onTick(const TickContext& ctx, Actions& out) {
    if (!ctx.hasSnapshot) return;
    if (!defaultsV2_) migrateDefaults(out);  // ticks only come from a running game: config.cfg is loaded
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
    case ShootPhase::Setup: {
        if (!s.car.valid) return; // spectating / respawning: wait
        prepareAttempt(ctx);
        phase_ = ShootPhase::Countdown;
        phaseStart_ = s.frame;
        lastCountdownShown_ = -1;
        // Get-ready (fix 1): the first attempt of a drill waits for the throttle, then 3 s; then the countdown setting.
        ReadyPhase::Options o = firstAttempt_ ? ReadyPhase::firstAttempt()
                                              : ReadyPhase::Options{false, std::max(0.f, cfg_.countdown), true, false};
        o.nativeMessages = cfg_.nativeCountdown;
        // Ball balanced on a stopped car: a throttle still held at GO would throw it off the roof (the kickoff habit),
        // so GO waits until the throttle is let go.
        o.waitThrottleRelease = !full_ && spawn_.ballOnRoof && !movingStart_;
        ready_.start(s.time, o);
        if (ready_.update(s, out)) {
            release(out, s.frame);
            return;
        }
        applyHeld(out, s.frame);
        out.freezeCar = true;
        return;
    }
    case ShootPhase::Countdown:
        // Car and ball held still every tick (gravity would make a ball placed once fall); GO releases them.
        if (ready_.update(s, out)) {
            release(out, s.frame);
        } else {
            applyHeld(out, s.frame);
            out.freezeCar = true;
        }
        return;
    case ShootPhase::Live:
        if (teleported) {
            newAttemptLater("Position changée : nouvel essai.");
            return;
        }
        if (std::fabs(s.in.throttle) > 0.2f || s.in.jump || s.in.boost) playerMoved_ = true;
        if (s.ball.valid) {
            // Floor bounces (cue "tire quand elle remonte", parcours complet).
            if (prevBallVz_ < -20.f && s.ball.vel.z > prevBallVz_ + 40.f && s.ball.vel.z >= -5.f &&
                s.ball.pos.z < kBallRestZ + 40.f)
                lastFloorBounce_ = s.time;
            prevBallVz_ = s.ball.vel.z;
        }
        tickAssist(s, out);
        if (earlyLossReplay(s)) return;
        if (full_) {
            fullTick(fullState_, s);
            steps_.update(s, &ctx);
            if (fullState_.setup.pickup) pickup_.update(steps_);
            if (steps_.running() && elapsed(s.frame) > fullSetup_.timeLimit) steps_.failOnTimeout(s.time);
            if (steps_.finished()) {
                finishFull(ctx, out);
                return;
            }
        }
        if (mode_ == ShootMode::Flick)
            tickFlick(ctx, out);
        else if (mode_ == ShootMode::Targets)
            tickTargets(ctx, out);
        else
            tickPower(ctx, out);
        if (full_ && phase_ == ShootPhase::Live && steps_.finished()) finishFull(ctx, out);
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

// Moving roof start: for kRoofAssistSeconds after the release (levels up to kRoofAssistMaxLevel), the ball is put back
// on its resting point with the car's velocity while it has not slid more than kRoofAssistRadius: the player has the
// time to react.
void ShootingModule::tickAssist(const Snapshot& s, Actions& out) {
    if (!assistActive_) return;
    const bool flying = flick_.phase() == FlickPhase::Air || flick_.phase() == FlickPhase::Dodged;
    if (elapsed(s.frame) > kRoofAssistSeconds || !s.car.valid || !s.ball.valid || !s.car.onGround || flying || s.in.jump) {
        assistActive_ = false;
        return;
    }
    const float r = s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    const Vec3 rest = roofSpawn(s.car.pos, s.car.basis, hitboxOf(s.car), r, cfg_.roofGap);
    if (distance(s.ball.pos, rest) > kRoofAssistRadius) {
        assistActive_ = false;
        return;
    }
    BallPlacement b;
    b.pos = rest;
    b.vel = s.car.vel;
    out.setBall = b;
    expectJumpUntil_ = s.frame + 3; // our own placement is not a teleport
}

// A ball that falls off the roof in the first second of a moving roof start is replayed: the player could not read
// that setup. The fall starts on the roof edge and ends on the floor a few tenths later, so the moment the ball left
// the roof is what counts (the last time, a ball back on the roof is carried again).
bool ShootingModule::earlyLossReplay(const Snapshot& s) {
    if (!movingStart_ || !s.ball.valid || !s.car.valid) return false;
    const FlickPhase ph = flick_.phase();
    if (ph == FlickPhase::Air || ph == FlickPhase::Dodged || touched_) return false;
    const bool onRoof = shootBallOnRoof(s);
    if (onRoof) {
        offRoof_ = false;
    } else if (!offRoof_) {
        offRoof_ = true;
        leftRoofEarly_ = elapsed(s.frame) <= kEarlyLossReplay;
    }
    if (onRoof || !leftRoofEarly_) return false;
    const bool onFloor = s.ball.pos.z < kBallRestZ + 25.f;
    const bool away = distance(s.ball.pos, hitboxCenter(s.car)) > 250.f;
    if (!(onFloor && away)) return false;
    // Twice in a row at most: a start the player keeps losing is graded as a lost ball (level lowered, tip shown)
    // instead of an endless replay.
    if (earlyReplays_ >= kMaxEarlyReplays) return false;
    ++earlyReplays_;
    newAttemptLater("Balle tombée au départ : essai rejoué, sans compter.");
    return true;
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
    touched_ = playerMoved_ = powerTouched_ = false;
    powerShot_ = PowerShot{};
    lastFlickReport_.reset();
    flick_.reset();
    shot_.reset();
    hasLastHit_ = false;
    movingStart_ = assistActive_ = false;
    bounceMarks_.clear();
    showMeet_ = showRunUp_ = false;
    lastFloorBounce_ = -1.f;
    prevBallVz_ = 0.f;
    if (full_) {
        prepareFull(ctx);
        return;
    }
    switch (mode_) {
    case ShootMode::Flick: {
        attemptLevel_ = flickLevel();
        const bool moving = cfg_.flickStart == 1 || (cfg_.flickStart == 2 && rng_.chance(0.5f));
        spawn_ = flickSpawn(attemptLevel_, moving, cfg_.flickAdaptive ? -1.f : cfg_.flickCarSpeed, goal_, roof, rng_);
        flickZoneActive_ = cfg_.flickTarget != 0;
        if (cfg_.flickTarget == 1)
            flickZone_ = pickTargetZone(zoneSel_, attemptLevel_, flickZone_, rng_);
        else if (cfg_.flickTarget == 2)
            flickZone_ = pickTargetZone(kZoneRandom, attemptLevel_, flickZone_, rng_);
        break;
    }
    case ShootMode::Targets: {
        setup_ = setupSel_ >= 1 && setupSel_ < kTargetSetupCount
                     ? static_cast<TargetSetup>(setupSel_)
                     : static_cast<TargetSetup>(rng_.rangeInt(1, kTargetSetupCount - 1));
        attemptLevel_ = targetLevel(setup_);
        zone_ = pickTargetZone(zoneSel_, attemptLevel_, zone_, rng_);  // low and centre zones only at niveaux 1-3
        spawn_ = targetSpawn(setup_, attemptLevel_, goal_, roof, rng_);
        break;
    }
    case ShootMode::Power:
        attemptLevel_ = powerStair_.level();
        spawn_ = powerSpawn(cfg_.powerBallSpeedKph / kUusToKph, goal_, rng_);
        break;
    default: break;
    }
    movingStart_ = spawn_.ballOnRoof && length2D(spawn_.carReleaseVel) > 1.f;
    // Circles on the floor, shown from the countdown: the next bounces, the meeting point, the run-up point.
    const Vec3& bp = spawn_.ball.pos;
    if (mode_ == ShootMode::Targets && setup_ == TargetSetup::Bouncing) {
        bounceMarks_ = predictFloorBounces(bp, spawn_.ballReleaseVel, spawn_.ballReleaseAngVel, 3);
    } else if (mode_ == ShootMode::Targets && setup_ == TargetSetup::Pass) {
        meet_ = crossMeetingPoint(bp, spawn_.ballReleaseVel, spawn_.ballReleaseAngVel, spawn_.car.pos).pos;
        showMeet_ = true;
    } else if (mode_ == ShootMode::Power) {
        runUp_ = powerRunUpPoint(spawn_.car.pos, powerMeetingPoint(spawn_.car.pos, bp, spawn_.ballReleaseVel));
        showRunUp_ = true;
    }
}

// Parcours complet: the setup of its kind at its own level (the pickup start at the pickup's level).
void ShootingModule::prepareFull(const TickContext& ctx) {
    const Snapshot& s = ctx.snap;
    RoofParams roof;
    roof.hb = hitboxOf(s.car);
    roof.ballRadius = s.ball.valid && s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    roof.gap = cfg_.roofGap;
    switch (mode_) {
    case ShootMode::Flick: fullKind_ = ShootDrill::Flick; break;
    case ShootMode::Power: fullKind_ = ShootDrill::Power; break;
    default: {
        setup_ = setupSel_ >= 1 && setupSel_ < kTargetSetupCount
                     ? static_cast<TargetSetup>(setupSel_)
                     : static_cast<TargetSetup>(rng_.rangeInt(1, kTargetSetupCount - 1));
        switch (setup_) {
        case TargetSetup::Roof: fullKind_ = ShootDrill::Roof; break;
        case TargetSetup::Bouncing: fullKind_ = ShootDrill::Bouncing; break;
        case TargetSetup::Pass: fullKind_ = ShootDrill::Pass; break;
        default: fullKind_ = ShootDrill::Rolling; break;
        }
        break;
    }
    }
    attemptLevel_ = fullLevel();
    if (mode_ == ShootMode::Targets) zone_ = pickTargetZone(zoneSel_, attemptLevel_, zone_, rng_);
    if (mode_ == ShootMode::Flick) flickZoneActive_ = false;  // the whole goal
    fullSetup_ = makeFullSetup(fullKind_, attemptLevel_, pickup_.niveau(), goal_, roof, rng_);
    spawn_ = Spawn{};
    spawn_.goal = goal_;
    spawn_.car = fullSetup_.car;
    spawn_.ball.pos = fullSetup_.ball.pos;
    spawn_.ballReleaseVel = fullSetup_.ball.vel;
    spawn_.ballReleaseAngVel = fullSetup_.ball.angVel;
    spawn_.carReleaseVel = fullSetup_.car.vel;  // pickup at speed: the car drives at GO
    steps_.clear();
    fullRecorded_ = false;
    switch (fullKind_) {
    case ShootDrill::Bouncing: bounceMarks_ = fullSetup_.bounces; break;
    case ShootDrill::Pass:
        showMeet_ = true;
        meet_ = fullSetup_.target;
        break;
    case ShootDrill::Power:
        showRunUp_ = true;
        runUp_ = powerRunUpPoint(fullSetup_.car.pos, fullSetup_.target);
        break;
    default: break;
    }
}

void ShootingModule::beginFull(const Snapshot& s) {
    const float r = s.ball.valid && s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    const ZoneRect zone = mode_ == ShootMode::Targets ? zoneRect(zone_, attemptLevel_) : goalMouthRect();
    fullBegin(fullState_, fullSetup_, shootPlayerLevel(attemptLevel_), goal_, zone, hitboxOf(s.car), r, s.time);
    steps_.setSteps(shoot::fullSteps(fullState_));
    steps_.start(s.time);
    if (fullSetup_.pickup) pickup_.beginAttempt();
    fullRecorded_ = false;
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
    expectJumpUntil_ = frame + 3;
    phase_ = ShootPhase::Live;
    phaseStart_ = frame;
    message_.clear();
    flick_.reset();
    shot_.reset();
    touched_ = playerMoved_ = powerTouched_ = false;
    firstAttempt_ = false;
    ready_.stop();
    // Moving roof start: carry assist up to kRoofAssistMaxLevel.
    assistActive_ = movingStart_ && attemptLevel_ <= kRoofAssistMaxLevel + 1e-4f;
    const float time = static_cast<float>(frame) / kTickRate;
    if (full_) {
        Snapshot s = prev_;
        s.time = time;
        s.ball.radius = s.ball.valid && s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
        beginFull(s);
    }
}

// ---- Flick drill ------------------------------------------------------------------------------------------
void ShootingModule::tickFlick(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (const auto rep = flick_.update(s)) {
        // Parcours complet: a flick before the pickup and the carry are done does not count.
        if (full_ && steps_.running()) {
            const Step* cur = steps_.currentStep();
            if (!cur || cur->id != kStepFlick) {
                steps_.fail(s.time, "shooting.full.early_flick", "Ramasse et porte d'abord la balle, puis flick.");
                return;
            }
        }
        onFlickReport(*rep, ctx, out);
        return;
    }
    if (dribbleAttempt() && tooClose(s)) {
        onTooClose(ctx, out);
        return;
    }
    if (full_) return;  // the steps handle a lost ball and the time
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

bool ShootingModule::dribbleAttempt() const {
    const bool roofDrill = mode_ == ShootMode::Flick || (mode_ == ShootMode::Targets && setup_ == TargetSetup::Roof);
    return roofDrill && (movingStart_ || full_);
}

bool ShootingModule::tooClose(const Snapshot& s) const {
    if (!s.car.valid || !s.ball.valid) return false;
    const FlickPhase ph = flick_.phase();
    if (ph == FlickPhase::Air || ph == FlickPhase::Dodged) return false;  // never cut a flick in progress
    return goal_.distanceToPlane(s.car.pos) < kFlickLateDist && shootBallOnRoof(s);
}

void ShootingModule::onTooClose(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const AttemptResult r = gradeTooClose(
        mode_ == ShootMode::Targets ? std::string("shoot.target.") + zoneId(zone_)
                                    : std::string("shoot.flick.") +
                                          (requested_ >= 0 ? flickTypeId(static_cast<FlickType>(requested_)) : "any"),
        s.time);
    if (full_) {
        // Parcours complet: the step being played fails with the tip (finishFull reports it next tick).
        if (steps_.running()) steps_.fail(s.time, r.tips.front().code, r.tips.front().text);
        return;
    }
    earlyReplays_ = 0;
    if (mode_ == ShootMode::Flick && cfg_.flickAdaptive) flickStair_.record(false);
    if (mode_ == ShootMode::Targets && cfg_.targetAdaptive)
        targetStairs_[static_cast<size_t>(setupIndex(setup_))].record(false);
    lastFlick_ = r.title;
    finishAttempt(r, ctx, out);
}

namespace {
// The verdict follows a success decided outside the grade (parcours complet).
void forceVerdict(AttemptResult& r, bool success) {
    r.success = success;
    if (success && r.verdict < Verdict::Ok) r.verdict = Verdict::Ok;
    if (!success && r.verdict > Verdict::Ok) r.verdict = Verdict::Ok;
    if (!success && r.verdict == Verdict::None) r.verdict = Verdict::Fail;
}

std::string firstTip(const AttemptResult& r, const char* fallback) {
    return r.tips.empty() ? std::string(fallback) : r.tips.front().text;
}
} // namespace

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
    // Niveaux 1 and 2 (and the parcours complet): any flick with contact and on target counts; the requested type
    // counts from niveau 3.
    p.requested = full_ || shootPlayerLevel(attemptLevel_) <= 2 ? -1 : requested_;
    p.level = attemptLevel_;
    p.minPower = flickMinPower(attemptLevel_, movingStart_ || full_);  // a moving ball needs a real shot
    p.requireOnTarget = full_ || cfg_.flickRequireOnTarget;
    p.aimErrorDeg = angleBetweenDeg(flat(rep.launchVel), flat(targetWorld - rep.ballPos));
    p.delayedSec = cfg_.delayedSec;
    p.time = s.time;
    AttemptResult res = gradeFlick(rep, aim, p, ctx.profile);
    if (full_) {
        // "Flick": the flip hit the ball. "Cadre": on goal and strong enough for the level.
        const Step* cur = steps_.currentStep();
        if (cur && cur->id == kStepFlick) {
            if (rep.contact) steps_.complete(s.time);
            else steps_.fail(s.time, "shooting.flick_no_contact", firstTip(res, "Flippe pendant que la balle touche encore le toit."));
        }
        cur = steps_.running() ? steps_.currentStep() : nullptr;
        if (cur && cur->id == kStepCadre) {
            if (rep.contact && aim.onGoal && rep.power >= p.minPower) steps_.complete(s.time);
            else steps_.fail(s.time, "shooting.full.cadre", firstTip(res, "Flick vers le but, plus fort."));
        }
        forceVerdict(res, steps_.isComplete());
        recordFull(res.success);
    } else if (cfg_.flickAdaptive) {
        flickStair_.record(res.success);
    }
    const float acc = metricValue(res, "accuracy", 0.f);
    const auto flags = stats_.addFlick(rep.category, rep.power, acc, res.success);
    lastFlick_ = std::string(flickTypeLabel(rep.category)) + (rep.cls.uncertain ? " (?)" : "");
    if (rep.contact) {
        hasLastShot_ = true;
        lastShotSpeed_ = rep.power;
        shot_.start(s.ball, goal_, s.frame, rep.power); // keeps the impact marker during the result
    }
    if (flags.powerAll)
        toastRecord(out,
                    std::string("Record ") + flickTypeHudLabel(rep.category) + " : " + formatSpeed(rep.power, unit, 1));
    if (flags.accuracyAll) toastRecord(out, "Record de précision en flick : " + formatNumber(acc, 0) + " %");
    finishAttempt(std::move(res), ctx, out);
}

// ---- Target drill -----------------------------------------------------------------------------------------
void ShootingModule::tickTargets(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (setup_ == TargetSetup::Roof) {
        if (const auto rep = flick_.update(s)) {
            if (full_ && steps_.running()) {
                const Step* cur = steps_.currentStep();
                if (!cur || cur->id != kStepFlick) {
                    steps_.fail(s.time, "shooting.full.early_flick", "Ramasse et dribble d'abord la balle, puis flick.");
                    return;
                }
                if (rep->contact) steps_.complete(s.time);
                else steps_.fail(s.time, "shooting.flick_no_contact", "Flippe pendant que la balle touche encore le toit.");
            }
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
        } else if (!lastFlickReport_ && dribbleAttempt() && tooClose(s)) {
            onTooClose(ctx, out);
            return;
        }
    }
    if (shot_.active()) {
        if (const auto o = shot_.update(s)) {
            onTargetOutcome(*o, ctx, out);
            return;
        }
    }
    if (full_) return;  // the steps have their own time limits
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
    if (full_) {
        const Step* cur = steps_.running() ? steps_.currentStep() : nullptr;
        if (cur && cur->id == kStepZone) {
            if (aim.inZone) steps_.complete(s.time);
            else steps_.fail(s.time, "shooting.full.zone", firstTip(res, "Vise la zone en couleur."));
        } else if (cur) {
            steps_.fail(s.time);
        }
        forceVerdict(res, steps_.isComplete());
        recordFull(res.success);
    } else if (cfg_.targetAdaptive) {
        targetStairs_[static_cast<size_t>(setupIndex(setup_))].record(res.success);
    }
    const bool streakRecord = stats_.addTarget(zone_, res.success, res.score);
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
            AttemptResult res = gradePower(powerShot_, powerGoal(), s.time);
            if (full_) {
                const Step* cur = steps_.running() ? steps_.currentStep() : nullptr;
                if (cur && cur->id == kStepPower) {
                    if (powerShot_.speed >= powerGoal()) steps_.complete(s.time);
                    else steps_.fail(s.time, "shooting.full.power", firstTip(res, "Arrive plus vite et flip au dernier moment."));
                }
                forceVerdict(res, steps_.isComplete());
                recordFull(res.success);
            } else if (cfg_.powerAdaptive) {
                powerStair_.record(res.success);
            }
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
    if (full_) return;  // the steps have their own time limits
    bool missed = elapsed(s.frame) > cfg_.attemptTimeout;
    if (s.ball.valid && s.car.valid && elapsed(s.frame) > 1.f) {
        const float behind = goal_.sign * (s.car.pos.y - s.ball.pos.y); // > 0: the ball went past the car
        if (behind > 1500.f) missed = true;
    }
    if (missed) newAttemptLater("Balle manquée : nouvel essai.");
}

// ---- Parcours complet -------------------------------------------------------------------------------------
// The touch steps of the parcours: judged on the touch itself (first touch, shot, volley, one-touch, flip).
void ShootingModule::onFullTouch(const TouchEvent& t, float post) {
    if (!steps_.running()) return;
    ++fullState_.touches;
    const Step* cur = steps_.currentStep();
    if (!cur) return;
    const std::string id = cur->id;
    const float now = t.post.time;
    auto startShot = [&]() { shot_.start(t.post.ball, goal_, t.post.frame, post); };
    auto crossing = [&]() {
        return predictToPlaneY(t.post.ball.pos, t.post.ball.vel, t.post.ball.angVel, goal_.planeY(), 4.f,
                               t.post.ball.radius)
            .has_value();
    };
    switch (fullKind_) {
    case ShootDrill::Rolling:
        if (id == kStepFirst) {
            const float rel = t.pre.car.valid && t.pre.ball.valid ? length(t.pre.ball.vel - t.pre.car.vel) : 0.f;
            if (rel < kFirstTouchMaxRel) steps_.complete(now);
            else steps_.fail(now, "shooting.full.first_hard", "Arrive sur la balle à peine plus vite qu'elle : elle doit rester devant toi.");
        } else if (id == "push") {
            if (post > kShotSpeed)
                steps_.fail(now, "shooting.full.shot_early", "Pousse d'abord la balle vers le but en douceur, puis tire.");
        } else if (id == kStepShot) {
            if (post > kShotSpeed && crossing()) {
                steps_.complete(now);
                startShot();
            }
        }
        break;
    case ShootDrill::Bouncing:
        if (id == kStepVolley) {
            const bool justBounced = fullState_.lastBounceTime >= 0.f && t.pre.time - fullState_.lastBounceTime <= kVolleyWindow;
            if (justBounced && post > kShotSpeed) {
                steps_.complete(now);
                startShot();
            } else {
                steps_.fail(now, "shooting.full.volley", justBounced ? "Frappe plus fort, en accélérant." : cur->tip);
            }
        } else {
            steps_.fail(now, "shooting.full.early_touch", "Arrive au deuxième rebond avant de toucher la balle.");
        }
        break;
    case ShootDrill::Pass:
        if (id == kStepOneTouch) {
            if (post > kShotSpeed && goal_.sign * t.post.ball.vel.y > 0.f) {
                steps_.complete(now);
                startShot();
            } else {
                steps_.fail(now, "shooting.full.onetouch", cur->tip);
            }
        } else {
            steps_.fail(now, "shooting.full.early_touch", "Coupe la trajectoire du centre avant de le toucher.");
        }
        break;
    case ShootDrill::Power:
        if (id == kStepDodge) {
            const int64_t since = t.pre.frame - lastDodgeFrame_;
            const bool recent = t.pre.car.dodging || (since >= 0 && since <= 30);
            const bool forward = !t.pre.car.dodging || length(t.pre.car.dodgeDir) < 0.1f || t.pre.car.dodgeDir.x > 0.3f;
            if (recent && forward) steps_.complete(now);
            else steps_.fail(now, "shooting.full.dodge", cur->tip);
        } else if (id != kStepPower) {
            steps_.fail(now, "shooting.full.early_touch", cur->tip);
        }
        break;
    default: break;
    }
}

void ShootingModule::recordFull(bool success) {
    if (fullRecorded_) return;
    fullRecorded_ = true;
    fullStairs_[static_cast<size_t>(fullKind_)].record(success);
}

// The parcours ended on a step without a shot to grade (pickup, carry, a check, a time limit): the missing step and its
// tip, the right staircase, no result for the coach.
void ShootingModule::finishFull(const TickContext& ctx, Actions& out) {
    if (phase_ != ShootPhase::Live || !steps_.finished()) return;
    earlyReplays_ = 0;
    const bool success = steps_.isComplete();
    const auto& f = steps_.failure();
    // A failed pickup (or carry) feeds the pickup's own staircase (pickup::Progress), never the mechanic's.
    const bool pickupPart = !success && (f.group == pickup::kGroup || f.id == "carry" || f.id == "dribble");
    if (!pickupPart) recordFull(success);
    AttemptResult r;
    r.module = "shooting";
    r.time = ctx.snap.time;
    r.skill = std::string("shoot.full.") + shootDrillId(fullKind_);
    r.title = std::string("Parcours complet : ") + shootDrillTitle(fullKind_);
    r.success = success;
    r.score = success ? 100.f : 0.f;
    r.verdict = success ? Verdict::Good : Verdict::Fail;
    if (!success) r.tips.push_back(steps_.missedTip(10.f));
    lastResult_ = r;
    hasLastResult_ = true;
    ++attempts_;
    message_ = success ? std::string() : steps_.missingText();
    phase_ = ShootPhase::Result;
    phaseStart_ = ctx.snap.frame;
    out.requestSave = true;
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
        if (full_) {
            if (setup_ != TargetSetup::Roof) onFullTouch(t, post);  // the shot is started by its step
            break;
        }
        if (!stillOnRoof) shot_.start(t.post.ball, goal_, t.post.frame, post);
        break;
    }
    case ShootMode::Power: {
        if (full_) onFullTouch(t, post);
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
    earlyReplays_ = 0;  // a graded attempt ends the run of early replays
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
    powerStair_.save(store, "shooting.stair.power");
    pickup_.save(store, "shooting.stair.pickup");
    for (int i = 0; i < kShootDrillCount; ++i)
        fullStairs_[static_cast<size_t>(i)].save(store, std::string("shooting.full.") + shootDrillId(static_cast<ShootDrill>(i)));
    store.setInt("shooting.defaults_v2", defaultsV2_ ? 1 : 0);
}

void ShootingModule::load(const Store& store) {
    stats_.load(store);
    // Levels are saved even for drills never played: only a played level is kept, a new player starts at niveau 1.
    auto loadStair = [&store](Staircase& st, const std::string& key) {
        if (store.getInt(key + ".attempts", 0) > 0) st.load(store, key);
    };
    loadStair(flickStair_, "shooting.stair.flick");
    for (int i = 1; i < kTargetSetupCount; ++i)
        loadStair(targetStairs_[static_cast<size_t>(i)],
                  std::string("shooting.stair.") + setupId(static_cast<TargetSetup>(i)));
    loadStair(powerStair_, "shooting.stair.power");
    loadStair(pickup_.staircase(), "shooting.stair.pickup");
    for (int i = 0; i < kShootDrillCount; ++i)
        loadStair(fullStairs_[static_cast<size_t>(i)], std::string("shooting.full.") + shootDrillId(static_cast<ShootDrill>(i)));
    defaultsV2_ = store.getInt("shooting.defaults_v2", 0) != 0;
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

// Compact HUD: at most 3 main lines (what happens now or the result, the one tip, how to do it or what comes next);
// targets, records, statistics, the predicted impact and the level are `detail` lines.
DrawPanel ShootingModule::buildPanel(const ViewContext& view, const DrawContext& ctx) const {
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
    const KeyStyle style = keyStyleFromIndex(ctx.settings.profile.buttonStyle);
    const int64_t frame = ctx.hasSnapshot ? ctx.snap.frame : prev_.frame;
    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = std::max(0.3f, ctx.hudScale * cfg_.hudScale);
    p.minWidth = 230.f * p.scale;
    const bool adaptive = mode_ == ShootMode::Flick || mode_ == ShootMode::Targets ||
                          (mode_ == ShootMode::Power && (cfg_.powerAdaptive || full_));
    float level = mode_ == ShootMode::Flick ? flickLevel() : (mode_ == ShootMode::Power ? powerStair_.level() : targetLevel(setup_));
    if (full_) level = fullLevel();
    p.title = "Tirs & flicks";
    if (full_) p.title += std::string(" : parcours complet, ") + shootDrillTitle(fullKind_);
    else if (mode_ != ShootMode::None) p.title += std::string(" : ") + shootModeLabel(mode_);
    if (adaptive) p.title += ", niveau " + std::to_string(shootPlayerLevel(level)) + "/10";
    auto line = [&p](const std::string& text, Color c = colors::White, float bar = -1.f, Color barColor = colors::Green,
                     float scale = 1.f, bool detail = false) {
        PanelLine l;
        l.text = text;
        l.color = c;
        l.bar = bar;
        l.barColor = barColor;
        l.scale = scale;
        l.detail = detail;
        p.lines.push_back(std::move(l));
    };
    auto detail = [&line](const std::string& text, Color c = kDetailColor, float bar = -1.f,
                          Color barColor = colors::Green) { line(text, c, bar, barColor, 0.9f, true); };

    const std::string tip = hasLastResult_ && !lastResult_.tips.empty() ? lastResult_.tips.front().text : "";
    // After a miss its tip stays as a reminder until the next result.
    const bool reminder = !tip.empty() && !lastResult_.success;
    const std::string goalSpeed = formatSpeed(powerGoal(), unit, 0);
    // How to do the attempt, in words (flick with the player's buttons, zone to aim at, speed to reach).
    std::string how;
    if (mode_ == ShootMode::Flick)
        how = requested_ >= 0 ? flickCue(static_cast<FlickType>(requested_), ctx.profile, style, cfg_.delayedSec)
                              : "N'importe quel flick vers le but";
    else if (mode_ == ShootMode::Targets)
        how = "Vise " + zoneWithArticle(zone_);
    else if (mode_ == ShootMode::Power)
        how = "Objectif : " + goalSpeed;

    const float now = ctx.hasSnapshot ? ctx.snap.time : prev_.time;
    const bool roofBall = mode_ == ShootMode::Flick || (mode_ == ShootMode::Targets && setup_ == TargetSetup::Roof);
    switch (phase_) {
    case ShootPhase::Setup:
    case ShootPhase::Countdown: {
        // What comes (a moving start is announced), the get-ready line, the reminder or how to do it.
        std::string first = message_.empty() ? "Prépare-toi..." : message_;
        if (movingStart_) first = shootPanelLine(ShootDrill::Roof, 0);
        else if (full_) first = shoot::fullLabels(fullKind_, shootPlayerLevel(level)).front();
        line(first, colors::Yellow);
        const std::string ready = phase_ == ShootPhase::Countdown ? ready_.hudLine(now) : std::string();
        if (!ready.empty()) line(ready, colors::White);
        if (reminder) line(tip, kTipColor);
        else if (roofBall && (movingStart_ || full_)) line(shootBallCamHint(), colors::Cyan);  // the goal stays in view
        else if (!how.empty()) line(how, colors::Cyan);
        break;
    }
    case ShootPhase::Live:
        if (full_) {
            // Validated steps turn green, the current one is highlighted (shared step tracker HUD).
            appendStepLines(p.lines, steps_, now);
            int mains = 0;
            for (const auto& l : p.lines) mains += l.detail ? 0 : 1;
            if (reminder && mains < 3) line(tip, kTipColor);
            break;
        }
        if (roofBall && flick_.phase() == FlickPhase::Carry) {
            // "Balle stable": settled 0.3 s on the roof, the flick can start.
            const bool stable = flick_.settledTime(frame) >= 0.3f;
            const bool named = mode_ == ShootMode::Flick && requested_ >= 0 && shootPlayerLevel(attemptLevel_) > 2;
            if (stable)
                line(named ? std::string("Balle stable : ") + flickTypeHudLabel(static_cast<FlickType>(requested_)) + " !"
                           : std::string("Balle stable : saute !"),
                     colors::Green);
            else if (movingStart_ || full_)
                line(shootPanelLine(ShootDrill::Flick, 0), colors::White);  // "Garde ta vitesse..."
            else
                line("Attends « Balle stable »", colors::White);  // stopped car
        } else if (roofBall && flick_.phase() == FlickPhase::Air) {
            line(shootPanelLine(ShootDrill::Flick, 2), colors::White);
        } else if (mode_ == ShootMode::Flick) {
            line(requested_ >= 0
                     ? std::string("À toi : ") + flickTypeHudLabel(static_cast<FlickType>(requested_)) + " vers le but !"
                     : "À toi : flick vers le but !");
        } else if (mode_ == ShootMode::Targets && setup_ == TargetSetup::Bouncing && lastFloorBounce_ >= 0.f &&
                   now - lastFloorBounce_ < 0.4f) {
            line(shootPanelLine(ShootDrill::Bouncing, 2), colors::Green);  // "tire quand elle remonte" after a bounce
        } else if (mode_ == ShootMode::Targets) {
            line("À toi : vise " + zoneWithArticle(zone_) + " !");
        } else {
            line("À toi : frappe le plus fort possible !");
        }
        if (reminder) line(tip, kTipColor);
        if (mode_ != ShootMode::Targets) line(how, colors::Cyan);
        else if (setup_ == TargetSetup::Roof && !reminder) line("Vise " + zoneWithArticle(zone_), colors::Cyan);
        break;
    case ShootPhase::Result:
        if (hasLastResult_) {
            std::string head = lastResult_.title + " : ";
            if (mode_ == ShootMode::Power) head += formatSpeed(lastShotSpeed_, unit, 0) + ", ";
            head += std::string(verdictLabel(lastResult_.verdict)) + " (" + formatNumber(lastResult_.score, 0) + "/100)";
            line(head, verdictColor(lastResult_.verdict));
            if (!tip.empty()) line(tip, kTipColor);
        }
        if (cfg_.autoRepeat) {
            const float left = cfg_.resultHold - elapsed(frame);
            line("Essai suivant dans " + std::to_string(static_cast<int>(std::ceil(std::max(0.f, left) - 1e-3f))) +
                 " s");
        }
        break;
    default:
        // Outside drills (panel "always"): the last shot and where it is going.
        if (!message_.empty()) line(message_);
        if (hasLastShot_) line("Dernier tir : " + formatSpeed(lastShotSpeed_, unit, 1));
        break;
    }

    // Detail lines (full HUD only).
    switch (mode_) {
    case ShootMode::Flick: {
        detail(requested_ >= 0 ? std::string("Demandé : ") + flickTypeLabel(static_cast<FlickType>(requested_))
                               : "Flick libre (tous types)");
        detail(std::string("Cible : ") + (flickZoneActive_ ? zoneLabel(flickZone_) : "but entier"), cfg_.targetColor);
        if (phase_ == ShootPhase::Live && flick_.phase() == FlickPhase::Carry) {
            const float settled = flick_.settledTime(frame);
            detail("Balle sur le toit : " + formatNumber(settled, 2) + " s",
                   settled >= 0.3f ? colors::Green : colors::Yellow);
        }
        if (!lastFlick_.empty()) detail("Dernier flick : " + lastFlick_);
        if (requested_ >= 0) {
            const float best = stats_.flickAll[static_cast<size_t>(requested_)].bestPower;
            if (best > 0.f)
                detail(std::string("Record ") + flickTypeLabel(static_cast<FlickType>(requested_)) + " : " +
                       formatSpeed(best, unit, 1));
        } else {
            float best = 0.f;
            for (const auto& r : stats_.flickAll) best = std::max(best, r.bestPower);
            if (best > 0.f) detail("Record tous flicks : " + formatSpeed(best, unit, 1));
        }
        break;
    }
    case ShootMode::Targets: {
        detail(std::string("Cible : ") + zoneLabel(zone_) + " (" + setupLabel(setup_) + ")", cfg_.targetColor);
        const ZoneStats& z = stats_.zoneSession[static_cast<size_t>(zone_)];
        if (z.attempts > 0)
            detail(capitalized(zoneLabel(zone_)) + " : " + std::to_string(z.hits) + "/" + std::to_string(z.attempts) +
                   " (" + formatPercent(z.hitRate()) + ")");
        detail("Série : " + std::to_string(stats_.streak) + " (record " + std::to_string(stats_.bestStreakAll) + ")");
        break;
    }
    case ShootMode::Power:
        if (phase_ == ShootPhase::Result) detail("Objectif : " + goalSpeed);
        if (stats_.powerAll.best > 0.f)
            detail("Record : " + formatSpeed(stats_.powerAll.best, unit, 1) + " (session " +
                   formatSpeed(stats_.powerSession.best, unit, 1) + ")");
        break;
    default:
        if (stats_.powerAll.best > 0.f) detail("Record de puissance : " + formatSpeed(stats_.powerAll.best, unit, 1));
        break;
    }

    // Early feedback while the ball travels: where it is predicted to cross the goal line (also drawn in 3D). Main
    // line outside drills, where it is the whole analysis.
    if (shot_.hasPrediction() && (phase_ == ShootPhase::Live || mode_ == ShootMode::None) &&
        predictionShown(mode_ != ShootMode::None, ctx.game)) {
        const Vec2 pp = shot_.predicted();
        const bool main = mode_ == ShootMode::None;
        auto predicted = [&](const std::string& text, Color c) {
            if (main) line(text, c);
            else detail(text, c);
        };
        if (mode_ == ShootMode::Targets && zoneRect(zone_, attemptLevel_).contains(pp))
            predicted("Impact prévu : dans la cible", colors::Green);
        else if (shoot::onGoal(pp))
            predicted(mode_ == ShootMode::Targets ? "Impact prévu : cadré, hors cible" : "Impact prévu : cadré",
                      colors::Yellow);
        else
            predicted("Impact prévu : hors cadre", colors::Red);
    }
    if (hasLastShot_ && mode_ != ShootMode::None && mode_ != ShootMode::Power)
        detail("Dernier tir : " + formatSpeed(lastShotSpeed_, unit, 1));
    if (hasLastResult_ && mode_ != ShootMode::Power && mode_ != ShootMode::None) {
        const float acc = metricValue(lastResult_, "accuracy", -1.f);
        if (acc >= 0.f) detail("Précision : " + formatNumber(acc, 0) + " %");
    }
    if (adaptive)
        detail("Niveau " + std::to_string(shootPlayerLevel(level)) + "/10", kDetailColor, level, colors::Cyan);
    return p;
}

bool ShootingModule::predictionShown(bool drill, const GameContext& game) const {
    if (drill) return cfg_.showPrediction;
    return cfg_.freeAnalysis || (game.category == MapCategory::Shooting && cfg_.showPrediction);
}

void ShootingModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled) return;
    Occluder occ; // goal lines behind the ball are hidden by it
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
    }
    if (ctx.game.mode == GameMode::Replay) {
        // No physics ticks in replays: predict from the visual ball state only.
        if (predictionShown(false, ctx.game) && view.valid && view.ballValid && std::fabs(view.ballVel.y) > 500.f) {
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
        // Predicted impact (a prediction: off by default); the point where the ball really crossed is drawn below.
        if (shot_.hasPrediction() && predictionShown(drill, ctx.game)) {
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
        // Circles on the floor until the first touch: next bounces, meeting point, run-up point (predictions: only
        // with "Cercles au sol").
        const bool beforeTouch = (phase_ == ShootPhase::Countdown || phase_ == ShootPhase::Live) && !touched_ &&
                                 !powerTouched_ && (!full_ || fullState_.touches == 0);
        if (drill && beforeTouch && cfg_.floorMarks) {
            const Vec3 up{0.f, 0.f, 1.f};
            const float t = phase_ == ShootPhase::Live ? elapsed(ctx.snap.frame) : 0.f;
            int shown = 0;
            for (const auto& b : bounceMarks_) {
                if (b.time < t || shown >= 2) continue;
                out.circle3D(Vec3{b.pos.x, b.pos.y, 6.f}, up, 110.f, shown == 0 ? cfg_.targetColor : colors::Cyan, 3.f, 28);
                ++shown;
            }
            if (showMeet_) out.circle3D(Vec3{meet_.x, meet_.y, 6.f}, up, 150.f, colors::Green, 3.f, 28);
            if (showRunUp_) out.circle3D(Vec3{runUp_.x, runUp_.y, 6.f}, up, 160.f, colors::Cyan, 3.f, 28);
        }
        if (drill && cfg_.roofGuide && phase_ != ShootPhase::Result &&
            (mode_ == ShootMode::Flick || (mode_ == ShootMode::Targets && setup_ == TargetSetup::Roof)))
            drawRoofGuide(view, ctx, out);
    }
    if (cfg_.hudMode == 2 || (cfg_.hudMode == 0 && !drill)) return;
    out.panel(buildPanel(view, ctx));
}

} // namespace rlt
