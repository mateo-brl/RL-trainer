#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Double tap (research section 2.7):
//   Idle -> FirstTouch (aerial touch) -> evaluated 2 ticks later: backboard trajectory? -> AwaitBounce
//   AwaitBounce: new touch before the wall -> abort (air dribble); no bounce within 2.5 s -> Idle
//   AwaitBounce -> bounce (vy changes sign near the opponent back wall) -> AwaitSecond
//   AwaitSecond: touch -> Eval -> grade | DT_LANDED (landed after flying) | DT_NO_SECOND_TOUCH (ball low or 2.5 s)
// Drill "read" mode: the drill sent the ball to the backboard itself, so the detector starts in AwaitBounce.
// A failure while the car was far from the rebound (> 1500 uu) is not a real attempt: weak, drill only.

void DoubleTapDetector::reset() {
    state_ = State::Idle;
    tFirst_ = tBounce_ = tSecond_ = bounceZ_ = carDistAtBounce_ = 0.f;
    evalAt_ = 0;
    readMode_ = false;
    wasAirborne_ = false;
}

void DoubleTapDetector::armReadMode(float now) {
    reset();
    state_ = State::AwaitBounce;
    tFirst_ = now;
    readMode_ = true;
}

bool DoubleTapDetector::engaged(float& since) const {
    if (state_ == State::Idle) return false;
    since = tFirst_;
    return true;
}

std::optional<MechGrade> DoubleTapDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)p;
    std::optional<MechGrade> out;
    if (!cur.standardArena) {
        if (state_ != State::Idle) reset();
        return out;
    }
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const float sgn = cur.sign;
    const bool carAir = cur.worldWheels == 0 && c.pos.z >= 150.f;
    if (state_ != State::Idle && cur.worldWheels == 0) wasAirborne_ = true;
    switch (state_) {
    case State::Idle:
        if (cur.touchNew && carAir) {
            state_ = State::FirstTouch;
            evalAt_ = cur.frame + 2;
            tFirst_ = cur.t;
            wasAirborne_ = true;
            readMode_ = false;
        }
        break;
    case State::FirstTouch:
        if (cur.frame >= evalAt_) {
            const float vy = b.vel.y * sgn;
            const float wallY = arena::kBackWallY - b.radius;
            bool armed = false;
            if (vy >= 500.f) {
                const float t = (wallY - b.pos.y * sgn) / vy;
                const float zw = b.pos.z + b.vel.z * t + 0.5f * arena::kGravityZ * t * t;
                const float xw = b.pos.x + b.vel.x * t;
                const bool backboard =
                    zw >= arena::kGoalHeight + 0.5f * b.radius || std::fabs(xw) > arena::kGoalHalfWidth + b.radius;
                armed = t > 0.f && t < 2.5f && backboard;
            }
            state_ = armed ? State::AwaitBounce : State::Idle;
        }
        break;
    case State::AwaitBounce:
        if (cur.touchNew) {
            state_ = State::Idle;  // re-touched before the wall: air dribble, not a double tap
            break;
        }
        if (prev.s.ball.vel.y * sgn > 0.f && b.vel.y * sgn <= 0.f &&
            b.pos.y * sgn >= arena::kBackWallY - b.radius - 80.f) {
            state_ = State::AwaitSecond;
            tBounce_ = cur.t;
            bounceZ_ = b.pos.z;
            carDistAtBounce_ = distance(b.pos, c.pos);
        } else if (cur.t - tFirst_ > 2.5f) {
            state_ = State::Idle;
        }
        break;
    case State::AwaitSecond:
        if (cur.touchNew) {
            state_ = State::Eval;
            evalAt_ = cur.frame + 2;
            tSecond_ = cur.t;
        } else if (cur.grounded && wasAirborne_ && cur.t - tBounce_ > 0.05f) {
            out = finish(cur, "DT_LANDED");
        } else if (b.pos.z <= b.radius + 15.f || cur.t - tBounce_ > 2.5f) {
            out = finish(cur, "DT_NO_SECOND_TOUCH");
        }
        break;
    case State::Eval:
        if (cur.frame >= evalAt_) out = finish(cur, nullptr);
        break;
    }
    return out;
}

MechGrade DoubleTapDetector::finish(const Frame& cur, const char* failCode) {
    const BallState& b = cur.s.ball;
    MechGrade g;
    g.family = Family::DoubleTap;
    g.variant = readMode_ ? "read" : "backboard";
    g.tStart = tFirst_;
    g.tEnd = cur.t;
    const bool second = failCode == nullptr;
    const GoalProjection proj = second ? projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena)
                                       : GoalProjection{};
    const float speed = second ? length(b.vel) : 0.f;
    const float bounceToSecond = second ? tSecond_ - tBounce_ : -1.f;
    g.metrics.push_back(makeMetric("first_to_backboard", "Touche - mur", tBounce_ - tFirst_, "s"));
    g.metrics.push_back(makeMetric("backboard_height", "Hauteur sur le mur", bounceZ_, "uu", 1100.f));
    g.metrics.push_back(makeMetric("car_dist_at_bounce", "Distance au rebond", carDistAtBounce_, "uu", 600.f));
    if (second) {
        g.metrics.push_back(makeMetric("bounce_to_second", "Rebond - 2e touche", bounceToSecond, "s"));
        g.metrics.push_back(makeMetric("ball_speed_after", "Vitesse de la balle", speed, "uu/s", 2000.f));
        if (proj.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj.onTarget ? 1.f : 0.f, "", 1.f));
        g.hasShot = true;
        g.ballSpeed = speed;
        g.projection = proj;
    }
    if (failCode) g.errors.push_back(failCode);
    if (carDistAtBounce_ > 1200.f) g.errors.push_back("DT_LATE_FOLLOW");
    if (!readMode_ && bounceZ_ < arena::kGoalHeight + 100.f) g.errors.push_back("DT_LOW_BACKBOARD");
    if (second && proj.valid && !proj.onTarget) g.errors.push_back("DT_OFF_TARGET");
    g.success = second && (!proj.valid || proj.onTarget);
    if (!second && carDistAtBounce_ > 1500.f) {
        g.weak = true;
        g.drillOnly = true;
    }
    const float first = readMode_ ? 1.f : scoreBandF(bounceZ_, 750.f, 1600.f, 300.f);
    const float read = score01(carDistAtBounce_, 1500.f, 600.f);
    const float timing = second ? scoreBandF(bounceToSecond, 0.15f, 0.8f, 1.2f) : 0.f;
    const float acc = second ? accuracyScore(proj) : 0.f;
    const float power = second ? score01(speed, 800.f, 2200.f) : 0.f;
    g.score = clamp01(0.15f * first + 0.25f * read + 0.20f * timing + 0.20f * acc + 0.20f * power);
    reset();
    return g;
}

} // namespace rlt::bm
