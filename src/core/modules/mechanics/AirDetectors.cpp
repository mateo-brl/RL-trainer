#include "core/modules/mechanics/AirDetectors.h"

namespace rlt::mech {

// ---- Fast aerial / double jump aerial ------------------------------------------------------------------
void FastAerialDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    const Snapshot& s = c.s;
    if (c.ev.reset) {
        active_ = false;
        return;
    }
    if (!c.ev.valid) return;
    if (c.ev.firstJump && classifySurface(contactNormal(c.p.car)) == Surface::Floor) {
        a_ = Attempt{};
        active_ = true;
        a_.jumpTick = s.frame;
        a_.pressTick = c.mt.jumpPressTick >= 0 ? c.mt.jumpPressTick : s.frame;
        a_.startZ = s.car.pos.z;
        a_.boostStart = s.in.boost ? 0 : kNever;  // already boosting when jumping counts as 0
    }
    if (!active_) return;
    const int64_t r = s.frame - a_.jumpTick;
    if (a_.holdTicks < 0 && !s.in.jump) {
        a_.holdTicks = s.frame - a_.pressTick;
        a_.releaseTick = s.frame;
    }
    if (s.in.boost && a_.boostStart == kNever) a_.boostStart = static_cast<int>(r);
    if (a_.djTick < 0 && !a_.dodged) {
        a_.pitchSum += clamp01(s.in.pitch);
        a_.pitchSamples++;
    }
    if (c.ev.doubleJump && a_.djTick < 0) {
        a_.djTick = s.frame;
        a_.noseAtDJ = safeAsinDeg(s.car.basis.fwd.z);
        a_.stickAtDJ = stickMag(s.in);
    }
    if (c.ev.dodgeStart && a_.djTick < 0 && !a_.dodged) {
        a_.dodged = true;
        a_.stickAtDJ = stickMag(s.in);
        const DodgeRecord* d = c.dt.active();
        a_.dodgeKind = d ? d->kind : DodgeKind::Forward;
        a_.dodgeTick = d ? d->tick : s.frame;
    }
    bool done = false;
    if (a_.djTick >= 0 && s.frame - a_.djTick >= 24) done = true;
    if (a_.dodged) done = true;
    if (r > 6 && grounded(s.car)) done = true;
    if (r >= 150) done = true;
    if (!done) return;
    a_.heightGain = s.car.pos.z - a_.startZ;
    a_.vzEnd = s.car.vel.z;
    emit(c, out);
    active_ = false;
}

