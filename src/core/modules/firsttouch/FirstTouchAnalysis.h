#pragma once
// Pure analysis of one car-ball touch: metrics at contact, touch type, post-touch control window, score
// and coaching diagnosis. No state besides ControlWindow, no SDK: everything here is unit-tested.
//
// Physics background (research/physics.md section 6, research/training.md section 4.2):
//  - car-ball restitution is 0, the ball separates mostly through the "extra impulse" ~ 0.65 x |v_ball - v_car|,
//    so the 3D relative speed at contact decides how far the ball runs away (150 uu/s ~ 2 s to re-touch);
//  - the contact point on the ball (bottom cap for catches / air, slightly below centre on the incoming
//    face for ground touches) decides where the push goes.

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Grade.h"
#include "core/common/History.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"
#include "core/common/Units.h"

namespace rlt::ft {

constexpr float kBallRestZ = 93.15f;      // ball centre at rest on the floor
constexpr float kGroundBallHeight = 150.f; // "near ground": ball centre < rest + 150
// Below this horizontal speed the ball counts as still: its travel direction is the car -> ball direction.
constexpr float kMovingBallSpeed = 50.f;

// ---- Classification labels ----------------------------------------------------------------------------
enum class TouchType : int { Ground = 0, Bounce = 1, Air = 2, Wall = 3, Dribble = 4 };
constexpr int kTouchTypeCount = 5;

const char* touchTypeId(TouchType t);     // "ground", "bounce", "air", "wall", "dribble" (skill ids)
const char* touchTypeLabel(TouchType t);  // "sol", "rebond", "aérien", "mur", "dribble"
const char* touchTypeTitle(TouchType t);  // "Sol", "Rebond", "Aérien", "Mur", "Dribble"

enum class CarPart : int { Nose = 0, Roof = 1, Side = 2, Back = 3, Under = 4 };
const char* carPartLabel(CarPart p);      // "nez", "toit", "côté", "arrière", "dessous"

enum class ContactHeight : int { Low = 0, Middle = 1, High = 2 };
const char* contactHeightLabel(ContactHeight h);  // "bas", "centre", "haut"

enum class SpeedZone : int { Good = 0, Ok = 1, Bad = 2 };
SpeedZone speedZone(float speedDiff, float goodZone, float okZone);
const char* zoneTag(SpeedZone z);         // "[BON]", "[MOYEN]", "[MAUVAIS]"

// ---- Car hitbox ---------------------------------------------------------------------------------------
struct Obb {
    Vec3 center;
    Basis basis;
    Vec3 half;  // half extents along fwd / right / up
};
Obb carHitbox(const CarState& car);
Vec3 closestPointOnObb(const Obb& box, const Vec3& p);
// Ball resting / bouncing on the roof (research BallOnCar envelope, without the speed condition).
bool ballOnRoof(const CarState& car, const BallState& ball);
// Ball sitting on the roof of a driving (not airborne) car and moving with it (vertical speeds within
// `maxRelVz`): a carry or a ball put on the roof ("ballontop"), as opposed to a ball falling onto the roof
// (a catch) or a car flying up into a ball (an air touch).
bool ballCarriedOnRoof(const CarState& car, const BallState& ball, float maxRelVz = 150.f);
// Car driving on a wall (engine flag, or wheels down with a mostly horizontal up axis).
bool carOnWall(const CarState& car);

// ---- Contact metrics ----------------------------------------------------------------------------------
struct TouchMetrics {
    bool valid = false;
    float speedDiff = 0.f;       // |v_car.xy| - |v_ball.xy| at pre (signed, + = car faster): FTT parity
    float relSpeed = 0.f;        // |v_ball - v_car| (3D) at pre: what the game's extra impulse uses
    float along = 0.f;           // (v_car - v_ball).xy along the ball travel direction (+ = car faster)
    float cross = 0.f;           // remaining horizontal mismatch (approach angle error), >= 0
    float vertDiff = 0.f;        // v_car.z - v_ball.z
    float closingSpeed = 0.f;    // (v_car - v_ball) . n with n the contact normal (car -> ball)
    float approachAngleDeg = 0.f;
    // Ball travel relative to the car, only when the ball really moves (|v_ball.xy| > kMovingBallSpeed):
    // a ball crossing the car's path is neither coming nor going away.
    bool ballComing = false;     // ball rolling / flying towards the car (head-on reception)
    bool ballAway = false;       // ball rolling / flying away from the car (chase)
    Vec3 contactDir;             // unit vector, ball centre -> contact point (world)
    Vec3 contactPoint;           // world point on the ball surface
    Vec3 normal;                 // unit contact normal, car -> ball (= -contactDir)
    bool fromHitInfo = false;    // contact taken from the OnHitBall hook (else hitbox geometry)
    float contactHeight01 = 0.5f;  // 0 = bottom of the ball, 1 = top
    ContactHeight height = ContactHeight::Middle;
    CarPart part = CarPart::Nose;
    float lateralOffset = 0.f;   // ball centre offset along the car right axis (uu, + = right)
    Vec3 surfaceUp{0.f, 0.f, 1.f};  // "up" used for the contact height (wall normal for wall touches)
    float contactUp = 0.f;       // contactDir . surfaceUp (-1 bottom of the ball .. +1 top)
    Vec3 desiredDir;             // ideal contact direction for the touch type
    float contactErrDeg = 0.f;   // angle(contactDir, desiredDir)
    float ballPop = 0.f;         // post ball vz - post car vz (uu/s)
    float separation = 0.f;      // |v_ball - v_car| at post (uu/s)
    float noseAimErrDeg = 0.f;   // angle(car forward, ball - nose point)
    bool boostAtContact = false;
    // Contact point in the approach frame (heatmap): x along the car's approach direction, z up, y to the right.
    float heatX = 0.f, heatY = 0.f, heatZ = 0.f;
};

// Ideal contact direction (ball centre -> contact) for a touch type.
//  approach: horizontal direction of the car's motion relative to the ball (unit, or zero),
//  up: surface normal the car drives on (world z on the ground, wall normal on walls).
Vec3 desiredContactDir(TouchType type, const Vec3& approach, const Vec3& up, const Vec3& ballTravel);

// Fills every metric from pre/post (and the hook's hit info when plausible).
TouchMetrics computeMetrics(const TouchEvent& touch, TouchType type);

// ---- Touch type ---------------------------------------------------------------------------------------
// True when the ball bounced on the floor during the `ticks` ticks before `frame` (vz flipped near the ground).
bool ballBouncedRecently(const TickHistory& history, int64_t frame, int ticks = 42);

// Classifies a touch from the pre-touch state.
//  - continued contact (not newContact), or a follow-up contact less than `dribbleGapTicks` after the previous
//    one (dribble, air dribble, juggling), or a ball carried on the roof (ballCarriedOnRoof, e.g. after a
//    "ballontop") -> Dribble: counted, not graded as a first touch;
//  - car on a wall -> Wall; car airborne with a high ball -> Air;
//  - ball near ground: rolling -> Ground, descending / just bounced -> Bounce.
TouchType classifyTouch(const Snapshot& pre, bool newContact, int ticksSincePrevTouch, bool recentBounce,
                        int dribbleGapTicks = 120);

// ---- Control window -----------------------------------------------------------------------------------
struct ControlSummary {
    int ticks = 0;               // evaluated ticks (car and ball valid)
    int controlledTicks = 0;
    float fraction = 0.f;        // controlledTicks / ticks
    bool controlled = false;     // fraction >= required share
    bool lost = false;           // out of the envelope at the end, for a while
    float popHeight = 0.f;       // max ball z in the window - ball z at the touch
    float maxSteerAfter = 0.f;   // max |steer| during the first 0.5 s
    float maxDistance = 0.f;     // max ball - hitbox centre distance
};

// Follows the ticks after a first touch. Feed snapshots with strictly increasing frames.
class ControlWindow {
public:
    void start(const TouchEvent& touch, int windowTicks, float radius, float maxRelSpeed, float requiredFraction);
    // Feeds one snapshot; ignored when its frame is not after the last fed one. Returns true once complete.
    bool feed(const Snapshot& s);
    void cancel() { active_ = false; }

