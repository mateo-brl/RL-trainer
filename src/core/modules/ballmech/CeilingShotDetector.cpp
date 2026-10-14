#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Ceiling shot (research section 2.2):
//   Idle -> OnCeiling (gap <= 90, roof down, >= 3 arena wheels) -> Falling{jumpedOff}
//   Falling -> touch (evaluated 2 ticks later) | fail: flip wasted (0.35 s without touch), landing, 4 s.
// A drop that never came near the ball (no dodge, no touch, ball always > 800 uu away) is not an attempt.

void CeilingShotDetector::reset() {
    state_ = State::Idle;
    tCeilStart_ = tLeave_ = 0.f;
    tDodge_ = -1.f;
    ceilTime_ = separation_ = ballSpeed_ = 0.f;
    minBallDist_ = 1e9f;
    jumpedOff_ = touchSeen_ = flipShot_ = false;
    evalAt_ = 0;
    proj_ = GoalProjection{};
}

void CeilingShotDetector::armOnCeiling(float t) {
    reset();
    state_ = State::OnCeiling;
    tCeilStart_ = t;
}

bool CeilingShotDetector::engaged(float& since) const {
    if (state_ != State::Falling) return false;
    since = tCeilStart_;
    return true;
}

std::optional<MechGrade> CeilingShotDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)p;
    std::optional<MechGrade> out;
    if (!cur.standardArena) {
        if (state_ != State::Idle) reset();
        return out;
    }
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const bool onCeiling = (arena::kCeilingZ - c.pos.z) <= 90.f && -c.basis.up.z >= 0.72f && cur.worldWheels >= 3;
    switch (state_) {
    case State::Idle:
        if (onCeiling) {
            reset();
            state_ = State::OnCeiling;
            tCeilStart_ = cur.t;
        }
        break;
    case State::OnCeiling:
        if (!onCeiling && cur.worldWheels == 0) {
            state_ = State::Falling;
            tLeave_ = cur.t;
            ceilTime_ = cur.t - tCeilStart_;
            jumpedOff_ = cur.s.in.jump || prev.s.in.jump;
        } else if (!onCeiling) {
            state_ = State::Idle;
        }
        break;
    case State::Falling: {
        minBallDist_ = std::min(minBallDist_, distance(c.pos, b.pos));
        if (cur.t - tLeave_ < 0.15f && ((c.jumped && !prev.s.car.jumped) || cur.jumpEdge)) jumpedOff_ = true;
        if (cur.dodgeStart && tDodge_ < 0.f) tDodge_ = cur.t;
        if (cur.touched && !touchSeen_) {
            touchSeen_ = true;
            evalAt_ = cur.frame + 2;
            flipShot_ = tDodge_ >= 0.f && cur.t - tDodge_ <= 0.35f;
            separation_ = arena::kCeilingZ - c.pos.z;
        }
        if (touchSeen_ && cur.frame >= evalAt_) {
            ballSpeed_ = length(b.vel);
            proj_ = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
            out = finish(cur, true);
        } else if (!touchSeen_ && tDodge_ >= 0.f && cur.t - tDodge_ > 0.35f) {
            out = finish(cur, false);
        } else if (!touchSeen_ && (cur.grounded || cur.t - tLeave_ > 4.f)) {
            out = finish(cur, false);
        }
        break;
    }
    }
    return out;
}

std::optional<MechGrade> CeilingShotDetector::finish(const Frame& cur, bool touched) {
    const bool attempt = touched || tDodge_ >= 0.f || minBallDist_ <= 800.f;
    if (!attempt) {
        reset();
        return std::nullopt;
    }
    MechGrade g;
    g.family = Family::Ceiling;
    g.variant = jumpedOff_ ? "jump_off" : "drop";
    g.tStart = tCeilStart_;
    g.tEnd = cur.t;
    g.metrics.push_back(makeMetric("ceiling_time", "Temps au plafond", ceilTime_, "s"));
    g.metrics.push_back(makeMetric("jumped_off", "Saut en quittant le plafond", jumpedOff_ ? 1.f : 0.f, "", 0.f));
    g.metrics.push_back(makeMetric("flip_shot", "Frappe en flip", flipShot_ ? 1.f : 0.f, "", 1.f));
    if (touched) {
        g.metrics.push_back(makeMetric("drop_to_shot", "Chute avant la frappe", cur.t - tLeave_, "s"));
        g.metrics.push_back(makeMetric("separation_at_touch", "Distance au plafond", separation_, "uu"));
        g.metrics.push_back(makeMetric("ball_speed_after", "Vitesse de la balle", ballSpeed_, "uu/s", 2000.f));
        if (proj_.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj_.onTarget ? 1.f : 0.f, "", 1.f));
        g.hasShot = true;
        g.ballSpeed = ballSpeed_;
        g.projection = proj_;
    }
    g.success = touched && flipShot_ && !jumpedOff_ && ballSpeed_ >= 1200.f;
    if (jumpedOff_) g.errors.push_back("CEIL_JUMPED_OFF");
    if (!touched && tDodge_ >= 0.f) g.errors.push_back("CEIL_FLIP_WASTED");
    if (!touched && tDodge_ < 0.f) g.errors.push_back("CEIL_MISSED");
    if (touched && !flipShot_) g.errors.push_back("CEIL_NO_FLIP_SHOT");
    if (touched && ballSpeed_ < 1500.f) g.errors.push_back("FR_WEAK_SHOT");
    if (touched && proj_.valid && !proj_.onTarget) g.errors.push_back("FR_OFF_TARGET");
    const float kept = jumpedOff_ ? 0.f : 1.f;
    const float conv = touched ? (flipShot_ ? 1.f : 0.4f) : 0.f;
    const float power = touched ? score01(ballSpeed_, 1000.f, 2600.f) : 0.f;
    const float acc = touched ? accuracyScore(proj_) : 0.f;
    g.score = clamp01(0.25f * kept + 0.25f * conv + 0.25f * power + 0.25f * acc);
    reset();
    return g;
}

} // namespace rlt::bm
