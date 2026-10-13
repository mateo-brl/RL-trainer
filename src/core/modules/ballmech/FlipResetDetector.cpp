#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Flip reset state machine (research section 2.1):
//   Idle --(acquisition rising edge)--> Armed{tReset, alignment, relSpeed, source, chain}
//   Idle --(underside touch, no reset within 0.10 s, no flip)--> "no_reset" near miss (weak grade)
//   Armed --(new acquisition)--> Armed (chain++)      Armed --(arena landing)--> "wasted"
//   Armed --(dodge)--> Converting                     Armed --(double jump)--> Converting (jump)
//   Armed --(6 s)--> "timeout"
//   Converting --(touch, evaluated 2 ticks later)--> "used" / "used_jump"
//   Converting --(0.35 s dodge / 0.5 s jump without touch)--> "flip_missed"

void FlipResetDetector::reset() {
    state_ = State::Idle;
    lastAcquireT_ = -1e9f;
    lastDodgeT_ = -1e9f;
    prevActive_ = false;
    nearMissArmed_ = false;
    acquiredSource_ = 0;
    clearAttempt();
}

void FlipResetDetector::clearAttempt() {
    tStart_ = tReset_ = tDodge_ = 0.f;
    source_ = 0;
    chain_ = 0;
    evalAt_ = 0;
    alignment_ = relSpeed_ = ballSpeedAfter_ = 0.f;
    usedDoubleJump_ = viaJump_ = touchSeen_ = false;
    proj_ = GoalProjection{};
}

bool FlipResetDetector::engaged(float& since) const {
    if (state_ != State::Idle) {
        since = tStart_;
        return true;
    }
    if (nearMissArmed_) {
        since = tNearMiss_;
        return true;
    }
    return false;
}

std::optional<MechGrade> FlipResetDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)p;
    std::optional<MechGrade> out;
    acquiredSource_ = 0;
    if (cur.dodgeStart) lastDodgeT_ = cur.t;
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const Vec3 toBall = b.pos - hitboxCenter(c);
    const float dist = length(toBall);
    const float under = undersideAlignment(c, b);
    const bool airWorld = cur.worldWheels == 0 && c.pos.z > 120.f;

    // ---- Acquisition signals, strongest first
    int source = 0;
    if (cur.sdkReset) {
        source = 1;
    } else if (airWorld) {
        if (cur.ballWheels >= 3) source = 2;
        else if (c.hasFlip && !prev.s.car.hasFlip && dist < b.radius + 140.f && under > 0.6f && !c.dodging) source = 3;
    }
    const bool active = source != 0;
    const bool refractory = cur.t - lastAcquireT_ < 0.15f;  // one contact seen by several signals
    const bool newReset = active && !prevActive_ && !refractory;
    prevActive_ = active;

    if (newReset && (state_ == State::Idle || state_ == State::Armed)) {
        if (state_ == State::Idle) {
            clearAttempt();
            tStart_ = cur.t;
        }
        ++chain_;
        state_ = State::Armed;
        tReset_ = cur.t;
        source_ = source;
        lastAcquireT_ = cur.t;
        alignment_ = under;
        // Approach speed: before the contact kills it.
        relSpeed_ = std::max(length(c.vel - b.vel), length(prev.s.car.vel - prev.s.ball.vel));
        nearMissArmed_ = false;
        acquiredSource_ = source;
    } else if (active && state_ == State::Armed && source == 1 && source_ != 1) {
        source_ = 1;  // the game confirmed the reset found by the wheel signals
    } else if (state_ == State::Idle && airWorld && cur.touchNew && under > 0.5f &&
               c.basis.toLocal(b.pos - c.pos).z < 0.f && !c.hasFlip && !c.dodging && cur.t - lastDodgeT_ > 0.4f) {
        if (!nearMissArmed_) {
            nearMissArmed_ = true;
            tNearMiss_ = cur.t;
            nmAlign_ = under;
            nmRel_ = std::max(length(c.vel - b.vel), length(prev.s.car.vel - prev.s.ball.vel));
        }
    }

    if (state_ == State::Idle && nearMissArmed_ && cur.t - tNearMiss_ > 0.10f) {
        nearMissArmed_ = false;
        MechGrade g;
        g.family = Family::FlipReset;
        g.variant = "no_reset";
        g.tStart = tNearMiss_;
        g.tEnd = cur.t;
        g.weak = true;
        const float alignDeg = std::acos(clampf(nmAlign_, -1.f, 1.f)) * kRadToDeg;
        g.metrics = {makeMetric("alignment_deg", "Angle roues / balle", alignDeg, "°", 15.f),
                     makeMetric("rel_speed", "Vitesse d'approche", nmRel_, "uu/s", 400.f),
                     makeMetric("wheels_on_ball", "Roues sur la balle", static_cast<float>(c.wheelContacts), "", 4.f)};
        g.errors.push_back("FR_NO_RESET");
        if (nmAlign_ < 0.8f) g.errors.push_back("FR_POOR_ALIGNMENT");
        if (nmRel_ > 900.f) g.errors.push_back("FR_TOO_FAST_CONTACT");
        g.score = 0.15f * score01(nmAlign_, 0.5f, 0.95f);
        out = g;
    }

    if (state_ == State::Armed && !active) {
        if (cur.grounded) {
            out = finish(cur, "wasted", false);
        } else if (cur.dodgeStart) {
            tDodge_ = cur.t;
            viaJump_ = false;
            state_ = State::Converting;
        } else if (cur.doubleJumpStart) {
            usedDoubleJump_ = true;
            tDodge_ = cur.t;
            viaJump_ = true;
            state_ = State::Converting;
        } else if (cur.t - tReset_ > 6.f) {
            out = finish(cur, "timeout", false);
        }
    }
    if (state_ == State::Converting) {
        if (cur.touched && !touchSeen_) {
            touchSeen_ = true;
            evalAt_ = cur.frame + 2;
        }
        if (touchSeen_ && cur.frame >= evalAt_) {
            ballSpeedAfter_ = length(b.vel);
            proj_ = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
            out = finish(cur, viaJump_ ? "used_jump" : "used", true);
        } else if (!touchSeen_ && cur.t - tDodge_ > (viaJump_ ? 0.5f : 0.35f)) {
            out = finish(cur, "flip_missed", false);
        }
    }
    return out;
}

