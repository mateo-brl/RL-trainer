// CoursesModule: drawing (rings, gates, guide line, catch marker) and the HUD panel. Read-only: draw never
// changes gameplay state. Positions come from the ViewContext (interpolated actors).

#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/courses/CourseRender.h"
#include "core/modules/courses/CoursesModule.h"

namespace rlt {

namespace {

constexpr Color kGrey{190, 190, 190, 255};
constexpr Color kMissed{255, 70, 70, 120};

std::string signedSec(float v) { return (v < 0.f ? "-" : "+") + formatNumber(std::fabs(v), 2); }

} // namespace

void CoursesModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (phase_ == CoursesPhase::Idle || !cfg_.enabled) return;
    if (view.valid) {
        if (kindUsesCourse(kind_) && !course_.rings.empty()) drawCourse(view, ctx, out);
        if (kind_ == CoursesKind::Catch && cfg_.catchMarker && phase_ != CoursesPhase::Result) drawCatchMarker(view, out);
    }
    if (cfg_.hud) drawHud(view, ctx, out);
}

void CoursesModule::drawCourse(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const auto& rings = course_.rings;
    const int n = static_cast<int>(rings.size());
    const bool started = phase_ == CoursesPhase::Running || phase_ == CoursesPhase::Result;
    const int next = started ? std::min(run_.nextRing(), n) : 0;
    const int first = cfg_.showPassed ? std::max(0, next - 3) : next;
    const int last = std::min(n - 1, next + std::clamp(cfg_.lookahead, 0, 8));
    const float pulse = 0.8f + 0.2f * std::sin(ctx.snap.time * 6.f);
    const float lw = std::clamp(cfg_.lineWidth, 0.5f, 10.f);
    const Vec3& fwd = view.camBasis.fwd;

    Occluder ballOcc;
    float ballDepth = 1e9f;
    if (view.ballValid) {
        ballOcc = Occluder{true, view.ballVisualPos, view.ballRadius};
        ballDepth = dot(view.ballVisualPos - view.camPos, fwd);
    }

    // Far to near: the next ring is drawn last, on top.
    for (int i = last; i >= first; --i) {
        const courses::Ring& r = rings[static_cast<size_t>(i)];
        const Vec3 c = courses::toF(r.center);
        const float radius = static_cast<float>(std::max(r.radius, r.halfHeight));
        const float depth = dot(c - view.camPos, fwd);
        if (depth < -radius) continue;  // entirely behind the camera
        const bool passed = i < next;
        const bool isNext = i == next;
        int segs = courses::circleSegments(courses::projectedRadiusPx(radius, std::max(depth, 50.f), view.fovDeg, view.screenW));
        Color col;
        float width = lw;
        if (passed) {
            col = run_.ringMissed(i) ? kMissed : cfg_.passedColor;
            width = lw * 0.6f;
            segs = std::min(segs, 24);
        } else if (isNext) {
            col = cfg_.nextColor.withAlpha(cfg_.nextColor.a * pulse);
            width = lw * 1.6f;
            segs = std::max(segs, 16);
        } else {
            const float fade = std::max(0.25f, 1.f - 0.18f * static_cast<float>(i - next - 1));
            col = cfg_.ringColor.withAlpha(cfg_.ringColor.a * fade);
        }
        const Occluder occ = (ballOcc.enabled && depth > ballDepth) ? ballOcc : Occluder{};
        courses::appendRingLines(r, segs, col, width, occ, out);

        if (passed) continue;
        const float top = static_cast<float>(courses::ringExtent(r, {0, 0, 1}));
        const Vec3 label = c + Vec3{0.f, 0.f, top + 55.f};
        if (isNext) {
            out.text3D(label, std::to_string(i + 1) + "/" + std::to_string(n), cfg_.nextColor, 1.8f);
            // Direction arrow through the opening.
            const Vec3 nrm = courses::toF(r.normal);
            const Vec3 side = normalize(cross(nrm, Vec3{0.f, 0.f, 1.f}), courses::toF(r.up));
            const float len = std::min(radius * 0.45f, 160.f);
            const Vec3 tail = c - nrm * len, tip = c + nrm * len;
            out.line3D(tail, tip, cfg_.nextColor, width * 0.8f, occ);
            out.line3D(tip, tip - nrm * (len * 0.4f) + side * (len * 0.3f), cfg_.nextColor, width * 0.8f, occ);
            out.line3D(tip, tip - nrm * (len * 0.4f) - side * (len * 0.3f), cfg_.nextColor, width * 0.8f, occ);
            // Floor shadow under air rings (depth cue).
            if (r.surface == courses::Surface::Air && c.z > 250.f) {
                const Color sh = cfg_.nextColor.withAlpha(90.f);
                out.line3D({c.x - 70.f, c.y, 4.f}, {c.x + 70.f, c.y, 4.f}, sh, 2.f);
                out.line3D({c.x, c.y - 70.f, 4.f}, {c.x, c.y + 70.f, 4.f}, sh, 2.f);
            }
        } else if (cfg_.ringNumbers) {
            out.text3D(label, std::to_string(i + 1), col, 1.1f);
        }
        if (r.flags & courses::kFlagFinal) out.text3D(label, "ARRIVÉE", cfg_.nextColor, 1.2f, {0.f, -22.f});
        if ((r.flags & courses::kFlagBoostRefill) && cfg_.boostRefills && !cfg_.unlimitedBoost && kindIsRings(kind_))
            out.text3D(c - Vec3{0.f, 0.f, top + 40.f}, "BOOST", colors::Orange, 1.1f);
    }

    // Guide line from the car (or the ball) to the next ring.
    if (cfg_.guideLine && next < n && phase_ != CoursesPhase::Result) {
        const bool ball = course_.ballCourse;
        const bool ok = ball ? view.ballValid : view.carValid;
        if (ok) {
            const Vec3 from = ball ? view.ballVisualPos : view.carVisualPos;
            const Vec3 to = courses::toF(rings[static_cast<size_t>(next)].center);
            if (distance(from, to) > 150.f) courses::appendDashedLine(from, to, 10, cfg_.guideColor, 2.f, out);
        }
    }
}

