#include "core/modules/shooting/FlickAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/modules/shooting/ShootGeometry.h"

namespace rlt::shoot {

namespace {
constexpr float kTeleportDist = 300.f; // uu in one tick: car max 19 uu/tick, ball max 50 uu/tick
constexpr int64_t kMaxFrameGap = 30; // frames: a longer gap (pause, reload) resets the analysis
constexpr int64_t kRoofRecentTicks = 36; // 0.3 s: ball on the roof recently enough for a flick
constexpr int64_t kEvalAfterTouch = 12; // 0.1 s after the last touch
constexpr int64_t kMaxAfterTouch = 30;
constexpr int64_t kNoContactTicks = 48; // 0.4 s after the dodge without a touch
constexpr int64_t kDodgedTimeout = 90; // 0.75 s
constexpr float kSeparatedDist = 170.f; // ball centre to hitbox centre once the ball left the car
constexpr float kInferredDv = 350.f; // uu/s: ball velocity change that proves a contact

float stickMag(const Inputs& in) { return std::fabs(in.pitch) + std::fabs(in.yaw) + std::fabs(in.roll); }
// Dodge direction the game derives from the input (DodgeForward/DodgeStrafe, else the raw stick), car-local.
Vec3 inputDodgeDir(const Inputs& in) {
    Vec3 d{in.dodgeForward, in.dodgeStrafe, 0.f};
    if (length(d) < 0.1f) d = {-in.pitch, in.yaw + in.roll, 0.f};
    return d;
}
constexpr float kDirAgreeDot = 0.5f; // engine vs stick direction: below this (60 degrees apart) they disagree
bool groundedCar(const CarState& c) { return c.onGround || c.wheelContacts >= 3; }
bool closeTo(float v, float edge, float margin) { return std::fabs(v - edge) < margin; }
} // namespace

// ---- Names -------------------------------------------------------------------------------------------------
const char* flickTypeId(FlickType t) {
    switch (t) {
    case FlickType::Front: return "front";
    case FlickType::Flick45: return "45";
    case FlickType::Flick90: return "90";
    case FlickType::Backflip: return "backflip";
    case FlickType::Delayed: return "delayed";
    case FlickType::Musty: return "musty";
    case FlickType::Breezi: return "breezi";
    case FlickType::Reverse: return "reverse";
    case FlickType::Diagonal: return "diagonal";
    case FlickType::Side: return "side";
    default: return "other";
    }
}

const char* flickTypeLabel(FlickType t) {
    switch (t) {
    case FlickType::Front: return "Flick avant";
    case FlickType::Flick45: return "Flick 45°";
    case FlickType::Flick90: return "Flick 90°";
    case FlickType::Backflip: return "Back flick";
    case FlickType::Delayed: return "Flick retardé";
    case FlickType::Musty: return "Musty flick";
    case FlickType::Breezi: return "Breezi flick";
    case FlickType::Reverse: return "Reverse 45";
    case FlickType::Diagonal: return "Flick diagonal";
    case FlickType::Side: return "Flick latéral";
    default: return "Flick";
    }
}

const char* flickTypeHudLabel(FlickType t) {
    switch (t) {
    case FlickType::Flick45: return "Flick en diagonale";
    case FlickType::Flick90: return "Flick sur le côté";
    case FlickType::Diagonal: return "Flick diagonal sans tourner";
    case FlickType::Side: return "Flick latéral sans tourner";
    default: return flickTypeLabel(t);
    }
}

bool parseFlickType(const std::string& text, int& out) {
    std::string s;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '-' || u == '_' || u == ' ' || u == '.') continue;
        s.push_back(static_cast<char>(std::tolower(u)));
    }
    struct Alias {
        const char* name;
        int type;
    };
    static const Alias kAliases[] = {
        {"any", -1},    {"tout", -1},      {"libre", -1},   {"all", -1},         {"", -1},
        {"other", -1},  {"front", 0},      {"avant", 0},    {"frontflick", 0},   {"45", 1},
        {"flick45", 1}, {"45flick", 1},    {"45deg", 1},    {"90", 2},           {"flick90", 2},
        {"90flick", 2}, {"90deg", 2},      {"backflip", 3}, {"back", 3},         {"backflick", 3},
        {"arriere", 3}, {"delayed", 4},    {"delay", 4},    {"retarde", 4},      {"delayedflick", 4},
        {"musty", 5},   {"mustyflick", 5}, {"breezi", 6},   {"breezy", 6},       {"breeziflick", 6},
        {"reverse", 7}, {"reverse45", 7},  {"mawkzy", 7},   {"reverseflick", 7}, {"diagonal", 8},
        {"diag", 8},    {"side", 9},       {"lateral", 9},
    };
    for (const auto& a : kAliases) {
        if (s == a.name) {
            out = a.type;
            return true;
        }
    }
    return false;
}

