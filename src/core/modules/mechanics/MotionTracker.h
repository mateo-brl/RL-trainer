#pragma once
// MotionTracker: per-tick edge detection (jump, double jump, dodge, landing, takeoff) and airborne-phase
// bookkeeping shared by every detector. One pass per physics tick.

#include <cstdint>

#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

struct TickEvents {
    bool valid = false;   // false on the first tick and right after a reset
    bool reset = false;   // teleport / freeplay reset / tick gap: detectors drop their state
    bool jumpPressed = false, jumpReleased = false;
    bool firstJump = false;   // bJumped rising edge (jump started from a surface)
    bool doubleJump = false;  // bDoubleJumped rising edge
    bool dodgeStart = false;  // IsDodging rising edge (stall included)
    bool landed = false;      // wheels < 3 -> >= 3
    bool tookOff = false;     // wheels >= 3 -> < 3
    bool wheelTouch = false;  // wheels 0 -> >= 1
    bool boostPressed = false, boostReleased = false;
};

class MotionTracker {
public:
    // Timeline (frames, -1 = never).
    int64_t jumpPressTick = -1, jumpReleaseTick = -1;
    int64_t phaseJumpTick = -1, phaseJumpPressTick = -1, phaseJumpReleaseTick = -1;  // first jump of the phase
    int64_t takeoffTick = -1, landingTick = -1, prevLandingAtTakeoff = -1;
    int64_t airborneSince = -1;               // first tick with 0 wheels in the current phase
    Surface takeoffSurface = Surface::Floor;
    Vec3 takeoffNormal{0.f, 0.f, 1.f}, takeoffPos;
    float phaseMaxHeight = 0.f;               // max height above the takeoff plane
    bool flipAvailableBeforeLanding = false;  // hasFlip on the tick before the last landing
    int64_t lastAirPhaseTicks = 0;            // length of the airborne phase that just ended
    float lastAirPhaseMaxHeight = 0.f;
    bool lastAirPhaseHadJump = false;

    bool hasPrev() const { return hasPrev_; }
    const Snapshot& prev() const { return prev_; }
    void clear() { hasPrev_ = false; }

    TickEvents step(const Snapshot& s);

    bool phaseHadJump() const { return phaseJumpTick >= 0; }
    int64_t firstJumpHoldTicks() const {
        if (phaseJumpPressTick < 0 || phaseJumpReleaseTick < 0) return -1;
        return phaseJumpReleaseTick - phaseJumpPressTick;
    }

private:
    void resetTimeline(const Snapshot& s);
    Snapshot prev_;
    bool hasPrev_ = false;
};

} // namespace rlt::mech
