#include "core/modules/courses/ControlDrills.h"

#include <algorithm>
#include <cmath>

#include "core/common/Rng.h"
#include "core/modules/courses/CourseGen.h"

namespace rlt::courses {

float roofHeight(const Vec3& hitboxOffset, const Vec3& hitboxExtent) { return hitboxOffset.z + hitboxExtent.z * 0.5f; }

Vec3 roofSpawn(const Vec3& carPos, const Basis& b, const Vec3& hitboxOffset, const Vec3& hitboxExtent, float ballRadius,
               float gap) {
    return carPos + b.fwd * hitboxOffset.x + b.up * (roofHeight(hitboxOffset, hitboxExtent) + ballRadius + gap);
}

bool ballOnRoof(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Vec3& hitboxOffset,
                const Vec3& hitboxExtent, float ballRadius, float tolUp, float margin) {
    const Vec3 l = b.toLocal(ballPos - carPos);
    const float zTop = roofHeight(hitboxOffset, hitboxExtent) + ballRadius;
    if (l.z < zTop - 15.f || l.z > zTop + tolUp) return false;
    return std::fabs(l.x - hitboxOffset.x) <= hitboxExtent.x * 0.5f + margin &&
           std::fabs(l.y - hitboxOffset.y) <= hitboxExtent.y * 0.5f + margin;
}

bool ballOnRoof(const BallState& ball, const CarState& car) {
    if (!ball.valid || !car.valid) return false;
    return ballOnRoof(ball.pos, car.pos, car.basis, car.hitboxOffset, car.hitboxExtent, ball.radius);
}

bool ballOnFloor(const BallState& ball) { return ball.valid && ball.pos.z < ball.radius + 10.f; }

FallSide classifyFall(const Vec3& relVelLocal, const Vec3& relPosLocal) {
    const float ax = std::fabs(relVelLocal.x), ay = std::fabs(relVelLocal.y), az = relVelLocal.z;
    if (az > 300.f && az > ax && az > ay) return FallSide::Up;
    if (std::max(ax, ay) >= 60.f) {
        if (ay > ax) return FallSide::Side;
        return relVelLocal.x > 0.f ? FallSide::Front : FallSide::Back;
    }
    // Slow drift: decide from where the ball sits relative to the roof centre.
    const float px = std::fabs(relPosLocal.x), py = std::fabs(relPosLocal.y);
    if (std::max(px, py) < 20.f) return FallSide::None;
    if (py > px) return FallSide::Side;
    return relPosLocal.x > 0.f ? FallSide::Front : FallSide::Back;
}

void RoofKeeper::update(bool onRoof, bool onFloor, float t, float dt) {
    if (lost_ != Loss::None) return;
    if (onRoof) {
        everOnRoof_ = true;
        offRoof_ = 0.f;
        heldUntil_ = t;
        onRoofTime_ += dt;
    } else {
        offRoof_ += dt;
    }
    if (onFloor) lost_ = Loss::Floor;
    else if (!onRoof && offRoof_ > tolerance_) lost_ = Loss::OffRoof;
}

namespace {

constexpr float kRoofBallZ = kCatchRoofZ;

float fallTimeTo(float z0, float vz, float zTarget) {
    // z0 + vz t - g/2 t^2 = zTarget, positive root.
    const float g = static_cast<float>(phys::kGravity);
    const float a = 0.5f * g, b = -vz, c = zTarget - z0;
    const float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return 0.f;
    return (-b + std::sqrt(disc)) / (2.f * a);
}

bool insideField(const Vec3& p, float margin) {
    return std::fabs(p.x) <= 4096.f - margin && std::fabs(p.y) <= 5120.f - margin - 600.f &&
           std::fabs(p.x) + std::fabs(p.y) <= 8064.f - margin * 1.5f;
}

Vec3 clampField(Vec3 p, float margin) {
    p.x = clampf(p.x, -(4096.f - margin), 4096.f - margin);
    p.y = clampf(p.y, -(4520.f - margin), 4520.f - margin);
    const float s = std::fabs(p.x) + std::fabs(p.y), lim = 8064.f - margin * 1.5f;
    if (s > lim) {
        const float k = lim / s;
        p.x *= k;
        p.y *= k;
    }
    return p;
}

} // namespace

float catchImpactSpeed(const Vec3& ballPos, const Vec3& ballVel) {
    const float g = static_cast<float>(phys::kGravity);
    return std::sqrt(std::max(0.f, ballVel.z * ballVel.z + 2.f * g * (ballPos.z - kRoofBallZ)));
}

float catchSettleTime(float impactSpeed, float e, float settleSpeed) {
    const float g = static_cast<float>(phys::kGravity);
    float t = 0.f;
    float v = impactSpeed * e;
    for (int i = 0; i < 30 && v >= settleSpeed; ++i) {
        t += 2.f * v / g;
        v *= e;
    }
    return t;
}

int catchHopCount(float impactSpeed, float e, float settleSpeed) {
    int n = 0;
    for (float v = impactSpeed * e; n < 30 && v >= settleSpeed; v *= e) ++n;
    return n;
}

CatchPlan planCatch(uint64_t seed, double d, int variant, float catchHold) {
    d = dclamp01(d);
    Rng rng(splitmix64(seed), 0xC47C4A11ULL);
    const float df = static_cast<float>(d);
    bool wall = variant == 2;
    if (variant == 0) wall = rngUniform01(rng) < 0.35;
    CatchPlan p;
    p.fromWall = wall;
    if (!wall) {
        // Highest point of the flight: low at niveau 1 (one or two small hops), up to 15 m.
        const float h = static_cast<float>(rngRange(rng, lerpf(350.f, 1350.f, df), lerpf(450.f, 1500.f, df)));
        // Low drops are tossed up a little first (released `rise` under the top, going up): the fall alone from 3.5 m
        // lasts 0.8 s, too short to get under the ball with a human reaction.
        const float rise = lerpf(150.f, 0.f, clamp01(df / 0.4f));
        const float vz0 = std::sqrt(2.f * static_cast<float>(phys::kGravity) * rise);
        p.apex = h;
        p.fallTime = fallTimeTo(h - rise, vz0, kRoofBallZ);
        const float cx = static_cast<float>(rngRange(rng, -2500, 2500));
        const float cy = static_cast<float>(rngRange(rng, -3000, 3000));
        const float yaw = static_cast<float>(rngRange(rng, -phys::kPi, phys::kPi));
        // Distance to the landing point grows slowly at first (a short fall leaves little time to drive).
        const float dist = lerpf(300.f, 1500.f, df * df) * static_cast<float>(rngRange(rng, 0.6, 1.0));
        const float spread = lerpf(35.f, 150.f, df) * kDegToRad;
        float bearing = yaw + spread * static_cast<float>(rngRange(rng, -1.0, 1.0));
        const float vh = lerpf(0.f, 450.f, df) * static_cast<float>(rngRange(rng, 0.4, 1.0));
        float vdir = static_cast<float>(rngRange(rng, -phys::kPi, phys::kPi));
        p.carPos = clampField({cx, cy, static_cast<float>(phys::kCarRestZ)}, 600.f);
        p.carYawRad = yaw;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const Vec3 land = p.carPos + Vec3{std::cos(bearing), std::sin(bearing), 0.f} * dist;
            const Vec3 vel{std::cos(vdir) * vh, std::sin(vdir) * vh, vz0};
            const Vec3 spawn = land - Vec3{vel.x, vel.y, 0.f} * p.fallTime;
            p.landing = {land.x, land.y, kRoofBallZ};
            p.ballPos = {spawn.x, spawn.y, h - rise};
            p.ballVel = vel;
            if (insideField(land, 400.f) && insideField(spawn, 300.f)) break;
            bearing += kPi;  // mirror towards the field centre side
            vdir += kPi;
        }
        // Last resort: keep everything inside the field even if the geometry changes a bit.
        if (!insideField(p.ballPos, 300.f) || !insideField(p.landing, 400.f)) {
            const Vec3 land = clampField(p.landing, 400.f);
            p.ballVel = {0.f, 0.f, vz0};
            p.landing = {land.x, land.y, kRoofBallZ};
            p.ballPos = {land.x, land.y, h - rise};
        }
    } else {
        const float side = rngUniform01(rng) < 0.5 ? -1.f : 1.f;
        const float y = static_cast<float>(rngRange(rng, -2500, 2500));
        // Niveau 1: 4.5 to 5 m high, a slow push off the wall and a little upward (1.4 s or more before it reaches the
        // roof), the car close to where it lands.
        const float h = lerpf(500.f, 1300.f, df) * static_cast<float>(rngRange(rng, 0.9, 1.0));
        const float vx = -side * lerpf(250.f, 650.f, df) * static_cast<float>(rngRange(rng, 0.8, 1.0));
        const float vy = lerpf(0.f, 350.f, df) * static_cast<float>(rngRange(rng, -1.0, 1.0));
        const float vz = lerpf(350.f, 0.f, df) * static_cast<float>(rngRange(rng, 0.7, 1.0));
        p.ballPos = {side * (4096.f - 91.25f - 40.f), y, h};
        p.ballVel = {vx, vy, vz};
        p.apex = h + vz * vz / (2.f * static_cast<float>(phys::kGravity));
        p.fallTime = fallTimeTo(h, vz, kRoofBallZ);
        const Vec3 land = p.ballPos + Vec3{vx, vy, 0.f} * p.fallTime;
        p.landing = {land.x, land.y, kRoofBallZ};
        const float back = lerpf(200.f, 1300.f, df) * static_cast<float>(rngRange(rng, 0.8, 1.0));
        const float yOff = lerpf(0.f, 800.f, df) * static_cast<float>(rngRange(rng, -1.0, 1.0));
        p.carPos = clampField({land.x - side * back, land.y + yOff, static_cast<float>(phys::kCarRestZ)}, 600.f);
        const Vec3 to = p.landing - p.carPos;
        p.carYawRad = std::atan2(to.y, to.x);
    }
    p.impactSpeed = catchImpactSpeed(p.ballPos, p.ballVel);
    p.settleTime = catchSettleTime(p.impactSpeed);
    p.timeLimit = p.fallTime + p.settleTime + std::max(0.f, catchHold) + kCatchMargin;
    return p;
}

bool predictDownCrossing(Vec3 p, Vec3 v, float planeZ, float maxT, Vec3& out, float& tOut) {
    const float dt = 1.f / 120.f;
    const float drag = static_cast<float>(phys::kBallDrag);
    const float g = static_cast<float>(phys::kGravity);
    const int steps = std::clamp(static_cast<int>(maxT * 120.f), 1, 1200);
    for (int i = 0; i < steps; ++i) {
        Vec3 nv = v * (1.f - dt * drag) + Vec3{0.f, 0.f, -g * dt};
        const Vec3 np = p + nv * dt;
        if (p.z >= planeZ && np.z < planeZ && nv.z < 0.f) {
            const float f = (p.z - planeZ) / (p.z - np.z);
            out = p + (np - p) * f;
            tOut = (static_cast<float>(i) + f) * dt;
            return true;
        }
        p = np;
        v = nv;
    }
    return false;
}

} // namespace rlt::courses
