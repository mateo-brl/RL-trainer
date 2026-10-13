#include "core/modules/kickoff/KickoffGhost.h"

#include <algorithm>
#include <cmath>

namespace rlt {

void KickoffGhost::init(KickoffSpawn spawn, KickoffTeam team, KickoffGhostProfile profile) {
    path_ = kickoffPath(spawn, team);
    prof_ = std::move(profile);
    yaw_ = std::atan2(path_.dir.y, path_.dir.x);
    const float d = path_.length;
    contactSpeed_ = std::max(0.f, prof_.contactSpeed);
    if (prof_.model == KickoffGhostModel::Samples) {
        model_ = KickoffGhostModel::Samples;
        arrive_ = std::max(prof_.arrive, kTickDt);
        if (prof_.samples.empty()) prof_.samples.push_back(0.f);
        return;
    }
    arrive_ = std::max(prof_.arrive, prof_.reaction + 0.05f);
    const float T = arrive_ - prof_.reaction;
    const float v = std::max(1.f, prof_.contactSpeed);
    model_ = prof_.model;
    // Accel-then-cruise is feasible iff d < v*T <= 2d (reach v before T and cover exactly d).
    if (model_ == KickoffGhostModel::AccelCruise && v * T > d && v * T <= 2.f * d) {
        accel_ = v * v / (2.f * (v * T - d));
        vmax_ = v;
    } else {
        model_ = KickoffGhostModel::ConstAccel;  // s = a t^2 / 2, v_end = 2d / T
        accel_ = 2.f * d / (T * T);
        vmax_ = 2.f * d / T;
        contactSpeed_ = vmax_;
    }
}

float KickoffGhost::sampleAlong(float t) const {
    // Polyline (i * dt, samples[i]) for i * dt < arrive, then (arrive, length).
    const auto& s = prof_.samples;
    const float dt = prof_.sampleDt;
    if (t <= 0.f) return std::min(s.front(), path_.length);
    if (t >= arrive_) return path_.length;
    const size_t i = static_cast<size_t>(t / dt);
    const float t0 = static_cast<float>(i) * dt;
    if (i + 1 < s.size()) {
        const float t1 = t0 + dt;
        if (t1 <= arrive_) return std::min(lerpf(s[i], s[i + 1], (t - t0) / dt), path_.length);
    }
    // Last segment: from the last sample before arrival to the path end at `arrive`.
    const size_t last = std::min(i, s.size() - 1);
    const float tl = static_cast<float>(last) * dt;
    const float span = std::max(1e-4f, arrive_ - tl);
    return std::min(lerpf(s[last], path_.length, clamp01((t - tl) / span)), path_.length);
}

float KickoffGhost::along(float t) const {
    if (model_ == KickoffGhostModel::Samples) return sampleAlong(t);
    const float tt = t - prof_.reaction;
    if (tt <= 0.f) return 0.f;
    float s;
    if (model_ == KickoffGhostModel::AccelCruise) {
        const float t1 = vmax_ / accel_;
        s = tt <= t1 ? 0.5f * accel_ * tt * tt : 0.5f * accel_ * t1 * t1 + vmax_ * (tt - t1);
    } else {
        s = 0.5f * accel_ * tt * tt;
    }
    return std::min(s, path_.length);
}

float KickoffGhost::speedAlong(float t) const {
    if (model_ == KickoffGhostModel::Samples) {
        const float t1 = std::min(t, arrive_);
        const float s1 = sampleAlong(t1), s0 = sampleAlong(t1 - kTickDt);
        return std::max(0.f, (s1 - s0) / kTickDt);
    }
    const float tt = std::min(t, arrive_) - prof_.reaction;
    if (tt <= 0.f) return 0.f;
    if (model_ == KickoffGhostModel::AccelCruise) return std::min(vmax_, accel_ * tt);
    return accel_ * tt;
}

float KickoffGhost::timeAt(float s) const {
    if (s >= path_.length) return arrive_;
    float lo = 0.f, hi = arrive_;
    for (int i = 0; i < 40; ++i) {
        const float m = 0.5f * (lo + hi);
        (along(m) < s ? lo : hi) = m;
    }
    return hi;
}

KickoffGhostState KickoffGhost::stateAt(float t) const {
    KickoffGhostState g;
    g.along = along(t);
    g.pos = path_.pointAt(g.along);
    g.vel = path_.dir * speedAlong(t);
    g.yawRad = yaw_;
    g.arrived = t >= arrive_;
    return g;
}

// ---- Duel ----------------------------------------------------------------------------------------------------
const char* kickoffDuelName(KickoffDuel d) {
    switch (d) {
    case KickoffDuel::PlayerClear: return "Kickoff gagné (net)";
    case KickoffDuel::PlayerNarrow: return "Kickoff gagné (de peu)";
    case KickoffDuel::Neutral: return "50/50 neutre";
    case KickoffDuel::GhostNarrow: return "Kickoff perdu (de peu)";
    default: return "Kickoff perdu (net)";
    }
}

float kickoffContactQuality(float lateralOffset, bool flipIntoBall) {
    const float q = 1.f - std::min(std::fabs(lateralOffset) / 140.f, 0.5f);
    return q + (flipIntoBall ? 0.08f : 0.f);
}

KickoffDuelResult kickoffResolveDuel(const KickoffPlayerContact& p, float ghostArrive, float ghostSpeed,
                                     const KickoffDuelParams& prm) {
    KickoffDuelResult r;
    r.ghostPower = ghostSpeed * prm.ghostQuality;
    if (!p.touched) {
        r.outcome = KickoffDuel::GhostClear;
        r.dt = -99.f;
        return r;
    }
    r.dt = ghostArrive - p.time;
    r.playerPower = std::max(0.f, p.speedToBall) * kickoffContactQuality(p.lateralOffset, p.flipIntoBall);
    if (r.dt >= prm.clearWindow) {
        r.outcome = KickoffDuel::PlayerClear;
        return r;
    }
    if (r.dt <= -prm.clearWindow) {
        r.outcome = KickoffDuel::GhostClear;
        return r;
    }
    const float eff = r.playerPower + r.dt * prm.timeToSpeed;
    const float rel = (eff - r.ghostPower) / std::max({eff, r.ghostPower, 1.f});
    r.outcome = rel > prm.neutralBand ? KickoffDuel::PlayerNarrow
                                      : (rel < -prm.neutralBand ? KickoffDuel::GhostNarrow : KickoffDuel::Neutral);
    return r;
}

float kickoffExtraImpulseFactor(float relSpeed) {
    if (relSpeed <= 500.f) return 0.65f;
    if (relSpeed <= 2300.f) return lerpf(0.65f, 0.55f, (relSpeed - 500.f) / 1800.f);
    if (relSpeed <= 4600.f) return lerpf(0.55f, 0.30f, (relSpeed - 2300.f) / 2300.f);
    return 0.30f;
}

Vec3 kickoffCarHitDir(const Vec3& carPos, const Vec3& carFwd, const Vec3& ballPos) {
    const Vec3 rel = ballPos - carPos;
    Vec3 h = normalize(Vec3{rel.x, rel.y, rel.z * 0.35f});
    h = normalize(h - carFwd * (dot(h, carFwd) * (1.f - 0.65f)));
    return h;
}

Vec3 kickoffGhostBallImpulse(const Vec3& ghostPos, const Vec3& ghostVel, const Vec3& ballPos, const Vec3& ballVel,
                             float scale) {
    const Vec3 fwd = normalize(flat(ghostVel));
    const Vec3 toBall = normalize(flat(ballPos - ghostPos));
    const float approach = dot(ghostVel - ballVel, toBall);
    if (approach <= 0.f) return {};  // ball already leaving faster: no contact
    const Vec3 n = normalize(toBall * 85.6f + Vec3{0.f, 0.f, 35.65f});
    const Vec3 base = n * (6.f / 7.f * std::max(0.f, dot(ghostVel - ballVel, n)));
    const float rel = std::min(length(ghostVel - ballVel), 4600.f);
    const Vec3 extra = kickoffCarHitDir(ghostPos, fwd, ballPos) * (rel * kickoffExtraImpulseFactor(rel));
    return (base + extra) * scale;
}

float kickoffLateralToExitAngleDeg(float lateralOffset, float forwardDist) {
    return std::atan2(lateralOffset, 0.65f * forwardDist) * kRadToDeg;
}

Vec3 kickoffDuelBallImpulse(KickoffDuel d, const Vec3& ballVel, const Vec3& ghostDir, const Vec3& ghostImpulseAtRest) {
    const Vec3 h = flat(ballVel);
    switch (d) {
    case KickoffDuel::PlayerClear: return {};
    case KickoffDuel::PlayerNarrow: return h * -0.45f + Vec3{0.f, 0.f, 450.f};  // slowed, popped, still forward
    case KickoffDuel::Neutral: return h * -0.9f + Vec3{0.f, 0.f, 850.f};        // classic kickoff pop
    case KickoffDuel::GhostNarrow: return h * -1.f + ghostDir * 1100.f + Vec3{0.f, 0.f, 500.f};
    default: return h * -1.f + ghostImpulseAtRest;  // ghost wins outright
    }
}

} // namespace rlt
