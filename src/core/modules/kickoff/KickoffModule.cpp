#include "core/modules/kickoff/KickoffModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/common/Text.h"

namespace rlt {

namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string seconds(float s, int decimals = 2) { return formatNumber(s, decimals) + " s"; }

KickoffLevel ghostLevel(KickoffGhostMode m) {
    switch (m) {
    case KickoffGhostMode::Beginner: return KickoffLevel::Beginner;
    case KickoffGhostMode::Standard: return KickoffLevel::Standard;
    case KickoffGhostMode::Elite: return KickoffLevel::Elite;
    default: return KickoffLevel::Pro;
    }
}

constexpr int64_t kPadRespawnTicks = 4 * 120;  // small boost pad respawn time (4 s)

BallPlacement restingBall(float z) {
    BallPlacement b;
    b.pos = {0.f, 0.f, z};
    return b;
}

} // namespace

// ---- Names ---------------------------------------------------------------------------------------------------
const char* kickoffModeName(KickoffMode m) {
    switch (m) {
    case KickoffMode::Chosen: return "Position choisie";
    case KickoffMode::Random: return "Aléatoire";
    case KickoffMode::Sequence: return "Série des 5";
    default: return "Faiblesse";
    }
}

const char* kickoffGhostModeName(KickoffGhostMode g) {
    switch (g) {
    case KickoffGhostMode::Off: return "Désactivé";
    case KickoffGhostMode::Beginner: return "Débutant";
    case KickoffGhostMode::Standard: return "Standard";
    case KickoffGhostMode::Pro: return "Pro";
    case KickoffGhostMode::Elite: return "Élite";
    case KickoffGhostMode::PersonalBest: return "Mon record";
    default: return "Adaptatif";
    }
}

bool kickoffGhostModeFromKey(const std::string& raw, KickoffGhostMode& out) {
    const std::string k = lower(raw);
    if (k == "off" || k == "none" || k == "aucun") out = KickoffGhostMode::Off;
    else if (k == "beginner" || k == "debutant") out = KickoffGhostMode::Beginner;
    else if (k == "standard") out = KickoffGhostMode::Standard;
    else if (k == "pro") out = KickoffGhostMode::Pro;
    else if (k == "elite") out = KickoffGhostMode::Elite;
    else if (k == "pb" || k == "record" || k == "best") out = KickoffGhostMode::PersonalBest;
    else if (k == "adaptive" || k == "adaptatif") out = KickoffGhostMode::Adaptive;
    else return false;
    return true;
}

