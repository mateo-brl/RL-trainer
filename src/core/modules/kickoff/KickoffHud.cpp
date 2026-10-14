// Drawing of the kickoff module: HUD panel (top-left drill zone), ghost box, and the options off by default: the
// plugin's big centred countdown and the lines to the ball.
// Compact HUD: the panel keeps at most 3 main lines (result or chrono, the one tip in words, what comes next); the
// mode, the ghost, the split times, the measures and the other tips are `detail` lines.
// One countdown on screen: the game's own (default), else the big number, else the panel line.
#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/kickoff/KickoffModule.h"

namespace rlt {

namespace {

constexpr Color kDetail{205, 205, 205, 255};
constexpr Color kTip{255, 220, 120, 255};
constexpr Color kAhead{90, 230, 120, 255};
constexpr Color kBehind{255, 110, 90, 255};

std::string secs(float s) { return formatNumber(s, 2) + " s"; }

std::string signedSecs(float s) {
    const std::string n = formatNumber(s, 2);
    return (s > 0.f && n != "0.00" ? "+" : "") + n;
}

// Whole seconds left, rounded up ("2 s" until the last second is over).
std::string wholeSecs(float s) { return std::to_string(static_cast<int>(std::ceil(std::max(0.f, s) - 1e-3f))) + " s"; }

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

// The 50/50 in two words for the main line.
const char* duelShort(KickoffDuel d) {
    switch (d) {
    case KickoffDuel::PlayerClear:
    case KickoffDuel::PlayerNarrow: return "kickoff gagné";
    case KickoffDuel::Neutral: return "50/50 neutre";
    default: return "kickoff perdu";
    }
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

void addDetail(DrawPanel& p, const std::string& text, Color c, bool accents, float scale = 0.9f) {
    addLine(p, text, c, accents, scale);
    p.lines.back().detail = true;
}

// Tip of the last drill attempt, kept as a reminder until the next result (none after a great kickoff).
const KickoffTipLoss* reminderTip(const KickoffResultView& r) {
    if (!r.valid || r.passive || r.grade.tips.empty()) return nullptr;
    const KickoffTipLoss& t = r.grade.tips.front();
    return t.tip == KickoffTip::GreatKickoff ? nullptr : &t;
}

// Main lines of a result: verdict, score, time and 50/50, then the one tip (or the unlimited boost warning).
void appendResult(DrawPanel& p, const KickoffResultView& r, bool accents) {
    const KickoffMetrics& m = r.metrics;
    const Verdict v = verdictFromScore(static_cast<float>(r.grade.total));
    std::string head = std::string(verdictLabel(v)) + " " + std::to_string(r.grade.total) + "/100 : ";
    // "RECORD" only when an earlier best was beaten (the first kickoff of a spawn is always a record).
    if (m.touched) head += secs(m.timeToTouch) + (r.newPb && r.previousBest > 0.f ? " RECORD" : "");
    else if (m.estimated) head += "~" + secs(m.timeToTouch);
    else head += "balle pas touchée";
    if (r.hasDuel) head += std::string(", ") + duelShort(r.duel.outcome);
    addLine(p, head, verdictColor(v), accents, 1.f, static_cast<float>(r.grade.total) / 100.f, verdictColor(v));

    // Unlimited boost makes the run unrealistic: that warning replaces the tip. The drill limits the boost at every
    // placement, so this mostly concerns passive kickoffs.
    if (m.boostLooksUnlimited) addLine(p, "Boost illimité détecté : record non compté.", colors::Orange, accents);
    else if (!r.grade.tips.empty()) addLine(p, kickoffTipText(r.grade.tips.front().tip), kTip, accents);
}

// Detail lines of a result: why the boost warning, type and 50/50, record, measures, the other tips.
void appendResultDetails(DrawPanel& p, const KickoffResultView& r, const DrawContext& ctx, bool accents) {
    const KickoffMetrics& m = r.metrics;
    if (m.boostLooksUnlimited)
        addDetail(p,
                  r.passive ? "Lance l'exercice : il le coupe pendant tes essais."
                            : "Pas pu le couper ici : coupe-le en freeplay.",
                  colors::Orange, accents);

    std::string kind = std::string("Type : ") + kickoffTypeName(m.type);
    if (r.hasDuel) {
        kind += std::string(", ") + kickoffDuelName(r.duel.outcome);
        if (!r.opponent.empty()) kind += " contre " + r.opponent;
    }
    addDetail(p, kind, kDetail, accents);
    if (m.touched && !r.newPb && r.previousBest > 0.f)
        addDetail(p, "Record : " + secs(r.previousBest), kDetail, accents);
    if (m.touched) {
        const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
        addDetail(p,
                  "Vitesse : " + formatSpeed(m.speedToBall, unit, 0) +
                      "   Décalage : " + formatNumber(std::fabs(m.lateralOffset), 0) + " uu",
                  kDetail, accents);
    }
    const size_t firstDetailTip = m.boostLooksUnlimited ? 0 : 1;  // the first tip is a main line
    for (size_t i = firstDetailTip; i < r.grade.tips.size(); ++i)
        addDetail(p, kickoffTipText(r.grade.tips[i].tip), kTip, accents);
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
    const KeyStyle style = keyStyleFromIndex(ctx.settings.profile.buttonStyle);  // Xbox or PlayStation names
    const float s = ctx.hudScale;
    const int64_t frame = ctx.hasSnapshot ? std::max<int64_t>(ctx.snap.frame, lastTickFrame_) : lastTickFrame_;
    const KickoffSpawnInfo& info = kickoffSpawnInfo(spawn_);

    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = s * cfg_.hudScale;
    p.minWidth = 270.f * p.scale;
    std::string title = std::string("Kickoff : ") + info.name + (team_ == KickoffTeam::Orange ? " (orange)" : "");
    if (mode_ == KickoffMode::Sequence) {
        const int idx = std::min(kKoSpawnCount, sequenceDone_ + (phase_ == KickoffPhase::Done ? 0 : 1));
        title += ", " + std::to_string(idx) + "/" + std::to_string(kKoSpawnCount);
    }
    p.title = hudText(title, acc);

    // Main lines, at most 3: chrono (or the result, or the countdown when no other one is shown), the one tip, what
    // to do now or what comes next.
    const KickoffTipLoss* reminder = reminderTip(last_);
    const int countdownSecs = std::max(1, cfg_.countdownSeconds);
    if (phase_ == KickoffPhase::Placing) {
        addLine(p, "Placement de la voiture...", colors::White, acc, 1.1f);
    } else if (phase_ == KickoffPhase::Countdown) {
        const int64_t left = nominalGo_ - frame;
        const int n = std::clamp(static_cast<int>((left + 119) / 120), 1, countdownSecs);
        // The game's countdown or the big number already counts: the panel would be a second timer.
        if (!cfg_.nativeCountdown && !cfg_.bigCountdown)
            addLine(p, "Décompte : " + std::to_string(n), colors::White, acc, 1.2f);
        if (reminder) addLine(p, kickoffTipText(reminder->tip), kTip, acc);
        addLine(p,
                "Appuie déjà sur " + actionPrompt(ctx.profile, GameAction::Boost, style) + " + " +
                    actionPrompt(ctx.profile, GameAction::Throttle, style),
                colors::White, acc);
        if (cfg_.bigCountdown)
            out.text({view.screenW * 0.5f, view.screenH * cfg_.countdownY}, std::to_string(n), colors::White, 6.f * s,
                     true, Align::Center);
    } else if (phase_ == KickoffPhase::Live) {
        addLine(p, "Chrono : " + secs(liveTimeAt(frame)), colors::White, acc, 1.2f);
        if (reminder) addLine(p, kickoffTipText(reminder->tip), kTip, acc);
        if (cfg_.bigCountdown && frame - goFrame_ < 60)
            out.text({view.screenW * 0.5f, view.screenH * cfg_.countdownY}, "GO !", kAhead, 6.f * s, true,
                     Align::Center);
    } else if (phase_ == KickoffPhase::Done && last_.valid && !last_.passive) {
        appendResult(p, last_, acc);
        const float wait = cfg_.resetDelay - static_cast<float>(frame - doneFrame_) * kTickDt;
        if (mode_ == KickoffMode::Sequence && sequenceDone_ >= kKoSpawnCount) {
            if (wait > 0.f) addLine(p, "Fin de la série dans " + wholeSecs(wait), colors::White, acc);
        } else if (waitingForNext_ || !cfg_.autoRepeat) {
            // Without the automatic chain, Reset starts the next kickoff (also during the pause).
            const std::string reset = actionPrompt(ctx.profile, GameAction::ResetShot, style);
            addLine(p, "Appuie sur " + reset + " pour relancer.", colors::White, acc);
        } else if (wait > 0.f) {
            addLine(p, "Essai suivant dans " + wholeSecs(wait), colors::White, acc);
        }
    }

    // Detail lines (full HUD only).
    if (phase_ == KickoffPhase::Done && last_.valid && !last_.passive) appendResultDetails(p, last_, ctx, acc);
    addDetail(p, std::string("Mode : ") + kickoffModeName(mode_), kDetail, acc);
    if (ghostActive_) {
        std::string gl =
            std::string("Fantôme : ") + kickoffGhostModeName(repGhostMode_) + " (" + secs(ghost_.arrivalTime()) + ")";
        if (ghostFallback_) gl += ", pas encore de record";
        addDetail(p, gl, cfg_.ghostColor.withAlpha(255.f), acc);
    }
    if (cfg_.showSplits && (phase_ == KickoffPhase::Live || phase_ == KickoffPhase::Done)) {
        const char* ref = ghostActive_ ? "fantôme" : kickoffLevelName(kickoffLevelFromIndex(cfg_.gradeTarget));
        for (const auto& sp : liveSplits_) {
            if (!sp.reached) continue;
            const int pct = static_cast<int>(std::lround(sp.fraction * 100.f));
            addDetail(
                p,
                std::to_string(pct) + " % : " + secs(sp.playerTime) + "  (" + signedSecs(sp.delta) + " vs " + ref + ")",
                splitColor(sp.delta), acc);
        }
    }
    if (phase_ != KickoffPhase::Done && last_.valid && !last_.passive) {
        std::string prev = "Dernier : ";
        prev += last_.metrics.touched ? secs(last_.metrics.timeToTouch) : std::string("pas de touche");
        prev += ", " + std::to_string(last_.grade.total) + "/100";
        addDetail(p, prev, kDetail, acc, 0.85f);
    }
    out.panel(std::move(p));
}

void KickoffModule::drawResultCard(const ViewContext& view, const DrawContext& ctx, DrawList& out,
                                   bool compactTitle) const {
    const bool acc = ctx.settings.general.hudAccents;
    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = ctx.hudScale * cfg_.hudScale;
    p.minWidth = 270.f * p.scale;
    const KickoffSpawnInfo& info = kickoffSpawnInfo(last_.metrics.spawn);
    p.title = hudText(std::string(compactTitle ? "Kickoff analysé : " : "Kickoff : ") + info.name, acc);
    appendResult(p, last_, acc);
    appendResultDetails(p, last_, ctx, acc);
    out.panel(std::move(p));
}

} // namespace rlt
