#pragma once
// Freeplay drill presets (one per trainable family): car (and ball) placement, timing, plain-words goal and steps,
// pass conditions of the level and 3D guides. `level` (0 easy .. 1 hard) comes from the family's Staircase: the
// lowest level is meant for a beginner (wide margins, more time, and for the speedflip a first step without the
// kickoff), the highest one asks for a clean execution. Coordinates are for the blue team (attacking +Y) and are
// rotated 180 degrees around Z for orange.

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

// What marks a drill step as done (live HUD: the next step is shown). Latched from the start of the attempt.
enum class Cue : uint8_t { None = 0, Boost, Steer, Slide, PitchBack, Roll, Yaw, Jump, DoubleJump, Dodge, Cancel, Air, Land, Touch };

struct DrillStep {
    std::string text;      // one action, plain French
    Cue done = Cue::None;  // None: never marked done
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
    CarPlacement car;
    bool placeBall = false;        // false: the ball is parked out of the way
    BallPlacement ball;
    float countdown = 0.f;         // seconds of frozen countdown before GO
    bool kickoff = false;          // the kickoff tracker follows the drill (time to ball); no touch = failure
    float timeLimit = 4.f;         // seconds after the start before the attempt is over
    float passScore = 60.f;        // drill success also needs this score (0 at the lowest levels)
    bool requireDetector = false;  // drill success also needs the detector's own success (top levels)
    std::vector<DrillCheck> checks;
    bool needTouch = false;        // fast aerial, half-flip: the result waits for the ball touch (time to ball)
    bool endOnResult = true;       // false: several results per attempt (wall dash runs)
    bool boostWindow = false;      // boost drill: the efficiency window is graded at the end
    bool timed = false;            // the time left is the main instruction (boost window, chain)
    int stage = 0, stages = 0;     // speedflip: decomposed steps of the progression (0 = single stage)
    std::string goal;              // plain French goal of this level ("Étape 1 sur 3 : ...")
    std::string memo;              // the move in a few words
    std::vector<DrillStep> steps;  // what to do, in order
    std::string variant;           // French variant name ("diagonale droite"...)
    std::vector<Vec3> guide;       // 3D polyline guide (world), may be empty
    std::vector<Vec3> ticks;       // short marker crossbars (pairs of points)
    bool hasTarget = false;
    Vec3 target;                   // 3D target marker (ball or zone)
};

// Builds the drill of a family. `attempt` alternates sides/spots, `rng` randomises recovery throws.
DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng);

struct DrillJudgement {
    bool pass = true;
    const char* code = nullptr;  // error code of the first failed check (its tip is the one to show)
};
// Pass conditions of the level (DRILL_NO_TOUCH first, then `checks` in order). The score and the detector's own
// success are checked by the module.
DrillJudgement judgeDrill(const DrillSetup& d, const MechGrade& g);

// Level shown to the player: 1 (lowest) .. 10.
int displayLevel(float level);

// Linear map of the difficulty level.
inline float levelLerp(float level, float easy, float hard) { return easy + (hard - easy) * level; }

} // namespace rlt::mech