// ---- Settings ------------------------------------------------------------------------------------------------
void KickoffModule::describeSettings(SettingsSchema& schema) {
    std::vector<std::string> spawns;
    for (int i = 0; i < kKoSpawnCount; ++i) spawns.push_back(kickoffSpawnInfo(kickoffSpawnFromIndex(i)).name);
    std::vector<std::string> levels;
    for (int i = 0; i < kKoLevelCount; ++i) levels.push_back(kickoffLevelName(kickoffLevelFromIndex(i)));
    std::vector<std::string> ghosts;
    for (int i = 0; i < kKoGhostModeCount; ++i)
        ghosts.push_back(kickoffGhostModeName(static_cast<KickoffGhostMode>(i)));

    auto g = schema.section(&Settings::kickoff, "kickoff", "Kickoff");
    g.toggle("rlt_ko_enabled", &KickoffConfig::enabled, true, "Activer",
             "Active le module Kickoff : exercice avec décompte, fantôme, notes et analyse de tes kickoffs.");

    auto d = schema.section(&Settings::kickoff, "kickoff", "Exercice");
    d.choice("rlt_ko_mode", &KickoffConfig::spawnMode, 1, {"Position choisie", "Aléatoire", "Série des 5", "Faiblesse"},
             "Mode",
             "Position de départ de chaque essai. Série : les 5 positions à la suite. Faiblesse : la position "
             "où ta note récente est la plus basse.");
    d.choice("rlt_ko_spawn", &KickoffConfig::spawn, 0, spawns, "Position choisie",
             "Position utilisée par le mode Position choisie.");
    d.choice("rlt_ko_team", &KickoffConfig::team, 0, {"Bleue", "Orange"}, "Équipe",
             "Ton côté du terrain. En orange, la position est le symétrique et le fantôme part du côté bleu.");
    d.toggle("rlt_ko_auto_repeat", &KickoffConfig::autoRepeat, true, "Enchaîner les essais",
             "Relance un kickoff tout seul après chaque essai. Sinon, appuie sur Reset pour relancer.");
    d.number("rlt_ko_reset_delay", &KickoffConfig::resetDelay, 2.f, 0.5f, 6.f, "Pause entre deux essais (s)",
             "Temps pour lire ton résultat avant le kickoff suivant.", 1);
    d.number("rlt_ko_timeout", &KickoffConfig::timeout, 6.f, 3.f, 10.f, "Temps max sans touche (s)",
             "Après ce temps sans toucher la balle, l'essai est noté comme raté.", 1);
    d.toggle("rlt_ko_go_jitter", &KickoffConfig::goJitter, false, "GO imprévisible",
             "Ajoute un petit délai aléatoire avant le GO : tu travailles ta réaction au lieu du rythme du décompte.");
    d.toggle("rlt_ko_native_countdown", &KickoffConfig::nativeCountdown, true, "Décompte du jeu",
             "Affiche aussi le 3, 2, 1, GO officiel de Rocket League.");

    auto da = schema.section(&Settings::kickoff, "kickoff", "Exercice");
    da.advanced();
    da.number("rlt_ko_go_jitter_max", &KickoffConfig::goJitterMax, 0.4f, 0.05f, 1.f, "Délai max du GO (s)",
              "Retard maximal ajouté au GO quand le GO imprévisible est activé.", 2);
    da.integer("rlt_ko_countdown", &KickoffConfig::countdownSeconds, 3, 1, 5, "Durée du décompte (s)",
               "3 secondes comme en match.");
    da.toggle("rlt_ko_hold_car", &KickoffConfig::holdCar, true, "Maintenir la voiture au décompte",
              "Replace la voiture à chaque image du décompte pour qu'elle ne bouge pas avant le GO.");
    da.number("rlt_ko_spawn_boost", &KickoffConfig::spawnBoost, 100.f / 3.f, 0.f, 100.f, "Boost de départ",
              "Boost donné au départ et gardé plein pendant le décompte (33 comme en match).", 1);
    da.number("rlt_ko_ball_height", &KickoffConfig::ballHeight, kKoBallRestZ, 85.f, 100.f, "Hauteur de la balle (uu)",
              "Hauteur de la balle posée au centre. Ne change que si la balle saute au départ.", 2);

    auto gh = schema.section(&Settings::kickoff, "kickoff", "Fantôme");
    gh.choice("rlt_ko_ghost", &KickoffConfig::ghostLevel, static_cast<int>(KickoffGhostMode::Adaptive), ghosts,
              "Adversaire fantôme",
              "Une voiture fantôme part de la position miroir. Adaptatif (conseillé) : il part au niveau Débutant, "
              "accélère quand tu gagnes et ralentit quand tu perds. Mon record : ton meilleur essai sur cette "
              "position.");
    gh.toggle("rlt_ko_ghost_hits_ball", &KickoffConfig::ghostHitsBall, true, "Le fantôme frappe la balle",
              "Quand le fantôme gagne ou fait match nul, la balle part comme s'il l'avait vraiment touchée.");
    gh.color("rlt_ko_ghost_color", &KickoffConfig::ghostColor, Color(120, 200, 255, 230), "Couleur du fantôme",
             "Couleur de la boîte du fantôme.");
    auto ga = schema.section(&Settings::kickoff, "kickoff", "Fantôme");
    ga.advanced();
    ga.number("rlt_ko_ghost_impulse", &KickoffConfig::ghostImpulseScale, 1.f, 0.f, 1.5f,
              "Force de la frappe du fantôme", "Multiplie la vitesse donnée à la balle par le fantôme.", 2);
    ga.number("rlt_ko_duel_window", &KickoffConfig::duelWindow, 0.10f, 0.03f, 0.30f, "Avance décisive (s)",
              "Écart d'arrivée au-delà duquel la première voiture gagne d'office le 50/50.", 2);
    ga.number("rlt_ko_duel_neutral", &KickoffConfig::duelNeutralBand, 0.06f, 0.f, 0.3f, "Marge du 50/50 neutre",
              "Différence de puissance (en proportion) considérée comme un 50/50 neutre.", 2);
    ga.number("rlt_ko_adaptive_target", &KickoffConfig::adaptiveTarget, 0.6f, 0.5f, 0.9f,
              "Victoires visées (adaptatif)",
              "Proportion de kickoffs gagnés vers laquelle le fantôme adaptatif se règle.", 2);

    auto n = schema.section(&Settings::kickoff, "kickoff", "Notation et retours");
    n.choice("rlt_ko_grade_target", &KickoffConfig::gradeTarget, static_cast<int>(KickoffLevel::Standard), levels,
             "Niveau de référence",
             "Le temps et la vitesse sont notés par rapport à ce niveau. Standard : kickoff avec des flips. Pro et "
             "Élite : speedflip (avancé).");
    n.toggle("rlt_ko_chat", &KickoffConfig::chatFeedback, true, "Résultat dans le chat",
             "Une ligne par essai : temps, note et le conseil principal.");
    n.toggle("rlt_ko_record_toasts", &KickoffConfig::recordToasts, true, "Notifications des records",
             "Notification quand tu bats ton record sur une position ou termines une série.");
    n.toggle("rlt_ko_passive", &KickoffConfig::passiveAnalysis, true, "Analyser mes kickoffs hors exercice",
             "Note aussi les kickoffs après un Reset en freeplay et en match hors ligne (sans toucher au jeu).");

    auto h = schema.section(&Settings::kickoff, "kickoff", "Affichage");
    h.toggle("rlt_ko_hud", &KickoffConfig::hudEnabled, true, "Panneau en jeu",
             "Position, décompte, chrono, temps intermédiaires et dernier résultat.");
    h.number("rlt_ko_hud_x", &KickoffConfig::hudX, 0.01f, 0.f, 1.f, "Position X du panneau",
             "Fraction de la largeur de l'écran (0 = bord gauche).", 2);
    h.number("rlt_ko_hud_y", &KickoffConfig::hudY, 0.12f, 0.f, 1.f, "Position Y du panneau",
             "Fraction de la hauteur de l'écran (0 = haut).", 2);
    h.number("rlt_ko_hud_scale", &KickoffConfig::hudScale, 1.f, 0.5f, 2.f, "Taille du panneau",
             "Multiplie la taille du panneau, en plus de la taille générale du HUD.", 2);
    h.toggle("rlt_ko_big_countdown", &KickoffConfig::bigCountdown, true, "Grand décompte",
             "Affiche 3, 2, 1, GO en grand au milieu de l'écran.");
    h.toggle("rlt_ko_show_path", &KickoffConfig::showPath, true, "Ligne vers la balle",
             "Trace la ligne droite de ta position à la balle, avec les repères 25/50/75 %.");
    h.toggle("rlt_ko_show_splits", &KickoffConfig::showSplits, true, "Temps intermédiaires",
             "Compare ton temps au fantôme à 25, 50, 75 et 100 % du trajet.");
    h.color("rlt_ko_path_color", &KickoffConfig::pathColor, Color(255, 200, 60, 170), "Couleur de la ligne",
            "Couleur de la ligne vers la balle et des repères.");
    auto ha = schema.section(&Settings::kickoff, "kickoff", "Affichage");
    ha.advanced();
    ha.number("rlt_ko_countdown_y", &KickoffConfig::countdownY, 0.30f, 0.05f, 0.9f, "Position Y du grand décompte",
              "Fraction de la hauteur de l'écran.", 2);
}

