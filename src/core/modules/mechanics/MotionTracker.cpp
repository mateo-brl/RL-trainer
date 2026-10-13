#include "core/modules/mechanics/MotionTracker.h"

namespace rlt::mech {

TickEvents MotionTracker::step(const Snapshot& s) {
    TickEvents ev;
    if (hasPrev_) {
        const Snapshot& p = prev_;
        const float maxMove = std::max(length(p.car.vel) * k::kDt * 3.f + 60.f, 250.f);
        if (s.frame - p.frame > 30 || s.frame <= p.frame || length(s.car.pos - p.car.pos) > maxMove) ev.reset = true;
    }
    if (!hasPrev_ || ev.reset) {
        resetTimeline(s);
        prev_ = s;
        hasPrev_ = true;
        return ev;  // valid == false
    }
    const Snapshot& p = prev_;
    ev.valid = true;
    ev.jumpPressed = s.in.jump && !p.in.jump;
    ev.jumpReleased = !s.in.jump && p.in.jump;
    ev.boostPressed = s.in.boost && !p.in.boost;
    ev.boostReleased = !s.in.boost && p.in.boost;
    ev.firstJump = s.car.jumped && !p.car.jumped;
    ev.doubleJump = s.car.doubleJumped && !p.car.doubleJumped;
    ev.dodgeStart = s.car.dodging && !p.car.dodging;
    // Fallback when IsDodging does not rise (UNVERIFIED need): the flip was consumed by a jump press in the
    // air that is not a double jump.
    if (!ev.dodgeStart && ev.jumpPressed && !grounded(s.car) && p.car.hasFlip && !s.car.hasFlip && !ev.doubleJump &&
        !s.car.doubleJumped && !p.car.dodging)
        ev.dodgeStart = true;
    const bool g = grounded(s.car), pg = grounded(p.car);
    ev.landed = g && !pg;
    ev.tookOff = !g && pg;
    ev.wheelTouch = s.car.wheelContacts > 0 && p.car.wheelContacts == 0;

    if (ev.landed) {
        flipAvailableBeforeLanding = p.car.hasFlip;
        lastAirPhaseTicks = takeoffTick >= 0 ? s.frame - takeoffTick : 0;
        lastAirPhaseMaxHeight = phaseMaxHeight;
        lastAirPhaseHadJump = phaseJumpTick >= 0;
        landingTick = s.frame;
        phaseJumpTick = phaseJumpPressTick = phaseJumpReleaseTick = -1;  // phase over
    }
    if (ev.jumpPressed) jumpPressTick = s.frame;
    if (ev.jumpReleased) jumpReleaseTick = s.frame;
    if (ev.firstJump) {
        phaseJumpTick = s.frame;
        phaseJumpPressTick = jumpPressTick >= 0 ? jumpPressTick : s.frame;
        phaseJumpReleaseTick = -1;
    }
    if (ev.jumpReleased && phaseJumpTick >= 0 && phaseJumpReleaseTick < 0) phaseJumpReleaseTick = s.frame;

    if (ev.tookOff) {
        takeoffTick = s.frame;
        takeoffNormal = contactNormal(p.car);
        takeoffSurface = classifySurface(takeoffNormal);
        takeoffPos = p.car.pos;
        prevLandingAtTakeoff = landingTick;
        phaseMaxHeight = 0.f;
        airborneSince = -1;
        // A jump that started more than 30 ticks before this takeoff belongs to an older phase.
        if (phaseJumpTick >= 0 && s.frame - phaseJumpTick > 30) phaseJumpTick = -1;
    }
    if (!g) {
        phaseMaxHeight = std::max(phaseMaxHeight, dot(s.car.pos - takeoffPos, takeoffNormal));
        if (s.car.wheelContacts == 0 && airborneSince < 0) airborneSince = s.frame;
    }
    prev_ = s;
    return ev;
}

void MotionTracker::resetTimeline(const Snapshot& s) {
    jumpPressTick = jumpReleaseTick = phaseJumpTick = phaseJumpPressTick = phaseJumpReleaseTick = -1;
    takeoffTick = landingTick = prevLandingAtTakeoff = airborneSince = -1;
    takeoffNormal = grounded(s.car) ? contactNormal(s.car) : Vec3{0.f, 0.f, 1.f};
    takeoffSurface = grounded(s.car) ? classifySurface(takeoffNormal) : Surface::Floor;
    takeoffPos = s.car.pos;
    phaseMaxHeight = 0.f;
    if (!grounded(s.car)) {  // spawned in the air (drill, reset): treat the reset tick as a floor takeoff
        takeoffTick = s.frame;
        takeoffPos = {s.car.pos.x, s.car.pos.y, k::kCarRestZ};
        phaseMaxHeight = s.car.pos.z - k::kCarRestZ;
    }
    flipAvailableBeforeLanding = false;
    lastAirPhaseTicks = 0;
    lastAirPhaseMaxHeight = 0.f;
    lastAirPhaseHadJump = false;
}

} // namespace rlt::mech
