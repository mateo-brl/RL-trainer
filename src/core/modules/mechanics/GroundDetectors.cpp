#include "core/modules/mechanics/GroundDetectors.h"

namespace rlt::mech {

// ---- Wavedash family -----------------------------------------------------------------------------------
bool WavedashDetector::onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out) {
    if (r.kind == DodgeKind::Stall) return false;
    const bool fromWall = r.surface == Surface::Wall;
    const bool quick = r.landingTicks >= 0 && r.landingTicks <= kMaxLandTicks;
    const bool zapIntent = c.cfg.expect(Focus::ZapDash);
    const bool dashIntent = c.cfg.expect(Focus::Wavedash) || c.cfg.expect(Focus::ChainDash) || zapIntent;
    if (!quick && dashIntent && !fromWall && r.landingSurface != Surface::Wall) {
        // Dash practice: a flip far above the floor is a real flip, the dash came too early. Graded with its tip
        // instead of a silent time-out (fix 5).
        MechGrade g;
        g.mech = zapIntent ? Mech::ZapDash : Mech::Wavedash;
        g.variant = "early";
        g.tickStart = r.takeoffTick >= 0 ? r.takeoffTick : r.tick;
        g.tickEnd = r.tick + std::max(r.landingTicks, 0);
        g.dodgeTick = r.tick;
        g.add("firstContactTicks", static_cast<float>(r.firstContactTicks));
        g.add("landingTicks", static_cast<float>(r.landingTicks));
        g.add("pitchAtDodgeDeg", r.pitchAtDodgeDeg);
        g.add("dodgeAngleDeg", r.angleDeg);
        g.err(zapIntent ? "ZAP_NO_BOUNCE" : "WD_TOO_EARLY", 80.f);
        g.success = false;
        finalizeGrade(g, 10.f);
        out.push_back(std::move(g));
        return true;
    }
    if (!quick) {
        // Wall dash attempt that never re-attached to the wall.
        const int maxPress = c.cfg.expect(Focus::WallDash) ? 30 : 12;
        const bool wallIntent = fromWall && r.ticksSinceJumpPress >= 0 && r.ticksSinceJumpPress <= maxPress;
        if (!wallIntent) return false;
        MechGrade g;
        g.mech = Mech::WallDash;
        g.variant = "wall";
        g.tickStart = r.takeoffTick >= 0 ? r.takeoffTick : r.tick;
        g.tickEnd = r.tick + std::max(r.landingTicks, 0);
        g.dodgeTick = r.tick;
        g.add("jumpToDodgeTicks", static_cast<float>(r.ticksSinceJumpPress));
        g.add("firstJumpHoldTicks", static_cast<float>(r.firstJumpHoldTicks));
        g.add("landingTicks", static_cast<float>(r.landingTicks));
        g.err("WALL_FELL_OFF", 60.f);
        if (r.ticksSinceJumpPress > 11) g.err("WALL_TAP_SLOW", 25.f);
        finalizeGrade(g, 10.f);
        out.push_back(std::move(g));
        return true;
    }

    MechGrade g;
    const Surface landS = r.landingSurface;
    const bool bounceZap = !r.phaseHadJump && r.prevLandingAtTakeoff >= 0 && r.takeoffTick >= 0 &&
                           (r.takeoffTick - r.prevLandingAtTakeoff) <= kZapMaxBounceTicks;
    // Zap dash practice, fallback while the suspension bounce is UNVERIFIED in game: a flip pressed without any jump
    // while only one or two wheels touch (the first contact of a nose-down landing) is the same move.
    const bool touchZap = zapIntent && !r.phaseHadJump && r.wheelsAtDodge > 0 && r.wheelsAtDodge < k::kGroundedWheels;
    const bool zap = bounceZap || touchZap;
    if (landS == Surface::Wall) g.mech = Mech::WallDash;
    else if (landS == Surface::Ceiling) g.mech = Mech::CeilingDash;
    else if (zap) g.mech = Mech::ZapDash;
    else g.mech = Mech::Wavedash;
    g.variant = dodgeVariant(r.kind, r.angleDeg);
    g.tickStart = r.takeoffTick >= 0 ? r.takeoffTick : r.tick;
    g.tickEnd = r.tick + r.landingTicks;
    g.dodgeTick = r.tick;
    g.recoveryLanding = r.airPhaseTicksAtLanding >= 60 && r.airPhaseHeightAtLanding >= c.cfg.recoveryMinHeight;

    const Vec3 n = r.landingNormal;
    const Vec3 vb = tangent(r.velBefore, n), va = tangent(r.velAfter, n);
    const Vec3 dv = tangent(r.expectedDv, n);
    const float speedGain = length(va) - length(vb);
    float expected, actual;
    if (landS == Surface::Floor) {
        // RocketSim applies the dodge impulse in the car's yaw plane: compare along it.
        const Vec3 dvHat = normalize(dv, normalize(tangent(r.fwdAtDodge, n), {1.f, 0.f, 0.f}));
        const Vec3 capped = clampLen(vb + dv, k::kCarMaxSpeed);
        expected = dot(capped - vb, dvHat);
        actual = dot(va - vb, dvHat);
    } else {
        // Wall/ceiling impulse direction is UNVERIFIED in game: grade the speed gain along the surface.
        expected = std::min(length(r.expectedDv), k::kCarMaxSpeed - length(vb));
        actual = speedGain;
    }
    float ratio;
    if (expected < 100.f) ratio = length(va) >= length(vb) - 60.f ? 1.f : 0.3f;  // near max speed: keep it
    else ratio = actual / expected;

    g.add("landingTicks", static_cast<float>(r.landingTicks));
    g.add("firstContactTicks", static_cast<float>(r.firstContactTicks));
    g.add("wheelsAtDodge", static_cast<float>(r.wheelsAtDodge));
    g.add("pitchAtDodgeDeg", r.pitchAtDodgeDeg);
    g.add("pitchAtContactDeg", r.pitchAtFirstContactDeg);
    g.add("rollAtContactDeg", r.rollAtFirstContactDeg);
    g.add("pitchAtLandingDeg", r.pitchAtLandingDeg);
    g.add("dodgeAngleDeg", r.angleDeg);
    g.add("gainAlongDash", actual);
    g.add("expectedGain", std::max(expected, 0.f));
    g.add("gainRatio", ratio);
    g.add("speedGain", speedGain);
    g.add("speedBefore", length(vb));
    g.add("speedAfter", length(va));
    g.add("apexHeight", r.maxHeight);
    if (r.phaseHadJump) g.add("hopHeight", r.maxHeight);
    g.add("jumpToDodgeTicks", static_cast<float>(r.ticksSinceJumpPress));
    g.add("firstJumpHoldTicks", static_cast<float>(r.firstJumpHoldTicks));
    g.add("handbrakeAtLanding", r.handbrakeAtLanding ? 1.f : 0.f);

    // Attitude at the FIRST wheel contact (before the suspension flattens the car).
    const bool sideDash = r.kind == DodgeKind::Side || r.kind == DodgeKind::DiagForward || r.kind == DodgeKind::DiagBack;
    const float pc = r.pitchAtFirstContactDeg;
    const float rc = sideDash ? 0.f : std::fabs(r.rollAtFirstContactDeg);
    const float gainScore = 100.f * clamp01(ratio);
    const float attScore = std::min(bandPts(pc, -3.f, 25.f, -20.f, 45.f), lerpPts(rc, 6.f, 30.f));
    const float timeScore = lerpPts(static_cast<float>(r.firstContactTicks), 8.f, 16.f);
    float score = 0.40f * gainScore + 0.35f * attScore + 0.25f * timeScore;
    const float attLoss = std::max(0.35f * (100.f - attScore), 15.f);

    if (!r.landedOnWheels) g.err("WD_CRASH_LANDING", 45.f);
    if (pc < -8.f) g.err("WD_NOSE_DOWN", attLoss);
    if (pc > 30.f) g.err("WD_NOSE_UP", attLoss);
    if (rc > 15.f) g.err("WD_ROLL_TILT", attLoss);
    if (r.firstContactTicks > 12) g.err("WD_TOO_EARLY", 0.25f * (100.f - timeScore));
    if (ratio < 0.5f) {
        const char* code = landS == Surface::Wall ? "WALL_LOW_GAIN"
                                                  : (sideDash && !r.handbrakeAtLanding ? "WD_SIDE_NO_POWERSLIDE" : "WD_LOW_GAIN");
        g.err(code, 0.40f * (100.f - gainScore));
    }
    // A bad contact attitude costs speed and control in game even when the impulse was applied: cap the score.
    const bool badAttitude = !r.landedOnWheels || pc < -8.f || pc > 30.f || rc > 15.f;
    if (badAttitude) score = std::min(score, 60.f);
    if (g.mech == Mech::WallDash) {
        g.variant = std::string("wall-") + g.variant;
        g.add("reattachTicks", static_cast<float>(r.landingTicks));
        const float tap = lerpPts(static_cast<float>(r.ticksSinceJumpPress), 10.f, 16.f);  // 85 ms green
        const float hold = lerpPts(static_cast<float>(r.firstJumpHoldTicks), 10.f, 16.f);
        score = 0.35f * gainScore + 0.25f * tap + 0.20f * timeScore + 0.20f * hold;
        if (r.ticksSinceJumpPress > 11) g.err("WALL_TAP_SLOW", 0.25f * (100.f - tap));   // > 95 ms
        if (r.firstJumpHoldTicks > 10) g.err("WALL_HOLD_LONG", 0.20f * (100.f - hold));  // > 85 ms
    } else if (r.phaseHadJump && r.maxHeight > 90.f) {
        g.err("WD_HIGH_HOP", 10.f);
        score -= 10.f;
    }
    g.success = !badAttitude && ratio >= 0.5f && score >= 60.f;
    // Zap dash practice: a dash done before the first landing is a regular wavedash, not a zap.
    if (g.mech == Mech::Wavedash && c.cfg.expect(Focus::ZapDash)) {
        g.mech = Mech::ZapDash;
        g.err("ZAP_NO_BOUNCE", 60.f);
        score = std::min(score, 40.f);
        g.success = false;
    }
    finalizeGrade(g, score);
    out.push_back(std::move(g));
    return true;
}

void WavedashDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    if (!c.ev.valid) return;
    const bool intent = c.cfg.expect(Focus::Wavedash) || c.cfg.expect(Focus::ChainDash) || c.cfg.expect(Focus::ZapDash);
    if (!intent) return;
    const MotionTracker& mt = c.mt;
    // Late press: the wheels were already down, so the press became a jump. Right after a landing that still
    // had a flip, stick tilted.
    if (c.ev.firstJump && mt.landingTick >= 0) {
        const int64_t since = c.s.frame - mt.landingTick;
        if (since <= 6 && mt.lastAirPhaseTicks >= 20 && mt.flipAvailableBeforeLanding &&
            std::max(std::fabs(c.s.in.pitch), std::fabs(c.s.in.yaw + c.s.in.roll)) >= 0.5f) {
            MechGrade g;
            g.mech = Mech::Wavedash;
            g.variant = "late";
            g.tickStart = mt.takeoffTick;
            g.tickEnd = c.s.frame;
            g.add("ticksAfterLanding", static_cast<float>(since));
            g.add("stickAtPress", stickMag(c.s.in));
            g.add("deadzone", c.cfg.dodgeDeadzone);
            g.add("hopHeight", mt.lastAirPhaseMaxHeight);
            g.err("WD_LATE_JUMPED", 90.f);
            finalizeGrade(g, 10.f);
            out.push_back(std::move(g));
            return;
        }
    }
    // Centered stick near the ground: the press made a double jump instead of a dash.
    if (c.ev.doubleJump && !grounded(c.s.car) && c.p.car.vel.z < 0.f) {
        const float h = dot(c.s.car.pos - mt.takeoffPos, mt.takeoffNormal);
        if (h < 80.f) {
            MechGrade g;
            g.mech = Mech::Wavedash;
            g.variant = "nostick";
            g.tickStart = mt.takeoffTick >= 0 ? mt.takeoffTick : c.s.frame;
            g.tickEnd = c.s.frame;
            g.add("stickAtPress", stickMag(c.s.in));
            g.add("deadzone", c.cfg.dodgeDeadzone);
            g.add("hopHeight", mt.phaseMaxHeight);
            g.err("WD_NO_STICK", 90.f);
            finalizeGrade(g, 10.f);
            out.push_back(std::move(g));
        }
    }
}

