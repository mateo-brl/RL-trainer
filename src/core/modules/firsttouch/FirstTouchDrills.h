#pragma once
// First-touch drills: catalogue (names, instructions) and ball spawns relative to the car pose.
// Every parameter (ball speed, angle, height, distance) is driven by one level in [0, 1] (0 = easiest),
// produced by a Staircase per drill, and randomised with a seeded Rng inside the level's band.

#include <cstdint>
#include <string>

#include "core/common/Actions.h"
#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Math.h"
#include "core/common/Rng.h"
#include "core/common/Snapshot.h"

namespace rlt::ft {

enum class DrillType : int { RollIn = 0, RollAway = 1, Pass = 2, Lob = 3, Wall = 4, High = 5, Mix = 6 };
constexpr int kDrillKinds = 6;  // concrete drills (Mix picks one of them every rep)

const char* drillId(DrillType t);            // "roll_in", "roll_away", "pass", "lob", "wall", "high", "mix"
bool parseDrillId(const std::string& id, DrillType& out);
const char* drillName(DrillType t);          // French display name
const char* drillDescription(DrillType t);   // one or two French sentences for the UI tab
// Live instruction shown in the drill panel, with the player's real buttons in the chosen style
// (keyStyleFromIndex(settings.profile.buttonStyle): Xbox or PlayStation names).
std::string drillInstruction(DrillType t, const PlayerProfile& profile, KeyStyle style);

// Start pose of a drill: flat basis, on the ground, kept away from the walls.
struct DrillPose {
    Vec3 pos;
    Vec3 fwd{1.f, 0.f, 0.f};
    Vec3 right{0.f, 1.f, 0.f};
    float yawDeg = 0.f;
};
DrillPose poseFromCar(const CarState& car);
CarPlacement carPlacementFor(const DrillPose& pose, bool fullBoost);

// Keeps a ball centre inside the soccar arena (walls, corners, ceiling).
Vec3 clampBallToArena(const Vec3& p);

struct DrillSpawn {
    DrillType type = DrillType::RollIn;  // concrete type (never Mix)
    BallPlacement ball;
};

// Ball placement for one rep. `type` must be concrete (not Mix). Deterministic for a given rng state.
DrillSpawn makeSpawn(DrillType type, const DrillPose& pose, float level, Rng& rng);

// Picks the concrete drill of a Mix rep (avoids repeating the previous one).
DrillType pickMixType(Rng& rng, DrillType previous);

} // namespace rlt::ft
