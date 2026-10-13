#pragma once
// DodgeTracker: one DodgeRecord per dodge (flip or stall), followed until the landing plus a few ticks.
// Most detectors (wavedash family, speedflip, half-flip, flip cancel, stall) grade closed records.

#include <cstdint>
#include <vector>

#include "core/modules/mechanics/MechCommon.h"
#include "core/modules/mechanics/MotionTracker.h"

namespace rlt::mech {

struct DodgeRecord {
    int64_t tick = 0;
    Dir2 dir;                    // local, normalized, (0, 0) for a stall
    float angleDeg = 0.f;
    DodgeKind kind = DodgeKind::Forward;
    Inputs inputAtDodge;
    int wheelsAtDodge = 0;
    float heightAtDodge = 0.f;   // above the takeoff plane
    float pitchAtDodgeDeg = 0.f; // nose angle relative to the takeoff surface (+ = nose up)
    bool phaseHadJump = false;
    int64_t ticksSinceFirstJump = -1;  // dodge tick - first jump tick
    int64_t ticksSinceJumpPress = -1;  // dodge tick - first jump press (press-to-press)
    int64_t firstJumpHoldTicks = -1;
    int64_t ticksSinceTakeoff = -1;
    int64_t takeoffTick = -1, prevLandingAtTakeoff = -1;
    Surface surface = Surface::Floor;
    Vec3 surfaceNormal{0.f, 0.f, 1.f};
    Vec3 posAtFirstJump, fwdAtFirstJump{1.f, 0.f, 0.f}, rightAtFirstJump{0.f, 1.f, 0.f};
    Vec3 velBefore, posAtDodge, fwdAtDodge{1.f, 0.f, 0.f}, rightAtDodge{0.f, 1.f, 0.f}, upAtDodge{0.f, 0.f, 1.f};
    // Flip cancel: pitch input opposing the flip's pitch torque.
    int cancelTicks = -1;         // ticks after the dodge, -1 = never
    float cancelStrength = 0.f;   // mean opposing pitch over 24 ticks after the cancel
    int cancelSamples = 0;
    int rollStartTicks = -1;      // first |roll| >= 0.5 after the cancel
    float rollSign = 0.f;
    float upDotNAtRollStart = 1.f;  // < 0: already past vertical (inverted) when rolling
    // Stall / hover
    int hoverTicks = 0;           // ticks within 78 after the dodge with |vz| < 60 and no wheel contact
    float heightDrop50 = 0.f;
    float vzAtDodge = 0.f;
    // First wheel contact after the dodge (0 when a wheel already touched at the dodge)
    int firstContactTicks = -1;
    float pitchAtFirstContactDeg = 0.f, rollAtFirstContactDeg = 0.f;
    int wheelsAtFirstContact = 0;
    // Landing (>= 3 wheels)
    int landingTicks = -1;
    bool landedOnWheels = false;
    float upDotNAtLanding = 0.f, pitchAtLandingDeg = 0.f, rollAtLandingDeg = 0.f;
    Surface landingSurface = Surface::None;
    Vec3 landingNormal{0.f, 0.f, 1.f};
    Vec3 posAtLanding, velAtLanding, fwdAtLanding{1.f, 0.f, 0.f};
    float maxHeight = 0.f;
    bool handbrakeAtLanding = false;
    // Landing ended a long aerial phase (filled from the MotionTracker at landing)
    int64_t airPhaseTicksAtLanding = 0;
    float airPhaseHeightAtLanding = 0.f;
    // Inputs over dodge -> landing
    int ticksTotal = 0, ticksBoost = 0, ticksThrottle = 0;
    // Filled when the record closes
    Vec3 velAfter;
    bool timedOut = false;
    Vec3 expectedDv;              // RocketSim dodge impulse for this dodge
};

class DodgeTracker {
public:
    static constexpr int kPostTicks = 8;    // velocity sample after landing
    static constexpr int kMaxTicks = 240;   // give up 2 s after the dodge

    const DodgeRecord* active() const { return active_ ? &rec_ : nullptr; }
    int64_t lastDodgeTick() const { return lastDodgeTick_; }
    DodgeKind lastDodgeKind() const { return lastKind_; }
    void reset() { active_ = false; }

    void step(const Snapshot& s, const Snapshot& p, const TickEvents& ev, const MotionTracker& mt,
              std::vector<DodgeRecord>& finished);
    // Pose at each first jump (speedflip drift / heading).
    void noteFirstJump(const Snapshot& s) {
        firstJumpPos_ = s.car.pos;
        firstJumpFwd_ = s.car.basis.fwd;
        firstJumpRight_ = s.car.basis.right;
    }

private:
    void begin(const Snapshot& s, const Snapshot& p, const MotionTracker& mt);
    void finalize(const Snapshot& s, std::vector<DodgeRecord>& finished, bool timedOut);

    DodgeRecord rec_;
    bool active_ = false;
    int64_t lastDodgeTick_ = -1;
    DodgeKind lastKind_ = DodgeKind::Forward;
    Vec3 firstJumpPos_, firstJumpFwd_{1.f, 0.f, 0.f}, firstJumpRight_{0.f, 1.f, 0.f};
};

} // namespace rlt::mech
