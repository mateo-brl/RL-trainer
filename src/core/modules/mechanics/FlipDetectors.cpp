#include "core/modules/mechanics/FlipDetectors.h"

namespace rlt::mech {

// ---- Kickoff context -----------------------------------------------------------------------------------
void KickoffTracker::clear() {
    phase_ = KickoffPhase::None;
    startTick_ = -1;
    firstJumpTicks_ = -1;
    timeToBall_ = -1.f;
    stillTicks_ = 0;
}

void KickoffTracker::begin(const Snapshot& s) {
    phase_ = KickoffPhase::Live;
    startTick_ = s.frame;
    startTime_ = s.time;
    noBoost_ = !s.in.boost;
    inAir_ = !grounded(s.car);
    firstJumpTicks_ = -1;
    timeToBall_ = -1.f;
    stillTicks_ = 0;
}

void KickoffTracker::step(const Ctx& c) {
    const Snapshot& s = c.s;
    if (c.ev.reset && !manual_) clear();
    if (phase_ == KickoffPhase::Live) {
        if (c.ev.valid && c.ev.firstJump && firstJumpTicks_ < 0) firstJumpTicks_ = s.frame - startTick_;
        if (s.frame - startTick_ > 600) clear();  // 5 s: the kickoff is over
        return;
    }
    if (manual_) return;
    if (!s.ball.valid || !s.car.valid) {
        stillTicks_ = 0;
        return;
    }
    const bool ballAtCentre = std::fabs(s.ball.pos.x) < 10.f && std::fabs(s.ball.pos.y) < 10.f && s.ball.pos.z < 130.f &&
                              length(s.ball.vel) < 10.f;
    const float dist = length2D(s.car.pos - s.ball.pos);
    const bool spawnDistance = dist > 1500.f && dist < 5200.f;
    if (!ballAtCentre || !spawnDistance) {
        stillTicks_ = 0;
        phase_ = KickoffPhase::None;
        return;
    }
    const float sp = length(s.car.vel);
    if (grounded(s.car) && sp < 15.f) {
        if (++stillTicks_ >= 12) phase_ = KickoffPhase::Countdown;
        return;
    }
    if (phase_ == KickoffPhase::Countdown) begin(s);
    else stillTicks_ = 0;
}

// ---- Speedflip -----------------------------------------------------------------------------------------
bool SpeedFlipDetector::onDodge(const DodgeRecord& r, const Ctx& c, const KickoffTracker& ko,
                                std::vector<MechGrade>& out) {
    if (r.surface != Surface::Floor) return false;
    const bool expect = c.cfg.expect(Focus::SpeedFlip);
    const float a = std::fabs(r.angleDeg);
    const bool diag = r.kind == DodgeKind::DiagForward || (r.kind == DodgeKind::Forward && a >= 5.f);
    const bool lowJump = r.ticksSinceFirstJump >= 0 && r.ticksSinceFirstJump <= 30;
    const bool cancelled = r.cancelTicks >= 0 && r.cancelTicks <= 20;
    const bool boosting = r.inputAtDodge.boost;
    if (!(diag || (expect && r.kind == DodgeKind::Forward))) return false;
    if (!lowJump) return false;
    if (!cancelled && !expect) return false;  // plain diagonal flip
    if (!boosting && !expect && !ko.live()) return false;

    MechGrade g;
    g.mech = Mech::SpeedFlip;
    g.variant = a < 5.f ? "straight" : (r.angleDeg >= 0.f ? "right" : "left");
    g.tickStart = r.tick - r.ticksSinceFirstJump;
    g.tickEnd = r.tick + std::max(r.landingTicks, 0);
    g.dodgeTick = r.tick;
    const float target = c.cfg.speedflipTargetAngleDeg;
    const float headingErr =
        r.landingTicks >= 0 ? planarAngleDeg(r.fwdAtLanding, r.fwdAtFirstJump, {0.f, 0.f, 1.f}) : 180.f;
    const float drift = r.landingTicks >= 0
                            ? dot(r.posAtLanding - r.posAtFirstJump,
                                  normalize(Vec3{r.rightAtFirstJump.x, r.rightAtFirstJump.y, 0.f}))
                            : 0.f;
    const float boostRatio = r.ticksTotal ? static_cast<float>(r.ticksBoost) / static_cast<float>(r.ticksTotal) : 0.f;
    const float throttleRatio =
        r.ticksTotal ? static_cast<float>(r.ticksThrottle) / static_cast<float>(r.ticksTotal) : 0.f;

    g.add("dodgeAngleDeg", r.angleDeg);
    g.add("absAngleDeg", a);
    g.add("targetAngleDeg", target);
    g.add("angleErrorDeg", a - target);
    g.add("firstJumpHoldTicks", static_cast<float>(r.firstJumpHoldTicks));
    g.add("jumpToDodgeTicks", static_cast<float>(r.ticksSinceFirstJump));
    g.add("cancelTicks", static_cast<float>(r.cancelTicks));
    g.add("cancelStrength", r.cancelStrength);
    g.add("boostRatio", boostRatio);
    g.add("throttleRatio", throttleRatio);
    g.add("landingTicks", static_cast<float>(r.landingTicks));
    g.add("headingErrorDeg", headingErr);
    g.add("lateralDriftUU", drift);
    g.add("speedAtLanding", length(tangent(r.velAtLanding, {0.f, 0.f, 1.f})));
    const bool kickoff = ko.live();
    if (kickoff) {
        g.variant += "-kickoff";
        g.add("kickoffFirstJumpTicks", static_cast<float>(ko.firstJumpTicks()));
    }

    const float angleScore = bandPts(a, target - 8.f, target + 8.f, target - 25.f, target + 25.f);
    const float cancelScore =
        r.cancelTicks >= 0 ? lerpPts(static_cast<float>(r.cancelTicks), static_cast<float>(c.cfg.speedflipCancelGreenTicks),
                                     static_cast<float>(c.cfg.speedflipCancelRedTicks))
                           : 0.f;
    const float jumpScore = lerpPts(static_cast<float>(r.ticksSinceFirstJump), 13.f, 30.f);
    const float boostScore = 100.f * boostRatio, throttleScore = 100.f * throttleRatio;
    const float inputScore = std::min(boostScore, throttleScore);
    const float landScore = r.landedOnWheels ? lerpPts(headingErr, 5.f, 25.f) : 0.f;
    const float driftScore = lerpPts(std::fabs(drift), 225.f, 600.f);  // SpeedFlipTrainer: < 225 green
    float score = 0.25f * angleScore + 0.25f * cancelScore + 0.10f * jumpScore + 0.15f * inputScore + 0.15f * landScore +
                  0.10f * driftScore;

    if (kickoff && ko.startedInAir()) {
        g.err("KO_STARTED_IN_AIR", 10.f);
        score -= 10.f;
    }
    if (kickoff && ko.startedNoBoost()) {
        g.err("KO_NO_BOOST_AT_START", 10.f);
        score -= 10.f;
    }
    if (r.cancelTicks < 0) g.err("SF_NO_CANCEL", 25.f);
    else if (r.cancelTicks > 9) g.err("SF_LATE_CANCEL", 0.25f * (100.f - cancelScore));
    if (a < target - 15.f) g.err("SF_ANGLE_STRAIGHT", 0.25f * (100.f - angleScore));
    if (a > target + 15.f) g.err("SF_ANGLE_WIDE", 0.25f * (100.f - angleScore));
    if (r.ticksSinceFirstJump > 18) g.err("SF_JUMP_LONG", 0.10f * (100.f - jumpScore));
    if (boostRatio < 0.9f) g.err("SF_BOOST_RELEASED", 0.15f * (100.f - boostScore));
    if (throttleRatio < 0.9f) g.err("SF_THROTTLE_RELEASED", 0.15f * (100.f - throttleScore));
    if (!r.landedOnWheels) g.err("SF_BAD_LANDING", 0.15f * (100.f - landScore) + 5.f);
    else if (headingErr > 12.f) g.err("SF_HEADING_OFF", 0.15f * (100.f - landScore));
    if (std::fabs(drift) > 425.f) g.err("SF_DRIFT", 0.10f * (100.f - driftScore));
    g.success = score >= 70.f && r.landedOnWheels && r.cancelTicks >= 0 && r.cancelTicks <= 10;
    finalizeGrade(g, score);
    if (kickoff && ko.timeToBall() < 0.f) {  // wait for the ball touch
        if (hasPending_) {
            pending_.add("timeToBall", -1.f);
            out.push_back(std::move(pending_));
        }
        pending_ = std::move(g);
        hasPending_ = true;
        fresh_ = true;
        return true;
    }
    if (kickoff) g.add("timeToBall", ko.timeToBall());
    out.push_back(std::move(g));
    return true;
}

void SpeedFlipDetector::onTick(const Ctx& c, const KickoffTracker& ko, std::vector<MechGrade>& out) {
    if (!hasPending_) return;
    const bool touched = ko.live() && ko.timeToBall() >= 0.f;
    if (touched || !ko.live() || c.s.frame - pending_.tickEnd > 360) {
        pending_.add("timeToBall", touched ? ko.timeToBall() : -1.f);
        out.push_back(std::move(pending_));
        pending_ = MechGrade{};
        hasPending_ = false;
        fresh_ = false;
    }
}

void SpeedFlipDetector::flush(std::vector<MechGrade>& out) {
    if (!hasPending_) return;
    pending_.add("timeToBall", -1.f);
    out.push_back(std::move(pending_));
    pending_ = MechGrade{};
    hasPending_ = false;
    fresh_ = false;
}

// ---- Half-flip -----------------------------------------------------------------------------------------
bool HalfFlipDetector::onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out) {
    const bool expect = c.cfg.expect(Focus::HalfFlip);
    const bool back = r.kind == DodgeKind::Back;
    const bool diagBack = r.kind == DodgeKind::DiagBack;
    const bool cancelled = r.cancelTicks >= 0 && r.cancelTicks <= 60;
    if (!(back || (diagBack && (cancelled || expect)))) return false;
    if (!cancelled && !expect) return false;  // ordinary backflip
    // Without intent, a late push on the stick with no air roll is a backflip landing, not a half-flip.
    if (!expect && r.rollStartTicks < 0 && r.cancelTicks > 36) return false;

    MechGrade g;
    g.mech = Mech::HalfFlip;
    g.variant = r.rollSign > 0.f ? "roll-right" : (r.rollSign < 0.f ? "roll-left" : "no-roll");
    g.tickStart = r.tick - std::max<int64_t>(r.ticksSinceFirstJump, 0);
    g.tickEnd = r.tick + std::max(r.landingTicks, 0);
    g.dodgeTick = r.tick;
    const Vec3 n = r.landingTicks >= 0 ? r.landingNormal : Vec3{0.f, 0.f, 1.f};
    const float headingChange = r.landingTicks >= 0 ? planarAngleDeg(r.fwdAtLanding, r.fwdAtDodge, n) : 0.f;
    const float headingErr = 180.f - headingChange;
    const Vec3 va = tangent(r.velAfter, n);
    const float exitSpeed = dot(va, normalize(tangent(r.fwdAtLanding, n)));
    const float rollDelay = (r.rollStartTicks >= 0 && r.cancelTicks >= 0)
                                ? static_cast<float>(r.rollStartTicks - r.cancelTicks)
                                : 999.f;

    g.add("dodgeAngleDeg", r.angleDeg);
    g.add("jumpToDodgeTicks", static_cast<float>(r.ticksSinceFirstJump));
    g.add("cancelTicks", static_cast<float>(r.cancelTicks));
    g.add("cancelStrength", r.cancelStrength);
    g.add("rollDelayTicks", rollDelay);
    g.add("upDotNAtRollStart", r.upDotNAtRollStart);
    g.add("landingTicks", static_cast<float>(r.landingTicks));
    g.add("headingChangeDeg", headingChange);
    g.add("upDotNAtLanding", r.upDotNAtLanding);
    g.add("exitSpeed", exitSpeed);
    g.add("speedBefore", length(r.velBefore));

    // Cancel window derived from RocketSim (224 rad/s^2 flip torque, 5.5 rad/s cap): cancelling ~0.2 s after the
    // dodge ends near 180 deg. UNVERIFIED in game.
    const float cancelScore = r.cancelTicks >= 0 ? bandPts(static_cast<float>(r.cancelTicks), 14.f, 30.f, 3.f, 48.f) : 0.f;
    const float headingScore = r.landingTicks >= 0 ? lerpPts(std::fabs(headingErr), 15.f, 50.f) : 0.f;
    const float landScore = r.landingTicks >= 0 ? lerpPts(r.upDotNAtLanding, 0.97f, 0.6f) : 0.f;
    // Roll once the car is past vertical: rolling earlier turns the pitch spin into yaw (crooked landing).
    const float rollScore =
        r.rollStartTicks < 0 ? 0.f : std::min(lerpPts(r.upDotNAtRollStart, 0.f, 0.6f), lerpPts(rollDelay, 40.f, 70.f));
    const float straightScore = lerpPts(180.f - std::fabs(r.angleDeg), 10.f, 35.f);
    const float durScore = r.landingTicks >= 0 ? lerpPts(static_cast<float>(r.landingTicks), 110.f, 170.f) : 0.f;
    const float score = 0.30f * cancelScore + 0.25f * headingScore + 0.20f * landScore + 0.10f * rollScore +
                        0.05f * straightScore + 0.10f * durScore;

    if (r.cancelTicks < 0) g.err("HF_NO_CANCEL", 30.f);
    else if (r.cancelTicks < 10) g.err("HF_EARLY_CANCEL", 0.30f * (100.f - cancelScore) + 5.f);
    else if (r.cancelTicks > 36) g.err("HF_LATE_CANCEL", 0.30f * (100.f - cancelScore) + 5.f);
    if (r.cancelTicks >= 0 && r.cancelStrength < 0.85f) g.err("HF_WEAK_CANCEL", 10.f);
    if (180.f - std::fabs(r.angleDeg) > 20.f) g.err("HF_NOT_STRAIGHT", 0.05f * (100.f - straightScore) + 3.f);
    if (r.rollStartTicks < 0) g.err("HF_NO_ROLL", 0.10f * (100.f - rollScore) + 5.f);
    else if (r.upDotNAtRollStart > 0.3f) g.err("HF_EARLY_ROLL", 0.10f * (100.f - rollScore) + 8.f);
    else if (rollDelay > 45.f) g.err("HF_LATE_ROLL", 0.10f * (100.f - rollScore) + 3.f);
    if (r.landingTicks < 0 || !r.landedOnWheels) g.err("HF_BAD_LANDING", 0.20f * (100.f - landScore) + 10.f);
    else if (std::fabs(headingErr) > 30.f) g.err("HF_HEADING_OFF", 0.25f * (100.f - headingScore));
    if (r.ticksSinceFirstJump > 20) g.err("HF_HIGH_JUMP", 8.f);
    g.success = r.cancelTicks >= 0 && r.landedOnWheels && std::fabs(headingErr) <= 30.f && score >= 60.f;
    finalizeGrade(g, score);
    out.push_back(std::move(g));
    return true;
}