// ---- Classification ----------------------------------------------------------------------------------------
FlickClass classifyFlick(const FlickFeatures& f, const FlickThresholds& t) {
    FlickClass c;
    const float af = std::fabs(f.dodgeFwd), as = std::fabs(f.dodgeSide);
    const float ay = std::fabs(f.yawTurnDeg);
    const bool reverseShape = std::fabs(f.underside) >= 0.2f && f.launchFwd >= 0.4f && f.vertFrac <= 0.6f;
    if (af < 0.2f && as < 0.2f) {
        c.base = FlickType::Other;
        c.uncertain = true;
        return c;
    }
    if (f.dodgeFwd <= -0.5f) {
        // Backflip family: breezi (spin first), musty (nose down first), reverse (rolled on the side), back flick.
        if (f.rollDeg >= t.breeziRollDeg && f.airRollFrac >= t.breeziAirRollFrac) {
            c.base = FlickType::Breezi;
            c.uncertain = closeTo(f.rollDeg, t.breeziRollDeg, 50.f) || f.launchFwd < 0.3f;
        } else if (f.pitchDeg <= t.mustyPitchDeg) {
            c.base = FlickType::Musty;
            c.uncertain = closeTo(f.pitchDeg, t.mustyPitchDeg, 10.f) || f.launchFwd < 0.3f;
        } else if (reverseShape) {
            c.base = FlickType::Reverse;
            c.uncertain = std::fabs(f.underside) < 0.27f; // plain backflips measured 0.00..0.13, reverse >= 0.27
        } else {
            c.base = FlickType::Backflip;
            // Forward and flat launch without the nose down: maybe a musty with a short tilt.
            c.uncertain = f.launchFwd >= 0.4f && f.vertFrac <= 0.6f;
        }
    } else if (f.dodgeFwd <= -0.25f && reverseShape) {
        c.base = FlickType::Reverse;
        c.uncertain = std::fabs(f.underside) < 0.27f;
    } else if (as >= 0.35f && ay >= 65.f && ay <= 115.f) {
        c.base = FlickType::Flick90;
        c.uncertain = closeTo(ay, 65.f, 8.f) || closeTo(ay, 115.f, 8.f);
    } else if (as >= 0.35f && ay >= 25.f && ay < 65.f) {
        c.base = FlickType::Flick45;
        c.uncertain = closeTo(ay, 25.f, 6.f) || closeTo(ay, 65.f, 6.f);
    } else if (as >= 0.35f && af >= 0.35f) {
        c.base = FlickType::Diagonal;
    } else if (as >= 0.6f) {
        c.base = FlickType::Side;
    } else if (f.dodgeFwd >= 0.35f) {
        c.base = FlickType::Front;
    } else {
        c.base = FlickType::Other;
        c.uncertain = true;
    }
    const bool specific = c.base == FlickType::Breezi || c.base == FlickType::Musty || c.base == FlickType::Reverse ||
                          c.base == FlickType::Other;
    if (!specific && f.delay >= t.delayedSec && f.keptFrac >= t.delayedKeptFrac) c.delayed = true;
    return c;
}

FlickType categoryOf(const FlickClass& c) { return c.delayed ? FlickType::Delayed : c.base; }

