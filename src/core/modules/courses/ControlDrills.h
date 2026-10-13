#pragma once
// Pure helpers of the control drills (roof dribble, slalom, catch): ball-on-roof geometry, the "keeper" that
// decides when a roof dribble is lost, where the ball fell off (for coaching), the seeded catch spawns and a
// ballistic landing prediction for the catch marker.

#include <cstdint>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt::courses {

// Roof height above the car origin (local z): hitbox offset z + half the full hitbox height.
float roofHeight(const Vec3& hitboxOffset, const Vec3& hitboxExtent);
// Ball centre resting on the roof (plus a small gap), world space. The official "ballontop" uses +170 in world Z.
Vec3 roofSpawn(const Vec3& carPos, const Basis& b, const Vec3& hitboxOffset, const Vec3& hitboxExtent,
               float ballRadius, float gap = 2.f);
// Ball resting on (or just above) the roof: local z within [roof + r - 15, roof + r + tolUp] and the ball centre
// above the roof rectangle expanded by `margin`.
bool ballOnRoof(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Vec3& hitboxOffset,
                const Vec3& hitboxExtent, float ballRadius, float tolUp = 40.f, float margin = 15.f);
bool ballOnRoof(const BallState& ball, const CarState& car);
// Ball touching (or about to touch) the floor.
bool ballOnFloor(const BallState& ball);

// Where the ball left the roof, from its velocity relative to the car (car-local axes: x forward, y right, z up).
enum class FallSide : int { None = 0, Front, Back, Side, Up };
FallSide classifyFall(const Vec3& relVelLocal, const Vec3& relPosLocal);

// Roof dribble keeper: the ball may leave the roof for at most `tolerance` seconds (small pops, bounces);
// touching the floor loses it at once.
class RoofKeeper {
public:
    enum class Loss : int { None = 0, OffRoof, Floor };

    void reset(float tolerance) {
        tolerance_ = tolerance;
        lost_ = Loss::None;
        offRoof_ = 0.f;
        heldUntil_ = 0.f;
        onRoofTime_ = 0.f;
        everOnRoof_ = false;
    }
    // t: seconds since the start of the attempt, dt: seconds since the previous update.
    void update(bool onRoof, bool onFloor, float t, float dt);

    bool lost() const { return lost_ != Loss::None; }
    Loss loss() const { return lost_; }
    float heldTime() const { return heldUntil_; }   // time of the last sample with the ball on the roof
    float onRoofTime() const { return onRoofTime_; } // accumulated time actually on the roof
    float offRoofFor() const { return offRoof_; }
    bool everOnRoof() const { return everOnRoof_; }

private:
    float tolerance_ = 0.3f;
    Loss lost_ = Loss::None;
    float offRoof_ = 0.f;
    float heldUntil_ = 0.f;
    float onRoofTime_ = 0.f;
    bool everOnRoof_ = false;
};

// Catch drill spawn, deterministic from a seed. The ball either falls from the sky or comes off a side wall.
struct CatchPlan {
    Vec3 carPos;
    float carYawRad = 0.f;
    Vec3 ballPos;
    Vec3 ballVel;
    Vec3 landing;          // where the ball centre reaches roof height (no drag, no bounce)
    float fallTime = 0.f;  // seconds until it reaches roof height
    float timeLimit = 0.f; // seconds to get it on the roof
    bool fromWall = false;
};
// variant: 0 mix, 1 drop from the sky, 2 off a side wall. d: difficulty 0..1.
CatchPlan planCatch(uint64_t seed, double d, int variant);

// Ballistic prediction with the game's ball drag (no bounces): first point where the ball centre goes down
// through z = planeZ. Returns false if it never does within maxT seconds.
bool predictDownCrossing(Vec3 p, Vec3 v, float planeZ, float maxT, Vec3& out, float& tOut);

} // namespace rlt::courses
