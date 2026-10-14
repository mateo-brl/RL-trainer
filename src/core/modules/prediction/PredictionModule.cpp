#include "core/modules/prediction/PredictionModule.h"

#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Text.h"

namespace rlt {

namespace {

constexpr const char* kKeyVersion = "prediction.version";
constexpr const char* kKeyCount = "prediction.acc.count";
constexpr const char* kKeyMean = "prediction.acc.mean";
constexpr const char* kKeyMax = "prediction.acc.max";
constexpr const char* kKeyLast = "prediction.acc.last";
constexpr const char* kKeyDisturbed = "prediction.acc.disturbed";
constexpr const char* kKeyRecent = "prediction.acc.recent";

std::string seconds1(float s) { return formatNumber(std::max(0.f, s), 1) + " s"; }

} // namespace

// ---- Settings ------------------------------------------------------------------------------------------
void PredictionModule::describeSettings(SettingsSchema& schema) {
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Prédiction");
        w.toggle("rlt_pr_enabled", &PredictionConfig::enabled, true, "Activer la prédiction",
                 "Affiche où va la balle : trajectoire, rebonds, point d'atterrissage, zone de première touche "
                 "et le meilleur moment pour l'intercepter.");
        w.number("rlt_pr_horizon", &PredictionConfig::horizon, 3.f, 1.f, 6.f, "Durée prédite (s)",
                 "Jusqu'où regarder dans le futur. 2 à 3 s suffisent pour lire un rebond, 5 à 6 s pour anticiper "
                 "un long dégagement.",
                 1);
        w.toggle("rlt_pr_only_airborne", &PredictionConfig::onlyAirborne, false, "Seulement quand la balle est en l'air",
                 "Masque tout quand la balle roule au sol : l'affichage n'apparaît que pour les balles hautes.");
        w.choice("rlt_pr_unit", &PredictionConfig::unit, 0, {"Auto (comme le jeu)", "km/h", "mph", "uu/s"},
                 "Unité de vitesse", "Unité du texte 'Arrive à ...'. Auto reprend le réglage métrique du jeu.");
        w.number("rlt_pr_text_scale", &PredictionConfig::textScale, 1.2f, 0.5f, 3.f, "Taille des textes",
                 "Taille des textes affichés sur le terrain (multipliée par la taille générale de l'interface).", 1);
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Trajectoire");
        w.toggle("rlt_pr_show_path", &PredictionConfig::showPath, true, "Afficher la trajectoire",
                 "Ligne qui suit le chemin futur de la balle.");
        w.color("rlt_pr_path_color", &PredictionConfig::pathColor, Color{80.f, 200.f, 255.f, 230.f},
                "Couleur de la trajectoire", "Couleur normale de la ligne (elle change si la balle va dans un but).");
        w.number("rlt_pr_line_width", &PredictionConfig::lineWidth, 2.f, 1.f, 6.f, "Épaisseur des lignes",
                 "Épaisseur de toutes les lignes de la prédiction.", 1);
        w.toggle("rlt_pr_fade", &PredictionConfig::fade, true, "S'estompe avec le temps",
                 "La ligne devient transparente vers la fin : ce qui arrive bientôt ressort davantage.");
        w.toggle("rlt_pr_time_markers", &PredictionConfig::showTimeMarkers, true, "Repères toutes les 0,5 s",
                 "Petits anneaux sur la trajectoire toutes les 0,5 s, avec les secondes écoulées.");
        w.toggle("rlt_pr_show_bounces", &PredictionConfig::showBounces, true, "Marquer les rebonds",
                 "Un cercle là où la balle touchera le sol, un mur ou le plafond.");
        w.color("rlt_pr_bounce_color", &PredictionConfig::bounceColor, Color{255.f, 220.f, 60.f, 230.f},
                "Couleur des rebonds", "Couleur des cercles de rebond.");
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Atterrissage et zone de touche");
        w.toggle("rlt_pr_show_landing", &PredictionConfig::showLanding, true, "Point d'atterrissage",
                 "Cercle au sol là où la balle retombera, avec le temps restant. Place-toi dessus pour la recevoir.");
        w.color("rlt_pr_landing_color", &PredictionConfig::landingColor, Color{255.f, 255.f, 255.f, 230.f},
                "Couleur de l'atterrissage", "Couleur du cercle d'atterrissage et de son texte.");
        w.toggle("rlt_pr_show_zone", &PredictionConfig::showZone, true, "Zone de première touche",
                 "Partie de la trajectoire où la balle redescend à une hauteur facile à toucher.");
        w.number("rlt_pr_zone_min", &PredictionConfig::zoneMin, 120.f, 93.f, 1000.f, "Zone : hauteur mini (uu)",
                 "Hauteur minimale du centre de la balle pour la zone de touche (93 = balle posée au sol).", 0);
        w.number("rlt_pr_zone_max", &PredictionConfig::zoneMax, 300.f, 100.f, 1800.f, "Zone : hauteur maxi (uu)",
                 "Hauteur maximale du centre de la balle pour la zone de touche. Monte-la si tu sautes pour "
                 "prendre la balle.",
                 0);
        w.color("rlt_pr_zone_color", &PredictionConfig::zoneColor, Color{255.f, 170.f, 40.f, 240.f},
                "Couleur de la zone",
                "Couleur du morceau de trajectoire à viser. Si elle ressemble trop à la couleur de la ligne (balle "
                "qui va au but), la zone passe en blanc pour rester visible.");
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Interception");
        w.toggle("rlt_pr_show_intercept", &PredictionConfig::showIntercept, true, "Conseiller d'interception",
                 "Montre le premier point où ta voiture peut atteindre la balle, dans combien de temps, et la vitesse "
                 "de la balle à ce moment ('Arrive à ...') pour caler ta vitesse sur la sienne.");
        w.color("rlt_pr_intercept_color", &PredictionConfig::interceptColor, Color{255.f, 90.f, 255.f, 240.f},
                "Couleur de l'interception", "Couleur du repère d'interception.");
        w.toggle("rlt_pr_intercept_boost", &PredictionConfig::interceptUseBoost, true, "Compter le boost",
                 "Le calcul suppose que tu utilises ton boost pour y aller. Décoche pour un calcul sans boost.");
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Alerte but");
        w.toggle("rlt_pr_goal_warning", &PredictionConfig::showGoalWarning, true, "Colorer si la balle va au but",
                 "La trajectoire change de couleur quand la balle va rentrer : vert dans le but adverse, rouge dans "
                 "le tien.");
        w.color("rlt_pr_own_goal_color", &PredictionConfig::ownGoalColor, Color{255.f, 60.f, 60.f, 240.f},
                "Couleur 'dans ton but'", "Couleur de la trajectoire quand la balle va rentrer dans ton but.");
        w.color("rlt_pr_opp_goal_color", &PredictionConfig::opponentGoalColor, Color{60.f, 255.f, 90.f, 240.f},
                "Couleur 'dans le but adverse'", "Couleur de la trajectoire quand la balle va marquer pour toi.");
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Panneau HUD");
        w.toggle("rlt_pr_panel", &PredictionConfig::showPanel, false, "Afficher le panneau",
                 "Petit panneau avec les chiffres (atterrissage, interception, but) même quand les repères sont hors "
                 "de l'écran.");
        w.number("rlt_pr_panel_x", &PredictionConfig::panelX, 0.01f, 0.f, 1.f, "Position X",
                 "Position horizontale du panneau (0 = bord gauche, 1 = bord droit).", 2);
        w.number("rlt_pr_panel_y", &PredictionConfig::panelY, 0.62f, 0.f, 1.f, "Position Y",
                 "Position verticale du panneau (0 = haut, 1 = bas).", 2);
    }
    {
        auto w = schema.section(&Settings::prediction, "prediction", "Avancé");
        w.advanced();
        w.integer("rlt_pr_recompute_ticks", &PredictionConfig::recomputeTicks, 4, 1, 12, "Recalcul tous les N ticks",
                  "Fréquence du recalcul (120 ticks par seconde). Plus petit = plus réactif mais plus coûteux.");
        w.number("rlt_pr_recompute_delta", &PredictionConfig::recomputeSpeedDelta, 50.f, 10.f, 500.f,
                 "Recalcul immédiat au-delà de (uu/s)",
                 "Si la vitesse de la balle s'écarte de la prédiction de plus que ça (touche, reset), on recalcule "
                 "tout de suite.",
                 0);
        w.integer("rlt_pr_draw_step", &PredictionConfig::drawStepTicks, 4, 1, 12, "Finesse de la ligne (ticks)",
                  "Un point de la ligne tous les N ticks. Plus petit = ligne plus lisse, plus de traits.");
        w.choice("rlt_pr_arena", &PredictionConfig::arenaMode, 0,
                 {"Automatique", "Terrain standard", "Sol seulement", "Sans collision"}, "Terrain simulé",
                 "Automatique : terrain standard sur les maps officielles, sol seul sur les maps workshop, aucune "
                 "collision sur les maps d'anneaux et d'air dribble.");
        w.number("rlt_pr_reach", &PredictionConfig::interceptReach, 150.f, 50.f, 300.f, "Portée de la voiture (uu)",
                 "Distance horizontale entre le centre de ta voiture et celui de la balle au moment du contact.", 0);
        w.number("rlt_pr_ground_reach", &PredictionConfig::groundReachHeight, 170.f, 93.f, 300.f,
                 "Hauteur touchable sans sauter (uu)",
                 "Au-dessus de cette hauteur (centre de la balle), le conseiller ajoute le temps d'un saut.", 0);
        w.number("rlt_pr_turn_penalty", &PredictionConfig::turnPenalty, 0.6f, 0.f, 2.f, "Temps perdu pour un demi-tour (s)",
                 "Temps ajouté quand ta voiture doit tourner pour viser la balle (proportionnel à l'angle).", 2);
    }
}

