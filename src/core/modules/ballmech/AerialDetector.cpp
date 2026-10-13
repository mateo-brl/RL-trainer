#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Aerial touch (research sections 2.9 and 2.10): car airborne (z >= 200), ball z >= 250, evaluated 2 ticks after a
// new contact; soft touches (dribble taps) below `aerialMinSpeed` are not shots.
// Variant: redirect (incoming >= 800 uu/s turned by >= 45 deg) > air_roll_shot (>= 40 % of the last 0.5 s with
// |roll| >= 0.5) > aerial_shot. Accuracy: goal projection, distance of the ball centre from the nose axis, and,
// when the hook gave the contact point, the angle between the impact direction and the goal direction.

void AerialDetector::reset() { *this = AerialDetector(); }

bool AerialDetector::engaged(float& since) const {
    if (!pending_) return false;
    since = tTouch_;
    return true;
}

std::optional<MechGrade> AerialDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)prev;
    std::optional<MechGrade> out;
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const float roll = cur.s.in.roll;
    rollHist_[static_cast<size_t>(rollHead_)] = static_cast<signed char>(roll >= 0.5f ? 1 : (roll <= -0.5f ? -1 : 0));
    rollHead_ = (rollHead_ + 1) % kRollWindow;
    rollCount_ = std::min(rollCount_ + 1, kRollWindow);

    if (!pending_ && cur.touchNew && cur.worldWheels == 0 && c.pos.z >= 200.f && b.pos.z >= 250.f) {
        pending_ = true;
        evalAt_ = cur.frame + 2;
        tTouch_ = cur.t;
        vIn_ = b.vel;  // the touch tick is sampled before the hit
        const Vec3 L = c.basis.toLocal(b.pos - hitboxCenter(c));
        offAxis_ = std::sqrt(L.y * L.y + L.z * L.z);
        int left = 0, right = 0;
        for (int i = 0; i < rollCount_; ++i) {
            const signed char v = rollHist_[static_cast<size_t>(i)];
            if (v > 0) ++right;
            else if (v < 0) ++left;
        }
        airRollFrac_ = rollCount_ ? static_cast<float>(left + right) / static_cast<float>(rollCount_) : 0.f;
        rollConsistency_ = (left + right) ? static_cast<float>(std::max(left, right)) / static_cast<float>(left + right)
                                          : 0.f;
        rollDir_ = right > left ? 1 : (left > right ? -1 : 0);
        impactErr_ = -1.f;
        if (cur.hasHit && cur.standardArena) {
            const Vec3 push = b.pos - cur.hitLocation;
            const Vec3 toGoal = Vec3{0.f, cur.sign * arena::kBackWallY, arena::kGoalHeight * 0.5f} - b.pos;
            if (length(push) > 1.f) impactErr_ = angleBetweenDeg(push, toGoal);
        }
    }
    if (pending_ && cur.frame >= evalAt_) {
        pending_ = false;
        const float speed = length(b.vel);
        if (speed >= p.aerialMinSpeed) out = grade(cur, speed);
    }
    return out;
}

MechGrade AerialDetector::grade(const Frame& cur, float speed) {
    const BallState& b = cur.s.ball;
    const GoalProjection proj = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
    const float turn = angleBetweenDeg(vIn_, b.vel);
    const bool redirect = length(vIn_) >= 800.f && turn >= 45.f;
    const bool airRoll = airRollFrac_ >= 0.4f;
    MechGrade g;
    g.family = Family::Aerial;
    g.tStart = tTouch_;
    g.tEnd = cur.t;
    g.variant = redirect ? "redirect" : (airRoll ? "air_roll_shot" : "aerial_shot");
    g.metrics.push_back(makeMetric("ball_speed_after", "Vitesse de la balle", speed, "uu/s", 2400.f));
    g.metrics.push_back(makeMetric("off_axis", "Écart au centre (nez)", offAxis_, "uu", 40.f));
    g.metrics.push_back(makeMetric("air_roll_frac", "Air roll (0,5 s avant)", airRollFrac_ * 100.f, "%"));
    g.metrics.push_back(makeMetric("air_roll_consistency", "Air roll directionnel", rollConsistency_ * 100.f, "%", 100.f));
    g.metrics.push_back(makeMetric("air_roll_dir", "Sens de l'air roll (-1 g, +1 d)", static_cast<float>(rollDir_), ""));
    g.metrics.push_back(makeMetric("incoming_speed", "Vitesse avant la touche", length(vIn_), "uu/s"));
    g.metrics.push_back(makeMetric("redirect_angle", "Déviation", turn, "°"));
    if (proj.valid) {
        g.metrics.push_back(makeMetric("angle_err_deg", "Écart de direction", proj.angleErrDeg, "°", 0.f));
        g.metrics.push_back(makeMetric("on_target", "Cadré", proj.onTarget ? 1.f : 0.f, "", 1.f));
    }
    if (impactErr_ >= 0.f) g.metrics.push_back(makeMetric("impact_err_deg", "Point d'impact vs but", impactErr_, "°", 0.f));
    g.hasShot = true;
    g.ballSpeed = speed;
    g.projection = proj;
    if (proj.valid && !proj.onTarget) g.errors.push_back("AE_OFF_TARGET");
    if (proj.valid && !proj.onTarget && impactErr_ > 30.f) g.errors.push_back("AE_HIT_SIDE");
    if (offAxis_ > 90.f) g.errors.push_back("AE_OFF_CENTER");
    if (speed < 1500.f) g.errors.push_back("AE_WEAK");
    if (airRoll && rollConsistency_ < 0.7f) g.errors.push_back("AE_ROLL_MIXED");
    g.success = !proj.valid || proj.onTarget;
    const float acc = accuracyScore(proj);
    const float power = score01(speed, 1200.f, 2800.f);
    const float centre = score01(offAxis_, 140.f, 40.f);
    if (airRoll && !redirect)
        g.score = clamp01(0.40f * acc + 0.25f * power + 0.25f * centre + 0.10f * rollConsistency_);
    else
        g.score = clamp01(0.45f * acc + 0.25f * power + 0.30f * centre);
    return g;
}

} // namespace rlt::bm
