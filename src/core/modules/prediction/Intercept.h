#pragma once
// Intercept advisor: earliest time t at which the local car can reach the predicted ball position p(t).
//
// Kinematic reach model (deliberately simple, explainable, cheap):
//   - the car drives in a straight line on the ground towards p(t), starting with its current speed component
//     along that line, throttle curve up to 1410 uu/s, plus ground boost acceleration (991.7 uu/s^2) up to
//     2300 uu/s while boost lasts (33.3 boost per second);
//   - it needs to get within `reach` uu (horizontally) of the ball centre;
//   - turning costs `turnPenalty` seconds per half turn between the car heading and the target direction;
//   - when the ball centre is above `groundReachZ`, the car must take off early enough to rise to it (single
//     jump, double jump, or aerial when boost is available); it keeps its take-off speed horizontally in the air.

#include <vector>

#include "core/modules/prediction/BallPath.h"

namespace rlt::pr {

namespace car {
constexpr float kMaxThrottleSpeed = 1410.f;
constexpr float kMaxSpeed = 2300.f;
constexpr float kThrottleAccelZero = 1600.f;     // at 0 uu/s
constexpr float kThrottleAccel1400 = 160.f;      // at 1400 uu/s, 0 at 1410
constexpr float kBoostAccelGround = 2975.f / 3.f;
constexpr float kBoostPerSecond = 100.f / 3.f;
constexpr float kJumpImpulse = 875.f / 3.f;
constexpr float kJumpHoldAccel = 4375.f / 3.f;
constexpr float kJumpHoldTime = 0.2f;
constexpr float kSecondJumpDelay = 0.05f;        // release and press jump again
constexpr float kAerialBoostAccel = 3175.f / 3.f;
constexpr float kSingleJumpMaxRise = 225.f;      // theoretical apex 232.6 uu
constexpr float kDoubleJumpMaxRise = 480.f;      // apex 486.9 uu with the 0.05 s release
constexpr float kAerialMaxRise = 1900.f;
} // namespace car

enum class JumpKind : int { None = 0, Jump = 1, DoubleJump = 2, Aerial = 3 };

struct InterceptParams {
    float reach = 150.f;          // horizontal car centre -> ball centre distance at contact
    float groundReachZ = 170.f;   // highest ball centre the car touches without jumping
    float turnPenalty = 0.6f;     // seconds per half turn
    bool useBoost = true;
    float aerialMinBoost = 20.f;  // boost kept for an aerial
    float minTime = 0.f;          // ignore path samples earlier than this (seconds after the path start)
};

struct InterceptCar {
    Vec3 pos;
    Vec3 vel;
    Vec3 forward{1.f, 0.f, 0.f};
    float boost = 0.f;  // 0..100
};

struct InterceptResult {
    bool found = false;
    int index = -1;            // sample index in the path
    float t = 0.f;             // seconds after the path start
    Vec3 ballPos;
    Vec3 ballVel;
    float ballSpeed2D = 0.f;   // horizontal ball speed at t (speed to match for a soft first touch)
    float carSpeed = 0.f;      // estimated car speed on arrival (full throttle, boost if allowed)
    float distance = 0.f;      // horizontal distance to cover (reach removed)
    float turnPenalty = 0.f;   // seconds
    JumpKind jump = JumpKind::None;
    float jumpTime = 0.f;      // seconds between take-off and contact
};

class CarReachModel {
public:
    CarReachModel();

    // Throttle-only acceleration at `speed` (uu/s^2).
    static float throttleAccel(float speed);
    // Distance driven in `seconds` from speed v0 along a straight line, boosting for `boostSeconds` first.
    float distance(float v0, float seconds, float boostSeconds, float* endSpeed = nullptr) const;
    // Time for the car's contact point to rise by `rise` uu with the simplest technique able to do it.
    // Returns false when it cannot (too high, or aerial needed without boost).
    static bool jumpTime(float rise, bool canAerial, float& seconds, JumpKind& kind);

private:
    struct Profile {
        std::vector<float> v;  // speed at k * dt from rest
        std::vector<float> x;  // distance at k * dt from rest
        float vTerminal = 0.f;
    };
    static Profile build(bool boost);
    static void advance(const Profile& p, float v0, float seconds, float& dist, float& vEnd);

    Profile boost_;
    Profile noBoost_;
};

// Earliest reachable sample of `path` for `carState`. Samples past a goal line are ignored.
InterceptResult findIntercept(const PredictedPath& path, const InterceptCar& carState, const InterceptParams& prm,
                              const CarReachModel& model);

} // namespace rlt::pr
