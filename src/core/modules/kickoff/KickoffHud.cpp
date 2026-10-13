// Drawing of the kickoff module: HUD panel (top-left drill zone), big centred countdown, ghost box, path line.
#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/kickoff/KickoffModule.h"

namespace rlt {

namespace {

constexpr Color kGrey{170, 170, 170, 255};
constexpr Color kTip{255, 220, 120, 255};
constexpr Color kAhead{90, 230, 120, 255};
constexpr Color kBehind{255, 110, 90, 255};

std::string secs(float s) { return formatNumber(s, 2) + " s"; }

std::string signedSecs(float s) {
    const std::string n = formatNumber(s, 2);
    return (s > 0.f && n != "0.00" ? "+" : "") + n;
}

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return {90, 230, 120, 255};
    case Verdict::Good: return {170, 230, 90, 255};
    case Verdict::Ok: return {255, 210, 80, 255};
    default: return {255, 110, 90, 255};
    }
}

Color splitColor(float delta) {
    if (delta < -0.005f) return kAhead;
    if (delta > 0.005f) return kBehind;
    return colors::White;
}

// Greedy word wrap on byte length (accents count double, good enough for HUD lines).
std::vector<std::string> wrap(const std::string& text, size_t maxChars) {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        if (!cur.empty() && cur.size() + 1 + word.size() > maxChars) {
            lines.push_back(cur);
            cur.clear();
        }
        if (!cur.empty()) cur += ' ';
        cur += word;
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void addLine(DrawPanel& p, const std::string& text, Color c, bool accents, float scale = 1.f, float bar = -1.f,
             Color barColor = colors::Green) {
    PanelLine l;
    l.text = hudText(text, accents);
    l.color = c;
    l.scale = scale;
    l.bar = bar;
    l.barColor = barColor;
    p.lines.push_back(std::move(l));
}

void appendResult(DrawPanel& p, const KickoffResultView& r, const DrawContext& ctx, bool accents, int maxTips) {
    const KickoffMetrics& m = r.metrics;
    std::string time;
    if (m.touched) time = "Temps : " + secs(m.timeToTouch);
    else if (m.estimated) time = "Temps estimé : ~" + secs(m.timeToTouch);
    else time = "Balle pas touchée";
    if (r.newPb && m.touched) {
        addLine(p, time + "  RECORD !", kAhead, accents);
    } else {
        if (r.previousBest > 0.f && m.touched) time += "  (record " + secs(r.previousBest) + ")";
        addLine(p, time, colors::White, accents);
    }
    const Verdict v = verdictFromScore(static_cast<float>(r.grade.total));
    addLine(p, "Note : " + std::to_string(r.grade.total) + "/100  " + verdictLabel(v), verdictColor(v), accents, 1.f,
            static_cast<float>(r.grade.total) / 100.f, verdictColor(v));
    std::string kind = kickoffTypeName(m.type);
    if (r.hasDuel) kind += std::string("  -  ") + kickoffDuelName(r.duel.outcome);
    addLine(p, kind, r.hasDuel && kickoffDuelWon(r.duel.outcome) ? kAhead : colors::White, accents);
    if (m.touched) {
        const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
        addLine(p,
                "Vitesse : " + formatSpeed(m.speedToBall, unit, 0) +
                    "   Décalage : " + formatNumber(std::fabs(m.lateralOffset), 0) + " uu",
                kGrey, accents, 0.9f);
    }
    int shown = 0;
    for (const auto& t : r.grade.tips) {
        if (shown >= maxTips) break;
        for (const auto& l : wrap(kickoffTipText(t.tip), 58)) addLine(p, l, kTip, accents, 0.9f);
        ++shown;
    }
    if (m.boostLooksUnlimited)
        addLine(p, "Boost illimité détecté : coupe-le pour un kickoff réaliste.", colors::Orange, accents, 0.85f);
}

} // namespace

void KickoffModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled) return;
    if (running_) {
        if (view.valid) drawWorld(view, ctx, out);
        if (cfg_.hudEnabled) drawDrillHud(view, ctx, out);
        return;
    }
    // Passive analysis: show the result card for a few seconds.
    if (cfg_.hudEnabled && last_.valid && last_.passive && ctx.hasSnapshot) {
        const float age = ctx.snap.time - last_.gameTime;
        if (age >= 0.f && age < 6.f) drawResultCard(view, ctx, out, true);
    }
}

void KickoffModule::drawWorld(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    if (phase_ == KickoffPhase::Idle || phase_ == KickoffPhase::Placing) return;
    Occluder occ;
    if (view.ballValid) {
        occ.enabled = true;
        occ.center = view.ballVisualPos;
        occ.radius = view.ballRadius;
    }
    const int64_t frame = ctx.hasSnapshot ? ctx.snap.frame : lastTickFrame_;
    if (cfg_.showPath) {
        const KickoffPath p = kickoffPath(spawn_, team_);
        const Vec3 lift{0.f, 0.f, 5.f};
        const Vec3 a = flat(p.start) + lift;
        const Vec3 b = flat(p.pointAt(p.length)) + lift;
        out.line3D(a, b, cfg_.pathColor, 2.f, occ);
        for (int k = 1; k <= 3; ++k) {
            const Vec3 c = flat(p.pointAt(p.length * 0.25f * static_cast<float>(k))) + lift;
            out.line3D(c - p.right * 70.f, c + p.right * 70.f, cfg_.pathColor, 2.f, occ);
        }
        out.circle3D(b, {0.f, 0.f, 1.f}, 45.f, cfg_.pathColor, 2.f, 16, occ);
    }
    if (ghostActive_) {
        const float t =
            phase_ == KickoffPhase::Countdown ? 0.f : std::max(0.f, static_cast<float>(frame - goFrame_) * kTickDt);
        const KickoffGhostState g = ghost_.stateAt(t);
        const Rot rot{0, static_cast<int>(std::lround(g.yawRad * kRadToUnr)), 0};
        const Basis basis = basisFromRot(rot);
        Vec3 ext = ctx.profile.hitboxExtent;
        if (ext.x < 10.f || ext.y < 10.f || ext.z < 5.f) ext = {118.01f, 84.20f, 36.16f};
        const Vec3 center = g.pos + basis.toWorld(ctx.profile.hitboxOffset);
        const size_t first = out.lines3D.size();
        out.box3D(center, basis, ext * 0.5f, cfg_.ghostColor, 2.f);
        for (size_t i = first; i < out.lines3D.size(); ++i) out.lines3D[i].occluder = occ;
        if (cfg_.showPath) {
            const KickoffPath& gp = ghost_.path();
            const Vec3 lift{0.f, 0.f, 5.f};
            out.line3D(flat(gp.start) + lift, flat(gp.pointAt(gp.length)) + lift, cfg_.ghostColor.withAlpha(90.f), 1.f,
                       occ);
        }
        out.text3D(
            center + Vec3{0.f, 0.f, 70.f},
            hudText(std::string("Fantôme ") + kickoffGhostModeName(repGhostMode_), ctx.settings.general.hudAccents),
            cfg_.ghostColor, 1.1f * ctx.hudScale);
    }
}