// ---- Chain wavedash ------------------------------------------------------------------------------------
void ChainWavedashDetector::onGrade(const MechGrade& g, const Ctx& c, std::vector<MechGrade>& out) {
    const bool isDash = g.mech == Mech::Wavedash || g.mech == Mech::ZapDash || g.mech == Mech::WallDash;
    if (!isDash || g.variant == "late" || g.variant == "nostick" || g.variant == "early") return;
    const int gap = c.cfg.chainMaxGapTicks;
    if (count_ > 0 && (g.tickStart - lastLanding_) > gap) flush(c, out);
    if (count_ == 0) {
        startTick_ = g.tickStart;
        boostTicks_ = handbrakeTicks_ = observedTicks_ = 0;
        maxSpeed_ = 0.f;
        scoreSum_ = gainSum_ = gapSum_ = 0.f;
        failures_ = 0;
        supersonicTick_ = -1;
        wall_ = g.mech == Mech::WallDash;
    } else {
        gapSum_ += static_cast<float>(g.tickStart - lastLanding_);
    }
    count_++;
    scoreSum_ += g.score;
    gainSum_ += g.metric("gainAlongDash", 0.f);
    maxSpeed_ = std::max(maxSpeed_, g.metric("speedAfter", 0.f));
    if (!g.success) failures_++;
    lastLanding_ = g.tickEnd;
}

void ChainWavedashDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    if (c.ev.reset) {
        count_ = 0;
        return;
    }
    if (count_ == 0) return;
    observedTicks_++;
    if (c.s.in.boost && c.s.car.boost > 0.f) boostTicks_++;
    if (c.s.in.handbrake) handbrakeTicks_++;
    const float sp = length(c.s.car.vel);
    maxSpeed_ = std::max(maxSpeed_, sp);
    if (supersonicTick_ < 0 && sp >= k::kSupersonicStartSpeed) supersonicTick_ = c.s.frame;
    // A takeoff soon enough after the last dash may still produce a dash grade (it arrives
    // DodgeTracker::kPostTicks after that phase lands): wait for it before flushing.
    const MotionTracker& mt = c.mt;
    const int gap = c.cfg.chainMaxGapTicks;
    const bool pending = mt.takeoffTick > lastLanding_ && mt.takeoffTick - lastLanding_ <= gap &&
                         !(mt.landingTick > mt.takeoffTick && c.s.frame - mt.landingTick > DodgeTracker::kPostTicks + 2);
    if (grounded(c.s.car) && c.s.frame - lastLanding_ > gap && !pending) flush(c, out);
}

void ChainWavedashDetector::flush(const Ctx& c, std::vector<MechGrade>& out) {
    (void)c;
    if (count_ >= 2) {
        MechGrade g;
        g.mech = wall_ ? Mech::WallChain : Mech::ChainWavedash;
        g.variant = wall_ ? "wall" : "floor";
        g.tickStart = startTick_;
        g.tickEnd = lastLanding_;
        const float n = static_cast<float>(count_);
        const float avgGap = gapSum_ / (n - 1.f);
        const float hbRatio = observedTicks_ ? static_cast<float>(handbrakeTicks_) / static_cast<float>(observedTicks_) : 0.f;
        const float boostUsed = static_cast<float>(boostTicks_) * k::kBoostUsedPerSecond * k::kDt;
        g.add("dashes", n);
        g.add("avgDashScore", scoreSum_ / n);
        g.add("avgGain", gainSum_ / n);
        g.add("avgGapTicks", avgGap);
        g.add("maxSpeed", maxSpeed_);
        g.add("boostTicks", static_cast<float>(boostTicks_));
        g.add("boostUsed", boostUsed);
        g.add("handbrakeRatio", hbRatio);
        g.add("failedDashes", static_cast<float>(failures_));
        if (supersonicTick_ >= 0) g.add("timeToSupersonic", static_cast<float>(supersonicTick_ - startTick_) * k::kDt);
        const float rhythm = lerpPts(avgGap, 4.f, 24.f);
        // On a wall the goal is a long regular run; on the floor it is supersonic without boost.
        const float speed = wall_ ? lerpPts(n, 4.f, 1.f) : lerpPts(maxSpeed_, k::kSupersonicStartSpeed, 1400.f);
        const float noBoost = boostTicks_ == 0 ? 100.f : lerpPts(static_cast<float>(boostTicks_), 0.f, 60.f);
        const float dashAvg = scoreSum_ / n;
        const float score = 0.35f * dashAvg + 0.25f * rhythm + 0.25f * speed + 0.15f * noBoost;
        if (failures_ > 0) g.err("CHAIN_BROKEN", std::max(0.35f * (100.f - dashAvg), 10.f));
        if (avgGap > 10.f) g.err(wall_ ? "WCHAIN_SLOW" : "CHAIN_GROUND_TIME", 0.25f * (100.f - rhythm));
        if (boostTicks_ > 0) g.err("CHAIN_BOOST_USED", std::max(0.15f * (100.f - noBoost), 8.f));
        if (hbRatio < 0.5f && !wall_) g.err("CHAIN_NO_POWERSLIDE", 8.f);
        if (!wall_ && maxSpeed_ < k::kSupersonicStartSpeed) g.err("CHAIN_NOT_SUPERSONIC", 0.25f * (100.f - speed));
        if (wall_) g.success = count_ >= 3 && failures_ == 0 && boostTicks_ == 0 && avgGap <= 16.f;
        else
            g.success = count_ >= 3 && maxSpeed_ >= k::kSupersonicStartSpeed && failures_ == 0 && boostTicks_ == 0 &&
                        avgGap <= 12.f;
        finalizeGrade(g, score);
        out.push_back(std::move(g));
    }
    count_ = 0;
}