void PredictionModule::onSettingsChanged(const Settings& settings) {
    const PredictionConfig old = cfg_;
    cfg_ = settings.prediction;
    if (old.horizon != cfg_.horizon || old.zoneMin != cfg_.zoneMin || old.zoneMax != cfg_.zoneMax ||
        old.arenaMode != cfg_.arenaMode || old.interceptReach != cfg_.interceptReach ||
        old.groundReachHeight != cfg_.groundReachHeight || old.turnPenalty != cfg_.turnPenalty ||
        old.interceptUseBoost != cfg_.interceptUseBoost) {
        forceRecompute_ = true;
        viewPath_.valid = false;
    }
    if (!cfg_.enabled) invalidate();
}

void PredictionModule::onProfileChanged(const PlayerProfile& profile) { metric_ = profile.metric; }

// ---- Helpers -------------------------------------------------------------------------------------------
pr::ArenaMode PredictionModule::chooseArena(int setting, const GameContext& game, float ballRadius) {
    switch (setting) {
    case 1: return pr::ArenaMode::Soccar;
    case 2: return pr::ArenaMode::FloorOnly;
    case 3: return pr::ArenaMode::None;
    default: break;
    }
    // Hoops / Dropshot / big-ball modes use other arenas: keep the floor only.
    if (ballRadius > 95.f) return pr::ArenaMode::FloorOnly;
    if (!game.isCustomMap) return pr::ArenaMode::Soccar;
    if (game.category == MapCategory::Rings || game.category == MapCategory::AirDribble) return pr::ArenaMode::None;
    return pr::ArenaMode::FloorOnly;
}

