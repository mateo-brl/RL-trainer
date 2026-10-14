#pragma once
// Freeplay drill presets (one per trainable family): car (and ball) placement, timing, plain-words goal and steps,
// pass conditions of the level and 3D guides. `level` (0 easy .. 1 hard) comes from the family's Staircase: the
// lowest level is meant for a Diamond player who never trained the move (wide margins, more time, and for the
// speedflip a first step without the kickoff), the highest one asks for a clean Champion / Grand Champion execution.
// Coordinates are for the blue team (attacking +Y) and are rotated 180 degrees around Z for orange.
//
// Every drill starts the same way (cross-cutting fix 1): the car (and the ball) are HELD at the start pose with zero
// velocity on every tick of the get-ready phase (first attempt: wait for the throttle, then 3 s; automatic reset:
// `countdown` seconds), then on GO the car and the ball get their launch velocity (`car.vel`, `ball.vel`).
//
// "Parcours complet" (`full`): the same family, but the player does every step himself from a still car (climb the
// wall, hop before the wavedash, reverse before the half-flip...). Its steps are graded one by one by the module
// (FullSequence.h).

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

// What marks a drill step as done (live HUD: the next step is shown). Latched from the start of the attempt.
enum class Cue : uint8_t {
    None = 0, Boost, Steer, Slide, PitchBack, Roll, Yaw, Jump, DoubleJump, Dodge, Cancel, Air, Land, Touch, Throttle
};

struct DrillStep {
    std::string text;      // one action, plain French
    Cue done = Cue::None;  // None: never marked done
    // Error code (Tips.h) of the attempt when this step is the first one missing at the time limit (fix 5: a timeout
    // is never silent). Empty: the family's default "nothing happened" code.
    const char* miss = "";
};

// One pass condition of the level: a band on a raw grade metric, or (key == nullptr) an error code the attempt must
// not have. Checks are listed in the order of the move: the first one that fails gives the single tip of the attempt.
struct DrillCheck {
    const char* key = nullptr;
    float lo = -1e9f, hi = 1e9f;   // pass band, raw metric units
    const char* lowCode = "";      // tip when under `lo` (or missing); the error code when key == nullptr
    const char* highCode = "";     // tip when over `hi`
    bool optional = false;         // missing or negative metric: not checked
};

struct DrillSetup {
    Focus focus = Focus::None;
    bool full = false;             // "parcours complet": every step done by the player, graded by the step tracker
    CarPlacement car;              // pose held during the countdown; vel / angVel applied on GO
    bool placeBall = false;        // false: the ball is parked out of the way
    BallPlacement ball;            // pose held during the countdown; vel / angVel applied on GO
    bool hangBall = false;         // the ball waits in the air, perfectly still, until the car touches it
    float countdown = 2.f;         // seconds of countdown after an automatic reset (first attempt: wait + 3 s)
    bool kickoff = false;          // the kickoff tracker follows the drill (time to ball); no touch = failure
    bool kickoffRules = false;     // match boost: limited boost (33), pads respawned, boost re-sent while held
    int spotKind = 0;              // kickoff spot: 0 diagonal, 1 offset, 2 centre (time-to-ball benchmark)
    float reachGap = 0.f;          // > 0: passing this close to the ball (hitbox to ball surface, uu) counts as the touch
    float timeLimit = 4.f;         // seconds after GO before the attempt is over
    float passScore = 60.f;        // drill success also needs this score (0 at the lowest levels)
    bool requireDetector = false;  // drill success also needs the detector's own success (top levels)
    std::vector<DrillCheck> checks;
    bool needTouch = false;        // the result waits for the ball touch (time to ball)
    float touchWindow = 2.5f;      // needTouch: seconds after the grade to wait for the touch
    bool needLanding = false;      // the result waits for the landing (wheels landing measured)
    bool endOnResult = true;       // false: the attempt runs to its end (several results)
    int runDashes = 0;             // wall dash: successful wall dashes needed in one run (the last one decides)
    bool boostWindow = false;      // boost drill: the efficiency window is graded at the end
    float boostNeeded = 0.f;       // boost drill: boost needed to reach supersonic from the launch speed
    bool timed = false;            // the time left is the main instruction (boost window, chain)
    float endY = 0.f;              // > 0: the attempt ends once the car passes this y (blue axes, end of the lane)
    float nowCue = 0.f;            // > 0: "Maintenant !" when the rear wheels touch in less than this (s)
    int stage = 0, stages = 0;     // speedflip: decomposed steps of the progression (0 = single stage)
    std::string goal;              // plain French goal of this level ("Étape 1 sur 3 : ...")
    std::string memo;              // the move in a few words
    std::vector<DrillStep> steps;  // what to do, in order
    std::string variant;           // French variant name ("diagonale droite"...)
    std::vector<Vec3> guide;       // 3D polyline guide (world), may be empty
    std::vector<Vec3> ticks;       // short marker crossbars (pairs of points)
    bool hasTarget = false;
    Vec3 target;                   // 3D target marker (ball or zone)
    bool mirrored = false;         // orange team: positions rotated 180 degrees
    int side = 1;                  // +1 / -1: side of the variant (wall, ball side) in blue axes
};

// Builds the drill of a family. `attempt` alternates sides/spots, `rng` randomises recovery throws and the kickoff
// spot of the speedflip "parcours complet".
DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng, bool full = false);

struct DrillJudgement {
    bool pass = true;
    const char* code = nullptr;  // error code of the first failed check (its tip is the one to show)
};
// Pass conditions of the level (DRILL_NO_TOUCH first, then `checks` in order). The score and the detector's own
// success are checked by the module.
DrillJudgement judgeDrill(const DrillSetup& d, const MechGrade& g);

// Level shown to the player: 1 (lowest) .. 10.
int displayLevel(float level);
// Lowest staircase level shown as niveau n (1..10).
inline float levelOf(int n) { return static_cast<float>(std::max(1, std::min(10, n)) - 1) / 9.f; }

// Linear map of the difficulty level.
inline float levelLerp(float level, float easy, float hard) { return easy + (hard - easy) * level; }

// Speedflip kickoff spots (blue team): position, yaw, French name, kind (0 diagonal, 1 offset, 2 centre).
struct KickoffSpot {
    float x, y, yaw;
    const char* name;
    int kind;
};
const KickoffSpot& kickoffSpot(int index);  // 0..4
// Time from GO to the touch a kickoff should reach, by spot kind, from the beginner benchmark (t = 0) to the solid
// one (t = 1). Same numbers as the kickoff module (KickoffData.cpp).
float kickoffTimeToBall(int kind, float t);

// Distance from the ball surface to the car hitbox (negative: overlapping). `halfExtent` and `offset` come from the
// player's car (PlayerProfile::hitboxExtent / 2, hitboxOffset).
float hitboxGap(const CarState& car, const Vec3& ball, const Vec3& halfExtent, const Vec3& offset,
                float ballRadius = 91.25f);
// Seconds until the lowest rear wheel touches the floor at the current fall speed (1e9 when not falling).
float rearWheelTimeToFloor(const CarState& car);

} // namespace rlt::mech