void FastAerialDetector::emit(const Ctx& c, std::vector<MechGrade>& out) {
    const bool boostEarly = a_.boostStart >= 0 && a_.boostStart <= 6;
    const int64_t djRel = a_.djTick >= 0 ? a_.djTick - a_.jumpTick : -1;
    const bool boostAfterDJ = a_.djTick >= 0 && a_.boostStart != kNever && a_.boostStart >= static_cast<int>(djRel) - 1;
    const bool expect = c.cfg.expect(Focus::FastAerial);
    // Accidental dodge: the 2nd press became a flip while going up for an aerial (boost on, jump held like an
    // aerial, stick pulled back). A forward or diagonal dodge with boost is a speedflip/flip, not a mistake.
    const bool backDodge = a_.dodgeKind == DodgeKind::Back || a_.dodgeKind == DodgeKind::DiagBack;
    const bool accidental = a_.dodged && (expect || (boostEarly && backDodge && a_.holdTicks >= 15));
    if (a_.dodged && !accidental) return;
    if (a_.djTick < 0 && !a_.dodged && !expect) return;  // single-jump aerial / hop: not graded
    const bool dja = a_.djTick >= 0 && boostAfterDJ;
    const bool fast = boostEarly && !dja;
    if (!fast && !dja && !accidental && !expect) return;  // plain double jump without boost

    MechGrade g;
    g.mech = dja ? Mech::DoubleJumpAerial : Mech::FastAerial;
    g.variant = dja ? "double-jump" : "fast";
    g.tickStart = a_.jumpTick;
    g.tickEnd = a_.djTick >= 0 ? a_.djTick : a_.jumpTick + 150;
    if (accidental) {
        g.dodgeTick = a_.dodgeTick;
        claimedDodge_ = a_.dodgeTick;
    }
    const int64_t gap = (a_.djTick >= 0 && a_.releaseTick >= 0) ? a_.djTick - a_.releaseTick : -1;
    const float tilt = a_.pitchSamples ? a_.pitchSum / static_cast<float>(a_.pitchSamples) : 0.f;
    g.add("holdTicks", static_cast<float>(a_.holdTicks));
    g.add("gapTicks", static_cast<float>(gap));
    g.add("boostStartTicks", static_cast<float>(a_.boostStart));
    g.add("boostAfterDJTicks", a_.djTick >= 0 && a_.boostStart != kNever ? static_cast<float>(a_.boostStart - djRel) : -1.f);
    g.add("tiltInput", tilt);
    g.add("noseAtDoubleJumpDeg", a_.noseAtDJ);
    g.add("stickAtSecondPress", a_.stickAtDJ);
    g.add("deadzone", c.cfg.dodgeDeadzone);
    g.add("heightGain", a_.heightGain);
    g.add("vzEnd", a_.vzEnd);

    float score = 0.f;
    if (accidental) {
        g.err("FA_ACCIDENTAL_DODGE", 90.f);
        score = 10.f;
    } else if (a_.djTick < 0) {
        g.err("FA_NO_DOUBLE_JUMP", 85.f);
        score = 15.f;
    } else if (g.mech == Mech::FastAerial) {
        // FastAerialTrainer (Vyncc): hold 195-225 ms green, gap < 75 ms green, < 110 ms yellow.
        const float hold = bandPts(static_cast<float>(a_.holdTicks), 23.f, 27.f, 12.f, 40.f);
        const float gapS = lerpPts(static_cast<float>(gap), 9.f, 20.f);
        const float boostS = a_.boostStart == kNever ? 0.f : lerpPts(static_cast<float>(a_.boostStart), 2.f, 10.f);
        const float tiltS = lerpPts(tilt, 0.7f, 0.2f);
        score = 0.30f * hold + 0.30f * gapS + 0.20f * boostS + 0.20f * tiltS;
        if (a_.holdTicks < 21) g.err("FA_HOLD_SHORT", 0.30f * (100.f - hold));
        if (a_.holdTicks > 30) g.err("FA_HOLD_LONG", 0.30f * (100.f - hold));
        if (gap > 13) g.err("FA_GAP_SLOW", 0.30f * (100.f - gapS));
        if (a_.boostStart == kNever || a_.boostStart > 6) g.err("FA_BOOST_LATE", 0.20f * (100.f - boostS));
        if (tilt < 0.3f) g.err("FA_NO_TILT", 0.20f * (100.f - tiltS));
    } else {
        const float gapS = lerpPts(static_cast<float>(gap), 9.f, 24.f);
        const float hold = bandPts(static_cast<float>(a_.holdTicks), 20.f, 27.f, 6.f, 40.f);
        const float boostS = lerpPts(static_cast<float>(a_.boostStart - djRel), 6.f, 30.f);
        score = 0.4f * gapS + 0.3f * hold + 0.3f * boostS;
        if (gap > 13) g.err("FA_GAP_SLOW", 0.4f * (100.f - gapS));
        if (a_.boostStart - djRel > 12) g.err("DJA_BOOST_LATE", 0.3f * (100.f - boostS));
    }
    // Near miss: the stick was close to the player's own flip deadzone at the second press.
    if (!accidental && a_.djTick >= 0 && a_.stickAtDJ >= 0.7f * c.cfg.dodgeDeadzone) g.err("FA_STICK_NEAR_DEADZONE", 4.f);
    g.success = score >= 70.f && !accidental && a_.djTick >= 0;
    finalizeGrade(g, score);
    out.push_back(std::move(g));
}

// ---- Stall ---------------------------------------------------------------------------------------------
namespace {
bool stallInputs(const Inputs& in) {
    return in.yaw * in.roll < 0.f && std::fabs(in.yaw) >= 0.4f && std::fabs(in.roll) >= 0.4f;
}
} // namespace