int PredictionModule::goalRelation(int goalSide, int team) {
    if (goalSide == 0 || (team != 0 && team != 1)) return 0;
    const int attacked = team == 0 ? 1 : -1;  // blue attacks the orange goal at +Y
    return goalSide == attacked ? 1 : -1;
}

Color PredictionModule::goalColor(int goalSide, int team) const {
    const int rel = goalRelation(goalSide, team);
    if (rel > 0) return cfg_.opponentGoalColor;
    if (rel < 0) return cfg_.ownGoalColor;
    // Team unknown (replay): colour of the goal's own team.
    const Color c = goalSide > 0 ? colors::Orange : colors::Blue;
    return c.withAlpha(cfg_.pathColor.a);
}

const char* PredictionModule::unitLabel(SpeedUnit unit) {
    switch (unit) {
    case SpeedUnit::Mph: return "mph";
    case SpeedUnit::Uus: return "uu/s";
    default: return "km/h";
    }
}

std::string PredictionModule::formatBallSpeed(float uus, SpeedUnit unit) {
    if (!std::isfinite(uus)) uus = 0.f;
    return formatNumber(convertSpeed(uus, unit), 0) + " " + unitLabel(unit);
}

std::string PredictionModule::jumpHint(pr::JumpKind kind, const PlayerProfile& profile, KeyStyle style) {
    const std::string jumpKey = actionKeyLabel(profile, GameAction::Jump, style);
    switch (kind) {
    case pr::JumpKind::Jump: return actionPrompt(profile, GameAction::Jump, style);
    case pr::JumpKind::DoubleJump: return jumpKey.empty() ? "Double saut" : "Double saut (" + jumpKey + ")";
    case pr::JumpKind::Aerial: {
        const std::string boostKey = actionKeyLabel(profile, GameAction::Boost, style);
        if (jumpKey.empty() || boostKey.empty()) return "Aérien (saut + boost)";
        return "Aérien (" + jumpKey + " + " + boostKey + ")";
    }
    default: return "";
    }
}

std::string PredictionModule::goalLabel(int goalSide, int team) {
    const int rel = goalRelation(goalSide, team);
    if (rel > 0) return "But pour toi";
    if (rel < 0) return "Contre ton camp";
    return goalSide > 0 ? "But orange" : "But bleu";
}

bool PredictionModule::colorsClose(const Color& a, const Color& b) {
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db < kCloseColorDistance * kCloseColorDistance;
}

Color PredictionModule::zoneColorOver(const Color& pathBase) const {
    if (!colorsClose(cfg_.zoneColor, pathBase)) return cfg_.zoneColor;
    return colors::White.withAlpha(cfg_.zoneColor.a);
}

bool PredictionModule::pathExpectsImpulse(const pr::PredictedPath& path, int ticks) {
    // posAt(ticks) is samples[ticks - 1]: look a little before and after (a bounce one or two ticks off is still
    // the model's own error).
    const int n = path.size();
    const int first = std::max(0, ticks - 4), last = std::min(n - 1, ticks + 2);
    for (int i = first; i <= last; ++i) {
        const pr::PathSample& smp = path.samples[static_cast<size_t>(i)];
        if (smp.flags & pr::kStepBounce) return true;
        if ((smp.flags & pr::kStepContact) && smp.normal.z < 0.98f) return true;  // wall, ramp, ceiling
    }
    return false;
}

void PredictionModule::invalidate() {
    path_.reset();
    intercept_ = {};
    probe_.active = false;
}

void PredictionModule::updateFrozen(const Snapshot& s, float arenaRadius) {
    if (hasPrevBall_ && s.frame > prevBallFrame_) {
        // Under gravity an airborne ball always moves and never keeps |v| < 1 uu/s for two ticks in a row: a ball
        // that stays put up there is held by the game (training pack waiting for the player's first move).
        const bool still = distance(s.ball.pos, prevBallPos_) < kFrozenMove && length(s.ball.vel) < kFrozenSpeed &&
                           pr::ballAirborne(s.ball.pos, s.ball.vel, arenaRadius);
        frozenTicks_ = still ? std::min(frozenTicks_ + 1, kFrozenTicks) : 0;
    } else if (!hasPrevBall_ || s.frame < prevBallFrame_) {
        frozenTicks_ = 0;
    }
    hasPrevBall_ = true;
    prevBallPos_ = s.ball.pos;
    prevBallFrame_ = s.frame;
    frozen_ = frozenTicks_ >= kFrozenTicks;
}

bool PredictionModule::localCarNearBall(const Snapshot& s) const {
    if (!s.car.valid) return false;
    const float halfDiag = 0.5f * length(s.car.hitboxExtent);
    return distance(hitboxCenter(s.car), s.ball.pos) < s.ball.radius + halfDiag + kLocalTouchMargin;
}

void PredictionModule::onForcedRecompute(const Snapshot& s, int elapsed, float dv, float dp) {
    ++forcedRecomputes_;
    if (!probe_.active) return;
    if (dp > kTeleportDistance) {
        probe_.active = false;  // teleport (reset, goal respawn): not a model error
        return;
    }
    if (dv <= cfg_.recomputeSpeedDelta || pathExpectsImpulse(path_, elapsed)) return;  // the model's own error
    // A sharp velocity change where the model predicted free flight or plain rolling: something hit the ball.
    probe_.active = false;
    if (localCarNearBall(s)) return;  // the local car: onTouch confirms it two ticks later, not a disturbance
    ++accDisturbed_;
    probeBlockedUntil_ = s.frame + kProbeCooldownTicks;  // let the ball calm down (a bot dribbling, a 50/50)
}

