#pragma once
// Physics helpers of the drill playability tests (cross-cutting fix 10): the core ball physics (pr::stepBall of the
// prediction module), the car hitbox as an oriented box, the car models the auditors used (fall from the ceiling,
// climb of the floor / wall / ceiling curve, ground and air reach) and a scripted reaction time.
//
// The ballmech test binary does not link the prediction module: test_drill_playability.cpp compiles
// core/modules/prediction/BallPhysics.cpp itself (namespace rlt::pr, no clash).

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"
#include "core/modules/ballmech/Primitives.h"
#include "core/modules/prediction/BallPhysics.h"

namespace pb {

using namespace rlt;

constexpr float kDt = kTickDt;
constexpr float kReact = 0.4f;        // scripted player reaction (0.3 to 0.5 s, fix 10)
constexpr float kBallR = 91.25f;
constexpr float kBoostAccel = 991.667f;   // ground boost
constexpr float kAirBoost = 1058.33f;     // air boost
const Vec3 kOctaneOffset{13.88f, 0.f, 20.75f};
const Vec3 kOctaneExtent{118.01f, 84.20f, 36.16f};

struct BallSample {
    Vec3 p, v;
    uint8_t flags = 0;
};

// Ball path with the core physics (arena, drag, bounces, spin), one sample per tick, sample 0 = start.
inline std::vector<BallSample> simBall(const Vec3& p, const Vec3& v, float seconds, const Vec3& w = {}) {
    pr::SimBall b;
    b.pos = p;
    b.vel = v;
    b.angVel = w;
    pr::SimParams prm;
    std::vector<BallSample> out;
    const int n = static_cast<int>(seconds * kTickRate);
    out.push_back({b.pos, b.vel, 0});
    for (int i = 0; i < n; ++i) {
        const pr::StepResult r = pr::stepBall(b, prm);
        out.push_back({b.pos, b.vel, r.flags});
    }
    return out;
}

// Distance from point q to the car hitbox (oriented box), 0 inside. Octane hitbox by default.
inline float obbDist(const Vec3& pos, const Basis& B, const Vec3& q, const Vec3& off = kOctaneOffset,
                     const Vec3& ext = kOctaneExtent) {
    const Vec3 c = pos + B.fwd * off.x + B.right * off.y + B.up * off.z;
    const Vec3 d = q - c;
    const float lx = dot(d, B.fwd), ly = dot(d, B.right), lz = dot(d, B.up);
    const float ex = std::max(0.f, std::fabs(lx) - ext.x * 0.5f);
    const float ey = std::max(0.f, std::fabs(ly) - ext.y * 0.5f);
    const float ez = std::max(0.f, std::fabs(lz) - ext.z * 0.5f);
    return std::sqrt(ex * ex + ey * ey + ez * ez);
}
inline bool touching(const Vec3& carPos, const Basis& B, const Vec3& ball) { return obbDist(carPos, B, ball) <= kBallR; }

// Ground acceleration of the throttle (RocketSim curve).
inline float throttleAccel(float v) {
    if (v < 1400.f) return 1600.f - 1440.f * (v / 1400.f);
    if (v < 1410.f) return 160.f * (1410.f - v) / 10.f;
    return 0.f;
}

// ---- Ceiling: car leaving the ceiling, optional flip --------------------------------------------------------------
// The car leaves after `leave` seconds at half gravity (wheels letting go), then falls. A flip at `tf` (< 0 = none)
// damps the fall for 0.21 s and adds 500 uu/s along the flat nose (auditors' flip model).
inline std::vector<Vec3> ceilingPath(const Vec3& pos, const Vec3& vel, const Basis& B, float leave, float tf, float seconds) {
    std::vector<Vec3> path;
    Vec3 p = pos, v = vel;
    const Vec3 f = normalize(flat(B.fwd));
    path.push_back(p);
    const int n = static_cast<int>(seconds * kTickRate);
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) * kDt;
        float az = t <= leave ? -325.f : -650.f;
        if (tf >= 0.f && t >= tf && t - tf < 0.21f) {
            v.z *= 0.65f;
            az = (t - tf < 0.15f || v.z < 0.f) ? 0.f : az;
        }
        if (tf >= 0.f && std::fabs(t - tf) < kDt * 0.5f) v = v + f * 500.f;
        v.z += az * kDt;
        p = p + v * kDt;
        if (p.z > 2027.f) {
            p.z = 2027.f;
            v.z = std::min(0.f, v.z);
        }
        path.push_back(p);
    }
    return path;
}

struct FlipWindow {
    float first = -1.f, last = -1.f, len = 0.f;
    float passive = -1.f;  // first contact with no input at all (-1 = never)
};