// ---- Powerslide turn -----------------------------------------------------------------------------------
void PowerslideDetector::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    const Snapshot& s = c.s;
    if (c.ev.reset) {
        state_ = 0;
        return;
    }
    const Vec3 n = contactNormal(s.car);
    const float speed = length(tangent(s.car.vel, n));
    switch (state_) {
    case 0:
        if (grounded(s.car) && s.in.handbrake && speed > 400.f) {
            state_ = 1;
            start_ = s.frame;
            speedIn_ = speed;
            lastFwd_ = s.car.basis.fwd;
            heading_ = 0.f;
            throttleTicks_ = slideTicks_ = 0;
        }
        break;
    case 1:
        heading_ += planarAngleDeg(s.car.basis.fwd, lastFwd_, n);
        lastFwd_ = s.car.basis.fwd;
        slideTicks_++;
        if (s.in.throttle > 0.5f) throttleTicks_++;
        if (!s.in.handbrake || !grounded(s.car)) {
            state_ = 2;
            releaseTick_ = s.frame;
            exitSlip_ = speed > 100.f ? planarAngleDeg(s.car.basis.fwd, s.car.vel, n) : 0.f;
            if (exitSlip_ > 90.f) exitSlip_ = 180.f - exitSlip_;
        }
        break;
    case 2: {
        heading_ += planarAngleDeg(s.car.basis.fwd, lastFwd_, n);
        lastFwd_ = s.car.basis.fwd;
        float slip = speed > 100.f ? planarAngleDeg(s.car.basis.fwd, s.car.vel, n) : 0.f;
        if (slip > 90.f) slip = 180.f - slip;
        if (slip < 5.f || s.frame - releaseTick_ >= 30) {
            state_ = 0;
            if (slideTicks_ < 4 || heading_ < 20.f) break;  // tap or no real turn
            MechGrade g;
            g.mech = Mech::Powerslide;
            g.tickStart = start_;
            g.tickEnd = s.frame;
            const float retained = speed / std::max(speedIn_, 1.f);
            const float rate = heading_ / (static_cast<float>(std::max<int64_t>(s.frame - start_, 1)) * k::kDt);
            const float thr = static_cast<float>(throttleTicks_) / static_cast<float>(std::max(slideTicks_, 1));
            g.variant = heading_ >= 120.f ? "u-turn" : "cut";
            g.add("headingChangeDeg", heading_);
            g.add("slideTicks", static_cast<float>(slideTicks_));
            g.add("speedIn", speedIn_);
            g.add("speedOut", speed);
            g.add("speedRetained", retained);
            g.add("exitSlipDeg", exitSlip_);
            g.add("turnRateDegPerS", rate);
            g.add("throttleRatio", thr);
            // Where the nose points once the slide is over, relative to the ball (powerslide drill: the turn must
            // end facing the ball). Meaningless in free play, never displayed.
            if (s.ball.valid) {
                const Vec3 toBall = s.ball.pos - s.car.pos;
                g.add("aimErrorDeg", planarAngleDeg(s.car.basis.fwd, toBall, n));
            }
            const float keep = lerpPts(retained, 0.85f, 0.5f), slipS = lerpPts(exitSlip_, 8.f, 35.f),
                        rateS = lerpPts(rate, 150.f, 50.f);
            const float score = 0.5f * keep + 0.3f * slipS + 0.2f * rateS;
            if (retained < 0.75f) g.err("PS_SPEED_LOSS", 0.5f * (100.f - keep));
            if (exitSlip_ > 15.f) g.err("PS_EXIT_SLIP", 0.3f * (100.f - slipS));
            if (thr < 0.5f) g.err("PS_NO_THROTTLE", 10.f);
            g.success = score >= 65.f;
            finalizeGrade(g, score);
            out.push_back(std::move(g));
        }
        break;
    }
    default: state_ = 0; break;
    }
}

} // namespace rlt::mech
