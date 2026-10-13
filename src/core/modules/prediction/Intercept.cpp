#include "core/modules/prediction/Intercept.h"

#include <algorithm>
#include <cmath>

namespace rlt::pr {

namespace {
constexpr float kProfileDt = 1.f / 120.f;
constexpr int kProfileSteps = 1200;  // 10 s, longer than any horizon

// Vertical motion of the first jump while the button is held (jump hold acceleration minus gravity).
constexpr float kHoldNetAccel = car::kJumpHoldAccel + phys::kGravityZ;
constexpr float kRiseAfterHold = car::kJumpImpulse * car::kJumpHoldTime + 0.5f * kHoldNetAccel * car::kJumpHoldTime * car::kJumpHoldTime;
constexpr float kSpeedAfterHold = car::kJumpImpulse + kHoldNetAccel * car::kJumpHoldTime;
constexpr float kG = -phys::kGravityZ;  // 650, positive

// Smallest positive root of a*t^2 + b*t + c = 0 (a may be 0); negative when none.
float firstRoot(float a, float b, float c) {
    if (std::fabs(a) < 1e-6f) return std::fabs(b) < 1e-6f ? -1.f : -c / b;
    const float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return -1.f;
    const float sq = std::sqrt(disc);
    const float t1 = (-b - sq) / (2.f * a), t2 = (-b + sq) / (2.f * a);
    const float lo = std::min(t1, t2), hi = std::max(t1, t2);
    if (lo >= 0.f) return lo;
    return hi >= 0.f ? hi : -1.f;
}
} // namespace

CarReachModel::CarReachModel() : boost_(build(true)), noBoost_(build(false)) {}

float CarReachModel::throttleAccel(float speed) {
    if (speed < 0.f) speed = 0.f;
    if (speed < 1400.f) return car::kThrottleAccelZero + (car::kThrottleAccel1400 - car::kThrottleAccelZero) * speed / 1400.f;
    if (speed < car::kMaxThrottleSpeed) return car::kThrottleAccel1400 * (car::kMaxThrottleSpeed - speed) / 10.f;
    return 0.f;
}

CarReachModel::Profile CarReachModel::build(bool boost) {
    Profile p;
    p.v.reserve(kProfileSteps + 1);
    p.x.reserve(kProfileSteps + 1);
    const float vMax = boost ? car::kMaxSpeed : car::kMaxThrottleSpeed;
    float v = 0.f, x = 0.f;
    p.v.push_back(v);
    p.x.push_back(x);
    for (int k = 1; k <= kProfileSteps; ++k) {
        const float a = throttleAccel(v) + (boost && v < car::kMaxSpeed ? car::kBoostAccelGround : 0.f);
        const float vNew = std::min(vMax, v + a * kProfileDt);
        x += 0.5f * (v + vNew) * kProfileDt;
        v = vNew;
        p.v.push_back(v);
        p.x.push_back(x);
    }
    p.vTerminal = p.v.back();
    return p;
}

void CarReachModel::advance(const Profile& p, float v0, float seconds, float& dist, float& vEnd) {
    if (!(seconds > 0.f)) {
        dist = 0.f;
        vEnd = v0;
        return;
    }
    if (v0 >= p.vTerminal - 0.5f) {  // nothing left to gain: cruise (cars have no air drag)
        dist = v0 * seconds;
        vEnd = v0;
        return;
    }
    const int last = static_cast<int>(p.v.size()) - 1;
    const float tEnd = static_cast<float>(last) * kProfileDt;
    auto sampleAt = [&](const std::vector<float>& arr, float t, float slopeAfter) {
        const float f = t / kProfileDt;
        if (f >= static_cast<float>(last)) return arr.back() + slopeAfter * (t - tEnd);
        const int i = static_cast<int>(f);
        const float u = f - static_cast<float>(i);
        return arr[static_cast<size_t>(i)] + (arr[static_cast<size_t>(i + 1)] - arr[static_cast<size_t>(i)]) * u;
    };
    // The motion only depends on the speed, so starting at v0 = the rest profile shifted to where it reaches v0.
    float t0 = 0.f;
    if (v0 > 0.f) {
        const auto it = std::lower_bound(p.v.begin(), p.v.end(), v0);
        const int k = static_cast<int>(it - p.v.begin());
        if (k <= 0) t0 = 0.f;
        else {
            const float lo = p.v[static_cast<size_t>(k - 1)], hi = p.v[static_cast<size_t>(k)];
            const float frac = hi > lo ? (v0 - lo) / (hi - lo) : 0.f;
            t0 = (static_cast<float>(k - 1) + frac) * kProfileDt;
        }
    }
    const float t1 = t0 + seconds;
    dist = sampleAt(p.x, t1, p.vTerminal) - sampleAt(p.x, t0, p.vTerminal);
    vEnd = sampleAt(p.v, t1, 0.f);
}

float CarReachModel::distance(float v0, float seconds, float boostSeconds, float* endSpeed) const {
    if (!std::isfinite(v0)) v0 = 0.f;
    v0 = std::max(0.f, v0);
    float d = 0.f, v = v0;
    if (seconds > 0.f) {
        if (boostSeconds > 0.f) {
            const float tb = std::min(seconds, boostSeconds);
            float d1 = 0.f, v1 = v0;
            advance(boost_, v0, tb, d1, v1);
            float d2 = 0.f, v2 = v1;
            advance(noBoost_, v1, seconds - tb, d2, v2);
            d = d1 + d2;
            v = v2;
        } else {
            advance(noBoost_, v0, seconds, d, v);
        }
    }
    if (endSpeed) *endSpeed = v;
    return d;
}

bool CarReachModel::jumpTime(float rise, bool canAerial, float& seconds, JumpKind& kind) {
    seconds = 0.f;
    kind = JumpKind::None;
    if (!std::isfinite(rise)) return false;
    if (rise <= 0.f) return true;
    if (rise > car::kAerialMaxRise) return false;

    if (rise <= car::kSingleJumpMaxRise) {
        kind = JumpKind::Jump;
        if (rise <= kRiseAfterHold) {
            seconds = firstRoot(0.5f * kHoldNetAccel, car::kJumpImpulse, -rise);
        } else {
            const float tau = firstRoot(-0.5f * kG, kSpeedAfterHold, kRiseAfterHold - rise);
            seconds = tau < 0.f ? -1.f : car::kJumpHoldTime + tau;
        }
        return seconds >= 0.f;
    }

    // Second jump after a full first jump and a short release.
    const float d = car::kSecondJumpDelay;
    const float hD = kRiseAfterHold + kSpeedAfterHold * d - 0.5f * kG * d * d;
    const float vD = kSpeedAfterHold - kG * d + car::kJumpImpulse;
    if (rise <= car::kDoubleJumpMaxRise) {
        kind = JumpKind::DoubleJump;
        const float tau = firstRoot(-0.5f * kG, vD, hD - rise);
        seconds = tau < 0.f ? -1.f : car::kJumpHoldTime + d + tau;
        if (seconds >= 0.f) return true;
    }
    if (!canAerial) {
        kind = JumpKind::None;
        seconds = 0.f;
        return false;
    }
    // Aerial: boost straight up after the double jump (conservative: real fast aerials boost earlier).
    kind = JumpKind::Aerial;
    const float aUp = car::kAerialBoostAccel - kG;
    const float tau = firstRoot(0.5f * aUp, vD, hD - rise);
    seconds = tau < 0.f ? -1.f : car::kJumpHoldTime + d + tau;
    if (seconds < 0.f) {
        kind = JumpKind::None;
        seconds = 0.f;
        return false;
    }
    return true;
}

InterceptResult findIntercept(const PredictedPath& path, const InterceptCar& carState, const InterceptParams& prm,
                              const CarReachModel& model) {
    InterceptResult res;
    if (!path.valid || path.samples.empty()) return res;
    const int n = path.size();
    const int end = path.goalIndex >= 0 ? path.goalIndex : n;  // a ball past the goal line is gone
    const Vec3 velFlat = flat(carState.vel);
    const Vec3 fwdFlat = normalize(flat(carState.forward), normalize(velFlat, {1.f, 0.f, 0.f}));
    const bool canAerial = prm.useBoost && carState.boost >= prm.aerialMinBoost;

    for (int i = 0; i < end; ++i) {
        const PathSample& s = path.samples[static_cast<size_t>(i)];
        if (s.t < prm.minTime) continue;
        if (path.arena == ArenaMode::Soccar && std::fabs(s.pos.y) > phys::kArenaY) continue;  // inside a goal

        float jt = 0.f;
        JumpKind kind = JumpKind::None;
        if (!CarReachModel::jumpTime(s.pos.z - prm.groundReachZ, canAerial, jt, kind)) continue;
        if (jt > s.t) continue;

        const Vec3 to = flat(s.pos - carState.pos);
        const float dist = length(to);
        const float need = std::max(0.f, dist - prm.reach);
        float penalty = 0.f;
        float arrival = length(velFlat);
        if (need > 0.f) {
            const Vec3 dir = to / dist;
            const float angle = std::acos(clampf(dot(fwdFlat, dir), -1.f, 1.f));
            penalty = prm.turnPenalty * angle / kPi;
            const float v0 = std::max(0.f, dot(velFlat, dir));
            const float tOff = s.t - penalty - jt;
            if (tOff < 0.f) continue;
            float boostLeft = carState.boost;
            if (kind == JumpKind::Aerial) boostLeft -= prm.aerialMinBoost;
            const float boostSeconds = prm.useBoost ? std::max(0.f, boostLeft) / car::kBoostPerSecond : 0.f;
            float vOff = v0;
            const float ground = model.distance(v0, tOff, boostSeconds, &vOff);
            if (ground + vOff * jt < need) continue;
            arrival = vOff;
        }
        res.found = true;
        res.index = i;
        res.t = s.t;
        res.ballPos = s.pos;
        res.ballVel = s.vel;
        res.ballSpeed2D = length2D(s.vel);
        res.carSpeed = arrival;
        res.distance = need;
        res.turnPenalty = penalty;
        res.jump = kind;
        res.jumpTime = jt;
        return res;
    }
    return res;
}

} // namespace rlt::pr