void KickoffModule::onSettingsChanged(const Settings& settings) {
    cfg_ = settings.kickoff;
    if (cfg_.ghostLevel != lastGhostLevelCfg_) {
        ghostOverride_.reset();  // the player picked a level in the settings: it wins over the console command
        lastGhostLevelCfg_ = cfg_.ghostLevel;
    }
    staircase_.setTarget(cfg_.adaptiveTarget);
}

KickoffGhostMode KickoffModule::effectiveGhostMode() const {
    if (ghostOverride_) return *ghostOverride_;
    return static_cast<KickoffGhostMode>(std::clamp(cfg_.ghostLevel, 0, kKoGhostModeCount - 1));
}

KickoffDuelParams KickoffModule::duelParams() const {
    KickoffDuelParams p;
    p.clearWindow = cfg_.duelWindow;
    p.neutralBand = cfg_.duelNeutralBand;
    return p;
}

KickoffRecorderParams KickoffModule::recorderParams(const PlayerProfile& profile) const {
    KickoffRecorderParams p;
    if (profile.valid) p.dodgeDeadzone = clampf(profile.dodgeDeadzone, 0.1f, 0.95f);
    // Only a pickup since the last PLACE (drill) or a recent passive attempt can leave a pad respawning.
    if (lastPadFrame_ >= 0) p.padsBusyUntilFrame = lastPadFrame_ + kPadRespawnTicks;
    return p;
}

void KickoffModule::notify(Actions& out, const std::string& text, ToastKind kind) {
    message_ = text;
    ToastMessage t;
    t.title = "Kickoff";
    t.text = text;
    t.kind = kind;
    out.toasts.push_back(std::move(t));
}

// The previous version defaulted to the Pro ghost and Pro grading, saved in config.cfg by BakkesMod. A player who
// still has exactly that pair never chose it: move them once to the beginner-friendly defaults.
void KickoffModule::migrateDefaults(Actions& out) {
    defaultsV2_ = true;
    out.requestSave = true;
    if (cfg_.ghostLevel != static_cast<int>(KickoffGhostMode::Pro) ||
        cfg_.gradeTarget != static_cast<int>(KickoffLevel::Pro))
        return;
    out.setCvars.emplace_back("rlt_ko_ghost", std::to_string(static_cast<int>(KickoffGhostMode::Adaptive)));
    out.setCvars.emplace_back("rlt_ko_grade_target", std::to_string(static_cast<int>(KickoffLevel::Standard)));
    ToastMessage t;
    t.title = "Kickoff";
    t.text = "Fantôme Adaptatif et notation Standard, plus adaptés pour progresser. Tu peux les changer dans la "
             "page Kickoff.";
    t.kind = ToastKind::Info;
    t.seconds = 8.f;
    out.toasts.push_back(std::move(t));
}

// ---- Tick ----------------------------------------------------------------------------------------------------
void KickoffModule::onTick(const TickContext& ctx, Actions& out) {
    if (contextKnown_ && (ctx.game.mode != lastMode_ || ctx.game.mapName != lastMap_)) resetTransient();
    lastMode_ = ctx.game.mode;
    lastMap_ = ctx.game.mapName;
    contextKnown_ = true;
    if (!ctx.hasSnapshot) return;
    if (!defaultsV2_) migrateDefaults(out);  // ticks only come from a running game: config.cfg is loaded
    const Snapshot& sn = ctx.snap;
    if (sn.frame == lastTickFrame_) return;
    if (sn.frame < lastTickFrame_) resetTransient();  // physics frame counter restarted (new map)
    lastTickFrame_ = sn.frame;

    if (!running_) {
        if (cfg_.enabled && cfg_.passiveAnalysis) tickPassive(ctx, out);
        else resetPassive();
        return;
    }
    if (!cfg_.enabled) {
        stopDrill(true, "Module Kickoff désactivé : exercice arrêté.");
        return;
    }
    if (!ctx.worldControlAllowed || !ctx.ownsActivity) {
        stopDrill(true, "Exercice arrêté : le plugin ne peut plus déplacer la voiture ici.");
        return;
    }
    switch (phase_) {
    case KickoffPhase::Placing: placeRep(ctx, out); break;
    case KickoffPhase::Countdown: tickCountdown(ctx, out); break;
    case KickoffPhase::Live:
    case KickoffPhase::Done: tickLive(ctx, out); break;
    default: break;
    }
}

bool KickoffModule::startDrill(const TickContext& ctx, Actions& out, KickoffMode mode,
                               std::optional<KickoffSpawn> spawn) {
    if (!cfg_.enabled) {
        notify(out, "Active d'abord le module Kickoff dans ses réglages.", ToastKind::Warning);
        return false;
    }
    const MapCategory cat = ctx.game.category;
    if (cat != MapCategory::Standard && cat != MapCategory::Other) {
        notify(out, "Le kickoff se travaille sur un terrain standard, pas sur cette map.", ToastKind::Warning);
        return false;
    }
    if (!ctx.worldControlAllowed || !requestActivity("Kickoff")) {
        notify(out, "Exercice indisponible ici : lance le freeplay (ou autorise les exercices dans Réglages > Avancé).",
               ToastKind::Warning);
        return false;
    }
    running_ = true;
    mode_ = mode;
    chosenSpawn_ = spawn ? *spawn : kickoffSpawnFromIndex(cfg_.spawn);
    sequenceDone_ = 0;
    sequenceScores_.clear();
    repeatSpawn_.reset();
    forceNext_ = false;
    rng_.reseed(mix64(static_cast<uint64_t>(ctx.snap.frame) + static_cast<uint64_t>(sessionAttempts_) * 7919u));
    resetPassive();
    resetRepState();
    phase_ = KickoffPhase::Placing;
    message_ = std::string("Kickoff lancé (") + kickoffModeName(mode_) + ").";
    return true;
}

void KickoffModule::stopDrill(bool release, const std::string& message) {
    if (release) releaseActivity();
    running_ = false;
    phase_ = KickoffPhase::Idle;
    resetRepState();
    ghostActive_ = false;
    forceNext_ = false;
    message_ = message;
}

