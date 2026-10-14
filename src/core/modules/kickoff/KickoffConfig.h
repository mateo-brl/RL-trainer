#pragma once
// Settings of the "Kickoff" module (kickoff drill with countdown, ghost opponent, grades and stats).
// Every field is declared in KickoffModule::describeSettings (cvar prefix "rlt_ko_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct KickoffConfig {
    bool enabled = true;

    // ---- Drill
    int spawnMode = 1;               // KickoffMode: 0 chosen spawn, 1 random, 2 sequence of the 5, 3 weakness
    int spawn = 0;                   // KickoffSpawn used by the "chosen spawn" mode
    int team = 0;                    // 0 blue, 1 orange (point reflection of the blue spawn)
    bool autoRepeat = true;          // next rep starts by itself after resetDelay
    float resetDelay = 2.f;          // s between the end of an attempt and the next placement
    float timeout = 6.f;             // s after GO without touch: attempt over
    int countdownSeconds = 3;        // "3, 2, 1, GO"
    bool nativeCountdown = true;     // also send the game's own countdown messages
    bool goJitter = false;           // random extra delay before GO (reaction training)
    float goJitterMax = 0.4f;        // s, upper bound of that delay
    bool holdCar = true;             // re-apply the car placement on every countdown tick
    float spawnBoost = 100.f / 3.f;  // boost given at placement and kept full during the countdown
    float ballHeight = 93.15f;       // z of the ball at rest at the centre spot

    // ---- Ghost opponent
    // KickoffGhostMode: 0 off, 1 beginner, 2 standard, 3 pro, 4 elite, 5 personal best, 6 adaptive. Adaptive by
    // default: it starts at the beginner time and speeds up with the wins.
    int ghostLevel = 6;
    bool ghostHitsBall = true;  // when the ghost wins or ties, push the ball like its hit would
    float ghostImpulseScale = 1.f;
    float duelWindow = 0.10f;  // s: beyond this arrival gap the first car wins outright
    float duelNeutralBand = 0.06f;
    float adaptiveTarget = 0.6f;  // win rate the adaptive ghost converges to
    Color ghostColor{120, 200, 255, 230};

    // ---- Grading and feedback
    int gradeTarget = 1;          // KickoffLevel the time/speed scores are measured against (Standard: flips)
    bool chatFeedback = true;     // one chat line per drill attempt (time, score, main tip)
    bool recordToasts = true;     // notification for a new personal best / end of a series
    bool passiveAnalysis = true;  // grade kickoffs outside the drill (freeplay reset, offline matches)

    // ---- HUD
    bool hudEnabled = true;
    float hudX = 0.01f;
    float hudY = 0.12f;
    float hudScale = 1.f;  // panel size, multiplies the general HUD size
    bool bigCountdown = true;
    float countdownY = 0.30f;
    bool showPath = true;
    bool showSplits = true;
    Color pathColor{255, 200, 60, 170};
};

} // namespace rlt
