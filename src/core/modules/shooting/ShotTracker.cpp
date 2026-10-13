#include "core/modules/shooting/ShotTracker.h"

#include <algorithm>

namespace rlt::shoot {

void ShotTracker::reset() { *this = ShotTracker(); }

void ShotTracker::start(const BallState& ball, const GoalFrame& goal, int64_t frame, float shotSpeed) {
    reset();
    active_ = true;
    goal_ = goal;
    start_ = frame;
    speed_ = shotSpeed;
    if (ball.valid) {
        prevPos_ = ball.pos;
        prevFrame_ = frame;
        hasPrev_ = true;
        if (const auto c = predictToPlaneY(ball.pos, ball.vel, ball.angVel, goal_.planeY(), 4.f, ball.radius)) {
            hasPred_ = hasFirstPred_ = true;
            pred_ = firstPred_ = goal_.toPlane(c->pos);
            predWorld_ = c->pos;
            predTime_ = c->time;
        }
    }
}

ShotOutcome ShotTracker::finish(ShotEnd end, bool reached, const Vec3& hitWorld, int64_t frame) {
    ShotOutcome o;
    o.end = end;
    o.reached = reached;
    o.hitWorld = hitWorld;
    o.hit = goal_.toPlane(hitWorld);
    o.shotSpeed = speed_;
    o.flightTime = static_cast<float>(std::max<int64_t>(0, frame - start_)) * kTickDt;
    o.hadEarlyPrediction = hasFirstPred_;
    o.earlyPrediction = firstPred_;
    active_ = false;
    hasPred_ = false;
    return o;
}

std::optional<ShotOutcome> ShotTracker::update(const Snapshot& s) {
    if (!active_ || !s.ball.valid) return std::nullopt;
    const Vec3 p = s.ball.pos;
    if (hasPrev_) {
        // Teleport (reset, other plugin): the tolerance grows with skipped frames (ball max 50 uu per tick).
        const int64_t gap = std::clamp<int64_t>(s.frame - prevFrame_, 1, 120);
        if (distance(p, prevPos_) > 400.f + 55.f * static_cast<float>(gap - 1))
            return finish(ShotEnd::Aborted, false, p, s.frame);
    }

    const float front = kGoalLineY - kFrontOffset;
    if (hasPrev_) {
        const float a = goal_.sign * prevPos_.y, b = goal_.sign * p.y;
        const bool crossedFront = a < front && b >= front;
        const bool crossedGoal = a < kGoalLineY && b >= kGoalLineY;
        if (crossedFront || crossedGoal) {
            Vec3 hit{p.x, goal_.planeY(), p.z};
            if (b >= kGoalLineY) {
                const float f = (kGoalLineY - a) / (b - a);
                hit = prevPos_ + (p - prevPos_) * f;
                hit.y = goal_.planeY();
            } else if (const auto c =
                           predictToPlaneY(p, s.ball.vel, s.ball.angVel, goal_.planeY(), 0.5f, s.ball.radius)) {
                hit = c->pos;
            }
            return finish(ShotEnd::Crossed, true, hit, s.frame);
        }
    }

    if (const auto c = predictToPlaneY(p, s.ball.vel, s.ball.angVel, goal_.planeY(), 4.f, s.ball.radius)) {
        hasPred_ = true;
        pred_ = goal_.toPlane(c->pos);
        predWorld_ = c->pos;
        predTime_ = c->time;
        if (!hasFirstPred_) {
            hasFirstPred_ = true;
            firstPred_ = pred_;
        }
    } else {
        hasPred_ = false;
    }

    const float towards = goal_.sign * s.ball.vel.y;
    const bool controlled = s.car.valid && distance(p, s.car.pos) < 400.f;
    if (towards < 50.f && !controlled)
        ++awayTicks_;
    else
        awayTicks_ = 0;
    prevPos_ = p;
    prevFrame_ = s.frame;
    hasPrev_ = true;
    if (awayTicks_ >= kAwayTicks) return finish(ShotEnd::Short, false, p, s.frame);
    if (static_cast<float>(s.frame - start_) * kTickDt > kMaxFlight) return finish(ShotEnd::Timeout, false, p, s.frame);
    return std::nullopt;
}

std::optional<ShotOutcome> ShotTracker::onGoalEvent(int64_t frame) {
    if (!active_) return std::nullopt;
    const Vec3 hit = hasPred_ ? predWorld_ : Vec3{prevPos_.x, goal_.planeY(), prevPos_.z};
    return finish(ShotEnd::Goal, true, hit, frame);
}

} // namespace rlt::shoot
