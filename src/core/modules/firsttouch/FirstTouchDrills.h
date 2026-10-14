#pragma once
// First-touch drills: catalogue (names, difficulty tags, plain step lists, instructions), level criteria and ball
// spawns relative to the car pose. Every parameter (ball speed, angle, height, distance) and what a rep must reach
// is driven by one level in [0, 1] (0 = easiest), produced by a Staircase per drill, and randomised with a seeded
// Rng inside the level's band.

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
// What to do, in order, one action per step (plain words, no numbers): the tab's step list.
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
