#pragma once
// MiniSim: a small 120 Hz car simulator for synthetic mechanic executions. Jump, double jump, dodge, flip cancel,
// flip z-damping, air control and boost follow RocketSim src/Sim/Car/Car.cpp; ground contact, driving and
// powerslide are simplified (no suspension, 3 uu wheel tolerance). Internally right-handed (fwd, left, up);
// snapshots are mirrored (y -> -y) into the game's left-handed axes so that fwd x right == up as in rlt::Basis.

#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Snapshot.h"
#include "core/modules/mechanics/MechCommon.h"
#include "core/modules/mechanics/MovementCoach.h"

namespace rlt_test_mech {

using namespace rlt;
using namespace rlt::mech;

inline Vec3 rodrigues(const Vec3& v, const Vec3& a, float ang) {
    const float c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(a, v) * s + a * (dot(a, v) * (1.f - c));
}
inline Vec3 mirrorY(const Vec3& v) { return {v.x, -v.y, v.z}; }

struct MiniSim {
    Vec3 pos{0.f, 0.f, k::kCarRestZ}, vel, w;
    Vec3 c0{1.f, 0.f, 0.f}, c1{0.f, 1.f, 0.f}, c2{0.f, 0.f, 1.f};  // fwd, left, up
    bool hasJumped = false, isJumping = false, hasDoubleJumped = false, hasFlipped = false, isFlipping = false;
    float jumpTime = 0.f, flipTime = 0.f, airTimeSinceJump = 0.f;
    Dir2 dodgeDir;
    float boost = 100.f;
    bool isBoosting = false;
    float boostingTime = 0.f;
    bool lastJump = false;
    float deadzone = 0.5f;
    int team = 0;

    Vec3 fwd() const { return c0; }
    Vec3 up() const { return c2; }
    Vec3 right() const { return -c1; }

