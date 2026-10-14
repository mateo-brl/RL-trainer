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

// ---- Catch ----------------------------------------------------------------------------------------------------
// A ball that lands on a still roof hops back up at about kCatchBounce of its impact speed, with a small forward kick
// (RocketSim car-ball "extra impulse"; UNVERIFIED in game, like the suspension damping). It only rests once a hop
// stays under the roof tolerance (40 uu: launch speed under kCatchSettleSpeed).
constexpr float kCatchBounce = 0.65f;
constexpr float kCatchSettleSpeed = 228.f;  // sqrt(2 * 650 * 40)
constexpr float kCatchRoofZ = 148.f;        // ball centre on the roof of a car resting on the floor (Octane)
// Tiny hops still count as "on the roof" for the catch: ball centre at most kCatchLooseTolUp above its resting
// height (and above the roof), and a break of at most kCatchHopGrace seconds does not restart the hold.
constexpr float kCatchLooseTolUp = 70.f;
constexpr float kCatchHopGrace = 0.15f;
// Time margin added to every catch limit (after the fall, the hops and the hold).
constexpr float kCatchMargin = 1.f;

// Vertical speed of the ball when it reaches roof height (no drag) from `ballPos` moving at `ballVel`.
float catchImpactSpeed(const Vec3& ballPos, const Vec3& ballVel);
// Seconds of hops on a still roof after an impact at `impactSpeed`: sum of 2 v / g over the hops v = impact * e^k
// (k >= 1) while v >= settleSpeed.
float catchSettleTime(float impactSpeed, float e = kCatchBounce, float settleSpeed = kCatchSettleSpeed);
int catchHopCount(float impactSpeed, float e = kCatchBounce, float settleSpeed = kCatchSettleSpeed);

// Catch drill spawn, deterministic from a seed. The ball either falls from the sky or comes off a side wall.
struct CatchPlan {
    Vec3 carPos;
    float carYawRad = 0.f;
    Vec3 ballPos;
    Vec3 ballVel;
    Vec3 landing;          // where the ball centre reaches roof height (no drag, no bounce)
    float fallTime = 0.f;  // seconds until it reaches roof height
    float apex = 0.f;      // highest ball centre height of the flight
    float impactSpeed = 0.f;  // vertical speed at roof height
    float settleTime = 0.f;   // hops expected on a still roof (catchSettleTime)
    float timeLimit = 0.f; // seconds to have held it: fall + hops + hold + kCatchMargin
    bool fromWall = false;
};
// variant: 0 mix, 1 drop from the sky, 2 off a side wall. d: difficulty 0..1. catchHold: seconds the ball must stay on
// the roof (part of the time limit).
// Niveau 1 (d 0): a ball tossed a little up (apex 350..450, one or two small hops) landing 180..300 uu in front of
// the car, or off the wall from 450..500 high; the toss gives 1.5 s to get under it with a 0.4 s reaction.
CatchPlan planCatch(uint64_t seed, double d, int variant, float catchHold = 1.f);

// Ballistic prediction with the game's ball drag (no bounces): first point where the ball centre goes down
// through z = planeZ. Returns false if it never does within maxT seconds.
bool predictDownCrossing(Vec3 p, Vec3 v, float planeZ, float maxT, Vec3& out, float& tOut);

} // namespace rlt::courses
