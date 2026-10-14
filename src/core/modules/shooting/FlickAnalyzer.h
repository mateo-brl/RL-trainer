#pragma once
// Flick detection and classification from the 120 Hz snapshot stream.
//
//   Idle -> Carry (car grounded, ball resting on the roof) -> Air (jump with the ball on the roof)
//        -> Dodged (dodge start while the ball is still on / near the car) -> report
//
// Timing of the snapshots (built in the SetVehicleInput pre-hook): `in` is the NEW input of the tick while the
// car flags are the state BEFORE its physics step. A dodge pressed at tick D therefore shows `in.jump` rising at
// D and `car.dodging` (with the engine dodge direction) only from D+1. The input edge opens the Dodged phase
// provisionally with the stick direction; within kConfirmTicks the engine either confirms the dodge (IsDodging,
// its direction replaces the stick one) or turns the press into a double jump (bDoubleJumped rises: the attempt
// is dropped, a double jump is not a flick). Only a dodge the engine never confirmed is flagged `uncertain`.
//
// The report is produced 0.1 s after the last ball touch following the dodge (or at a timeout), with the ball
// speed right after the flick (TouchEvent.post of the last touch), the launch direction, the timing (time on
// the roof, jump to dodge delay) and the features used by classifyFlick().
//
// Classification thresholds come from the research (mechanics-ball.md 2.3-2.6, virtual-courses.md 5.3). Most of
// them are UNVERIFIED in game: results close to a threshold are flagged `uncertain` and the main thresholds
// are exposed as advanced settings for calibration.

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "core/common/Context.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt::shoot {

// Order matters: the first 8 entries are the requestable types (settings choice "Flick demandé" = 1 + index).
enum class FlickType : int {
    Front = 0,
    Flick45,
    Flick90,
    Backflip,
    Delayed, // category: any regular flick with a long air delay and the ball kept on the car
    Musty,
    Breezi,
    Reverse, // reverse 45 / "Mawkzy": back or back-diagonal dodge with the car rolled on its side
    Diagonal, // diagonal dodge without the 45 degree turn
    Side, // side dodge without the 90 degree turn
    Other, // no clear direction (stall, double jump...)
    Count
};
constexpr int kFlickTypeCount = static_cast<int>(FlickType::Count);
constexpr int kRequestableFlickCount = 8; // Front .. Reverse

const char* flickTypeId(FlickType t); // stable id: "front", "45", "90", "backflip", "delayed", "musty", ...
const char* flickTypeLabel(FlickType t); // French display name: "Flick avant", "Flick 45°", ...
// Name for the HUD and the results, without degrees: "Flick en diagonale" (45), "Flick sur le côté" (90). The
// diagonal and side dodges without the turn say so, to stay apart from them.
const char* flickTypeHudLabel(FlickType t);
// "any"/"tout" -> -1, otherwise the FlickType index. Returns false when unknown.
bool parseFlickType(const std::string& text, int& out);

struct FlickThresholds {
    float mustyPitchDeg = -35.f; // UNVERIFIED nose-down gate at the dodge
    float breeziRollDeg = 230.f; // UNVERIFIED accumulated roll before the backflip
    float breeziAirRollFrac = 0.3f; // UNVERIFIED fraction of air ticks with air roll input
    float delayedSec = 0.6f; // UNVERIFIED jump to dodge delay of a delayed flick
    float delayedKeptFrac = 0.7f; // fraction of air ticks with the ball still on the car
    float roofReach = 260.f; // uu: ball centre to hitbox centre for "still on the car"
};

// Features measured for one flick (car-local dodge direction, rotation before the dodge, launch).
struct FlickFeatures {
    float dodgeFwd = 0.f; // +1 forward flip, -1 backflip
    float dodgeSide = 0.f; // +1 right, -1 left
    float yawTurnDeg = 0.f; // signed heading change between the jump and the dodge
    float pitchDeg = 0.f; // car pitch at the dodge (nose down < 0)
    float rollDeg = 0.f; // |accumulated roll| during the air phase
    float airRollFrac = 0.f; // fraction of air ticks with |roll input| >= 0.5
    float spinRate = 0.f; // max |angular velocity| (rad/s) in the 0.35 s before the dodge
    float underside = 0.f; // -right.z at the dodge (car rolled on its side when |.| is large)
    float launchFwd = 0.f; // ball impulse direction (flat) . heading at the jump
    float vertFrac = 0.f; // ball impulse z / |impulse|
    float delay = 0.f; // s, jump -> dodge
    float keptFrac = 1.f; // fraction of air ticks with the ball near the car
};

struct FlickClass {
    FlickType base = FlickType::Other;
    bool delayed = false;
    bool uncertain = false; // close to a threshold or dodge direction guessed from the stick
};

FlickClass classifyFlick(const FlickFeatures& f, const FlickThresholds& t);
// Category used for records and skills: Delayed for a delayed regular flick, else the base type.
FlickType categoryOf(const FlickClass& c);
// True when the flick satisfies a requested type (-1 = any).
bool flickMatches(const FlickClass& c, int requested);

// Typical jump -> dodge delay per type (UNVERIFIED estimates, research mechanics-ball.md 2.3).
struct DelayBand {
    float lo = 0.1f, hi = 0.4f;
};
DelayBand flickDelayBand(FlickType t);