// Flip times (from `react`) that hit the ball within 0.35 s with no contact before the flip.
inline FlipWindow ceilingFlipWindow(const Vec3& carPos, const Vec3& carVel, const Rot& rot, const Vec3& ballPos,
                                    const Vec3& ballVel, float leave, float react, float horizon = 2.5f) {
    FlipWindow w;
    const Basis B = basisFromRot(rot);
    const auto ball = simBall(ballPos, ballVel, horizon + 0.5f);
    const auto still = ceilingPath(carPos, carVel, B, leave, -1.f, horizon + 0.5f);
    for (size_t i = 1; i < still.size() && i < ball.size() && static_cast<float>(i) * kDt <= horizon; ++i)
        if (touching(still[i], B, ball[i].p)) {
            w.passive = static_cast<float>(i) * kDt;
            break;
        }
    for (float tf = react; tf < horizon; tf += kDt) {
        const auto path = ceilingPath(carPos, carVel, B, leave, tf, horizon + 0.5f);
        const int i0 = static_cast<int>(tf * kTickRate), i1 = static_cast<int>((tf + 0.35f) * kTickRate);
        bool early = false, hit = false;
        for (int i = 1; i < i0 && i < static_cast<int>(ball.size()); ++i)
            if (touching(path[static_cast<size_t>(i)], B, ball[static_cast<size_t>(i)].p)) {
                early = true;
                break;
            }
        if (!early)
            for (int i = i0; i <= i1 && i < static_cast<int>(ball.size()); ++i)
                if (touching(path[static_cast<size_t>(i)], B, ball[static_cast<size_t>(i)].p)) {
                    hit = true;
                    break;
                }
        if (hit) {
            if (w.first < 0.f) w.first = tf;
            w.last = tf;
            w.len += kDt;
        }
    }
    return w;
}

// ---- Floor -> side wall -> ceiling climb (cross-section model of the auditors) ---------------------------------------
struct Climb {
    float tWall = -1.f;     // mid-way up the floor-wall curve (the "Va au mur" step: wheels tilted past 45 deg)
    float tCeil = -1.f;     // on the ceiling
    float tDrop = -1.f;     // left the ceiling
    Vec3 dropPos, dropVel;  // side +1 wall, y from the start line
    bool fellOff = false;
};
// d0: floor distance to the curve, phi: heading off the wall normal (deg), v0: start speed, boost held until the car
// reaches `boostUntilZ`, `react` seconds of coasting first, `ceilHold` seconds on the ceiling before letting go.
inline Climb climb(float d0, float phiDeg, float v0, float react, float boostUntilZ, float ceilHold = 0.12f) {
    Climb o;
    const float phi = phiDeg * kDegToRad;
    const float L0 = d0, L1 = L0 + 0.5f * kPi * 256.f, L2 = L1 + (1536.f - 256.f), L3 = L2 + 0.5f * kPi * 512.f;
    const float Lmid = L0 + 0.25f * kPi * 256.f;
    float s = 0.f, v = v0, t = 0.f, y = 0.f, boostLeft = 100.f, ceilT = -1.f;
    while (t < 8.f) {
        float dz = 0.f;
        if (s >= L0 && s < L1) dz = std::sin((s - L0) / 256.f);
        else if (s >= L1 && s < L2) dz = 1.f;
        else if (s >= L2 && s < L3) dz = std::cos((s - L2) / 512.f);
        const float z = s < L0 ? 0.f
                        : (s < L1 ? 256.f - 256.f * std::cos((s - L0) / 256.f)
                                  : (s < L2 ? 256.f + (s - L1) : 1536.f + 512.f * std::sin((s - L2) / 512.f)));
        const bool playing = t >= react;
        float a = playing ? throttleAccel(v) : (v > 0.f ? -525.f : 0.f);
        if (playing && boostLeft > 0.f && z < boostUntilZ) {
            a += kBoostAccel;
            boostLeft -= 33.333f * kDt;
        }
        a += -650.f * std::cos(phi) * dz;
        v = clampf(v + a * kDt, 0.f, 2300.f);
        const float vxz = v * std::cos(phi);
        if (s >= L2 && s < L3) {
            const float al = (s - L2) / 512.f;
            if (vxz * vxz < 512.f * (650.f * std::sin(al) - 325.f)) {
                o.fellOff = true;
                break;
            }
        }
        if (s >= L1 && s < L2 && vxz < 50.f) {
            o.fellOff = true;
            break;
        }
        s += vxz * kDt;
        y += v * std::sin(phi) * kDt;
        t += kDt;
        if (o.tWall < 0.f && s >= Lmid) o.tWall = t;
        if (o.tCeil < 0.f && s >= L3) {
            o.tCeil = t;
            ceilT = t;
        }
        if (ceilT >= 0.f && t - ceilT >= ceilHold) {
            o.tDrop = t;
            o.dropPos = {4096.f - 512.f - (s - L3), y, 2027.f};
            o.dropVel = {-vxz, v * std::sin(phi), 0.f};
            break;
        }
    }
    return o;
}

// ---- Reach models -----------------------------------------------------------------------------------------------
// Air: car flying at p0 / v0, constant boost direction after `react`: can it be within `tol` of q after dt more?
inline bool airReach(const Vec3& p0, const Vec3& v0, const Vec3& q, float dt, float tol, float accel = kAirBoost) {
    if (dt <= 0.05f) return false;
    const Vec3 w = q - p0 - v0 * dt - Vec3{0.f, 0.f, -325.f * dt * dt};
    return length(w) <= 0.5f * accel * dt * dt + tol;
}

