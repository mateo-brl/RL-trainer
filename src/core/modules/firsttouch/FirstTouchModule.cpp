#include "core/modules/firsttouch/FirstTouchModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/common/Text.h"

namespace rlt {

namespace {
constexpr float kChainGap = 1.0f;        // s without contact ending a dribble chain
constexpr int64_t kFlipResetDedupeTicks = 60;  // 0.5 s
constexpr size_t kTipHistory = 5;
constexpr size_t kMaxScoresSaved = 50;
constexpr size_t kMaxHeatSaved = 40;
constexpr const char* kToastTitle = "Première touche";

bool inputActive(const Inputs& in) {
    return std::fabs(in.throttle) > 0.1f || std::fabs(in.steer) > 0.1f || in.jump || in.boost || in.holdingBoost ||
           in.handbrake;
}

std::string intText(float v) { return std::to_string(static_cast<int>(std::lround(v))); }

void toast(Actions& out, const std::string& text, ToastKind kind) {
    ToastMessage t;
    t.title = kToastTitle;
    t.text = text;
    t.kind = kind;
    out.toasts.push_back(std::move(t));
}

void addMetric(AttemptResult& r, const char* key, const char* label, float value, const char* unit, float ideal = 0.f,
               bool hasIdeal = false) {
    Metric m;
    m.key = key;
    m.label = label;
    m.value = value;
    m.unit = unit;
    m.ideal = ideal;
    m.hasIdeal = hasIdeal;
    r.metrics.push_back(std::move(m));
}
} // namespace

// ---- Settings -------------------------------------------------------------------------------------------
void FirstTouchModule::describeSettings(SettingsSchema& schema) {
    using C = FirstTouchConfig;
    auto a = schema.section(&Settings::firstTouch, "firsttouch", "Analyse des touches");
    a.toggle("rlt_ft_enabled", &C::enabled, true, "Activer",
             "Analyse et note chaque touche de balle : en freeplay, en entraînement et sur les maps workshop.");
    a.number("rlt_ft_control_window", &C::controlWindow, 1.0f, 0.5f, 3.0f, "Fenêtre de contrôle (s)",
             "Durée suivie après la touche pour juger si tu gardes la balle.", 1);
    a.number("rlt_ft_control_radius", &C::controlRadius, 350.f, 200.f, 700.f, "Rayon de contrôle (uu)",
             "Distance max entre la balle et ta voiture pour que la balle compte comme contrôlée.", 0);
    a.number("rlt_ft_control_speed", &C::controlRelSpeed, 450.f, 200.f, 900.f, "Vitesse relative max (uu/s)",
             "Au-delà, la balle s'échappe : elle ne compte plus comme contrôlée.", 0);
    a.number("rlt_ft_control_fraction", &C::controlFraction, 0.6f, 0.3f, 1.0f, "Part de contrôle exigée",
             "Part de la fenêtre où la balle doit rester contrôlée pour réussir la touche. Dans les exercices, "
             "le contrôle compte à partir du niveau 4 et la part exigée monte avec le niveau.", 2);
    a.number("rlt_ft_success_score", &C::successScore, 60.f, 30.f, 95.f, "Note pour réussir",
             "Note minimale (sur 100) d'une touche réussie, en plus du contrôle. Dans les exercices, le seuil part "
             "de 10 points plus bas au niveau 1 (la touche seule compte) et finit 15 points plus haut au niveau 10.",
             0);
    a.number("rlt_ft_good_zone", &C::goodZone, 150.f, 50.f, 400.f, "Zone verte (uu/s)",
             "Écart de vitesse jugé bon ([BON] dans le chat). 150 uu/s = 5,4 km/h.", 0);
    a.number("rlt_ft_ok_zone", &C::okZone, 300.f, 100.f, 800.f, "Zone jaune (uu/s)",
             "Écart de vitesse encore correct ([MOYEN]). Au-delà : [MAUVAIS].", 0);
    a.choice("rlt_ft_unit", &C::unit, 0, {"Auto (réglage du jeu)", "km/h", "mph", "uu/s"}, "Unité",
             "Unité des vitesses dans le panneau et le chat. Auto suit le réglage métrique du jeu.");
    a.toggle("rlt_ft_send_results", &C::sendResults, true, "Envoyer au coach",
             "Chaque touche notée alimente la page Progression (historique, points faibles).");

    auto h = schema.section(&Settings::firstTouch, "firsttouch", "Affichage");
    h.toggle("rlt_ft_hud", &C::hudEnabled, true, "Panneau des touches",
             "Affiche la note de la dernière touche et le conseil principal en bas à droite. Avec le HUD compact "
             "désactivé, ajoute les stats de session et la carte des contacts.");
    h.number("rlt_ft_hud_x", &C::hudX, 0.99f, 0.f, 1.f, "Position X", "Position horizontale du panneau (fraction de l'écran).", 2);
    h.number("rlt_ft_hud_y", &C::hudY, 0.70f, 0.f, 1.f, "Position Y", "Position verticale du panneau (fraction de l'écran).", 2);
    h.number("rlt_ft_hud_size", &C::hudSize, 1.f, 0.5f, 2.f, "Taille du panneau", "Multiplie la taille du panneau.", 2);
    h.toggle("rlt_ft_heatmap", &C::showHeatmap, true, "Carte des points de contact",
             "Petit schéma de la balle avec tes 40 derniers points de contact, colorés selon la note. En jeu, "
             "seulement quand le HUD compact est désactivé. Toujours visible dans la page Première touche (onglet "
             "Carte des contacts).");
    h.toggle("rlt_ft_tips", &C::showTips, true, "Conseil après chaque touche", "Affiche le conseil principal de la dernière touche.");
    h.toggle("rlt_ft_trend", &C::showTrend, true, "Tendance de vitesse",
             "Indique si tu arrives en moyenne trop vite ou trop lentement sur la balle.");
    h.toggle("rlt_ft_marker", &C::showContactMarker, true, "Marqueur du point de contact",
             "Montre un instant sur la balle l'endroit exact où tu l'as touchée.");
    h.number("rlt_ft_marker_seconds", &C::markerSeconds, 1.5f, 0.5f, 5.f, "Durée du marqueur (s)",
             "Temps d'affichage du marqueur de contact.", 1);
    h.color("rlt_ft_color_good", &C::goodColor, Color{40, 220, 90, 255}, "Couleur bonne touche", "Note >= 70 et zone verte.");
    h.color("rlt_ft_color_ok", &C::okColor, Color{255, 205, 40, 255}, "Couleur touche correcte", "Note >= 45 et zone jaune.");
    h.color("rlt_ft_color_bad", &C::badColor, Color{255, 70, 60, 255}, "Couleur touche ratée", "Note < 45 et zone rouge.");

    auto c = schema.section(&Settings::firstTouch, "firsttouch", "Chat");
    c.toggle("rlt_ft_chat", &C::chatLog, false, "Log des touches dans le chat",
             "Écrit après chaque touche notée une ligne comme \"[BON] Écart +3.2 km/h, note 84\".");
    c.toggle("rlt_ft_chat_flip", &C::chatFlipReset, true, "Aussi sur les flip resets",
             "Ajoute une ligne \"[FLIP RESET]\" avec l'écart de vitesse à chaque flip reset.");
    c.toggle("rlt_ft_chat_score", &C::chatWithScore, true, "Afficher la note", "Ajoute la note sur 100 à la ligne du chat.");

    auto d = schema.section(&Settings::firstTouch, "firsttouch", "Exercices");
    d.number("rlt_ft_drill_timeout", &C::drillTimeout, 6.f, 3.f, 15.f, "Délai pour toucher (s)",
             "Sans touche dans ce délai, l'essai est raté et le niveau baisse. Au niveau 1, tu as 50 % de temps "
             "en plus.", 1);
    d.number("rlt_ft_drill_pause", &C::drillPause, 1.5f, 0.5f, 5.f, "Pause entre deux essais (s)",
             "Temps de pause avant le placement suivant.", 1);
    d.toggle("rlt_ft_drill_reset_car", &C::drillResetCar, true, "Replacer la voiture à chaque essai",
             "Remet ta voiture à l'arrêt sur sa position de départ avant chaque balle.");
    d.toggle("rlt_ft_drill_boost", &C::drillFullBoost, true, "Boost plein à chaque essai", "Remplit ton boost à chaque placement.");
    d.number("rlt_ft_drill_hud_x", &C::drillHudX, 0.01f, 0.f, 1.f, "Panneau exercice : X",
             "Position horizontale du panneau d'exercice (fraction de l'écran).", 2);
    d.number("rlt_ft_drill_hud_y", &C::drillHudY, 0.12f, 0.f, 1.f, "Panneau exercice : Y",
             "Position verticale du panneau d'exercice (fraction de l'écran).", 2);

    auto x = schema.section(&Settings::firstTouch, "firsttouch", "Note (expert)");
    x.advanced();
    x.number("rlt_ft_first_touch_gap", &C::firstTouchGap, 1.0f, 0.3f, 3.f, "Délai d'une première touche (s)",
             "Une touche n'est notée comme première touche que si tu n'as pas touché la balle depuis ce délai. "
             "Les touches plus rapprochées comptent comme dribble.", 1);
    x.number("rlt_ft_speed_good", &C::speedGood, 150.f, 50.f, 400.f, "Vitesse relative parfaite (uu/s)",
             "En dessous, la partie vitesse de la note est pleine (la balle se décolle à ~0,65 x cette vitesse).", 0);
    x.number("rlt_ft_speed_bad", &C::speedBad, 600.f, 300.f, 1500.f, "Vitesse relative nulle (uu/s)",
             "Au-dessus, la partie vitesse de la note vaut 0.", 0);
    x.number("rlt_ft_contact_good", &C::contactGoodDeg, 30.f, 10.f, 60.f, "Erreur de contact parfaite (°)",
             "Écart d'angle toléré entre ton point de contact et le point idéal.", 0);
    x.number("rlt_ft_contact_bad", &C::contactBadDeg, 70.f, 40.f, 120.f, "Erreur de contact nulle (°)",
             "Au-delà, la partie contact de la note vaut 0.", 0);
    x.number("rlt_ft_weight_speed", &C::weightSpeed, 35.f, 0.f, 100.f, "Poids vitesse (%)", "Part de la vitesse dans la note.", 0);
    x.number("rlt_ft_weight_contact", &C::weightContact, 25.f, 0.f, 100.f, "Poids contact (%)", "Part du point de contact dans la note.", 0);
    x.number("rlt_ft_weight_control", &C::weightControl, 40.f, 0.f, 100.f, "Poids contrôle (%)", "Part du contrôle après la touche.", 0);
    x.number("rlt_ft_tip_fast", &C::tooFastThreshold, 300.f, 100.f, 800.f, "Seuil trop vite / trop lent (uu/s)",
             "Écart dans l'axe de la balle à partir duquel le conseil \"vitesse\" s'affiche.", 0);
    x.number("rlt_ft_tip_angle", &C::angleThreshold, 300.f, 100.f, 800.f, "Seuil d'angle (uu/s)",
             "Écart perpendiculaire à la balle à partir duquel le conseil \"angle\" s'affiche.", 0);
    x.number("rlt_ft_tip_lateral", &C::lateralThreshold, 45.f, 15.f, 120.f, "Seuil de décentrage (uu)",
             "Décalage latéral de la balle à partir duquel le conseil \"centre la balle\" s'affiche.", 0);
    x.number("rlt_ft_tip_pop", &C::popThreshold, 300.f, 100.f, 900.f, "Seuil de pop (uu/s)",
             "Vitesse verticale de la balle après la touche au-delà de laquelle elle \"rebondit\" sur toi.", 0);

    auto e = schema.section(&Settings::firstTouch, "firsttouch", "Exercices (expert)");
    e.advanced();
    e.number("rlt_ft_drill_target", &C::drillTarget, 0.75f, 0.5f, 0.95f, "Taux de réussite visé",
             "Le niveau s'ajuste pour que tu réussisses environ cette part des essais (0,75 = défi mais faisable).", 2);
    e.number("rlt_ft_drill_step", &C::drillStep, 0.04f, 0.01f, 0.15f, "Pas de niveau",
             "Hausse du niveau après une réussite (la baisse après un échec en découle).", 2);
    e.integer("rlt_ft_drill_seed", &C::drillSeed, 0, 0, 999999, "Graine des balles",
              "0 = nouvelle série à chaque lancement. Une autre valeur rejoue toujours la même série.");
}

FirstTouchModule::FirstTouchModule() {
    for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
    for (auto& s : fullStairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
}

void FirstTouchModule::onSettingsChanged(const Settings& settings) {
    const bool stairsChanged = settings.firstTouch.drillTarget != cfg_.drillTarget || settings.firstTouch.drillStep != cfg_.drillStep;
    cfg_ = settings.firstTouch;
    keyStyle_ = keyStyleFromIndex(settings.profile.buttonStyle);
    if (stairsChanged) rebuildStaircases();
    refreshUnit();
    if (!cfg_.enabled) {
        cancelPending();
        if (drill_.active) stopDrill(nullptr, "");
    }
}

void FirstTouchModule::onProfileChanged(const PlayerProfile& profile) {
    profile_ = profile;
    profileMetric_ = profile.metric;
    refreshUnit();
}

void FirstTouchModule::refreshUnit() { unit_ = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), profileMetric_); }

