#include "core/modules/overlay/OverlayModule.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "core/common/Controls.h"
#include "core/common/Text.h"

namespace rlt {

namespace {
constexpr int64_t kMaxCountedGapTicks = 30;   // a tick gap longer than 0.25 s (load, hitch) counts as 0.25 s
constexpr float kMaxFrameDt = 0.1f;           // draw-side smoothing step clamp (s)
constexpr float kTeleportDistance = 600.f;    // ball jump between two frames that snaps the zone direction (uu)
constexpr float kOccluderMargin = 2.f;        // occluder slightly smaller than the ball: the circle sits ON it
constexpr int kSphereSegments = 12;           // 3 great circles x 12 = 36 lines
constexpr float kZOffsetPerScale = 75.f;      // original: Z line 225 px right of the value at text size 3
constexpr float kCharWidthPerScale = 8.3f;    // estimated canvas glyph width at scale 1 (px)
constexpr double kMaxDaySeconds = 90000.0;    // sanity bound of one persisted day
constexpr double kMaxTotalSeconds = 1e9;      // sanity bound of the persisted total (31 years)
constexpr double kMaxSessionSeconds = 1e7;    // sanity bound of the persisted longest session

SpeedUnit unitFromIndex(int i) {
    switch (i) {
    case 1: return SpeedUnit::Kph;
    case 2: return SpeedUnit::Mph;
    case 3: return SpeedUnit::Uus;
    default: return SpeedUnit::Auto;
    }
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool validDate(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (d[i] < '0' || d[i] > '9') return false;
    }
    return true;
}

// Ticks of a persisted value, 0 when it is missing, negative or beyond `hiSeconds`. Returns false when unreadable.
bool loadTicks(const std::string& text, double hiSeconds, int64_t& ticks) {
    ticks = 0;
    int64_t t = 0;
    if (!OverlayModule::parseSecondsText(text, t)) return false;
    if (t > 0 && static_cast<double>(t) <= hiSeconds * static_cast<double>(kTickRate)) ticks = t;
    return true;
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

OverlayModule::OverlayModule() : clock_(&steadySeconds) {}

// ---- Time accounting helpers -----------------------------------------------------------------------------------
int64_t OverlayModule::secondsToTicks(double seconds) {
    if (!std::isfinite(seconds)) return 0;
    const double t = seconds * static_cast<double>(kTickRate);
    if (t >= 9.0e15) return static_cast<int64_t>(9.0e15);
    if (t <= -9.0e15) return static_cast<int64_t>(-9.0e15);
    return static_cast<int64_t>(std::llround(t));
}

std::string OverlayModule::secondsText(int64_t ticks) {
    // Integer arithmetic only: exact, and independent of the C locale (no decimal comma).
    const int64_t rate = static_cast<int64_t>(kTickRate);
    const bool neg = ticks < 0;
    const int64_t a = neg ? -ticks : ticks;
    const int64_t whole = a / rate;
    const int64_t millis = ((a % rate) * 1000 + rate / 2) / rate;   // at most 992: never carries into `whole`
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%lld.%03lld", neg ? "-" : "", static_cast<long long>(whole),
                  static_cast<long long>(millis));
    return buf;
}

bool OverlayModule::parseSecondsText(const std::string& text, int64_t& ticks) {
    // Fast exact path: "digits[.digits]" (what secondsText writes, and the older "%.1f" day entries).
    size_t i = 0;
    int64_t whole = 0;
    int wholeDigits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9' && wholeDigits < 15) {
        whole = whole * 10 + (text[i] - '0');
        ++wholeDigits;
        ++i;
    }
    const bool exact = wholeDigits > 0;
    int64_t frac = 0;     // micro seconds
    if (exact && i < text.size() && text[i] == '.') {
        ++i;
        int64_t scale = 100000;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            frac += (text[i] - '0') * scale;
            scale /= 10;
            ++i;
        }
    }
    if (exact && i == text.size()) {
        const int64_t rate = static_cast<int64_t>(kTickRate);
        ticks = whole * rate + (frac * rate + 500000) / 1000000;
        return true;
    }
    // Anything else ("1.2e+06" written by older versions, a sign): generic float parse.
    float f = 0.f;
    if (!parseFloatValue(text, f)) return false;
    ticks = secondsToTicks(static_cast<double>(f));
    return true;
}

