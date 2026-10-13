#include "core/modules/prediction/BallPath.h"

#include <algorithm>
#include <cmath>

namespace rlt::pr {

namespace {
bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

void PredictedPath::reset() {
    valid = false;
    frame = 0;
    samples.clear();
    startAirborne = false;
    apexIndex = -1;
    landingIndex = -1;
    zoneStart = zoneEnd = -1;
    goalIndex = -1;
    goalSide = 0;
    bounceCount = 0;
}

Vec3 PredictedPath::posAt(int ticks) const {
    if (ticks <= 0 || samples.empty()) return start.pos;
    return samples[static_cast<size_t>(std::min(ticks, size()) - 1)].pos;
}

Vec3 PredictedPath::velAt(int ticks) const {
    if (ticks <= 0 || samples.empty()) return start.vel;
    return samples[static_cast<size_t>(std::min(ticks, size()) - 1)].vel;
}

void simulatePath(const SimBall& start, float horizon, const SimParams& prm, ArenaMode mode, PredictedPath& out) {
    out.reset();
    if (out.samples.capacity() < static_cast<size_t>(kMaxSamples)) out.samples.reserve(static_cast<size_t>(kMaxSamples));
    if (!finite(start.pos) || !finite(start.vel) || !finite(start.angVel) || !std::isfinite(horizon)) return;

    out.params = prm;
    out.arena = mode;
    out.horizon = std::clamp(horizon, kMinHorizon, kMaxHorizon);
    out.start = start;
    const float w = length(out.start.angVel);
    if (w > prm.maxAngSpeed) out.start.angVel *= prm.maxAngSpeed / w;

    const int ticks = std::min(kMaxSamples, static_cast<int>(std::lround(out.horizon * kTickRate)));
    const float dragMul = dragFactor(prm, kTickDt);
    SimBall b = out.start;
    for (int i = 1; i <= ticks; ++i) {
        const StepResult r = stepBall(b, prm, mode, kTickDt, dragMul);
        PathSample s;
        s.t = static_cast<float>(i) * kTickDt;
        s.pos = b.pos;
        s.vel = b.vel;
        s.normal = r.normal;
        s.point = r.point;
        s.impactSpeed = r.impactSpeed;
        s.flags = r.flags;
        out.samples.push_back(s);
        if (r.flags & kStepGoal) break;
    }
    out.valid = true;
}

void analyzePath(PredictedPath& path, float zoneMin, float zoneMax) {
    path.startAirborne = false;
    path.apexIndex = -1;
    path.landingIndex = -1;
    path.zoneStart = path.zoneEnd = -1;
    path.goalIndex = -1;
    path.goalSide = 0;
    path.bounceCount = 0;
    if (!path.valid || path.samples.empty()) return;
    if (zoneMin > zoneMax) std::swap(zoneMin, zoneMax);

    const int n = path.size();
    const float r = path.params.arenaRadius;
    const auto& s = path.samples;

    // Free flight at the start: not touching anything during the first tick and above the floor (or rising).
    const bool firstContact = (s[0].flags & kStepContact) != 0;
    path.startAirborne = !firstContact && ballAirborne(path.start.pos, path.start.vel, r);

    // Apex: first sample where vz <= 0 when the ball is rising, otherwise the start itself.
    if (path.start.vel.z > 0.f) {
        path.apexIndex = -2;
        for (int i = 0; i < n; ++i)
            if (s[static_cast<size_t>(i)].vel.z <= 0.f) {
                path.apexIndex = i;
                break;
            }
    }

    for (int i = 0; i < n; ++i) {
        const PathSample& p = s[static_cast<size_t>(i)];
        if (path.goalIndex < 0 && (p.flags & kStepGoal)) {
            path.goalIndex = i;
            path.goalSide = p.pos.y > 0.f ? 1 : -1;
        }
        if (path.landingIndex < 0 && path.startAirborne && (p.flags & kStepFloor)) path.landingIndex = i;
        if ((p.flags & kStepBounce) && p.impactSpeed >= kMinBounceMarkerSpeed && path.bounceCount < kMaxBounces) {
            PathBounce& b = path.bounces[static_cast<size_t>(path.bounceCount++)];
            b.index = i;
            b.normal = p.normal;
            b.point = p.point;
            b.impactSpeed = p.impactSpeed;
            b.flags = p.flags;
        }
    }

    // First-touch zone: after the apex, the first airborne sample descending inside the height band.
    if (path.apexIndex != -2) {
        const int from = std::max(0, path.apexIndex);
        auto inZone = [&](const PathSample& p) {
            return p.pos.z >= zoneMin && p.pos.z <= zoneMax && p.vel.z <= 0.f && !(p.flags & kStepContact);
        };
        for (int i = from; i < n; ++i) {
            if (!inZone(s[static_cast<size_t>(i)])) continue;
            path.zoneStart = i;
            int j = i;
            while (j + 1 < n && inZone(s[static_cast<size_t>(j + 1)])) ++j;
            path.zoneEnd = j;
            break;
        }
    }
}

bool ballAirborne(const Vec3& pos, const Vec3& vel, float arenaRadius) {
    return pos.z > arenaRadius + 20.f || vel.z > 100.f;
}

int closestSample(const PredictedPath& path, const Vec3& p, int maxSearch) {
    int best = -1;
    float bestD = lengthSq(path.start.pos - p);
    const int n = std::min(path.size(), std::max(0, maxSearch));
    for (int i = 0; i < n; ++i) {
        const float d = lengthSq(path.samples[static_cast<size_t>(i)].pos - p);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

} // namespace rlt::pr