void FirstTouchModule::rebuildStaircases() {
    auto rebuild = [this](Staircase& s) {
        Store tmp;
        s.save(tmp, "s");
        Staircase n(cfg_.drillTarget, cfg_.drillStep, s.level());
        n.load(tmp, "s");
        s = n;
    };
    for (auto& s : stairs_) rebuild(s);
    for (auto& s : fullStairs_) rebuild(s);
}

ft::ScoreParams FirstTouchModule::scoreParams() const {
    ft::ScoreParams p;
    p.speedGood = cfg_.speedGood;
    p.speedBad = cfg_.speedBad;
    p.contactGoodDeg = cfg_.contactGoodDeg;
    p.contactBadDeg = cfg_.contactBadDeg;
    p.wSpeed = cfg_.weightSpeed;
    p.wContact = cfg_.weightContact;
    p.wControl = cfg_.weightControl;
    return p;
}

ft::DrillCriteria FirstTouchModule::criteriaFor(float level) const {
    return ft::drillCriteria(level, cfg_.successScore, cfg_.controlFraction, cfg_.drillTimeout);
}

ft::DiagnosisParams FirstTouchModule::diagnosisParams() const {
    ft::DiagnosisParams d;
    d.tooFast = cfg_.tooFastThreshold;
    d.angle = cfg_.angleThreshold;
    d.lateral = cfg_.lateralThreshold;
    d.pop = cfg_.popThreshold;
    return d;
}

// ---- Analysis -------------------------------------------------------------------------------------------
bool FirstTouchModule::detectTeleport(const Snapshot& s) {
    bool tp = false;
    if (havePrev_ && s.frame > prevFrame_) {
        const float ticks = static_cast<float>(std::min<int64_t>(s.frame - prevFrame_, 8));
        if (s.car.valid && prevCarValid_ && distance(s.car.pos, prevCarPos_) > 40.f * ticks + 40.f) tp = true;
        if (s.ball.valid && prevBallValid_ && distance(s.ball.pos, prevBallPos_) > 80.f * ticks + 60.f) tp = true;
    }
    havePrev_ = true;
    prevFrame_ = s.frame;
    prevCarValid_ = s.car.valid;
    prevBallValid_ = s.ball.valid;
    prevCarPos_ = s.car.pos;
    prevBallPos_ = s.ball.pos;
    return tp;
}

void FirstTouchModule::onTick(const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) {
        if (drill_.active) stopDrill(&out, "");
        return;
    }
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    if (detectTeleport(s)) {
        cancelPending();
        endChain();
        ignoreTouchesUntil_ = s.frame - 1;
    }
    if (pending_.window.active() && pending_.window.feed(s)) finalizePending(ctx, out);
    if (chain_.active && s.time - chain_.last > kChainGap) endChain();
    tickDrill(ctx, out);
}