// ---- Settings ------------------------------------------------------------------------------------------------
void OverlayModule::describeSettings(SettingsSchema& schema) {
    using C = OverlayConfig;
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionActivation);
        w.toggle("rlt_ov_speed_enabled", &C::speedEnabled, false, "Écart de vitesse",
                 "Affiche en bas de l'écran la différence de vitesse entre ta voiture et la balle. "
                 "Vert : tu arrives à la bonne vitesse pour un contact contrôlé.");
        w.toggle("rlt_ov_timer_enabled", &C::timerEnabled, false, "Chrono de session",
                 "Affiche depuis combien de temps tu t'entraînes sur cette map (temps de jeu, s'arrête en pause).");
        w.toggle("rlt_ov_zone_enabled", &C::zoneEnabled, false, "Cercle de contact",
                 "Dessine sur la balle la zone à toucher : dessous pour la garder en l'air, décalée selon sa vitesse.");
        w.toggle("rlt_ov_sphere_enabled", &C::sphereEnabled, false, "Sphère centrale",
                 "Petite sphère au centre de la balle quand elle est en l'air, pour viser le centre.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionSpeed);
        w.choice("rlt_ov_speed_mode", &C::speedMode, 0, {"XY (horizontal)", "XYZ (3D)", "XY + Z", "Vitesse d'approche"},
                 "Calcul",
                 "XY : vitesse horizontale de ta voiture moins celle de la balle. XYZ : vitesses en 3D. "
                 "XY + Z : ajoute à droite une petite ligne avec l'écart vertical. Vitesse d'approche : à quelle "
                 "vitesse tu te rapproches de la balle (positif = tu arrives plus vite qu'elle ne s'éloigne).");
        w.choice("rlt_ov_speed_unit", &C::speedUnit, 0, {"Auto (réglage du jeu)", "KPH", "MPH", "uu/s"}, "Unité",
                 "Auto suit l'unité choisie dans les options de Rocket League (KPH en métrique, sinon MPH).");
        w.number("rlt_ov_text_scale", &C::textScale, 3.f, 1.f, 10.f, "Taille du texte",
                 "Taille de l'écart de vitesse et du chrono. 3 = taille d'origine en 1080p, suit la taille de "
                 "l'interface et la résolution.", 1);
        w.toggle("rlt_ov_shadow", &C::shadow, true, "Ombre du texte",
                 "Ajoute une ombre sous le texte de l'écart et du chrono pour mieux le lire.");
        w.number("rlt_ov_speed_x", &C::speedX, 0.4587f, 0.f, 1.f, "Position horizontale",
                 "Bord gauche du texte en fraction de la largeur de l'écran (0 = gauche, 1 = droite). "
                 "Reste au même endroit si tu changes de résolution.", 3);
        w.number("rlt_ov_speed_y", &C::speedY, 0.9524f, 0.f, 1.f, "Position verticale",
                 "Haut du texte en fraction de la hauteur de l'écran (0 = haut, 1 = bas).", 3);
        w.toggle("rlt_ov_speed_last_touch", &C::lastTouchLine, false, "Rappel du dernier contact",
                 "Ajoute au-dessus une petite ligne avec l'écart mesuré juste avant ton dernier contact "
                 "avec la balle.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionTimer);
        w.number("rlt_ov_timer_x", &C::timerX, 0.4695f, 0.f, 1.f, "Position horizontale",
                 "Bord gauche du chrono en fraction de la largeur de l'écran.", 3);
        w.number("rlt_ov_timer_y", &C::timerY, 0.0093f, 0.f, 1.f, "Position verticale",
                 "Haut du chrono en fraction de la hauteur de l'écran.", 3);
        w.color("rlt_ov_timer_color", &C::timerColor, colors::White, "Couleur du chrono",
                "Couleur du chrono. Sa taille et son ombre suivent celles de l'écart de vitesse.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionZone);
        w.choice("rlt_ov_zone_behavior", &C::zoneBehavior, 0,
                 {"Statique", "Pilotée par la vitesse", "Garder en l'air", "Auto"}, "Comportement",
                 "Statique : sous la balle. Pilotée par la vitesse : glisse à l'opposé de sa direction pour faire "
                 "monter un air dribble. Garder en l'air : glisse vers sa direction pour la freiner. Auto : pilotée "
                 "par la vitesse dès que la balle va vite, et toujours sur les maps d'air dribble.");
        w.number("rlt_ov_zone_radius", &C::zoneRadius, 47.f, 30.f, 60.f, "Rayon du cercle",
                 "Taille de la zone dessinée sur la balle, en uu (la balle fait 91 uu de rayon).", 0);
        w.integer("rlt_ov_zone_thickness", &C::zoneThickness, 1, 1, 5, "Épaisseur", "Épaisseur du trait en pixels.");
        w.color(kCvarZoneColor, &C::zoneColor, colors::Red, "Couleur du cercle",
                "Couleur fixe du cercle. La modifier désactive « Couleur de l'écart de vitesse ».");
        w.toggle(kCvarZoneMatch, &C::zoneMatchColor, true, "Couleur de l'écart de vitesse",
                 "Le cercle prend la couleur de l'écart de vitesse actuel (bon, correct, trop d'écart).");
        w.toggle("rlt_ov_only_air", &C::onlyBallInAir, true, "Seulement balle en l'air",
                 "Le cercle et la sphère n'apparaissent que si la balle est plus haute que la hauteur minimale.");
        w.number("rlt_ov_air_height", &C::airHeight, 400.f, 100.f, 1500.f, "Hauteur minimale de la balle",
                 "Hauteur du centre de la balle (uu) au-dessus de laquelle elle compte comme en l'air. "
                 "Elle est posée au sol à 93 uu.", 0);
        w.toggle("rlt_ov_car_below", &C::onlyCarBelowBall, true, "Seulement voiture sous la balle",
                 "Le cercle n'apparaît que si ta voiture est plus basse que le centre de la balle.");
        w.toggle("rlt_ov_zone_occlusion", &C::zoneOcclusion, true, "Masquer derrière la balle",
                 "Cache la partie du cercle située derrière la balle, comme si elle était peinte dessus.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionZone);
        w.advanced();
        w.number("rlt_ov_zone_hidden_alpha", &C::zoneHiddenAlpha, 0.f, 0.f, 1.f, "Opacité de la partie cachée",
                 "0 : la partie derrière la balle est invisible. Plus haut : elle reste visible en transparence.", 2);
        w.number("rlt_ov_zone_max_tilt", &C::zoneMaxTiltDeg, 35.f, 0.f, 80.f, "Inclinaison maximale (°)",
                 "Décalage maximal de la zone depuis le dessous de la balle, atteint à la vitesse maximale.", 0);
        w.number("rlt_ov_zone_min_speed", &C::zoneMinSpeed, 800.f, 0.f, 3000.f, "Vitesse de début (uu/s)",
                 "Vitesse horizontale de la balle à partir de laquelle la zone commence à glisser "
                 "(et seuil du mode Auto).", 0);
        w.number("rlt_ov_zone_max_speed", &C::zoneMaxSpeed, 2100.f, 100.f, 6000.f, "Vitesse maximale (uu/s)",
                 "Vitesse horizontale de la balle à laquelle la zone atteint l'inclinaison maximale.", 0);
        w.number("rlt_ov_zone_smoothing", &C::zoneSmoothing, 0.1f, 0.f, 0.5f, "Lissage (s)",
                 "Temps de réaction de la zone quand la balle change de direction. 0 = instantané (peut trembler).", 2);
        w.integer("rlt_ov_zone_segments", &C::zoneSegments, 32, 12, 64, "Segments du cercle",
                  "Nombre de traits du cercle : plus = plus rond, un peu plus coûteux.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionSphere);
        w.number("rlt_ov_sphere_radius", &C::sphereRadius, 5.f, 1.f, 10.f, "Rayon de la sphère",
                 "Taille de la sphère dessinée au centre de la balle (uu).", 1);
        w.color(kCvarSphereColor, &C::sphereColor, colors::Red, "Couleur de la sphère",
                "Couleur fixe de la sphère. La modifier désactive « Couleur de l'écart de vitesse ».");
        w.toggle(kCvarSphereMatch, &C::sphereMatchColor, true, "Couleur de l'écart de vitesse",
                 "La sphère prend la couleur de l'écart de vitesse actuel.");
    }
    {
        auto w = schema.section(&Settings::overlay, "overlay", kSectionColors);
        w.color("rlt_ov_good_color", &C::goodColor, colors::Green, "Couleur : bon",
                "Couleur quand l'écart est faible : contact contrôlé.");
        w.color("rlt_ov_alright_color", &C::alrightColor, colors::Yellow, "Couleur : correct",
                "Couleur quand l'écart est moyen.");
        w.color("rlt_ov_bad_color", &C::badColor, colors::Red, "Couleur : trop d'écart",
                "Couleur quand l'écart est trop grand : la balle va rebondir loin de toi.");
        w.number("rlt_ov_good_threshold", &C::goodThreshold, 150.f, 0.f, 1000.f, "Seuil bon (uu/s)",
                 "Écart maximal, en valeur absolue, pour la couleur « bon ». 150 uu/s = 5,4 km/h.", 0);
        w.number("rlt_ov_alright_threshold", &C::alrightThreshold, 300.f, 0.f, 2000.f, "Seuil correct (uu/s)",
                 "Écart maximal pour la couleur « correct ». Au-delà : trop d'écart. 300 uu/s = 10,8 km/h.", 0);
    }
}

void OverlayModule::onSettingsChanged(const Settings& settings) {
    cfg_ = settings.overlay;
    if (cfg_.alrightThreshold < cfg_.goodThreshold) cfg_.alrightThreshold = cfg_.goodThreshold;
}

// ---- Ticks: session timer --------------------------------------------------------------------------------------
void OverlayModule::onTick(const TickContext& ctx, Actions& out) {
    (void)out;
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    // Only the snapshot's pause flag: GameContext.paused is refreshed with the context (map, mode, date), not on
    // every pause change, so it can stay stale. Offline, no tick arrives during a pause anyway.
    if (lastTickFrame_ >= 0 && s.frame > lastTickFrame_ && !s.paused) {
        const int64_t df = std::min<int64_t>(s.frame - lastTickFrame_, kMaxCountedGapTicks);
        addTrainingTime(df, ctx.game.localDate);
    }
    lastTickFrame_ = s.frame;
}

void OverlayModule::addTrainingTime(int64_t ticks, const std::string& date) {
    sessionTicks_ += ticks;
    totalTicks_ += ticks;
    if (date != todayDate_) switchDay(date);
    if (todayIdx_ >= 0) days_[static_cast<size_t>(todayIdx_)].ticks += ticks;
}

void OverlayModule::noteDate(const std::string& date) {
    if (date != knownDate_ && validDate(date)) knownDate_ = date;
}

int OverlayModule::findDay(const std::string& date) const {
    auto it = std::lower_bound(days_.begin(), days_.end(), date,
                               [](const DayTicks& d, const std::string& key) { return d.date < key; });
    return (it != days_.end() && it->date == date) ? static_cast<int>(it - days_.begin()) : -1;
}

void OverlayModule::switchDay(const std::string& date) {
    todayDate_ = date;
    todayIdx_ = -1;
    if (!validDate(date)) return;
    knownDate_ = date;
    auto it = std::lower_bound(days_.begin(), days_.end(), date,
                               [](const DayTicks& d, const std::string& key) { return d.date < key; });
    if (it == days_.end() || it->date != date) days_.insert(it, DayTicks{date, 0});
    // Keep the newest kMaxDays entries, never dropping the current day.
    while (days_.size() > static_cast<size_t>(kMaxDays)) {
        auto victim = days_.begin();
        if (victim->date == date) ++victim;
        days_.erase(victim);
    }
    for (size_t i = 0; i < days_.size(); ++i)
        if (days_[i].date == date) todayIdx_ = static_cast<int>(i);
}

void OverlayModule::endSession(Actions& out) {
    if (sessionTicks_ >= secondsToTicks(kMinCountedSession)) {
        ++sessions_;
        longestTicks_ = std::max(longestTicks_, sessionTicks_);
        out.requestSave = true;
    }
    sessionTicks_ = 0;
    lastTickFrame_ = -1;
}

// ---- Touches: last touch reminder -----------------------------------------------------------------------------
void OverlayModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    if (!touch.newContact) return;  // a continuing contact (dribble) keeps the first touch value
    const Snapshot& s = touch.pre;
    if (!s.car.valid || !s.ball.valid) return;
    lastTouch_ = {s.car.pos, s.car.vel, s.ball.pos, s.ball.vel};
    hasLastTouch_ = true;
}

// ---- Events ----------------------------------------------------------------------------------------------------
void OverlayModule::resetTransient() {
    hasDir_ = false;
    snapNext_ = false;
    dir_ = {0.f, 0.f, -1.f};
    hasLastBall_ = false;
    hasLastTouch_ = false;
    lastTickFrame_ = -1;
    lastDrawSnapTime_ = -1.f;
    lastClock_ = -1.0;
    OverlayStatus fresh;
    fresh.mode = cfg_.speedMode;
    status_ = fresh;
}

void OverlayModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    // "Today" for the UI is known from the first event (menu included), before any physics tick: a restart in the
    // main menu must not show 0:00 for a day already played. Lookup only, no empty day is recorded.
    noteDate(ctx.game.localDate);
    switch (ev.kind) {
    case GameEventKind::ContextChanged: {
        const GameMode mode = ctx.game.mode;
        if (!sessionKnown_ || mode != sessionMode_ || ctx.game.mapName != sessionMap_) {
            endSession(out);
            resetTransient();
            sessionKnown_ = true;
            sessionMode_ = mode;
            sessionMap_ = ctx.game.mapName;
        }
        if (mode == GameMode::Menu || mode == GameMode::Online) {
            status_.speedAvailable = false;
            status_.ballValid = false;
            status_.zoneVisible = false;
            status_.sphereVisible = false;
        }
        break;
    }
    case GameEventKind::MapUnloaded:
        endSession(out);
        resetTransient();
        sessionKnown_ = false;
        break;
    case GameEventKind::ResetShot:
    case GameEventKind::CarSpawned: snapNext_ = true; break;
    default: break;
    }
}

// ---- Commands --------------------------------------------------------------------------------------------------
std::string OverlayModule::speedChatText(const TickContext& ctx) const {
    if (ctx.game.mode == GameMode::Replay || !ctx.hasSnapshot || !ctx.snap.car.valid || !ctx.snap.ball.valid)
        return "Écart de vitesse indisponible : il faut ta voiture et la balle en jeu (freeplay, entraînement, map).";
    const Snapshot& s = ctx.snap;
    const ovl::SpeedMode mode = ovl::speedModeFromIndex(cfg_.speedMode);
    const ovl::SpeedDiff d = ovl::computeSpeedDiff(mode, s.car.pos, s.car.vel, s.ball.pos, s.ball.vel);
    const ovl::SpeedZone zone = ovl::classifyZone(d.value, cfg_.goodThreshold, cfg_.alrightThreshold);
    const SpeedUnit unit = resolveUnit(unitFromIndex(cfg_.speedUnit), ctx.profile.metric);
    const KeyStyle style = keyStyleFromIndex(ctx.settings.profile.buttonStyle);
    return ovl::speedReport(mode, d, zone, unit, actionKeyLabel(ctx.profile, GameAction::Throttle, style));
}

void OverlayModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    if (cmd.name == "speed") {
        ChatMessage m;
        m.text = speedChatText(ctx);
        out.chat.push_back(std::move(m));
    } else if (cmd.name == "reset_timer") {
        endSession(out);
        ToastMessage t;
        t.title = "Chrono de session";
        t.text = "Remis à zéro.";
        t.kind = ToastKind::Info;
        out.toasts.push_back(std::move(t));
    } else if (cmd.name == "reset_stats") {
        totalTicks_ = 0;
        sessions_ = 0;
        longestTicks_ = 0;
        days_.clear();
        todayDate_.clear();
        todayIdx_ = -1;
        out.requestSave = true;
        ToastMessage t;
        t.title = "Temps d'entraînement";
        t.text = "Statistiques effacées.";
        out.toasts.push_back(std::move(t));
    } else if (cmd.name == "preset_original") {
        // Settings live in cvars, which only the UI/glue can write: the tab resets every "overlay" setting to its
        // schema default (the defaults ARE the original plugin's values) and posts this command. Nothing to emit.
    }
}