bool StallDetector::onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out) {
    const Inputs& in = r.inputAtDodge;
    const bool stallLike = stallInputs(in);
    if (r.kind != DodgeKind::Stall && !stallLike) return false;
    if (r.kind != DodgeKind::Stall && !c.cfg.expect(Focus::Stall)) {
        // A real dodge with stall-like inputs: only an aerial stall attempt, not a low side dash.
        const bool quick = r.landingTicks >= 0 && r.landingTicks <= 20;
        if (quick || r.heightAtDodge < 80.f) return false;
    }
    MechGrade g;
    g.mech = Mech::Stall;
    g.variant = in.roll < 0.f ? "airroll-left" : "airroll-right";
    g.tickStart = r.tick;
    g.tickEnd = r.tick + 78;
    g.dodgeTick = r.tick;
    g.add("yawPlusRoll", in.yaw + in.roll);
    g.add("pitchAbs", std::fabs(in.pitch));
    g.add("hoverTicks", static_cast<float>(r.hoverTicks));
    g.add("heightDrop50", r.heightDrop50);
    g.add("vzAtStall", r.vzAtDodge);
    float score;
    if (r.kind == DodgeKind::Stall) {
        score = 55.f + 45.f * clamp01(static_cast<float>(r.hoverTicks) / 50.f);
        if (r.hoverTicks < 20) g.err("ST_SHORT_HOVER", 100.f - score);
        g.success = r.hoverTicks >= 20;
    } else {
        if (std::fabs(in.yaw + in.roll) >= 0.1f) g.err("ST_YAW_ROLL_MISMATCH", 70.f);
        if (std::fabs(in.pitch) >= 0.1f) g.err("ST_PITCH_LEAK", 50.f);
        score = 15.f;
        g.success = false;
    }
    finalizeGrade(g, score);
    out.push_back(std::move(g));
    return true;
}

void StallDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    if (!c.ev.valid || !c.ev.jumpPressed || grounded(c.s.car) || c.p.car.hasFlip) return;
    const Inputs& in = c.s.in;
    if (!stallInputs(in) || stickMag(in) < c.cfg.dodgeDeadzone || c.s.frame - lastNoFlip_ < 120) return;
    lastNoFlip_ = c.s.frame;
    MechGrade g;
    g.mech = Mech::Stall;
    g.variant = "no-flip";
    g.tickStart = g.tickEnd = c.s.frame;
    g.add("yawPlusRoll", in.yaw + in.roll);
    g.add("pitchAbs", std::fabs(in.pitch));
    if (c.mt.phaseJumpTick >= 0) g.add("sinceJumpTicks", static_cast<float>(c.s.frame - c.mt.phaseJumpTick));
    else if (c.mt.takeoffTick >= 0) g.add("sinceJumpTicks", static_cast<float>(c.s.frame - c.mt.takeoffTick));
    g.err("ST_NO_FLIP", 100.f);
    finalizeGrade(g, 0.f);
    out.push_back(std::move(g));
}