bool flickMatches(const FlickClass& c, int requested) {
    if (requested < 0) return true;
    const FlickType want = static_cast<FlickType>(requested);
    if (want == FlickType::Delayed) return c.delayed;
    if (want == FlickType::Flick45 && c.base == FlickType::Reverse) return false;
    return c.base == want;
}

DelayBand flickDelayBand(FlickType t) {
    switch (t) {
    case FlickType::Musty: return {0.20f, 0.70f};
    case FlickType::Breezi: return {0.50f, 1.20f};
    case FlickType::Reverse: return {0.15f, 0.60f};
    case FlickType::Flick90: return {0.12f, 0.50f};
    case FlickType::Flick45: return {0.12f, 0.45f};
    case FlickType::Delayed: return {0.60f, 1.20f};
    default: return {0.10f, 0.40f};
    }
}

// ---- Analyzer ----------------------------------------------------------------------------------------------
void FlickAnalyzer::reset() {
    const FlickThresholds th = th_;
    const float dz = deadzone_;
    *this = FlickAnalyzer();
    th_ = th;
    deadzone_ = dz;
}

void FlickAnalyzer::enterIdle() {
    phase_ = FlickPhase::Idle;
    contact_ = false;
    lost_ = false;
    lostAtDodge_ = false;
    dodgePending_ = false;
    spinCount_ = 0;
    spinHead_ = 0;
}

float FlickAnalyzer::settledTime(int64_t nowFrame) const {
    if (phase_ != FlickPhase::Carry) return 0.f;
    return static_cast<float>(std::max<int64_t>(0, nowFrame - carryStart_)) * kTickDt;
}

void FlickAnalyzer::onTouch(const TouchEvent& t) {
    if (phase_ != FlickPhase::Dodged) return;
    if (t.pre.frame < dodgeFrame_ - 1) return; // contact of the carry, before the dodge
    if (!contact_) {
        contact_ = true;
        firstTouch_ = t.pre.frame;
    }
    if (t.pre.frame >= lastTouch_) {
        lastTouch_ = t.pre.frame;
        launchVel_ = t.post.ball.vel;
        launchFrame_ = t.post.frame;
    }
}

