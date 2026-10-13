#pragma once
// Grading of the shooting attempts into AttemptResult (score, success, verdict, metrics, French tips).
// Everything the player is told after an attempt is decided here, so it is unit-tested in isolation.

#include <string>

#include "core/common/Context.h"
#include "core/common/Grade.h"
#include "core/modules/shooting/FlickAnalyzer.h"
#include "core/modules/shooting/ShootGeometry.h"
#include "core/modules/shooting/ShootingDrills.h"

namespace rlt::shoot {

// Ball speed after a flick needed for a success at a level (uu/s): 1300 (47 km/h) .. 2400 (86 km/h).
float flickMinPower(float level);

struct FlickGradeParams {
    int requested = -1; // requested FlickType index, -1 = any
    float level = 0.f; // adaptive level of the flick drill
    float minPower = 1300.f; // uu/s for a success
    bool requireOnTarget = true; // success needs a shot predicted on goal
    float aimErrorDeg = 0.f; // flat angle between the launch and the target (used when the plane is not reached)
    float time = 0.f;
};

// `aim`: crossing predicted from the ball state at the report, scored against the flick target.
AttemptResult gradeFlick(const FlickReport& r, const AimResult& aim, const FlickGradeParams& p,
                         const PlayerProfile& profile);
// The ball fell off the car before any flick.
AttemptResult gradeLostBall(int requested, float time);

struct TargetGradeParams {
    Zone zone = Zone::Center;
    ZoneRect rect;
    TargetSetup setup = TargetSetup::Rolling;
    float minSpeed = 0.f; // uu/s, 0 = no requirement
    float time = 0.f;
    const FlickReport* flick = nullptr; // roof setup: the flick that produced the shot
};
AttemptResult gradeTarget(const AimResult& aim, float shotSpeed, const TargetGradeParams& p);

struct PowerShot {
    float speed = 0.f; // uu/s, ball speed after the touch
    float carSpeed = 0.f; // uu/s at the touch
    bool flip = false; // dodge into the ball
    float dodgeToTouch = -1.f; // s, < 0 when unknown
    bool sideHit = false; // contact with the side of the car
    bool onGoal = false; // predicted on target
};
float powerScore01(float kmh);
AttemptResult gradePower(const PowerShot& shot, float goalUus, float time);

// Short HUD cue of a flick type with the player's jump button, e.g. "Saut (A), nez en bas, puis backflip".
std::string flickCue(FlickType t, const PlayerProfile& profile);
// French how-to of a flick type with the player's own buttons, for prompts and the UI.
std::string flickHowTo(FlickType t, const PlayerProfile& profile);
// "Air roll (LB)" with the best air roll binding available (directional when that is what the player uses).
std::string airRollPrompt(const PlayerProfile& profile);

} // namespace rlt::shoot
