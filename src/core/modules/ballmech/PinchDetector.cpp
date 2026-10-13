#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Pinch (research section 2.11): at a new contact the ball is within 120 uu of a surface (floor, ceiling, side wall,
// back wall outside the goal mouth, corner), the car moves into that surface at >= 300 uu/s and sits on the field
// side of the ball. The exit speed is the maximum over 6 ticks. Success: exit >= pinchSpeed (default 4000 uu/s,
// UNVERIFIED: a normal supersonic flip hit tops out around 3800). "Kuxir" = wall pinch while airborne.
// A weak hit (< 3000 uu/s) near a surface is only a weak claim, reported inside the pinch drill only.

namespace {
const char* surfaceVariant(Surface s) {
    switch (s) {
    case Surface::Floor: return "ground_pinch";
    case Surface::Ceiling: return "ceiling_pinch";
    case Surface::SideWall: return "wall_pinch";
    case Surface::BackWall: return "backwall_pinch";
    case Surface::Corner: return "corner_pinch";
    default: return "pinch";
    }
}
} // namespace

void PinchDetector::reset() { *this = PinchDetector(); }

bool PinchDetector::engaged(float& since) const {
    if (!pending_) return false;
    since = tTouch_;
    return true;
}

std::optional<MechGrade> PinchDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)prev;
    std::optional<MechGrade> out;
    if (!cur.standardArena) {
        pending_ = false;
        return out;
    }
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    if (!pending_ && cur.touchNew) {
        const SurfaceHit sg = nearestSurface(b.pos, b.radius);
        const float push = dot(c.vel, -sg.normal);
        const bool fieldSide = dot(b.pos - c.pos, -sg.normal) > 0.f;
        if (sg.gap <= 120.f && push >= 300.f && fieldSide) {
            pending_ = true;
            ticks_ = 0;
            gap_ = sg.gap;
            push_ = push;
            surface_ = sg.kind;
            maxSpeed_ = 0.f;
            tTouch_ = cur.t;
            kuxir_ = (sg.kind == Surface::SideWall || sg.kind == Surface::Corner) && cur.worldWheels == 0;
        }
    }
    if (pending_) {
        maxSpeed_ = std::max(maxSpeed_, length(b.vel));
        if (++ticks_ >= 7) {  // the touch tick is sampled before the hit: 6 ticks after it
            pending_ = false;
            const GoalProjection proj = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
            MechGrade g;
            g.family = Family::Pinch;
            g.variant = std::string(surfaceVariant(surface_)) + (kuxir_ ? "_kuxir" : "");
            g.tStart = tTouch_;
            g.tEnd = cur.t;
            g.metrics.push_back(makeMetric("exit_speed", "Vitesse de sortie", maxSpeed_, "uu/s", p.pinchSpeed + 500.f));
            g.metrics.push_back(makeMetric("surface_gap", "Écart balle / surface", gap_, "uu", 0.f));
            g.metrics.push_back(makeMetric("push_speed", "Vitesse vers la surface", push_, "uu/s"));
            g.metrics.push_back(makeMetric("exit_kmh", "Vitesse de sortie (km/h)", maxSpeed_ * 0.036f, "km/h"));
            if (proj.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj.onTarget ? 1.f : 0.f, "", 1.f));
            g.hasShot = true;
            g.ballSpeed = maxSpeed_;
            g.projection = proj;
            g.success = maxSpeed_ >= p.pinchSpeed;
            if (!g.success) g.errors.push_back(gap_ > 25.f ? "PI_TIMING" : "PI_NOT_PINCHED");
            if (proj.valid && !proj.onTarget) g.errors.push_back("PI_OFF_TARGET");
            g.weak = maxSpeed_ < 3000.f;
            g.drillOnly = g.weak;  // a soft hit near a surface is rarely a pinch attempt outside the pinch drill
            g.score = clamp01(0.6f * score01(maxSpeed_, 3000.f, 5500.f) + 0.4f * accuracyScore(proj));
            out = g;
        }
    }
    return out;
}

} // namespace rlt::bm