// ---- Tick ----------------------------------------------------------------------------------------------
void PredictionModule::onTick(const TickContext& ctx, Actions& out) {
    drawFramesSinceTick_ = 0;
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    lastTickFrame_ = s.frame;
    metric_ = ctx.profile.metric;
    if (s.car.valid) team_ = s.car.team;
    if (!cfg_.enabled || !s.ball.valid) {
        hasPrevBall_ = false;
        frozenTicks_ = 0;
        frozen_ = false;
        if (path_.valid || probe_.active) invalidate();
        return;
    }
    if (s.paused) return;  // keep the cache, the ball does not move
    paused_ = false;

    const float arenaRadius = pr::SimParams::forRadius(s.ball.radius).arenaRadius;
    updateFrozen(s, arenaRadius);
    if (frozen_) {
        // A hanging ball is not in free fall: show nothing (no fake landing or intercept) and measure nothing.
        if (path_.valid || probe_.active) invalidate();
        return;
    }
    if (pr::ballAirborne(s.ball.pos, s.ball.vel, arenaRadius)) lastAirborneFrame_ = s.frame;
    updateProbe(s, out);
    if (hidden_) return;  // nothing displayed: no need to predict

    bool need = forceRecompute_ || !path_.valid;
    if (!need) {
        const int64_t elapsed = s.frame - path_.frame;
        if (elapsed < 0 || elapsed > path_.size()) {
            need = true;  // history jump or a cache older than its own horizon: nothing to compare with
        } else if (elapsed > 0) {
            // Always compare with the cache first, even on a regular recompute tick: a touch landing on that tick
            // must still be classed (forced recompute, outside disturbance), the period is only the fallback.
            const int e = static_cast<int>(elapsed);
            const float dv = length(s.ball.vel - path_.velAt(e));
            const float dp = distance(s.ball.pos, path_.posAt(e));
            if (dv > cfg_.recomputeSpeedDelta || dp > kTeleportCheck) {
                need = true;
                onForcedRecompute(s, e, dv, dp);
            } else if (elapsed >= std::max(1, cfg_.recomputeTicks)) {
                need = true;
            }
        }
    }
    if (need) recompute(ctx);
}

void PredictionModule::recompute(const TickContext& ctx) {
    const Snapshot& s = ctx.snap;
    const pr::SimParams prm = pr::SimParams::forRadius(s.ball.radius);
    const pr::ArenaMode mode = chooseArena(cfg_.arenaMode, ctx.game, s.ball.radius);
    pr::SimBall start;
    start.pos = s.ball.pos;
    start.vel = s.ball.vel;
    start.angVel = s.ball.angVel;
    pr::simulatePath(start, cfg_.horizon, prm, mode, path_);
    path_.frame = s.frame;
    pr::analyzePath(path_, cfg_.zoneMin, cfg_.zoneMax);
    forceRecompute_ = false;
    ++recomputes_;

    intercept_ = {};
    if (path_.valid && s.car.valid && s.car.pos.z < kMaxInterceptCarZ) {
        pr::InterceptCar c;
        c.pos = s.car.pos;
        c.vel = s.car.vel;
        c.forward = s.car.basis.fwd;
        c.boost = s.car.boost;
        pr::InterceptParams ip;
        ip.reach = cfg_.interceptReach;
        ip.groundReachZ = cfg_.groundReachHeight;
        ip.turnPenalty = cfg_.turnPenalty;
        ip.useBoost = cfg_.interceptUseBoost;
        intercept_ = pr::findIntercept(path_, c, ip, reach_);
    }
    if (!probe_.active) startProbe();
}

// ---- Accuracy probe ------------------------------------------------------------------------------------
void PredictionModule::startProbe() {
    const int idx = kProbeTicks - 1;
    if (!path_.valid || idx >= path_.size() || path_.frame < probeBlockedUntil_) return;
    if (path_.goalIndex >= 0 && path_.goalIndex <= idx) return;
    probe_.active = true;
    probe_.startFrame = path_.frame;
    probe_.targetFrame = path_.frame + kProbeTicks;
    probe_.predicted = path_.samples[static_cast<size_t>(idx)].pos;
}

void PredictionModule::updateProbe(const Snapshot& s, Actions& out) {
    if (!probe_.active || s.frame < probe_.targetFrame) return;
    if (s.frame == probe_.targetFrame) {
        const float err = distance(s.ball.pos, probe_.predicted);
        if (err > kDisturbedError) ++accDisturbed_;
        else recordProbe(err, out);
    }
    probe_.active = false;  // a frame gap (pause, history jump) just drops it
}

void PredictionModule::recordProbe(float error, Actions& out) {
    ++accCount_;
    accSum_ += error;
    accMax_ = std::max(accMax_, error);
    accLast_ = error;
    accRecent_[static_cast<size_t>(accRecentHead_)] = error;
    accRecentHead_ = (accRecentHead_ + 1) % kAccuracyHistory;
    accRecentCount_ = std::min(accRecentCount_ + 1, kAccuracyHistory);
    if (accCount_ % 30 == 0) out.requestSave = true;
}

void PredictionModule::resetAccuracy() {
    accCount_ = 0;
    accSum_ = 0.0;
    accMax_ = accLast_ = 0.f;
    accDisturbed_ = 0;
    accRecent_.fill(0.f);
    accRecentCount_ = accRecentHead_ = 0;
    probe_.active = false;
    probeBlockedUntil_ = 0;
}

