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
#include "core/common/Units.h"
#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

// The player sees the adaptive level (0..1) as "niveau 1" to "niveau 10". The drill steps (what counts as a
// success, which setup) change at these displayed levels so the tab can name them exactly.
int playerLevel(float level);
float levelOfPlayer(int n);  // 1 -> 0, 10 -> 1

constexpr int kFrConvertLevel = 5;  // flip reset: from this level the reset must be used (flip into the ball)
constexpr int kFrTargetLevel = 8;   // flip reset: from this level the shot must be on target
constexpr int kTargetLevel = 6;     // ceiling, flicks, pinch: from this level the shot must be on target
constexpr int kDtTargetLevel = 3;   // double tap: below this level touching the rebound is enough
constexpr int kDtFullLevel = 7;     // double tap: from this level the player makes the first shot himself
constexpr int kAdWallLevel = 6;     // air dribble: from this level some attempts start from the wall
constexpr int kAdTargetLevel = 7;   // air dribble: from this level a finish must be on target
constexpr int kPsSideLevel = 6;     // power shot: from this level the ball comes from the side

// Minimum score of a drill success: 25 points under the player's threshold (rlt_bm_success_score) at level 1,
// 20 points over it at level 10, so the first level is reachable with a rough attempt.
int drillMinScore(int successScore, float level);
// Time per attempt multiplier: 1.5 at level 1 down to 1 at level 10.
float drillTimeFactor(float level);

// HUD instruction of the drill at this level: the short step list in plain words, no numbers, no button names.
std::string drillAction(Mech m, float level, const std::string& flickTarget = "");
// What a first level asks less than the full mechanic ("poser les 4 roues sur la balle suffit"), "" when the level
// asks the whole mechanic. Shown with the drill instruction when the drill starts.
std::string drillLevelNote(Mech m, float level);
// Tab: how the drill gets harder with the level, in plain words.
std::string drillProgression(Mech m);
// Tab, technical details: what a success needs at this level, with the numbers (score, speed, duration).
std::string drillCriteria(Mech m, float level, int successScore, float pinchSpeed, SpeedUnit unit);

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

// Drill verdict for a grade of the drill's family: mechanical success, minimum score (0..100, see drillMinScore)
// and the level criteria (power, accuracy, duration...). Extra error codes explain a failure caused by the drill
// criteria. The first levels split the hardest mechanics: the flip reset alone, then the rebound touch alone.
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
