#pragma once
// Settings of the "Parcours" module: virtual training maps drawn over plain freeplay (rings courses: aerial,
// ground & walls, air dribble; control drills: roof dribble, slalom, catch).
// Every field is declared once in CoursesModule::describeSettings (cvar prefix "rlt_co_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct CoursesConfig {
    bool enabled = true;

    // ---- Difficulty and flow
    int difficultyMode = 0;        // 0 Auto (staircase), 1 Facile, 2 Moyen, 3 Difficile, 4 Extreme, 5 Manuel
    int manualDifficulty = 50;     // 0..100, used when difficultyMode == 5
    int ringCount = 0;             // 0 = from difficulty, otherwise rings/gates per generated course
    float countdown = 2.f;         // seconds the car is frozen before the start
    int chainMode = 0;             // 0 smart, 1 same course, 2 new course after a success, 3 stop at the end
    float resultHold = 1.5f;       // seconds the result stays on screen before the next attempt

    // ---- Rings
    int missPolicy = 0;            // 0 time penalty and go on, 1 go back for it, 2 immediate fail
    float missPenalty = 2.f;       // seconds added per missed ring (policy 0)
    bool grazeCounts = true;       // a pass touching the rim still counts
    int boostStart = 100;          // boost given at the start (0..100)
    bool unlimitedBoost = false;
    bool boostRefills = true;      // refill rings put the tank back to 100
    int airDribbleStart = 0;       // 0 ball on the roof, 1 ball popped in front of the car
    bool parkBall = true;          // car-only courses: keep the ball out of the way (top corner)
    float timeoutFactor = 2.5f;    // time limit = factor x target time + 10 s

    // ---- Control drills
    float roofTolerance = 0.3f;    // seconds the ball may leave the roof before the dribble counts as lost
    float catchHold = 1.f;         // seconds on the roof to validate a catch
    int catchType = 0;             // 0 mix, 1 drop from the sky, 2 off the side wall
    bool catchMarker = true;       // landing marker on the floor

    // ---- Display
    Color nextColor{60, 255, 120, 255};
    Color ringColor{255, 255, 255, 170};
    Color passedColor{60, 200, 60, 110};
    Color guideColor{255, 220, 60, 200};
    float lineWidth = 3.f;
    int lookahead = 4;             // upcoming rings drawn after the next one
    bool showPassed = true;
    bool guideLine = true;
    bool ringNumbers = true;
    bool hud = true;
    float hudX = 0.01f;            // HUD panel anchor, screen fractions (top-left zone)
    float hudY = 0.12f;
};

} // namespace rlt
