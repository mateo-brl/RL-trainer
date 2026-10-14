#include "core/modules/mechanics/MechanicsModule.h"

#include <algorithm>
#include <cmath>

#include "core/common/Text.h"
#include "core/modules/mechanics/Tips.h"

namespace rlt {

using mech::Focus;
using mech::Mech;
using mech::MechGrade;

namespace {

std::string focusTitle(Focus f) { return f == Focus::None ? std::string() : std::string(mech::focusInfo(f).title); }

std::string upperCode(const std::string& tipCode) {
    std::string s = tipCode.rfind("me.", 0) == 0 ? tipCode.substr(3) : tipCode;
    for (char& c : s) c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    return s;
}

std::string seconds1(float s) {
    std::string t = formatNumber(std::max(0.f, s), 1);
    for (char& c : t)
        if (c == '.') c = ',';
    return t + " s";
}

std::string dashCount(int n) { return std::to_string(n) + (n > 1 ? " dashes" : " dash"); }

} // namespace

// ---- Settings ------------------------------------------------------------------------------------------
void MechanicsModule::describeSettings(SettingsSchema& schema) {
    auto g = schema.section(&Settings::mechanics, "mechanics", "Mécas mouvement");
    g.toggle("rlt_me_enabled", &MechanicsConfig::enabled, true, "Activer",
             "Détecte et note automatiquement tes mécas de déplacement (freeplay, matchs hors ligne, maps workshop).");
    g.toggle("rlt_me_chat", &MechanicsConfig::chatSummary, false, "Résumé dans le chat",
             "Écrit une ligne dans le chat après chaque méca notée (score et conseil principal).");
    g.toggle("rlt_me_toast_records", &MechanicsConfig::toastRecords, true, "Notifier les records",
             "Affiche une notification quand tu bats ton record sur une méca.");
    g.integer("rlt_me_min_score", &MechanicsConfig::minScore, 0, 0, 100, "Score minimum affiché",
              "Les essais sous ce score ne remplacent pas le panneau ni le chat. Ils restent comptés dans tes stats.");

    auto h = schema.section(&Settings::mechanics, "mechanics", "Panneau « Dernière méca »");
    h.toggle("rlt_me_hud", &MechanicsConfig::hud, true, "Afficher le panneau",
             "Panneau à gauche de l'écran : dernière méca, score et conseil. Pendant un exercice : l'action à faire.");
    h.number("rlt_me_hud_x", &MechanicsConfig::hudX, 0.01f, 0.f, 1.f, "Position horizontale",
             "Position du bord gauche du panneau, en fraction de la largeur de l'écran.", 2);
    h.number("rlt_me_hud_y", &MechanicsConfig::hudY, 0.40f, 0.f, 1.f, "Position verticale",
             "Position du haut du panneau, en fraction de la hauteur de l'écran.", 2);
    h.number("rlt_me_hud_scale", &MechanicsConfig::hudScale, 1.f, 0.5f, 2.f, "Taille du panneau",
             "Taille du texte du panneau (multipliée par la taille générale de l'interface).", 2);
    h.number("rlt_me_hud_seconds", &MechanicsConfig::hudSeconds, 8.f, 0.f, 30.f, "Durée d'affichage (s)",
             "Temps pendant lequel le résultat reste affiché. 0 = toujours. En focus ou en exercice, il reste affiché.", 0);
    h.integer("rlt_me_hud_metrics", &MechanicsConfig::hudMetrics, 3, 0, 5, "Mesures affichées",
              "Nombre de mesures « mesuré / idéal » montrées dans le panneau quand le HUD compact est désactivé "
              "(réglages généraux). Elles restent toujours dans la page Mécas mouvement.");
    h.toggle("rlt_me_hud_counters", &MechanicsConfig::hudCounters, false, "Compteurs de session",
             "Ajoute une ligne avec le nombre de mécas réussies pendant la session (HUD compact désactivé).");

    auto sol = schema.section(&Settings::mechanics, "mechanics", "Sol");
    sol.toggle("rlt_me_wavedash", &MechanicsConfig::wavedash, true, "Wavedash", "Note les wavedashes (avant, côté, arrière).");
    sol.toggle("rlt_me_chaindash", &MechanicsConfig::chainDash, true, "Chain wavedash",
               "Note les chaînes de wavedashes (infinite wavedash) : rythme et vitesse atteinte.");
    sol.toggle("rlt_me_zapdash", &MechanicsConfig::zapDash, true, "Zap dash", "Note les zap dashes et les speed zaps.");
    sol.toggle("rlt_me_powerslide", &MechanicsConfig::powerslide, true, "Virages en powerslide",
               "Note les virages au powerslide : vitesse conservée et glisse en sortie.");

    auto flips = schema.section(&Settings::mechanics, "mechanics", "Flips");
    flips.toggle("rlt_me_speedflip", &MechanicsConfig::speedflip, true, "Speedflip",
                 "Note les speedflips, y compris au kickoff (temps jusqu'à la balle).");
    flips.toggle("rlt_me_halfflip", &MechanicsConfig::halfFlip, true, "Half-flip", "Note les half-flips (et half-flip + wavedash).");
    flips.toggle("rlt_me_flipcancel", &MechanicsConfig::flipCancel, true, "Flip cancel",
                 "Note les flips annulés qui ne sont ni des speedflips ni des half-flips.");

    auto air = schema.section(&Settings::mechanics, "mechanics", "Air");
    air.toggle("rlt_me_fastaerial", &MechanicsConfig::fastAerial, true, "Fast aerial",
               "Note les fast aerials et les double jump aerials (timing des sauts, boost, stick).");
    air.toggle("rlt_me_stall", &MechanicsConfig::stall, true, "Stall", "Note les stalls (tornado stall).");
    air.toggle("rlt_me_helicopter", &MechanicsConfig::helicopter, true, "Hélico / tornado",
               "Note les rotations en l'air (hélicoptère, tornado spin).");
    air.toggle("rlt_me_recovery", &MechanicsConfig::recovery, true, "Réceptions",
               "Note tes réceptions après un aérien (roues, alignement, vitesse gardée, wavedash).");

    auto mur = schema.section(&Settings::mechanics, "mechanics", "Mur");
    mur.toggle("rlt_me_walldash", &MechanicsConfig::wallDash, true, "Wall dash", "Note les wall dashes (et ceiling dashes).");

    auto boost = schema.section(&Settings::mechanics, "mechanics", "Boost");
    boost.toggle("rlt_me_boost", &MechanicsConfig::boost, true, "Gestion du boost",
                 "Analyse ta gestion du boost au supersonique (boost gaspillé, accélérateur relâché).");

    auto drill = schema.section(&Settings::mechanics, "mechanics", "Focus et exercices");
    drill.number("rlt_me_slowmo", &MechanicsConfig::slowMotion, 1.f, 0.25f, 1.f, "Ralenti",
                 "Vitesse du jeu pendant le focus et les exercices (1 = normal). Remise à 1 à l'arrêt.", 2);
    drill.toggle("rlt_me_drill_autoreset", &MechanicsConfig::drillAutoReset, true, "Relance automatique",
                 "Replace la voiture automatiquement après chaque essai.");
    drill.number("rlt_me_drill_delay", &MechanicsConfig::drillDelay, 1.5f, 0.5f, 5.f, "Pause entre les essais (s)",
                 "Temps pour lire le résultat avant la relance automatique.", 1);
    drill.toggle("rlt_me_drill_adaptive", &MechanicsConfig::drillAdaptive, true, "Difficulté adaptative",
                 "Ajuste la difficulté de chaque exercice pour que tu réussisses environ 3 essais sur 4.");
    drill.toggle("rlt_me_guides", &MechanicsConfig::showGuides, true, "Repères 3D",
                 "Dessine les repères des exercices (couloir, cible, bande sur le mur).");

    auto exp = schema.section(&Settings::mechanics, "mechanics", "Réglages experts");
    exp.advanced();
    exp.number("rlt_me_sf_angle", &MechanicsConfig::speedflipAngle, 30.f, 15.f, 60.f, "Angle idéal du speedflip (°)",
               "Angle du flip par rapport au nez de la voiture. 30° par défaut (les bots de référence utilisent 26 à 45°).", 0);
    exp.integer("rlt_me_sf_cancel_ms", &MechanicsConfig::speedflipCancelMs, 50, 25, 120, "Annulation speedflip idéale (ms)",
                "Délai maximum entre le flip et le stick tiré pour une annulation parfaite.");
    exp.integer("rlt_me_chain_gap_ms", &MechanicsConfig::chainGapMs, 200, 100, 400, "Pause max d'une chaîne (ms)",
                "Temps au sol maximum entre deux wavedashes pour que la chaîne continue.");
    exp.number("rlt_me_recovery_height", &MechanicsConfig::recoveryMinHeight, 200.f, 100.f, 600.f,
               "Hauteur min d'une réception (uu)", "Les sauts plus bas ne sont pas notés comme des réceptions.", 0);
    exp.integer("rlt_me_boost_window", &MechanicsConfig::boostWindowSeconds, 20, 5, 60, "Fenêtre d'analyse du boost (s)",
                "Durée de chaque bilan de gestion du boost.");
}

void MechanicsModule::onSettingsChanged(const Settings& settings) {
    const float oldSlow = cfg_.slowMotion;
    cfg_ = settings.mechanics;
    keyStyle_ = keyStyleFromIndex(settings.profile.buttonStyle);
    cfg_.slowMotion = clampf(cfg_.slowMotion, 0.25f, 1.f);
    // Slow motion turned on (or changed) while a focus runs without the activity: ask for it on the next tick, or
    // once another module's exercise ends. Only on a change of this setting, never as a standing condition (that
    // would take the activity back after the stop button).
    if (std::fabs(cfg_.slowMotion - oldSlow) > 1e-4f && cfg_.slowMotion < 0.999f && focus_ != Focus::None &&
        !drill_.active && !focusSpeedOwned_)
        focusSpeedWanted_ = true;
    applyDetectConfig();
}

void MechanicsModule::onProfileChanged(const PlayerProfile& profile) {
    profile_ = profile;
    const float dz = profile.dodgeDeadzone;
    deadzone_ = (std::isfinite(dz) && dz > 0.01f && dz <= 1.5f) ? dz : mech::k::kDodgeDeadzoneDefault;
    applyDetectConfig();
}

void MechanicsModule::applyDetectConfig() {
    mech::DetectConfig& d = coach_.cfg;
    d.speedflipTargetAngleDeg = cfg_.speedflipAngle;
    d.speedflipCancelGreenTicks = std::max(1, static_cast<int>(std::lround(static_cast<float>(cfg_.speedflipCancelMs) * 0.12f)));
    d.speedflipCancelRedTicks = d.speedflipCancelGreenTicks + 8;
    d.chainMaxGapTicks = std::max(6, static_cast<int>(std::lround(static_cast<float>(cfg_.chainGapMs) * 0.12f)));
    d.recoveryMinHeight = cfg_.recoveryMinHeight;
    d.speedWindowTicks = (drill_.active && setup_.boostWindow) ? 1000000 : std::max(5, cfg_.boostWindowSeconds) * 120;
    d.dodgeDeadzone = deadzone_;
    d.focus = focus_;
    d.recoveryEnabled = cfg_.recovery || trained(Focus::Recovery);
}

bool MechanicsModule::familyEnabled(Focus f) const {
    switch (f) {
    case Focus::SpeedFlip: return cfg_.speedflip;
    case Focus::HalfFlip: return cfg_.halfFlip;
    case Focus::FlipCancel: return cfg_.flipCancel;
    case Focus::Wavedash: return cfg_.wavedash;
    case Focus::ChainDash: return cfg_.chainDash;
    case Focus::ZapDash: return cfg_.zapDash;
    case Focus::WallDash: return cfg_.wallDash;
    case Focus::FastAerial: return cfg_.fastAerial;
    case Focus::Stall: return cfg_.stall;
    case Focus::Helicopter: return cfg_.helicopter;
    case Focus::Recovery: return cfg_.recovery;
    case Focus::Powerslide: return cfg_.powerslide;
    case Focus::Boost: return cfg_.boost;
    default: return false;
    }
}

bool MechanicsModule::categoryAllows(Mech m, MapCategory c) {
    // Ring, dribble and obstacle maps have their own goals: boost economy and powerslide cuts mean nothing there.
    if (m == Mech::BoostEfficiency || m == Mech::Powerslide)
        return c == MapCategory::Standard || c == MapCategory::Shooting || c == MapCategory::Other;
    return true;
}

float MechanicsModule::level(Focus f) const {
    const int i = mech::index(f);
    return (i >= 0 && i < mech::kFocusCount) ? levels_[static_cast<size_t>(i)].level() : 0.f;
}

void MechanicsModule::toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const {
    ToastMessage t;
    t.title = title;
    t.text = text;
    t.kind = kind;
    out.toasts.push_back(std::move(t));
}

// ---- Tick ----------------------------------------------------------------------------------------------
void MechanicsModule::onTick(const TickContext& ctx, Actions& out) {
    worldControl_ = ctx.worldControlAllowed;
    if (pendingRelease_) {
        pendingRelease_ = false;
        if (!drill_.active && !focusSpeedOwned_ && ctx.ownsActivity) releaseActivity();
    }
    if (!cfg_.enabled) {
        focusSpeedWanted_ = false;
        restoreWanted_ = false;
        if (drill_.active || focusSpeedOwned_) stopWorld(ctx, out);
        return;
    }
    // Wishes that wait for another module's exercise to end (never stolen from it).
    if (focusSpeedWanted_) takeFocusSpeed(ctx, out);
    if (restoreWanted_ && !ctx.otherActivityRunning) restoreSpeed(ctx, out);
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    nowTime_ = s.time;
    if (s.paused) return;
    if (!s.car.valid) {
        coach_.reset();
        hasPrevSnap_ = false;
        return;
    }
    if (drill_.active || focusSpeedOwned_) runWorld(ctx, out);

    // While the drill holds its start pose the detectors see nothing (the player may press anything, the car is held).
    const bool holding = drill_.active && (drill_.phase == Phase::Setup || drill_.phase == Phase::Countdown);
    const bool fullRunning = drill_.active && drill_.full && drill_.phase == Phase::Running && live_;
    if (holding) {
        coach_.reset();
    } else if (resetTicks_ > 0) {
        --resetTicks_;
        coach_.reset();
    } else {
        grades_.clear();
        coach_.step(s, grades_);
        if (drill_.active && drill_.phase == Phase::Running) trackCues(s);
        if (fullRunning) {
            live_->newGrades = live_->grades.size();
            for (const MechGrade& g : grades_) live_->grades.push_back(g);
        }
        for (MechGrade& g : grades_) handleGrade(g, ctx, out);
    }
    if (drill_.active) afterDetection(ctx, out);
    prevSnap_ = s;
    hasPrevSnap_ = true;
}

void MechanicsModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) return;
    coach_.onBallTouch(touch.pre.time);
    if (!drill_.active || drill_.phase != Phase::Running) return;
    if (drill_.touchTime < 0.f) {
        drill_.touchTime = touch.pre.time;
        drill_.touchBallZ = touch.pre.ball.pos.z;
        if (live_) {
            live_->touchTime = touch.pre.time;
            live_->touchBallZ = touch.pre.ball.pos.z;
        }
    }
    if (held_ && heldFor_ == HeldFor::Touch) {
        MechGrade g = std::move(*held_);
        held_.reset();
        heldFor_ = HeldFor::None;
        g.add("timeToBall", std::max(0.f, drill_.touchTime - drill_.startTime));
        g.add("ballZAtTouch", drill_.touchBallZ);
        emitAttempt(g, ctx, out, true);
    }
}

void MechanicsModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    switch (ev.kind) {
    case GameEventKind::ContextChanged: {
        const std::string key = std::to_string(static_cast<int>(ctx.game.mode)) + "|" + ctx.game.mapName;
        if (key != mapKey_) {
            mapKey_ = key;
            coach_.reset();
            held_.reset();
            focusSpeedWanted_ = false;  // a wish made on the previous map does not take the activity on this one
            restoreWanted_ = false;
            if (drill_.active || focusSpeedOwned_) stopWorld(ctx, out);
        } else if (!ctx.worldControlAllowed && (drill_.active || focusSpeedOwned_)) {
            stopWorld(ctx, out);
        }
        break;
    }
    case GameEventKind::ResetShot:
    case GameEventKind::CarSpawned:
        // A kickoff speedflip still waiting for the ball (missed, then reset at once) is graded, not dropped.
        if (cfg_.enabled) flushBeforeReset(ctx, out);
        coach_.reset();
        if (drill_.active) {
            if (drill_.phase == Phase::Showing) drill_.attempt++;  // manual reset after a result: next variant
            drill_.phase = Phase::Setup;                           // placed again on the next tick
            drill_.nextStart = Start::Manual;
        }
        break;
    case GameEventKind::MapUnloaded:
        coach_.reset();
        held_.reset();
        focusSpeedWanted_ = false;
        restoreWanted_ = false;
        break;
    default: break;
    }
}