void FirstTouchModule::onTouch(const TouchEvent& t, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) return;
    touchFrame_ = t.post.frame;  // a hanging ball is never put back on the tick of a touch
    if (!t.pre.car.valid || !t.pre.ball.valid) return;
    if (ignoreTouchesUntil_ - t.pre.frame > 1200) ignoreTouchesUntil_ = -1;  // frame counter restarted
    if (t.pre.frame <= ignoreTouchesUntil_) return;  // state before a teleport / placement
    // A previous contact older than the last teleport / placement / reset does not make this one a follow-up.
    int sincePrev = t.ticksSincePrevTouch;
    if (t.pre.frame - static_cast<int64_t>(sincePrev) <= ignoreTouchesUntil_) sincePrev = 1 << 30;
    const int gapTicks = std::max(1, static_cast<int>(std::lround(cfg_.firstTouchGap * kTickRate)));
    const bool recentBounce = ft::ballBouncedRecently(ctx.history, t.pre.frame);
    const ft::TouchType type = ft::classifyTouch(t.pre, t.newContact, sincePrev, recentBounce, gapTicks);
    if (sincePrev == (1 << 30) && chain_.active) endChain();  // fresh possession after a reset
    // Parcours complet: the first touch of the rep is judged by its touch step on the next tick (after the step
    // checks of that tick).
    if (drill_.active && drill_.full && drill_.type != ft::DrillType::Mix && drill_.phase == DrillPhase::WaitTouch &&
        !drill_.fullState.touched && !drill_.touchPending && t.pre.frame > drill_.placeFrame) {
        drill_.touchPending = true;
        drill_.touchMetrics = ft::computeMetrics(t, type == ft::TouchType::Dribble ? ft::TouchType::Ground : type);
        drill_.touchPre = t.pre;
    }
    updateChain(t.pre.time, t.newContact);
    if (type == ft::TouchType::Dribble) {
        if (t.newContact) {
            session_.addDribbleContact();
            total_.addDribbleContact();
        }
        return;
    }
    if (pending_.window.active()) finalizePending(ctx, out);  // a new first touch closes the previous window
    startPending(t, type, ctx);
}

void FirstTouchModule::startPending(const TouchEvent& t, ft::TouchType type, const TickContext& ctx) {
    (void)ctx;
    pending_ = Pending{};
    pending_.type = type;
    pending_.metrics = ft::computeMetrics(t, type);
    pending_.time = t.pre.time;
    if (drill_.active && drill_.phase == DrillPhase::WaitTouch && t.pre.frame > drill_.placeFrame) {
        pending_.inDrill = true;
        pending_.drillLevel = drill_.repLevel;
        pending_.drillType = drill_.current;
        // A parcours complet keeps its own steps: the window only feeds the "Garde-la" step.
        if (!(drill_.full && drill_.type != ft::DrillType::Mix)) drill_.phase = DrillPhase::Evaluate;
    }
    const int windowTicks = static_cast<int>(std::lround(cfg_.controlWindow * kTickRate));
    pending_.window.start(t, windowTicks, cfg_.controlRadius, cfg_.controlRelSpeed, cfg_.controlFraction);
    if (pending_.metrics.valid) {
        markerValid_ = true;
        markerDir_ = pending_.metrics.contactDir;
        markerTime_ = t.pre.time;
        markerScore_ = -1.f;
    }
}

void FirstTouchModule::cancelPending() { pending_.window.cancel(); }

void FirstTouchModule::finalizePending(const TickContext& ctx, Actions& out) {
    const ft::ControlSummary c = pending_.window.summary();
    const int windowTicks = pending_.window.windowTicks();
    pending_.window.cancel();
    const ft::TouchMetrics& m = pending_.metrics;
    const bool fullKeep = pending_.inDrill && drill_.active && drill_.full && drill_.type != ft::DrillType::Mix &&
                          drill_.steps.running() && drill_.steps.currentStep() &&
                          drill_.steps.currentStep()->id == ft::kFullKeepStep;
    if (!m.valid || c.ticks < std::max(1, windowTicks / 3)) {
        // Not enough valid ticks (car demolished, ball gone): no grade; a drill rep is replayed.
        if (pending_.inDrill && drill_.active && (drill_.phase == DrillPhase::Evaluate || fullKeep))
            drill_.phase = DrillPhase::Place;
        return;
    }
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
    const ft::ScoreParams sp = scoreParams();
    const ft::TouchScore sc = ft::scoreTouch(m, c, sp);
    const KeyStyle style = keyStyleFromIndex(ctx.settings.profile.buttonStyle);
    std::vector<Tip> tips = ft::diagnose(pending_.type, m, c, sc, sp, diagnosisParams(), ctx.profile, style);
    // A drill rep is judged at its level (the touch alone at the first levels), everything else with the player's
    // thresholds.
    bool success = sc.score >= cfg_.successScore && c.controlled;
    float repScore = sc.score;
    std::string repTip;
    if (pending_.inDrill) {
        const ft::DrillCriteria dc = criteriaFor(pending_.drillLevel);
        repScore = ft::drillRepScore(dc, sc, sp);
        success = ft::drillRepSuccess(dc, repScore, c.fraction);
        repTip = ft::drillRepTip(dc, tips);
    }
    const ft::SpeedZone zone = ft::speedZone(m.speedDiff, cfg_.goodZone, cfg_.okZone);

    session_.addGraded(pending_.type, sc.score, success, m.speedDiff, zone == ft::SpeedZone::Good);
    total_.addGraded(pending_.type, sc.score, success, m.speedDiff, zone == ft::SpeedZone::Good);
    recentScores_.push(sc.score);
    heat_.push(ft::HeatPoint{m.heatX, m.heatY, m.heatZ, sc.score, static_cast<int>(pending_.type)});
    recentTipCodes_.push_back(tips.empty() ? std::string() : tips.front().code);
    if (recentTipCodes_.size() > kTipHistory) recentTipCodes_.erase(recentTipCodes_.begin());
    markerScore_ = sc.score;

    LastTouchView v;
    v.valid = true;
    v.type = pending_.type;
    v.time = pending_.time;
    v.score = sc.score;
    v.success = success;
    v.controlled = c.controlled;
    v.verdict = verdictFromScore(sc.score);
    v.headline = ft::scoreHeadline(sc.score);
    v.speedDiff = m.speedDiff;
    v.relSpeed = m.relSpeed;
    v.along = m.along;
    v.cross = m.cross;
    v.closingSpeed = m.closingSpeed;
    v.separation = m.separation;
    v.ballPop = m.ballPop;
    v.popHeight = c.popHeight;
    v.controlFraction = c.fraction;
    v.contactErrDeg = m.contactErrDeg;
    v.lateralOffset = m.lateralOffset;
    v.height = m.height;
    v.part = m.part;
    v.zone = zone;
    v.sSpeed = sc.sSpeed;
    v.sContact = sc.sContact;
    v.sControl = sc.sControl;
    v.fromHitInfo = m.fromHitInfo;
    v.inDrill = pending_.inDrill;
    v.tips = tips;
    last_ = std::move(v);

    if (cfg_.sendResults) {
        AttemptResult r;
        r.module = "firsttouch";
        r.skill = std::string("first_touch.") + ft::touchTypeId(pending_.type);
        r.title = std::string("Première touche (") + ft::touchTypeLabel(pending_.type) + ")";
        r.score = sc.score;
        r.success = success;
        r.verdict = verdictFromScore(sc.score);
        addMetric(r, "speed_diff", "Écart de vitesse", m.speedDiff, "uu/s", 0.f, true);
        addMetric(r, "rel_speed", "Vitesse relative au contact", m.relSpeed, "uu/s", 0.f, true);
        addMetric(r, "closing_speed", "Vitesse d'impact", m.closingSpeed, "uu/s");
        addMetric(r, "along", "Écart dans l'axe de la balle", m.along, "uu/s", 0.f, true);
        addMetric(r, "cross", "Écart d'angle", m.cross, "uu/s", 0.f, true);
        addMetric(r, "contact_height", "Hauteur du contact sur la balle", m.contactHeight01 * 100.f, "%");
        addMetric(r, "contact_error", "Erreur de point de contact", m.contactErrDeg, "°", 0.f, true);
        addMetric(r, "lateral_offset", "Décentrage", m.lateralOffset, "uu", 0.f, true);
        addMetric(r, "ball_pop", "Pop de la balle", m.ballPop, "uu/s");
        addMetric(r, "separation", "Vitesse de séparation", m.separation, "uu/s", 0.f, true);
        addMetric(r, "pop_height", "Hauteur du rebond", c.popHeight, "uu");
        addMetric(r, "control", "Contrôle après la touche", c.fraction * 100.f, "%", 100.f, true);
        addMetric(r, "score_speed", "Note vitesse", sc.sSpeed * 100.f, "%", 100.f, true);
        addMetric(r, "score_contact", "Note contact", sc.sContact * 100.f, "%", 100.f, true);
        addMetric(r, "score_control", "Note contrôle", sc.sControl * 100.f, "%", 100.f, true);
        if (pending_.inDrill)
            addMetric(r, "drill_level", "Niveau de l'exercice", static_cast<float>(ft::playerLevel(pending_.drillLevel)),
                      "");
        r.tips = tips;
        r.time = pending_.time;
        r.valueForRecord = sc.score;
        r.hasRecordValue = true;
        r.lowerIsBetter = false;
        out.results.push_back(std::move(r));
    }
    if (cfg_.chatLog) {
        ChatMessage msg;
        msg.text = ft::chatTouchLine(m.speedDiff, sc.score, zone, unit, cfg_.chatWithScore);
        out.chat.push_back(std::move(msg));
    }
    if (fullKeep) {
        // "Garde-la": the ball stayed close for most of the window.
        if (c.fraction >= ft::kFullKeepFraction) drill_.steps.complete(ctx.snap.time);
        else drill_.steps.fail(ctx.snap.time);
        fullRepDone(drill_.steps.isComplete(), out);
    } else if (pending_.inDrill && drill_.active && drill_.phase == DrillPhase::Evaluate) {
        drillRepDone(success, repScore, repTip, out);
    }
    if (++gradedSinceSave_ >= 10) {
        gradedSinceSave_ = 0;
        out.requestSave = true;
    }
}

