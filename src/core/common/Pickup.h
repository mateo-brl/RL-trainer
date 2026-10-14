#pragma once
// Shared "Ramassage" start (cross-cutting fix 4) of the drills that need the ball on the roof first: Flick, Musty,
// Breezi, Air dribble (Mécas balle), Flick (Tirs), Dribble / Slalom / Anneaux air dribble (Parcours).
//
// Setup: the car starts stopped. Niveaux 1-3: the ball rests 600 uu ahead of the car. Niveau 4 and up: the ball
// rolls toward the car at 350..450 uu/s from 1500 uu ahead (with the matching rolling spin).
// Steps (group "pickup"):
//   R1 "Ramasse la balle sur ton toit": ball on the roof for 0.5 s, within 8 s.
//   R2 "Garde-la au centre du toit": still on the roof, speed relative to the car < 150 uu/s and centred within
//      40 uu (along the car) of the hitbox centre, for 0.3 s.
// A failed pickup feeds the pickup's own staircase (pickup::Progress), never the mechanic's.
// UNVERIFIED in game: the approach speed (~200-500 uu/s) at which a resting ball climbs the hood.
//
// Positions are given by the module (already mirrored for its team); the helper never clamps them to the field.

#include <functional>
#include <string>

#include "core/common/Actions.h"
#include "core/common/Adaptive.h"
#include "core/common/Rng.h"
#include "core/common/Snapshot.h"
#include "core/common/Steps.h"

