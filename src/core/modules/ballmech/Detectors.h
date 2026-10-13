#pragma once
// The eight detector families (one .cpp each). Ported from the tested research reference (ballmech_ref.hpp),
// adapted to the repo types: touches come from the engine's TouchEvent (marked on the tick they happened),
// arena contacts are separated from ball contacts by primitives, and goal accuracy follows the car's team.
//
// Thresholds marked UNVERIFIED are reasoned estimates to calibrate in game (see the module report).

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "core/modules/ballmech/Detector.h"

namespace rlt::bm {

// ================================================================================================
// Flip reset: acquisition (game event, wheels on the ball, flip regained near the ball) then use
// (dodge or jump into a second touch). Also grades near misses (underside touch without reset).
// ================================================================================================
class FlipResetDetector final : public Detector {
public:
    Family family() const override { return Family::FlipReset; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

    bool armed() const { return state_ == State::Armed; }
    // Source of the reset acquired during the last update: 0 none, 1 game event, 2 wheels on the ball,
    // 3 flip regained near the ball.
    int acquiredSource() const { return acquiredSource_; }
    int chain() const { return chain_; }

private:
    enum class State { Idle, Armed, Converting };
    MechGrade finish(const Frame& cur, const char* outcome, bool converted);
    void clearAttempt();

    State state_ = State::Idle;
    float tStart_ = 0.f, tReset_ = 0.f, tDodge_ = 0.f, tNearMiss_ = 0.f, lastAcquireT_ = -1e9f, lastDodgeT_ = -1e9f;
    int source_ = 0, chain_ = 0, acquiredSource_ = 0;
    int64_t evalAt_ = 0;
    float alignment_ = 0.f, relSpeed_ = 0.f, nmAlign_ = 0.f, nmRel_ = 0.f, ballSpeedAfter_ = 0.f;
    bool usedDoubleJump_ = false, viaJump_ = false, touchSeen_ = false, nearMissArmed_ = false, prevActive_ = false;
    GoalProjection proj_;
};

// ================================================================================================
// Ceiling shot: wheels on the ceiling, drop without jumping (flip kept), flip into the ball.
// ================================================================================================
class CeilingShotDetector final : public Detector {
public:
    Family family() const override { return Family::Ceiling; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

private:
    enum class State { Idle, OnCeiling, Falling };
    std::optional<MechGrade> finish(const Frame& cur, bool touched);

    State state_ = State::Idle;
    float tCeilStart_ = 0.f, tLeave_ = 0.f, tDodge_ = -1.f, ceilTime_ = 0.f, separation_ = 0.f, ballSpeed_ = 0.f;
    float minBallDist_ = 1e9f;
    bool jumpedOff_ = false, touchSeen_ = false, flipShot_ = false;
    int64_t evalAt_ = 0;
    GoalProjection proj_;
};

// ================================================================================================
// Flick family: front, 45, 90, backflip, side, reverse ("Mawkzy"), musty, breezi, "+ delayed".
// ================================================================================================
class FlickDetector final : public Detector {
public:
    explicit FlickDetector(std::string target = "") : target_(std::move(target)) {}
    Family family() const override { return Family::Flick; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

    // Requested variant in a drill ("" = any, "45", "90", "front", "musty", "breezi", ...).
    void setTarget(const std::string& t) { target_ = t; }
    const std::string& target() const { return target_; }

private:
    enum class State { Idle, Carry, Air, Dodged };
    std::string classify(float launchFwd, float vertFrac) const;
    MechGrade failGrade(const Frame& cur, const char* code);
    MechGrade grade(const Frame& cur);
    void restart();

    std::string target_;
    State state_ = State::Idle;
    float tCarry_ = 0.f, lastGeo_ = 0.f, tJump_ = 0.f, tDodge_ = -1.f, tTouch_ = 0.f;
    int nCarry_ = 0, airTicks_ = 0, airRollTicks_ = 0, keptTicks_ = 0, farTicks_ = 0;
    int64_t evalAt_ = 0;
    float sumVz_ = 0.f, avgVz_ = 0.f, roofZ_ = 130.f, carryTime_ = 0.f, relHAtJump_ = 0.f, rollAccum_ = 0.f;
    float pitchDeg_ = 0.f, yawDeg_ = 0.f, underside_ = 0.f;
    bool supersonicStart_ = false, touchSeen_ = false, lost_ = false;
    Vec3 heading0_, travel0_, dodge_, vBefore_;
};

// Jump-to-dodge delay band (seconds) of a flick variant. UNVERIFIED estimates.
struct FlickBand {
    float lo, hi;
};
FlickBand flickDelayBand(const std::string& variant);

// ================================================================================================
// Double tap: aerial touch onto the opponent backboard, read the rebound, second touch before landing.
// ================================================================================================
class DoubleTapDetector final : public Detector {
public:
    Family family() const override { return Family::DoubleTap; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;
    // Drill "read" mode: the drill itself sent the ball to the backboard; wait for the bounce from now.
    void armReadMode(float now);

private:
    enum class State { Idle, FirstTouch, AwaitBounce, AwaitSecond, Eval };
    MechGrade finish(const Frame& cur, const char* failCode);

    State state_ = State::Idle;
    float tFirst_ = 0.f, tBounce_ = 0.f, tSecond_ = 0.f, bounceZ_ = 0.f, carDistAtBounce_ = 0.f;
    int64_t evalAt_ = 0;
    bool readMode_ = false, wasAirborne_ = false;
};

// ================================================================================================
// Air dribble: ground-to-air or wall-to-air, carry or bump style; ends when the ball is lost.
// ================================================================================================
class AirDribbleDetector final : public Detector {
public:
    Family family() const override { return Family::AirDribble; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

private:
    std::optional<MechGrade> finish(const Frame& cur, const char* reason);
    void startSession(const Frame& cur, bool carAir);

    bool active_ = false, wasAirborne_ = false, lastOnTarget_ = false, lastOnTargetValid_ = false, wallOrigin_ = false;
    float tFirst_ = 0.f, tLast_ = 0.f, tLastCounted_ = -1e9f;
    int touches_ = 0, airTouches_ = 0, distN_ = 0, behindTicks_ = 0, airTicks_ = 0;
    int distNAtLast_ = 0, behindAtLast_ = 0, airTicksAtLast_ = 0;
    int64_t evalAt_ = 0;
    float distSum_ = 0.f, maxDist_ = 0.f, distSumAtLast_ = 0.f, maxDistAtLast_ = 0.f;
    float boostStart_ = 0.f, minBoost_ = 0.f, startZ_ = 0.f, maxZ_ = 0.f, travel_ = 0.f, travelAtLast_ = 0.f;
    Vec3 lastBallPos_;
    int intervalCount_ = 0;
    std::array<float, 64> intervals_{};
    GoalProjection lastProj_;
};

// ================================================================================================
// Aerial touch: aerial shot, air roll shot (directional air roll control), redirect; accuracy of the hit point.
// ================================================================================================
class AerialDetector final : public Detector {
public:
    Family family() const override { return Family::Aerial; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

private:
    MechGrade grade(const Frame& cur, float speed);

    static constexpr int kRollWindow = 60;  // 0.5 s
    std::array<signed char, kRollWindow> rollHist_{};
    int rollHead_ = 0, rollCount_ = 0;
    bool pending_ = false;
    int64_t evalAt_ = 0;
    float tTouch_ = 0.f, offAxis_ = 0.f, airRollFrac_ = 0.f, rollConsistency_ = 0.f, impactErr_ = -1.f;
    int rollDir_ = 0;
    Vec3 vIn_;
};

// ================================================================================================
// Pinch: ball squeezed against the floor, a wall (Kuxir), the ceiling, the back wall or a corner.
// ================================================================================================
class PinchDetector final : public Detector {
public:
    Family family() const override { return Family::Pinch; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

private:
    bool pending_ = false, kuxir_ = false;
    int ticks_ = 0;
    float tTouch_ = 0.f, gap_ = 0.f, push_ = 0.f, maxSpeed_ = 0.f;
    Surface surface_ = Surface::None;
};

// ================================================================================================
// Power shot: strong ground hit, flip shot, powerslide shot.
// ================================================================================================
class PowerShotDetector final : public Detector {
public:
    Family family() const override { return Family::PowerShot; }
    void reset() override;
    std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) override;
    bool engaged(float& since) const override;

private:
    MechGrade grade(const Frame& cur, float exitSpeed, bool flipShot);

    static constexpr int kYawWindow = 60;
    std::array<float, kYawWindow> yawHist_{};
    int yawHead_ = 0, yawCount_ = 0;
    bool pending_ = false, slideHeld_ = false;
    float tDodge_ = -1e9f, tSlide_ = -1e9f, tTouch_ = 0.f, lastCarry_ = -1e9f;
    int64_t evalAt_ = 0;
    float carSpeed_ = 0.f, dodgeAgo_ = -1.f, slideAgo_ = -1.f, maxYaw_ = 0.f;
    Vec3 local_;
};

} // namespace rlt::bm
