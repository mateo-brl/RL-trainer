#include "core/modules/mechanics/DodgeTracker.h"

namespace rlt::mech {

void DodgeTracker::step(const Snapshot& s, const Snapshot& p, const TickEvents& ev, const MotionTracker& mt,
                        std::vector<DodgeRecord>& finished) {
    if (ev.reset) {
        active_ = false;
        return;
    }
    if (!ev.valid) return;
    if (ev.dodgeStart) {
        if (active_) finalize(s, finished, true);
        begin(s, p, mt);
        return;
    }
    if (!active_) return;
    const int r = static_cast<int>(s.frame - rec_.tick);
    // Cancel: a forward flip (dir.x > 0) is cancelled by pulling back (pitch > 0), a backflip by pushing.
    const float fp = sgn(rec_.dir.x);
    if (std::fabs(rec_.dir.x) > 0.1f && r <= 78) {
        const float opp = s.in.pitch * fp;
        if (rec_.cancelTicks < 0 && opp >= 0.8f && rec_.landingTicks < 0) rec_.cancelTicks = r;
        if (rec_.cancelTicks >= 0 && r - rec_.cancelTicks < 24 && rec_.landingTicks < 0) {
            rec_.cancelStrength += clamp01(opp);
            rec_.cancelSamples++;
        }
    }
    if (rec_.cancelTicks >= 0 && rec_.rollStartTicks < 0 && std::fabs(s.in.roll) >= 0.5f) {
        rec_.rollStartTicks = r;
        rec_.rollSign = sgn(s.in.roll);
        rec_.upDotNAtRollStart = dot(s.car.basis.up, mt.takeoffNormal);
    }
    if (r <= 78 && s.car.wheelContacts == 0 && std::fabs(s.car.vel.z) < 60.f) rec_.hoverTicks++;
    const float h = dot(s.car.pos - mt.takeoffPos, mt.takeoffNormal);
    if (r == 50) rec_.heightDrop50 = rec_.heightAtDodge - h;
    if (rec_.firstContactTicks < 0 && s.car.wheelContacts > 0) {
        const Vec3 n = contactNormal(s.car);
        rec_.firstContactTicks = r;
        rec_.pitchAtFirstContactDeg = safeAsinDeg(dot(s.car.basis.fwd, n));
        rec_.rollAtFirstContactDeg = safeAsinDeg(dot(s.car.basis.right, n));
        rec_.wheelsAtFirstContact = s.car.wheelContacts;
    }
    if (rec_.landingTicks < 0) {
        rec_.maxHeight = std::max(rec_.maxHeight, h);
        rec_.ticksTotal++;
        if (s.in.boost) rec_.ticksBoost++;
        if (s.in.throttle > 0.9f) rec_.ticksThrottle++;
        if (grounded(s.car)) {
            rec_.landingTicks = r;
            const Vec3 n = contactNormal(s.car);
            rec_.landingNormal = n;
            rec_.landingSurface = classifySurface(n);
            rec_.upDotNAtLanding = dot(s.car.basis.up, n);
            rec_.landedOnWheels = rec_.upDotNAtLanding > 0.8f;
            rec_.pitchAtLandingDeg = safeAsinDeg(dot(s.car.basis.fwd, n));
            rec_.rollAtLandingDeg = safeAsinDeg(dot(s.car.basis.right, n));
            rec_.posAtLanding = s.car.pos;
            rec_.velAtLanding = s.car.vel;
            rec_.fwdAtLanding = s.car.basis.fwd;
            rec_.handbrakeAtLanding = s.in.handbrake;
            rec_.airPhaseTicksAtLanding = mt.lastAirPhaseTicks;
            rec_.airPhaseHeightAtLanding = mt.lastAirPhaseMaxHeight;
        }
    }
    if (rec_.kind == DodgeKind::Stall && r >= 80 && rec_.landingTicks < 0) finalize(s, finished, false);
    else if (rec_.landingTicks >= 0 && r >= rec_.landingTicks + kPostTicks) finalize(s, finished, false);
    else if (r >= kMaxTicks) finalize(s, finished, true);
}

void DodgeTracker::begin(const Snapshot& s, const Snapshot& p, const MotionTracker& mt) {
    rec_ = DodgeRecord{};
    active_ = true;
    rec_.tick = s.frame;
    lastDodgeTick_ = s.frame;
    // Direction: the dodge component (local x fwd, y right, UNVERIFIED in game), else the stick (RocketSim rule).
    Dir2 d{s.car.dodgeDir.x, s.car.dodgeDir.y};
    const float dl = std::sqrt(d.x * d.x + d.y * d.y);
    if (dl > 0.1f) d = {d.x / dl, d.y / dl};
    else d = dodgeDirFromInputs(s.in);
    const bool zero = d.x == 0.f && d.y == 0.f;
    rec_.dir = d;
    rec_.angleDeg = zero ? 0.f : std::atan2(d.y, d.x) * kRadToDeg;
    rec_.kind = dodgeKindFromAngle(rec_.angleDeg, zero);
    lastKind_ = rec_.kind;
    rec_.inputAtDodge = s.in;
    rec_.wheelsAtDodge = p.car.wheelContacts;
    if (p.car.wheelContacts > 0) {  // already touching: the attitude at the dodge is the contact attitude
        const Vec3 n = contactNormal(p.car);
        rec_.firstContactTicks = 0;
        rec_.pitchAtFirstContactDeg = safeAsinDeg(dot(p.car.basis.fwd, n));
        rec_.rollAtFirstContactDeg = safeAsinDeg(dot(p.car.basis.right, n));
        rec_.wheelsAtFirstContact = p.car.wheelContacts;
    }
    rec_.surface = mt.takeoffSurface;
    rec_.surfaceNormal = mt.takeoffNormal;
    rec_.heightAtDodge = dot(s.car.pos - mt.takeoffPos, mt.takeoffNormal);
    rec_.maxHeight = std::max(mt.phaseMaxHeight, rec_.heightAtDodge);
    rec_.pitchAtDodgeDeg = safeAsinDeg(dot(p.car.basis.fwd, mt.takeoffNormal));
    rec_.phaseHadJump = mt.phaseHadJump();
    rec_.ticksSinceFirstJump = mt.phaseHadJump() ? s.frame - mt.phaseJumpTick : -1;
    rec_.ticksSinceJumpPress = mt.phaseHadJump() ? s.frame - mt.phaseJumpPressTick : -1;
    rec_.firstJumpHoldTicks = mt.firstJumpHoldTicks();
    rec_.ticksSinceTakeoff = mt.takeoffTick >= 0 ? s.frame - mt.takeoffTick : -1;
    rec_.takeoffTick = mt.takeoffTick;
    rec_.prevLandingAtTakeoff = mt.prevLandingAtTakeoff;
    rec_.velBefore = p.car.vel;
    rec_.posAtDodge = s.car.pos;
    rec_.fwdAtDodge = p.car.basis.fwd;
    rec_.rightAtDodge = p.car.basis.right;
    rec_.upAtDodge = p.car.basis.up;
    rec_.vzAtDodge = p.car.vel.z;
    rec_.expectedDv = dodgeDeltaV(rec_.dir, p.car.vel, p.car.basis.fwd, p.car.basis.right);
    rec_.posAtFirstJump = firstJumpPos_;
    rec_.fwdAtFirstJump = firstJumpFwd_;
    rec_.rightAtFirstJump = firstJumpRight_;
}

void DodgeTracker::finalize(const Snapshot& s, std::vector<DodgeRecord>& finished, bool timedOut) {
    rec_.velAfter = s.car.vel;
    rec_.timedOut = timedOut;
    if (rec_.cancelSamples > 0) rec_.cancelStrength /= static_cast<float>(rec_.cancelSamples);
    finished.push_back(rec_);
    active_ = false;
}

} // namespace rlt::mech
