#pragma once
// Freeplay drill presets (one per trainable family): car (and ball) placement, timing, goal text and 3D guides.
// `level` (0 easy .. 1 hard) comes from the family's Staircase. Coordinates are for the blue team (attacking
// +Y) and are rotated 180 degrees around Z for orange.

#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Rng.h"
#include "core/modules/mechanics/MechCatalog.h"

namespace rlt::mech {

struct DrillSetup {
    Focus focus = Focus::None;
    CarPlacement car;
    bool placeBall = false;        // false: the ball is parked out of the way
    BallPlacement ball;
    float countdown = 0.f;         // seconds of frozen countdown before GO (speedflip kickoff)
    bool kickoff = false;          // the kickoff tracker follows the drill (time to ball); no touch = failure
    float timeLimit = 4.f;         // seconds after the start before the attempt is over
    float passScore = 60.f;        // drill success: detector success and score >= passScore (and the touch below)
    bool needTouch = false;        // fast aerial, half-flip: the result waits for the ball touch (time to ball)
    bool endOnResult = true;       // false: several results per attempt (wall dash runs)
    bool boostWindow = false;      // boost drill: the efficiency window is graded at the end
    std::string goal;              // French instruction line
    std::string variant;           // French variant name ("diagonale droite"...)
    std::vector<Vec3> guide;       // 3D polyline guide (world), may be empty
    std::vector<Vec3> ticks;       // short marker crossbars (pairs of points)
    bool hasTarget = false;
    Vec3 target;                   // 3D target marker (ball or zone)
};

// Builds the drill of a family. `attempt` alternates sides/spots, `rng` randomises recovery throws.
DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng);

// Linear map of the difficulty level.
inline float levelLerp(float level, float easy, float hard) { return easy + (hard - easy) * level; }

} // namespace rlt::mech