void FirstTouchModule::updateChain(float now, bool newContact) {
    if (chain_.active && now - chain_.last > kChainGap) endChain();
    if (!chain_.active) {
        chain_ = Chain{};
        chain_.active = true;
        chain_.start = now;
    }
    chain_.last = std::max(chain_.last, now);
    if (newContact) ++chain_.contacts;
}

void FirstTouchModule::endChain() {
    if (!chain_.active) return;
    const float seconds = std::max(0.f, chain_.last - chain_.start);
    session_.addChain(chain_.contacts, seconds);
    total_.addChain(chain_.contacts, seconds);
    if (chain_.contacts >= 2) lastChain_ = chain_;
    lastChain_.active = false;
    chain_ = Chain{};
}

void FirstTouchModule::resetTransient() {
    cancelPending();
    chain_ = Chain{};
    havePrev_ = false;
    markerValid_ = false;
    haveFlipReset_ = false;
    ignoreTouchesUntil_ = -1;
}

// ---- Events ---------------------------------------------------------------------------------------------
void FirstTouchModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    switch (ev.kind) {
    case GameEventKind::ContextChanged: {
        const GameContext& g = ctx.game;
        category_ = g.category;
        const bool changed = !haveContext_ || g.mode != mode_ || g.mapName != mapName_;
        if (changed) {
            haveContext_ = true;
            mode_ = g.mode;
            mapName_ = g.mapName;
            resetTransient();
            if (drill_.active) stopDrill(&out, "Exercice arrêté : la partie a changé.");
        }
        break;
    }
    case GameEventKind::MapUnloaded:
        resetTransient();
        if (drill_.active) stopDrill(&out, "");
        break;
    case GameEventKind::ResetShot:
    case GameEventKind::CarSpawned:
        cancelPending();
        endChain();
        if (ctx.hasSnapshot) ignoreTouchesUntil_ = ctx.snap.frame;
        if (drill_.active && (drill_.phase == DrillPhase::WaitTouch || drill_.phase == DrillPhase::Evaluate))
            drill_.phase = DrillPhase::Place;  // replay the rep, not counted
        break;
    case GameEventKind::Goal:
        cancelPending();
        endChain();
        if (drill_.active && (drill_.phase == DrillPhase::WaitTouch || drill_.phase == DrillPhase::Evaluate)) {
            drill_.phase = DrillPhase::Pause;
            drill_.deadline = drill_.now + cfg_.drillPause;
            drill_.lastSuccess = false;
            drill_.lastOutcome = "But : essai non compté.";
            drill_.lastTip.clear();
        }
        break;
    case GameEventKind::FlipReset: {
        const int64_t f = ctx.hasSnapshot ? ctx.snap.frame : ev.frame;
        if (haveFlipReset_ && f >= lastFlipResetFrame_ && f - lastFlipResetFrame_ < kFlipResetDedupeTicks) break;
        haveFlipReset_ = true;
        lastFlipResetFrame_ = f;
        if (!cfg_.enabled) break;
        ++session_.flipResets;
        ++total_.flipResets;
        if (cfg_.chatLog && cfg_.chatFlipReset && ctx.hasSnapshot && ctx.snap.car.valid && ctx.snap.ball.valid) {
            const float diff = length2D(ctx.snap.car.vel) - length2D(ctx.snap.ball.vel);
            const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
            ChatMessage msg;
            msg.text = ft::chatFlipResetLine(diff, ft::speedZone(diff, cfg_.goodZone, cfg_.okZone), unit);
            out.chat.push_back(std::move(msg));
        }
        break;
    }
    default: break;
    }
}

// ---- Commands -------------------------------------------------------------------------------------------
void FirstTouchModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    if (cmd.name == "drill" || cmd.name == "start") {
        ft::DrillType t = ft::DrillType::Mix;
        bool full = false;
        for (size_t i = 0; i < cmd.args.size(); ++i) {
            const std::string& a = cmd.args[i];
            if (a == "full" || a == "parcours" || a == "complet") {
                full = true;
                continue;
            }
            if (i == 0 && ft::parseDrillId(a, t)) continue;
            toast(out,
                  "Exercice inconnu : " + a +
                      " (roll_in, roll_away, pass, lob, wall, high, mix, puis full pour le parcours complet).",
                  ToastKind::Warning);
            return;
        }
        startDrill(t, full, ctx, out);
    } else if (cmd.name == "stop") {
        if (drill_.active) stopDrill(&out, "Exercice arrêté.");
    } else if (cmd.name == "skip") {
        if (drill_.active && drill_.phase != DrillPhase::Idle) {
            cancelPending();
            drill_.phase = DrillPhase::Place;
        }
    } else if (cmd.name == "reset_level") {
        // "Revenir au niveau 1": one drill and its parcours complet start again from the first level.
        ft::DrillType t = ft::DrillType::Mix;
        if (cmd.args.empty() || !ft::parseDrillId(cmd.args[0], t)) {
            toast(out, "Exercice inconnu (roll_in, roll_away, pass, lob, wall, high, mix).", ToastKind::Warning);
            return;
        }
        const auto i = static_cast<size_t>(t);
        if (t != ft::DrillType::Mix) {
            stairs_[i] = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
            drillBest_[i] = 0.f;
        }
        fullStairs_[i] = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
        out.requestSave = true;
        toast(out, std::string(ft::drillName(t)) + " : retour au niveau 1.", ToastKind::Info);
    } else if (cmd.name == "reset_stats") {
        session_ = ft::TouchStats{};
        recentScores_.clear();
        heat_.clear();
        recentTipCodes_.clear();
        last_ = LastTouchView{};
        chain_ = Chain{};
        lastChain_ = Chain{};
        markerValid_ = false;
        drillSessionReps_.fill(0);
        drillSessionSuccesses_.fill(0);
        toast(out, "Stats de session remises à zéro.", ToastKind::Info);
    } else if (cmd.name == "reset_levels") {
        for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
        for (auto& s : fullStairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
        drillBest_.fill(0.f);
        out.requestSave = true;
        toast(out, "Niveaux des exercices remis au niveau 1.", ToastKind::Info);
    } else if (cmd.name == "reset_all") {
        session_ = ft::TouchStats{};
        total_ = ft::TouchStats{};
        recentScores_.clear();
        heat_.clear();
        recentTipCodes_.clear();
        last_ = LastTouchView{};
        for (auto& s : stairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
        for (auto& s : fullStairs_) s = Staircase(cfg_.drillTarget, cfg_.drillStep, ft::kDrillStartLevel);
        drillBest_.fill(0.f);
        drillSessionReps_.fill(0);
        drillSessionSuccesses_.fill(0);
        out.requestSave = true;
        toast(out, "Toutes les stats de première touche sont effacées.", ToastKind::Info);
    } else {
        toast(out,
              "Commande inconnue : " + cmd.name +
                  " (drill, stop, skip, reset_level, reset_stats, reset_levels, reset_all).",
              ToastKind::Warning);
    }
}

// ---- Drills ---------------------------------------------------------------------------------------------
void FirstTouchModule::startDrill(ft::DrillType type, bool full, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) {
        toast(out, "Active d'abord le module Première touche.", ToastKind::Warning);
        return;
    }
    if (ctx.game.mode != GameMode::Freeplay) {
        toast(out, "Les exercices se lancent en freeplay. Ici, seule l'analyse des touches est active.", ToastKind::Warning);
        return;
    }
    if (!ctx.worldControlAllowed) {
        toast(out, "Exercices indisponibles ici : seule l'analyse des touches est active.", ToastKind::Warning);
        return;
    }
    std::string activity = std::string("Première touche : ") + ft::drillName(type);
    if (full) activity += type == ft::DrillType::Mix ? " (séquence de match)" : " (parcours complet)";
    if (!requestActivity(activity)) {
        toast(out, "Impossible de lancer l'exercice maintenant.", ToastKind::Warning);
        return;
    }
    drill_ = Drill{};
    drill_.active = true;
    drill_.full = full;
    drill_.type = type;
    drill_.current = type == ft::DrillType::Mix ? ft::DrillType::RollIn : type;
    drill_.phase = DrillPhase::Place;
    const uint64_t seed = cfg_.drillSeed != 0
                              ? static_cast<uint64_t>(cfg_.drillSeed)
                              : mix64(static_cast<uint64_t>(ctx.snap.frame) * 0x9E3779B97F4A7C15ULL + (++startCounter_));
    drill_.rng.reseed(seed);
    if (ctx.hasSnapshot && ctx.snap.car.valid) {
        drill_.pose = ft::poseFromCar(ctx.snap.car);
        drill_.havePose = true;
        drill_.wallSide = ctx.snap.car.pos.x >= 0.f ? 1.f : -1.f;  // the nearest side wall first, then alternate
    }
    drill_.now = ctx.snap.time;
    cancelPending();
    if (full && type == ft::DrillType::Mix) {
        // Séquence de match: the first ball comes after the same short gap as the others, its name shown.
        drill_.phase = DrillPhase::Pause;
        drill_.deadline = ctx.snap.time + ft::kMatchGap;
        drill_.nextType = ft::pickMixType(drill_.rng, drill_.current, mixWeights(false, nullptr));
        drill_.haveNext = true;
    }
    std::string msg;
    if (full && type == ft::DrillType::Mix) {
        msg = "Séquence de match lancée : " + std::to_string(ft::kMatchBalls) + " balles, réussis-en " +
              std::to_string(ft::kMatchToPass) + ".";
    } else if (full) {
        const float level = fullStairs_[static_cast<size_t>(type)].level();
        msg = std::string("Parcours complet lancé : ") + ft::drillName(type) + ", niveau " +
              std::to_string(ft::playerLevel(level)) + "/10.";
    } else {
        msg = std::string("Exercice lancé : ") + ft::drillName(type);
        if (type != ft::DrillType::Mix) {
            const float level = stairs_[static_cast<size_t>(type)].level();
            msg += ", niveau " + std::to_string(ft::playerLevel(level)) + "/10. " + ft::drillGoal(level);
        } else {
            msg += ".";
        }
    }
    toast(out, msg, ToastKind::Ok);
}

