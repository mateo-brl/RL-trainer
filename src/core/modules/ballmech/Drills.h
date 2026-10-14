#pragma once
// Freeplay drills of the ball mechanics: one randomised setup per mechanic whose difficulty follows the adaptive
// level (0 easiest .. 1 hardest), and the level-dependent success criteria. Pure functions: the module applies the
// placements through Actions while it owns the activity.
//
// All positions are written for the blue team (attacking +Y) and mirrored for orange.

#include <array>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/common/Snapshot.h"
#include "core/common/Units.h"
#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

// How a drill starts. Short: the mechanic alone, the car and ball placed for it (ball on the roof, car on the
// ceiling...). Full ("parcours complet"): the positioning before it too (pick the ball up, climb the wall...), with the
// shared step tracker naming each step on the HUD. The ceiling shot starts with the full sequence by default.
enum class DrillMode : int { Short = 0, Full = 1 };

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
std::string drillLevelNote(Mech m, float level, DrillMode mode = DrillMode::Short);
// Tab: how the drill gets harder with the level, in plain words.
std::string drillProgression(Mech m);
// Tab, technical details: what a success needs at this level, with the numbers (score, speed, duration).
std::string drillCriteria(Mech m, float level, int successScore, float pinchSpeed, SpeedUnit unit);

DrillMode defaultMode(Mech m);
// Tab / HUD name of a start ("Départ au plafond", "Parcours complet"...).
const char* modeLabel(Mech m, DrillMode mode);
// Console ids: "court" / "parcours" (also "short", "full", "complet").
const char* modeId(DrillMode mode);
bool modeFromId(const std::string& id, DrillMode& out);

struct DrillSetup {
    CarPlacement car;
    BallPlacement ball;
    bool burnFlip = false;         // script a double jump right after the launch so the car starts without flip
    bool doubleTapRead = false;    // the drill sends the ball to the backboard itself (double tap "read" mode)
    std::string flickTarget;       // requested flick variant ("" = any)
    std::string label;             // short French description of this setup ("Mur droit", "Lecture du rebond"...)
    DrillMode mode = DrillMode::Short;
    // Hanging ball (fix 3): from GO the ball waits still at ball.pos until the step `releaseStep` is validated, the
    // car comes within `releaseDist` (0 = off) or the player hits it; it is then let go with `releaseVel`.
    bool hangBall = false;
    std::string releaseStep;
    float releaseDist = 0.f;
    Vec3 releaseVel;
    // Ball aimed from the real car at the release (ceiling parcours, niveaux 1-3): instead of `releaseVel`, it is
    // lobbed from below so that its apex comes `releaseApex` s later right in front of the falling car's nose, like
    // "Départ au plafond" (where the car drops depends on its heading on the wall, the player cannot line it up).
    float releaseApex = 0.f;       // 0 = off
    bool pickup = false;           // shared "Ramassage" start: stopped car, ball resting or rolling toward it
    bool pickupRolling = false;
    bool armCeiling = false;       // the car starts on the ceiling: the ceiling detector starts there too
    float gameSpeed = 1.f;         // slow motion of the first levels (restored by the engine on stop)
    float side = 0.f;              // +1 / -1: the side wall the drill uses (after the team mirror), 0 = none
    int airRollHint = 0;           // flip reset: -1 air roll left, +1 air roll right brings the wheels to the ball
    Vec3 target;                   // a point the full sequence aims at (aerial: where the centre is met)
    // Guide drawn in 3D on the first levels: the ball's path until the meeting point and a ring there.
    int guideCount = 0;
    std::array<Vec3, 16> guide{};
    bool hasMeet = false;
    Vec3 meetPoint;
};

struct SetupOptions {
    DrillMode mode = DrillMode::Short;
    int pickupNiveau = 1;          // 1..10: level of the "Ramassage" start (its own staircase)
    std::string flickVariant;
};

// `car` gives the hitbox (ball placed on the roof) and `team` the attack direction.
DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float ballRadius, Rng& rng,
                          const SetupOptions& options);
DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float ballRadius, Rng& rng,
                          const std::string& flickVariant = "");

// Thresholds of the level criteria (shared by the verdict, the texts and the tests).
float ceilingSpeedNeeded(float level);       // ball speed after a ceiling shot
float flickPowerNeeded(Mech m, float level); // speed the flick adds to the ball (flick, musty, breezi)
float airDribbleSeconds(float level);        // air dribble duration (0 below niveau 4)
float pinchSpeedNeeded(float level);         // pinch exit speed in the drill (free play keeps rlt_bm_pinch_speed)
float airRollSpeedNeeded(float level);
float aerialSpeedNeeded(float level);
float powerShotSpeedNeeded(float level);
float doubleTapSpeedNeeded(float level);
constexpr int kBzTornadoLevel = 3;  // breezi: below this level the tornado alone is enough (module check)
constexpr int kArTargetLevel = 2;   // air roll: from this level the shot must be on target

// Seconds the car and ball are held before an attempt that follows another one: 1.5 s at niveaux 1-3 (fix 1), the
// player's setting (rlt_bm_drill_ready) above.
float drillReadyTime(float level, float setting);

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

// Ceiling parcours, niveaux 1-3 (DrillSetup::releaseApex): the ball relaunched from the real car once it has left
// the ceiling. Lobbed from below, it reaches its apex `apex` s later right in front of the falling car's nose (at
// least 260 uu ahead of the hitbox centre, more when the car is fast), at the hitbox height: flipping then hits it,
// a passive fall never does (same construction as "Départ au plafond").
BallPlacement ceilingReleaseAim(const CarState& car, float apex);

// Ballistic launch: velocity at t = 0 and start position so that the ball is at `meetPos` with `meetVel` after `t`.
void ballisticStart(const Vec3& meetPos, const Vec3& meetVel, float t, Vec3& startPos, Vec3& startVel);

// Rotator from Euler angles in degrees (yaw wrapped).
Rot rotDeg(float pitchDeg, float yawDeg, float rollDeg);

} // namespace rlt::bm