namespace rlt::pickup {

constexpr int kRollNiveau = 4;          // from this niveau the ball rolls toward the car
constexpr float kRestAhead = 600.f;     // uu from the car origin to the resting ball centre, along the nose
constexpr float kRollAhead = 1500.f;    // uu from the car origin to the rolling ball's start
constexpr float kRollSpeedMin = 350.f;  // uu/s
constexpr float kRollSpeedMax = 450.f;
constexpr float kArenaMargin = 1.9f;    // resting ball centre = radius + 1.9 (93.15 for the Soccar ball)
constexpr float kMaxSpin = 6.f;         // rad/s, the game's ball angular speed cap

constexpr float kRoofHold = 0.5f;       // R1
constexpr float kRoofTimeout = 8.f;
constexpr float kSettleHold = 0.3f;     // R2
constexpr float kSettleTimeout = 6.f;
constexpr float kSettleRelSpeed = 150.f;
constexpr float kSettleCentre = 40.f;
constexpr float kDropZ = 20.f;          // R2: ball centre lower than radius + 20 = on the floor

inline constexpr const char* kGroup = "pickup";
inline constexpr const char* kStepRoof = "pickup.roof";
inline constexpr const char* kStepSettle = "pickup.settle";

// Default French texts (plain words, tutoiement).
inline constexpr const char* kRoofLabel = "Ramasse la balle sur ton toit";
inline constexpr const char* kSettleLabel = "Garde-la au centre du toit";
inline constexpr const char* kRoofTip =
    "Roule doucement vers la balle, nez bien droit : elle monte sur le capot puis sur le toit.";
inline constexpr const char* kSettleTip =
    "Garde la balle au centre du toit : si elle glisse vers l'arrière, lâche un peu l'accélérateur ; si elle glisse "
    "vers l'avant, accélère un peu.";
inline constexpr const char* kRoofLabelRolling = "Va chercher la balle et ramasse-la sur ton toit";
inline constexpr const char* kRoofTipRolling =
    "Avance doucement vers la balle qui arrive, nez bien droit : elle monte sur le capot puis sur le toit.";
inline constexpr const char* kDropTip = "La balle est tombée du toit : roule plus doucement pour la garder.";
inline constexpr const char* kJumpTip = "Attends que la balle soit stable au centre du toit avant de sauter.";

struct Setup {
    CarPlacement car;        // stopped at the given pose (boost left unchanged: the module sets it)
    BallPlacement ball;      // the ball at GO: resting, or rolling toward the car with its rolling spin
    BallPlacement heldBall;  // the same position with zero velocity, to hold during the get-ready phase
    bool rolling = false;
    float rollSpeed = 0.f;   // uu/s (0 when resting)
};

// `carPos` (z included, e.g. 17) and `yawDeg` give the car pose; `niveau` 1..10 picks resting (1-3) or rolling (4+);
// `rng` picks the rolling speed (not used at niveaux 1-3).
Setup makeSetup(const Vec3& carPos, float yawDeg, int niveau, Rng& rng, float ballRadius = 91.25f);

// Spin of a ball rolling on the floor without slipping at `vel` (same convention as the prediction physics).
Vec3 rollingSpin(const Vec3& vel, float ballRadius = 91.25f);

// ---- Geometry ----------------------------------------------------------------------------------------------
// Ball resting on the roof: car roughly upright, ball centre above the roof rectangle (+ margin) and between 15 uu
// under and `tolUp` above its resting height.
bool ballOnRoof(const Snapshot& s, float tolUp = 40.f, float margin = 15.f);
// Speed of the ball relative to the car, in the roof plane (uu/s).
float relativePlanarSpeed(const Snapshot& s);
// Ball position along the car minus the hitbox centre (uu, > 0 toward the nose).
float roofOffsetX(const Snapshot& s);
// R2 condition on one tick: `onRoof` and slow and centred.
bool ballSettled(const Snapshot& s, bool onRoof);

// ---- Steps -------------------------------------------------------------------------------------------------
struct Options {
    // The module's own roof test (ballmech carryGeometry, shooting ballOnRoof...); empty = pickup::ballOnRoof.
    std::function<bool(const Snapshot&)> onRoof;
    float roofTimeout = kRoofTimeout;
    float settleTimeout = kSettleTimeout;
    std::string roofLabel = kRoofLabel;
    std::string settleLabel = kSettleLabel;
    std::string roofCode = "pickup.no_roof";       // R1 missed (e.g. AD_NO_CATCH for the air dribble)
    std::string settleCode = "pickup.unstable";    // R2 missed
    std::string roofTip = kRoofTip;
    std::string settleTip = kSettleTip;
    bool failOnDrop = true;                        // R2 fails at once when the ball falls on the floor
    std::string dropCode = "pickup.dropped";
    std::string dropTip = kDropTip;
    std::string jumpCode;                          // non empty: a jump during R2 fails with it (FL_JUMP_UNSETTLED)
    std::string jumpTip = kJumpTip;
};

// Default options with the R1 label and tip that match the setup (resting or rolling ball).
Options options(bool rolling);

Step roofStep(const Options& o = {});
Step settleStep(const Options& o = {});
// Appends R1 then R2 to the tracker (call it first: the pickup is the start of the parcours).
void addSteps(StepTracker& t, const Options& o = {});

// ---- Own staircase -----------------------------------------------------------------------------------------
// The pickup's difficulty (resting or rolling ball) follows its own staircase. Call beginAttempt() when an attempt
// starts and update() after every StepTracker::update(): it records one success when both pickup steps are done and
// one failure when the tracker failed on a pickup step, at most once per attempt.
class Progress {
public:
    explicit Progress(float startLevel = 0.f) : stairs_(0.75f, 0.03f, startLevel) {}

    void beginAttempt() { recorded_ = false; }
    // +1 when it recorded a success on this call, -1 a failure, 0 otherwise.
    int update(const StepTracker& t);
    bool recorded() const { return recorded_; }

    int niveau() const;  // 1..10
    const Staircase& staircase() const { return stairs_; }
    Staircase& staircase() { return stairs_; }
    void reset(float startLevel = 0.f) { stairs_ = Staircase(0.75f, 0.03f, startLevel); recorded_ = false; }

    void save(Store& st, const std::string& key) const { stairs_.save(st, key); }
    void load(const Store& st, const std::string& key) { stairs_.load(st, key); }

private:
    Staircase stairs_;
    bool recorded_ = false;
};

} // namespace rlt::pickup