// ---- Events --------------------------------------------------------------------------------------------
void PredictionModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    forceRecompute_ = true;
    // The hit impulse is fully applied two ticks after `pre`: probes started before that are spoilt.
    if (probe_.active && touch.pre.frame + 2 >= probe_.startFrame && touch.pre.frame <= probe_.targetFrame)
        probe_.active = false;
}

void PredictionModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    switch (ev.kind) {
    case GameEventKind::ContextChanged:
    case GameEventKind::MapUnloaded:
        invalidate();
        viewPath_.reset();
        team_ = -1;
        paused_ = false;
        hasPrevBall_ = false;
        frozenTicks_ = 0;
        frozen_ = false;
        lastDrawFromView_ = false;
        lastDrawReplay_ = false;
        probeBlockedUntil_ = 0;
        drawFramesSinceTick_ = 0;
        viewFrames_ = 0;
        viewOffset_ = 0;
        lastAirborneFrame_ = -1000000;
        forceRecompute_ = true;
        break;
    case GameEventKind::ResetShot:
    case GameEventKind::CarSpawned:
    case GameEventKind::Goal:
        forceRecompute_ = true;
        probe_.active = false;
        break;
    case GameEventKind::Paused: paused_ = true; break;
    case GameEventKind::Unpaused: paused_ = false; break;
    default: break;
    }
}

void PredictionModule::setHidden(bool hidden, const TickContext& ctx, Actions& out) {
    hidden_ = hidden;
    if (!hidden_) forceRecompute_ = true;
    ToastMessage t;
    t.title = "RL Trainer";
    t.text = hudText(hidden_ ? "Prédiction masquée" : "Prédiction affichée", ctx.settings.general.hudAccents);
    t.kind = ToastKind::Info;
    t.seconds = 2.f;
    out.toasts.push_back(std::move(t));
}

void PredictionModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    if (cmd.name == "toggle") {
        setHidden(!hidden_, ctx, out);
    } else if (cmd.name == "show") {
        setHidden(false, ctx, out);
    } else if (cmd.name == "hide") {
        setHidden(true, ctx, out);
    } else if (cmd.name == "reset_stats") {
        resetAccuracy();
        out.requestSave = true;
    } else if (cmd.name == "recompute") {
        forceRecompute_ = true;
        viewPath_.valid = false;
    }
}

// ---- Status --------------------------------------------------------------------------------------------
PredictionStatus PredictionModule::status() const {
    PredictionStatus st;
    st.enabled = cfg_.enabled;
    st.hidden = hidden_;
    st.horizon = cfg_.horizon;
    st.unit = resolveUnit(static_cast<SpeedUnit>(std::clamp(cfg_.unit, 0, 3)), metric_);
    st.recomputes = recomputes_;
    st.forcedRecomputes = forcedRecomputes_;
    st.viewRecomputes = viewRecomputes_;
    st.frozen = cfg_.enabled && frozen_ && !lastDrawFromView_;

    const bool fromView = lastDrawFromView_;
    const pr::PredictedPath& p = fromView ? viewPath_ : path_;
    if (!cfg_.enabled || !p.valid || p.samples.empty()) return st;
    const int n = p.size();
    const int now = fromView ? viewOffset_ : static_cast<int>(std::clamp<int64_t>(lastTickFrame_ - p.frame, 0, n));
    if (now >= n) return st;
    const float tNow = static_cast<float>(now) * kTickDt;
    auto at = [&](int i) -> const pr::PathSample& { return p.samples[static_cast<size_t>(i)]; };

    st.active = true;
    st.fromView = fromView;
    st.replay = fromView && lastDrawReplay_;
    st.arena = p.arena;
    const Vec3 pos = p.posAt(now), vel = p.velAt(now);
    st.ballAirborne = pr::ballAirborne(pos, vel, p.params.arenaRadius);
    st.ballHeight = pos.z;
    st.ballSpeed = length(vel);
    if (p.landingIndex >= now) {
        st.hasLanding = true;
        st.landingIn = at(p.landingIndex).t - tNow;
        st.landingPos = at(p.landingIndex).pos;
    }
    if (p.apexIndex >= now) {
        st.hasApex = true;
        st.apexIn = at(p.apexIndex).t - tNow;
        st.apexHeight = at(p.apexIndex).pos.z;
    }
    if (p.zoneStart >= 0 && p.zoneEnd >= now) {
        st.hasZone = true;
        const int idx = std::max(p.zoneStart, now);
        st.zoneIn = std::max(0.f, at(idx).t - tNow);
        st.zoneHeight = at(idx).pos.z;
    }
    for (int b = 0; b < p.bounceCount; ++b)
        if (p.bounces[static_cast<size_t>(b)].index >= now) ++st.bounces;
    if (!fromView && intercept_.found && intercept_.index >= now) {
        st.hasIntercept = true;
        st.interceptIn = intercept_.t - tNow;
        st.interceptBallSpeed = intercept_.ballSpeed2D;
        st.interceptCarSpeed = intercept_.carSpeed;
        st.interceptHeight = intercept_.ballPos.z;
        st.interceptJump = intercept_.jump;
    }
    if (p.goalIndex >= now) {
        st.goalSide = p.goalSide;
        st.goalForPlayer = goalRelation(p.goalSide, st.replay ? -1 : team_);  // the team draw() colours with
        st.goalIn = at(p.goalIndex).t - tNow;
    }
    return st;
}