    bool active() const { return active_; }
    bool complete() const { return active_ && elapsedTicks_ >= windowTicks_; }
    int64_t startFrame() const { return startFrame_; }
    int64_t lastFrame() const { return lastFrame_; }
    int elapsedTicks() const { return elapsedTicks_; }
    int windowTicks() const { return windowTicks_; }
    ControlSummary summary() const;

private:
    bool active_ = false;
    int64_t startFrame_ = 0;
    int64_t lastFrame_ = 0;
    int windowTicks_ = 120;
    int elapsedTicks_ = 0;
    float radius_ = 350.f;
    float maxRel_ = 450.f;
    float required_ = 0.6f;
    float startBallZ_ = 0.f;
    int ticks_ = 0;
    int controlled_ = 0;
    int trailingOut_ = 0;
    bool lastIn_ = true;
    float maxBallZ_ = 0.f;
    float maxSteer_ = 0.f;
    float maxDist_ = 0.f;
};

// ---- Score --------------------------------------------------------------------------------------------
struct ScoreParams {
    float speedGood = 150.f, speedBad = 600.f;
    float contactGoodDeg = 30.f, contactBadDeg = 70.f;
    float wSpeed = 0.35f, wContact = 0.25f, wControl = 0.40f;  // normalised by their sum
    float lostCap = 40.f;  // max score when the ball is lost
};

struct TouchScore {
    float sSpeed = 0.f, sContact = 0.f, sControl = 0.f;  // 0..1
    float score = 0.f;                                    // 0..100 (rounded)
};

TouchScore scoreTouch(const TouchMetrics& m, const ControlSummary& c, const ScoreParams& p);
// Score of the touch alone, 0..100: the speed and contact parts with their weights renormalised, whatever happens
// to the ball afterwards (no control part, no lost cap). First step of the drills.
float touchOnlyScore(const TouchScore& s, const ScoreParams& p);

// ---- Diagnosis ----------------------------------------------------------------------------------------
struct DiagnosisParams {
    float tooFast = 300.f;     // along-track threshold (uu/s)
    float angle = 300.f;       // cross-track threshold (uu/s)
    float lateral = 45.f;      // uu
    float pop = 300.f;         // ball pop (uu/s)
    float popHeight = 250.f;   // uu
    float contactHighZ = 0.1f; // contactDir.z above which a ground contact is "too high"
};

// Tips in French (imperative, one short sentence in plain words, no numbers: the measures stay in the tab) sorted
// by weight (points lost), most important first. Uses the player's real buttons (actionPrompt with the chosen
// button style). Speed tips along the ball travel: car faster -> "too_fast"; ball coming at the car -> "head_on";
// ball going away (or crossing while really faster than the car) -> "too_slow"; a crossing ball otherwise ->
// "angle" only.
std::vector<Tip> diagnose(TouchType type, const TouchMetrics& m, const ControlSummary& c, const TouchScore& s,
                          const ScoreParams& sp, const DiagnosisParams& dp, const PlayerProfile& profile,
                          KeyStyle style);

// Short feedback headline for a score ("Belle touche !", "Bonne touche", "Touche correcte", "Touche ratée").
const char* scoreHeadline(float score);

// ---- Text helpers -------------------------------------------------------------------------------------
const char* unitLabel(SpeedUnit unit);  // "km/h", "mph", "uu/s"
// "+3.2 km/h" (sign when `withSign`, values within +-1 uu/s print as 0).
std::string formatSpeedValue(float uus, SpeedUnit unit, int decimals = 1, bool withSign = true);
// "[BON] Écart +3.2 km/h, note 84"
std::string chatTouchLine(float speedDiff, float score, SpeedZone zone, SpeedUnit unit, bool withScore);
// "[FLIP RESET] Écart +1.2 km/h [BON]"
std::string chatFlipResetLine(float speedDiff, SpeedZone zone, SpeedUnit unit);

} // namespace rlt::ft