    void orient(const Vec3& f, const Vec3& u) {
        c0 = normalize(f);
        c2 = normalize(u);
        c1 = cross(c2, c0);
    }
    std::array<Vec3, 4> wheels() const {
        const float xs[4] = {51.25f, 51.25f, -33.75f, -33.75f};
        const float ys[4] = {25.9f, -25.9f, 29.5f, -29.5f};
        std::array<Vec3, 4> p;
        for (int i = 0; i < 4; ++i) p[static_cast<size_t>(i)] = pos + fwd() * xs[i] + right() * ys[i] - up() * 17.f;
        return p;
    }
    float rearWheelZ() const {
        auto p = wheels();
        return std::min(p[2].z, p[3].z);
    }
    float minWheelZ() const {
        float m = 1e9f;
        for (auto& p : wheels()) m = std::min(m, p.z);
        return m;
    }
    int wheelCount() const {
        if (up().z <= 0.3f) return 0;
        int n = 0;
        for (auto& p : wheels()) n += p.z <= 3.f ? 1 : 0;
        return n;
    }
    float minBodyZ() const {
        float m = 1e9f;
        for (float x : {-47.f, 73.f})
            for (float y : {-42.f, 42.f})
                for (float z : {-11.f, 28.f}) m = std::min(m, (pos + fwd() * x + right() * y + up() * z).z);
        return m;
    }
    static float throttleAccel(float v) {
        if (v >= 1410.f) return 0.f;
        if (v >= 1400.f) return 160.f * (1410.f - v) / 10.f;
        return 1600.f + (160.f - 1600.f) * (v / 1400.f);
    }
    static float curvature(float v) {
        const float xs[] = {0.f, 500.f, 1000.f, 1500.f, 1750.f, 2300.f};
        const float ys[] = {0.0069f, 0.00398f, 0.00235f, 0.001375f, 0.0011f, 0.00088f};
        if (v <= 0.f) return ys[0];
        for (int i = 1; i < 6; ++i)
            if (v <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (v - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return ys[5];
    }

    void tick(const Inputs& in) {
        const float dt = k::kDt;
        const int nW = wheelCount();
        const bool onGround = nW >= 3;
        const bool jumpPressed = in.jump && !lastJump;
        // ---- air torque (RocketSim _UpdateAirTorque)
        if (nW < 3) {
            bool doAir = false;
            float pUp = 0.f, yR = 0.f, rR = 0.f;
            if (isFlipping) isFlipping = hasFlipped && flipTime < k::kFlipTorqueTime;
            if (isFlipping) {
                if (dodgeDir.x != 0.f || dodgeDir.y != 0.f) {
                    float pitchScale = 1.f;
                    if (dodgeDir.x != 0.f && in.pitch != 0.f && sgn(dodgeDir.x) == sgn(in.pitch)) {
                        pitchScale = 1.f - std::min(std::fabs(in.pitch), 1.f);
                        doAir = true;
                    }
                    pUp += -dodgeDir.x * k::kFlipTorqueY * pitchScale;
                    rR += dodgeDir.y * k::kFlipTorqueX;
                } else {
                    doAir = true;  // stall
                }
            } else {
                doAir = true;
            }
            doAir = doAir && nW == 0;
            if (doAir) {
                float pts = 1.f;
                if (isFlipping) pts = 0.f;
                else if (hasFlipped && flipTime < k::kFlipTorqueTime + k::kFlipPitchlockExtraTime) pts = 0.f;
                const float wp = dot(w, right()), wy = -dot(w, up()), wr = dot(w, fwd());
                pUp += (in.pitch * k::kAirTorquePitch * pts - wp * k::kAirDampPitch * (1.f - std::fabs(in.pitch * pts))) *
                       k::kCarTorqueScale;
                yR += (in.yaw * k::kAirTorqueYaw - wy * k::kAirDampYaw * (1.f - std::fabs(in.yaw))) * k::kCarTorqueScale;
                rR += (in.roll * k::kAirTorqueRoll - wr * k::kAirDampRoll) * k::kCarTorqueScale;
            }
            w += (right() * pUp + (-up()) * yR + fwd() * rR) * dt;
            if (in.throttle != 0.f) vel += fwd() * (in.throttle * k::kThrottleAirAccel * dt);
        } else {
            isFlipping = false;
        }
        // ---- jump (RocketSim _UpdateJump)
        if (onGround && !isJumping) {
            if (!(hasJumped && jumpTime < k::kJumpMinTime + 1.f / 40.f)) {
                hasJumped = false;
                jumpTime = 0.f;
            }
        }
        if (isJumping) {
            isJumping = jumpTime < k::kJumpMinTime || (in.jump && jumpTime < k::kJumpMaxTime);
        } else if (onGround && jumpPressed) {
            isJumping = true;
            jumpTime = 0.f;
            vel += up() * k::kJumpImpulse;
        }
        if (isJumping) {
            hasJumped = true;
            vel += up() * (k::kJumpAccel * (jumpTime < k::kJumpMinTime ? k::kJumpPreMinAccelScale : 1.f) * dt);
        }
        if (isJumping || hasJumped) jumpTime += dt;
        // ---- double jump / flip (RocketSim _UpdateDoubleJumpOrFlip)
        if (onGround) {
            hasDoubleJumped = hasFlipped = false;
            airTimeSinceJump = 0.f;
            flipTime = 0.f;
        } else {
            if (hasJumped && !isJumping) airTimeSinceJump += dt;
            else airTimeSinceJump = 0.f;
            if (jumpPressed && airTimeSinceJump < k::kDoubleJumpMaxDelay && !hasDoubleJumped && !hasFlipped) {
                if (stickMag(in) >= deadzone) {
                    flipTime = 0.f;
                    hasFlipped = isFlipping = true;
                    dodgeDir = dodgeDirFromInputs(in);
                    vel += dodgeDeltaV(dodgeDir, vel, fwd(), right());
                } else {
                    vel += up() * k::kJumpImpulse;
                    hasDoubleJumped = true;
                }
            }
        }
        if (isFlipping) {
            flipTime += dt;
            if (flipTime <= k::kFlipTorqueTime && flipTime >= k::kFlipZDampStart && (vel.z < 0.f || flipTime < k::kFlipZDampEnd))
                vel.z *= 1.f - k::kFlipZDamp120;
        } else if (hasFlipped) {
            flipTime += dt;
        }
        // ---- ground driving (simplified)
        if (onGround) {
            const Vec3 n{0.f, 0.f, 1.f};
            const Vec3 f = normalize(tangent(fwd(), n)), rt = normalize(tangent(right(), n));
            float vf = dot(vel, f), vr = dot(vel, rt);
            const float vn = dot(vel, n);
            if (in.throttle != 0.f) {
                if (vf * in.throttle >= 0.f || std::fabs(vf) < 1.f) {
                    vf += in.throttle * throttleAccel(std::fabs(vf)) * dt;
                } else {
                    const float d = k::kBrakeDecel * dt;
                    vf = std::fabs(vf) <= d ? 0.f : vf - sgn(vf) * d;
                }
            } else if (!in.boost) {
                const float d = k::kCoastDecel * dt;
                vf = std::fabs(vf) <= d ? 0.f : vf - sgn(vf) * d;
            }
            const float yawRate = in.steer * curvature(std::fabs(vf)) * vf * (in.handbrake ? 1.5f : 1.f);
            vr *= in.handbrake ? 0.985f : 0.6f;
            vel = f * vf + rt * vr + n * vn;
            w = (-n) * yawRate;
        }
        // ---- boost (RocketSim _UpdateBoost)
        if (boost > 0.f) isBoosting = isBoosting ? (in.boost || boostingTime < k::kBoostMinTime) : in.boost;
        else isBoosting = false;
        if (isBoosting) {
            boostingTime += dt;
            boost = std::max(0.f, boost - k::kBoostUsedPerSecond * dt);
            vel += fwd() * ((onGround ? k::kBoostAccelGround : k::kBoostAccelAir) * dt);
        } else {
            boostingTime = 0.f;
        }
        vel.z += k::kGravityZ * dt;
        vel = clampLen(vel, k::kCarMaxSpeed);
        w = clampLen(w, k::kCarMaxAngSpeed);
        // ---- integrate
        pos += vel * dt;
        const float wl = length(w);
        if (wl > 1e-6f) {
            const Vec3 a = w * (1.f / wl);
            c0 = normalize(rodrigues(c0, a, wl * dt));
            c2 = rodrigues(c2, a, wl * dt);
            c2 = normalize(c2 - c0 * dot(c2, c0));
            c1 = cross(c2, c0);
        }
        // ---- contact (simplified)
        const Vec3 Z{0.f, 0.f, 1.f};
        float mw = minWheelZ();
        if (up().z > 0.3f && mw < 0.5f) {
            const Vec3 axis = cross(up(), Z);
            const float s = length(axis);
            if (s > 1e-4f) {
                const float xs[4] = {51.25f, 51.25f, -33.75f, -33.75f};
                const float ys[4] = {25.9f, -25.9f, 29.5f, -29.5f};
                auto wp = wheels();
                size_t lo = 0;
                for (size_t i = 1; i < 4; ++i)
                    if (wp[i].z < wp[lo].z) lo = i;
                const Vec3 P = wp[lo];
                const float ang = std::min(std::asin(std::min(s, 1.f)), 5.f * kDegToRad);
                const Vec3 a = axis * (1.f / s);
                c0 = rodrigues(c0, a, ang);
                c2 = rodrigues(c2, a, ang);
                c1 = cross(c2, c0);
                pos = P - (fwd() * xs[lo] + right() * ys[lo] - up() * 17.f);
            }
            w = Z * dot(w, Z);
            mw = minWheelZ();
            if (mw < 0.f) pos.z -= mw;
            if (vel.z < 0.f) vel.z = 0.f;
        } else if (up().z <= 0.3f) {
            const float mb = minBodyZ();
            if (mb < 0.f) {
                pos.z -= mb;
                if (vel.z < 0.f) vel.z = -vel.z * 0.2f;
                vel.x *= 0.97f;
                vel.y *= 0.97f;
                w *= 0.85f;
            }
        }
        if (wheelCount() >= 3 && up().z > std::cos(2.f * kDegToRad)) {
            orient(normalize(tangent(fwd(), Z)), Z);
            w = Z * dot(w, Z);
        }
        lastJump = in.jump;
    }

    // Snapshot in the game's axes (mirror y).
    Snapshot snap(int64_t frame, const Inputs& in) const {
        Snapshot s;
        s.frame = frame;
        s.time = static_cast<float>(frame) / 120.f;
        s.in = in;
        CarState& c = s.car;
        c.valid = true;
        c.pos = mirrorY(pos);
        c.vel = mirrorY(vel);
        c.angVel = Vec3{-w.x, w.y, -w.z};  // pseudovector under a reflection
        c.basis.fwd = mirrorY(fwd());
        c.basis.right = mirrorY(right());
        c.basis.up = mirrorY(up());
        c.wheelContacts = wheelCount();
        c.groundNormal = {0.f, 0.f, 1.f};
        c.onGround = c.wheelContacts >= 3;
        c.hasFlip = c.onGround || (!hasFlipped && !hasDoubleJumped && airTimeSinceJump < k::kDoubleJumpMaxDelay);
        c.jumped = hasJumped;
        c.doubleJumped = hasDoubleJumped;
        c.dodging = isFlipping;
        c.dodgeDir = isFlipping ? Vec3{dodgeDir.x, dodgeDir.y, 0.f} : Vec3{};
        c.boost = boost;
        c.supersonic = length(vel) >= k::kSupersonicStartSpeed;
        c.team = team;
        s.ball.valid = true;
        s.ball.pos = {0.f, 0.f, 92.75f};
        return s;
    }

    // Applies a drill placement given in game axes.
    void place(const CarPlacement& p) {
        pos = mirrorY(p.pos);
        vel = mirrorY(p.vel);
        const Basis b = basisFromRot(p.rot);
        c0 = mirrorY(b.fwd);
        c2 = mirrorY(b.up);
        c1 = cross(c2, c0);
        w = Vec3{-p.angVel.x, p.angVel.y, -p.angVel.z};
        if (p.boost) boost = *p.boost;
        hasJumped = isJumping = hasDoubleJumped = hasFlipped = isFlipping = false;
        jumpTime = flipTime = airTimeSinceJump = 0.f;
    }
};

// Small controllers used by scripts (MiniSim internal axes).
inline float noseDeg(const MiniSim& c) { return safeAsinDeg(c.fwd().z); }
inline float rollToLevel(const MiniSim& c) {
    const Vec3 Z{0.f, 0.f, 1.f};
    const float phi = std::atan2(dot(Z, c.right()), dot(Z, c.up()));
    return clampf(4.f * phi - 0.6f * dot(c.w, c.fwd()), -1.f, 1.f);
}
inline float pitchToNose(const MiniSim& c, float targetDeg) {
    const float err = (targetDeg - noseDeg(c)) * kDegToRad;
    const float rate = dot(c.w, c.right());
    return clampf(8.f * err - 1.6f * rate, -1.f, 1.f);
}
inline float rollToLeftDown(const MiniSim& c, float targetDeg) {
    const float leftDown = safeAsinDeg(dot(c.right(), Vec3{0.f, 0.f, 1.f}));
    const float err = (targetDeg - leftDown) * kDegToRad;
    const float rate = dot(c.w, c.fwd());
    return clampf(-(8.f * err + 1.6f * rate) * 0.3f, -1.f, 1.f);
}
inline void orientPD(const MiniSim& c, const Vec3& dir, Inputs& in) {
    const Vec3 Z{0.f, 0.f, 1.f};
    const Vec3 ft = normalize(Vec3{dir.x, dir.y, 0.f}, c.fwd());
    const Vec3 e = cross(c.fwd(), ft) + cross(c.up(), Z);
    const Vec3 acc = e * 14.f - c.w * 4.5f;
    in.pitch = clampf(dot(acc, c.right()) / 12.46f, -1.f, 1.f);
    in.yaw = clampf(dot(acc, -c.up()) / 9.11f, -1.f, 1.f);
    in.roll = clampf(dot(acc, c.fwd()) / 38.34f, -1.f, 1.f);
}

using Script = std::function<Inputs(int, MiniSim&)>;

// Runs a script through a MovementCoach and collects the grades.
struct Run {
    MiniSim sim;
    MovementCoach coach;
    std::vector<MechGrade> grades;
    int64_t tick = 1000;
    Vec3 ballPos{1e6f, 0.f, 0.f};  // sim axes; far away by default
    bool ballInSnapshot = false;   // put the ball at the centre spot in snapshots (kickoff tests)
    bool touched = false;
    float touchRadius = 92.75f + 70.f;

    explicit Run(DetectConfig cfg = {}) { coach.cfg = cfg; }

    void go(int ticks, const Script& script) {
        for (int t = 0; t < ticks; ++t) {
            const Inputs in = script(t, sim);
            sim.tick(in);
            Snapshot s = sim.snap(tick++, in);
            if (!ballInSnapshot) s.ball.pos = {1e5f, 1e5f, 92.75f};
            coach.step(s, grades);
            if (!touched && length(sim.pos - ballPos) < touchRadius) {
                touched = true;
                coach.onBallTouch(s.time);
            }
        }
    }
    void flush() {
        const Snapshot s = sim.snap(tick, {});
        coach.flushPending(s, grades, false);
    }
    const MechGrade* find(Mech m, int nth = 0) const {
        for (const auto& g : grades)
            if (g.mech == m && nth-- == 0) return &g;
        return nullptr;
    }
    int count(Mech m) const {
        int n = 0;
        for (const auto& g : grades) n += g.mech == m ? 1 : 0;
        return n;
    }
};

} // namespace rlt_test_mech