void FirstTouchModule::stopDrill(Actions* out, const std::string& message) {
    if (!drill_.active) return;
    if (ownsActivity()) releaseActivity();
    drill_ = Drill{};
    if (out) {
        if (!message.empty()) toast(*out, message, ToastKind::Info);
        out->requestSave = true;
    }
}

void FirstTouchModule::onActivityStopped() {
    // Another module took the world control, or it is no longer allowed: stop without touching the world.
    drill_ = Drill{};
}

void FirstTouchModule::recordDrillRep(bool success) {
    const auto i = static_cast<size_t>(drill_.current);
    if (i >= stairs_.size()) return;
    stairs_[i].record(success);
    drillBest_[i] = std::max(drillBest_[i], stairs_[i].level());
    ++drill_.reps;
    if (success) ++drill_.successes;
    ++drillSessionReps_[i];
    if (success) ++drillSessionSuccesses_[i];
    drill_.idleReps = 0;
}

std::array<float, ft::kDrillKinds> FirstTouchModule::mixWeights(bool matchOnly, const ft::DrillPose* pose) const {
    std::array<float, ft::kDrillKinds> w{};
    for (int i = 0; i < ft::kDrillKinds; ++i) {
        const auto t = static_cast<ft::DrillType>(i);
        const Staircase& s = stairs_[static_cast<size_t>(i)];
        if (!ft::mixUnlocked(t, s.level(), s.successes())) continue;
        w[static_cast<size_t>(i)] = ft::mixWeight(s.recentRate());
    }
    // A wall ball needs the car near a side wall when the car is not placed by the drill.
    float sigma = 0.f;
    if (matchOnly && pose && !ft::wallInReach(*pose, sigma)) w[static_cast<size_t>(ft::DrillType::Wall)] = 0.f;
    return w;
}

void FirstTouchModule::drillRepDone(bool success, float score, const std::string& tip, Actions& out) {
    const std::string outcome = (success ? "Réussi ! " : "Raté : ") + intText(score) + "/100";
    if (drill_.full && drill_.type == ft::DrillType::Mix) {
        matchBallDone(success, outcome, success ? std::string() : tip, out);
        return;
    }
    recordDrillRep(success);
    drill_.lastSuccess = success;
    // Verdict and score on one line, the single most important tip on its own (kept for the next rep).
    drill_.lastOutcome = outcome;
    drill_.lastTip = success ? std::string() : tip;
    drill_.phase = DrillPhase::Pause;
    drill_.deadline = drill_.now + cfg_.drillPause;
    if (drill_.type == ft::DrillType::Mix) {
        // The next ball is drawn now: its name shows during the pause and the hold.
        drill_.nextType = ft::pickMixType(drill_.rng, drill_.current, mixWeights(false, nullptr));
        drill_.haveNext = true;
    }
    out.requestSave = true;
}

void FirstTouchModule::fullRepDone(bool success, Actions& out) {
    if (drill_.phase == DrillPhase::Pause || !drill_.steps.finished()) return;
    const auto i = static_cast<size_t>(drill_.type);
    fullStairs_[i].record(success);
    ++drill_.reps;
    if (success) ++drill_.successes;
    drill_.idleReps = 0;
    drill_.lastSuccess = success;
    drill_.lastOutcome = success ? std::string("Parcours réussi !") : drill_.steps.missingText();
    drill_.lastTip = success ? std::string() : drill_.steps.failure().tip;
    drill_.phase = DrillPhase::Pause;
    drill_.deadline = drill_.now + cfg_.drillPause;
    out.requestSave = true;
}

void FirstTouchModule::matchBallDone(bool success, const std::string& outcome, const std::string& tip, Actions& out) {
    ++drill_.matchBall;
    if (success) ++drill_.matchPassed;
    drill_.idleReps = 0;
    drill_.lastSuccess = success;
    drill_.lastOutcome = "Balle " + std::to_string(drill_.matchBall) + "/" + std::to_string(ft::kMatchBalls) + " : " +
                         outcome;
    drill_.lastTip = tip;
    drill_.phase = DrillPhase::Pause;
    drill_.deadline = drill_.now + ft::kMatchGap;
    if (drill_.matchBall >= ft::kMatchBalls) {
        const bool ok = drill_.matchPassed >= ft::kMatchToPass;
        fullStairs_[static_cast<size_t>(ft::DrillType::Mix)].record(ok);
        ++drill_.reps;
        if (ok) ++drill_.successes;
        drill_.lastSuccess = ok;
        drill_.lastOutcome = "Séquence : " + std::to_string(drill_.matchPassed) + "/" + std::to_string(ft::kMatchBalls) +
                             (ok ? " réussies !" : " réussies, il en faut " + std::to_string(ft::kMatchToPass) + ".");
        drill_.lastTip = ok ? std::string() : (tip.empty() ? "Reprends chaque balle calmement : place-toi avant elle." : tip);
        drill_.matchBall = 0;
        drill_.matchPassed = 0;
        drill_.deadline = drill_.now + std::max(cfg_.drillPause, ft::kMatchGap) + 1.f;
    }
    drill_.nextType = ft::pickMixType(drill_.rng, drill_.current, mixWeights(false, nullptr));
    drill_.haveNext = true;
    out.requestSave = true;
}

