// Drawing of the first-touch module: session panel (zone "firsttouch", right bottom), contact heatmap,
// drill panel (zone "exercice en cours", top left) and the 3D marker of the last contact point.
// Compact HUD: each panel keeps at most 3 main lines (result, one tip, current action or time left); the rest is
// flagged `detail` and only shows with the compact HUD off, like the heatmap.

#include <cmath>

#include "core/common/Text.h"
#include "core/modules/firsttouch/FirstTouchModule.h"

namespace rlt {

namespace {
constexpr Color kTextColor{235, 235, 235, 255};
constexpr Color kDimColor{205, 205, 205, 255};
constexpr Color kAccent{0, 200, 255, 255};

PanelLine makeLine(std::string text, Color c, float scale = 1.f, float bar = -1.f, Color barColor = colors::Green) {
    PanelLine l;
    l.text = std::move(text);
    l.color = c;
    l.scale = scale;
    l.bar = bar;
    l.barColor = barColor;
    return l;
}

PanelLine detailLine(std::string text, Color c, float scale = 0.9f, float bar = -1.f, Color barColor = colors::Green) {
    PanelLine l = makeLine(std::move(text), c, scale, bar, barColor);
    l.detail = true;
    return l;
}

std::string pct(float ratio) { return std::to_string(static_cast<int>(std::lround(clamp01(ratio) * 100.f))) + " %"; }

// Whole seconds left, rounded up ("3 s" until the last second is over).
std::string secondsText(float s) { return std::to_string(static_cast<int>(std::ceil(std::max(0.f, s) - 1e-3f))) + " s"; }
} // namespace

Color FirstTouchModule::scoreColor(float score) const {
    if (score >= 70.f) return cfg_.goodColor;
    if (score >= 45.f) return cfg_.okColor;
    return cfg_.badColor;
}

Color FirstTouchModule::zoneColor(ft::SpeedZone z) const {
    switch (z) {
    case ft::SpeedZone::Good: return cfg_.goodColor;
    case ft::SpeedZone::Ok: return cfg_.okColor;
    case ft::SpeedZone::Bad: return cfg_.badColor;
    }
    return kTextColor;
}

void FirstTouchModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled) return;
    // During a drill the compact HUD keeps the drill panel only: it already carries the verdict and the tip.
    if (cfg_.hudEnabled && !(ctx.compactHud && drill_.active)) drawStatsPanel(view, ctx, out);
    if (drill_.active) drawDrillPanel(view, ctx, out);
    if (cfg_.showContactMarker) drawContactMarker(view, ctx, out);
}

void FirstTouchModule::drawStatsPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const DribbleView dv = dribble();
    if (!last_.valid && session_.graded == 0 && session_.dribbleContacts == 0) return;
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(cfg_.unit), ctx.profile.metric);
    const float scale = std::max(0.3f, ctx.hudScale * cfg_.hudSize);
    const bool right = cfg_.hudX > 0.5f;
    const bool dribbleMap = ctx.game.category == MapCategory::Dribble || ctx.game.category == MapCategory::AirDribble;

    DrawPanel p;
    p.pos = Vec2{cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.anchorX = right ? Align::Right : Align::Left;
    p.title = "Première touche";
    p.scale = scale;
    p.minWidth = 190.f * scale;

    // Main lines: the dribble count on dribble maps, the last touch (score and verdict), its one tip in words.
    auto dribbleLines = [&](bool main) {
        if (dv.contacts >= 1) {
            std::string t = "Dribble : " + std::to_string(dv.contacts) + (dv.contacts > 1 ? " touches" : " touche");
            t += " (" + formatNumber(dv.seconds, 1) + " s)";
            p.lines.push_back(main ? makeLine(t, dv.active ? kAccent : kTextColor) : detailLine(t, kDimColor));
            if (dv.cadence > 0.f)
                p.lines.push_back(detailLine("Rythme : " + formatNumber(dv.cadence, 1) + " touches /s", kDimColor));
        }
        if (session_.bestChainContacts > 0)
            p.lines.push_back(detailLine("Record dribble : " + std::to_string(session_.bestChainContacts) + " touches, " +
                                             formatNumber(session_.bestChainSeconds, 1) + " s",
                                         kDimColor));
    };
    if (dribbleMap) dribbleLines(true);

    if (last_.valid) {
        const std::string head = std::string(ft::touchTypeTitle(last_.type)) + " : " +
                                 std::to_string(static_cast<int>(last_.score)) + "/100  " + last_.headline;
        p.lines.push_back(makeLine(head, scoreColor(last_.score)));
        if (cfg_.showTips && !last_.tips.empty() && last_.score < 85.f)
            p.lines.push_back(makeLine(last_.tips.front().text, cfg_.okColor));
        p.lines.push_back(detailLine("Écart " + ft::formatSpeedValue(last_.speedDiff, unit) + "  |  contact " +
                                         ft::contactHeightLabel(last_.height) + ", " + ft::carPartLabel(last_.part),
                                     zoneColor(last_.zone)));
        p.lines.push_back(detailLine("Contrôle " + pct(last_.controlFraction), kTextColor, 0.9f, last_.controlFraction,
                                     last_.controlled ? cfg_.goodColor : cfg_.badColor));
    }
    if (session_.graded > 0) {
        p.lines.push_back(detailLine("Session : " + std::to_string(session_.graded) + " touches, moyenne " +
                                         formatNumber(session_.averageScore(), 0),
                                     kTextColor));
        p.lines.push_back(detailLine("Zone verte " + pct(session_.goodZoneRate()) + ", réussite " + pct(session_.successRate()),
                                     kDimColor));
        p.lines.push_back(detailLine("Série " + std::to_string(session_.streak) + " (record " +
                                         std::to_string(session_.bestStreak) + ")",
                                     kDimColor));
        if (cfg_.showTrend) {
            const std::string t = ft::trendText(session_, unit, cfg_.goodZone * 0.66f);
            if (!t.empty()) p.lines.push_back(detailLine(t, kAccent));
        }
        const std::string code = persistentTipCode();
        if (!code.empty() && ft::tipShortLabel(code)[0] != '\0')
            p.lines.push_back(detailLine(std::string("À travailler : ") + ft::tipShortLabel(code), cfg_.okColor));
    }
    if (!dribbleMap && (dv.active || session_.bestChainContacts > 0)) dribbleLines(false);
    out.panel(std::move(p));
    if (cfg_.showHeatmap && !heat_.empty() && !ctx.compactHud)
        drawHeatmap(cfg_.hudX * view.screenW, cfg_.hudY * view.screenH, right, scale, out);
}

