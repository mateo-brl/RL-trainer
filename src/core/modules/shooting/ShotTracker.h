#pragma once
// Follows a shot after a touch until the ball reaches the attacked goal plane (or clearly does not).
//
// The hit point is measured when the ball centre crosses a "front" plane 120 uu before the goal plane (the ball
// surface is then about to touch the goal plane or the back wall), continued ballistically to the goal plane.
// That gives one consistent point for goals AND for shots into the back wall, post or crossbar (a ball centre
// outside the goal mouth never reaches the goal plane itself). While the ball travels, the predicted crossing
// (predictToPlaneY, with floor bounces) is kept up to date for the 3D marker and the early feedback.

#include <cstdint>
#include <optional>

#include "core/common/Snapshot.h"
#include "core/modules/shooting/ShootGeometry.h"

namespace rlt::shoot {

enum class ShotEnd : int { None = 0, Crossed, Goal, Short, Timeout, Aborted };

struct ShotOutcome {
    ShotEnd end = ShotEnd::None;
    bool reached = false; // the ball reached the goal plane (crossing or goal event)
    Vec2 hit; // (u, z) on the goal plane
    Vec3 hitWorld;
    float shotSpeed = 0.f; // uu/s right after the touch that started the shot
    float flightTime = 0.f; // s since that touch
    bool hadEarlyPrediction = false;
    Vec2 earlyPrediction; // first prediction made after the touch (accuracy of the early feedback)
};

class ShotTracker {
public:
    static constexpr float kFrontOffset = kBallRadius + 30.f; // front plane distance from the goal plane
    static constexpr float kMaxFlight = 6.f; // s
    static constexpr int kAwayTicks = 60; // 0.5 s moving away from the goal

    void reset();
    // Starts (or restarts after another touch) following the ball towards `goal`.
    void start(const BallState& ball, const GoalFrame& goal, int64_t frame, float shotSpeed);
    bool active() const { return active_; }
    const GoalFrame& goal() const { return goal_; }
    int64_t startFrame() const { return start_; }

    // Newest snapshot. A ball near the car (still dribbled) is never declared "short".
    std::optional<ShotOutcome> update(const Snapshot& s);
    // Goal event from the game (ball exploded): resolves with the latest prediction.
    std::optional<ShotOutcome> onGoalEvent(int64_t frame);

    bool hasPrediction() const { return active_ && hasPred_; }
    Vec2 predicted() const { return pred_; }
    Vec3 predictedWorld() const { return predWorld_; }
    float predictedTime() const { return predTime_; }

private:
    ShotOutcome finish(ShotEnd end, bool reached, const Vec3& hitWorld, int64_t frame);

    bool active_ = false;
    GoalFrame goal_;
    int64_t start_ = 0;
    float speed_ = 0.f;
    bool hasPrev_ = false;
    Vec3 prevPos_;
    int64_t prevFrame_ = 0;
    int awayTicks_ = 0;
    bool hasPred_ = false;
    Vec2 pred_;
    Vec3 predWorld_;
    float predTime_ = 0.f;
    bool hasFirstPred_ = false;
    Vec2 firstPred_;
};

} // namespace rlt::shoot