// ---- Commands ------------------------------------------------------------------------------------------
void MechanicsModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    const std::string arg = cmd.args.empty() ? std::string() : cmd.args[0];
    const std::string arg2 = cmd.args.size() > 1 ? cmd.args[1] : std::string();
    if (cmd.name == "stop") {
        const bool wasRunning = drill_.active || focus_ != Focus::None;
        stopWorld(ctx, out);
        restoreWanted_ = false;
        focus_ = Focus::None;
        applyDetectConfig();
        if (wasRunning) toast(out, "Mécas mouvement", "Focus et exercice arrêtés.", ToastKind::Info);
        return;
    }
    if (cmd.name == "reset_stats") {
        const Focus f = arg.empty() ? Focus::None : mech::focusFromId(arg);
        if (!arg.empty() && f == Focus::None) {
            toast(out, "Mécas mouvement", "Méca inconnue : " + arg + ". Choix : " + mech::focusIdList() + ".", ToastKind::Warning);
            return;
        }
        if (f == Focus::None) {
            stats_.reset();
            levels_ = freshLevels();
            fullLevels_ = freshLevels();
        } else {
            stats_.reset(f);
            levels_[static_cast<size_t>(mech::index(f))] = freshLevel();
            fullLevels_[static_cast<size_t>(mech::index(f))] = freshLevel();
        }
        out.requestSave = true;
        toast(out, "Mécas mouvement", f == Focus::None ? "Toutes les stats sont remises à zéro." : "Stats remises à zéro : " + focusTitle(f) + ".",
              ToastKind::Info);
        return;
    }
    if (cmd.name == "reset_level") {
        // "Revenir au niveau 1": the exercise (or its "parcours complet") starts again from the first level.
        const Focus f = mech::focusFromId(arg);
        if (f == Focus::None) {
            toast(out, "Mécas mouvement", "Méca inconnue : " + arg + ". Choix : " + mech::focusIdList() + ".", ToastKind::Warning);
            return;
        }
        // "full": the "parcours complet" only; "all": both; nothing: the exercise only.
        const bool full = arg2 == "full", all = arg2 == "all";
        if (!full || all) staircase(f, false) = freshLevel();
        if (full || all) staircase(f, true) = freshLevel();
        out.requestSave = true;
        toast(out, "Mécas mouvement",
              std::string(all ? "Exercice et parcours complet " : (full ? "Parcours complet " : "Exercice ")) + focusTitle(f) +
                  " : retour au niveau 1.",
              ToastKind::Info);
        return;
    }
    if (cmd.name == "retry") {
        if (drill_.active) {
            drill_.phase = Phase::Setup;
            drill_.nextStart = Start::Manual;
        }
        return;
    }
    if (cmd.name == "restore_speed") {
        restoreSpeed(ctx, out);
        return;
    }
    if (cmd.name != "focus" && cmd.name != "drill") {
        toast(out, "Mécas mouvement",
              "Commande inconnue : " + cmd.name + " (focus, drill, stop, retry, reset_level, reset_stats, restore_speed).",
              ToastKind::Warning);
        return;
    }
    if (!cfg_.enabled) {
        toast(out, "Mécas mouvement", "Le module est désactivé dans les réglages.", ToastKind::Warning);
        return;
    }
    if (cmd.name == "focus" && (arg.empty() || arg == "off" || arg == "none" || arg == "aucun")) {
        setFocus(Focus::None, ctx, out);
        return;
    }
    const Focus f = mech::focusFromId(arg);
    if (f == Focus::None) {
        toast(out, "Mécas mouvement", "Méca inconnue : " + arg + ". Choix : " + mech::focusIdList() + ".", ToastKind::Warning);
        return;
    }
    if (cmd.name == "focus") {
        setFocus(f, ctx, out);
        return;
    }
    // "drill speedflip kickoff": straight to the kickoff step (niveau 4) for a player who already has the flip.
    if (arg2 == "kickoff" && f == Focus::SpeedFlip && level(f) < mech::levelOf(4))
        staircase(f, false).setLevel(mech::levelOf(4));
    startDrill(f, ctx, out, arg2 == "full");
}

void MechanicsModule::setFocus(Focus f, const TickContext& ctx, Actions& out) {
    focusSpeedWanted_ = false;
    if (f == Focus::None) {
        if (drill_.active || focusSpeedOwned_) stopWorld(ctx, out);
        focus_ = Focus::None;
        applyDetectConfig();
        toast(out, "Mécas mouvement", "Focus désactivé : toutes les mécas sont notées.", ToastKind::Info);
        return;
    }
    if (drill_.active && drill_.focus != f) stopWorld(ctx, out);
    focusFromDrill_ = false;
    focus_ = f;
    applyDetectConfig();
    if (cfg_.slowMotion < 0.999f && !drill_.active && !focusSpeedOwned_) {
        takeFocusSpeed(ctx, out);
        if (focusSpeedWanted_)
            toast(out, "Mécas mouvement", "Un autre exercice est en cours : le ralenti s'appliquera quand il se terminera.",
                  ToastKind::Info);
    }
    toast(out, "Focus : " + focusTitle(f), "Seule cette méca est notée. Ce qu'il faut faire reste affiché à gauche.",
          ToastKind::Info);
}

void MechanicsModule::startDrill(Focus f, const TickContext& ctx, Actions& out, bool full) {
    if (!ctx.worldControlAllowed) {
        toast(out, "Mécas mouvement", "Exercices disponibles en freeplay uniquement (ici : analyse seulement).",
              ToastKind::Warning);
        return;
    }
    if (!requestActivity("Mécas : " + focusTitle(f))) {
        toast(out, "Mécas mouvement", "Impossible de lancer l'exercice ici.", ToastKind::Warning);
        return;
    }
    pendingRelease_ = false;
    focusSpeedOwned_ = false;
    focusSpeedWanted_ = false;  // the drill applies the slow motion itself
    restoreWanted_ = false;
    drill_ = Drill{};
    drill_.active = true;
    drill_.full = full;
    drill_.focus = f;
    drill_.phase = Phase::Setup;
    drill_.nextStart = Start::First;
    held_.reset();
    heldFor_ = HeldFor::None;
    focusFromDrill_ = focus_ != f;  // a focus chosen before the drill survives it
    focus_ = f;
    applyDetectConfig();
    toast(out, (full ? "Parcours complet : " : "Exercice : ") + focusTitle(f),
          "Appuie sur accélérer quand tu es prêt. " + std::string(mech::focusInfo(f).memo), ToastKind::Info);
}

void MechanicsModule::emitSpeed(float speed, Actions& out) {
    out.setGameSpeed = speed;
    appliedSpeed_ = speed;
}

void MechanicsModule::takeFocusSpeed(const TickContext& ctx, Actions& out) {
    focusSpeedWanted_ = false;
    if (focus_ == Focus::None || drill_.active || focusSpeedOwned_ || cfg_.slowMotion >= 0.999f) return;  // obsolete
    restoreWanted_ = false;  // the newer slow motion wish replaces a waiting "restore_speed"
    if (ctx.worldControlAllowed && ctx.otherActivityRunning) {
        focusSpeedWanted_ = true;  // slow motion is not worth stopping another exercise: retried every tick
        return;
    }
    if (ctx.worldControlAllowed && requestActivity("Mécas : focus " + focusTitle(focus_))) {
        focusSpeedOwned_ = true;
        pendingRelease_ = false;
    } else {
        toast(out, "Mécas mouvement", "Ralenti indisponible ici : focus sans ralenti.", ToastKind::Info);
    }
}

void MechanicsModule::stopWorld(const TickContext& ctx, Actions& out) {
    const bool owner = ownsActivity();
    if (owner && ctx.worldControlAllowed) {
        // Normal stop, still the owner: the speed restore is routed with this callback, the release happens next tick.
        if (appliedSpeed_ != 1.f) emitSpeed(1.f, out);
        pendingRelease_ = true;
    } else {
        // A restore would be dropped here: the engine puts the speed back to 1 when the activity ends.
        if (owner) releaseActivity();
        appliedSpeed_ = 1.f;
        pendingRelease_ = false;
    }
    focusSpeedWanted_ = false;
    if (drill_.active && focusFromDrill_) focus_ = Focus::None;  // the drill chose the focus: it ends with it
    focusFromDrill_ = false;
    drill_ = Drill{};
    setup_ = mech::DrillSetup{};
    ready_.stop();
    hanging_.stop();
    tracker_.clear();
    live_.reset();
    focusSpeedOwned_ = false;
    held_.reset();
    heldFor_ = HeldFor::None;
    resetTicks_ = 0;
    coach_.kickoff().setManual(false);
    applyDetectConfig();
}

void MechanicsModule::onActivityStopped() {
    // No Actions here and no ownership any more: the engine restores the game speed when the activity ends (stop
    // button, handover, map unloaded, world control lost). The focus slow motion is not taken back by itself.
    appliedSpeed_ = 1.f;
    focusSpeedWanted_ = false;
    if (drill_.active && focusFromDrill_) focus_ = Focus::None;
    focusFromDrill_ = false;
    drill_ = Drill{};
    setup_ = mech::DrillSetup{};
    ready_.stop();
    hanging_.stop();
    tracker_.clear();
    live_.reset();
    focusSpeedOwned_ = false;
    pendingRelease_ = false;
    held_.reset();
    heldFor_ = HeldFor::None;
    resetTicks_ = 0;
    coach_.kickoff().setManual(false);
    applyDetectConfig();
}

// ---- Drill state machine -------------------------------------------------------------------------------
Staircase& MechanicsModule::staircase(Focus f, bool full) {
    const size_t i = static_cast<size_t>(std::max(0, std::min(mech::kFocusCount - 1, mech::index(f))));
    return full ? fullLevels_[i] : levels_[i];
}

float MechanicsModule::fullLevel(Focus f) const {
    const int i = mech::index(f);
    return (i >= 0 && i < mech::kFocusCount) ? fullLevels_[static_cast<size_t>(i)].level() : 0.f;
}