// ---- Helicopter / tornado spin -------------------------------------------------------------------------
void AirSpinDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    const Snapshot& s = c.s;
    if (c.ev.reset) {
        ticks_ = 0;
        mode_ = 0;
        return;
    }
    const bool air = s.car.wheelContacts == 0;
    const bool rollOn = std::fabs(s.in.roll) >= 0.5f;
    const bool heli = rollOn && std::fabs(s.in.yaw) >= 0.5f && sgn(s.in.yaw) == sgn(s.in.roll);
    const bool tornado = rollOn && !heli && std::fabs(s.in.yaw) < 0.3f && s.in.boost;
    const int mode = heli ? 1 : (tornado ? 2 : 0);
    // A dodge (speedflip barrel roll, flip) is not a freestyle spin; neither is a low hop.
    if (s.car.dodging || c.ev.dodgeStart) dodgeSeen_ = true;
    if (air && mode != 0 && (ticks_ == 0 || mode == mode_)) {
        if (ticks_ == 0) {
            dodgeSeen_ = s.car.dodging;
            maxH_ = 0.f;
            start_ = s.frame;
            axisSum_ = {};
            rateSum_ = 0.f;
            angle_ = 0.f;
        }
        maxH_ = std::max(maxH_, c.mt.phaseMaxHeight);
        mode_ = mode;
        ticks_++;
        const Vec3 w = s.car.angVel;
        const float wl = length(w);
        rateSum_ += wl;
        angle_ += wl * k::kDt;
        if (wl > 0.5f) axisSum_ += normalize(w) * (dot(w, axisSum_) < 0.f ? -1.f : 1.f);
        return;
    }
    if (ticks_ >= kMinTicks && !dodgeSeen_ && maxH_ >= 150.f) {
        MechGrade g;
        g.mech = Mech::AirSpin;
        g.variant = mode_ == 1 ? "helicopter" : "tornado";
        g.tickStart = start_;
        g.tickEnd = s.frame;
        const float n = static_cast<float>(ticks_);
        const float stability = length(axisSum_) / n;  // 1 = perfectly constant axis
        const float rate = rateSum_ / n;
        g.add("durationTicks", n);
        g.add("avgSpinRate", rate);
        g.add("axisStability", stability);
        g.add("turns", angle_ / kTwoPi);
        const float score = 50.f + 25.f * clamp01(stability) + 25.f * clamp01(rate / k::kCarMaxAngSpeed);
        if (stability < 0.8f) g.err("AS_UNSTABLE_AXIS", 25.f * (1.f - clamp01(stability)) + 5.f);
        if (rate < 3.f) g.err("AS_SLOW_SPIN", 25.f * (1.f - clamp01(rate / k::kCarMaxAngSpeed)));
        g.success = stability >= 0.8f && rate >= 3.f;
        finalizeGrade(g, score);
        out.push_back(std::move(g));
    }
    ticks_ = 0;
    mode_ = 0;
}

// ---- Recovery ------------------------------------------------------------------------------------------
bool RecoveryDetector::absorbDash(const MechGrade& dash) {
    if (!pending_ || dash.tickEnd != landTick_) return false;
    absorbed_ = true;
    wavedash_ = true;
    dashScore_ = dash.score;
    dashGain_ = dash.metric("gainAlongDash", 0.f);
    dashSuccess_ = dash.success;
    return true;
}

void RecoveryDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    const Snapshot& s = c.s;
    if (c.ev.reset) {
        reset();
        return;
    }
    if (!c.ev.valid) return;
    if (c.ev.tookOff) {
        crashed_ = false;
        crashTicks_ = 0;
    }
    // A long aerial that ends lying on the roof or the side: the wheels never touch, so grade it here.
    if (!crashed_ && !grounded(s.car) && s.car.wheelContacts == 0 && c.mt.takeoffTick >= 0 &&
        s.frame - c.mt.takeoffTick >= kMinAirTicks && c.mt.phaseMaxHeight >= c.cfg.recoveryMinHeight) {
        const Vec3 n = c.mt.takeoffNormal;
        const float h = dot(s.car.pos - c.mt.takeoffPos, n);
        const bool lying = h < 70.f && std::fabs(dot(s.car.vel, n)) < 150.f && dot(s.car.basis.up, n) < 0.3f;
        crashTicks_ = lying ? crashTicks_ + 1 : 0;
        if (crashTicks_ >= 20) {
            crashed_ = true;
            touch_ = false;
            MechGrade g;
            g.mech = Mech::Recovery;
            g.variant = "crash";
            g.tickStart = c.mt.takeoffTick;
            g.tickEnd = s.frame;
            g.add("tiltAtTouchDeg", safeAcosDeg(dot(s.car.basis.up, n)));
            g.add("airTicks", static_cast<float>(s.frame - c.mt.takeoffTick));
            g.err("RC_CRASHED", 90.f);
            g.success = false;
            finalizeGrade(g, 5.f);
            out.push_back(std::move(g));
        }
    }
    // First wheel contact of an airborne phase: record the attitude.
    if (c.ev.wheelTouch && !grounded(c.p.car)) {
        touch_ = true;
        const Vec3 n = contactNormal(s.car);
        touchUpDotN_ = dot(s.car.basis.up, n);
        const Vec3 vt = tangent(c.p.car.vel, n);
        touchSpeed_ = length(vt);
        touchAlign_ = touchSpeed_ > 300.f ? planarAngleDeg(s.car.basis.fwd, vt, n) : 0.f;
        if (touchAlign_ > 90.f) touchAlign_ = 180.f - touchAlign_;  // driving backwards along the velocity is aligned
    }
    if (c.ev.landed) {
        const bool longAir =
            c.mt.lastAirPhaseTicks >= kMinAirTicks && c.mt.lastAirPhaseMaxHeight >= c.cfg.recoveryMinHeight;
        if (longAir && touch_ && !crashed_) {
            pending_ = true;
            landTick_ = s.frame;
            airTicks_ = c.mt.lastAirPhaseTicks;
            stableRun_ = 0;
            settleTicks_ = -1;
            speedAfter_ = -1.f;
            flipAvail_ = c.mt.flipAvailableBeforeLanding;
            absorbed_ = false;
            dashSuccess_ = false;
            dashScore_ = dashGain_ = 0.f;
            wavedash_ = c.dt.lastDodgeTick() >= 0 && s.frame - c.dt.lastDodgeTick() <= 20 &&
                        c.dt.lastDodgeKind() != DodgeKind::Stall;
        }
        touch_ = false;
        crashed_ = false;
    }
    if (!pending_) return;
    const int64_t r = s.frame - landTick_;
    const Vec3 nNow = contactNormal(s.car);
    if (grounded(s.car) && length(tangent(s.car.angVel, nNow)) < 0.6f) {  // steering (yaw) is allowed
        if (++stableRun_ >= 12 && settleTicks_ < 0) settleTicks_ = static_cast<int>(r - 11);
    } else {
        stableRun_ = 0;
    }
    if (r >= 30 && speedAfter_ < 0.f) speedAfter_ = length(tangent(s.car.vel, nNow));
    if (r < 90 && !(r >= 30 && settleTicks_ >= 0)) return;
    pending_ = false;
    MechGrade g;
    g.mech = Mech::Recovery;
    g.variant = wavedash_ ? "wavedash" : "landing";
    g.tickStart = landTick_ - airTicks_;
    g.tickEnd = s.frame;
    const float tilt = safeAcosDeg(touchUpDotN_);
    const float after = std::max(speedAfter_, 0.f);
    const float retained = touchSpeed_ > 300.f ? after / touchSpeed_ : 1.f;
    g.add("tiltAtTouchDeg", tilt);
    g.add("velocityAlignDeg", touchAlign_);
    g.add("speedAtTouch", touchSpeed_);
    g.add("speedAfter", after);
    g.add("speedRetained", retained);
    g.add("settleTicks", static_cast<float>(settleTicks_));
    g.add("flipAvailable", flipAvail_ ? 1.f : 0.f);
    g.add("airTicks", static_cast<float>(airTicks_));
    if (absorbed_) {
        g.add("dashGain", dashGain_);
        g.add("dashScore", dashScore_);
    }
    const float upright = lerpPts(tilt, 10.f, 60.f);
    const float align = touchSpeed_ > 300.f ? lerpPts(touchAlign_, 15.f, 60.f) : 100.f;
    const float keep = lerpPts(retained, 0.95f, 0.6f);
    const float settle = settleTicks_ >= 0 ? lerpPts(static_cast<float>(settleTicks_), 15.f, 60.f) : 0.f;
    const float score = 0.35f * upright + 0.25f * align + 0.25f * keep + 0.15f * settle + (wavedash_ ? 10.f : 0.f);
    if (touchUpDotN_ < 0.f) g.err("RC_ON_ROOF", 0.35f * (100.f - upright) + 10.f);
    else if (tilt > 25.f) g.err("RC_TILTED", 0.35f * (100.f - upright));
    if (touchSpeed_ > 300.f && touchAlign_ > 30.f) g.err("RC_MISALIGNED", 0.25f * (100.f - align));
    if (retained < 0.8f) g.err("RC_SPEED_LOST", 0.25f * (100.f - keep));
    if (settleTicks_ < 0 || settleTicks_ > 45) g.err("RC_SLOW_SETTLE", 0.15f * (100.f - settle));
    if (flipAvail_ && !wavedash_ && touchSpeed_ > 500.f) g.err("RC_MISSED_WAVEDASH", 10.f);
    if (absorbed_ && !dashSuccess_) g.err("RC_WEAK_WAVEDASH", 8.f);
    g.success = tilt <= 25.f && retained >= 0.8f && score >= 65.f;
    finalizeGrade(g, score);
    out.push_back(std::move(g));
}

} // namespace rlt::mech
