#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Power shot (research section 2.12): a new contact with the car low (z < 140) and the ball low (z < 260), not a
// pinch candidate, ball not on the roof, no carry in the last 1.5 s (flick territory). Graded 2 ticks later when
// the exit speed reaches `powerMinSpeed` or when it was a flip shot (dodge <= 0.6 s before the contact).
// Variant: powerslide_shot (handbrake <= 0.5 s before, yaw rate >= 2 rad/s, UNVERIFIED) > flip_shot > power_shot.

void PowerShotDetector::reset() { *this = PowerShotDetector(); }

bool PowerShotDetector::engaged(float& since) const {
    if (!pending_) return false;
    since = tTouch_;
    return true;
}

std::optional<MechGrade> PowerShotDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)prev;
    std::optional<MechGrade> out;
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    if (cur.dodgeStart) tDodge_ = cur.t;
    if (cur.s.in.handbrake) tSlide_ = cur.t;
    if (cur.grounded && carryGeometry(cur.s)) lastCarry_ = cur.t;
    yawHist_[static_cast<size_t>(yawHead_)] = std::fabs(dot(c.angVel, c.basis.up));
    yawHead_ = (yawHead_ + 1) % kYawWindow;
    yawCount_ = std::min(yawCount_ + 1, kYawWindow);

    if (!pending_ && cur.touchNew && c.pos.z < 140.f && b.pos.z < 260.f) {
        const SurfaceHit sg = nearestSurface(b.pos, b.radius);
        const bool pinchy = cur.standardArena && sg.gap <= 120.f && dot(c.vel, -sg.normal) >= 300.f;
        const bool onRoof = c.basis.toLocal(b.pos - c.pos).z >= 100.f;
        const bool recentlyCarried = cur.t - lastCarry_ <= 1.5f;
        if (!pinchy && !onRoof && !recentlyCarried) {
            pending_ = true;
            evalAt_ = cur.frame + 2;
            tTouch_ = cur.t;
            carSpeed_ = length(c.vel);
            local_ = c.basis.toLocal(b.pos - c.pos);
            slideHeld_ = cur.s.in.handbrake;
            dodgeAgo_ = tDodge_ > -1e8f ? cur.t - tDodge_ : -1.f;
            slideAgo_ = tSlide_ > -1e8f ? cur.t - tSlide_ : -1.f;
            maxYaw_ = 0.f;
            for (int i = 0; i < yawCount_; ++i) maxYaw_ = std::max(maxYaw_, yawHist_[static_cast<size_t>(i)]);
        }
    }
    if (pending_ && cur.frame >= evalAt_) {
        pending_ = false;
        const float exitSpeed = length(b.vel);
        const bool flipShot = dodgeAgo_ >= 0.f && dodgeAgo_ <= 0.6f;
        if (exitSpeed >= p.powerMinSpeed || flipShot) out = grade(cur, exitSpeed, flipShot);
    }
    return out;
}

MechGrade PowerShotDetector::grade(const Frame& cur, float exitSpeed, bool flipShot) {
    const BallState& b = cur.s.ball;
    const GoalProjection proj = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
    const bool slideShot = slideAgo_ >= 0.f && slideAgo_ <= 0.5f && maxYaw_ >= 2.0f;
    MechGrade g;
    g.family = Family::PowerShot;
    g.tStart = tTouch_;
    g.tEnd = cur.t;
    g.variant = slideShot ? "powerslide_shot" : (flipShot ? "flip_shot" : "power_shot");
    const bool front = local_.x > 60.f && std::fabs(local_.y) < 70.f;
    const bool corner = local_.x > 30.f && std::fabs(local_.y) < 120.f;
    g.metrics.push_back(makeMetric("exit_speed", "Vitesse de sortie", exitSpeed, "uu/s", 3000.f));
    g.metrics.push_back(makeMetric("car_speed", "Vitesse de la voiture", carSpeed_, "uu/s", 1800.f));
    g.metrics.push_back(makeMetric("exit_kmh", "Vitesse de sortie (km/h)", exitSpeed * 0.036f, "km/h"));
    g.metrics.push_back(makeMetric("contact_local_x", "Contact (avant)", local_.x, "uu"));
    g.metrics.push_back(makeMetric("contact_local_y", "Contact (côté)", local_.y, "uu", 0.f));
    if (flipShot) g.metrics.push_back(makeMetric("dodge_to_contact", "Délai flip - contact", dodgeAgo_, "s", 0.1f));
    if (slideShot) {
        g.metrics.push_back(makeMetric("slide_to_contact", "Délai glissade - contact", slideAgo_, "s"));
        g.metrics.push_back(makeMetric("max_yaw_rate", "Rotation max", maxYaw_, "rad/s"));
    }
    if (proj.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj.onTarget ? 1.f : 0.f, "", 1.f));
    g.hasShot = true;
    g.ballSpeed = exitSpeed;
    g.projection = proj;
    if (exitSpeed < 2000.f) g.errors.push_back("PS_WEAK");
    if (flipShot && dodgeAgo_ > 0.25f) g.errors.push_back("PS_FLIP_EARLY");
    if (!corner) g.errors.push_back("PS_SIDE_HIT");
    if (slideHeld_) g.errors.push_back("PS_SLIDE_HELD");
    if (proj.valid && !proj.onTarget) g.errors.push_back("PS_OFF_TARGET");
    g.success = exitSpeed >= 2000.f && (!proj.valid || proj.onTarget);
    const float contactQ = front ? 1.f : (corner ? 0.7f : 0.2f);
    g.score = clamp01(0.5f * score01(exitSpeed, 1500.f, 3500.f) + 0.3f * accuracyScore(proj) + 0.2f * contactQ);
    return g;
}

} // namespace rlt::bm
