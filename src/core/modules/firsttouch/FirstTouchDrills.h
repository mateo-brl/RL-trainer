#pragma once
// First-touch drills: catalogue (names, difficulty tags, plain step lists, instructions), level criteria and ball
// spawns relative to the car pose. Every parameter (ball speed, angle, height, distance) and what a rep must reach
// is driven by one level in [0, 1] (0 = easiest), produced by a Staircase per drill, and randomised with a seeded
// Rng inside the level's band.
//
// Setups (audit of the drills, spec firsttouch-shooting):
//  - floor drills start from a "drill anchor" (own half, facing the opponent goal) when the player's pose has too
//    little room ahead or sits next to a side wall, and their floor balls are kept off the floor-to-wall ramp;
//  - "Chute du mur" places the car itself, parallel to the wall, close to it (the wall side alternates);
//  - "Balle haute" hangs the ball in the air at niveaux 1 to 6 (released by the jump, then by being under it);
//  - every rep starts with a short get-ready hold (car and ball held still) at the first levels, the ball's path
//    or landing point drawn, and the velocity given at the release.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Math.h"
#include "core/common/Rng.h"
#include "core/common/Snapshot.h"
#include "core/modules/firsttouch/FirstTouchAnalysis.h"

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

// Difficulty tag shown in the tab (basics first). The catalogue order is already easiest first.
enum class Tier : int { Beginner = 0, Intermediate = 1, Advanced = 2 };
Tier drillTier(DrillType t);
const char* tierLabel(Tier t);               // "Débutant", "Intermédiaire", "Avancé"
const char* drillAdvice();                   // one line telling a beginner where to start
// What to do, in order, one action per step (plain words, no numbers): the tab's step list (the tutorial's step
// titles, see FirstTouchTutorials.h).
std::vector<std::string> drillSteps(DrillType t);

// ---- Levels ---------------------------------------------------------------------------------------------
// The player sees the adaptive level (0..1) as "niveau 1" to "niveau 10".
int playerLevel(float level);
float levelOfPlayer(int n);                  // 1 -> 0, 10 -> 1
constexpr float kDrillStartLevel = 0.f;      // a new player starts at niveau 1
// First step: below this displayed level a rep is judged on the touch alone (speed and contact point, see
// touchOnlyScore), the ball may get away afterwards. From it, the full score counts and the ball must stay close.
constexpr int kControlLevel = 4;

// What a rep must reach at a level. Niveau 1: touch score 10 points under the player's threshold
// (rlt_ft_success_score), control not judged, 1.5 x the time. Niveau 10: full score 15 points over the threshold
// and a control share 0.1 over rlt_ft_control_fraction, normal time.
struct DrillCriteria {
    bool firstStep = true;    // touch alone (below kControlLevel)
    float minScore = 60.f;    // 0..100
    float minControl = 0.f;   // share of the control window with the ball close; 0 = not judged
    float timeout = 6.f;      // s to touch the ball
};
DrillCriteria drillCriteria(float level, float successScore, float controlFraction, float timeout);
// Score a rep is judged on (and shown in the drill panel): touchOnlyScore during the first step, else the full score.
float drillRepScore(const DrillCriteria& c, const TouchScore& s, const ScoreParams& p);
bool drillRepSuccess(const DrillCriteria& c, float repScore, float controlFraction);
// The one tip of a rep (tips sorted by weight): the most important one the level judges. The first step skips the
// tips about the control after the touch. "" when none applies.
std::string drillRepTip(const DrillCriteria& c, const std::vector<Tip>& tips);
// Goal of the level in a few words for the HUD ("Objectif : une touche douce.").
const char* drillGoal(float level);

// Get-ready hold before each rep (car and ball held still, path drawn): 1 s at niveaux 1 to 3, 0.5 s at niveaux 4 to
// 6, none after. The first ball of a drill waits for the throttle, then counts down 3 s (Hold.h ReadyPhase).
float freezeSeconds(float level);
constexpr float kMixFreeze = 1.f;            // Mix: the next drill's name is shown during at least this hold

// ---- Poses ----------------------------------------------------------------------------------------------
// Start pose of a drill: flat basis, on the ground, kept away from the walls.
struct DrillPose {
    Vec3 pos;
    Vec3 fwd{1.f, 0.f, 0.f};
    Vec3 right{0.f, 1.f, 0.f};
    float yawDeg = 0.f;
};
DrillPose poseFromCar(const CarState& car);
DrillPose poseFromYaw(const Vec3& pos, float yawDeg);  // pos.z forced to the resting height
CarPlacement carPlacementFor(const DrillPose& pose, bool fullBoost);

