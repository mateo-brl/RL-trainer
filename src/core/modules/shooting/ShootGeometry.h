#pragma once
// Geometry shared by the shooting module: field and goal constants, the attacked goal frame, target zones
// on the goal plane, ball-on-roof helpers and a light ball predictor (drag, gravity, floor bounces).
//
// Conventions (Rocket League / Unreal): Z up, blue (team 0) attacks +Y, orange (team 1) attacks -Y.
// On the goal plane we use 2D coordinates (u, z): u is the lateral offset towards the ATTACKER's right
// (facing the goal), z the height. "Left" zones therefore have u < 0. Zone rectangles are expressed for the
// BALL CENTRE: a shot is "in the zone" when the ball centre crosses the goal plane inside the rectangle.

#include <optional>
#include <string>

#include "core/common/DrawList.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt::shoot {

// ---- Constants (RocketSim RLConst, rlgym game values; see research physics.md) -------------------------
constexpr float kGoalHalfWidth = 892.755f; // goal centre to post
constexpr float kGoalHeight = 642.775f; // crossbar
constexpr float kGoalLineY = 5120.f; // goal plane |y| (back wall plane)
constexpr float kBallRadius = 91.25f; // collision radius vs cars
constexpr float kBallRestZ = 93.15f; // ball centre height when resting (arena contact radius)
constexpr float kGravity = 650.f; // uu/s^2, downwards
constexpr float kBallDrag = 0.03f; // v *= 1 - drag * dt per tick
constexpr float kBallMaxSpeed = 6000.f;
constexpr float kBallMaxSpin = 6.f; // rad/s
constexpr float kBallRestitution = 0.6f; // ball vs arena
constexpr float kBallFriction = 0.35f; // ball vs arena (Coulomb)
constexpr float kArenaHalfX = 4096.f;
constexpr float kArenaHalfY = 5120.f;
constexpr float kCeilingZ = 2044.f;
constexpr float kCarRestZ = 17.f; // car origin height when resting on the floor
constexpr float kSupersonic = 2200.f;
// Ball-centre limits of a shot that "probably goes in" (centre crossing the goal plane): the ball may touch
// the post or the crossbar but deflects inside (same margins as the ball mechanics research).
constexpr float kOnTargetHalfU = kGoalHalfWidth - 0.4f * kBallRadius; // 856.26
constexpr float kOnTargetTopZ = kGoalHeight - 0.3f * kBallRadius; // 615.40
constexpr float kZoneFloorZ = 60.f; // bottom edge of the low zones (below a rolling ball centre)

inline float attackSign(int team) { return team == 1 ? -1.f : 1.f; }

// Frame of the goal attacked by a team: plane y = sign * 5120, lateral axis = attacker's right.
struct GoalFrame {
    float sign = 1.f;

    static GoalFrame forTeam(int team) { return GoalFrame{attackSign(team)}; }
    float planeY() const { return sign * kGoalLineY; }
    Vec3 right() const { return {-sign, 0.f, 0.f}; } // attacker's right in world space
    Vec3 normal() const { return {0.f, sign, 0.f}; } // from the field into the goal
    // World point -> (u, z) on the goal plane (the y coordinate is ignored).
    Vec2 toPlane(const Vec3& p) const { return {-sign * p.x, p.z}; }
    // (u, z) -> world point on the plane, `inset` uu towards the field (positive = in front of the plane).
    Vec3 toWorld(float u, float z, float inset = 0.f) const { return {-sign * u, sign * (kGoalLineY - inset), z}; }
    // Distance of a world point in front of the goal plane (> 0 on the field side).
    float distanceToPlane(const Vec3& p) const { return kGoalLineY - sign * p.y; }
};

// ---- Target zones -----------------------------------------------------------------------------------------
enum class Zone : int { TopLeft = 0, TopRight = 1, LowLeft = 2, LowRight = 3, Center = 4, Count = 5 };
constexpr int kZoneCount = static_cast<int>(Zone::Count);
constexpr int kZoneRandom = kZoneCount; // selection index meaning "random zone each attempt"