std::optional<FlickReport> FlickAnalyzer::update(const Snapshot& s) {
    std::optional<FlickReport> out;
    if (!s.car.valid || !s.ball.valid) {
        enterIdle();
        onRoof_ = false;
        hasPrev_ = false;
        return out;
    }
    if (hasPrev_) {
        const bool gap = s.frame <= prev_.frame || s.frame - prev_.frame > kMaxFrameGap;
        const float tol = kTeleportDist + 55.f * static_cast<float>(std::max<int64_t>(0, s.frame - prev_.frame - 1));
        const bool jump = distance(s.car.pos, prev_.car.pos) > tol || distance(s.ball.pos, prev_.ball.pos) > tol;
        if (jump || gap) {
            enterIdle();
            hasPrev_ = false;
        }
    }
    const bool jumpPress = hasPrev_ && s.in.jump && !prev_.in.jump;
    if (jumpPress) lastJumpPress_ = s.frame;
    const Hitbox hb = hitboxOf(s.car);
    const Vec3 hbCenter = hitboxCenter(s.car);
    onRoof_ = ballOnRoof(s.ball.pos, s.car.pos, s.car.basis, hb, s.ball.radius);
    offset_ = roofOffset(s.ball.pos, s.car.pos, s.car.basis, hb, s.ball.radius);
    const bool grounded = groundedCar(s.car);
    const bool prevGrounded = hasPrev_ && groundedCar(prev_.car);
    const float distToCar = distance(s.ball.pos, hbCenter);
    if (onRoof_) lastOnRoof_ = s.frame;

    switch (phase_) {
    case FlickPhase::Idle:
    case FlickPhase::Carry: {
        const float relSpeed = length2D(s.ball.vel - s.car.vel);
        if (onRoof_ && grounded) {
            if (phase_ == FlickPhase::Idle) {
                phase_ = FlickPhase::Carry;
                carryStart_ = s.frame;
                offsetAvg_ = offset_;
                relSpeedAvg_ = relSpeed;
            } else {
                offsetAvg_ = offsetAvg_ + (offset_ - offsetAvg_) * 0.08f;
                relSpeedAvg_ += (relSpeed - relSpeedAvg_) * 0.08f;
            }
        } else if (phase_ == FlickPhase::Carry && grounded && s.frame - lastOnRoof_ > 14) {
            phase_ = FlickPhase::Idle; // ball fell off while driving
        }
        const bool jumpEvidence = s.car.jumped || s.in.jump || (hasPrev_ && prev_.in.jump);
        if (phase_ == FlickPhase::Carry && prevGrounded && !grounded && jumpEvidence && s.frame - lastOnRoof_ <= 6) {
            phase_ = FlickPhase::Air;
            // The press precedes the take-off by a tick or two (flags lag the input): time the jump from it.
            const bool recentPress = lastJumpPress_ >= carryStart_ && s.frame - lastJumpPress_ <= 6;
            jumpFrame_ = recentPress ? lastJumpPress_ : s.frame;
            carryAtJump_ = static_cast<float>(std::max<int64_t>(0, jumpFrame_ - carryStart_)) * kTickDt;
            offsetAtJump_ = offsetAvg_;
            relSpeedAtJump_ = relSpeedAvg_;
            const Vec3 h = flat(prev_.car.basis.fwd);
            heading0_ = normalize(h, {1.f, 0.f, 0.f});
            const Vec3 tv = flat(prev_.car.vel);
            travel0_ = length(tv) > 200.f ? normalize(tv) : heading0_;
            carSpeedAtJump_ = length(prev_.car.vel);
            supersonic_ = prev_.car.supersonic || carSpeedAtJump_ >= kSupersonic;
            airTicks_ = airRollTicks_ = keptTicks_ = farTicks_ = groundTicks_ = 0;
            lost_ = false;
            rollAccum_ = 0.f;
            spinCount_ = spinHead_ = 0;
        }
        break;
    }
    case FlickPhase::Air: {
        ++airTicks_;
        rollAccum_ += dot(s.car.angVel, s.car.basis.fwd) * kTickDt;
        if (std::fabs(s.in.roll) >= 0.5f) ++airRollTicks_;
        if (distToCar <= th_.roofReach) {
            ++keptTicks_;
            farTicks_ = 0;
        } else if (++farTicks_ >= 6) {
            lost_ = true;
        }
        spin_[static_cast<size_t>(spinHead_)] = length(s.car.angVel);
        spinHead_ = (spinHead_ + 1) % static_cast<int>(spin_.size());
        spinCount_ = std::min(spinCount_ + 1, static_cast<int>(spin_.size()));

        const bool flagEdge = s.car.dodging && hasPrev_ && !prev_.car.dodging;
        // In game the press tick always comes first (flags lag the input by one tick): provisional dodge.
        const bool inputEdge = !flagEdge && jumpPress && prev_.car.hasFlip && !s.car.dodging && !prev_.car.dodging &&
                               !s.car.doubleJumped && stickMag(s.in) >= deadzone_;
        const bool doubleJumpEdge = hasPrev_ && s.car.doubleJumped && !prev_.car.doubleJumped && !s.car.dodging;
        if (flagEdge || inputEdge) {
            startDodge(s, distToCar, flagEdge);
        } else if (doubleJumpEdge) {
            enterIdle();
        } else if (grounded) {
            if (++groundTicks_ >= 3 && s.frame - jumpFrame_ > 12) {
                // Landed without a dodge: a pop or a jump with the ball, not graded.
                if (onRoof_) {
                    phase_ = FlickPhase::Carry;
                    carryStart_ = s.frame;
                    offsetAvg_ = offset_;
                    relSpeedAvg_ = length2D(s.ball.vel - s.car.vel);
                } else {
                    enterIdle();
                }
            }
        } else {
            groundTicks_ = 0;
        }
        break;
    }
    case FlickPhase::Dodged: {
        const int64_t since = s.frame - dodgeFrame_;
        if (dodgePending_ && !confirmDodge(s)) {
            enterIdle(); // the game turned the press into a double jump: a pop, not a flick
            break;
        }
        if (dodgePending_) break; // never report before the engine had its say
        if (contact_) {
            if ((s.frame >= lastTouch_ + kEvalAfterTouch && distToCar > kSeparatedDist) ||
                s.frame >= lastTouch_ + kMaxAfterTouch || since >= kDodgedTimeout) {
                out = buildReport(s, false);
                enterIdle();
            }
        } else {
            const Vec3 dv = s.ball.vel - vBallBefore_ + Vec3{0.f, 0.f, kGravity * static_cast<float>(since) * kTickDt};
            if (length(dv) > kInferredDv && distToCar > kSeparatedDist && since >= 4) {
                out = buildReport(s, true);
                enterIdle();
            } else if (since >= kNoContactTicks) {
                out = buildReport(s, false);
                enterIdle();
            }
        }
        break;
    }
    }
    prev_ = s;
    hasPrev_ = true;
    return out;
}

