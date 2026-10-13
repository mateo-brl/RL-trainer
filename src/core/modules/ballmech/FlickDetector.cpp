#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

// Flick family (research sections 2.3 to 2.6):
//   Idle -> Carry (ball in the roof box, car on the ground) -> Air (left the ground after >= 0.10 s of carry)
//   Air: ball farther than 280 uu for 6 ticks -> lost; landing without dodge -> Idle (dribble pop, not graded);
//        double jump -> Idle; a second separate contact after the takeoff -> Idle (the ball is bumped: air dribble,
//        a carried flick keeps a continuous roof contact); flip gone without a dodge or 1.5 s after the jump -> Idle
//   Air -> Dodged -> grade (touch within 0.32 s) | fail FL_LOST_BALL / FL_NO_CONTACT
// Classification order: breezi, musty, reverse, backflip, 90, 45, side, front, other; "+_delayed" when the
// jump-to-dodge delay is >= 0.6 s with the ball kept on the car for >= 70 % of the air ticks.

namespace {
constexpr int kMaxAirNewTouches = 2;  // separate contacts after the takeoff that turn the attempt into an air dribble
constexpr int kNoFlipTicks = 3;       // ticks without flip (and without dodge) before giving up (flag ordering slack)
constexpr float kMaxAirTime = 1.5f;   // the flip expires 1.25 s after the jump (research): no flick after that
} // namespace

FlickBand flickDelayBand(const std::string& v) {
    if (v == "musty") return {0.20f, 0.70f};
    if (v == "breezi") return {0.50f, 1.20f};
    if (v == "reverse") return {0.15f, 0.60f};
    if (v == "90") return {0.12f, 0.50f};
    if (v == "45") return {0.12f, 0.45f};
    if (v.find("_delayed") != std::string::npos) return {0.60f, 1.20f};
    return {0.10f, 0.40f};
}

void FlickDetector::reset() {
    const std::string t = target_;
    *this = FlickDetector(t);
}

void FlickDetector::restart() { reset(); }

bool FlickDetector::engaged(float& since) const {
    if (state_ != State::Air && state_ != State::Dodged) return false;
    since = tCarry_;
    return true;
}

