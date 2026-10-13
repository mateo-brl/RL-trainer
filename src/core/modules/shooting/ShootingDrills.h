#pragma once
// Start positions of the shooting drills (pure functions of the difficulty level and a seeded Rng).
// A Spawn is held still during the countdown (car and ball re-placed every tick with zero velocity), then
// released with its release velocities.

#include <string>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/modules/shooting/ShootGeometry.h"

namespace rlt::shoot {

enum class TargetSetup : int { Mix = 0, Rolling = 1, Bouncing = 2, Pass = 3, Roof = 4, Count = 5 };
constexpr int kTargetSetupCount = static_cast<int>(TargetSetup::Count);

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

// Ball resting on the roof, car facing the attacked goal at a distance growing with the level.
// `fixedSpeed` < 0: driving speed from the level (400..1100 uu/s) when `moving`.
Spawn flickSpawn(float level, bool moving, float fixedSpeed, const GoalFrame& goal, const RoofParams& roof, Rng& rng);
// Net targeting setups (Mix is resolved by the caller).
Spawn targetSpawn(TargetSetup setup, float level, const GoalFrame& goal, const RoofParams& roof, Rng& rng);
// Power challenge: ball rolling towards the player who faces the attacked goal.
Spawn powerSpawn(float ballSpeed, const GoalFrame& goal, Rng& rng);

} // namespace rlt::shoot
