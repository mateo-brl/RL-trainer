#pragma once
// "Parcours complet" of the first-touch drills: the whole situation of a match (a clearance rolling back at you, a
// teammate's pass, an opponent's lob, a ball coming off the wall...) with every step checked in order by the shared
// StepTracker (cross-cutting fix 2): read the ball, get placed, slow down, the touch itself, keep the ball.
//
// The setups start from the drill anchor (car at rest in the player's half, facing the opponent goal). The ball's
// path is predicted once at the release (light predictor of FirstTouchDrills) to place the circles and the targets
// of the steps. The touch step has no check of its own: the module validates it from the touch event
// (fullTouchVerdict), and the "Garde-la" step from the control window of the touch analysis.
//
// Where the audit's figures were impossible in the game's physics, the setup is the closest playable one (see the
// comments of makeFullSetup and the tab's "Détails techniques").

#include <string>
#include <vector>

#include "core/common/Steps.h"
#include "core/modules/firsttouch/FirstTouchAnalysis.h"
#include "core/modules/firsttouch/FirstTouchDrills.h"

namespace rlt::ft {

// Step ids the module drives itself.
inline constexpr const char* kFullTouchStep = "touch";  // validated by fullTouchVerdict at the first touch
inline constexpr const char* kFullKeepStep = "keep";    // validated by the control window (niveau 4+)
constexpr float kFullKeepFraction = 0.6f;               // ball close for 60 % of the window
constexpr int kMatchBalls = 5;                          // Mix: séquence de match
constexpr int kMatchToPass = 4;
constexpr float kMatchGap = 2.f;                        // s between two balls of the séquence de match
constexpr float kMatchRoom = 3000.f;                    // uu of room in the direction a ball comes from

struct FullSetup {
    DrillType type = DrillType::RollIn;
    DrillPose car;                 // car pose, placed at rest
    BallPlacement ball;            // ball at GO (velocity included); held still at this position before GO
    BallTrack track;               // predicted path from GO (6 s)
    Vec3 landing;                  // first floor contact of an airborne ball (lob, wall, high)
    float landingTime = -1.f;      // s after GO, -1 = none
    Vec3 target;                   // the point the steps use: meeting point, where the ball comes down slowly...
    float targetTime = -1.f;       // s after GO when the ball is there
    int bouncesBeforeTarget = 0;   // floor bounces of the ball before it reaches the target
    float timeLimit = 12.f;        // s after GO for the whole parcours
};

// Setup of the full sequence of `t` (not Mix) from the player's pose (only its x is kept, like the drill anchor).
FullSetup makeFullSetup(DrillType t, const DrillPose& player, int team, float level, Rng& rng);

// Live facts the step checks read; the module refreshes them on every tick (fullTick) before StepTracker::update.
struct FullState {
    FullSetup setup;
    DrillType type = DrillType::RollIn;
    int niveau = 1;
    float goTime = 0.f;
    float now = 0.f;
    // Ball.
    int floorBounces = 0;          // floor bounces since GO
    bool hasPrev = false;
    float prevBallVz = 0.f;
    // Touch.
    bool touched = false;
    int bouncesAtTouch = -1;
    float touchTime = -1.f;
    bool airborneAtTouch = false;
    // Landing after the touch (balle haute).
    bool airborneSinceTouch = false;
    bool landedAfterTouch = false;
    float ballDistAtLanding = 1e9f;

    float elapsed() const { return now - goTime; }
};

void fullBegin(FullState& st, const FullSetup& setup, DrillType t, int niveau, float goTime);
void fullTick(FullState& st, const Snapshot& s);
// The first car touch of the rep (pre-touch state).
void fullOnTouch(FullState& st, const Snapshot& pre);

// The steps of the full sequence (checks keep a pointer to `st`).
std::vector<Step> fullSteps(FullState& st);
// Plain step labels (tab, tests), numbered like the HUD ("1/4 ...").
std::vector<std::string> fullLabels(DrillType t, int niveau);
// Verdict of the touch step from the first touch: done, or failed with the code and tip of what was wrong.
StepVerdict fullTouchVerdict(const FullState& st, const TouchMetrics& m, const Snapshot& pre);
// Tip given when the ball is touched while an earlier step is still the current one.
std::string fullEarlyTouchTip(DrillType t, const std::string& stepId);

// ---- Séquence de match (Mix) --------------------------------------------------------------------------------
// The pose a ball of the séquence de match comes from: the car's current position (kept on the field) and the
// direction it drives (or faces), turned toward the field when there is less than kMatchRoom uu ahead.
DrillPose matchPose(const CarState& car);

} // namespace rlt::ft