// +1 for blue (attacks +Y), -1 for orange.
inline float teamSign(int team) { return team == 1 ? -1.f : 1.f; }
// Free floor ahead of the pose along its nose, up to the walls or the corner planes (uu).
float freeAhead(const DrillPose& p);
// Drill anchor: x clamped to [-2000, 2000], y = anchorY x team sign (own half), facing the opponent goal.
DrillPose anchorPose(const DrillPose& p, int team, float anchorY);
// The pose replaced by the anchor when it has less than `needAhead` uu free ahead or is within 1200 uu of a side
// wall (floor drills only).
DrillPose anchoredPose(const DrillPose& p, int team, float needAhead, float anchorY);
constexpr float kAnchorY = 2500.f;           // floor drills (roulante vers toi, passe, lob, balle haute)
constexpr float kNeedAhead = 3500.f;
constexpr float kAwayAnchorY = 3000.f;       // roulante qui s'éloigne: about 8000 uu free ahead
constexpr float kAwayNeedAhead = 4500.f;
constexpr float kSideWallMargin = 1200.f;
// The pose a drill starts from (anchored when needed). Wall and Mix: the pose unchanged.
DrillPose drillPoseFor(DrillType t, const DrillPose& player, int team);

// Keeps a ball centre inside the soccar arena (walls, corners, ceiling).
Vec3 clampBallToArena(const Vec3& p);
// Floor balls: also off the floor-to-wall ramp (|x| <= 3800, |y| <= 4900).
Vec3 clampFloorBall(const Vec3& p);

// ---- Spawns ---------------------------------------------------------------------------------------------
// How a hanging ball of "Balle haute" is let go.
enum class HangRelease : int {
    None = 0,     // a normal ball (velocity at the release)
    OnJump = 1,   // niveaux 1 to 3: released by the jump pressed with the ball over the front half of the roof
    Under = 2,    // niveaux 4 to 6: released as soon as the ball is over the front half of the roof
};
constexpr float kHangAhead = 1000.f;         // uu ahead of the pose
constexpr float kHangZ = 345.f;              // ball centre height
constexpr float kHangJumpVz = 280.f;         // release on the jump (niveaux 1 to 3)
constexpr float kHangIdleRelease = 1.5f;     // s under the ball without jumping: released anyway (the jump is needed)
constexpr float kRoofZoneAhead = 120.f;      // ball 0..120 uu ahead of the car origin along the nose
constexpr float kRoofZoneSide = 60.f;        // |lateral| < 60 uu
// Ball over the front half of the roof (the release zone of the hanging ball).
bool ballOverFrontRoof(const CarState& car, const Vec3& ballPos);

struct SpawnContext {
    int team = 0;
    bool placeCar = true;   // the drill may place the car ("Replacer la voiture" on)
    float wallSide = 0.f;   // Wall: +1 / -1 side wall when the drill places the car (0 = from the pose)
};

struct DrillSpawn {
    DrillType type = DrillType::RollIn;  // concrete type (never Mix)
    BallPlacement ball;                  // the ball at the release (velocity included)
    bool placesCar = false;              // the drill chose the car pose itself (Wall)
    DrillPose carPose;                   // the pose of the rep (anchored, or the Wall placement)
    HangRelease hang = HangRelease::None;
    float releaseVz = 0.f;               // hanging ball: vertical speed given at the release
    bool valid = true;                   // Wall without car placement: false while no side wall is close enough
};

// Ball placement for one rep. `type` must be concrete (not Mix). `pose` is the pose of the rep (drillPoseFor).
// Deterministic for a given rng state.
DrillSpawn makeSpawn(DrillType type, const DrillPose& pose, float level, Rng& rng, const SpawnContext& ctx = {});

// Wall drill without car placement: the side wall the car faces (sign of fwd.x, else the nearest one) and whether it
// is within kWallReach of the car.
constexpr float kWallReach = 2000.f;
bool wallInReach(const DrillPose& pose, float& sigma);

// ---- Mix ------------------------------------------------------------------------------------------------
// A drill joins the Mix when it is a Débutant drill, its own level is at least niveau 4, or it was passed 5 times.
bool mixUnlocked(DrillType t, float level, int successes);
// Draw weight: the drills the player succeeds least come more often (1 / recent success rate, capped).
float mixWeight(float recentRate);
// Picks the concrete drill of a Mix rep among weights > 0, never `previous` twice in a row (unless it is the only
// one). All weights 0: RollIn.
DrillType pickMixType(Rng& rng, DrillType previous, const std::array<float, kDrillKinds>& weights);
// Uniform pick among the 6 drills (avoids repeating the previous one).
DrillType pickMixType(Rng& rng, DrillType previous);

// ---- Light ball predictor (the module cannot use the prediction module) -------------------------------------
// Gravity, drag, floor bounces with restitution and Coulomb friction (spin), side and back walls, ceiling.
struct BallTrack {
    std::vector<Vec3> points;             // every 2 ticks from the start (points[0] = start)
    std::vector<Vec3> floorBounces;       // floor contact points (bounces of an airborne ball), in order
    std::vector<float> floorBounceTimes;  // s after the start
    bool reached(int n) const { return static_cast<int>(floorBounces.size()) >= n; }
};
BallTrack predictBall(const Vec3& pos, const Vec3& vel, const Vec3& angVel, float seconds);
// Ball position `t` seconds after the start of a track (linear between the stored points).
Vec3 trackAt(const BallTrack& track, float t);

} // namespace rlt::ft
