#pragma once
// Freeplay drills of the ball mechanics: one randomised setup per mechanic whose difficulty follows the adaptive
// level (0 easiest .. 1 hardest), and the level-dependent success criteria. Pure functions: the module applies the
// placements through Actions while it owns the activity.
//
// All positions are written for the blue team (attacking +Y) and mirrored for orange.

#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/common/Snapshot.h"
#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

struct DrillSetup {
    CarPlacement car;
    BallPlacement ball;
    bool burnFlip = false;         // script a double jump right after the launch so the car starts without flip
    bool doubleTapRead = false;    // the drill sends the ball to the backboard itself (double tap "read" mode)
    std::string flickTarget;       // requested flick variant ("" = any)
    std::string label;             // short French description of this setup ("Mur droit", "Lecture du rebond"...)
};

// `car` gives the hitbox (ball placed on the roof) and `team` the attack direction.
DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float ballRadius, Rng& rng,
                          const std::string& flickVariant = "");

// Drill verdict for a grade of the drill's family: mechanical success, minimum score (0..100) and the level
// criteria (power, accuracy, duration...). Extra error codes explain a failure caused by the drill criteria.
struct DrillVerdict {
    bool success = false;
    std::vector<std::string> extraErrors;
};
DrillVerdict judgeDrill(Mech drill, const MechGrade& g, float level, int minScore, float pinchSpeed);

// True when a grade belongs to the drill (its family), so it resolves the attempt with the drill's criteria.
bool gradeMatchesDrill(Mech drill, const MechGrade& g);

// Ballistic launch: velocity at t = 0 and start position so that the ball is at `meetPos` with `meetVel` after `t`.
void ballisticStart(const Vec3& meetPos, const Vec3& meetVel, float t, Vec3& startPos, Vec3& startVel);

// Rotator from Euler angles in degrees (yaw wrapped).
Rot rotDeg(float pitchDeg, float yawDeg, float rollDeg);

} // namespace rlt::bm
