#pragma once
// Settings of the "Tirs & flicks" module: flick drill (ball on the roof), net targeting (zones inside the
// opponent goal) and power shots. Every field is declared once in ShootingModule::describeSettings
// (cvar prefix "rlt_sh_"). Keep this header dependency-light: it is included by every module via Settings.h.

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct ShootingConfig {
    // ---- Général
    bool enabled = true;
    int unit = 0; // SpeedUnit: 0 auto (game setting), 1 km/h, 2 mph, 3 uu/s
    bool autoRepeat = true; // chain attempts automatically
    float countdown = 1.f; // s: car and ball held before each attempt
    float resultHold = 1.5f; // s: pause showing the result before the next attempt
    bool nativeCountdown = true; // game countdown messages ("1", "GO!")
    float startBoost = 100.f; // boost given at each attempt (0..100)
    bool chatResults = false; // one chat line per graded attempt
    bool toastRecords = true; // toast when a record is beaten
    float attemptTimeout = 10.f; // s without a result: new attempt (advanced)

    // ---- Flicks
    int flickRequested = 0; // 0 any, else 1 + shoot::FlickType index (Front .. Reverse, see FlickAnalyzer.h)
    int flickStart = 1; // 0 stationary car (learn the motion), 1 dribble start towards the goal (match-like), 2 random
    bool flickAdaptive = true; // car speed / distance follow the adaptive level
    float flickCarSpeed = 1100.f; // uu/s of the dribble start when not adaptive
    int flickTarget = 0; // 0 whole goal, 1 zone chosen for "Cibles", 2 random zone
    bool flickRequireOnTarget = true;
    bool roofGuide = true; // small circle on the roof centre
    float roofGap = 2.f; // uu between the roof and the ball at spawn (advanced)

    // ---- Cibles
    int targetZone = 5; // 0 top left, 1 top right, 2 low left, 3 low right, 4 centre, 5 random
    int targetSetup = 0; // 0 mix, 1 rolling, 2 bouncing, 3 pass across the box, 4 ball on the roof
    bool targetAdaptive = true;
    float targetLevel = 0.3f; // fixed level (0..1) when not adaptive
    float targetMinSpeedKph = 0.f; // minimum shot speed for a success (0 = off, advanced)
    bool showAllZones = true;
    bool showPrediction = false; // predicted impact marker on the goal plane (a prediction: off by default)

    // ---- Puissance
    float powerBallSpeedKph = 25.f; // speed of the ball rolling towards the player
    float powerGoalKph = 90.f; // success threshold when the adaptive goal is off
    bool powerAdaptive = true; // goal from the adaptive level: 70 km/h at niveau 1 .. 110 at niveau 10

    // ---- Affichage
    int hudMode = 0; // 0 during drills, 1 always (outside replays), 2 never
    float hudX = 0.01f; // screen fractions (top-left zone of ARCHITECTURE.md)
    float hudY = 0.12f;
    float hudScale = 1.f;
    bool freeAnalysis = false; // predicted impact marker outside drills (freeplay, workshop, replays)
    bool floorMarks = false; // circles on the floor: next bounces, meeting point, run-up point (predictions: off)
    Color zoneColor{200.f, 200.f, 200.f, 140.f};
    Color targetColor{255.f, 200.f, 40.f, 255.f};
    Color markerColor{0.f, 220.f, 255.f, 255.f};

    // ---- Avancé: flick classification thresholds (UNVERIFIED, to calibrate in game)
    float mustyPitchDeg = -35.f; // car pitch at the dodge at or below this = nose down (musty)
    float breeziRollDeg = 230.f; // accumulated roll before the backflip for a breezi
    float delayedSec = 0.6f; // jump to dodge delay for a delayed flick
    float roofReach = 260.f; // uu, ball centre to hitbox centre for "still on the car"
};

} // namespace rlt