MechGrade FlipResetDetector::finish(const Frame& cur, const char* outcome, bool converted) {
    MechGrade g;
    g.family = Family::FlipReset;
    g.variant = outcome;
    g.tStart = tStart_;
    g.tEnd = cur.t;
    const float alignDeg = std::acos(clampf(alignment_, -1.f, 1.f)) * kRadToDeg;
    g.metrics.push_back(makeMetric("alignment_deg", "Angle roues / balle", alignDeg, "°", 15.f));
    g.metrics.push_back(makeMetric("rel_speed", "Vitesse d'approche", relSpeed_, "uu/s", 400.f));
    g.metrics.push_back(makeMetric("resets_in_chain", "Resets enchaînés", static_cast<float>(chain_), ""));
    g.metrics.push_back(makeMetric("source", "Signal (1 jeu, 2 roues, 3 flip)", static_cast<float>(source_), ""));
    g.metrics.push_back(makeMetric("used_double_jump", "Reset utilisé en saut", usedDoubleJump_ ? 1.f : 0.f, ""));
    if (converted) {
        g.metrics.push_back(makeMetric("time_to_use", "Délai reset - flip", tDodge_ - tReset_, "s"));
        g.metrics.push_back(makeMetric("ball_speed_after", "Vitesse de la balle", ballSpeedAfter_, "uu/s", 2000.f));
        if (proj_.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj_.onTarget ? 1.f : 0.f, "", 1.f));
        g.hasShot = true;
        g.ballSpeed = ballSpeedAfter_;
        g.projection = proj_;
    }
    g.success = converted;
    const std::string o = outcome;
    if (o == "wasted" || o == "timeout") g.errors.push_back("FR_RESET_WASTED");
    if (o == "flip_missed") g.errors.push_back("FR_FLIP_MISSED_BALL");
    if (alignment_ < 0.8f) g.errors.push_back("FR_POOR_ALIGNMENT");
    if (relSpeed_ > 900.f) g.errors.push_back("FR_TOO_FAST_CONTACT");
    if (converted && ballSpeedAfter_ < 1500.f) g.errors.push_back("FR_WEAK_SHOT");
    if (converted && proj_.valid && !proj_.onTarget) g.errors.push_back("FR_OFF_TARGET");
    const float align = score01(alignment_, 0.6f, 0.95f);
    const float soft = score01(relSpeed_, 1200.f, 400.f);
    const float conv = converted ? 0.5f + 0.25f * score01(ballSpeedAfter_, 1000.f, 2500.f) + 0.25f * accuracyScore(proj_)
                                 : 0.f;
    g.score = clamp01(0.30f + 0.20f * align + 0.15f * soft + 0.35f * conv);
    state_ = State::Idle;
    clearAttempt();
    return g;
}

} // namespace rlt::bm