PredictionAccuracy PredictionModule::accuracy() const {
    PredictionAccuracy a;
    a.count = accCount_;
    a.meanError = accCount_ > 0 ? static_cast<float>(accSum_ / accCount_) : 0.f;
    a.maxError = accMax_;
    a.lastError = accLast_;
    a.disturbed = accDisturbed_;
    a.recent.reserve(static_cast<size_t>(accRecentCount_));
    const int first = (accRecentHead_ - accRecentCount_ + kAccuracyHistory) % kAccuracyHistory;
    for (int i = 0; i < accRecentCount_; ++i) a.recent.push_back(accRecent_[static_cast<size_t>((first + i) % kAccuracyHistory)]);
    return a;
}

// ---- Draw ----------------------------------------------------------------------------------------------
void PredictionModule::updateViewPath(const ViewContext& view, const DrawContext& ctx) {
    if (!view.ballValid) {
        viewPath_.valid = false;
        return;
    }
    ++viewFrames_;
    if (!viewPath_.valid || viewFrames_ >= kViewRecomputeFrames) {
        pr::SimBall start;
        start.pos = view.ballVisualPos;
        start.vel = view.ballVel;
        const pr::SimParams prm = pr::SimParams::forRadius(view.ballRadius);
        pr::simulatePath(start, cfg_.horizon, prm, chooseArena(cfg_.arenaMode, ctx.game, view.ballRadius), viewPath_);
        pr::analyzePath(viewPath_, cfg_.zoneMin, cfg_.zoneMax);
        viewFrames_ = 0;
        ++viewRecomputes_;
    }
    // Where the ball is now on the cached path (the cache is up to 5 frames old).
    viewOffset_ = viewPath_.valid ? pr::closestSample(viewPath_, view.ballVisualPos, 90) + 1 : 0;
}

void PredictionModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (drawFramesSinceTick_ < (1 << 30)) ++drawFramesSinceTick_;
    if (!cfg_.enabled || hidden_) {
        lastDrawFromView_ = false;
        return;
    }
    const bool replay = ctx.game.mode == GameMode::Replay;
    lastDrawReplay_ = replay;
    // No physics tick for a while (local car demolished, spectating) while the ball keeps moving: the tick cache
    // is frozen, predict from the camera view instead. A paused game (ball still) keeps the tick cache.
    bool stale = false;
    if (!replay && !paused_ && drawFramesSinceTick_ > kStaleDrawFrames) {
        if (drawFramesSinceTick_ > kStaleMaxFrames || !path_.valid) {
            stale = true;
        } else if (view.ballValid) {
            const int now = static_cast<int>(std::clamp<int64_t>(lastTickFrame_ - path_.frame, 0, path_.size()));
            stale = distance(view.ballVisualPos, path_.posAt(now)) > kStaleMoveDistance;
        }
    }
    if (replay || stale) {
        // Entering the view mode again (a second demolition, back to a replay): the view cache is from an earlier
        // period, possibly minutes old. Drop it so the first frame predicts from the ball seen now.
        if (!lastDrawFromView_) viewPath_.valid = false;
        lastDrawFromView_ = true;
        updateViewPath(view, ctx);
        if (!viewPath_.valid) return;
        if (cfg_.onlyAirborne && !pr::ballAirborne(viewPath_.posAt(viewOffset_), viewPath_.velAt(viewOffset_),
                                                   viewPath_.params.arenaRadius))
            return;
        const int team = replay ? -1 : team_;
        drawPath(viewPath_, viewOffset_, false, team, view, ctx, out);
        if (cfg_.showPanel) drawPanel(viewPath_, viewOffset_, false, team, view, ctx, out);
        return;
    }
    lastDrawFromView_ = false;
    if (!path_.valid || path_.samples.empty()) return;
    if (cfg_.onlyAirborne && lastTickFrame_ - lastAirborneFrame_ > kAirborneHoldTicks) return;
    const int now = static_cast<int>(std::clamp<int64_t>(lastTickFrame_ - path_.frame, 0, path_.size()));
    drawPath(path_, now, true, team_, view, ctx, out);
    if (cfg_.showPanel) drawPanel(path_, now, true, team_, view, ctx, out);
}