struct Window {
    float first = -1.f, last = -1.f, len = 0.f, longest = 0.f;
    Vec3 ballAtFirst;
    float speedAtFirst = 0.f;
};
inline void addToWindow(Window& w, float t, float step, bool ok, float& runStart, float& runLen) {
    if (ok) {
        if (w.first < 0.f) w.first = t;
        w.last = t;
        w.len += step;
        if (runStart < 0.f) runStart = t;
        runLen += step;
        w.longest = std::max(w.longest, runLen);
    } else {
        runStart = -1.f;
        runLen = 0.f;
    }
}

// Air start (car already flying): coasts during `react`, then constant boost in any direction.
inline Window airWindow(const Vec3& p0, const Vec3& v0, const std::vector<BallSample>& ball, float react, float tol,
                        float minBallZ, float maxT = 6.f) {
    Window w;
    const Vec3 p1 = p0 + v0 * react + Vec3{0.f, 0.f, -325.f * react * react};
    const Vec3 v1 = v0 + Vec3{0.f, 0.f, -650.f * react};
    float rs = -1.f, rl = 0.f;
    for (size_t i = static_cast<size_t>(react * kTickRate) + 6; i < ball.size() && static_cast<float>(i) * kDt <= maxT; ++i) {
        const float T = static_cast<float>(i) * kDt;
        const bool ok = ball[i].p.z >= minBallZ && airReach(p1, v1, ball[i].p, T - react, tol);
        if (ok && w.first < 0.f) w.ballAtFirst = ball[i].p;
        addToWindow(w, T, kDt, ok, rs, rl);
    }
    return w;
}

// Ground start: drive straight toward the ball's ground spot (coast during `react`, then throttle + boost), jump
// (0.2 s held: +75 uu, 450 up), then constant air boost. Contact tolerance `tol`. With `allowGround`, a low ball can
// also be met on the ground.
inline bool groundAerial(const Vec3& p0, const Vec3& v0, const Vec3& q, float T, float react, float tol,
                         bool allowGround, float* carSpeed = nullptr) {
    Vec3 dirXY = flat(q - p0);
    const float dist = length(dirXY);
    dirXY = dist < 1.f ? Vec3{0.f, 1.f, 0.f} : dirXY * (1.f / dist);
    const float vAlong = dot(v0, dirXY);
    for (float tj = react; tj <= T - 0.05f; tj += 1.f / 30.f) {
        float x = 0.f, v = vAlong;
        for (float t = 0.f; t < tj; t += kDt) {
            const float a = t < react ? -525.f : throttleAccel(v) + kBoostAccel;
            v = clampf(v + a * kDt, 0.f, 2300.f);
            x += v * kDt;
        }
        if (allowGround && q.z < 200.f) {
            float xx = x, vv = v;
            for (float t = tj; t < T; t += kDt) {
                vv = std::min(2300.f, vv + (throttleAccel(vv) + kBoostAccel) * kDt);
                xx += vv * kDt;
            }
            const Vec3 pT = p0 + dirXY * xx;
            if (distance(flat(pT), flat(q)) <= tol + 40.f && xx >= dist - tol - 60.f) {
                if (carSpeed) *carSpeed = vv;
                return true;
            }
        }
        const float ta = tj + 0.2f;
        if (ta >= T - 0.05f) continue;
        Vec3 pj = p0 + dirXY * x;
        pj.z = 17.f;
        const Vec3 pa = pj + dirXY * (v * 0.2f) + Vec3{0.f, 0.f, 75.f};
        const Vec3 va = dirXY * v + Vec3{0.f, 0.f, 450.f};
        const float dtA = T - ta;
        const Vec3 w = q - pa - va * dtA - Vec3{0.f, 0.f, -325.f * dtA * dtA};
        if (length(w) <= 0.5f * 1058.f * dtA * dtA + tol) {
            if (carSpeed) *carSpeed = length(va);
            return true;
        }
    }
    return false;
}

inline Window groundWindow(const Vec3& p0, const Vec3& v0, const std::vector<BallSample>& ball, float react, float tol,
                           float minBallZ, bool allowGround, float maxT = 6.f) {
    Window w;
    float rs = -1.f, rl = 0.f;
    const float step = 2.f * kDt;
    for (size_t i = static_cast<size_t>(react * kTickRate) + 12; i < ball.size() && static_cast<float>(i) * kDt <= maxT; i += 2) {
        const float T = static_cast<float>(i) * kDt;
        float sp = 0.f;
        const bool ok = ball[i].p.z >= minBallZ && groundAerial(p0, v0, ball[i].p, T, react, tol, allowGround, &sp);
        if (ok && w.first < 0.f) {
            w.ballAtFirst = ball[i].p;
            w.speedAtFirst = sp;
        }
        addToWindow(w, T, step, ok, rs, rl);
    }
    return w;
}

} // namespace pb
