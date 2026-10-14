#pragma once
// "Parcours complet" of the shooting drills (cross-cutting fix 2): the whole match situation, every step checked in
// order by the shared StepTracker.
//   Flick:    shared "Ramassage" start at speed (pickup R1, R2), "Porte-la" (carry while driving), "Flick", "Cadre".
//   Roof:     shared "Ramassage" start at speed, "Dribble vers le but", "Flick", "Dans la zone".
//   Rolling:  ball at midfield, "Va à la balle sans la frapper", "Pousse-la vers le but", "Tire", "Dans la zone".
//   Bouncing: high drop in front of the goal, "Lis les rebonds", "Arrive au rebond", "Tire en montée", "Dans la zone".
//   Pass:     cross from the corner, "Démarre au bon moment", "Coupe la trajectoire", "Tire du premier coup", zone.
//   Power:    ball from the opponent half, "Prends de l'élan", "Aligne", "Flip dans la balle", "Puissance".
// The steps whose verdict comes from an event (a flick report, a touch, the shot's outcome, the power measure) have no
// check: the module completes or fails them itself (ids below). The others read a FullState the module refreshes on
// every tick.

#include <string>
#include <vector>

#include "core/common/Pickup.h"
#include "core/common/Steps.h"
#include "core/modules/shooting/ShootGeometry.h"
#include "core/modules/shooting/ShootingDrills.h"
#include "core/modules/shooting/ShootingTutorials.h"

namespace rlt::shoot {

// Ids of the steps the module resolves from events.
inline constexpr const char* kStepFlick = "flick";    // a flick report with contact
inline constexpr const char* kStepCadre = "cadre";    // on goal and strong enough
inline constexpr const char* kStepZone = "zone";      // the predicted hit in the zone
inline constexpr const char* kStepFirst = "first";    // rolling: first touch soft enough
inline constexpr const char* kStepShot = "shot";      // rolling: a shot (fast, with a crossing)
inline constexpr const char* kStepVolley = "volley";  // bouncing: a shot just after a bounce
inline constexpr const char* kStepOneTouch = "onetouch";  // pass: the first touch is the shot
inline constexpr const char* kStepDodge = "dodge";    // power: front flip into the ball
inline constexpr const char* kStepPower = "power";    // power: speed over the bar

constexpr float kCarryMinSpeed = 300.f;    // "Porte-la": uu/s
constexpr float kDribbleMinSpeed = 500.f;  // "Dribble vers le but"
constexpr float kDribbleMaxSpeed = 1400.f;  // the throttle alone holds up to about 1410 uu/s
constexpr float kShotSpeed = 1200.f;       // a touch that is a shot
constexpr float kFirstTouchMaxRel = 600.f; // rolling: "sans la frapper"
constexpr float kRunUpSpeed = 1600.f;      // power: at 800 uu from the ball
constexpr float kVolleyWindow = 0.3f;      // s after a floor bounce

struct FullSetup {
    ShootDrill kind = ShootDrill::Flick;
    CarPlacement car;            // at rest
    BallPlacement ball;          // at GO (velocity included); held still at this position before GO
    bool pickup = false;         // shared Ramassage start (flick, roof)
    bool rolling = false;        // the pickup ball rolls toward the car
    bool chase = false;          // pickup at speed: the car drives at GO after the ball rolling ahead (car.vel)
    Vec3 target;                 // pass: meeting point; bouncing: second bounce point; power: meeting point
    float targetTime = -1.f;     // s after GO the ball is there
    Vec3 aim;                    // pass: where the cross is aimed (penalty spot)
    std::vector<FloorBounce> bounces;  // bouncing: the predicted floor bounces
    float timeLimit = 16.f;      // s after GO for the whole parcours
};

// `pickupNiveau`: niveau of the shared pickup staircase. Flick and roof: the pickup at speed (pickup::makeChaseSetup)
// from 5500 uu of the goal line, like a match; the flick must still come before kFlickLateDist.
FullSetup makeFullSetup(ShootDrill kind, float level, int pickupNiveau, const GoalFrame& goal, const RoofParams& roof,
                        Rng& rng);

// Live facts the checks read; refreshed by the module every tick (fullTick) before StepTracker::update.
struct FullState {
    FullSetup setup;
    ShootDrill kind = ShootDrill::Flick;
    int niveau = 1;
    GoalFrame goal;
    ZoneRect zone;               // target zone (roof, rolling, bouncing, pass)
    float goTime = 0.f;
    float now = 0.f;
    Hitbox hb;
    float ballRadius = kBallRadius;
    // Ball.
    int floorBounces = 0;
    float lastBounceTime = -1.f;
    bool hasPrev = false;
    float prevBallVz = 0.f;
    // Car touches since GO.
    int touches = 0;

    float elapsed() const { return now - goTime; }
};

void fullBegin(FullState& st, const FullSetup& setup, int niveau, const GoalFrame& goal, const ZoneRect& zone,
               const Hitbox& hb, float ballRadius, float goTime);
void fullTick(FullState& st, const Snapshot& s);

// The shooting roof test used by the pickup steps (the module's own ballOnRoof, tolerances 15 / 40 / 15).
bool shootBallOnRoof(const Snapshot& s);
// Options of the shared pickup steps for the shooting drills (codes, and labels of the rolling ball or the chase).
pickup::Options pickupOptions(bool rolling, bool chase = false);

// Steps of the full sequence (checks keep a pointer to `st`), pickup steps first for the flick and the roof.
std::vector<Step> fullSteps(FullState& st);
// Plain labels of the steps (tab), numbered like the HUD.
std::vector<std::string> fullLabels(ShootDrill kind, int niveau);

} // namespace rlt::shoot