void PredictionModule::drawPath(const pr::PredictedPath& p, int now, bool withIntercept, int team,
                                const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const int n = p.size();
    if (now >= n) return;
    const auto& s = p.samples;
    auto at = [&](int i) -> const pr::PathSample& { return s[static_cast<size_t>(i)]; };
    const float tNow = static_cast<float>(now) * kTickDt;
    const float horizon = std::max(0.1f, p.horizon);
    const float hs = std::max(0.1f, ctx.hudScale);
    const float textScale = cfg_.textScale * hs;
    const float lineH = 14.f * textScale;
    const bool accents = ctx.settings.general.hudAccents;
    const float w = cfg_.lineWidth;
    const float radius = p.params.radius;

    Occluder occ;
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
    }
    auto faded = [&](const Color& c, float t) {
        if (!cfg_.fade) return c;
        const float f = clamp01((t - tNow) / horizon);
        return c.withAlpha(c.a * (1.f - 0.8f * f));
    };

    const bool goalAhead = cfg_.showGoalWarning && p.goalIndex >= now;
    const Color base = goalAhead ? goalColor(p.goalSide, team) : cfg_.pathColor;
    const bool zoneShown = cfg_.showZone && p.zoneStart >= 0 && p.zoneEnd >= now;
    const Color zoneColor = zoneColorOver(base);

    // ---- Path polyline (vertices every `step` ticks, plus every bounce and the zone limits).
    if (cfg_.showPath || zoneShown) {
        const int step = std::clamp(cfg_.drawStepTicks, 1, 12);
        Vec3 prev = p.posAt(now);
        if (view.ballValid && distance(view.ballVisualPos, prev) < 2.f * radius) prev = view.ballVisualPos;
        int prevIdx = now - 1;
        for (int i = now; i < n; ++i) {
            const pr::PathSample& smp = at(i);
            const bool key = (smp.flags & pr::kStepBounce) || i == n - 1 || i == p.zoneStart || i == p.zoneEnd;
            if (i - prevIdx < step && !key) continue;
            const bool inZone = zoneShown && prevIdx >= p.zoneStart && i <= p.zoneEnd;
            if (inZone) out.line3D(prev, smp.pos, faded(zoneColor, smp.t), w + 1.5f, occ);
            else if (cfg_.showPath) out.line3D(prev, smp.pos, faded(base, smp.t), w, occ);
            prev = smp.pos;
            prevIdx = i;
        }
    }

    // ---- Time markers every 0.5 s (rings across the path), seconds written every 1 s.
    if (cfg_.showPath && cfg_.showTimeMarkers) {
        for (int k = 1;; ++k) {
            const int idx = now - 1 + k * 60;
            if (idx >= n) break;
            const pr::PathSample& smp = at(idx);
            const Color c = faded(base, smp.t);
            out.circle3D(smp.pos, normalize(smp.vel, {0.f, 0.f, 1.f}), 14.f, c, w, 8, occ);
            if (k % 2 == 0) out.text3D(smp.pos, std::to_string(k / 2) + " s", c, textScale * 0.8f, {0.f, -lineH});
        }
    }

    // ---- Bounces.
    if (cfg_.showBounces) {
        for (int b = 0; b < p.bounceCount; ++b) {
            const pr::PathBounce& bn = p.bounces[static_cast<size_t>(b)];
            if (bn.index < now) continue;
            if (cfg_.showLanding && bn.index == p.landingIndex) continue;  // the landing marker is drawn instead
            out.circle3D(bn.point + bn.normal * 2.f, bn.normal, 55.f, faded(cfg_.bounceColor, at(bn.index).t), w, 20, occ);
        }
    }

    // ---- Landing: first floor contact of an airborne ball.
    if (cfg_.showLanding && p.landingIndex >= now) {
        const pr::PathSample& smp = at(p.landingIndex);
        const Vec3 nrm = smp.normal;
        const Vec3 pt = smp.point + nrm * 2.f;
        out.circle3D(pt, nrm, radius, cfg_.landingColor, w, 32, occ);
        out.circle3D(pt, nrm, 20.f, cfg_.landingColor, w, 10, occ);
        out.text3D(pt, hudText("Atterrit dans " + seconds1(smp.t - tNow), accents), cfg_.landingColor, textScale,
                   {0.f, lineH});
    }

    // ---- First-touch zone.
    if (zoneShown) {
        const int idx = std::max(p.zoneStart, now);
        const pr::PathSample& smp = at(idx);
        const float tIn = smp.t - tNow;
        out.circle3D(smp.pos, {0.f, 0.f, 1.f}, radius + 10.f, zoneColor, w, 24, occ);
        const std::string label = tIn > 0.05f ? "Zone de touche dans " + seconds1(tIn) : std::string("Zone de touche");
        out.text3D(smp.pos - Vec3{0.f, 0.f, radius + 10.f}, hudText(label, accents), zoneColor, textScale,
                   {0.f, lineH});
    }

    // ---- Intercept advisor (tick mode only).
    if (withIntercept && cfg_.showIntercept && intercept_.found && intercept_.index >= now &&
        intercept_.index < n) {
        const float tIn = intercept_.t - tNow;
        if (tIn >= kMinInterceptDisplay) {
            const Vec3 bp = intercept_.ballPos;
            const Vec3 ground{bp.x, bp.y, 2.f};
            const Color c = cfg_.interceptColor;
            out.circle3D(ground, {0.f, 0.f, 1.f}, 70.f, c, w, 24, occ);
            if (bp.z - radius > 20.f) out.line3D(ground, bp - Vec3{0.f, 0.f, radius}, c, w, occ);
            const Vec3 toCam = view.valid ? normalize(view.camPos - bp, {0.f, 0.f, 1.f}) : Vec3{0.f, 0.f, 1.f};
            out.circle3D(bp, toCam, radius + 12.f, c, w, 24, occ);
            const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(std::clamp(cfg_.unit, 0, 3)), ctx.profile.metric);
            const Vec3 anchor = bp + Vec3{0.f, 0.f, radius + 30.f};
            out.text3D(anchor, hudText("Interception dans " + seconds1(tIn), accents), c, textScale, {0.f, -2.f * lineH});
            out.text3D(anchor, hudText("Arrive à " + formatBallSpeed(intercept_.ballSpeed2D, unit), accents), c, textScale,
                       {0.f, -lineH});
            const std::string hint =
                jumpHint(intercept_.jump, ctx.profile, keyStyleFromIndex(ctx.settings.profile.buttonStyle));
            if (!hint.empty()) out.text3D(anchor, hudText(hint, accents), c, textScale * 0.9f);
        }
    }

    // ---- Goal warning: text at the goal line and the goal mouth outlined.
    if (goalAhead) {
        const pr::PathSample& g = at(p.goalIndex);
        const std::string label = goalLabel(p.goalSide, team) + " dans " + seconds1(g.t - tNow);
        out.text3D(g.pos + Vec3{0.f, 0.f, radius + 40.f}, hudText(label, accents), base.withAlpha(255.f), textScale * 1.2f);
        const float gy = static_cast<float>(p.goalSide) * pr::phys::kArenaY;
        const float gx = pr::phys::kGoalHalfWidth, gz = pr::phys::kGoalHeight;
        const Color gc = base.withAlpha(255.f);
        out.line3D({-gx, gy, 0.f}, {-gx, gy, gz}, gc, w + 1.f);
        out.line3D({-gx, gy, gz}, {gx, gy, gz}, gc, w + 1.f);
        out.line3D({gx, gy, gz}, {gx, gy, 0.f}, gc, w + 1.f);
        out.line3D({gx, gy, 0.f}, {-gx, gy, 0.f}, gc, w + 1.f);
    }
}