enum class FlickPhase : int { Idle = 0, Carry, Air, Dodged };

struct FlickReport {
    FlickClass cls;
    FlickType category = FlickType::Other;
    FlickFeatures f;
    bool contact = false; // the dodge hit the ball
    bool inferredContact = false; // contact deduced from the ball velocity (no touch event)
    bool lostBall = false; // ball left the car before the dodge
    bool dodgeFromInput = false; // dodge direction taken from the stick (engine gave no direction)
    bool dodgeConfirmed = false; // the engine reported the dodge (IsDodging) after the press
    bool dirMismatch = false; // engine and stick directions disagree (frame of GetDodgeDirection to verify)
    float power = 0.f; // uu/s, ball speed right after the flick
    float gain = 0.f; // uu/s, power - car speed at the dodge
    float carSpeedAtJump = 0.f;
    float carSpeedAtDodge = 0.f;
    bool supersonicStart = false;
    float carryTime = 0.f; // s, ball settled on the roof (car grounded) before the jump
    float roofTime = 0.f; // s, ball on the car from settling to the dodge
    Vec3 roofOffset; // ball offset from the roof centre at the jump (car-local, x fwd, y right)
    float relSpeedAtJump = 0.f; // horizontal ball speed relative to the car at the jump
    float elevationDeg = 0.f; // launch angle above the horizontal
    float dodgeToTouch = 0.f; // s
    Vec3 launchVel; // ball velocity right after the flick
    Vec3 ballPos, ballVel, ballAngVel; // ball state at the report (free flight)
    Vec3 heading; // car heading (flat) at the jump
    int64_t dodgeFrame = 0;
    int64_t frame = 0;
};

class FlickAnalyzer {
public:
    void reset();
    void setThresholds(const FlickThresholds& t) { th_ = t; }
    const FlickThresholds& thresholds() const { return th_; }
    void setDodgeDeadzone(float dz) { deadzone_ = dz > 0.05f ? dz : 0.5f; }

    // Feeds the newest snapshot. Returns a report when a flick attempt resolves.
    std::optional<FlickReport> update(const Snapshot& s);
    // Touches resolved by the engine (pre/post states). Only those after the dodge matter.
    void onTouch(const TouchEvent& t);

    FlickPhase phase() const { return phase_; }
    bool ballOnRoofNow() const { return onRoof_; }
    Vec3 currentOffset() const { return offset_; }
    // Seconds the ball has been settled on the roof with the car grounded (0 when not carrying).
    float settledTime(int64_t nowFrame) const;

    // Ticks after the press during which the engine may confirm the dodge (IsDodging) or report a double jump.
    static constexpr int64_t kConfirmTicks = 3;

private:
    void enterIdle();
    // Opens the Dodged phase. `confirmed`: the engine flag rose this tick (else the jump press edge of this tick).
    void startDodge(const Snapshot& s, float distToCar, bool confirmed);
    // Engine confirmation of a provisional dodge. Returns false when the press became a double jump.
    bool confirmDodge(const Snapshot& s);
    FlickReport buildReport(const Snapshot& s, bool inferred);

    FlickThresholds th_;
    float deadzone_ = 0.5f;
    FlickPhase phase_ = FlickPhase::Idle;
    Snapshot prev_;
    bool hasPrev_ = false;

    // Carry
    bool onRoof_ = false;
    Vec3 offset_;
    Vec3 offsetAvg_;
    float relSpeedAvg_ = 0.f;
    int64_t carryStart_ = 0;
    int64_t lastOnRoof_ = -1000000;

    // Jump / air phase
    int64_t lastJumpPress_ = -1000000; // last rising edge of the jump button
    int64_t jumpFrame_ = 0;
    Vec3 heading0_{1.f, 0.f, 0.f};
    Vec3 travel0_{1.f, 0.f, 0.f};
    float carSpeedAtJump_ = 0.f;
    bool supersonic_ = false;
    float carryAtJump_ = 0.f;
    Vec3 offsetAtJump_;
    float relSpeedAtJump_ = 0.f;
    int airTicks_ = 0, airRollTicks_ = 0, keptTicks_ = 0, farTicks_ = 0, groundTicks_ = 0;
    bool lost_ = false;
    float rollAccum_ = 0.f;
    std::array<float, 42> spin_{};
    int spinCount_ = 0, spinHead_ = 0;

    // Dodge
    int64_t dodgeFrame_ = 0;
    FlickFeatures feat_;
    Vec3 vBallBefore_;
    float carSpeedAtDodge_ = 0.f;
    bool dodgeFromInput_ = false;
    bool dodgePending_ = false; // input edge seen, engine confirmation still expected
    bool dodgeConfirmed_ = false;
    bool dirMismatch_ = false;
    Vec3 inputDir_; // normalized stick direction at the press (car-local x fwd, y right)
    bool lostAtDodge_ = false;

    // Touches after the dodge
    bool contact_ = false;
    int64_t firstTouch_ = 0, lastTouch_ = 0, launchFrame_ = 0;
    Vec3 launchVel_;
};

} // namespace rlt::shoot