// ---- Drawing ---------------------------------------------------------------------------------------------------
bool OverlayModule::speedKinematics(const ViewContext& view, const DrawContext& ctx, Kinematics& k) const {
    if (ctx.game.mode == GameMode::Replay) return false;  // no local car in replays: no differential
    if (ctx.hasSnapshot && ctx.snap.car.valid && ctx.snap.ball.valid) {
        k = {ctx.snap.car.pos, ctx.snap.car.vel, ctx.snap.ball.pos, ctx.snap.ball.vel};
    } else if (view.carValid && view.ballValid) {
        k = {view.carVisualPos, view.carVel, view.ballVisualPos, view.ballVel};
    } else {
        return false;
    }
    return finite(k.carPos) && finite(k.carVel) && finite(k.ballPos) && finite(k.ballVel);
}

float OverlayModule::frameDt(const DrawContext& ctx) {
    // Live game: physics time of the newest snapshot (stops in pause, exact under slow motion).
    if (ctx.hasSnapshot && ctx.game.mode != GameMode::Replay) {
        lastClock_ = -1.0;
        float dt = 0.f;
        if (lastDrawSnapTime_ >= 0.f) dt = ctx.snap.time - lastDrawSnapTime_;
        lastDrawSnapTime_ = ctx.snap.time;
        return std::isfinite(dt) ? clampf(dt, 0.f, kMaxFrameDt) : 0.f;
    }
    // Replays (no physics ticks): wall clock.
    lastDrawSnapTime_ = -1.f;
    const double now = clock_ ? clock_() : 0.0;
    float dt = 0.f;
    if (lastClock_ >= 0.0) dt = static_cast<float>(now - lastClock_);
    lastClock_ = now;
    return std::isfinite(dt) ? clampf(dt, 0.f, kMaxFrameDt) : 0.f;
}

