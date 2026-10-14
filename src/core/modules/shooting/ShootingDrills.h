#pragma once
// Start positions of the shooting drills (pure functions of the difficulty level and a seeded Rng).
// A Spawn is held still during the countdown (car and ball re-placed every tick with zero velocity), then
// released with its release velocities.

#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/modules/shooting/ShootGeometry.h"

namespace rlt::shoot {

enum class TargetSetup : int { Mix = 0, Rolling = 1, Bouncing = 2, Pass = 3, Roof = 4, Count = 5 };
constexpr int kTargetSetupCount = static_cast<int>(TargetSetup::Count);

// Adaptive level of a player who never did the drill: the easiest one (niveau 1: closest goal, biggest zones,
// slowest balls, weakest flick accepted).
constexpr float kShootStartLevel = 0.f;

const char* setupId(TargetSetup s); // "mix", "rolling", "bouncing", "pass", "roof"
const char* setupLabel(TargetSetup s); // French: "Mélange", "Balle roulante", ...
bool parseSetup(const std::string& text, int& out);

struct RoofParams {
    Hitbox hb;
    float ballRadius = kBallRadius;
    float gap = 2.f;
};

struct Spawn {
    CarPlacement car; // held pose (zero velocity)
    BallPlacement ball; // held pose (zero velocity)
    Vec3 carReleaseVel;
    Vec3 ballReleaseVel;
    Vec3 ballReleaseAngVel;
    bool ballOnRoof = false;
    GoalFrame goal;
};

// Keeps a point inside the field (walls and corner chamfers) with a margin.
Vec3 clampToField(Vec3 p, float margin);
// Spin of a ball rolling on the floor without slipping (capped at the game's 6 rad/s), for rolling spawns.
Vec3 rollingSpin(const Vec3& vel, float radius = kBallRadius);

// ---- Moving roof start ("Balle sur le toit en mouvement") ---------------------------------------------------------
// Driving speed of a moving roof start: 300 uu/s on the first moving levels (up to 0.1 above `firstMovingLevel`), then
// 400 .. 1100 uu/s.
float movingRoofSpeed(float level, float firstMovingLevel);
constexpr float kRoofMovingFirstLevel = 0.3f;  // Cibles, balle sur le toit: moving starts above this level...
constexpr int kRoofMovingFlickNiveau = 3;      // ...once the stationary flick reached niveau 3
constexpr float kRoofAssistMaxLevel = 0.6f;    // carry assist after a moving release, up to this level
constexpr float kRoofAssistSeconds = 0.6f;     // the ball is put back on the roof this long after the release...
constexpr float kRoofAssistRadius = 40.f;      // ...while it is within this distance of its resting point
constexpr float kEarlyLossReplay = 1.f;        // a ball that leaves the roof this soon after a moving release: replayed

// Ball resting on the roof, car facing the attacked goal at a distance growing with the level (2000 .. 4500 uu).
// `fixedSpeed` < 0: driving speed from the level (movingRoofSpeed) when `moving`.
Spawn flickSpawn(float level, bool moving, float fixedSpeed, const GoalFrame& goal, const RoofParams& roof, Rng& rng);

struct TargetOptions {
    bool roofMovingAllowed = true;  // Balle sur le toit: a moving start may be drawn (stationary flick at niveau 3)
};
// Net targeting setups (Mix is resolved by the caller).
//  - Rolling: the ball rolls on a line that misses the goal mouth by at least 400 uu: the player redirects it.
//  - Bouncing: dropped from 400 uu high at niveau 1 (900 at niveau 10), car 1200 uu behind (2000).
//  - Pass: a cross from 1500 uu on the side at niveau 1 (2600), rolling at 550 uu/s (1300), car at rest.
Spawn targetSpawn(TargetSetup setup, float level, const GoalFrame& goal, const RoofParams& roof, Rng& rng,
                  const TargetOptions& opt = {});
// Power challenge: ball rolling towards the player who faces the attacked goal.
Spawn powerSpawn(float ballSpeed, const GoalFrame& goal, Rng& rng);

// ---- Zones and goals by level -------------------------------------------------------------------------------
// Niveaux 1 to 3 draw only the low and centre zones (the top corners need a lifted shot); the player's own choice of
// zone is always kept.
constexpr int kTopCornersNiveau = 4;
Zone pickTargetZone(int selection, float level, Zone previous, Rng& rng);
// Puissance: speed to reach (km/h): 70 at niveau 1, 90 at mid level, 110 at niveau 10.
float powerGoalKph(float level);
constexpr float kPowerStep = 1.f / 24.f;  // 3 successes = +5 km/h

// ---- Light predictions for the circles drawn on the floor ---------------------------------------------------
// The next floor bounces of a ball (up to `count`), with their times (s from now).
struct FloorBounce {
    Vec3 pos;
    float time = 0.f;
};
std::vector<FloorBounce> predictFloorBounces(Vec3 pos, Vec3 vel, Vec3 angVel, int count, float maxTime = 6.f);
// Ball position `t` seconds from now (drag, gravity, floor bounces with spin).
Vec3 predictBallAt(Vec3 pos, Vec3 vel, Vec3 angVel, float t);
// Where a cross meets a car driving straight along y from `carPos`: the predicted ball point closest to the car's lane
// (x = carPos.x) within 5 s, with its time.
FloorBounce crossMeetingPoint(const Vec3& ballPos, const Vec3& ballVel, const Vec3& ballAngVel, const Vec3& carPos);
// Puissance: where a car starting at rest at `carPos` (boost) meets the ball rolling at it, and the "élan" point
// 1200 uu before it on the car's way.
Vec3 powerMeetingPoint(const Vec3& carPos, const Vec3& ballPos, const Vec3& ballVel);
Vec3 powerRunUpPoint(const Vec3& carPos, const Vec3& meet);

} // namespace rlt::shoot