// ---- Flip cancel ---------------------------------------------------------------------------------------
bool FlipCancelDetector::onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out) {
    if (r.kind == DodgeKind::Stall || r.kind == DodgeKind::Side || std::fabs(r.dir.x) <= 0.1f) return false;
    const bool expect = c.cfg.expect(Focus::FlipCancel);
    const bool cancelled = r.cancelTicks >= 0 && r.cancelTicks <= kMaxCancelTicks;
    if (!cancelled && !expect) return false;

    MechGrade g;
    g.mech = Mech::FlipCancel;
    g.variant = dodgeVariant(r.kind, r.angleDeg);
    g.tickStart = r.tick;
    g.tickEnd = r.tick + std::max(r.landingTicks, 0);
    g.dodgeTick = r.tick;
    g.add("dodgeAngleDeg", r.angleDeg);
    g.add("cancelTicks", static_cast<float>(r.cancelTicks));
    g.add("cancelStrength", r.cancelStrength);
    g.add("landingTicks", static_cast<float>(r.landingTicks));
    g.add("landedOnWheels", r.landedOnWheels ? 1.f : 0.f);

    const float timing = cancelled ? lerpPts(static_cast<float>(r.cancelTicks), 6.f, 20.f) : 0.f;
    const float strength = r.cancelTicks >= 0 ? lerpPts(r.cancelStrength, 0.95f, 0.5f) : 0.f;
    const float landing = r.landingTicks < 0 ? 50.f : (r.landedOnWheels ? 100.f : 0.f);
    const float score = 0.5f * timing + 0.3f * strength + 0.2f * landing;
    if (!cancelled) g.err("FC_NO_CANCEL", 50.f);
    else if (r.cancelTicks > 9) g.err("FC_LATE", 0.5f * (100.f - timing));
    if (r.cancelTicks >= 0 && r.cancelStrength < 0.85f) g.err("FC_WEAK", 0.3f * (100.f - strength) + 3.f);
    if (r.landingTicks >= 0 && !r.landedOnWheels) g.err("FC_BAD_LANDING", 0.2f * (100.f - landing));
    g.success = cancelled && r.cancelTicks <= 10 && r.cancelStrength >= 0.8f && score >= 65.f;
    finalizeGrade(g, score);
    out.push_back(std::move(g));
    return true;
}