void FirstTouchModule::updateLiveFacts(const Snapshot& s) {
    if (!s.car.valid || !s.ball.valid) return;
    drill_.ballDist = distance(hitboxCenter(s.car), s.ball.pos);
    // Speed of the car along the ball's path minus the ball's speed (roulante qui s'éloigne).
    const Vec3 dir = normalize(flat(s.ball.vel), flat(s.car.basis.fwd));
    drill_.speedDiff = dot(flat(s.car.vel), dir) - length2D(s.ball.vel);
    if (drill_.havePrevBall && drill_.prevBallVz < -20.f && s.ball.vel.z > drill_.prevBallVz + 40.f &&
        s.ball.vel.z >= -5.f && s.ball.pos.z < 93.15f + 40.f)
        ++drill_.floorBounces;
    drill_.prevBallVz = s.ball.vel.z;
    drill_.havePrevBall = true;
}

void FirstTouchModule::placeRep(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (!s.car.valid || !s.ball.valid) return;
    if (drill_.full && drill_.type == ft::DrillType::Mix) {
        placeMatchBall(ctx, out);
        return;
    }
    if (drill_.full) {
        placeFullRep(ctx, out);
        return;
    }
    if (!drill_.havePose || !cfg_.drillResetCar) {
        drill_.pose = ft::poseFromCar(s.car);
        drill_.havePose = true;
    }
    if (drill_.type == ft::DrillType::Mix) {
        drill_.current = drill_.haveNext ? drill_.nextType
                                         : ft::pickMixType(drill_.rng, drill_.current, mixWeights(false, nullptr));
        drill_.haveNext = false;
    } else {
        drill_.current = drill_.type;
    }
    const auto i = static_cast<size_t>(drill_.current);
    drill_.repLevel = stairs_[i].level();
    drill_.repCriteria = criteriaFor(drill_.repLevel);
    ft::SpawnContext sc;
    sc.team = s.car.team;
    sc.placeCar = cfg_.drillResetCar;
    sc.wallSide = drill_.wallSide;
    const ft::DrillPose pose =
        cfg_.drillResetCar ? ft::drillPoseFor(drill_.current, drill_.pose, s.car.team) : ft::poseFromCar(s.car);
    const ft::DrillSpawn spawn = ft::makeSpawn(drill_.current, pose, drill_.repLevel, drill_.rng, sc);
    if (!spawn.valid) {  // Chute du mur without car reset: wait until a side wall is close enough
        drill_.waitingWall = true;
        return;
    }
    drill_.waitingWall = false;
    if (drill_.current == ft::DrillType::Wall && cfg_.drillResetCar) drill_.wallSide = -drill_.wallSide;
    drill_.spawn = spawn;
    drill_.repPose = spawn.placesCar ? spawn.carPose : pose;

    // What is drawn during the hold and the rep: the path, the landing circle, the meeting point.
    drill_.showLanding = drill_.showMeet = false;
    if (spawn.hang == ft::HangRelease::None) {
        drill_.path = ft::predictBall(spawn.ball.pos, spawn.ball.vel, spawn.ball.angVel, 4.f);
        const bool airborneBall = drill_.current == ft::DrillType::Lob || drill_.current == ft::DrillType::Wall ||
                                  drill_.current == ft::DrillType::High;
        if (airborneBall && !drill_.path.floorBounces.empty()) {
            drill_.showLanding = true;
            drill_.landing = drill_.path.floorBounces.front();
        }
        if (drill_.current == ft::DrillType::Pass) {
            // Meeting point: the predicted point closest to the car's route straight ahead.
            float best = 1e9f;
            for (const Vec3& p : drill_.path.points) {
                const Vec3 rel = flat(p - drill_.repPose.pos);
                const float along = dot(rel, drill_.repPose.fwd);
                if (along < 200.f) continue;
                const float off = length(rel - drill_.repPose.fwd * along);
                if (off < best) {
                    best = off;
                    drill_.meet = Vec3{p.x, p.y, 93.15f};
                }
            }
            drill_.showMeet = best < 1e8f;
        }
    } else {
        drill_.path = ft::BallTrack{};
    }

    // Get-ready hold: car and ball held still; the first ball waits for the throttle.
    drill_.carHeld = cfg_.drillResetCar;
    drill_.hold = HoldPose{};
    drill_.hold.car = ft::carPlacementFor(drill_.repPose, false);
    BallPlacement held;
    held.pos = spawn.ball.pos;
    drill_.hold.ball = held;
    if (cfg_.drillFullBoost) drill_.hold.boost = 100.f;
    float freeze = ft::freezeSeconds(drill_.repLevel);
    if (drill_.type == ft::DrillType::Mix) freeze = std::max(freeze, ft::kMixFreeze);
    // Only a held car gets the freeze: a ball frozen in front of a car that drives freely would erase its touches.
    if (drill_.firstRep && cfg_.drillResetCar)
        drill_.ready.start(s.time, ReadyPhase::firstAttempt());
    else if (freeze > 0.f && drill_.carHeld)
        drill_.ready.start(s.time, ReadyPhase::timed(freeze));
    else
        drill_.ready.stop();

    drill_.hang.stop();
    drill_.hangMode = ft::HangRelease::None;
    drill_.underSince = -1.f;
    drill_.underBall = false;
    drill_.floorBounces = 0;
    drill_.havePrevBall = false;
    drill_.placeFrame = s.frame;
    ignoreTouchesUntil_ = s.frame;
    cancelPending();
    endChain();
    havePrev_ = false;  // our own teleport is not a "reset"
    if (drill_.ready.active()) {
        drill_.phase = DrillPhase::Ready;
        applyHold(drill_.hold, &s, out);  // the get-ready phase only runs with the car held
    } else {
        launchRep(s, out);
    }
}

void FirstTouchModule::placeFullRep(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    if (!drill_.havePose) {
        drill_.pose = ft::poseFromCar(s.car);
        drill_.havePose = true;
    }
    drill_.current = drill_.type;
    drill_.repLevel = fullStairs_[static_cast<size_t>(drill_.type)].level();
    drill_.repCriteria = criteriaFor(drill_.repLevel);
    drill_.fullSetup = ft::makeFullSetup(drill_.type, drill_.pose, s.car.team, drill_.repLevel, drill_.rng);
    drill_.repPose = drill_.fullSetup.car;
    drill_.path = drill_.fullSetup.track;
    drill_.showLanding = drill_.fullSetup.landingTime >= 0.f &&
                         (drill_.type == ft::DrillType::Lob || drill_.type == ft::DrillType::High);
    drill_.landing = drill_.fullSetup.landing;
    drill_.showMeet = drill_.fullSetup.targetTime >= 0.f;
    drill_.meet = drill_.fullSetup.target;
    drill_.spawn = ft::DrillSpawn{};
    drill_.spawn.type = drill_.type;
    drill_.spawn.ball = drill_.fullSetup.ball;
    drill_.carHeld = true;  // a parcours complet always starts from its own place
    drill_.hold = HoldPose{};
    drill_.hold.car = ft::carPlacementFor(drill_.repPose, false);
    BallPlacement held;
    held.pos = drill_.fullSetup.ball.pos;
    drill_.hold.ball = held;
    if (cfg_.drillFullBoost) drill_.hold.boost = 100.f;
    drill_.ready.start(s.time, drill_.firstRep ? ReadyPhase::firstAttempt() : ReadyPhase::autoReset());
    drill_.steps.clear();
    drill_.touchPending = false;
    drill_.hang.stop();
    drill_.hangMode = ft::HangRelease::None;
    drill_.floorBounces = 0;
    drill_.havePrevBall = false;
    drill_.placeFrame = s.frame;
    ignoreTouchesUntil_ = s.frame;
    cancelPending();
    endChain();
    havePrev_ = false;
    drill_.phase = DrillPhase::Ready;
    applyHold(drill_.hold, &s, out);
}