const char* MechanicsModule::defaultMiss(Focus f) {
    switch (f) {
    case Focus::SpeedFlip: return "SF_NO_FLIP";
    case Focus::HalfFlip: return "HF_NO_FLIP";
    case Focus::FlipCancel: return "FC_NO_FLIP";
    case Focus::Wavedash: return "WD_NO_FLIP";
    case Focus::ChainDash: return "DRILL_CHAIN_SHORT";
    case Focus::ZapDash: return "ZAP_NO_FLIP";
    case Focus::WallDash: return "WALL_NO_DASH";
    case Focus::FastAerial: return "FA_NO_JUMP";
    case Focus::Stall: return "ST_NO_STALL";
    case Focus::Helicopter: return "AS_NO_SPIN";
    case Focus::Recovery: return "RC_TILTED";
    case Focus::Powerslide: return "PS_NO_SLIDE";
    case Focus::Boost: return "SM_SLOW_TO_SUPERSONIC";
    default: return "DRILL_NO_TOUCH";
    }
}

void MechanicsModule::runWorld(const TickContext& ctx, Actions& out) {
    // Live query: ctx.ownsActivity was taken before this callback, the activity may have been granted during it.
    if (!ownsActivity() || !ctx.worldControlAllowed) {
        stopWorld(ctx, out);
        return;
    }
    const float desired = clampf(cfg_.slowMotion, 0.25f, 1.f);
    if (std::fabs(desired - appliedSpeed_) > 0.001f) emitSpeed(desired, out);
    if (focusSpeedOwned_ && !drill_.active) {
        if (desired >= 0.999f) {  // slow motion turned off: give the activity back
            focusSpeedOwned_ = false;
            pendingRelease_ = true;
        }
        return;
    }
    if (!drill_.active) return;
    const Snapshot& s = ctx.snap;
    switch (drill_.phase) {
    case Phase::Setup: beginAttempt(ctx, out, drill_.nextStart); break;
    case Phase::Countdown:
        // GO: launch without holding (freezeCar would swallow the inputs the player already holds).
        if (ready_.update(s, out)) launch(ctx, out);
        else applyHold(hold_, &s, out);
        break;
    case Phase::Running: runAttempt(ctx, out); break;
    case Phase::Showing:
        if (cfg_.drillAutoReset && s.time >= drill_.showUntil) {
            drill_.attempt++;
            beginAttempt(ctx, out, drill_.nextStart);
        }
        break;
    }
}

void MechanicsModule::beginAttempt(const TickContext& ctx, Actions& out, Start start) {
    const Snapshot& s = ctx.snap;
    const int oldStage = setup_.stage;
    setup_ = mech::makeDrill(drill_.focus, levelFor(drill_.focus, drill_.full), drill_.attempt, s.car.team, rng_, drill_.full);
    // The level crossed a step of the progression (speedflip): say what changes before the attempt starts.
    drill_.newStage = !drill_.full && drill_.attempts > 0 && setup_.stage != oldStage;
    if (drill_.newStage)
        toast(out, setup_.stage > oldStage ? "Étape suivante" : "Retour à l'étape précédente", setup_.goal,
              setup_.stage > oldStage ? ToastKind::Ok : ToastKind::Info);
    // Start pose, held on every tick of the get-ready phase (zero velocity, boost re-sent).
    hold_ = HoldPose{};
    hold_.car = setup_.car;
    hold_.ball = setup_.ball;  // the drill ball, or the ball parked out of the way
    hold_.boost = setup_.car.boost;
    applyHold(hold_, nullptr, out);
    if (setup_.kickoffRules) {
        // Match conditions: limited boost (the freeplay unlimited boost would falsify the times; the engine puts the
        // player's rules back when the activity ends) and every boost pad respawned.
        out.boostRules = BoostRules{false, false};
        out.resetPickups = true;
    }
    coach_.kickoff().setManual(setup_.kickoff);
    coach_.reset();
    resetTicks_ = 0;
    held_.reset();
    heldFor_ = HeldFor::None;
    hanging_.stop();
    drill_.touchTime = -1.f;
    drill_.touchBallZ = 0.f;
    drill_.minGap = 1e9f;
    drill_.reached = false;
    drill_.anyInput = false;
    drill_.floorTicks = 0;
    drill_.runOk = 0;
    drill_.cues = 0;
    drill_.timedOut = false;
    drill_.missing.clear();
    applyDetectConfig();
    ReadyPhase::Options opt;
    switch (start) {
    case Start::First: opt = ReadyPhase::firstAttempt(); break;
    case Start::Manual: opt = ReadyPhase::manualRetry(); break;
    case Start::Auto: opt = ReadyPhase::Options{false, std::max(0.5f, setup_.countdown), true, false}; break;
    }
    ready_.start(s.time, opt);
    drill_.nextStart = Start::Auto;
    drill_.phase = Phase::Countdown;
    drill_.message = "Prêt ?";
    if (setup_.kickoff) coach_.kickoff().countdown();
    tracker_.clear();
    live_.reset();
    if (drill_.full) {
        live_ = std::make_shared<mech::FullLive>();
        live_->coach = &coach_;
        live_->mirrored = setup_.mirrored;
        live_->side = setup_.side;
        live_->spotKind = setup_.spotKind;
        tracker_.setSteps(mech::fullSteps(drill_.focus, levelFor(drill_.focus, true), setup_, live_));
    }
}

void MechanicsModule::launch(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    // Launch velocity (a car held still, like the kickoff, needs nothing: its inputs simply start to count).
    if (length(setup_.car.vel) > 1e-3f || length(setup_.car.angVel) > 1e-3f) {
        CarPlacement c = setup_.car;
        c.boost.reset();
        // A floor car keeps the height its suspension settled at during the hold.
        if (c.pos.z <= kSettleMaxZ && s.car.valid && length2D(s.car.pos - c.pos) <= 30.f)
            c.pos.z = clampf(s.car.pos.z, c.pos.z - kSettleBand, c.pos.z + kSettleBand);
        out.setCar = c;
        resetTicks_ = 1;  // the launch shows up on the next snapshot
    }
    if (setup_.hangBall) {
        hanging_.hang(setup_.ball.pos);
        hanging_.update(s, out);
    } else if (setup_.placeBall && (length(setup_.ball.vel) > 1e-3f || length(setup_.ball.angVel) > 1e-3f)) {
        out.setBall = setup_.ball;
    }
    drill_.phase = Phase::Running;
    drill_.startTime = s.time;
    drill_.goFrame = s.frame;
    drill_.message = "GO !";
    coach_.reset();
    if (setup_.kickoff) coach_.kickoff().go(s);
    if (drill_.full && live_) {
        live_->goFrame = s.frame;
        live_->goTime = s.time;
        live_->s = s;
        live_->p = s;
        tracker_.start(s.time);
    }
}

void MechanicsModule::runAttempt(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const Inputs& in = s.in;
    if (std::fabs(in.throttle) > 0.2f || std::fabs(in.steer) > 0.3f || in.jump || in.boost || in.handbrake ||
        std::fabs(in.pitch) > 0.3f || std::fabs(in.yaw) > 0.3f || std::fabs(in.roll) > 0.3f)
        drill_.anyInput = true;
    // Fast aerial: the ball waits in the air until the car touches it (never re-sent on a tick that shows a touch).
    if (setup_.hangBall) hanging_.update(s, out, drill_.touchTime >= 0.f);
    // Hitbox to ball distance: near-miss levels count a pass close enough as the touch.
    if (setup_.placeBall && s.ball.valid) {
        const Vec3 half = ctx.profile.hitboxExtent * 0.5f;
        const float gap = mech::hitboxGap(s.car, s.ball.pos, half, ctx.profile.hitboxOffset);
        drill_.minGap = std::min(drill_.minGap, gap);
        if (setup_.reachGap > 0.f && !drill_.reached && gap <= setup_.reachGap) {
            drill_.reached = true;
            if (setup_.kickoff) coach_.onBallTouch(s.time);  // the kickoff speedflip gets its "time to ball"
            if (drill_.touchTime < 0.f) {
                drill_.touchTime = s.time;
                drill_.touchBallZ = s.ball.pos.z;
            }
        }
    }
}

void MechanicsModule::trackCues(const Snapshot& s) {
    using mech::Cue;
    auto see = [this](Cue c) { drill_.cues |= 1u << static_cast<unsigned>(c); };
    const CarState& car = s.car;
    if (s.in.boost) see(Cue::Boost);
    if (s.in.throttle >= 0.5f) see(Cue::Throttle);
    if (s.in.handbrake) see(Cue::Slide);
    if (std::fabs(s.in.steer) >= 0.15f) see(Cue::Steer);
    if (s.in.pitch >= 0.3f) see(Cue::PitchBack);
    if (std::fabs(s.in.roll) >= 0.5f) see(Cue::Roll);
    if (std::fabs(s.in.yaw) >= 0.5f) see(Cue::Yaw);
    if (car.jumped) see(Cue::Jump);
    if (car.doubleJumped) see(Cue::DoubleJump);
    if (car.dodging) see(Cue::Dodge);
    const mech::DodgeRecord* d = coach_.dodges().active();
    if (d && d->cancelTicks >= 0) see(Cue::Cancel);
    if (car.wheelContacts == 0) see(Cue::Air);
    else if (cueSeen(Cue::Air) && (mech::grounded(car) || car.wheelContacts > 0)) see(Cue::Land);
    if (drill_.touchTime >= 0.f) see(Cue::Touch);
}

