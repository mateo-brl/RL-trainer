#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Air dribble (research section 2.8):
//   A session starts at a player touch. While the car is still on the ground or a wall and no air touch happened,
//   each new touch restarts it (that touch is the "pop"). Air touch = car without arena contact, ball z >= 300.
//   End: goal, ball back on the ground (z <= 110), car lands after flying, or 1.5 s without a touch.
//   Graded when >= 2 touches with >= 1 air touch (weak and drill-only below 2 air touches);
//   success = >= 3 touches, >= 2 air touches, >= 0.65 s (subtr-actor AIR_DRIBBLE_MIN_*).
//   Continuous contact counts as one touch every 0.15 s (the game reports a hit on every tick of contact).
//   Control statistics are frozen at the last touch so the post-shot flight does not count.

namespace {
constexpr float kCountedTouchSpacing = 0.15f;
constexpr float kCarryMedianInterval = 0.17f;  // UNVERIFIED carry vs bump split (research: 0.12 s at 30 Hz)
} // namespace

void AirDribbleDetector::reset() { *this = AirDribbleDetector(); }

bool AirDribbleDetector::engaged(float& since) const {
    if (!active_ || touches_ < 2 || airTouches_ < 1) return false;
    since = tFirst_;
    return true;
}

void AirDribbleDetector::startSession(const Frame& cur, bool carAir) {
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    active_ = true;
    tFirst_ = tLast_ = tLastCounted_ = cur.t;
    touches_ = 1;
    airTouches_ = (carAir && b.pos.z >= 300.f) ? 1 : 0;
    const bool wallGeo = cur.standardArena && c.pos.z >= 120.f &&
                         (std::fabs(c.pos.x) >= 3200.f || std::fabs(c.pos.y) >= 4600.f);
    wallOrigin_ = c.onWall || wallGeo;
    distSum_ = maxDist_ = distSumAtLast_ = maxDistAtLast_ = 0.f;
    distN_ = behindTicks_ = airTicks_ = distNAtLast_ = behindAtLast_ = airTicksAtLast_ = 0;
    boostStart_ = minBoost_ = c.boost;
    startZ_ = maxZ_ = b.pos.z;
    travel_ = travelAtLast_ = 0.f;
    lastBallPos_ = b.pos;
    intervalCount_ = 0;
    wasAirborne_ = carAir;
    evalAt_ = 0;
    lastOnTarget_ = false;
    lastOnTargetValid_ = false;
    lastProj_ = GoalProjection{};
}

std::optional<MechGrade> AirDribbleDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)prev;
    (void)p;
    std::optional<MechGrade> out;
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const bool carAir = cur.worldWheels == 0;
    const bool counted = cur.touched && (cur.touchNew || cur.t - tLastCounted_ >= kCountedTouchSpacing);
    if (!active_) {
        if (counted) startSession(cur, carAir);
        return out;
    }
    if (carAir) {
        wasAirborne_ = true;
        ++airTicks_;
    }
    const float d = distance(b.pos, c.pos);
    distSum_ += d;
    ++distN_;
    maxDist_ = std::max(maxDist_, d);
    maxZ_ = std::max(maxZ_, b.pos.z);
    minBoost_ = std::min(minBoost_, c.boost);
    travel_ += distance(b.pos, lastBallPos_);
    lastBallPos_ = b.pos;
    if (carAir && c.basis.toLocal(b.pos - c.pos).x < -40.f) ++behindTicks_;

    if (counted && airTouches_ == 0 && !carAir) {
        startSession(cur, carAir);  // still a ground / wall dribble: restart at the latest pop
        return out;
    }
    if (counted) {
        if (intervalCount_ < static_cast<int>(intervals_.size())) intervals_[intervalCount_++] = cur.t - tLastCounted_;
        ++touches_;
        tLast_ = tLastCounted_ = cur.t;
        evalAt_ = cur.frame + 2;
        if (carAir && b.pos.z >= 300.f) ++airTouches_;
    }
    if (cur.touched) {
        tLast_ = cur.t;  // continued contact keeps the session alive
        distSumAtLast_ = distSum_;
        distNAtLast_ = distN_;
        maxDistAtLast_ = maxDist_;
        behindAtLast_ = behindTicks_;
        airTicksAtLast_ = airTicks_;
        travelAtLast_ = travel_;
    }
    if (evalAt_ && cur.frame == evalAt_) {
        lastProj_ = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
        lastOnTarget_ = lastProj_.onTarget;
        lastOnTargetValid_ = lastProj_.valid;
    }
    const bool crossed = cur.standardArena && std::fabs(b.pos.y) > arena::kBackWallY + b.radius &&
                         b.pos.y * cur.sign > 0.f;
    const bool goal = crossed || (cur.goalEvent && b.pos.y * cur.sign > 0.f);
    if (goal) out = finish(cur, "goal");
    else if (b.pos.z <= 110.f) out = finish(cur, "ball_dropped");
    else if (wasAirborne_ && cur.grounded && cur.t - tFirst_ > 0.3f) out = finish(cur, "landed");
    else if (cur.t - tLast_ > 1.5f) out = finish(cur, "lost");
    return out;
}