void CoursesModule::drawCatchMarker(const ViewContext& view, DrawList& out) const {
    Vec3 land;
    bool have = false;
    if (phase_ == CoursesPhase::Setup || phase_ == CoursesPhase::Countdown) {
        land = catchPlan_.landing;
        have = true;
    } else if (phase_ == CoursesPhase::Running && view.ballValid && !ballOnRoofNow_ && view.ballVisualPos.z > 200.f) {
        float t = 0.f;
        have = courses::predictDownCrossing(view.ballVisualPos, view.ballVel, 148.f, 4.f, land, t);
    }
    if (!have) return;
    const Vec3 ground{land.x, land.y, 4.f};
    out.circle3D(ground, {0.f, 0.f, 1.f}, 130.f, cfg_.guideColor, 2.5f, 24);
    if (view.ballValid) courses::appendDashedLine(view.ballVisualPos, ground, 8, cfg_.guideColor.withAlpha(120.f), 1.5f, out);
}

void CoursesModule::drawHud(const ViewContext& view, const DrawContext& ctx, DrawList& out) const {
    const float scale = ctx.hudScale > 0.f ? ctx.hudScale : 1.f;
    DrawPanel p;
    p.pos = {clamp01(cfg_.hudX) * view.screenW, clamp01(cfg_.hudY) * view.screenH};
    p.anchorX = Align::Left;
    p.scale = scale;
    p.minWidth = 260.f * scale;
    p.title = std::string(kindTitle(kind_)) + (fromCode_ ? " - code, niv. " : " - niv. ") + std::to_string(difficulty_);
    const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
    const KeyStyle style = keyStyleFromIndex(ctx.settings.profile.buttonStyle);
    const bool course = kindUsesCourse(kind_);
    const int n = course ? static_cast<int>(course_.rings.size()) : 0;
    const PbEntry* pb = course ? findPb(code_) : nullptr;
    const bool hasPb = pb && pb->time > 0.f;
    auto line = [&](std::string text, Color c = colors::White, float bar = -1.f, Color barColor = colors::Green, float sc = 1.f) {
        PanelLine l;
        l.text = std::move(text);
        l.color = c;
        l.bar = bar;
        l.barColor = barColor;
        l.scale = sc;
        p.lines.push_back(std::move(l));
    };
    if (course && !code_.empty()) line("Code " + code_, kGrey, -1.f, colors::Green, 0.85f);

    switch (phase_) {
    case CoursesPhase::Setup:
    case CoursesPhase::Countdown: {
        const int remaining = std::max(0, countdownTotal_ - phaseTicks_);
        line("Départ dans " + std::to_string((remaining + 119) / 120), colors::Yellow, -1.f, colors::Green, 1.3f);
        switch (kind_) {
        case CoursesKind::RingsAirDribble: line("Fais passer la balle dans les " + std::to_string(n) + " anneaux"); break;
        case CoursesKind::Slalom: line("Passe les " + std::to_string(n) + " portes, balle sur le toit"); break;
        case CoursesKind::Dribble:
            line("Tiens " + formatNumber(dribbleTarget_, 0) + " s, moyenne min " + formatSpeed(dribbleMinSpeed_, unit, 0));
            break;
        case CoursesKind::Catch:
            line("Rattrape la balle et garde-la " + formatNumber(cfg_.catchHold, 1) + " s sur le toit");
            break;
        default: line("Passe les " + std::to_string(n) + " anneaux dans l'ordre"); break;
        }
        if (kind_ == CoursesKind::RingsAir || kind_ == CoursesKind::RingsGround) {
            const std::string key = actionKeyLabel(ctx.profile, GameAction::BallCam, style);
            line("Astuce : coupe la caméra balle" + (key.empty() ? std::string() : " (" + key + ")"), kGrey, -1.f,
                 colors::Green, 0.85f);
        }
        if (hasPb) line("Record " + formatTime(pb->time), colors::Cyan);
        break;
    }
    case CoursesPhase::Running: {
        if (course) {
            const float t = runTime_ + static_cast<float>(run_.penaltyTime());
            std::string tl = "Temps " + formatTime(t);
            if (run_.penaltyTime() > 0.0) tl += " (dont " + formatNumber(static_cast<float>(run_.penaltyTime()), 1) + " s de pénalité)";
            line(tl, colors::White, -1.f, colors::Green, 1.2f);
            const int k = std::min(run_.nextRing() + 1, n);
            line(std::string(kind_ == CoursesKind::Slalom ? "Porte " : "Anneau ") + std::to_string(k) + "/" + std::to_string(n),
                 colors::White, n > 0 ? static_cast<float>(run_.nextRing()) / static_cast<float>(n) : 0.f, cfg_.nextColor);
            if (hasDelta_) line("Écart record " + signedSec(lastDelta_) + " s", lastDelta_ <= 0.f ? colors::Green : colors::Red);
            else if (hasPb) line("Record " + formatTime(pb->time), colors::Cyan);
            if (kindIsRings(kind_) && !cfg_.unlimitedBoost && ctx.hasSnapshot && ctx.snap.car.valid) {
                const float boost = std::clamp(ctx.snap.car.boost, 0.f, 100.f);
                line("Boost " + std::to_string(static_cast<int>(std::lround(boost))), colors::White, boost / 100.f, colors::Orange);
            }
            if (kind_ == CoursesKind::Slalom && !ballOnRoofNow_) line("Balle hors du toit !", colors::Orange);
            if (run_.misses() > 0) line("Ratés : " + std::to_string(run_.misses()), colors::Red);
            const float left = timeLimit() - runTime_;
            if (left < 10.f) line("Temps restant " + formatNumber(std::max(0.f, left), 1) + " s", colors::Orange);
        } else if (kind_ == CoursesKind::Dribble) {
            const float held = keeper_.heldTime();
            line("Tenue " + formatNumber(held, 1) + " / " + formatNumber(dribbleTarget_, 0) + " s", colors::White,
                 dribbleTarget_ > 0.f ? clamp01(held / dribbleTarget_) : 0.f, targetReached_ ? colors::Green : colors::Cyan, 1.2f);
            if (dribbleBest_ > 0.f) line("Record " + formatNumber(dribbleBest_, 1) + " s", colors::Cyan);
            if (ballOnRoofNow_) line("Balle sur le toit", colors::Green);
            else line("Balle hors du toit !", colors::Orange);
            const float avg = speedSamples_ > 0 ? static_cast<float>(speedSum_ / speedSamples_) : 0.f;
            line("Vitesse moy. " + formatSpeed(avg, unit, 0) + " (min " + formatSpeed(dribbleMinSpeed_, unit, 0) + ")",
                 avg >= dribbleMinSpeed_ ? colors::Green : colors::Orange);
        } else {
            const float limit = std::max(0.1f, catchPlan_.timeLimit);
            line("Temps " + formatNumber(runTime_, 1) + " / " + formatNumber(limit, 1) + " s", colors::White,
                 clamp01(1.f - runTime_ / limit), runTime_ < limit ? colors::Cyan : colors::Red, 1.2f);
            line("Sur le toit " + formatNumber(catchOnRoof_, 1) + " / " + formatNumber(cfg_.catchHold, 1) + " s", colors::White,
                 cfg_.catchHold > 0.f ? clamp01(catchOnRoof_ / cfg_.catchHold) : 0.f, colors::Green);
            line("Touches : " + std::to_string(touches_), kGrey);
        }
        break;
    }
    case CoursesPhase::Result: {
        line(last_.headline, last_.success ? colors::Green : colors::Red, -1.f, colors::Green, 1.1f);
        line("Score " + std::to_string(static_cast<int>(std::lround(last_.score))) + " - " + last_.verdict, colors::White);
        if (!resultHint_.empty()) line(resultHint_, colors::Yellow);
        if (cfg_.chainMode != 3) {
            const int hold = std::max(1, static_cast<int>(std::lround(cfg_.resultHold * 120.f)));
            const float left = static_cast<float>(std::max(0, hold - phaseTicks_)) / 120.f;
            line("Suite dans " + formatNumber(left, 1) + " s", kGrey, -1.f, colors::Green, 0.85f);
        }
        break;
    }
    default: break;
    }
    if (flashTicks_ > 0 && !flashText_.empty()) line(flashText_, flashColor_, -1.f, colors::Green, 1.15f);
    if (phase_ == CoursesPhase::Running || phase_ == CoursesPhase::Result)
        line("Recommencer : " + actionPrompt(ctx.profile, GameAction::ResetShot, style), kGrey, -1.f, colors::Green, 0.8f);
    out.panel(std::move(p));
}

} // namespace rlt