std::optional<MechGrade> FlickDetector::update(const Frame& prev, const Frame& cur, const DetectParams& p) {
    (void)p;
    std::optional<MechGrade> out;
    const CarState& c = cur.s.car;
    const BallState& b = cur.s.ball;
    const bool grounded = cur.grounded;
    const float dt = std::max(0.f, cur.t - prev.t);
    switch (state_) {
    case State::Idle:
    case State::Carry: {
        Vec3 L;
        const bool geo = grounded && carryGeometry(cur.s, &L);
        if (geo) {
            if (state_ == State::Idle) {
                state_ = State::Carry;
                tCarry_ = cur.t;
                nCarry_ = 0;
                sumVz_ = 0.f;
            }
            ++nCarry_;
            sumVz_ += L.z;
            lastGeo_ = cur.t;
            roofZ_ = roofBallHeight(c, b.radius);
        } else if (state_ == State::Carry && cur.t - lastGeo_ > 0.12f && grounded) {
            state_ = State::Idle;
        }
        // Jump with the ball on the car: airborne phase.
        if (state_ == State::Carry && !grounded && prev.grounded) {
            const float carry = prev.t - tCarry_;
            if (carry >= 0.10f) {
                state_ = State::Air;
                tJump_ = cur.t;
                carryTime_ = carry;
                avgVz_ = nCarry_ ? sumVz_ / static_cast<float>(nCarry_) : 0.f;
                const CarState& pc = prev.s.car;
                const Vec3 rel = prev.s.ball.vel - pc.vel;
                relHAtJump_ = length(rel - pc.basis.up * dot(rel, pc.basis.up));
                heading0_ = normalize(flat(pc.basis.fwd), {1.f, 0.f, 0.f});
                travel0_ = normalize(flat(pc.vel));
                if (length(travel0_) < 0.5f) travel0_ = heading0_;
                supersonicStart_ = pc.supersonic || length(pc.vel) >= arena::kSupersonic;
                rollAccum_ = 0.f;
                airTicks_ = airRollTicks_ = keptTicks_ = farTicks_ = airNewTouches_ = noFlipTicks_ = 0;
                lost_ = false;
                flipSeen_ = c.hasFlip;
            } else {
                state_ = State::Idle;
            }
        }
        break;
    }
    case State::Air: {
        ++airTicks_;
        rollAccum_ += dot(c.angVel, c.basis.fwd) * dt;
        if (std::fabs(cur.s.in.roll) >= 0.5f) ++airRollTicks_;
        const float d = distance(b.pos, c.pos);
        if (d <= 280.f) {  // UNVERIFIED reach
            ++keptTicks_;
            farTicks_ = 0;
        } else if (++farTicks_ >= 6) {
            lost_ = true;
        }
        if (cur.doubleJumpStart) {
            state_ = State::Idle;  // double jump pop, not a flick
            break;
        }
        if (cur.dodgeStart) {
            if (length(cur.dodgeDir) < 0.1f) {
                state_ = State::Idle;
                break;
            }
            state_ = State::Dodged;
            tDodge_ = cur.t;
            dodge_ = cur.dodgeDir;
            pitchDeg_ = std::asin(clampf(c.basis.fwd.z, -1.f, 1.f)) * kRadToDeg;
            const Vec3 h = flat(c.basis.fwd);
            if (length(h) > 0.3f) {
                const Vec3 hn = normalize(h);
                yawDeg_ = std::atan2(cross(heading0_, hn).z, dot(heading0_, hn)) * kRadToDeg;
            } else {
                yawDeg_ = 0.f;
            }
            underside_ = -c.basis.right.z;
            vBefore_ = prev.s.ball.vel;
            touchSeen_ = false;
            break;
        }
        const float sinceJump = cur.t - tJump_;
        // Separate new contacts after the takeoff: the ball is bumped in the air (air dribble), not carried.
        if (cur.touched && cur.touchNew && sinceJump > 0.1f && ++airNewTouches_ >= kMaxAirNewTouches) {
            state_ = State::Idle;
            break;
        }
        // Flip expired (or used by something we did not see as a dodge): no flick can follow any more. Only trusted
        // once the flag was seen true in this jump, so a game that never reports it falls back to the time cap.
        if (c.hasFlip) flipSeen_ = true;
        noFlipTicks_ = (c.hasFlip || !flipSeen_) ? 0 : noFlipTicks_ + 1;
        if (noFlipTicks_ >= kNoFlipTicks || sinceJump > kMaxAirTime) {
            state_ = State::Idle;
            break;
        }
        if (grounded && sinceJump > 0.1f) state_ = State::Idle;  // landed without flicking: dribble pop, not graded
        break;
    }
    case State::Dodged: {
        if (cur.touched) {
            if (!touchSeen_) {
                touchSeen_ = true;
                tTouch_ = cur.t;
                evalAt_ = cur.frame + 2;
            } else if (cur.t - tTouch_ < 0.1f) {
                evalAt_ = std::max(evalAt_, cur.frame + 2);  // the flip keeps pushing: wait for the release
            }
        }
        if (touchSeen_ && cur.frame >= evalAt_) {
            out = grade(cur);
        } else if (!touchSeen_ && cur.t - tDodge_ > 0.32f) {
            out = failGrade(cur, lost_ ? "FL_LOST_BALL" : "FL_NO_CONTACT");
        }
        break;
    }
    }
    return out;
}