void KickoffModule::onActivityStopped() {
    // Another drill took over, or world control is gone: forget the rep (the freeze stops with our ticks).
    running_ = false;
    phase_ = KickoffPhase::Idle;
    resetRepState();
    ghostActive_ = false;
    forceNext_ = false;
    message_ = "Exercice kickoff interrompu.";
}

void KickoffModule::notePadPickups() {
    if (recorder_.started()) lastPadFrame_ = std::max(lastPadFrame_, recorder_.metrics().lastPadFrame);
}

void KickoffModule::resetRepState() {
    notePadPickups();
    recorder_.reset();
    attemptDone_ = false;
    waitingForNext_ = false;
    ghostResolved_ = false;
    duel_.reset();
    liveSplits_ = {};
    haveLastCar_ = false;
    invalidTicks_ = 0;
    fedFrame_ = -1;
}

void KickoffModule::resetTransient() {
    if (running_) stopDrill(true, "Exercice arrêté : la map a changé.");
    resetPassive();
    lastTickFrame_ = -1;
    lastPadFrame_ = -1;  // new map or restarted frame counter: old pickup frames mean nothing
}

KickoffSpawn KickoffModule::pickSpawn() {
    if (repeatSpawn_) {
        const KickoffSpawn s = *repeatSpawn_;
        repeatSpawn_.reset();
        return s;
    }
    switch (mode_) {
    case KickoffMode::Chosen: return chosenSpawn_;
    case KickoffMode::Sequence: return kickoffSpawnFromIndex(sequenceDone_ % kKoSpawnCount);
    case KickoffMode::Weakness: return stats_.weakest();
    default: return kickoffSpawnFromIndex(rng_.rangeInt(0, kKoSpawnCount - 1));
    }
}

void KickoffModule::setupGhost(KickoffGhostMode mode) {
    repGhostMode_ = mode;
    ghostActive_ = mode != KickoffGhostMode::Off;
    ghostFallback_ = false;
    const KickoffLevel target = kickoffLevelFromIndex(cfg_.gradeTarget);
    KickoffGhostProfile prof;
    switch (mode) {
    case KickoffGhostMode::PersonalBest:
        if (!stats_.personalBestProfile(spawn_, prof)) {
            // No record on this spawn yet: the reference level stands in (Standard by default).
            prof = KickoffGhostProfile::fromBenchmark(kickoffBenchmark(spawn_, target));
            ghostFallback_ = true;
        }
        break;
    case KickoffGhostMode::Adaptive:
        prof = KickoffGhostProfile::fromBenchmark(kickoffBenchmarkAt(kickoffKind(spawn_), staircase_.level()));
        break;
    case KickoffGhostMode::Off: prof = KickoffGhostProfile::fromBenchmark(kickoffBenchmark(spawn_, target)); break;
    default: prof = KickoffGhostProfile::fromBenchmark(kickoffBenchmark(spawn_, ghostLevel(mode))); break;
    }
    if (ghostActive_) {
        ghost_.init(spawn_, kickoffOpponent(team_), prof);
        splitRef_ = ghost_;
    } else {
        splitRef_.init(spawn_, team_, prof);
    }
}

void KickoffModule::placeRep(const TickContext& ctx, Actions& out) {
    const Snapshot& sn = ctx.snap;
    resetRepState();
    team_ = cfg_.team == 1 ? KickoffTeam::Orange : KickoffTeam::Blue;
    spawn_ = pickSpawn();
    const KickoffPose pose = kickoffPose(spawn_, team_, kKoCarPlaceZ);
    CarPlacement car;
    car.pos = pose.pos;
    car.rot = pose.rot;
    car.boost = cfg_.spawnBoost;
    out.setCar = car;
    out.setBall = restingBall(cfg_.ballHeight);
    out.setBoost = cfg_.spawnBoost;
    // Match conditions for every rep (placeRep only runs while the drill owns the activity): limited boost (the
    // freeplay "unlimited boost" would falsify the times; the engine puts the player's rules back when the activity
    // ends) and every boost pad respawned, so pickups made before this tick no longer matter.
    out.boostRules = BoostRules{false, false};
    out.resetPickups = true;
    lastPadFrame_ = -1;
    // Zero the inputs without freezing on this tick: the placement is applied before the car is frozen.
    out.overrideInputs = Inputs{};
    const int countdown = std::max(1, cfg_.countdownSeconds);
    placeFrame_ = sn.frame;
    nominalGo_ = placeFrame_ + static_cast<int64_t>(countdown) * 120;
    int jitter = 0;
    if (cfg_.goJitter && cfg_.goJitterMax > 0.f)
        jitter = static_cast<int>(std::lround(rng_.range(0.f, cfg_.goJitterMax) * kTickRate));
    goFrame_ = nominalGo_ + jitter;
    if (cfg_.nativeCountdown) out.countdownMessage = countdown;
    setupGhost(effectiveGhostMode());
    phase_ = KickoffPhase::Countdown;
}

void KickoffModule::tickCountdown(const TickContext& ctx, Actions& out) {
    const Snapshot& sn = ctx.snap;
    if (sn.frame >= goFrame_) {
        enterLive(ctx, out);
        return;
    }
    out.freezeCar = true;
    const KickoffPose pose = kickoffPose(spawn_, team_, kKoCarPlaceZ);
    const bool drifted = !sn.car.valid || length2D(sn.car.pos - pose.pos) > 30.f;
    if (cfg_.holdCar || drifted) {
        CarPlacement car;
        car.pos = pose.pos;
        // Keep the height the car settled at (rest height depends on the car body).
        if (sn.car.valid && !drifted) car.pos.z = clampf(sn.car.pos.z, kKoCarRestZ - 3.f, kKoCarPlaceZ);
        car.rot = pose.rot;
        car.boost = cfg_.spawnBoost;
        out.setCar = car;
    }
    out.setBoost = cfg_.spawnBoost;  // held boost drains even while frozen
    out.setBall = restingBall(cfg_.ballHeight);
    const int64_t left = nominalGo_ - sn.frame;  // messages follow the nominal schedule; GO may come later
    if (cfg_.nativeCountdown && left > 0 && left % 120 == 0) {
        const int n = static_cast<int>(left / 120);
        if (n <= std::max(1, cfg_.countdownSeconds)) out.countdownMessage = n;
    }
}