std::optional<MechGrade> AirDribbleDetector::finish(const Frame& cur, const char* reason) {
    active_ = false;
    if (touches_ < 2 || airTouches_ < 1) return std::nullopt;  // not an attempt
    const float duration = tLast_ - tFirst_;
    const float avgDist = distNAtLast_ ? distSumAtLast_ / static_cast<float>(distNAtLast_) : 0.f;
    float median = 1.f;
    if (intervalCount_ > 0) {
        std::array<float, 64> iv = intervals_;
        std::sort(iv.begin(), iv.begin() + intervalCount_);
        median = iv[static_cast<size_t>(intervalCount_ / 2)];
    }
    const bool carry = median <= kCarryMedianInterval && avgDist <= 200.f;
    const std::string r = reason;
    const bool isGoal = r == "goal";
    const bool finished = isGoal || lastOnTarget_;
    const float airTime = static_cast<float>(airTicksAtLast_) * kTickDt;

    MechGrade g;
    g.family = Family::AirDribble;
    g.variant = std::string(wallOrigin_ ? "wall_to_air" : "ground_to_air") + (carry ? "_carry" : "_bump");
    g.tStart = tFirst_;
    g.tEnd = cur.t;
    g.weak = airTouches_ < 2;
    g.drillOnly = g.weak;  // a pop plus one air touch is reported inside the air dribble drill only
    g.metrics.push_back(makeMetric("duration", "Durée", duration, "s", 2.f));
    g.metrics.push_back(makeMetric("touches", "Touches", static_cast<float>(touches_), "", 6.f));
    g.metrics.push_back(makeMetric("air_touches", "Touches en l'air", static_cast<float>(airTouches_), ""));
    g.metrics.push_back(makeMetric("air_time", "Temps en l'air", airTime, "s"));
    g.metrics.push_back(makeMetric("avg_dist", "Distance moyenne", avgDist, "uu", 170.f));
    g.metrics.push_back(makeMetric("max_dist", "Distance max", maxDistAtLast_, "uu"));
    g.metrics.push_back(makeMetric("ball_travel", "Distance parcourue", travelAtLast_, "uu"));
    g.metrics.push_back(makeMetric("median_touch_interval", "Intervalle médian", median, "s"));
    g.metrics.push_back(makeMetric("boost_used", "Boost utilisé", boostStart_ - minBoost_, ""));
    g.metrics.push_back(makeMetric("height_gain", "Hauteur gagnée", maxZ_ - startZ_, "uu"));
    g.metrics.push_back(makeMetric("goal", "But", isGoal ? 1.f : 0.f, ""));
    if (lastOnTargetValid_ || isGoal)
        g.metrics.push_back(makeMetric("finished_on_target", "Fin cadrée", finished ? 1.f : 0.f, "", 1.f));
    if (lastProj_.valid || lastProj_.pointCount > 0) {
        g.projection = lastProj_;
        g.hasShot = lastOnTargetValid_;
        g.ballSpeed = 0.f;
    }
    g.success = touches_ >= 3 && airTouches_ >= 2 && duration >= 0.65f;
    if (duration < 0.65f || touches_ < 3) g.errors.push_back("AD_SHORT");
    if (avgDist > 300.f || maxDistAtLast_ > 600.f) g.errors.push_back("AD_BALL_TOO_FAR");
    if (distNAtLast_ && static_cast<float>(behindAtLast_) / static_cast<float>(distNAtLast_) > 0.25f)
        g.errors.push_back("AD_UNDER_BALL");
    if (minBoost_ <= 0.5f && !isGoal) g.errors.push_back("AD_BOOST_OUT");
    if (!finished && (lastOnTargetValid_ || cur.standardArena)) g.errors.push_back("AD_NO_FINISH");
    const float dur = score01(duration, 0.65f, 3.0f);
    const float ctrl = score01(avgDist, 400.f, 150.f);
    const float tq = score01(static_cast<float>(touches_), 2.f, 8.f);
    const float fin = !cur.standardArena ? 0.6f : (isGoal ? 1.f : (lastOnTarget_ ? 0.8f : 0.f));
    g.score = clamp01(0.35f * dur + 0.25f * ctrl + 0.15f * tq + 0.25f * fin);
    return g;
}

} // namespace rlt::bm
