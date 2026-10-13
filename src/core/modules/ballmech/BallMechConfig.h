#pragma once
// Settings of the "Mécas balle" module: automatic detection and grading of ball / aerial mechanics (flip reset,
// ceiling shot, flicks, double tap, air dribble, air roll shots, aerial accuracy, pinches, power shots) and the
// matching freeplay drills. Every field is declared once in BallMechModule::describeSettings (cvar prefix "rlt_bm_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct BallMechConfig {
    // ---- General
    bool enabled = true;
    bool inMatches = true;          // also analyse offline matches (exhibition vs bots)
    int successScore = 50;          // minimum score (0..100) for an attempt to count as a success
    int chatMode = 1;               // 0 never, 1 successes only, 2 every graded attempt

    // ---- HUD
    bool hudEnabled = true;
    float hudX = 0.01f;             // screen fractions (left zone, below the movement panel)
    float hudY = 0.60f;
    float hudSeconds = 8.f;         // how long the last result stays on screen (0 = always)
    int hudMetrics = 3;             // metric lines in the panel
    bool hudTip = true;
    bool hudResetCounter = true;
    bool drawTrajectory = true;     // 3D path of the last graded shot
    bool drawResetIndicator = true; // 3D ring around the car while a flip reset is available
    float drillHudX = 0.01f;        // drill panel (top-left "exercise" zone)
    float drillHudY = 0.12f;

    // ---- Detection toggles (one per detector family)
    bool detFlipReset = true;
    bool detCeiling = true;
    bool detFlick = true;           // front / 45 / 90 / backflip / side / reverse / musty / breezi
    bool detDoubleTap = true;
    bool detAirDribble = true;
    bool detAerial = true;          // aerial shots, air roll shots, redirects
    bool detPinch = true;
    bool detPowerShot = true;

    // ---- Detection thresholds (expert)
    float pinchSpeed = 4000.f;      // uu/s exit speed that counts as a pinch
    float aerialMinSpeed = 1200.f;  // uu/s: softer aerial touches are dribble taps, not shots
    float powerMinSpeed = 1500.f;   // uu/s: softer ground touches are not graded (unless flip shots)

    // ---- Drills
    float drillReady = 0.8f;        // seconds the car is held before each attempt
    float drillPause = 1.5f;        // seconds between the result and the next attempt
    float drillTimeoutScale = 1.f;  // multiplies the per-mechanic attempt time limit
    bool drillAdaptive = true;      // adaptive difficulty (staircase)
    float drillTarget = 0.75f;      // success rate the adaptive difficulty aims for
    bool drillGo = true;            // native "GO!" message at the start of each attempt
};

} // namespace rlt