void PredictionModule::drawPanel(const pr::PredictedPath& p, int now, bool withIntercept, int team,
                                 const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const int n = p.size();
    if (now >= n) return;
    const bool accents = ctx.settings.general.hudAccents;
    const float tNow = static_cast<float>(now) * kTickDt;
    auto at = [&](int i) -> const pr::PathSample& { return p.samples[static_cast<size_t>(i)]; };
    DrawPanel panel;
    panel.pos = {clamp01(cfg_.panelX) * view.screenW, clamp01(cfg_.panelY) * view.screenH};
    panel.anchorX = cfg_.panelX > 0.5f ? Align::Right : Align::Left;
    panel.title = hudText("Prédiction", accents);
    panel.scale = cfg_.textScale * std::max(0.1f, ctx.hudScale) * 0.9f;
    // Compact HUD: goal, interception and landing first (at most three lines); the ball speed at the interception
    // point, the touch zone when three lines are already there, and "ball on the floor" are details.
    auto line = [&](const std::string& text, const Color& c, bool detail = false) {
        PanelLine l;
        l.text = hudText(text, accents);
        l.color = c;
        l.detail = detail;
        panel.lines.push_back(std::move(l));
    };
    const bool goal = cfg_.showGoalWarning && p.goalIndex >= now;
    const bool intercept = withIntercept && intercept_.found && intercept_.index >= now;
    const bool landing = p.landingIndex >= now;
    const bool zone = p.zoneStart >= 0 && p.zoneEnd >= now;
    if (landing) line("Atterrit dans " + seconds1(at(p.landingIndex).t - tNow), cfg_.landingColor);
    else if (!pr::ballAirborne(p.posAt(now), p.velAt(now), p.params.arenaRadius)) line("Balle au sol", colors::Grey, true);
    if (zone)
        line("Zone de touche dans " + seconds1(at(std::max(p.zoneStart, now)).t - tNow), cfg_.zoneColor,
             goal && intercept && landing);
    if (intercept) {
        const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(std::clamp(cfg_.unit, 0, 3)), ctx.profile.metric);
        line("Interception dans " + seconds1(intercept_.t - tNow), cfg_.interceptColor);
        line("Arrive à " + formatBallSpeed(intercept_.ballSpeed2D, unit), cfg_.interceptColor, true);
    }
    if (goal) {
        line(goalLabel(p.goalSide, team) + " dans " + seconds1(at(p.goalIndex).t - tNow),
             goalColor(p.goalSide, team).withAlpha(255.f));
    }
    if (panel.lines.empty()) return;
    // Nothing left once the details are dropped: no empty panel with only its title.
    if (ctx.compactHud &&
        std::none_of(panel.lines.begin(), panel.lines.end(), [](const PanelLine& l) { return !l.detail; }))
        return;
    out.panel(std::move(panel));
}

// ---- Persistence ---------------------------------------------------------------------------------------
void PredictionModule::save(Store& store) const {
    store.setInt(kKeyVersion, 1);
    const PredictionAccuracy a = accuracy();
    store.setInt(kKeyCount, a.count);
    store.setFloat(kKeyMean, a.meanError);
    store.setFloat(kKeyMax, a.maxError);
    store.setFloat(kKeyLast, a.lastError);
    store.setInt(kKeyDisturbed, a.disturbed);
    std::vector<std::string> recent;
    recent.reserve(a.recent.size());
    for (float e : a.recent) recent.push_back(formatNumber(e, 1));
    store.setList(kKeyRecent, recent);
}

void PredictionModule::load(const Store& store) {
    resetAccuracy();
    if (!store.has(kKeyCount)) return;
    const long long count = store.getInt(kKeyCount, 0);
    const float mean = store.getFloat(kKeyMean, 0.f);
    if (count <= 0 || count > 1000000000LL || !(mean >= 0.f) || mean > 100000.f) return;
    accCount_ = static_cast<int>(count);
    accSum_ = static_cast<double>(mean) * static_cast<double>(count);
    accMax_ = std::clamp(store.getFloat(kKeyMax, mean), 0.f, 100000.f);
    accLast_ = std::clamp(store.getFloat(kKeyLast, 0.f), 0.f, 100000.f);
    accDisturbed_ = static_cast<int>(std::clamp<long long>(store.getInt(kKeyDisturbed, 0), 0, 1000000000LL));
    const std::vector<std::string> recent = store.getList(kKeyRecent);
    const size_t keep = static_cast<size_t>(kAccuracyHistory);
    const size_t skip = recent.size() > keep ? recent.size() - keep : 0;
    for (size_t i = skip; i < recent.size(); ++i) {
        float v = 0.f;
        if (!parseFloatValue(recent[i], v) || v < 0.f || v > 100000.f) continue;
        accRecent_[static_cast<size_t>(accRecentHead_)] = v;
        accRecentHead_ = (accRecentHead_ + 1) % kAccuracyHistory;
        accRecentCount_ = std::min(accRecentCount_ + 1, kAccuracyHistory);
    }
}

} // namespace rlt
