#include "core/modules/prediction/BallPhysics.h"

#include <algorithm>
#include <cmath>

namespace rlt::pr {

namespace {

constexpr float kInvSqrt2 = 0.70710678f;

uint8_t kindFromNormal(const Vec3& n) {
    if (n.z > kInvSqrt2) return kContactFloor;
    if (n.z < -kInvSqrt2) return kContactCeiling;
    return kContactWall;
}

// Concave quarter-cylinder fillet. h = unit horizontal outward direction, u = dot(p, h), axis at (u0, z0),
// radius rf. floorSide: region z < z0, else z > z0. The region also needs u > u0.
void fillet(ContactList& out, const Vec3& p, const Vec3& h, float u, float u0, float z0, float rf, bool floorSide,
            float maxDist) {
    if (u <= u0) return;
    if (floorSide ? (p.z >= z0) : (p.z <= z0)) return;
    const float du = u - u0, dz = p.z - z0;
    const float l = std::sqrt(du * du + dz * dz);
    if (l < 1e-4f) return;
    const float d = rf - l;
    if (d >= maxDist) return;
    const Vec3 n = (h * du + Vec3{0.f, 0.f, dz}) * (-1.f / l);
    out.add(n, d, kindFromNormal(n));
}

// Rounded edge (goal post / crossbar) as a segment: contact normal from the closest point to the ball centre.
void segment(ContactList& out, const Vec3& p, const Vec3& a, const Vec3& b, float maxDist) {
    const Vec3 ab = b - a;
    const float t = std::clamp(dot(p - a, ab) / dot(ab, ab), 0.f, 1.f);
    const Vec3 q = a + ab * t;
    const Vec3 d = p - q;
    const float l = length(d);
    if (l >= maxDist || l < 1e-4f) return;
    out.add(d / l, l, static_cast<uint8_t>(kContactWall | kContactGoal));
}

} // namespace

SimParams SimParams::forRadius(float ballRadius) {
    SimParams p;
    if (std::isfinite(ballRadius) && ballRadius > 10.f && ballRadius < 400.f) {
        p.radius = ballRadius;
        p.arenaRadius = ballRadius + phys::kArenaMargin;
    }
    return p;
}

ContactList arenaContacts(const Vec3& p, float maxDist, ArenaMode mode) {
    using namespace phys;
    ContactList out;
    if (mode == ArenaMode::None) return out;
    auto plane = [&](const Vec3& n, float d, uint8_t k) {
        if (d < maxDist) out.add(n, d, k);
    };
    plane({0.f, 0.f, 1.f}, p.z, kContactFloor);
    if (mode == ArenaMode::FloorOnly) return out;

    const float ax = std::fabs(p.x), ay = std::fabs(p.y);
    const float sx = p.x < 0.f ? -1.f : 1.f, sy = p.y < 0.f ? -1.f : 1.f;
    const bool inGoal = ay > kArenaY;
    const bool inMouth = ax < kGoalHalfWidth && p.z < kGoalHeight;

    plane({0.f, 0.f, -1.f}, kArenaZ - p.z, kContactCeiling);
    plane({-sx, 0.f, 0.f}, kArenaX - ax, kContactWall);
    if (!inGoal && !inMouth) plane({0.f, -sy, 0.f}, kArenaY - ay, kContactWall);
    plane({-sx * kInvSqrt2, -sy * kInvSqrt2, 0.f}, (kCornerSum - ax - ay) * kInvSqrt2, kContactWall);

    // Ramps: floor -> side wall, side wall -> ceiling, floor -> back wall, back wall -> ceiling, corners.
    fillet(out, p, {sx, 0.f, 0.f}, ax, kArenaX - kFilletFloorSide, kFilletFloorSide, kFilletFloorSide, true, maxDist);
    fillet(out, p, {sx, 0.f, 0.f}, ax, kArenaX - kFilletCeiling, kArenaZ - kFilletCeiling, kFilletCeiling, false, maxDist);
    if (!inGoal && ax > kGoalHalfWidth)
        fillet(out, p, {0.f, sy, 0.f}, ay, kArenaY - kFilletFloorBack, kFilletFloorBack, kFilletFloorBack, true, maxDist);
    if (!inGoal)
        fillet(out, p, {0.f, sy, 0.f}, ay, kArenaY - kFilletCeiling, kArenaZ - kFilletCeiling, kFilletCeiling, false,
               maxDist);
    {
        const Vec3 h{sx * kInvSqrt2, sy * kInvSqrt2, 0.f};
        const float u = (ax + ay) * kInvSqrt2, uc = kCornerSum * kInvSqrt2;
        fillet(out, p, h, u, uc - kFilletFloorSide, kFilletFloorSide, kFilletFloorSide, true, maxDist);
        fillet(out, p, h, u, uc - kFilletCeiling, kArenaZ - kFilletCeiling, kFilletCeiling, false, maxDist);
    }

    // Goal (simplified box) and goal-frame edges.
    if (ay > kArenaY - maxDist - 1.f && ax < kGoalHalfWidth + maxDist + 1.f && p.z < kGoalHeight + maxDist + 1.f) {
        if (inGoal) {
            plane({-sx, 0.f, 0.f}, kGoalHalfWidth - ax, static_cast<uint8_t>(kContactWall | kContactGoal));
            plane({0.f, 0.f, -1.f}, kGoalHeight - p.z, static_cast<uint8_t>(kContactCeiling | kContactGoal));
            plane({0.f, -sy, 0.f}, kGoalBackY - ay, static_cast<uint8_t>(kContactWall | kContactGoal));
        }
        const float gy = sy * kArenaY;
        segment(out, p, {kGoalHalfWidth, gy, 0.f}, {kGoalHalfWidth, gy, kGoalHeight}, maxDist);
        segment(out, p, {-kGoalHalfWidth, gy, 0.f}, {-kGoalHalfWidth, gy, kGoalHeight}, maxDist);
        segment(out, p, {-kGoalHalfWidth, gy, kGoalHeight}, {kGoalHalfWidth, gy, kGoalHeight}, maxDist);
    }
    return out;
}

bool resolveContact(Vec3& vel, Vec3& angVel, const Vec3& n, float vnPre, const SimParams& prm) {
    const float vn = dot(vel, n);
    if (vn >= 0.f) return false;
    const bool bounce = -vnPre > prm.restitutionThreshold;
    const float target = bounce ? -prm.restitution * vnPre : 0.f;  // outgoing normal speed
    const float jn = target - vn;                                    // normal delta-v (> 0)
    const Vec3 vt = vel - n * vn;
    const Vec3 s = vt + cross(angVel, n * -prm.radius);              // contact-point slip
    const float sl = length(s);
    Vec3 dv = n * jn;
    if (sl > 1e-4f) {
        const float stopSlip = (2.f / 7.f) * sl;  // removes all slip of a solid sphere
        const float coulomb = prm.friction * jn;  // |J_t| <= mu |J_n|
        const Vec3 dvt = s * (-std::min(stopSlip, coulomb) / sl);
        dv += dvt;
        angVel += cross(dvt, n) * (1.f / (0.4f * prm.radius));
    }
    vel += dv;
    return bounce;
}

float dragFactor(const SimParams& prm, float dt) { return std::pow(1.f - prm.drag, dt); }

StepResult stepBall(SimBall& b, const SimParams& prm, ArenaMode mode, float dt, float dragMul) {
    StepResult res;
    const float vzBefore = b.vel.z;
    const Vec3 vPre = b.vel * dragMul;  // Bullet damps before the solver
    b.vel = vPre;
    b.vel.z += prm.gravityZ * dt;

    // Contacts at the look-ahead position.
    const Vec3 ahead = b.pos + b.vel * dt;
    const ContactList cl = arenaContacts(ahead, prm.arenaRadius, mode);
    const Vec3 vIn = b.vel;
    float toi = 1.f;  // fraction of the tick before the first contact
    for (int i = 0; i < cl.count; ++i) {
        const float approach = -dot(vIn, cl.c[static_cast<size_t>(i)].n) * dt;
        if (approach > 1e-6f)
            toi = std::min(toi, std::clamp(1.f - (prm.arenaRadius - cl.c[static_cast<size_t>(i)].dist) / approach, 0.f, 1.f));
    }
    const Vec3 posAtImpact = b.pos + vIn * (dt * toi);
    float strongest = -1.f;
    for (int i = 0; i < cl.count; ++i) {
        const Contact& c = cl.c[static_cast<size_t>(i)];
        if (dot(b.vel, c.n) >= 0.f) continue;
        res.flags |= kStepContact;
        if (c.kind & kContactFloor) res.flags |= kStepFloor;
        if (c.kind & kContactCeiling) res.flags |= kStepCeiling;
        if (c.kind & kContactWall) res.flags |= kStepWall;
        // Restitution uses the normal speed at the moment of impact (pre-gravity when toi = 0), so a resting
        // ball never re-bounces from the gravity of the current tick.
        const Vec3 vImpact = vPre + Vec3{0.f, 0.f, prm.gravityZ * dt * toi};
        const float vnPre = dot(vImpact, c.n);
        if (-vnPre > strongest) {
            strongest = -vnPre;
            res.normal = c.n;
            res.point = posAtImpact - c.n * prm.arenaRadius;
            res.impactSpeed = std::max(0.f, -vnPre);
        }
        if (resolveContact(b.vel, b.angVel, c.n, vnPre, prm)) res.flags |= kStepBounce;
    }

    b.pos += vIn * (dt * toi) + b.vel * (dt * (1.f - toi));  // move to the contact, then rebound
    for (int it = 0; it < 3; ++it) {                         // depenetrate, deepest first
        const ContactList pc = arenaContacts(b.pos, prm.arenaRadius, mode);
        int deepest = -1;
        float best = 0.f;
        for (int i = 0; i < pc.count; ++i) {
            const float pen = prm.arenaRadius - pc.c[static_cast<size_t>(i)].dist;
            if (pen > best) {
                best = pen;
                deepest = i;
            }
        }
        if (deepest < 0) break;
        const Vec3& n = pc.c[static_cast<size_t>(deepest)].n;
        b.pos += n * best;
        const float vn = dot(b.vel, n);
        if (vn < 0.f) b.vel -= n * vn;  // no further approach
    }

    const float sp = length(b.vel);
    if (sp > prm.maxSpeed) b.vel *= prm.maxSpeed / sp;
    const float w = length(b.angVel);
    if (w > prm.maxAngSpeed) b.angVel *= prm.maxAngSpeed / w;

    if (vzBefore > 0.f && b.vel.z <= 0.f && !(res.flags & kStepContact)) res.flags |= kStepApex;
    if (mode == ArenaMode::Soccar && isGoal(b.pos, prm.radius)) res.flags |= kStepGoal;
    return res;
}

} // namespace rlt::pr