void OverlayModule::drawSpeed(const ViewContext& view, const ovl::SpeedDiff& diff, ovl::SpeedZone zone,
                              SpeedUnit unit, float scale, DrawList& out) const {
    const float x = clamp01(cfg_.speedX) * view.screenW;
    const float y = clamp01(cfg_.speedY) * view.screenH;
    const Color col = ovl::zoneColor(zone, cfg_);
    std::string main = ovl::speedText(diff.value, unit);
    const float mainWidth = kCharWidthPerScale * static_cast<float>(main.size()) * scale;
    out.text({x, y}, std::move(main), col, scale, cfg_.shadow);
    if (diff.hasZ) {
        // Smaller line to the right, like the original (225 px at size 3), pushed further for long values.
        const float off = std::max(kZOffsetPerScale * scale, mainWidth + 4.f * scale);
        out.text({x + off, y + 5.f * scale}, ovl::zText(diff.z, unit), col, scale * 0.5f, cfg_.shadow);
    }
    if (cfg_.lastTouchLine && hasLastTouch_) {
        const ovl::SpeedDiff t = ovl::computeSpeedDiff(ovl::speedModeFromIndex(cfg_.speedMode), lastTouch_.carPos,
                                                       lastTouch_.carVel, lastTouch_.ballPos, lastTouch_.ballVel);
        const ovl::SpeedZone tz = ovl::classifyZone(t.value, cfg_.goodThreshold, cfg_.alrightThreshold);
        out.text({x, y - 8.f * scale}, "Contact : " + ovl::speedText(t.value, unit), ovl::zoneColor(tz, cfg_),
                 scale * 0.5f, cfg_.shadow);
    }
}