void FlickAnalyzer::startDodge(const Snapshot& s, float distToCar, bool confirmed) {
    // Stick direction: the input of this tick, or of the press tick when the engine flag is what we saw first.
    Vec3 stick = inputDodgeDir(s.in);
    if (length(stick) < 0.1f && confirmed && hasPrev_) stick = inputDodgeDir(prev_.in);
    inputDir_ = length(stick) >= 0.1f ? normalize(stick) : Vec3{};
    Vec3 dir = inputDir_;
    dodgeFromInput_ = true;
    dirMismatch_ = false;
    if (confirmed) {
        const Vec3 engine{s.car.dodgeDir.x, s.car.dodgeDir.y, 0.f};
        if (length(engine) >= 0.1f) {
            dir = normalize(engine);
            dodgeFromInput_ = false;
            dirMismatch_ = length(inputDir_) > 0.5f && dot(dir, inputDir_) < kDirAgreeDot;
        }
    }
    if (length(dir) < 0.1f) { // no direction at all (double jump, stall): not a flick
        enterIdle();
        return;
    }
    phase_ = FlickPhase::Dodged;
    // Engine flag seen first: the press was the previous tick when the button rose there.
    dodgeFrame_ = confirmed && s.frame - lastJumpPress_ <= 1 && lastJumpPress_ > jumpFrame_ ? lastJumpPress_ : s.frame;
    dodgeConfirmed_ = confirmed;
    dodgePending_ = !confirmed;
    contact_ = false;
    firstTouch_ = lastTouch_ = launchFrame_ = 0;
    feat_ = FlickFeatures{};
    feat_.dodgeFwd = dir.x;
    feat_.dodgeSide = dir.y;
    const Vec3& fwd = s.car.basis.fwd;
    feat_.pitchDeg = std::asin(clampf(fwd.z, -1.f, 1.f)) * kRadToDeg;
    const Vec3 h = flat(fwd);
    if (length(h) > 0.3f) {
        const Vec3 nh = normalize(h);
        feat_.yawTurnDeg = std::atan2(cross(heading0_, nh).z, dot(heading0_, nh)) * kRadToDeg;
    }
    feat_.underside = -s.car.basis.right.z;
    float maxSpin = 0.f;
    for (int i = 0; i < spinCount_; ++i) maxSpin = std::max(maxSpin, spin_[static_cast<size_t>(i)]);
    feat_.spinRate = maxSpin;
    feat_.rollDeg = std::fabs(rollAccum_) * kRadToDeg;
    feat_.airRollFrac = airTicks_ > 0 ? static_cast<float>(airRollTicks_) / static_cast<float>(airTicks_) : 0.f;
    feat_.delay = static_cast<float>(std::max<int64_t>(0, dodgeFrame_ - jumpFrame_)) * kTickDt;
    feat_.keptFrac = airTicks_ > 0 ? static_cast<float>(keptTicks_) / static_cast<float>(airTicks_) : 1.f;
    vBallBefore_ = hasPrev_ ? prev_.ball.vel : s.ball.vel;
    carSpeedAtDodge_ = length(s.car.vel);
    lostAtDodge_ = lost_ || (distToCar > th_.roofReach && s.frame - lastOnRoof_ > kRoofRecentTicks);
}