std::string FlickDetector::classify(float launchFwd, float vertFrac) const {
    const float f = dodge_.x, side = dodge_.y;
    const float rollDeg = std::fabs(rollAccum_ * kRadToDeg);
    const float airRollFrac = airTicks_ ? static_cast<float>(airRollTicks_) / static_cast<float>(airTicks_) : 0.f;
    if (f <= -0.5f && rollDeg >= 230.f && airRollFrac >= 0.3f) return "breezi";  // UNVERIFIED spin gate
    if (f <= -0.5f && pitchDeg_ <= -35.f) return "musty";                         // UNVERIFIED nose-down gate
    if (f <= -0.25f && std::fabs(underside_) >= 0.2f && launchFwd >= 0.4f && vertFrac <= 0.6f) return "reverse";
    if (f <= -0.5f) return "backflip";
    const float ay = std::fabs(yawDeg_);
    if (ay >= 65.f && ay <= 115.f) return "90";
    if (ay >= 25.f && ay < 65.f) return "45";
    if (std::fabs(side) >= 0.6f) return "side";
    if (f >= 0.35f) return "front";
    return "other";
}

MechGrade FlickDetector::failGrade(const Frame& cur, const char* code) {
    MechGrade g;
    g.family = Family::Flick;
    g.variant = target_.empty() ? "unknown" : target_;
    g.tStart = tCarry_;
    g.tEnd = cur.t;
    g.metrics.push_back(makeMetric("carry_time", "Dribble avant le saut", carryTime_, "s", 0.3f));
    g.metrics.push_back(makeMetric("delay", "Délai saut - flip", tDodge_ > 0.f ? tDodge_ - tJump_ : -1.f, "s"));
    g.metrics.push_back(makeMetric("yaw_deg", "Rotation avant le flip", yawDeg_, "°"));
    g.metrics.push_back(makeMetric("pitch_deg", "Inclinaison au flip", pitchDeg_, "°"));
    g.errors.push_back(code);
    if (carryTime_ < 0.2f || relHAtJump_ > 300.f) g.errors.push_back("FL_JUMP_UNSETTLED");
    g.score = 0.10f * score01(carryTime_, 0.f, 0.3f);
    restart();
    return g;
}