void OverlayModule::drawTimer(const ViewContext& view, float scale, DrawList& out) const {
    const float x = clamp01(cfg_.timerX) * view.screenW;
    const float y = clamp01(cfg_.timerY) * view.screenH;
    out.text({x, y}, formatClock(sessionSeconds()), cfg_.timerColor, scale, cfg_.shadow);
}

void OverlayModule::drawBallOverlays(const ViewContext& view, const DrawContext& ctx, float dt, bool haveSpeed,
                                     ovl::SpeedZone zone, DrawList& out) {
    const bool ballOk = view.ballValid && finite(view.ballVisualPos);
    status_.ballValid = ballOk;
    if (!ballOk) {
        hasDir_ = false;
        hasLastBall_ = false;
        status_.ballInAir = false;
        status_.zoneVisible = false;
        status_.sphereVisible = false;
        return;
    }
    const Vec3 ball = view.ballVisualPos;
    const float R =
        (std::isfinite(view.ballRadius) && view.ballRadius > 10.f) ? view.ballRadius : ovl::kDefaultBallRadius;

    // Direction of the zone, smoothed; snapped on the first frame, after a reset/teleport or a respawn.
    bool snap = !hasDir_ || snapNext_;
    if (hasLastBall_ && distance(ball, lastBallPos_) > kTeleportDistance) snap = true;
    lastBallPos_ = ball;
    hasLastBall_ = true;
    snapNext_ = false;
    const Vec3 ballVel = finite(view.ballVel) ? view.ballVel : Vec3{};
    const float speedXY = length2D(ballVel);
    const ovl::ZoneBehavior eff = ovl::resolveBehavior(ovl::behaviorFromIndex(cfg_.zoneBehavior), speedXY,
                                                       ctx.game.category, cfg_.zoneMinSpeed);
    const Vec3 target = ovl::zoneTargetDirection(eff, ballVel, cfg_.zoneMinSpeed, cfg_.zoneMaxSpeed,
                                                 cfg_.zoneMaxTiltDeg);
    dir_ = snap ? target : ovl::smoothDirection(dir_, target, dt, cfg_.zoneSmoothing);
    hasDir_ = true;

    // Conditions (each optional, like the original's fixed rules).
    const bool inAir = ovl::ballInAir(ball.z, cfg_.airHeight);
    bool carKnown = false, carBelow = true;
    if (view.carValid && finite(view.carVisualPos)) {
        carKnown = true;
        carBelow = view.carVisualPos.z < ball.z;
    } else if (ctx.game.mode != GameMode::Replay && ctx.hasSnapshot && ctx.snap.car.valid) {
        carKnown = true;
        carBelow = ctx.snap.car.pos.z < ball.z;
    }
    const bool airOk = !cfg_.onlyBallInAir || inAir;
    const bool zoneVisible = cfg_.zoneEnabled && airOk && (!cfg_.onlyCarBelowBall || carBelow);
    const bool sphereVisible = cfg_.sphereEnabled && airOk;

    status_.ballInAir = inAir;
    status_.carKnown = carKnown;
    status_.carBelowBall = carKnown && carBelow;
    status_.effectiveBehavior = static_cast<int>(eff);
    status_.tiltDeg = angleBetweenDeg(dir_, {0.f, 0.f, -1.f});
    status_.zoneVisible = zoneVisible;
    status_.sphereVisible = sphereVisible;

    const Color speedColor = ovl::zoneColor(zone, cfg_);
    if (zoneVisible) {
        const ovl::ZoneCircle c = ovl::zoneCircle(ball, R, dir_, cfg_.zoneRadius);
        const Color col = (cfg_.zoneMatchColor && haveSpeed) ? speedColor : cfg_.zoneColor;
        const float width = static_cast<float>(std::clamp(cfg_.zoneThickness, 1, 5));
        const int segments = std::clamp(cfg_.zoneSegments, 12, 64);
        Occluder occ;
        if (cfg_.zoneOcclusion) {
            occ.enabled = true;
            occ.center = ball;
            occ.radius = R - kOccluderMargin;
            const float hiddenAlpha = clamp01(cfg_.zoneHiddenAlpha);
            if (hiddenAlpha > 0.f)  // faint full circle under the visible part
                out.circle3D(c.center, c.normal, c.radius, col.withAlpha(col.a * hiddenAlpha), width, segments);
        }
        out.circle3D(c.center, c.normal, c.radius, col, width, segments, occ);
    }
    if (sphereVisible) {
        const Color col = (cfg_.sphereMatchColor && haveSpeed) ? speedColor : cfg_.sphereColor;
        out.sphere3D(ball, clampf(cfg_.sphereRadius, 1.f, 10.f), col, 1.f, kSphereSegments);
    }
}

void OverlayModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    const float dt = frameDt(ctx);
    const bool replay = ctx.game.mode == GameMode::Replay;
    const SpeedUnit unit = resolveUnit(unitFromIndex(cfg_.speedUnit), ctx.profile.metric);
    const ovl::SpeedMode mode = ovl::speedModeFromIndex(cfg_.speedMode);

    Kinematics k;
    const bool haveSpeed = speedKinematics(view, ctx, k);
    ovl::SpeedDiff diff;
    ovl::SpeedZone zone = ovl::SpeedZone::Good;
    if (haveSpeed) {
        diff = ovl::computeSpeedDiff(mode, k.carPos, k.carVel, k.ballPos, k.ballVel);
        zone = ovl::classifyZone(diff.value, cfg_.goodThreshold, cfg_.alrightThreshold);
    }

    status_.replay = replay;
    status_.mode = static_cast<int>(mode);
    status_.unit = unit;
    status_.speedAvailable = haveSpeed;
    status_.diff = diff.value;
    status_.zDiff = diff.z;
    status_.hasZ = diff.hasZ;
    status_.zone = static_cast<int>(zone);

    const float hud = (std::isfinite(ctx.hudScale) && ctx.hudScale > 0.f) ? ctx.hudScale : 1.f;
    const float scale = clampf(cfg_.textScale, 1.f, 10.f) * hud;
    if (cfg_.speedEnabled && haveSpeed) drawSpeed(view, diff, zone, unit, scale, out);
    if (cfg_.timerEnabled && !replay) drawTimer(view, scale, out);
    drawBallOverlays(view, ctx, dt, haveSpeed, zone, out);
}