void KickoffModule::enterLive(const TickContext& ctx, Actions& out) {
    const Snapshot& sn = ctx.snap;
    phase_ = KickoffPhase::Live;
    goFrame_ = sn.frame;
    // No freeze from this tick on: the inputs the player already holds act on this very tick.
    out.setBoost = cfg_.spawnBoost;
    if (cfg_.nativeCountdown) out.goMessage = true;
    recorder_.begin(spawn_, team_, goFrame_, recorderParams(ctx.profile));
    fedFrame_ = goFrame_ - 1;
    haveLastCar_ = sn.car.valid;
    lastCarPos_ = sn.car.pos;
    lastCarFrame_ = sn.frame;
    invalidTicks_ = 0;
}

bool KickoffModule::carTeleported(const Snapshot& sn) {
    if (!sn.car.valid) return ++invalidTicks_ > 120;  // car gone for a second (demolished, despawned)
    invalidTicks_ = 0;
    bool jumped = false;
    if (haveLastCar_) {
        const float gap = static_cast<float>(std::clamp<int64_t>(sn.frame - lastCarFrame_, 1, 60));
        const float limit = std::max(3.f * length(sn.car.vel) * kTickDt * gap + 60.f, 250.f);
        jumped = distance(sn.car.pos, lastCarPos_) > limit;
    }
    lastCarPos_ = sn.car.pos;
    lastCarFrame_ = sn.frame;
    haveLastCar_ = true;
    return jumped;
}

bool KickoffModule::headingToBall(const Snapshot& sn) const {
    if (!sn.car.valid || !recorder_.started()) return false;
    const KickoffPath& p = recorder_.path();
    return dot(sn.car.vel, p.dir) > 300.f && p.along(sn.car.pos) > 0.25f * p.length;
}

void KickoffModule::restartRep() {
    repeatSpawn_ = spawn_;
    resetRepState();
    phase_ = KickoffPhase::Placing;
    message_ = "Essai relancé.";
}

void KickoffModule::feedUpTo(const TickHistory& history, int64_t frame) {
    if (!recorder_.started()) return;
    int64_t f = std::max(fedFrame_, recorder_.lastFrame());
    for (int guard = 0; guard < 1200; ++guard) {
        const Snapshot* s = history.after(f);
        if (!s || s->frame > frame) break;
        recorder_.feed(*s);
        f = s->frame;
    }
    fedFrame_ = f;
}

void KickoffModule::updateLiveSplits() {
    const auto& along = recorder_.metrics().along;
    if (along.empty()) return;
    const float len = recorder_.path().length;
    const float last = along.back();
    for (size_t k = 0; k < 3; ++k) {
        KickoffSplit& s = liveSplits_[k];
        s.fraction = 0.25f * static_cast<float>(k + 1);
        if (s.reached || last < s.fraction * len) continue;
        s.playerTime = kickoffTimeAtDistance(along, s.fraction * len);
        s.refTime = splitRef_.timeAt(s.fraction * splitRef_.path().length);
        s.reached = s.playerTime >= 0.f;
        s.delta = s.reached ? s.playerTime - s.refTime : 0.f;
    }
    liveSplits_[3].fraction = 1.f;
}

void KickoffModule::tickLive(const TickContext& ctx, Actions& out) {
    const Snapshot& sn = ctx.snap;
    const float t = static_cast<float>(sn.frame - goFrame_) * kTickDt;
    if (!attemptDone_) {
        if (carTeleported(sn)) {  // freeplay reset, demolition: redo the same spawn
            restartRep();
            return;
        }
        feedUpTo(ctx.history, sn.frame - 2);  // touches are resolved two ticks late: stay behind them
        updateLiveSplits();
    }
    tickGhost(ctx, out, t);
    // Touches are resolved two ticks late: leave them that margin before declaring the ball missed.
    if (!attemptDone_ && t >= cfg_.timeout + 2.f * kTickDt) finalize(ctx, out, false, std::nullopt);
    if (attemptDone_) {
        const bool force = forceNext_;
        forceNext_ = false;
        advanceAfterResult(ctx, out, force);
    }
}

void KickoffModule::tickGhost(const TickContext& ctx, Actions& out, float t) {
    if (!ghostActive_ || ghostResolved_) return;
    const float ga = ghost_.arrivalTime();
    if (t < ga) return;
    const Snapshot& sn = ctx.snap;
    const KickoffDuelParams prm = duelParams();
    const KickoffGhostState g = ghost_.stateAt(ga);
    const bool canHit = cfg_.ghostHitsBall && sn.ball.valid;
    const KickoffMetrics& m = recorder_.metrics();
    if (m.touched) {
        ghostResolved_ = true;
        if (ga - m.timeToTouch >= prm.clearWindow || !canHit || !duel_) return;  // player clearly first: ghost too late
        const Vec3 atRest = kickoffGhostBallImpulse(g.pos, g.vel, sn.ball.pos, Vec3{});
        const Vec3 nudge =
            kickoffDuelBallImpulse(duel_->outcome, sn.ball.vel, ghost_.path().dir, atRest) * cfg_.ghostImpulseScale;
        if (length(nudge) > 1.f) out.setBall = BallPlacement{sn.ball.pos, sn.ball.vel + nudge, sn.ball.angVel};
        return;
    }
    if (attemptDone_ || !canHit) {
        // The ball is left alone: the real touch (or the timeout) is graded, the duel follows from the times.
        ghostResolved_ = true;
        return;
    }
    // No touch known yet (touches arrive two ticks late): estimate the player's arrival.
    float tEst = 1e9f;
    if (sn.car.valid) {
        const KickoffPath& p = recorder_.path();
        const float remaining = std::max(0.f, p.length - p.along(sn.car.pos));
        const float v = dot(sn.car.vel, p.dir);
        if (v > 50.f) tEst = t + remaining / v;
    }
    const float deadline = ga + prm.clearWindow + 2.f * kTickDt;
    if (tEst - ga < prm.clearWindow && t < deadline) return;  // close race: wait for the player's touch
    // The ghost wins outright: it hits the ball now and the player's arrival is estimated.
    ghostResolved_ = true;
    const Vec3 imp = kickoffGhostBallImpulse(g.pos, g.vel, sn.ball.pos, sn.ball.vel, cfg_.ghostImpulseScale);
    if (length(imp) > 1.f) out.setBall = BallPlacement{sn.ball.pos, sn.ball.vel + imp, sn.ball.angVel};
    finalize(ctx, out, false, KickoffDuel::GhostClear);
}