int MechanicsModule::currentStep() const {
    // The step after the furthest one done. An input step (boost, steer...) may be skipped; a move step (jump, flip,
    // landing...) only counts once the move steps before it are done: landing after a jump without the flip is not
    // the end of a speedflip.
    using mech::Cue;
    int next = 0;
    bool chain = true;
    for (size_t i = 0; i < setup_.steps.size(); ++i) {
        const Cue c = setup_.steps[i].done;
        const bool input = c == Cue::Boost || c == Cue::Steer || c == Cue::Slide || c == Cue::PitchBack ||
                           c == Cue::Roll || c == Cue::Yaw || c == Cue::Throttle;
        const bool done = c != Cue::None && cueSeen(c) && (input || chain);
        if (done) next = static_cast<int>(i) + 1;
        else if (!input) chain = false;
    }
    return next;
}

void MechanicsModule::afterDetection(const TickContext& ctx, Actions& out) {
    if (!drill_.active || drill_.phase != Phase::Running) return;
    const Snapshot& s = ctx.snap;
    if (drill_.full) {
        runFull(ctx, out);
        return;
    }
    const float elapsed = s.time - drill_.startTime;
    if (held_) {
        if (heldFor_ == HeldFor::Landing && (mech::grounded(s.car) || s.time - heldTime_ > 2.5f)) {
            // Helicopter top levels: the spin counts once the car is back on its wheels.
            MechGrade g = std::move(*held_);
            held_.reset();
            heldFor_ = HeldFor::None;
            g.add("landedOnWheels", mech::grounded(s.car) && dot(s.car.basis.up, mech::contactNormal(s.car)) > 0.8f ? 1.f : 0.f);
            emitAttempt(g, ctx, out, true);
            return;
        }
        if (heldFor_ == HeldFor::Touch && s.time - heldTime_ > setup_.touchWindow) {
            releaseHeld(ctx, out);
            return;
        }
    }
    // Chain lane: the attempt ends at the end of the lane (the back wall is close after it).
    const float yBlue = setup_.mirrored ? -s.car.pos.y : s.car.pos.y;
    const bool laneEnd = setup_.endY > 0.f && yBlue >= setup_.endY;
    if (setup_.runDashes > 0) {
        // Wall dash runs: over once the car is back on the floor.
        const bool onFloor = mech::grounded(s.car) && mech::contactNormal(s.car).z > 0.7f;
        drill_.floorTicks = onFloor ? drill_.floorTicks + 1 : 0;
        if (elapsed > 0.5f && drill_.floorTicks > 30 && !held_) {
            grades_.clear();
            coach_.flushPending(s, grades_, false);
            for (MechGrade& g : grades_) handleGrade(g, ctx, out);
            if (drill_.phase == Phase::Running) recordMiss(drill_.runOk > 0 ? "WALL_FELL_OFF" : nullptr, ctx, out);
            return;
        }
    }
    // A flip still in the air at the time limit: its grade comes at the landing (at most 2 s later).
    const bool inFlight = coach_.dodges().active() != nullptr && elapsed < setup_.timeLimit + 2.f;
    if ((elapsed > setup_.timeLimit && !held_ && !inFlight) || laneEnd) {
        grades_.clear();
        coach_.flushPending(s, grades_, setup_.boostWindow);
        for (MechGrade& g : grades_) handleGrade(g, ctx, out);
        if (drill_.phase == Phase::Running && !held_) recordMiss(nullptr, ctx, out);
    }
}

void MechanicsModule::releaseHeld(const TickContext& ctx, Actions& out) {
    if (!held_) return;
    MechGrade g = std::move(*held_);
    held_.reset();
    const HeldFor why = heldFor_;
    heldFor_ = HeldFor::None;
    if (why == HeldFor::Landing) {
        g.add("landedOnWheels", 0.f);
        emitAttempt(g, ctx, out, drill_.active && drill_.phase == Phase::Running);
        return;
    }
    g.err("DRILL_NO_TOUCH", 30.f);
    mech::finalizeGrade(g, std::min(g.score, 40.f));
    g.success = false;
    emitAttempt(g, ctx, out, drill_.active && drill_.phase == Phase::Running);
}

void MechanicsModule::flushBeforeReset(const TickContext& ctx, Actions& out) {
    if (!ctx.hasSnapshot) return;
    grades_.clear();
    coach_.flushPending(ctx.snap, grades_, false);
    for (MechGrade& g : grades_) handleGrade(g, ctx, out);
    grades_.clear();
    releaseHeld(ctx, out);
}

void MechanicsModule::restoreSpeed(const TickContext& ctx, Actions& out) {
    restoreWanted_ = false;
    if (drill_.active || focusSpeedOwned_ || focusSpeedWanted_) {
        toast(out, "Mécas mouvement", "Le ralenti suit le réglage « Ralenti » : mets-le à 1 ou arrête l'exercice.",
              ToastKind::Info);
        return;
    }
    if (ctx.worldControlAllowed && ctx.otherActivityRunning) {
        restoreWanted_ = true;  // never stops another exercise: applied by onTick once it ends
        toast(out, "Mécas mouvement", "Un autre exercice est en cours : vitesse remise à 1 quand il se terminera.",
              ToastKind::Info);
        return;
    }
    if (!ctx.worldControlAllowed || (!ownsActivity() && !requestActivity("Mécas : vitesse normale"))) {
        toast(out, "Mécas mouvement", "Vitesse du jeu modifiable en freeplay uniquement.", ToastKind::Warning);
        return;
    }
    emitSpeed(1.f, out);
    pendingRelease_ = true;  // owner during this callback: the restore is routed, the activity is given back next tick
    toast(out, "Mécas mouvement", "Vitesse du jeu remise à 1.", ToastKind::Ok);
}

// ---- "Parcours complet" ----------------------------------------------------------------------------------
void MechanicsModule::runFull(const TickContext& ctx, Actions& out) {
    if (!live_) return;
    const Snapshot& s = ctx.snap;
    live_->p = hasPrevSnap_ ? prevSnap_ : s;
    live_->s = s;
    if (tracker_.running()) tracker_.update(s, &ctx);
    const float elapsed = s.time - drill_.startTime;
    const float yBlue = setup_.mirrored ? -s.car.pos.y : s.car.pos.y;
    const bool laneEnd = setup_.endY > 0.f && yBlue >= setup_.endY;
    if (tracker_.running() && (elapsed > setup_.timeLimit || laneEnd)) tracker_.failOnTimeout(s.time);
    if (tracker_.finished()) finishFull(ctx, out);
}

void MechanicsModule::finishFull(const TickContext& ctx, Actions& out) {
    const float t = ctx.hasSnapshot ? ctx.snap.time : nowTime_;
    const bool ok = tracker_.isComplete();
    const mech::FocusInfo& fi = mech::focusInfo(drill_.focus);
    AttemptResult r;
    r.module = "mechanics";
    r.skill = std::string("mech.") + mech::mechInfo(fi.primary).key;
    r.title = std::string(fi.title) + " (parcours complet)";
    int required = 0, reached = 0;
    for (int i = 0; i < tracker_.size(); ++i) {
        if (tracker_.steps()[static_cast<size_t>(i)].optional) continue;
        ++required;
        reached += tracker_.state(i) == StepState::Done ? 1 : 0;
    }
    r.score = ok ? 100.f : std::round(100.f * static_cast<float>(reached) / static_cast<float>(std::max(1, required)));
    r.success = ok;
    r.verdict = ok ? Verdict::Good : Verdict::Fail;
    r.time = t;
    Metric m;
    m.key = "stepsDone";
    m.label = "Étapes réussies";
    m.value = static_cast<float>(reached);
    m.ideal = static_cast<float>(required);
    m.hasIdeal = true;
    r.metrics.push_back(m);
    drill_.missing.clear();
    if (!ok) {
        const StepTracker::Failure& f = tracker_.failure();
        const std::string code = f.code.empty() ? std::string(defaultMiss(drill_.focus)) : f.code;
        MechGrade why;
        why.mech = fi.primary;
        why.add("targetAngleDeg", cfg_.speedflipAngle);
        r.tips.push_back(Tip{mech::tipCode(code), mech::tipText(code, why, ctx.profile, deadzone_, keyStyle_), 50.f});
        drill_.missing = f.label;
        drill_.timedOut = f.timedOut;
    }
    if (cfg_.drillAdaptive) staircase(drill_.focus, true).record(ok);
    closeDrillAttempt(ok, t);
    drill_.message = ok ? "Parcours réussi !" : "Raté : il manquait " + drill_.missing + ".";
    last_.valid = true;
    last_.result = r;
    last_.variant = setup_.variant;
    last_.lines.clear();
    last_.headline = ok ? std::string("Parcours réussi : toutes les étapes sont faites.") : r.tips.front().text;
    last_.drillAttempt = true;
    last_.time = t;
    if (cfg_.chatSummary) {
        std::string line = "[Mécas] " + r.title + " : " + (ok ? "réussi" : "raté");
        if (!r.tips.empty()) line += ". " + r.tips.front().text;
        out.chat.push_back({line, "RL Trainer"});
    }
    out.requestSave = true;
    out.results.push_back(std::move(r));
}