// ---- Status / persistence --------------------------------------------------------------------------------------
OverlayStatus OverlayModule::status() const {
    OverlayStatus s = status_;
    s.sessionSeconds = ticksToSeconds(sessionTicks_);
    s.totalSeconds = ticksToSeconds(totalTicks_);
    s.sessions = sessions_;
    s.longestSession = ticksToSeconds(std::max(longestTicks_, sessionTicks_));
    const int today = findDay(knownDate_.empty() ? todayDate_ : knownDate_);
    s.todaySeconds = today >= 0 ? ticksToSeconds(days_[static_cast<size_t>(today)].ticks) : 0.f;
    // Last touch, re-evaluated with the current mode and thresholds (valid right after the touch, before any draw).
    s.lastTouchAvailable = hasLastTouch_;
    s.lastTouchDiff = 0.f;
    s.lastTouchZone = 0;
    if (hasLastTouch_) {
        const ovl::SpeedDiff t = ovl::computeSpeedDiff(ovl::speedModeFromIndex(cfg_.speedMode), lastTouch_.carPos,
                                                       lastTouch_.carVel, lastTouch_.ballPos, lastTouch_.ballVel);
        s.lastTouchDiff = t.value;
        s.lastTouchZone = static_cast<int>(ovl::classifyZone(t.value, cfg_.goodThreshold, cfg_.alrightThreshold));
    }
    return s;
}