void KickoffModule::advanceAfterResult(const TickContext& ctx, Actions& out, bool force) {
    if (!force) {
        if (waitingForNext_) return;
        if (ctx.snap.frame - doneFrame_ < static_cast<int64_t>(std::lround(cfg_.resetDelay * kTickRate))) return;
    }
    if (mode_ == KickoffMode::Sequence && sequenceDone_ >= kKoSpawnCount) {
        finishSeries(out);
        return;
    }
    if (!force && !cfg_.autoRepeat) {
        waitingForNext_ = true;
        return;
    }
    placeRep(ctx, out);
}

void KickoffModule::finishSeries(Actions& out) {
    float sum = 0.f;
    int best = 0;
    for (int s : sequenceScores_) {
        sum += static_cast<float>(s);
        best = std::max(best, s);
    }
    const int n = static_cast<int>(sequenceScores_.size());
    const int avg = n > 0 ? static_cast<int>(std::lround(sum / static_cast<float>(n))) : 0;
    const std::string text = "Série terminée : moyenne " + std::to_string(avg) + "/100 sur " + std::to_string(n) +
                             " kickoffs (meilleur " + std::to_string(best) + ").";
    if (cfg_.recordToasts) {
        ToastMessage t;
        t.title = "Kickoff";
        t.text = text;
        t.kind = ToastKind::Ok;
        t.seconds = 5.f;
        out.toasts.push_back(std::move(t));
    }
    if (cfg_.chatFeedback) out.chat.push_back({text, "RL Trainer"});
    stopDrill(true, text);
}

void KickoffModule::finalize(const TickContext& ctx, Actions& out, bool passive,
                             std::optional<KickoffDuel> forcedOutcome) {
    const Snapshot& sn = ctx.snap;
    // Ended by the opponent: estimate the arrival only when the car was really heading to the ball.
    if (forcedOutcome && !recorder_.touched() && headingToBall(sn)) recorder_.estimate(sn);
    const KickoffMetrics& m = recorder_.finish();
    std::optional<KickoffDuelResult> duel;
    std::string opponent;
    if (!passive && ghostActive_) {
        opponent = std::string("fantôme ") + kickoffGhostModeName(repGhostMode_);
        if (m.touched) {
            KickoffPlayerContact pc;
            pc.touched = true;
            pc.time = m.timeToTouch;
            pc.speedToBall = m.speedToBall;
            pc.lateralOffset = m.lateralOffset;
            pc.flipIntoBall = m.flipIntoBall;
            duel = kickoffResolveDuel(pc, ghost_.arrivalTime(), ghost_.contactSpeed(), duelParams());
        } else {
            KickoffDuelResult d;
            d.outcome = KickoffDuel::GhostClear;
            d.dt = m.estimated ? ghost_.arrivalTime() - m.timeToTouch : -99.f;
            d.ghostPower = ghost_.contactSpeed();
            duel = d;
        }
    } else if (forcedOutcome) {
        opponent = "adversaire";
        KickoffDuelResult d;
        d.outcome = *forcedOutcome;
        d.dt = -99.f;
        duel = d;
    }
    duel_ = duel;
    const KickoffDuelResult* dp = duel ? &*duel : nullptr;
    const KickoffLevel target = kickoffLevelFromIndex(cfg_.gradeTarget);
    const KickoffGrade g = kickoffGrade(m, target, dp);
    KickoffGhost bench;
    if (passive || !ghostActive_)
        bench.init(m.spawn, m.team, KickoffGhostProfile::fromBenchmark(kickoffBenchmark(m.spawn, target)));
    const std::vector<KickoffSplit> splits = kickoffSplits(m, (passive || !ghostActive_) ? bench : ghost_);
    for (size_t k = 0; k < splits.size() && k < liveSplits_.size(); ++k) liveSplits_[k] = splits[k];

    const float previousBest = stats_.of(m.spawn).bestTime;
    // Passive results never count as ghost duels (the "vs fantôme" win rate stays a drill statistic).
    // A run with unlimited boost is graded but never becomes a record (best time, "Mon record" ghost).
    const bool newPb = stats_.record(m, g.total, passive ? nullptr : dp, !m.boostLooksUnlimited);
    if (!passive && repGhostMode_ == KickoffGhostMode::Adaptive && dp) staircase_.record(kickoffDuelWon(dp->outcome));
    out.results.push_back(kickoffMakeResult(m, g, dp, target, sn.time));
    out.requestSave = true;

    last_ = KickoffResultView{};
    last_.valid = true;
    last_.passive = passive;
    last_.metrics = m;
    last_.metrics.along.clear();
    last_.metrics.along.shrink_to_fit();
    last_.grade = g;
    last_.hasDuel = duel.has_value();
    if (duel) last_.duel = *duel;
    last_.opponent = opponent;
    last_.newPb = newPb;
    last_.previousBest = previousBest;
    last_.splits = splits;
    last_.gameTime = sn.time;

    attemptDone_ = true;
    doneFrame_ = sn.frame;
    if (passive) return;

    phase_ = KickoffPhase::Done;
    ++sessionAttempts_;
    sessionScoreSum_ += g.total;
    if (mode_ == KickoffMode::Sequence) {
        ++sequenceDone_;
        sequenceScores_.push_back(g.total);
    }
    if (cfg_.chatFeedback) {
        std::string line = std::string("Kickoff ") + kickoffSpawnInfo(m.spawn).nameLower + " : ";
        if (m.touched) line += seconds(m.timeToTouch) + ", ";
        else if (m.estimated) line += "~" + seconds(m.timeToTouch) + " (estimé), ";
        else line += "balle pas touchée, ";
        line += std::to_string(g.total) + "/100 (" + verdictLabel(verdictFromScore(static_cast<float>(g.total))) + ")";
        if (duel) {
            std::string d = kickoffDuelName(duel->outcome);
            if (!d.empty()) d[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(d[0])));
            line += ", " + d;
        }
        if (newPb) line += ". Nouveau record !";
        else line += ".";
        if (m.boostLooksUnlimited) line += " Boost illimité : record non compté.";
        out.chat.push_back({line, "RL Trainer"});
        if (!g.tips.empty())
            out.chat.push_back({std::string("Conseil : ") + kickoffTipText(g.tips.front().tip), "RL Trainer"});
    }
    if (newPb && previousBest > 0.f && cfg_.recordToasts) {
        ToastMessage t;
        t.title = "Nouveau record kickoff !";
        t.text = std::string(kickoffSpawnInfo(m.spawn).name) + " : " + seconds(m.timeToTouch) + " (avant " +
                 seconds(previousBest) + ")";
        t.kind = ToastKind::Ok;
        out.toasts.push_back(std::move(t));
    }
}