void KickoffModule::drawDrillHud(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const bool acc = ctx.settings.general.hudAccents;
    const float s = ctx.hudScale;
    const int64_t frame = ctx.hasSnapshot ? std::max<int64_t>(ctx.snap.frame, lastTickFrame_) : lastTickFrame_;
    const KickoffSpawnInfo& info = kickoffSpawnInfo(spawn_);

    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = s;
    p.minWidth = 270.f * s;
    p.title = hudText(std::string("Kickoff : ") + info.name + (team_ == KickoffTeam::Orange ? " (orange)" : ""), acc);

    std::string mode = std::string("Mode : ") + kickoffModeName(mode_);
    if (mode_ == KickoffMode::Sequence) {
        const int idx = std::min(kKoSpawnCount, sequenceDone_ + (phase_ == KickoffPhase::Done ? 0 : 1));
        mode += "  " + std::to_string(idx) + "/" + std::to_string(kKoSpawnCount);
    }
    addLine(p, mode, kGrey, acc, 0.9f);
    if (ghostActive_) {
        std::string gl =
            std::string("Fantôme : ") + kickoffGhostModeName(repGhostMode_) + " (" + secs(ghost_.arrivalTime()) + ")";
        if (ghostFallback_) gl += ", pas encore de record";
        addLine(p, gl, cfg_.ghostColor.withAlpha(255.f), acc, 0.9f);
    }

    const int countdownSecs = std::max(1, cfg_.countdownSeconds);
    if (phase_ == KickoffPhase::Placing) {
        addLine(p, "Placement de la voiture...", colors::White, acc, 1.1f);
    } else if (phase_ == KickoffPhase::Countdown) {
        const int64_t left = nominalGo_ - frame;
        const int n = std::clamp(static_cast<int>((left + 119) / 120), 1, countdownSecs);
        addLine(p, "Décompte : " + std::to_string(n), colors::White, acc, 1.2f);
        addLine(p,
                "Pré-appuie " + actionPrompt(ctx.profile, GameAction::Boost) + " + " +
                    actionPrompt(ctx.profile, GameAction::Throttle),
                kTip, acc, 0.9f);
        if (cfg_.bigCountdown)
            out.text({view.screenW * 0.5f, view.screenH * cfg_.countdownY}, std::to_string(n), colors::White, 6.f * s,
                     true, Align::Center);
    } else if (phase_ == KickoffPhase::Live) {
        addLine(p, "Chrono : " + secs(liveTimeAt(frame)), colors::White, acc, 1.2f);
        if (cfg_.bigCountdown && frame - goFrame_ < 60)
            out.text({view.screenW * 0.5f, view.screenH * cfg_.countdownY}, "GO !", kAhead, 6.f * s, true,
                     Align::Center);
    }
    if (cfg_.showSplits && (phase_ == KickoffPhase::Live || phase_ == KickoffPhase::Done)) {
        const char* ref = ghostActive_ ? "fantôme" : kickoffLevelName(kickoffLevelFromIndex(cfg_.gradeTarget));
        for (const auto& sp : liveSplits_) {
            if (!sp.reached) continue;
            const int pct = static_cast<int>(std::lround(sp.fraction * 100.f));
            addLine(
                p,
                std::to_string(pct) + " % : " + secs(sp.playerTime) + "  (" + signedSecs(sp.delta) + " vs " + ref + ")",
                splitColor(sp.delta), acc, 0.9f);
        }
    }
    if (phase_ == KickoffPhase::Done && last_.valid && !last_.passive) {
        appendResult(p, last_, ctx, acc, 2);
        if (waitingForNext_) {
            addLine(p, "Appuie sur " + actionPrompt(ctx.profile, GameAction::ResetShot) + " pour relancer.", kGrey, acc,
                    0.9f);
        } else {
            const float wait = cfg_.resetDelay - static_cast<float>(frame - doneFrame_) * kTickDt;
            if (wait > 0.f) addLine(p, "Essai suivant dans " + formatNumber(wait, 1) + " s", kGrey, acc, 0.9f);
        }
    } else if (last_.valid && !last_.passive) {
        std::string prev = "Dernier : ";
        prev += last_.metrics.touched ? secs(last_.metrics.timeToTouch) : std::string("pas de touche");
        prev += ", " + std::to_string(last_.grade.total) + "/100";
        addLine(p, prev, kGrey, acc, 0.85f);
    }
    out.panel(std::move(p));
}

void KickoffModule::drawResultCard(const ViewContext& view, const DrawContext& ctx, DrawList& out,
                                   bool compactTitle) const {
    const bool acc = ctx.settings.general.hudAccents;
    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = ctx.hudScale;
    p.minWidth = 270.f * ctx.hudScale;
    const KickoffSpawnInfo& info = kickoffSpawnInfo(last_.metrics.spawn);
    p.title = hudText(std::string(compactTitle ? "Kickoff analysé : " : "Kickoff : ") + info.name, acc);
    appendResult(p, last_, ctx, acc, 1);
    out.panel(std::move(p));
}

} // namespace rlt
