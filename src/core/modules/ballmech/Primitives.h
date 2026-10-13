#pragma once
// Detection primitives shared by every ball-mechanic detector: arena geometry, wheel-contact attribution
// (arena vs ball), ball-on-roof geometry, goal projection with the attack direction, score helpers and the
// per-tick `Frame` the detectors consume.
//
// Conventions: Unreal units, Z up, blue (team 0) attacks +Y, orange (team 1) attacks -Y.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Grade.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt::bm {

// ---- Game constants (RocketSim RLConst, rlgym game values) ----------------------------------------------
namespace arena {
constexpr float kGravityZ = -650.f;
constexpr float kCeilingZ = 2044.f;
constexpr float kSideWallX = 4096.f;
constexpr float kBackWallY = 5120.f;
constexpr float kGoalHalfWidth = 892.755f;
constexpr float kGoalHeight = 642.775f;
constexpr float kCornerSum = 8064.f;       // |x| + |y| of the 45 degree corner planes
constexpr float kCarRestZ = 17.f;
constexpr float kSupersonic = 2200.f;
constexpr float kJumpImpulse = 291.667f;   // JUMP_IMMEDIATE_FORCE, also the double jump impulse
constexpr float kBallDrag = 0.0305f;       // per second
constexpr float kBallRestitution = 0.6f;
} // namespace arena

inline float attackSign(int team) { return team == 1 ? -1.f : 1.f; }
inline float stickMagnitude(const Inputs& in) {
    return std::fabs(in.pitch) + std::fabs(in.yaw) + std::fabs(in.roll);
}

// ---- Surfaces ----------------------------------------------------------------------------------------------
enum class Surface : int { None = 0, Floor, Ceiling, SideWall, BackWall, Corner };

struct SurfaceHit {
    float gap = 1e9f;             // distance from the sphere surface to the arena surface
    Vec3 normal{0.f, 0.f, 1.f};   // arena surface normal, pointing into the field
    Surface kind = Surface::None;
};

// Nearest arena surface to a sphere of radius `r` at `p`. The back wall inside the goal mouth is excluded.
SurfaceHit nearestSurface(const Vec3& p, float r);

// ---- Wheel contacts ---------------------------------------------------------------------------------------
// Snapshot::wheelContacts counts wheels touching anything (arena, ball, cars). The ball takes them when it sits
// against the underside of the car (that is how a flip reset looks), or, on a standard arena, when the car is too
// far from every surface to touch the arena while the ball is within reach (grazing wheel contact). Otherwise the
// contacts belong to the arena.
bool ballUnderWheels(const Snapshot& s);
int ballWheelContacts(const Snapshot& s, bool standardArena = true);
int worldWheelContacts(const Snapshot& s, bool standardArena = true);

// Unit vector from the hitbox centre to the ball, and the cosine between it and the car underside (-up).
float undersideAlignment(const CarState& c, const BallState& b);

// ---- Ball on the roof (carry) --------------------------------------------------------------------------------
// Height of the ball centre above the car origin when it rests on the roof (Octane: 20.75 + 18.08 + 91.25 = 130).
float roofBallHeight(const CarState& c, float ballRadius);
// Ball inside the carry box above the roof (subtr-actor FLICK_* box scaled to the hitbox), relative speed <= 300,
// car roughly upright (roof up).
bool carryGeometry(const Snapshot& s, Vec3* localOut = nullptr);

// ---- Goal projection ----------------------------------------------------------------------------------------
struct GoalProjection {
    bool valid = false;        // false on non standard arenas (custom maps): accuracy is then unknown
    bool reaches = false;      // crosses the opponent goal line within the horizon
    bool onTarget = false;     // inside the goal frame (with a margin of the ball radius)
    float x = 0.f, z = 0.f, t = 0.f;
    float angleErrDeg = 180.f; // horizontal angle between the ball velocity and the goal centre
    int pointCount = 0;
    std::array<Vec3, 24> points{};  // sampled path (for drawing)
};

// Ball path toward the opponent goal of `team`: drag, floor/ceiling/side wall bounces, up to `horizon` seconds.
GoalProjection projectToGoal(const Vec3& p, const Vec3& v, int team, float r, bool standardArena,
                             float horizon = 4.f);
// 0..1: 1 at 5 deg of horizontal error, 0 at 30 deg, at least 0.8 when on target. 0.7 when unknown.
float accuracyScore(const GoalProjection& g);

// ---- Score helpers (reference formulas) ---------------------------------------------------------------------
inline float score01(float value, float bad, float good) {
    if (good == bad) return value >= good ? 1.f : 0.f;
    return clamp01((value - bad) / (good - bad));
}
inline float scoreBandF(float v, float lo, float hi, float tol) { return bandScore(v, lo, hi, tol); }

Metric makeMetric(const char* key, const char* label, float value, const char* unit);
Metric makeMetric(const char* key, const char* label, float value, const char* unit, float ideal);

// ---- Per-tick frame ---------------------------------------------------------------------------------------
// One physics tick as the detectors see it: the snapshot plus everything derived once (contacts, touch, dodge,
// game events). Built by FrameBuilder, consumed 2 ticks after the fact (touches are resolved with that delay).
struct Frame {
    Snapshot s;
    float t = 0.f;
    int64_t frame = 0;
    int team = 0;
    float sign = 1.f;              // attack direction along Y
    bool standardArena = true;
    int ballWheels = 0;
    int worldWheels = 0;
    bool grounded = false;         // >= 3 wheels on the arena (floor, walls, ceiling)
    bool freeFlight = false;       // no arena contact and fewer than 3 wheels on anything
    bool touched = false;          // the local car touched the ball during this tick
    bool touchNew = false;         // first tick of a new contact (not a continued push)
    bool hasHit = false;
    Vec3 hitLocation;
    Vec3 hitNormal;
    bool jumpEdge = false;         // jump button pressed this tick
    bool dodgeStart = false;       // a dodge (flip) started this tick
    Vec3 dodgeDir;                 // car-local (x forward, y right), normalised; valid with dodgeStart
    bool doubleJumpStart = false;  // airborne jump press without stick while the flip was available
    bool sdkReset = false;         // the game reported a flip reset (EventPerformedFlipReset) at this tick
    bool goalEvent = false;        // the game reported a goal around this tick
};

struct TouchMark {
    int64_t frame = 0;
    bool newContact = true;
    bool hasHit = false;
    Vec3 hitLocation;
    Vec3 hitNormal;
};

struct BuildOptions {
    bool standardArena = true;
    float dodgeDeadzone = 0.5f;
};

// Derives Frames from consecutive snapshots. Keeps the dodge refractory state (a jump press and the
// `dodging` flag rising one tick later are the same dodge).
class FrameBuilder {
public:
    void reset() { lastDodgeT_ = -1e9f; }
    // `prev`: previous snapshot (nullptr when unknown). `next1`/`next2`: the following snapshots when already known
    // (look-ahead for the dodge direction reported by the game one tick after the input).
    Frame build(const Snapshot& s, const Snapshot* prev, const Snapshot* next1, const Snapshot* next2,
                const TouchMark* touch, bool sdkReset, bool goalEvent, const BuildOptions& opt);

private:
    float lastDodgeT_ = -1e9f;
};

// Dodge direction from the inputs (game formula): (DodgeForward, DodgeStrafe), else (-pitch, yaw + roll).
Vec3 dodgeDirectionFromInputs(const Inputs& in);

} // namespace rlt::bm