// ---- Touches, events, commands -------------------------------------------------------------------------------
void KickoffModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) return;
    const bool drill = running_ && phase_ == KickoffPhase::Live && !attemptDone_;
    const bool passive = !running_ && passivePhase_ == PassivePhase::Live;
    if (!drill && !passive) return;
    if (!recorder_.started() || touch.pre.frame < recorder_.goFrame()) return;
    feedUpTo(ctx.history, touch.pre.frame);
    recorder_.touch(touch.pre, touch.post);
    if (!recorder_.touched()) return;
    finalize(ctx, out, passive, std::nullopt);
    if (passive) resetPassive();
}

void KickoffModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    (void)out;
    switch (ev.kind) {
    case GameEventKind::ContextChanged:
        if (contextKnown_ && (ctx.game.mode != lastMode_ || ctx.game.mapName != lastMap_)) resetTransient();
        lastMode_ = ctx.game.mode;
        lastMap_ = ctx.game.mapName;
        contextKnown_ = true;
        if (running_ && !ctx.worldControlAllowed)
            stopDrill(true, "Exercice arrêté : contrôle du jeu indisponible ici.");
        break;
    case GameEventKind::MapUnloaded: resetTransient(); break;
    case GameEventKind::ResetShot:
    case GameEventKind::CarSpawned:
        if (running_) {
            if (phase_ == KickoffPhase::Done) forceNext_ = true;  // "Reset" = next kickoff right away
            else if (phase_ == KickoffPhase::Live) restartRep();  // mid-attempt: redo the same spawn
            // Placing / Countdown: the countdown re-pins the car on the next tick anyway.
        } else if (passivePhase_ == PassivePhase::Live) {
            resetPassive();
        }
        break;
    default: break;
    }
}

void KickoffModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    const std::string name = lower(cmd.name);
    const std::string arg = cmd.args.empty() ? std::string() : lower(cmd.args[0]);
    if (name == "start") {
        KickoffMode mode = static_cast<KickoffMode>(std::clamp(cfg_.spawnMode, 0, 3));
        std::optional<KickoffSpawn> spawn;
        if (!arg.empty()) {
            KickoffSpawn s;
            if (kickoffSpawnFromKey(arg, s)) {
                mode = KickoffMode::Chosen;
                spawn = s;
            } else if (arg == "random" || arg == "aleatoire") {
                mode = KickoffMode::Random;
            } else if (arg == "sequence" || arg == "serie") {
                mode = KickoffMode::Sequence;
            } else if (arg == "weakness" || arg == "faiblesse") {
                mode = KickoffMode::Weakness;
            } else if (arg == "chosen") {
                mode = KickoffMode::Chosen;
            } else {
                notify(out,
                       "Position inconnue : " + cmd.args[0] +
                           " (diagonal_left, diagonal_right, offcenter_left, offcenter_right, back, random, "
                           "sequence, weakness).",
                       ToastKind::Warning);
                return;
            }
        }
        startDrill(ctx, out, mode, spawn);
    } else if (name == "stop") {
        if (running_) stopDrill(true, "Exercice kickoff arrêté.");
    } else if (name == "next") {
        if (!running_) return;
        if (phase_ == KickoffPhase::Done) {
            forceNext_ = true;
        } else {
            resetRepState();
            phase_ = KickoffPhase::Placing;
        }
    } else if (name == "ghost") {
        KickoffGhostMode g;
        if (arg.empty() || !kickoffGhostModeFromKey(arg, g)) {
            notify(out, "Fantôme inconnu : pro, elite, standard, beginner, pb, adaptive ou off.", ToastKind::Warning);
            return;
        }
        ghostOverride_ = g;
        notify(out,
               std::string("Fantôme : ") + kickoffGhostModeName(g) + (running_ ? " (dès le prochain essai)." : "."),
               ToastKind::Info);
    } else if (name == "reset_stats") {
        stats_.reset();
        staircase_ = Staircase(cfg_.adaptiveTarget, kKoAdaptiveStep, kKoAdaptiveStart);
        last_ = KickoffResultView{};
        sessionAttempts_ = 0;
        sessionScoreSum_ = 0;
        out.requestSave = true;
        notify(out, "Statistiques kickoff remises à zéro.", ToastKind::Info);
    } else {
        notify(out, "Commande kickoff inconnue : " + cmd.name, ToastKind::Warning);
    }
}