void FirstTouchModule::drawHeatmap(float anchorX, float topY, bool anchorRight, float scale, DrawList& out) const {
    const float size = 104.f * scale;
    const float x0 = anchorRight ? anchorX - size : anchorX;
    const float y0 = std::max(2.f, topY - 4.f * scale - size);
    out.rect(Vec2{x0, y0}, Vec2{size, size}, Color{0, 0, 0, 150}, true);
    out.text(Vec2{x0 + 4.f * scale, y0 + 3.f * scale}, "Points de contact", kDimColor, 0.8f * scale);

    const Vec2 c{x0 + size * 0.5f, y0 + size * 0.57f};
    const float R = size * 0.32f;
    const Color ring{255, 255, 255, 200};
    constexpr int kSeg = 24;
    Vec2 prev{c.x + R, c.y};
    for (int i = 1; i <= kSeg; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(kSeg);
        const Vec2 q{c.x + R * std::cos(a), c.y - R * std::sin(a)};
        out.line(prev, q, ring, 1.5f * scale);
        prev = q;
    }
    out.line(Vec2{c.x - R, c.y}, Vec2{c.x + R, c.y}, Color{255, 255, 255, 60}, 1.f);
    out.line(Vec2{c.x, c.y - R}, Vec2{c.x, c.y + R}, Color{255, 255, 255, 40}, 1.f);
    // The car comes from the left (x = approach direction): arrow at car height.
    const float ay = c.y + R * 0.45f;
    const float ax1 = c.x - R - 5.f * scale;
    out.line(Vec2{ax1 - 12.f * scale, ay}, Vec2{ax1, ay}, kAccent, 2.f * scale);
    out.triangle(Vec2{ax1 + 3.f * scale, ay}, Vec2{ax1 - 2.f * scale, ay - 3.5f * scale}, Vec2{ax1 - 2.f * scale, ay + 3.5f * scale},
                 kAccent);

    const size_t n = heat_.size();
    for (size_t i = 0; i < n; ++i) {
        const ft::HeatPoint& hp = heat_.at(i);
        const bool newest = i + 1 == n;
        const float d = (newest ? 3.5f : 2.2f) * scale;
        const float px = c.x + clampf(hp.x, -1.f, 1.f) * R;
        const float py = c.y - clampf(hp.z, -1.f, 1.f) * R;
        Color col = scoreColor(hp.score);
        col.a = 110.f + 145.f * static_cast<float>(i + 1) / static_cast<float>(n);
        out.rect(Vec2{px - d, py - d}, Vec2{2.f * d, 2.f * d}, col, true);
        if (newest) out.rect(Vec2{px - d - 1.5f * scale, py - d - 1.5f * scale}, Vec2{2.f * d + 3.f * scale, 2.f * d + 3.f * scale},
                             colors::White, false);
    }
}