// ---- Results -------------------------------------------------------------------------------------------
void MechanicsModule::handleGrade(MechGrade& g, const TickContext& ctx, Actions& out) {
    const Focus fam = mech::familyOf(g.mech);
    const bool asked = trained(fam);  // explicit focus or drill: the family toggle and the map filter do not apply
    if (!asked && !familyEnabled(fam)) return;
    if (!asked && !categoryAllows(g.mech, ctx.game.category)) return;
    if (focus_ != Focus::None && fam != focus_) return;
    // "Parcours complet": the family's grades feed the step checks, the sequence makes the single result.
    if (drill_.active && drill_.full && fam == drill_.focus) return;
    bool drillAttempt = drill_.active && drill_.phase == Phase::Running && fam == drill_.focus;
    // Kickoff drill: the speedflip has to reach the ball (time to ball measured at the touch).
    if (drillAttempt && setup_.kickoff && g.mech == Mech::SpeedFlip && g.metric("timeToBall", -1.f) < 0.f &&
        !g.hasError("DRILL_NO_TOUCH") && !g.hasError("SF_SIDE_FLIP") && !g.hasError("SF_BACK_FLIP")) {
        g.err("DRILL_NO_TOUCH", 30.f);
        mech::finalizeGrade(g, g.score);
        g.success = false;
    }
    // Wall dash run: a chain grade is a summary, each wall dash decides; the run needs `runDashes` good ones.
    if (drillAttempt && g.mech == Mech::WallChain) drillAttempt = false;
    if (drillAttempt && setup_.runDashes > 1 && g.mech == Mech::WallDash) {
        const mech::DrillJudgement j = mech::judgeDrill(setup_, g);
        if (j.pass && ++drill_.runOk < setup_.runDashes) {
            drill_.message = "Wall dash " + std::to_string(drill_.runOk) + " sur " + std::to_string(setup_.runDashes) + " : continue !";
            drillAttempt = false;  // shown, not the end of the run
        }
    }
    if (drillAttempt && setup_.needTouch && !held_) {
        if (drill_.touchTime >= 0.f) {
            g.add("timeToBall", std::max(0.f, drill_.touchTime - drill_.startTime));
            g.add("ballZAtTouch", drill_.touchBallZ);
        } else {
            held_ = std::move(g);
            heldFor_ = HeldFor::Touch;
            heldTime_ = ctx.snap.time;
            return;
        }
    }
    if (drillAttempt && setup_.needLanding && !held_ && g.mech == Mech::AirSpin) {
        if (!mech::grounded(ctx.snap.car)) {
            held_ = std::move(g);
            heldFor_ = HeldFor::Landing;
            heldTime_ = ctx.snap.time;
            return;
        }
        g.add("landedOnWheels", dot(ctx.snap.car.basis.up, mech::contactNormal(ctx.snap.car)) > 0.8f ? 1.f : 0.f);
    }
    emitAttempt(g, ctx, out, drillAttempt);
}

void MechanicsModule::closeDrillAttempt(bool ok, float t) {
    drill_.attempts++;
    drill_.newStage = false;
    if (ok) {
        drill_.successes++;
        drill_.failStreak = 0;
    } else {
        drill_.failStreak++;
    }
    drill_.phase = Phase::Showing;
    drill_.showUntil = t + std::max(drill_.timedOut ? 1.f : 0.f, cfg_.drillDelay);
    // The player did nothing at all: he may be in the menu, the next attempt waits for the throttle again.
    drill_.nextStart = drill_.anyInput ? Start::Auto : Start::First;
}

void MechanicsModule::recordMiss(const char* code, const TickContext& ctx, Actions& out) {
    const float t = ctx.hasSnapshot ? ctx.snap.time : nowTime_;
    const int step = currentStep();
    const bool stepLeft = step < static_cast<int>(setup_.steps.size());
    if (!code || !*code) code = stepLeft ? setup_.steps[static_cast<size_t>(step)].miss : "";
    if (!code || !*code) code = defaultMiss(drill_.focus);
    const mech::FocusInfo& fi = mech::focusInfo(drill_.focus);
    MechGrade g;
    g.mech = fi.primary;
    g.variant = "drill";
    g.add("targetAngleDeg", cfg_.speedflipAngle);
    g.err(code, 50.f);
    mech::finalizeGrade(g, 0.f);
    g.success = false;
    AttemptResult r = mech::buildResult(g, ctx.profile, coach_.cfg, t, keyStyle_);
    r.success = false;
    r.verdict = Verdict::Fail;
    r.hasRecordValue = false;
    drill_.timedOut = true;
    drill_.missing = stepLeft ? setup_.steps[static_cast<size_t>(step)].text : std::string();
    if (cfg_.drillAdaptive) staircase(drill_.focus, false).record(false);
    closeDrillAttempt(false, t);
    drill_.message = drill_.missing.empty() ? std::string("Temps écoulé : on recommence.")
                                            : "Temps écoulé. Il manquait : " + drill_.missing + ".";
    stats_.record(g.mech, r);
    last_.valid = true;
    last_.result = r;
    last_.variant = setup_.variant;
    last_.lines.clear();
    last_.headline = r.tips.empty() ? drill_.message : r.tips.front().text;
    last_.drillAttempt = true;
    last_.time = t;
    if (cfg_.chatSummary) out.chat.push_back({"[Mécas] " + r.title + " : temps écoulé. " + last_.headline, "RL Trainer"});
    if (++resultsSinceSave_ >= 10) {
        out.requestSave = true;
        resultsSinceSave_ = 0;
    }
    out.results.push_back(std::move(r));
}

void MechanicsModule::emitAttempt(const MechGrade& g, const TickContext& ctx, Actions& out, bool drillAttempt) {
    const float t = ctx.hasSnapshot ? ctx.snap.time : nowTime_;
    AttemptResult r = mech::buildResult(g, ctx.profile, coach_.cfg, t, keyStyle_);
    if (drillAttempt) {
        // The level decides: its pass conditions, then the score it asks for, then (top levels) the full grading.
        const mech::DrillJudgement j = mech::judgeDrill(setup_, g);
        const bool detectorOk = r.success;
        const bool scoreOk = r.score >= setup_.passScore;
        const bool ok = j.pass && scoreOk && (detectorOk || !setup_.requireDetector);
        r.success = ok;
        if (!j.pass) {
            // The first condition this level missed is the one thing to fix now: its tip comes first.
            const std::string code = mech::tipCode(j.code);
            float w = 1.f;
            for (const mech::MechError& e : g.errors)
                if (code == mech::tipCode(e.code)) w = std::max(w, e.weight);
            if (!r.tips.empty()) w = std::max(w, r.tips.front().weight);
            r.tips.erase(std::remove_if(r.tips.begin(), r.tips.end(), [&](const Tip& t) { return t.code == code; }),
                         r.tips.end());
            r.tips.insert(r.tips.begin(), Tip{code, mech::tipText(j.code, g, ctx.profile, deadzone_, keyStyle_), w});
            if (r.tips.size() > 3) r.tips.resize(3);
        } else if (!scoreOk) {
            // Good enough for this level's conditions, but under the score it asks for: say why it does not count.
            // Ranked after the technique tips (they say what to improve), the only tip of a clean attempt.
            mech::MechGrade why;
            why.mech = g.mech;
            why.add("passScore", setup_.passScore);
            float w = std::max(1.f, setup_.passScore - r.score);
            if (!r.tips.empty()) w = std::min(w, r.tips.back().weight);
            Tip tip{mech::tipCode("DRILL_BELOW_LEVEL"),
                    mech::tipText("DRILL_BELOW_LEVEL", why, ctx.profile, deadzone_, keyStyle_), w};
            if (r.tips.size() >= 3) r.tips.back() = std::move(tip);
            else r.tips.push_back(std::move(tip));
        }
        if (!ok && (r.verdict == Verdict::Good || r.verdict == Verdict::Perfect)) r.verdict = Verdict::Ok;
        if (ok && r.verdict == Verdict::Fail) r.verdict = Verdict::Ok;
        if (cfg_.drillAdaptive) staircase(drill_.focus, false).record(ok);
        drill_.timedOut = false;
        drill_.missing.clear();
        closeDrillAttempt(ok, t);
        drill_.message = ok ? "Réussi !" : "Raté : lis le conseil et recommence.";
    }
    const bool newPb = stats_.record(g.mech, r);

    const bool quiet = g.mech == Mech::BoostEfficiency && focus_ != Focus::Boost && g.errors.empty() && !drillAttempt;
    const bool shown = !quiet && (r.score >= static_cast<float>(cfg_.minScore) || drillAttempt || focus_ != Focus::None);
    const std::string variant = mech::variantLabel(g.variant);
    if (shown) {
        last_.valid = true;
        last_.result = r;
        last_.variant = variant;
        last_.lines = mech::metricLines(g, coach_.cfg, speedUnit(), 5);
        last_.headline = r.tips.empty() ? std::string(mech::praiseText(g.mech)) : r.tips.front().text;
        last_.drillAttempt = drillAttempt;
        last_.time = t;
    }
    if (cfg_.chatSummary && shown) {
        std::string line = "[Mécas] " + r.title + (variant.empty() ? "" : " (" + variant + ")") + " : " +
                           formatNumber(r.score, 0) + "/100, " + verdictLabel(r.verdict);
        if (!r.tips.empty()) line += ". " + r.tips.front().text;
        out.chat.push_back({line, "RL Trainer"});
    }
    if (newPb && cfg_.toastRecords) {
        const mech::RecordDef& rd = mech::recordDef(g.mech);
        toast(out, "Nouveau record : " + r.title,
              std::string(rd.label) + " : " + mech::formatRecord(g.mech, r.valueForRecord, speedUnit()), ToastKind::Ok);
    }
    if (newPb || ++resultsSinceSave_ >= 10) {
        out.requestSave = true;
        resultsSinceSave_ = 0;
    }
    out.results.push_back(std::move(r));
}

