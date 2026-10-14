#pragma once
// Settings of the "Mécas mouvement" module (detection, grading and drills of movement mechanics).
// Every field is declared once in MechanicsModule::describeSettings (cvar prefix "rlt_me_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct MechanicsConfig {
    bool enabled = true;

    // Feedback
    bool chatSummary = false;     // one chat line per graded attempt
    bool toastRecords = true;     // notification when a personal best falls
    int minScore = 0;             // attempts below this score do not replace the HUD panel

    // HUD "Dernière méca" (left middle zone)
    bool hud = true;
    float hudX = 0.01f;           // screen fractions
    float hudY = 0.40f;
    float hudScale = 1.f;
    float hudSeconds = 8.f;       // 0 = always visible
    int hudMetrics = 3;
    bool hudCounters = false;

    // Families (Sol / Flips / Air / Mur / Boost)
    bool wavedash = true;
    bool chainDash = true;
    bool zapDash = true;
    bool powerslide = true;
    bool speedflip = true;
    bool halfFlip = true;
    bool flipCancel = true;
    bool fastAerial = true;
    bool stall = true;
    bool helicopter = true;
    bool recovery = true;
    bool wallDash = true;
    bool boost = true;

    // Focus and drills
    float slowMotion = 1.f;       // game speed in focus/drills (0.25 .. 1)
    bool drillAutoReset = true;
    float drillDelay = 1.5f;      // seconds the result stays before the next attempt
    bool drillAdaptive = true;
    bool showGuides = false;      // 3D lines of the drills (lane, line to the ball, target, wall band), off by default

    // Expert
    float speedflipAngle = 30.f;
    int speedflipCancelMs = 50;
    int chainGapMs = 200;
    float recoveryMinHeight = 200.f;
    int boostWindowSeconds = 20;
};

} // namespace rlt