void FirstTouchModule::placeMatchBall(const TickContext& ctx, Actions& out) {
    // Séquence de match: the car is never placed; the ball comes relative to where it is and where it goes.
    const Snapshot& s = ctx.snap;
    const ft::DrillPose pose = ft::matchPose(s.car);
    const auto weights = mixWeights(true, &pose);
    if (!drill_.haveNext || weights[static_cast<size_t>(drill_.nextType)] <= 0.f)
        drill_.nextType = ft::pickMixType(drill_.rng, drill_.current, weights);
    drill_.current = drill_.nextType;
    drill_.haveNext = false;
    drill_.repLevel = stairs_[static_cast<size_t>(drill_.current)].level();
    drill_.repCriteria = criteriaFor(drill_.repLevel);
    ft::SpawnContext sc;
    sc.team = s.car.team;
    sc.placeCar = false;
    ft::DrillSpawn spawn = ft::makeSpawn(drill_.current, pose, drill_.repLevel, drill_.rng, sc);
    if (!spawn.valid) {
        drill_.current = ft::DrillType::RollIn;
        spawn = ft::makeSpawn(drill_.current, pose, drill_.repLevel, drill_.rng, sc);
    }
    drill_.spawn = spawn;
    drill_.repPose = pose;
    drill_.path = ft::BallTrack{};
    drill_.showLanding = drill_.showMeet = false;
    drill_.carHeld = false;
    drill_.ready.stop();
    drill_.hang.stop();
    drill_.hangMode = ft::HangRelease::None;
    drill_.underSince = -1.f;
    drill_.floorBounces = 0;
    drill_.havePrevBall = false;
    cancelPending();
    endChain();
    havePrev_ = false;
    launchRep(s, out);
}

void FirstTouchModule::launchRep(const Snapshot& s, Actions& out) {
    const bool wasHeld = drill_.phase == DrillPhase::Ready;
    const ft::DrillSpawn& sp = drill_.spawn;
    if (sp.hang != ft::HangRelease::None) {
        drill_.hangMode = sp.hang;
        drill_.hangVz = sp.releaseVz;
        drill_.underSince = -1.f;
        drill_.hang.hang(sp.ball.pos);
        drill_.hang.update(s, out, touchFrame_ == s.frame);  // held from this tick
    } else {
        out.setBall = sp.ball;
    }
    // A car that was not held is placed now (no hold at the high levels); a held car is left to the player's inputs.
    if (drill_.carHeld && !wasHeld) out.setCar = ft::carPlacementFor(drill_.repPose, cfg_.drillFullBoost);
    else if (cfg_.drillFullBoost) out.setBoost = 100.f;
    drill_.ready.stop();
    drill_.placeFrame = s.frame;
    ignoreTouchesUntil_ = s.frame;
    drill_.goTime = s.time;
    drill_.repInput = false;
    drill_.firstRep = false;
    drill_.phase = DrillPhase::WaitTouch;
    cancelPending();
    endChain();
    havePrev_ = false;
    if (drill_.full && drill_.type != ft::DrillType::Mix) {
        ft::fullBegin(drill_.fullState, drill_.fullSetup, drill_.type, ft::playerLevel(drill_.repLevel), s.time);
        drill_.steps.setSteps(ft::fullSteps(drill_.fullState));
        drill_.steps.start(s.time);
        drill_.touchPending = false;
        drill_.deadline = s.time + drill_.fullSetup.timeLimit;
    } else {
        drill_.deadline = s.time + drill_.repCriteria.timeout;
    }
}

void FirstTouchModule::tickHanging(const Snapshot& s, Actions& out) {
    drill_.underBall = drill_.hang.hanging() && ft::ballOverFrontRoof(s.car, drill_.hang.position());
    if (!drill_.hang.hanging()) return;
    const bool jumpEdge = s.in.jump && !drill_.prevJump;
    if (drill_.underBall) {
        if (drill_.underSince < 0.f) drill_.underSince = s.time;
    } else {
        drill_.underSince = -1.f;
    }
    bool release = false;
    if (drill_.hangMode == ft::HangRelease::OnJump)
        release = drill_.underBall && (jumpEdge || s.time - drill_.underSince >= ft::kHangIdleRelease);
    else if (drill_.hangMode == ft::HangRelease::Under)
        release = drill_.underBall;
    if (release) drill_.hang.release(Vec3{0.f, 0.f, drill_.hangVz});
    drill_.hang.update(s, out, touchFrame_ == s.frame);
}

void FirstTouchModule::tickFull(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    ft::fullTick(drill_.fullState, s);
    if (s.frame > drill_.placeFrame + 1 && inputActive(s.in)) drill_.repInput = true;
    drill_.steps.update(s, &ctx);
    if (drill_.touchPending) {
        drill_.touchPending = false;
        ft::fullOnTouch(drill_.fullState, drill_.touchPre);
        const Step* cur = drill_.steps.running() ? drill_.steps.currentStep() : nullptr;
        if (cur && cur->id == ft::kFullTouchStep) {
            const StepVerdict v = ft::fullTouchVerdict(drill_.fullState, drill_.touchMetrics, drill_.touchPre);
            if (v.state == StepState::Done) drill_.steps.complete(s.time);
            else drill_.steps.fail(s.time, v.code, v.tip);
        } else if (cur && drill_.steps.indexOf(cur->id) < drill_.steps.indexOf(ft::kFullTouchStep)) {
            drill_.steps.fail(s.time, "ft.full.early_touch", ft::fullEarlyTouchTip(drill_.type, cur->id));
        }
    }
    if (drill_.steps.running() && s.time >= drill_.deadline) {
        if (!drill_.repInput && !drill_.fullState.touched) {
            // Nothing done at all: the rep is ignored, not failed (like the drills).
            ++drill_.idleReps;
            drill_.steps.reset();
            drill_.lastSuccess = false;
            drill_.lastOutcome = "Aucune action : essai ignoré.";
            drill_.lastTip.clear();
            if (drill_.idleReps >= 3) {
                stopDrill(&out, "Exercice arrêté : aucune action détectée. Relance-le quand tu veux.");
                return;
            }
            drill_.phase = DrillPhase::Pause;
            drill_.deadline = s.time + cfg_.drillPause;
            return;
        }
        drill_.steps.failOnTimeout(s.time);  // never silent: the missing step and its tip
    }
    if (drill_.steps.finished()) fullRepDone(drill_.steps.isComplete(), out);
}

void FirstTouchModule::tickDrill(const TickContext& ctx, Actions& out) {
    if (!drill_.active) return;
    if (!ctx.ownsActivity) {  // the engine gave the activity to someone else
        drill_ = Drill{};
        return;
    }
    if (!ctx.worldControlAllowed || ctx.game.mode != GameMode::Freeplay) {
        stopDrill(&out, "Exercice arrêté : exercices indisponibles ici.");
        return;
    }
    const Snapshot& s = ctx.snap;
    drill_.now = s.time;
    if (s.paused) return;
    updateLiveFacts(s);
    switch (drill_.phase) {
    case DrillPhase::Place: placeRep(ctx, out); break;
    case DrillPhase::Ready:
        if (drill_.ready.update(s, out)) {
            launchRep(s, out);
        } else {
            applyHold(drill_.hold, &s, out);  // the get-ready phase only runs with the car held
            ignoreTouchesUntil_ = s.frame;
            havePrev_ = false;
        }
        break;
    case DrillPhase::WaitTouch: {
        if (drill_.full && drill_.type != ft::DrillType::Mix) {
            tickFull(ctx, out);
            break;
        }
        if (drill_.hangMode != ft::HangRelease::None) tickHanging(s, out);
        if (s.frame > drill_.placeFrame + 1 && inputActive(s.in)) drill_.repInput = true;
        if (s.time < drill_.deadline) break;
        const std::string tip = drill_.hangMode == ft::HangRelease::OnJump
                                    ? "Mets la balle au-dessus de l'avant de ton toit, puis saute."
                                    : "Va chercher la balle plus tôt.";
        if (drill_.repInput && drill_.full) {
            matchBallDone(false, "temps écoulé", tip, out);
            break;
        }
        if (drill_.repInput) {
            recordDrillRep(false);
            drill_.lastOutcome = "Temps écoulé.";
            drill_.lastTip = tip;
            out.requestSave = true;
        } else {
            ++drill_.idleReps;
            drill_.lastOutcome = "Aucune action : essai ignoré.";
            drill_.lastTip.clear();
            if (drill_.idleReps >= 3) {
                stopDrill(&out, "Exercice arrêté : aucune action détectée. Relance-le quand tu veux.");
                return;
            }
        }
        drill_.lastSuccess = false;
        drill_.phase = DrillPhase::Pause;
        drill_.deadline = s.time + (drill_.full ? ft::kMatchGap : cfg_.drillPause);
        if (drill_.type == ft::DrillType::Mix) {
            drill_.nextType = ft::pickMixType(drill_.rng, drill_.current, mixWeights(false, nullptr));
            drill_.haveNext = true;
        }
        break;
    }
    case DrillPhase::Evaluate:
        if (!pending_.window.active()) drill_.phase = DrillPhase::Place;  // evaluation cancelled: replay the rep
        break;
    case DrillPhase::Pause:
        if (s.time >= drill_.deadline) drill_.phase = DrillPhase::Place;
        break;
    case DrillPhase::Idle: break;
    }
    drill_.prevJump = s.in.jump;
}