// ---- Draw ----------------------------------------------------------------------------------------------
// Compact HUD (the default): at most three lines, the result (verdict and score), one tip in words, and during an
// exercise the action to do now. Measures, levels, steps and counters are `detail` lines (detailed HUD only).
void MechanicsModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled || !cfg_.hud) return;
    if (ctx.game.mode == GameMode::Replay) return;  // no live data in replays
    const float now = ctx.hasSnapshot ? ctx.snap.time : nowTime_;
    const SpeedUnit unit = speedUnit();

    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = std::max(0.2f, cfg_.hudScale * ctx.hudScale);
    p.minWidth = 300.f * p.scale;
    const Color white = colors::White, yellow(255, 210, 60, 255), green(80, 230, 80, 255), red(255, 80, 60, 255),
                cyan(0, 220, 255, 255), light(215, 215, 215, 255), accent(255, 199, 64, 255);
    auto line = [&p](std::string text, Color c) { p.lines.push_back({std::move(text), c}); };
    auto detail = [&p, &light](std::string text) {
        PanelLine l;
        l.text = std::move(text);
        l.color = light;
        l.scale = 0.85f;
        l.detail = true;
        p.lines.push_back(std::move(l));
    };
    auto metricDetails = [&]() {
        const size_t n = std::min(last_.lines.size(), static_cast<size_t>(std::max(0, cfg_.hudMetrics)));
        for (size_t i = 0; i < n; ++i) {
            const mech::MetricLine& l = last_.lines[i];
            std::string text = l.label + " : " + l.value;
            if (!l.ideal.empty()) text += "  (idéal " + l.ideal + ")";
            detail(std::move(text));
            p.lines.back().color = mech::qualityColor(l.quality);
        }
    };
    auto tipLine = [&]() {
        if (last_.headline.empty()) return;
        const bool praise = last_.result.tips.empty();
        line(last_.headline, praise ? green : yellow);
    };
    auto mainCount = [&p]() {
        return static_cast<int>(std::count_if(p.lines.begin(), p.lines.end(), [](const PanelLine& l) { return !l.detail; }));
    };

    const float age = now - last_.time;
    const bool persistent = focus_ != Focus::None || drill_.active || cfg_.hudSeconds <= 0.f;
    bool showResult = last_.valid && (persistent || (age >= -0.05f && age <= cfg_.hudSeconds));
    const AttemptResult& r = last_.result;

    if (drill_.active) {
        const int lvl = mech::displayLevel(levelFor(drill_.focus, drill_.full));
        p.title = std::string(drill_.full ? "Parcours complet " : "Exercice ") + focusTitle(drill_.focus) + " : niveau " +
                  std::to_string(lvl) + "/10";
        p.titleColor = cyan;
        showResult = showResult && last_.drillAttempt && drill_.attempts > 0;
        const bool showing = drill_.phase == Phase::Showing;
        const bool failedFull = showing && drill_.full && !r.success && showResult;
        const bool timedOut = showing && !drill_.full && drill_.timedOut;
        const int step = currentStep();
        const int nSteps = static_cast<int>(setup_.steps.size());
        const bool stepLeft = step < nSteps;
        const std::string counts = "(" + std::to_string(drill_.successes) + " sur " + std::to_string(drill_.attempts) + ")";
        bool tipShown = false;
        if (drill_.phase == Phase::Countdown || drill_.phase == Phase::Setup) {
            // Get ready: the goal, what to remember, then "press the throttle" / the countdown.
            line(setup_.goal, drill_.newStage ? cyan : white);
            if (drill_.failStreak >= 3 && cfg_.slowMotion >= 0.999f && !drill_.newStage)
                line("Trop dur ? Essaie le ralenti 0,75 (menu, Mécas mouvement)", yellow);
            else if (drill_.full && tracker_.size() > 0)
                line(tracker_.number(0) + tracker_.steps()[0].label, accent);
            else if (!setup_.memo.empty())
                line(setup_.memo, white);
            const std::string ready = ready_.hudLine(now);
            if (!ready.empty()) line(ready, cyan);
        } else if (drill_.phase == Phase::Running && drill_.full) {
            StepHudStyle style;
            style.showDone = !ctx.compactHud;
            style.showUpcoming = !ctx.compactHud;
            appendStepLines(p.lines, tracker_, now, style);
            const float left = setup_.timeLimit - (now - drill_.startTime);
            if (mainCount() < 3) line("Temps restant : " + seconds1(left), white);
            else detail("Temps restant : " + seconds1(left));
        } else if (drill_.phase == Phase::Running) {
            // 1. Result of the last attempt (or what this level asks), 2. one tip in words, 3. what to do now.
            if (showResult) {
                const Color c = r.success ? green : red;
                p.lines.push_back({std::string(r.success ? "Réussi ! " : "Raté : ") + formatNumber(r.score, 0) + "/100  " + counts,
                                   c, clamp01(r.score / 100.f), c});
                tipLine();
                tipShown = true;
            } else {
                line(setup_.goal, white);
                if (!setup_.memo.empty()) line(setup_.memo, white);
            }
            std::string action;
            Color ac = white;
            const float left = setup_.timeLimit - (now - drill_.startTime);
            const bool cue = setup_.nowCue > 0.f && ctx.hasSnapshot && !cueSeen(mech::Cue::Dodge) &&
                             ctx.snap.car.wheelContacts == 0 && mech::rearWheelTimeToFloor(ctx.snap.car) <= setup_.nowCue;
            if (cue) {
                action = "Maintenant !";
                ac = green;
            } else if (setup_.timed || !stepLeft) {
                action = "Temps restant : " + seconds1(left);
            } else {
                action = std::to_string(step + 1) + "/" + std::to_string(nSteps) + " " + setup_.steps[static_cast<size_t>(step)].text;
                ac = accent;
            }
            line(action, ac);
        } else if (showing) {
            if (timedOut) {
                line("Temps écoulé", red);
                if (!drill_.missing.empty()) line("Il manquait : " + drill_.missing, yellow);
                else tipLine(), tipShown = true;
            } else if (failedFull) {
                line("Raté " + counts, red);
                if (drill_.timedOut && !drill_.missing.empty()) {
                    line("Il manquait : " + drill_.missing, yellow);
                } else {
                    tipLine();
                    tipShown = true;
                }
            } else if (showResult) {
                const Color c = r.success ? green : red;
                const std::string score = drill_.full ? std::string() : formatNumber(r.score, 0) + "/100  ";
                p.lines.push_back({std::string(r.success ? "Réussi ! " : "Raté : ") + score + counts, c,
                                   drill_.full ? -1.f : clamp01(r.score / 100.f), c});
                tipLine();
                tipShown = true;
            } else {
                line(drill_.message.empty() ? setup_.goal : drill_.message, white);
            }
            line(cfg_.drillAutoReset ? "Essai suivant dans " + seconds1(drill_.showUntil - now) : "Fais un reset pour recommencer",
                 white);
        }

        std::string st = "Niveau " + std::to_string(lvl) + "/10";
        if (drill_.attempts > 0) st += " | réussis " + std::to_string(drill_.successes) + "/" + std::to_string(drill_.attempts);
        if (!setup_.variant.empty()) st += " | " + setup_.variant;
        detail(std::move(st));
        if (showing || drill_.phase == Phase::Running) detail(setup_.goal);
        if (!tipShown && showResult && !last_.headline.empty() && (timedOut || failedFull)) detail(last_.headline);
        if (drill_.full) {
            if (drill_.phase != Phase::Running) {
                StepHudStyle style;
                style.flashSeconds = 0.f;
                std::vector<PanelLine> steps;
                appendStepLines(steps, tracker_, now, style);
                for (PanelLine& l : steps) {
                    l.detail = true;
                    l.bar = -1.f;
                    l.scale = 0.85f;
                    p.lines.push_back(std::move(l));
                }
                if (tracker_.idle())
                    for (int i = 0; i < tracker_.size(); ++i) detail(tracker_.number(i) + tracker_.steps()[static_cast<size_t>(i)].label);
            }
        } else {
            for (int i = 0; i < nSteps; ++i)
                detail(std::to_string(i + 1) + "/" + std::to_string(nSteps) + " " + setup_.steps[static_cast<size_t>(i)].text);
        }
        if (showResult && !drill_.full) metricDetails();
    } else {
        if (focus_ != Focus::None) {
            p.title = "Focus : " + focusTitle(focus_);
            p.titleColor = cyan;
        } else {
            p.title = "Dernière méca";
        }
        if (showResult) {
            const Color vc = mech::verdictColor(r.verdict);
            std::string head = r.title;
            if (!last_.variant.empty()) head += " (" + last_.variant + ")";
            head += std::string(" : ") + verdictLabel(r.verdict) + " " + formatNumber(r.score, 0) + "/100";
            p.lines.push_back({head, vc, clamp01(r.score / 100.f), vc});
            tipLine();
        } else if (focus_ != Focus::None) {
            line(std::string("À faire : ") + mech::focusInfo(focus_).memo, white);
        }
        if (coach_.chainCount() > 0 && (cfg_.chainDash || cfg_.wallDash) &&
            (focus_ == Focus::None || focus_ == Focus::ChainDash || focus_ == Focus::WallDash)) {
            line("Chaîne en cours : " + dashCount(coach_.chainCount()) + ", " + formatSpeed(coach_.chainMaxSpeed(), unit, 0),
                 white);
        }
        if (focus_ != Focus::None) {
            if (showResult) detail(std::string("À faire : ") + mech::focusInfo(focus_).memo);
            for (const std::string& t : mech::targetLines(focus_, coach_.cfg, unit, 4)) detail("Objectif " + t);
        }
        if (showResult) metricDetails();
    }
    if (cfg_.hudCounters) {
        int total = 0, ok = 0;
        for (int i = 0; i < mech::kMechCount; ++i) {
            const mech::MechStat& st = stats_.get(static_cast<Mech>(i));
            total += st.sessionAttempts;
            ok += st.sessionSuccesses;
        }
        if (total > 0) detail("Session : " + std::to_string(ok) + "/" + std::to_string(total) + " mécas réussies");
    }
    // A panel of detail lines only would show as an empty box in the compact HUD.
    const bool main = std::any_of(p.lines.begin(), p.lines.end(), [](const PanelLine& l) { return !l.detail; });
    if (main || (!ctx.compactHud && !p.lines.empty())) out.panel(std::move(p));

    // 3D guides of the running drill (bounded: < 60 lines).
    if (!drill_.active || !cfg_.showGuides || !view.valid) return;
    Occluder occ;
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
    }
    const Color guide(0, 220, 255, 170);
    for (size_t i = 0; i + 1 < setup_.guide.size() && i < 16; ++i) out.line3D(setup_.guide[i], setup_.guide[i + 1], guide, 2.f, occ);
    for (size_t i = 0; i + 1 < setup_.ticks.size() && i < 32; i += 2) out.line3D(setup_.ticks[i], setup_.ticks[i + 1], guide, 2.f, occ);
    if (setup_.hasTarget) {
        // The ring follows the ball once it moves (kickoff ball, lob); a fixed target stays where it is.
        const Vec3 at = (setup_.placeBall && view.ballValid && !setup_.full) ? view.ballVisualPos : setup_.target;
        out.circle3D(at, {0.f, 0.f, 1.f}, 160.f, Color(255, 210, 60, 200), 2.f, 24);
    }
}