std::vector<OverlayDay> OverlayModule::recentDays(size_t maxCount) const {
    const size_t n = std::min(maxCount, days_.size());
    std::vector<OverlayDay> out;
    out.reserve(n);
    for (size_t i = days_.size() - n; i < days_.size(); ++i)
        out.push_back(OverlayDay{days_[i].date, ticksToSeconds(days_[i].ticks)});
    return out;
}

void OverlayModule::save(Store& store) const {
    // Seconds written with tick precision (not Store::setFloat's 6 significant digits, which drops seconds once the
    // total passes 278 h).
    store.setString("overlay.total_seconds", secondsText(totalTicks_));
    store.setInt("overlay.sessions", sessions_);
    store.setString("overlay.longest_session", secondsText(longestTicks_));
    std::vector<std::string> list;
    list.reserve(days_.size());
    for (const auto& d : days_) list.push_back(d.date + ";" + secondsText(d.ticks));
    store.setList("overlay.days", list);
}

void OverlayModule::load(const Store& store) {
    loadTicks(store.getString("overlay.total_seconds", "0"), kMaxTotalSeconds, totalTicks_);
    const long long sessions = store.getInt("overlay.sessions", 0);
    sessions_ = static_cast<int>(std::clamp<long long>(sessions, 0, 10000000));
    loadTicks(store.getString("overlay.longest_session", "0"), kMaxSessionSeconds, longestTicks_);

    days_.clear();
    for (const auto& entry : store.getList("overlay.days")) {
        const size_t sep = entry.find(';');
        if (sep == std::string::npos) continue;
        const std::string date = entry.substr(0, sep);
        if (!validDate(date)) continue;
        int64_t ticks = 0;
        if (!loadTicks(entry.substr(sep + 1), kMaxDaySeconds, ticks)) continue;
        auto it = std::lower_bound(days_.begin(), days_.end(), date,
                                   [](const DayTicks& d, const std::string& key) { return d.date < key; });
        if (it != days_.end() && it->date == date) it->ticks = ticks;  // duplicate: last wins
        else days_.insert(it, DayTicks{date, ticks});
    }
    if (days_.size() > static_cast<size_t>(kMaxDays))
        days_.erase(days_.begin(), days_.end() - kMaxDays);
    todayDate_.clear();
    todayIdx_ = -1;
}

} // namespace rlt