// ---- Combos --------------------------------------------------------------------------------------------
void ComboDetector::onGrade(const MechGrade& g, std::vector<MechGrade>& out) {
    if (g.mech == Mech::SpeedFlip) {
        if (usedSF_ && g.dodgeTick >= 0 && g.dodgeTick == usedSFDodgeTick_) return;  // its speed zap is out already
        lastSpeedflip_ = g;
        haveSF_ = true;
        return;
    }
    if (g.mech == Mech::HalfFlip) {
        lastHalfFlip_ = g;
        haveHF_ = true;
        return;
    }
    if (g.mech == Mech::ZapDash && haveSF_ && g.tickStart >= lastSpeedflip_.tickEnd &&
        g.tickStart - lastSpeedflip_.tickEnd <= kMaxGapTicks) {
        out.push_back(combine(Mech::SpeedZap, lastSpeedflip_, g));
        haveSF_ = false;
        usedSF_ = true;
        usedSFDodgeTick_ = lastSpeedflip_.dodgeTick;
    }
    if ((g.mech == Mech::Wavedash || g.mech == Mech::ZapDash) && g.variant != "late" && g.variant != "nostick" &&
        haveHF_ && g.tickStart >= lastHalfFlip_.tickEnd && g.tickStart - lastHalfFlip_.tickEnd <= kMaxGapTicks) {
        out.push_back(combine(Mech::HalfFlipWavedash, lastHalfFlip_, g));
        haveHF_ = false;
    }
}

MechGrade ComboDetector::combine(Mech m, const MechGrade& a, const MechGrade& b) {
    MechGrade g;
    g.mech = m;
    g.variant = a.variant + "+" + b.variant;
    g.tickStart = a.tickStart;
    g.tickEnd = b.tickEnd;
    const float gapTicks = static_cast<float>(b.tickStart - a.tickEnd);
    g.add("firstScore", a.score);
    g.add("secondScore", b.score);
    g.add("gapTicks", gapTicks);
    // Errors of both parts, so the tip points at the weaker part.
    for (const auto& e : a.errors) g.errors.push_back(e);
    for (const auto& e : b.errors) g.errors.push_back(e);
    const float gap = lerpPts(gapTicks, 10.f, static_cast<float>(kMaxGapTicks));
    if (gapTicks > 20.f) g.err("COMBO_SLOW_LINK", 0.2f * (100.f - gap));
    g.success = a.success && b.success;
    finalizeGrade(g, 0.4f * a.score + 0.4f * b.score + 0.2f * gap);
    return g;
}

} // namespace rlt::mech