// ---- UI read models ------------------------------------------------------------------------------------
MechanicsModule::Status MechanicsModule::status() const {
    Status s;
    s.enabled = cfg_.enabled;
    s.focus = focus_;
    s.drillActive = drill_.active;
    s.worldControl = worldControl_;
    s.gameSpeed = (drill_.active || focusSpeedOwned_) ? appliedSpeed_ : 1.f;
    s.speedWaiting = speedWaiting();
    s.chainDashes = coach_.chainCount();
    s.sessionAttempts = stats_.sessionTotal();
    if (drill_.active) {
        s.full = drill_.full;
        s.level = levelFor(drill_.focus, drill_.full);
        s.levelNumber = mech::displayLevel(s.level);
        s.drillAttempts = drill_.attempts;
        s.drillSuccesses = drill_.successes;
        s.failStreak = drill_.failStreak;
        s.goal = setup_.goal;
        s.variant = setup_.variant;
        s.message = drill_.message;
        s.waitingForThrottle = drill_.phase == Phase::Countdown && ready_.waiting();
        if (drill_.full) {
            for (const Step& st : tracker_.steps()) s.steps.push_back(st.label);
            if (drill_.phase == Phase::Running) s.currentStep = tracker_.current();
            else if (drill_.phase == Phase::Countdown) s.currentStep = 0;
        } else {
            for (const mech::DrillStep& st : setup_.steps) s.steps.push_back(st.text);
            if (drill_.phase == Phase::Running || drill_.phase == Phase::Countdown) s.currentStep = currentStep();
            s.checks = mech::drillCheckLines(setup_, coach_.cfg, speedUnit());
        }
        switch (drill_.phase) {
        case Phase::Setup: s.phase = "Placement"; break;
        case Phase::Countdown: s.phase = "Compte à rebours"; break;
        case Phase::Running:
            s.phase = "Essai en cours";
            s.timeLeft = std::max(0.f, setup_.timeLimit - (nowTime_ - drill_.startTime));
            break;
        case Phase::Showing: s.phase = "Résultat"; break;
        }
    }
    return s;
}

std::vector<MechanicsModule::StatRow> MechanicsModule::statRows() const {
    std::vector<StatRow> rows;
    rows.reserve(mech::kFocusCount);
    for (int i = 0; i < mech::kFocusCount; ++i) {
        const Focus f = static_cast<Focus>(i);
        const mech::FocusInfo& fi = mech::focusInfo(f);
        const mech::MechStat& st = stats_.get(fi.primary);
        StatRow r;
        r.focus = f;
        r.id = fi.id;
        r.title = fi.title;
        r.group = fi.group;
        r.what = fi.what;
        r.key = fi.key;
        r.tier = fi.tier;
        r.memo = fi.memo;
        r.tech = fi.tech;
        r.progression = fi.progression;
        for (const char* step : fi.steps)
            if (step) r.steps.push_back(step);
        const mech::Tutorial& tu = mech::tutorial(f);
        auto fill = [this](const char* t) { return t ? mech::fillButtons(t, profile_, keyStyle_) : std::string(); };
        r.intro = fill(tu.intro);
        r.full = fill(tu.full);
        r.level1 = fill(tu.level1);
        for (const mech::TutorialStep& ts : tu.steps)
            if (ts.action) r.tutorial.push_back({fill(ts.action), fill(ts.why), fill(ts.ok)});
        for (const char* l : tu.panel)
            if (l) r.panel.push_back(l);
        r.enabled = familyEnabled(f);
        r.attempts = st.attempts;
        r.successes = st.successes;
        r.sessionAttempts = st.sessionAttempts;
        r.sessionSuccesses = st.sessionSuccesses;
        r.average = st.average();
        r.best = st.best;
        r.hasPb = st.hasPb;
        r.pbLabel = mech::recordDef(fi.primary).label;
        if (st.hasPb) r.pbText = mech::formatRecord(fi.primary, st.pb, speedUnit());
        const std::string top = st.topError();
        if (!top.empty()) {
            MechGrade ctxGrade;
            ctxGrade.mech = fi.primary;
            ctxGrade.add("targetAngleDeg", cfg_.speedflipAngle);
            r.topErrorTip = mech::tipText(upperCode(top), ctxGrade, profile_, deadzone_, keyStyle_);
        }
        r.recent.assign(st.recent.begin(), st.recent.end());
        r.level = level(f);
        r.levelNumber = mech::displayLevel(r.level);
        r.fullLevel = fullLevel(f);
        r.fullLevelNumber = mech::displayLevel(r.fullLevel);
        Rng rng(1);  // only the recovery throw uses it, not the pass conditions
        r.checksFirst = mech::drillCheckLines(mech::makeDrill(f, 0.f, 0, 0, rng), coach_.cfg, speedUnit());
        r.checksLast = mech::drillCheckLines(mech::makeDrill(f, 1.f, 0, 0, rng), coach_.cfg, speedUnit());
        r.checksNow = mech::drillCheckLines(mech::makeDrill(f, r.level, 0, 0, rng), coach_.cfg, speedUnit());
        rows.push_back(std::move(r));
    }
    return rows;
}

// ---- Persistence ---------------------------------------------------------------------------------------
void MechanicsModule::save(Store& store) const {
    store.setInt("mechanics.version", 1);
    stats_.save(store);
    for (int i = 0; i < mech::kFocusCount; ++i) {
        const std::string id = mech::focusInfo(static_cast<Focus>(i)).id;
        levels_[static_cast<size_t>(i)].save(store, "mechanics.level." + id);
        fullLevels_[static_cast<size_t>(i)].save(store, "mechanics.full." + id);
    }
}

void MechanicsModule::load(const Store& store) {
    stats_.load(store);
    for (int i = 0; i < mech::kFocusCount; ++i) {
        const std::string id = mech::focusInfo(static_cast<Focus>(i)).id;
        // The previous version saved every level, played or not, at its old start value: only a played level is
        // kept, so a drill never tried starts at niveau 1.
        Staircase s = freshLevel();
        if (store.getInt("mechanics.level." + id + ".attempts", 0) > 0) s.load(store, "mechanics.level." + id);
        levels_[static_cast<size_t>(i)] = s;
        Staircase full = freshLevel();
        if (store.getInt("mechanics.full." + id + ".attempts", 0) > 0) full.load(store, "mechanics.full." + id);
        fullLevels_[static_cast<size_t>(i)] = full;
    }
}

} // namespace rlt