// ---- Views ----------------------------------------------------------------------------------------------
FirstTouchModule::DrillView FirstTouchModule::drillStatus() const {
    DrillView v;
    v.active = drill_.active;
    if (!drill_.active) return v;
    v.full = drill_.full;
    v.type = drill_.type;
    v.current = drill_.current;
    v.phase = drill_.phase;
    if (drill_.phase == DrillPhase::Place)
        v.level = drill_.full ? fullStairs_[static_cast<size_t>(drill_.type)].level()
                              : stairs_[static_cast<size_t>(drill_.current)].level();
    else
        v.level = drill_.repLevel;
    v.playerLevel = ft::playerLevel(v.level);
    v.criteria = drill_.phase == DrillPhase::Place ? criteriaFor(v.level) : drill_.repCriteria;
    v.reps = drill_.reps;
    v.successes = drill_.successes;
    if (drill_.phase == DrillPhase::WaitTouch || drill_.phase == DrillPhase::Pause)
        v.timeLeft = std::max(0.f, drill_.deadline - drill_.now);
    v.instruction = ft::drillInstruction(drill_.current, profile_, keyStyle_);
    v.lastOutcome = drill_.lastOutcome;
    v.lastTip = drill_.lastTip;
    v.lastSuccess = drill_.lastSuccess;
    v.readyLine = drill_.phase == DrillPhase::Ready ? drill_.ready.hudLine(drill_.now) : std::string();
    if (drill_.type == ft::DrillType::Mix) {
        if (drill_.haveNext) v.nextBall = ft::drillName(drill_.nextType);
        else if (drill_.phase == DrillPhase::Ready) v.nextBall = ft::drillName(drill_.current);
    }
    if (drill_.full && drill_.type != ft::DrillType::Mix) {
        if (!drill_.steps.idle()) {
            std::vector<PanelLine> lines;
            StepHudStyle st;
            st.flashSeconds = 0.f;
            appendStepLines(lines, drill_.steps, drill_.now, st);
            for (const auto& l : lines) v.steps.push_back(l.text);
        } else {
            v.steps = ft::fullLabels(drill_.type, v.playerLevel);
        }
    }
    v.matchBall = drill_.matchBall;
    v.matchPassed = drill_.matchPassed;
    v.waitingWall = drill_.waitingWall;
    return v;
}

FirstTouchModule::DrillLevelView FirstTouchModule::drillLevel(ft::DrillType t) const {
    DrillLevelView v;
    const auto i = static_cast<size_t>(t);
    if (i < fullStairs_.size()) {
        const Staircase& f = fullStairs_[i];
        v.fullLevel = f.level();
        v.fullAttempts = f.attempts();
        v.fullSuccesses = f.successes();
    }
    if (i >= stairs_.size()) return v;
    const Staircase& s = stairs_[i];
    v.level = s.level();
    v.best = std::max(drillBest_[i], s.level());
    v.attempts = s.attempts();
    v.successes = s.successes();
    v.recentRate = s.recentRate();
    v.sessionReps = drillSessionReps_[i];
    v.sessionSuccesses = drillSessionSuccesses_[i];
    v.mixUnlocked = ft::mixUnlocked(t, s.level(), s.successes());
    return v;
}

FirstTouchModule::DribbleView FirstTouchModule::dribble() const {
    const Chain& c = chain_.active && chain_.contacts >= 1 ? chain_ : lastChain_;
    DribbleView v;
    v.active = chain_.active && chain_.contacts >= 2;
    v.contacts = c.contacts;
    v.seconds = std::max(0.f, c.last - c.start);
    v.cadence = c.contacts >= 2 && v.seconds > 0.05f ? static_cast<float>(c.contacts - 1) / v.seconds : 0.f;
    return v;
}

std::string FirstTouchModule::persistentTipCode() const { return ft::persistentCode(recentTipCodes_, 3); }

std::string FirstTouchModule::trend() const { return ft::trendText(session_, unit_, cfg_.goodZone * 0.66f); }

// ---- Persistence ----------------------------------------------------------------------------------------
void FirstTouchModule::save(Store& store) const {
    total_.save(store, "firsttouch.total");
    std::vector<std::string> scores;
    const size_t n0 = recentScores_.size() > kMaxScoresSaved ? recentScores_.size() - kMaxScoresSaved : 0;
    for (size_t i = n0; i < recentScores_.size(); ++i) scores.push_back(formatNumber(recentScores_.at(i), 0));
    store.setList("firsttouch.scores", scores);
    std::vector<std::string> heat;
    const size_t h0 = heat_.size() > kMaxHeatSaved ? heat_.size() - kMaxHeatSaved : 0;
    for (size_t i = h0; i < heat_.size(); ++i) {
        const ft::HeatPoint& p = heat_.at(i);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.3f;%.3f;%.3f;%.0f;%d", static_cast<double>(p.x), static_cast<double>(p.y),
                      static_cast<double>(p.z), static_cast<double>(p.score), p.type);
        heat.push_back(buf);
    }
    store.setList("firsttouch.heat", heat);
    for (int i = 0; i < ft::kDrillKinds; ++i) {
        const std::string key = std::string("firsttouch.drill.") + ft::drillId(static_cast<ft::DrillType>(i));
        stairs_[static_cast<size_t>(i)].save(store, key);
        store.setFloat(key + ".best", drillBest_[static_cast<size_t>(i)]);
    }
    for (int i = 0; i <= ft::kDrillKinds; ++i)
        fullStairs_[static_cast<size_t>(i)].save(store, std::string("firsttouch.full.") +
                                                            ft::drillId(static_cast<ft::DrillType>(i)));
}

void FirstTouchModule::load(const Store& store) {
    total_.load(store, "firsttouch.total");
    recentScores_.clear();
    const std::vector<std::string> scores = store.getList("firsttouch.scores");
    const size_t s0 = scores.size() > kMaxScoresSaved ? scores.size() - kMaxScoresSaved : 0;
    for (size_t i = s0; i < scores.size(); ++i) {
        float f = 0.f;
        if (parseFloatValue(scores[i], f)) recentScores_.push(clampf(f, 0.f, 100.f));
    }
    heat_.clear();
    const std::vector<std::string> heat = store.getList("firsttouch.heat");
    const size_t h0 = heat.size() > kMaxHeatSaved ? heat.size() - kMaxHeatSaved : 0;
    for (size_t i = h0; i < heat.size(); ++i) {
        float v[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
        int n = 0;
        size_t start = 0;
        const std::string& e = heat[i];
        while (n < 5 && start <= e.size()) {
            const size_t end = e.find(';', start);
            const std::string part = e.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!parseFloatValue(part, v[n])) break;
            ++n;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (n != 5) continue;
        ft::HeatPoint p;
        p.x = clampf(v[0], -1.f, 1.f);
        p.y = clampf(v[1], -1.f, 1.f);
        p.z = clampf(v[2], -1.f, 1.f);
        p.score = clampf(v[3], 0.f, 100.f);
        // Clamp as a float first: a corrupt entry (1e30, -1e30) must not overflow the int conversion.
        p.type = static_cast<int>(clampf(v[4], 0.f, static_cast<float>(ft::kTouchTypeCount - 1)));
        heat_.push(p);
    }
    for (int i = 0; i < ft::kDrillKinds; ++i) {
        const std::string key = std::string("firsttouch.drill.") + ft::drillId(static_cast<ft::DrillType>(i));
        auto& s = stairs_[static_cast<size_t>(i)];
        // Only a played level is kept: the previous version saved untried drills at its old start value.
        if (store.getInt(key + ".attempts", 0) > 0) s.load(store, key);
        drillBest_[static_cast<size_t>(i)] = clampf(store.getFloat(key + ".best", s.level()), 0.f, 1.f);
    }
    for (int i = 0; i <= ft::kDrillKinds; ++i) {
        const std::string key = std::string("firsttouch.full.") + ft::drillId(static_cast<ft::DrillType>(i));
        if (store.getInt(key + ".attempts", 0) > 0) fullStairs_[static_cast<size_t>(i)].load(store, key);
    }
}

} // namespace rlt