const char* zoneId(Zone z); // "tl", "tr", "bl", "br", "center" (stable ids for skills, commands, saves)
const char* zoneLabel(Zone z); // French: "lucarne gauche", ...
// Parses a command argument: tl/tr/bl/br/center (and French aliases), "random"/"aleatoire" -> kZoneRandom.
// Returns false when unknown.
bool parseZone(const std::string& text, int& out);

// Axis-aligned rectangle on the goal plane (u, z), centre and half sizes, for the ball centre.
struct ZoneRect {
    float u = 0.f, z = 0.f, hw = 0.f, hh = 0.f;

    bool contains(const Vec2& p) const { return std::fabs(p.x - u) <= hw && std::fabs(p.y - z) <= hh; }
    // 0 at the centre, 1 on the border (Chebyshev distance normalised by the half sizes), > 1 outside.
    float normDistance(const Vec2& p) const;
    // Euclidean distance from the rectangle (0 inside).
    float edgeDistance(const Vec2& p) const;
    float centreDistance(const Vec2& p) const;
};

// Zone rectangle for a difficulty level in [0, 1]: the zones shrink into their corner as the level rises.
ZoneRect zoneRect(Zone z, float level);
// The whole goal mouth (ball-centre limits of a shot on target).
ZoneRect goalMouthRect();
// True when the ball centre crossing point is inside the goal mouth (likely a goal).
bool onGoal(const Vec2& p);

// Result of comparing a crossing point with a target rectangle.
struct AimResult {
    bool reached = false; // the ball crossed (or is predicted to cross) the goal plane
    bool onGoal = false; // ... inside the goal mouth
    bool inZone = false; // ... inside the target rectangle
    Vec2 hit; // crossing point (u, z)
    float du = 0.f, dz = 0.f; // hit - zone centre (u: + = to the attacker's right, z: + = higher)
    float centreDist = 0.f; // uu from the zone centre
    float edgeDist = 0.f; // uu outside the zone (0 inside)
    float score01 = 0.f; // 1 at the centre, 0.7 on the border, 0.45..0.65 goal outside the zone, < 0.4 miss
};
AimResult scoreAim(const ZoneRect& zone, bool reached, const Vec2& hit);

// ---- Car hitbox and the ball on the roof --------------------------------------------------------------------
struct Hitbox {
    Vec3 extent{118.01f, 84.20f, 36.16f}; // full sizes, car-local (x fwd, y right, z up)
    Vec3 offset{13.88f, 0.f, 20.75f}; // hitbox centre from the car origin, car-local
    float roofZ() const { return offset.z + extent.z * 0.5f; } // roof height above the car origin (local)
};
// Hitbox from the snapshot (falls back to the Octane values when the sizes look invalid).
Hitbox hitboxOf(const CarState& car);
// Centre of the roof top surface in world space.
Vec3 roofCenter(const Vec3& carPos, const Basis& b, const Hitbox& hb);
// Ball centre resting on the roof: car origin + fwd * off.x + right * off.y + up * (roofZ + r + gap).
Vec3 roofSpawn(const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius, float gap);
// Ball centre relative to its resting point on the roof, car-local (x fwd, y right, z up). (0,0,0) = resting.
Vec3 roofOffset(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius);
// Ball resting on (or just above) the roof: local height in [rest - 15, rest + 40] and the centre over the
// roof rectangle expanded by `margin`.
bool ballOnRoof(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius,
                float tolDown = 15.f, float tolUp = 40.f, float margin = 15.f);

// ---- Ball prediction -------------------------------------------------------------------------------------
struct PlaneCrossing {
    Vec3 pos; // ball centre on the plane
    Vec3 vel;
    float time = 0.f; // s from now
    int bounces = 0; // floor / wall / ceiling bounces before the crossing
};
// First crossing of the plane y = planeY by the ball centre, moving from the field side (|y| < |planeY|)
// to the other side. Integrates drag, gravity, floor bounces with spin (Coulomb friction, rolling) and simple
// side wall / ceiling bounces. The back wall is ignored on purpose: the crossing point is the "virtual" hit
// point used for accuracy. Returns nothing when the ball does not reach the plane within maxTime.
std::optional<PlaneCrossing> predictToPlaneY(Vec3 pos, Vec3 vel, Vec3 angVel, float planeY, float maxTime = 4.f,
                                             float radius = kBallRadius);

} // namespace rlt::shoot