void FirstTouchModule::drawDrillPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const float scale = std::max(0.3f, ctx.hudScale * cfg_.hudSize);
    DrawPanel p;
    p.pos = Vec2{cfg_.drillHudX * view.screenW, cfg_.drillHudY * view.screenH};
    p.anchorX = cfg_.drillHudX > 0.5f ? Align::Right : Align::Left;
    p.scale = scale;
    p.minWidth = 220.f * scale;

    const auto i = static_cast<size_t>(drill_.current);
    const float level = drill_.phase == DrillPhase::Place ? stairs_[i].level() : drill_.repLevel;
    // Title: the drill and its level (1..10); a Mix names the drill of the current ball.
    p.title = drill_.type == ft::DrillType::Mix ? std::string("Mix : ") + ft::drillName(drill_.current)
                                                : std::string(ft::drillName(drill_.type));
    p.title += ", niveau " + std::to_string(ft::playerLevel(level)) + "/10";

    // At most 3 main lines: what to do now, the time left (or the verdict of the last ball), the one tip.
    switch (drill_.phase) {
    case DrillPhase::Place: p.lines.push_back(makeLine("Placement de la balle...", kTextColor)); break;
    case DrillPhase::WaitTouch: {
        const float left = std::max(0.f, drill_.deadline - drill_.now);
        const float total = drill_.repCriteria.timeout;
        p.lines.push_back(makeLine(
            ft::drillInstruction(drill_.current, ctx.profile, keyStyleFromIndex(ctx.settings.profile.buttonStyle)),
            colors::White));
        std::string timer = "Temps : " + secondsText(left);
        if (drill_.reps > 0) timer += "   Réussis : " + std::to_string(drill_.successes) + "/" + std::to_string(drill_.reps);
        p.lines.push_back(makeLine(timer, kTextColor, 1.f, total > 0.f ? left / total : 0.f,
                                   left < 2.f ? cfg_.badColor : cfg_.okColor));
        // After a miss the tip stays as a reminder; otherwise the goal of the level.
        if (!drill_.lastSuccess && !drill_.lastTip.empty()) p.lines.push_back(makeLine(drill_.lastTip, cfg_.okColor));
        else p.lines.push_back(makeLine(ft::drillGoal(level), kAccent));
        break;
    }
    case DrillPhase::Evaluate: {
        const float frac = pending_.window.windowTicks() > 0
                               ? static_cast<float>(pending_.window.elapsedTicks()) / static_cast<float>(pending_.window.windowTicks())
                               : 0.f;
        p.lines.push_back(makeLine("Garde la balle près de toi !", cfg_.goodColor, 1.f, frac, cfg_.goodColor));
        break;
    }
    case DrillPhase::Pause: {
        if (!drill_.lastOutcome.empty())
            p.lines.push_back(makeLine(drill_.lastOutcome, drill_.lastSuccess ? cfg_.goodColor : cfg_.badColor));
        if (!drill_.lastSuccess && !drill_.lastTip.empty()) p.lines.push_back(makeLine(drill_.lastTip, cfg_.okColor));
        p.lines.push_back(makeLine("Balle suivante dans " + secondsText(drill_.deadline - drill_.now), kTextColor));
        break;
    }
    case DrillPhase::Idle: break;
    }
    if (drill_.reps > 0 && drill_.phase != DrillPhase::WaitTouch)
        p.lines.push_back(detailLine("Réussis : " + std::to_string(drill_.successes) + "/" + std::to_string(drill_.reps),
                                     kDimColor));
    p.lines.push_back(detailLine("Arrêter : rlt firsttouch stop", kDimColor, 0.8f));
    out.panel(std::move(p));
}

void FirstTouchModule::drawContactMarker(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    if (!markerValid_ || !ctx.hasSnapshot || !view.valid || !view.ballValid) return;
    if (ctx.game.mode == GameMode::Replay) return;
    const float age = ctx.snap.time - markerTime_;
    if (age < 0.f || age > cfg_.markerSeconds) return;
    const float r = view.ballRadius > 10.f ? view.ballRadius : 91.25f;
    const Vec3 n = normalize(markerDir_, Vec3{0.f, 0.f, -1.f});
    const Vec3 center = view.ballVisualPos + n * (r + 2.f);
    Color col = markerScore_ < 0.f ? colors::White : scoreColor(markerScore_);
    col.a = 255.f * (1.f - 0.6f * age / std::max(0.1f, cfg_.markerSeconds));
    Occluder occ;
    occ.enabled = true;
    occ.center = view.ballVisualPos;
    occ.radius = r - 1.f;
    out.circle3D(center, n, 16.f, col, 2.f, 12, occ);
    out.line3D(center, center + n * 40.f, col, 2.f, occ);
}

} // namespace rlt
