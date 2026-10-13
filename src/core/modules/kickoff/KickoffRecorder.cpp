#include "core/modules/kickoff/KickoffRecorder.h"

#include <algorithm>
#include <cmath>

namespace rlt {

namespace {
constexpr int kDodgeDedupTicks = 6;  // input edge and engine "dodging" flag of the same dodge
constexpr float kFlipIntoBallWindow = 0.35f;
} // namespace

const char* kickoffTypeName(KickoffType t) {
    switch (t) {
    case KickoffType::Speedflip: return "Speedflip";
    case KickoffType::FrontFlip: return "Front flip";
    case KickoffType::DoubleFlip: return "Double flip";
    case KickoffType::DiagonalFlip: return "Flip diagonal";
    case KickoffType::Wavedash: return "Wavedash";
    case KickoffType::BoostOnly: return "Boost seul";
    case KickoffType::Delayed: return "Kickoff retardé";
    case KickoffType::Fake: return "Fake";
    case KickoffType::BoostRoute: return "Route boost";
    case KickoffType::Missed: return "Balle manquée";
    default: return "Inconnu";
    }
}

void KickoffRecorder::begin(KickoffSpawn spawn, KickoffTeam team, int64_t goFrame, const KickoffRecorderParams& p) {
    prm_ = p;
    m_ = KickoffMetrics{};
    m_.spawn = spawn;
    m_.team = team;
    m_.along.reserve(static_cast<size_t>(std::max(0, prm_.maxSamples)));
    path_ = kickoffPath(spawn, team);
    prev_ = Snapshot{};
    havePrev_ = false;
    started_ = true;
    prevJump_ = prevDodging_ = airborneFromJump_ = usedSecond_ = false;
    goFrame_ = goFrame;
    lastFrame_ = goFrame - 1;
    lastDodgeFrame_ = -1000;
    devN_ = boostTicks_ = boostHeld_ = boostHeldWithTank_ = 0;
    devSq_ = 0.f;
    lastDodge_ = -1.f;
}

void KickoffRecorder::registerDodge(float t, float fwd, float side, int64_t frame) {
    ++m_.dodges;
    lastDodge_ = t;
    lastDodgeFrame_ = frame;
    if (m_.dodge < 0.f) {
        m_.dodge = t;
        const float l = std::sqrt(fwd * fwd + side * side);
        m_.dodgeForward = l > 1e-6f ? fwd / l : 0.f;
        m_.dodgeSide = l > 1e-6f ? side / l : 0.f;
        m_.dodgeAngleDeg = std::atan2(m_.dodgeSide, m_.dodgeForward) * kRadToDeg;
    } else if (m_.secondDodge < 0.f) {
        m_.secondDodge = t;
    }
}

void KickoffRecorder::feed(const Snapshot& sn) {
    if (!started_ || sn.frame <= lastFrame_) return;
    lastFrame_ = sn.frame;
    const float t = timeOf(sn.frame);
    if (t < 0.f || m_.touched || m_.estimated) return;
    if (!sn.car.valid) return;
    const CarState& c = sn.car;
    const Inputs& in = sn.in;

    if (m_.reaction < 0.f && (in.throttle > 0.5f || in.boost)) m_.reaction = t;

    // Path.
    const float a = path_.along(c.pos), lat = path_.lateral(c.pos);
    m_.progress = std::max(m_.progress, a);
    if (static_cast<int>(m_.along.size()) < prm_.maxSamples) m_.along.push_back(a);
    devSq_ += lat * lat;
    ++devN_;
    m_.pathDevMax = std::max(m_.pathDevMax, std::fabs(lat));
    if (sn.ball.valid) m_.minDistToBall = std::min(m_.minDistToBall, length2D(sn.ball.pos - c.pos));

    // Boost economy and discipline.
    if (havePrev_) {
        if (prev_.car.boost > 0.5f) {
            ++boostTicks_;
            if (in.boost) ++boostHeld_;
        }
        if (prev_.in.boost && prev_.car.boost > 0.5f) ++boostHeldWithTank_;
        const float db = c.boost - prev_.car.boost;
        if (db > 5.f) {
            m_.boostPicked += db;
            ++m_.padsPicked;
        } else if (db < 0.f) {
            m_.boostUsed += -db;
        }
    }

    // Jumps and dodges: input edges, with the engine "dodging" flag as a second source.
    const bool jumpEdge = in.jump && !prevJump_;
    if (jumpEdge) {
        if (c.onGround) {
            if (m_.firstJump < 0.f) m_.firstJump = t;
            airborneFromJump_ = true;
            usedSecond_ = false;
        } else if (airborneFromJump_ && !usedSecond_) {
            usedSecond_ = true;
            const float stick = std::fabs(in.pitch) + std::fabs(in.yaw) + std::fabs(in.roll);
            if (stick >= prm_.dodgeDeadzone && sn.frame - lastDodgeFrame_ > kDodgeDedupTicks)
                registerDodge(t, -in.pitch, in.yaw + in.roll, sn.frame);
        }
    }
    if (c.dodging && !prevDodging_ && sn.frame - lastDodgeFrame_ > kDodgeDedupTicks) {
        usedSecond_ = true;
        if (length2D(c.dodgeDir) > 0.1f) registerDodge(t, c.dodgeDir.x, c.dodgeDir.y, sn.frame);
        else registerDodge(t, -in.pitch, in.yaw + in.roll, sn.frame);
    }
    prevDodging_ = c.dodging;

    if (m_.dodge >= 0.f && m_.cancel < 0.f && t > m_.dodge && t - m_.dodge <= prm_.cancelWindow &&
        in.pitch >= prm_.cancelPitch)
        m_.cancel = t;
    if (m_.dodge >= 0.f && m_.landing < 0.f && t > m_.dodge && c.onGround) m_.landing = t;
    if (c.onGround && havePrev_ && !prev_.car.onGround) {
        airborneFromJump_ = false;
        usedSecond_ = false;
    }
    prevJump_ = in.jump;
    prev_ = sn;
    havePrev_ = true;
}

void KickoffRecorder::touch(const Snapshot& pre, const Snapshot& post) {
    if (!started_ || m_.touched || m_.estimated) return;
    if (pre.frame < goFrame_) return;
    if (pre.frame > lastFrame_) feed(pre);
    const float t = timeOf(pre.frame);
    m_.touched = true;
    m_.ticksToTouch = static_cast<int>(pre.frame - goFrame_);
    m_.timeToTouch = t;
    // Events after the contact tick do not belong to the approach.
    if (m_.firstJump > t) m_.firstJump = -1.f;
    if (m_.secondDodge > t) {
        m_.secondDodge = -1.f;
        --m_.dodges;
    }
    if (m_.dodge > t) {
        m_.dodge = m_.cancel = m_.landing = -1.f;
        m_.dodges = 0;
    }
    if (pre.car.valid) {
        const CarState& c = pre.car;
        const Vec3 toBall = pre.ball.pos - c.pos;
        m_.speedAtTouch = length(c.vel);
        // Horizontal speed toward the ball: the ball centre sits ~76 uu above the car origin, so the 3D direction
        // would count only ~90 % of a perfect head-on approach (benchmarks and the ghost use horizontal speeds).
        m_.speedToBall = dot(flat(c.vel), normalize(flat(toBall)));
        m_.lateralOffset = dot(toBall, c.basis.right);
        m_.contactHeight = dot(toBall, c.basis.up);
        m_.boostAtTouch = c.boost;
    }
    m_.flipIntoBall = lastDodge_ >= 0.f && lastDodge_ <= t && t - lastDodge_ <= kFlipIntoBallWindow;
    if (!m_.along.empty()) m_.along.back() = std::max(m_.along.back(), path_.length);
    if (post.ball.valid && post.frame > pre.frame) {
        m_.hasBallExit = true;
        m_.ballExitVel = post.ball.vel;
        const float s = kickoffAttackSign(m_.team);
        const Vec3 v = flat(post.ball.vel);
        m_.ballExitAngleDeg = length(v) > 1.f ? std::fabs(std::atan2(v.x, v.y * s)) * kRadToDeg : 0.f;
        m_.projectedY = (post.ball.pos.y + post.ball.vel.y * 0.5f) * s;
    }
}

void KickoffRecorder::estimate(const Snapshot& now) {
    if (!started_ || m_.touched || m_.estimated || !now.car.valid) return;
    const float t = std::max(0.f, timeOf(now.frame));
    const float remaining = std::max(0.f, path_.length - path_.along(now.car.pos));
    const float vAlong = dot(now.car.vel, path_.dir);
    m_.estimated = true;
    m_.timeToTouch = t + remaining / std::max(vAlong, 400.f);
    m_.ticksToTouch = static_cast<int>(std::lround(m_.timeToTouch * kTickRate));
    m_.speedToBall = std::max(0.f, vAlong);
    m_.speedAtTouch = length(now.car.vel);
    m_.boostAtTouch = now.car.boost;
    m_.lateralOffset = 0.f;
    m_.flipIntoBall = false;
}

const KickoffMetrics& KickoffRecorder::finish() {
    m_.pathDevRms = devN_ ? std::sqrt(devSq_ / static_cast<float>(devN_)) : 0.f;
    m_.boostHeldRatio = boostTicks_ ? static_cast<float>(boostHeld_) / static_cast<float>(boostTicks_) : 0.f;
    m_.boostLooksUnlimited = boostHeldWithTank_ >= 24 && m_.boostUsed < 1.f;
    m_.type = classify(m_);
    return m_;
}

KickoffType KickoffRecorder::classify(const KickoffMetrics& m) {
    if (!m.touched && !m.estimated) {
        if (m.progress < 600.f && m.padsPicked > 0 && m.boostPicked >= 12.f) return KickoffType::BoostRoute;
        if (m.progress < 1200.f) return KickoffType::Fake;
        return KickoffType::Missed;
    }
    const bool early = m.dodge >= 0.f && m.dodge <= 1.1f;
    const bool cancelled = m.cancel >= 0.f && m.cancel - m.dodge <= 0.20f;
    if (m.dodge >= 0.f) {
        if (early && cancelled && std::fabs(m.dodgeSide) >= 0.25f && m.dodgeForward > 0.3f)
            return KickoffType::Speedflip;
        if (m.landing >= 0.f && m.landing - m.dodge <= 0.10f && m.dodgeForward > 0.5f) return KickoffType::Wavedash;
        if (m.secondDodge >= 0.f) return KickoffType::DoubleFlip;
        if (std::fabs(m.dodgeSide) >= 0.35f) return KickoffType::DiagonalFlip;
        if (m.dodgeForward >= 0.45f) return KickoffType::FrontFlip;
    }
    const KickoffBenchmark slow = kickoffBenchmark(m.spawn, KickoffLevel::Beginner);
    if (m.reaction > 0.30f || m.timeToTouch > slow.arrive + 0.40f) return KickoffType::Delayed;
    return KickoffType::BoostOnly;
}

} // namespace rlt