bool FlickAnalyzer::confirmDodge(const Snapshot& s) {
    const bool doubleJumpEdge = hasPrev_ && s.car.doubleJumped && !prev_.car.doubleJumped && !s.car.dodging;
    if (doubleJumpEdge) return false;
    if (s.car.dodging) {
        dodgePending_ = false;
        dodgeConfirmed_ = true;
        const Vec3 engine{s.car.dodgeDir.x, s.car.dodgeDir.y, 0.f};
        if (length(engine) >= 0.1f) {
            const Vec3 dir = normalize(engine);
            feat_.dodgeFwd = dir.x;
            feat_.dodgeSide = dir.y;
            dodgeFromInput_ = false;
            dirMismatch_ = length(inputDir_) > 0.5f && dot(dir, inputDir_) < kDirAgreeDot;
        }
        return true;
    }
    // Neither a dodge nor a double jump reported in time: keep the stick direction (flagged uncertain).
    if (s.frame - dodgeFrame_ >= kConfirmTicks) dodgePending_ = false;
    return true;
}

FlickReport FlickAnalyzer::buildReport(const Snapshot& s, bool inferred) {
    FlickReport r;
    r.f = feat_;
    r.contact = contact_ || inferred;
    r.inferredContact = inferred && !contact_;
    const Vec3 launch = contact_ ? launchVel_ : s.ball.vel;
    const int64_t launchFrame = contact_ ? launchFrame_ : s.frame;
    const float flight = static_cast<float>(std::max<int64_t>(0, launchFrame - dodgeFrame_)) * kTickDt;
    const Vec3 impulse = launch - vBallBefore_ + Vec3{0.f, 0.f, kGravity * flight};
    const float il = length(impulse);
    r.f.launchFwd = dot(normalize(flat(impulse)), travel0_);
    r.f.vertFrac = il > 1.f ? impulse.z / il : 0.f;
    r.cls = classifyFlick(r.f, th_);
    r.dodgeFromInput = dodgeFromInput_;
    r.dodgeConfirmed = dodgeConfirmed_;
    r.dirMismatch = dirMismatch_;
    // The stick direction is the one the game uses for the dodge: only an unconfirmed dodge (or an engine
    // direction contradicting the stick) makes the classification doubtful.
    if (!dodgeConfirmed_ || dirMismatch_) r.cls.uncertain = true;
    r.category = categoryOf(r.cls);
    r.lostBall = lostAtDodge_ && !r.contact;
    r.power = r.contact ? length(launch) : 0.f;
    r.carSpeedAtJump = carSpeedAtJump_;
    r.carSpeedAtDodge = carSpeedAtDodge_;
    r.gain = r.power - carSpeedAtDodge_;
    r.supersonicStart = supersonic_;
    r.carryTime = carryAtJump_;
    r.roofTime = carryAtJump_ + feat_.delay * clamp01(feat_.keptFrac);
    r.roofOffset = offsetAtJump_;
    r.relSpeedAtJump = relSpeedAtJump_;
    r.elevationDeg = std::atan2(launch.z, std::max(1e-3f, length2D(launch))) * kRadToDeg;
    r.dodgeToTouch = contact_ ? static_cast<float>(std::max<int64_t>(0, firstTouch_ - dodgeFrame_)) * kTickDt : 0.f;
    r.launchVel = launch;
    r.ballPos = s.ball.pos;
    r.ballVel = s.ball.vel;
    r.ballAngVel = s.ball.angVel;
    r.heading = heading0_;
    r.dodgeFrame = dodgeFrame_;
    r.frame = s.frame;
    return r;
}

} // namespace rlt::shoot