MechGrade FlickDetector::grade(const Frame& cur) {
    const BallState& b = cur.s.ball;
    const float since = cur.t - tDodge_;
    const Vec3 impulse = b.vel - vBefore_ - Vec3{0.f, 0.f, arena::kGravityZ * since};
    const float dv = length(impulse);
    const float launchFwd = dot(normalize(flat(impulse)), travel0_);
    const float vertFrac = dv > 1e-3f ? impulse.z / dv : 0.f;
    const float delay = tDodge_ - tJump_;
    const float keptFrac = airTicks_ ? static_cast<float>(keptTicks_) / static_cast<float>(airTicks_) : 0.f;
    std::string v = classify(launchFwd, vertFrac);
    if (delay >= 0.60f && keptFrac >= 0.7f && v != "breezi") v += "_delayed";
    const std::string base = v.substr(0, v.find("_delayed"));
    const GoalProjection proj = projectToGoal(b.pos, b.vel, cur.team, b.radius, cur.standardArena);
    const float latency = tTouch_ - tDodge_;
    const float speedAfter = length(b.vel);

    MechGrade g;
    g.family = Family::Flick;
    g.variant = v;
    g.tStart = tCarry_;
    g.tEnd = cur.t;
    g.metrics.push_back(makeMetric("carry_time", "Dribble avant le saut", carryTime_, "s", 0.3f));
    g.metrics.push_back(makeMetric("roof_gap", "Hauteur balle / toit", avgVz_ - roofZ_, "uu", 0.f));
    g.metrics.push_back(makeMetric("rel_speed_at_jump", "Vitesse relative au saut", relHAtJump_, "uu/s", 50.f));
    g.metrics.push_back(makeMetric("delay", "Délai saut - flip", delay, "s"));
    g.metrics.push_back(makeMetric("yaw_deg", "Rotation avant le flip", yawDeg_, "°"));
    g.metrics.push_back(makeMetric("pitch_deg", "Inclinaison au flip", pitchDeg_, "°"));
    g.metrics.push_back(makeMetric("roll_deg", "Roulis accumulé", rollAccum_ * kRadToDeg, "°"));
    g.metrics.push_back(makeMetric("dodge_forward", "Flip avant / arrière", dodge_.x, ""));
    g.metrics.push_back(makeMetric("dodge_side", "Flip latéral", dodge_.y, ""));
    g.metrics.push_back(makeMetric("dodge_to_touch", "Délai flip - contact", latency, "s", 0.08f));
    g.metrics.push_back(makeMetric("ball_dv", "Impulsion donnée", dv, "uu/s", 1600.f));
    g.metrics.push_back(makeMetric("launch_forward", "Direction vers l'avant", launchFwd, ""));
    g.metrics.push_back(makeMetric("launch_vertical_frac", "Part verticale", vertFrac, ""));
    g.metrics.push_back(makeMetric("ball_speed_after", "Vitesse de la balle", speedAfter, "uu/s", 2000.f));
    if (proj.valid) g.metrics.push_back(makeMetric("on_target", "Cadré", proj.onTarget ? 1.f : 0.f, "", 1.f));
    g.hasShot = true;
    g.ballSpeed = speedAfter;
    g.projection = proj;

    // ---- Errors
    const std::string& want = target_.empty() ? base : target_;
    if (carryTime_ < 0.2f || relHAtJump_ > 300.f) g.errors.push_back("FL_JUMP_UNSETTLED");
    if (supersonicStart_ && (want == "breezi" || want == "musty")) g.errors.push_back("FL_SUPERSONIC_START");
    if (want == "45" && std::fabs(yawDeg_) < 25.f && base != "musty") g.errors.push_back("FL_UNDER_ROTATED");
    if (want == "45" && std::fabs(yawDeg_) > 65.f) g.errors.push_back("FL_OVER_ROTATED");
    if (want == "musty" && base != "musty" && base != "breezi") g.errors.push_back("FL_MUSTY_NOSE_NOT_DOWN");
    if (want == "breezi" && base != "breezi") g.errors.push_back("FL_BREEZI_NO_SPIN");
    const FlickBand band = flickDelayBand(target_.empty() ? v : target_);
    if (delay < band.lo - 0.05f) g.errors.push_back("FL_FLIP_TOO_EARLY");
    if (delay > band.hi + 0.10f) g.errors.push_back("FL_FLIP_TOO_LATE");
    if (dv < 700.f) g.errors.push_back("FL_WEAK");
    if (proj.valid && !proj.onTarget) g.errors.push_back("FL_OFF_TARGET");

    // ---- Score
    const float carryQ = (scoreBandF(avgVz_, roofZ_ - 30.f, roofZ_ + 20.f, 60.f) + score01(relHAtJump_, 300.f, 50.f) +
                          score01(carryTime_, 0.10f, 0.30f)) / 3.f;
    const float timingQ = scoreBandF(delay, band.lo, band.hi, 0.30f);
    float rotQ = 1.f;
    if (base == "45") rotQ = scoreBandF(std::fabs(yawDeg_), 35.f, 55.f, 20.f);
    else if (base == "90") rotQ = scoreBandF(std::fabs(yawDeg_), 80.f, 100.f, 20.f);
    else if (base == "musty") rotQ = scoreBandF(pitchDeg_, -120.f, -50.f, 25.f);
    if (!target_.empty() && base != target_) rotQ *= 0.3f;
    const float latencyQ = score01(latency, 0.32f, 0.08f);
    const float powerQ = score01(dv, 600.f, 1600.f);
    const float accQ = accuracyScore(proj);
    const bool matches = target_.empty() || base == target_;
    g.success = dv >= 700.f && matches;
    float total = 0.15f * carryQ + 0.20f * timingQ + 0.15f * rotQ + 0.10f * latencyQ + 0.25f * powerQ + 0.15f * accQ;
    if (!matches) total = std::min(total, 0.55f);  // requested mechanic not performed: capped
    g.score = clamp01(total);
    restart();
    return g;
}

} // namespace rlt::bm