// ---- Passive analysis ----------------------------------------------------------------------------------------
void KickoffModule::resetPassive() {
    if (passivePhase_ == PassivePhase::Live) {
        notePadPickups();
        recorder_.reset();
    }
    passivePhase_ = PassivePhase::Waiting;
    ballMovingTicks_ = 0;
}

void KickoffModule::tickPassive(const TickContext& ctx, Actions& out) {
    const GameMode mode = ctx.game.mode;
    const bool supported =
        (mode == GameMode::Freeplay || mode == GameMode::OfflineMatch) && ctx.game.category == MapCategory::Standard;
    const Snapshot& sn = ctx.snap;
    if (!supported || !sn.car.valid || !sn.ball.valid) {
        resetPassive();
        return;
    }
    if (passivePhase_ == PassivePhase::Live) {
        if (carTeleported(sn)) {
            resetPassive();
            return;
        }
        feedUpTo(ctx.history, sn.frame - 2);
        const float t = static_cast<float>(sn.frame - goFrame_) * kTickDt;
        ballMovingTicks_ = length(sn.ball.vel) > 150.f ? ballMovingTicks_ + 1 : 0;
        if (ballMovingTicks_ >= 4) {
            // The ball left the centre without a touch of ours (it would have been reported by now): the opponent
            // was first. Graded only when we were going for the ball, otherwise it was not our kickoff.
            if (headingToBall(sn)) finalize(ctx, out, true, KickoffDuel::GhostClear);
            resetPassive();
            return;
        }
        if (t >= cfg_.timeout) resetPassive();  // never reached the ball: nothing to report
        return;
    }
    // Waiting / Armed: ball at rest on the centre spot and car at rest on a kickoff spawn.
    const bool ballReady =
        length2D(sn.ball.pos) < 20.f && std::fabs(sn.ball.pos.z - cfg_.ballHeight) < 10.f && length(sn.ball.vel) < 5.f;
    KickoffSpawn s = KickoffSpawn::BackCenter;
    KickoffTeam tm = KickoffTeam::Blue;
    const bool atSpawn = ballReady && kickoffFindSpawn(sn.car.pos, 60.f, s, tm);
    const float v = length2D(sn.car.vel);  // horizontal: a car settling on its wheels moves vertically
    if (ballReady && atSpawn && v < 10.f) {
        passivePhase_ = PassivePhase::Armed;
        passiveSpawn_ = s;
        passiveTeam_ = tm;
        return;
    }
    if (passivePhase_ == PassivePhase::Armed && ballReady && v >= 10.f) {
        // The car started to move: GO was the previous tick.
        passivePhase_ = PassivePhase::Live;
        goFrame_ = sn.frame - 1;
        recorder_.begin(passiveSpawn_, passiveTeam_, goFrame_, recorderParams(ctx.profile));
        fedFrame_ = goFrame_ - 1;
        attemptDone_ = false;
        ballMovingTicks_ = 0;
        haveLastCar_ = true;
        lastCarPos_ = sn.car.pos;
        lastCarFrame_ = sn.frame;
        invalidTicks_ = 0;
        feedUpTo(ctx.history, sn.frame - 1);
        return;
    }
    passivePhase_ = PassivePhase::Waiting;
}

// ---- Accessors, persistence ----------------------------------------------------------------------------------
float KickoffModule::liveTimeAt(int64_t frame) const {
    if (phase_ != KickoffPhase::Live && phase_ != KickoffPhase::Done) return 0.f;
    const KickoffMetrics& m = recorder_.metrics();
    if (recorder_.started() && (m.touched || m.estimated)) return m.timeToTouch;
    if (attemptDone_) return static_cast<float>(doneFrame_ - goFrame_) * kTickDt;
    return std::max(0.f, static_cast<float>(frame - goFrame_) * kTickDt);
}

KickoffStatusView KickoffModule::status() const {
    KickoffStatusView v;
    v.enabled = cfg_.enabled;
    v.running = running_;
    v.phase = phase_;
    v.mode = mode_;
    v.spawn = spawn_;
    v.team = team_;
    v.ghost = running_ && phase_ != KickoffPhase::Placing ? repGhostMode_ : effectiveGhostMode();
    v.ghostOverridden = ghostOverride_.has_value();
    v.ghostFallback = running_ && ghostFallback_;
    v.ghostArrive = running_ && ghostActive_ ? ghost_.arrivalTime() : 0.f;
    if (running_ && phase_ == KickoffPhase::Countdown) {
        const int64_t left = nominalGo_ - lastTickFrame_;
        v.countdown = std::clamp(static_cast<int>((left + 119) / 120), 1, std::max(1, cfg_.countdownSeconds));
    }
    v.liveTime = running_ ? liveTimeAt(lastTickFrame_) : 0.f;
    v.sequenceIndex = sequenceDone_;
    v.sessionAttempts = sessionAttempts_;
    v.sessionAvgScore =
        sessionAttempts_ > 0 ? static_cast<float>(sessionScoreSum_) / static_cast<float>(sessionAttempts_) : 0.f;
    v.adaptiveLevel = staircase_.level();
    v.message = message_;
    return v;
}

std::array<KickoffSpawnStats, kKoSpawnCount> KickoffModule::spawnStats() const {
    std::array<KickoffSpawnStats, kKoSpawnCount> out;
    for (int i = 0; i < kKoSpawnCount; ++i) out[static_cast<size_t>(i)] = stats_.of(kickoffSpawnFromIndex(i));
    return out;
}

void KickoffModule::save(Store& store) const {
    stats_.save(store);
    staircase_.save(store, "kickoff.adaptive");
    store.setInt("kickoff.defaults_v2", defaultsV2_ ? 1 : 0);
}

void KickoffModule::load(const Store& store) {
    stats_.load(store);
    // The level is saved even when the adaptive ghost was never raced: only a played level is kept, so a player
    // who never used it starts at the beginner time.
    if (store.getInt("kickoff.adaptive.attempts", 0) > 0) staircase_.load(store, "kickoff.adaptive");
    defaultsV2_ = store.getInt("kickoff.defaults_v2", 0) != 0;
}

} // namespace rlt
