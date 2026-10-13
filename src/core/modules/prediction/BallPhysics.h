#pragma once
// Ball simulation used by the prediction module: gravity, air drag, bounces with restitution and spin friction
// against an analytic Soccar arena (floor, side/back walls, 45 deg corners, floor and ceiling ramps, ceiling,
// goal boxes and goal-frame edges) and goal detection. Ported from the project's tested reference predictor
// (research/ballpredict_ref.hpp) to the repo math types. Constants come from published values (RocketSim
// RLConst, RLBot wiki); see the research note physics.md for sources.
//
// Model (one tick, dt = 1/120 s, semi-implicit Euler like Bullet):
//   1) v *= (1 - drag)^dt (= v_pre); v.z += g * dt
//   2) contacts are searched at the look-ahead position p + v * dt. For each approaching contact:
//        vn = v.n (< 0, gravity included); vnPre = (v_pre + g * dt * toi).n
//        bounce = |vnPre| > 10 uu/s;  vn' = bounce ? -0.6 * vnPre : 0;  jn = vn' - vn
//        s = v_t + w x (-R n) (contact-point slip)
//        dv_t = -min((2/7)|s|, mu * jn) * s / |s|  (Coulomb, mu = 0.35, 2/7 = solid sphere)
//        dw = (dv_t x n) / (0.4 R)
//   3) p += v_in * dt * toi + v_out * dt * (1 - toi), then depenetrate (deepest first, 3 iterations)
//   4) |v| <= 6000, |w| <= 6 rad/s
//
// Everything lives in namespace rlt::pr ("pr" is this module's registered prefix) so that no symbol can clash
// with another module linked into the same plugin.

#include <array>
#include <cmath>
#include <cstdint>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt::pr {

namespace phys {
constexpr float kGravityZ = -650.f;            // uu/s^2
constexpr float kBallRadius = 91.25f;          // collision radius vs cars (Soccar)
constexpr float kArenaMargin = 1.9f;           // effective radius vs arena = radius + mesh margin (93.15)
constexpr float kBallArenaRadius = kBallRadius + kArenaMargin;
constexpr float kBallMaxSpeed = 6000.f;
constexpr float kBallMaxAngSpeed = 6.f;        // rad/s
constexpr float kBallDrag = 0.03f;             // v *= (1 - 0.03)^dt
constexpr float kBallRestitution = 0.6f;
constexpr float kBallFriction = 0.35f;
constexpr float kRestitutionThreshold = 10.f;  // uu/s, below: perfectly inelastic contact

constexpr float kArenaX = 4096.f;              // side walls
constexpr float kArenaY = 5120.f;              // back walls (outside the goal mouth)
constexpr float kArenaZ = 2048.f;              // ceiling plane
constexpr float kCornerSum = 8064.f;           // corner planes |x| + |y| = 8064
constexpr float kFilletFloorSide = 256.f;      // floor -> side wall / corner ramp radius
constexpr float kFilletFloorBack = 160.f;      // floor -> back wall ramp radius
constexpr float kFilletCeiling = 512.f;        // any wall -> ceiling ramp radius
constexpr float kGoalHalfWidth = 892.755f;
constexpr float kGoalHeight = 642.775f;
constexpr float kGoalBackY = 6000.f;           // back of the net
constexpr float kGoalScoreY = 5124.25f;        // goal when |y| > 5124.25 + ball radius
} // namespace phys

// Which collision world the prediction uses.
enum class ArenaMode : int {
    Soccar = 0,     // standard Soccar arena with goals (stock maps)
    FloorOnly = 1,  // infinite floor at z = 0, no walls, no goals (most workshop maps)
    None = 2,       // free flight (ring maps, air dribble maps)
};

struct SimParams {
    float gravityZ = phys::kGravityZ;
    float drag = phys::kBallDrag;
    float restitution = phys::kBallRestitution;
    float friction = phys::kBallFriction;
    float radius = phys::kBallRadius;             // inertia / spin lever
    float arenaRadius = phys::kBallArenaRadius;   // contact radius vs the arena
    float maxSpeed = phys::kBallMaxSpeed;
    float maxAngSpeed = phys::kBallMaxAngSpeed;
    float restitutionThreshold = phys::kRestitutionThreshold;

    // Parameters for a ball of the given collision radius (Soccar 91.25; other modes have bigger balls).
    static SimParams forRadius(float ballRadius);
};

struct SimBall {
    Vec3 pos{0.f, 0.f, phys::kBallArenaRadius};
    Vec3 vel;
    Vec3 angVel;
};

// Surface kinds of a contact (bit set).
enum ContactKind : uint8_t { kContactFloor = 1, kContactCeiling = 2, kContactWall = 4, kContactGoal = 8 };

struct Contact {
    Vec3 n;          // unit normal from the surface into the playable volume
    float dist = 0;  // distance from the ball centre to the surface along n
    uint8_t kind = 0;
};

struct ContactList {
    std::array<Contact, 24> c{};
    int count = 0;
    void add(const Vec3& n, float d, uint8_t k) {
        if (count < static_cast<int>(c.size())) c[static_cast<size_t>(count++)] = {n, d, k};
    }
};

// Every arena surface closer than `maxDist` to point `p` (ball centre).
ContactList arenaContacts(const Vec3& p, float maxDist, ArenaMode mode = ArenaMode::Soccar);

// Ball centre past the scoring line of a Soccar goal.
inline bool isGoal(const Vec3& p, float ballRadius = phys::kBallRadius) {
    return std::fabs(p.y) > phys::kGoalScoreY + ballRadius;
}

enum StepFlags : uint8_t {
    kStepContact = 1,   // touching something this tick (rolling counts)
    kStepBounce = 2,    // a contact with normal speed above the restitution threshold
    kStepFloor = 4,
    kStepWall = 8,
    kStepCeiling = 16,
    kStepGoal = 32,     // ball crossed the scoring line
    kStepApex = 64,     // vz went from > 0 to <= 0 this tick (free flight)
};

struct StepResult {
    uint8_t flags = 0;
    Vec3 normal{0.f, 0.f, 1.f};  // normal of the strongest contact (when kStepContact)
    Vec3 point;                  // point of the surface touched by that contact (at the time of impact)
    float impactSpeed = 0.f;     // normal speed at impact of that contact (uu/s, >= 0)
};

// Resolves one contact impulse on (vel, angVel). `vnPre` is the normal velocity at impact without the gravity
// of the rest of the tick. Returns true when it was a real bounce (above the restitution threshold).
bool resolveContact(Vec3& vel, Vec3& angVel, const Vec3& n, float vnPre, const SimParams& prm);

// (1 - drag)^dt, precomputed once per prediction.
float dragFactor(const SimParams& prm, float dt = kTickDt);

// Advances the ball by one tick.
StepResult stepBall(SimBall& b, const SimParams& prm, ArenaMode mode, float dt, float dragMul);
inline StepResult stepBall(SimBall& b, const SimParams& prm = {}, ArenaMode mode = ArenaMode::Soccar) {
    return stepBall(b, prm, mode, kTickDt, dragFactor(prm, kTickDt));
}

} // namespace rlt::pr
