#include "core/modules/shooting/ShootingDrills.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace rlt::shoot {

const char* setupId(TargetSetup s) {
    switch (s) {
    case TargetSetup::Mix: return "mix";
    case TargetSetup::Rolling: return "rolling";
    case TargetSetup::Bouncing: return "bouncing";
    case TargetSetup::Pass: return "pass";
    case TargetSetup::Roof: return "roof";
    default: return "?";
    }
}

const char* setupLabel(TargetSetup s) {
    switch (s) {
    case TargetSetup::Mix: return "Mélange";
    case TargetSetup::Rolling: return "Balle roulante";
    case TargetSetup::Bouncing: return "Balle qui rebondit";
    case TargetSetup::Pass: return "Centre en travers";
    case TargetSetup::Roof: return "Balle sur le toit";
    default: return "";
    }
}

bool parseSetup(const std::string& text, int& out) {
    std::string s;
    for (char c : text) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    struct Alias {
        const char* name;
        int setup;
    };
    static const Alias kAliases[] = {
        {"mix", 0},      {"melange", 0},  {"random", 0}, {"aleatoire", 0}, {"rolling", 1}, {"roll", 1},
        {"roulante", 1}, {"bouncing", 2}, {"bounce", 2}, {"rebond", 2},    {"pass", 3},    {"cross", 3},
        {"travers", 3},  {"roof", 4},     {"toit", 4},   {"flick", 4},
    };
    for (const auto& a : kAliases) {
        if (s == a.name) {
            out = a.setup;
            return true;
        }
    }
    return false;
}

Vec3 clampToField(Vec3 p, float margin) {
    const float mx = kArenaHalfX - margin, my = kArenaHalfY - margin;
    p.x = clampf(p.x, -mx, mx);
    p.y = clampf(p.y, -my, my);
    const float corner = 8064.f - margin * 1.42f; // chamfer plane |x| + |y| = 8064
    const float sum = std::fabs(p.x) + std::fabs(p.y);
    if (sum > corner) {
        const float k = corner / sum;
        p.x *= k;
        p.y *= k;
    }
    return p;
}

Vec3 rollingSpin(const Vec3& vel, float radius) {
    if (!(radius > 1.f)) radius = kBallRadius;
    const Vec3 w{-vel.y / radius, vel.x / radius, 0.f};
    const float l = length(w);
    return l > kBallMaxSpin ? w * (kBallMaxSpin / l) : w;
}

namespace {

CarPlacement heldCar(const Vec3& pos, const Rot& rot) {
    CarPlacement c;
    c.pos = pos;
    c.rot = rot;
    return c;
}

BallPlacement heldBall(const Vec3& pos) {
    BallPlacement b;
    b.pos = pos;
    return b;
}

Spawn facing(const Vec3& carPos, const Vec3& dir, const GoalFrame& goal) {
    Spawn sp;
    sp.goal = goal;
    sp.car = heldCar(carPos, rotFromDirection(flat(dir)));
    return sp;
}

} // namespace

float movingRoofSpeed(float level, float firstMovingLevel) {
    const float d = clamp01(level);
    const float first = clamp01(firstMovingLevel);
    if (d < first + 0.1f) return 300.f;
    return lerpf(400.f, 1100.f, clamp01((d - first - 0.1f) / std::max(0.05f, 0.9f - first)));
}

Spawn flickSpawn(float level, bool moving, float fixedSpeed, const GoalFrame& goal, const RoofParams& roof, Rng& rng) {
    const float d = clamp01(level);
    const float dist = lerpf(2000.f, 4500.f, d);
    const float xr = lerpf(200.f, 1400.f, d);
    Vec3 carPos{rng.range(-xr, xr), goal.sign * (kGoalLineY - dist), kCarRestZ};
    carPos = clampToField(carPos, 300.f);
    carPos.z = kCarRestZ;
    const Vec3 aim{0.f, goal.planeY(), 0.f};
    Spawn sp = facing(carPos, normalize(flat(aim - carPos), {0.f, goal.sign, 0.f}), goal);
    const Basis b = basisFromRot(sp.car.rot);
    sp.ball = heldBall(roofSpawn(carPos, b, roof.hb, roof.ballRadius, roof.gap));
    sp.ballOnRoof = true;
    const float speed = moving ? (fixedSpeed >= 0.f ? fixedSpeed : movingRoofSpeed(d, 0.f)) : 0.f;
    sp.carReleaseVel = flat(b.fwd) * speed;
    sp.ballReleaseVel = sp.carReleaseVel;
    return sp;
}

Spawn targetSpawn(TargetSetup setup, float level, const GoalFrame& goal, const RoofParams& roof, Rng& rng,
                  const TargetOptions& opt) {
    const float d = clamp01(level);
    const float s = goal.sign;
    switch (setup) {
    case TargetSetup::Bouncing: {
        // Niveau 1: dropped from 400 uu, the car 1200 uu behind at rest reaches it around its second bounce.
        const float by = s * (kGoalLineY - lerpf(1600.f, 2300.f, d));
        const float xr = lerpf(200.f, 1200.f, d);
        const Vec3 ballPos{rng.range(-xr, xr), by, lerpf(400.f, 900.f, d)};
        Vec3 carPos{ballPos.x * 0.5f + rng.range(-300.f, 300.f), by - s * lerpf(1200.f, 2000.f, d), kCarRestZ};
        carPos = clampToField(carPos, 300.f);
        carPos.z = kCarRestZ;
        Spawn sp = facing(carPos, normalize(flat(ballPos - carPos), {0.f, s, 0.f}), goal);
        sp.ball = heldBall(ballPos);
        sp.ballReleaseVel = {rng.range(-250.f, 250.f) * d, -s * rng.range(0.f, lerpf(100.f, 500.f, d)),
                             lerpf(0.f, 300.f, d)};
        return sp;
    }
    case TargetSetup::Pass: {
        // A cross from the side rolling across the goal (with its rolling spin); the car starts at rest and can be at
        // the middle 0.2 to 1 s before the ball (about 0.6 s at niveau 1: 550 uu/s from 1500 uu on the side).
        const float side = rng.chance(0.5f) ? 1.f : -1.f;
        const Vec3 ballPos{side * lerpf(1500.f, 2600.f, d), s * (kGoalLineY - lerpf(1400.f, 1600.f, d)), kBallRestZ};
        const Vec3 carPos{rng.range(-400.f, 400.f), s * (kGoalLineY - lerpf(3300.f, 3900.f, d)), kCarRestZ};
        Spawn sp = facing(carPos, {0.f, s, 0.f}, goal);
        sp.ball = heldBall(ballPos);
        sp.ballReleaseVel = {-side * lerpf(550.f, 1300.f, std::sqrt(d)), -s * rng.range(0.f, 150.f), 0.f};
        sp.ballReleaseAngVel = rollingSpin(sp.ballReleaseVel, roof.ballRadius);
        sp.carReleaseVel = {};
        return sp;
    }
    case TargetSetup::Roof: {
        const bool moving = opt.roofMovingAllowed && d > kRoofMovingFirstLevel && rng.chance(0.6f);
        Spawn sp = flickSpawn(level, false, -1.f, goal, roof, rng);
        if (moving) {
            const float speed = movingRoofSpeed(d, kRoofMovingFirstLevel);
            sp.carReleaseVel = flat(basisFromRot(sp.car.rot).fwd) * speed;
            sp.ballReleaseVel = sp.carReleaseVel;
        }
        return sp;
    }
    case TargetSetup::Rolling:
    case TargetSetup::Mix:
    default: {
        // The ball rolls on a line that crosses the goal line at least 400 uu outside the goal mouth: untouched, it
        // misses; the player catches it and redirects it into the zone.
        const float side = rng.chance(0.5f) ? 1.f : -1.f;
        const Vec3 ballPos{side * rng.range(lerpf(200.f, 600.f, d), lerpf(900.f, 2200.f, d)),
                           s * (kGoalLineY - lerpf(3000.f, 3800.f, d)), kBallRestZ};
        const Vec3 aim{side * rng.range(1350.f, 1700.f), s * kGoalLineY, kBallRestZ};
        const Vec3 dir = normalize(flat(aim - ballPos), {0.f, s, 0.f});
        const float vb = lerpf(500.f, 1100.f, d);
        Vec3 carPos = clampToField(ballPos - dir * lerpf(700.f, 1000.f, d), 300.f);
        carPos.z = kCarRestZ;
        Spawn sp = facing(carPos, dir, goal);
        sp.ball = heldBall(ballPos);
        sp.ballReleaseVel = dir * vb;
        sp.ballReleaseAngVel = rollingSpin(sp.ballReleaseVel, roof.ballRadius);
        sp.carReleaseVel = flat(basisFromRot(sp.car.rot).fwd) * (vb * lerpf(0.9f, 0.7f, d));
        return sp;
    }
    }
}

Spawn powerSpawn(float ballSpeed, const GoalFrame& goal, Rng& rng) {
    const float s = goal.sign;
    const Vec3 carPos{rng.range(-300.f, 300.f), -s * 2500.f, kCarRestZ};
    const Vec3 ballPos{carPos.x + rng.range(-400.f, 400.f), s * 500.f, kBallRestZ};
    Spawn sp = facing(carPos, {0.f, s, 0.f}, goal);
    sp.ball = heldBall(ballPos);
    sp.ballReleaseVel = normalize(flat(carPos - ballPos), {0.f, -s, 0.f}) * std::max(0.f, ballSpeed);
    sp.ballReleaseAngVel = rollingSpin(sp.ballReleaseVel);
    return sp;
}

} // namespace rlt::shoot

namespace rlt::shoot {

Zone pickTargetZone(int selection, float level, Zone previous, Rng& rng) {
    if (selection >= 0 && selection < kZoneCount) return static_cast<Zone>(selection);
    if (1 + static_cast<int>(clamp01(level) * 9.f + 0.5f) < kTopCornersNiveau) {  // shootPlayerLevel
        static const Zone kLow[3] = {Zone::LowLeft, Zone::LowRight, Zone::Center};
        int i = rng.rangeInt(0, 2);
        if (kLow[i] == previous) i = rng.rangeInt(0, 2);  // fewer repeats
        return kLow[i];
    }
    int z = rng.rangeInt(0, kZoneCount - 1);
    if (z == static_cast<int>(previous)) z = rng.rangeInt(0, kZoneCount - 1);
    return static_cast<Zone>(z);
}

float powerGoalKph(float level) { return lerpf(70.f, 110.f, clamp01(level)); }

namespace {
// One tick of the light ball model (same as predictToPlaneY): drag, gravity, floor contact with spin, walls.
bool stepLight(Vec3& p, Vec3& v, Vec3& w) {
    const float dt = kTickDt;
    const float radius = kBallRadius;
    const float lever = 0.4f * radius;
    bool bounced = false;
    v = v * (1.f - kBallDrag * dt);
    v.z -= kGravity * dt;
    if (v.z < 0.f && p.z + v.z * dt < kBallRestZ) {
        const float vn = v.z;
        const bool bounce = -vn > 15.f;
        const float vnOut = bounce ? -kBallRestitution * vn : 0.f;
        const float jn = vnOut - vn;
        const Vec3 slip{v.x - radius * w.y, v.y + radius * w.x, 0.f};
        const float sl = length(slip);
        if (sl > 1e-4f) {
            const float mag = std::min(sl * (2.f / 7.f), kBallFriction * jn);
            const Vec3 dvt = slip * (-mag / sl);
            v.x += dvt.x;
            v.y += dvt.y;
            w.x += dvt.y / lever;
            w.y -= dvt.x / lever;
        }
        v.z = vnOut;
        bounced = bounce;
    }
    const float wallX = kArenaHalfX - kBallRestZ;
    if (std::fabs(p.x + v.x * dt) > wallX && v.x * p.x > 0.f) v.x = -kBallRestitution * v.x;
    const float spin = length(w);
    if (spin > kBallMaxSpin) w = w * (kBallMaxSpin / spin);
    p += v * dt;
    if (p.z < kBallRestZ) p.z = kBallRestZ;
    return bounced;
}
} // namespace

std::vector<FloorBounce> predictFloorBounces(Vec3 pos, Vec3 vel, Vec3 angVel, int count, float maxTime) {
    std::vector<FloorBounce> out;
    const int steps = std::clamp(static_cast<int>(maxTime * kTickRate), 1, 1440);
    for (int i = 1; i <= steps && static_cast<int>(out.size()) < count; ++i) {
        const Vec3 before = pos;
        if (stepLight(pos, vel, angVel)) out.push_back(FloorBounce{Vec3{before.x, before.y, kBallRestZ}, static_cast<float>(i) * kTickDt});
    }
    return out;
}

Vec3 predictBallAt(Vec3 pos, Vec3 vel, Vec3 angVel, float t) {
    const int steps = std::clamp(static_cast<int>(t * kTickRate), 0, 1440);
    for (int i = 0; i < steps; ++i) stepLight(pos, vel, angVel);
    return pos;
}

FloorBounce crossMeetingPoint(const Vec3& ballPos, const Vec3& ballVel, const Vec3& ballAngVel, const Vec3& carPos) {
    Vec3 p = ballPos, v = ballVel, w = ballAngVel;
    FloorBounce best{p, 0.f};
    float bestDx = std::fabs(p.x - carPos.x);
    for (int i = 1; i <= 600; ++i) {
        stepLight(p, v, w);
        const float dx = std::fabs(p.x - carPos.x);
        if (dx < bestDx) {
            bestDx = dx;
            best = FloorBounce{Vec3{p.x, p.y, kBallRestZ}, static_cast<float>(i) * kTickDt};
        }
    }
    return best;
}

Vec3 powerMeetingPoint(const Vec3& carPos, const Vec3& ballPos, const Vec3& ballVel) {
    // 1D: the car accelerates from rest with boost toward the ball rolling at it; contact at 150 uu.
    const Vec3 dir = normalize(flat(ballPos - carPos), {0.f, 1.f, 0.f});
    const float vb = std::max(0.f, -dot(flat(ballVel), dir));
    float x = 0.f, v = 0.f, gap = length2D(ballPos - carPos);
    for (int i = 0; i < 1200 && gap - x > 150.f; ++i) {
        const float a = (v < 1400.f ? 1600.f - 1440.f * v / 1400.f : 0.f) + 991.67f;
        v = std::min(2300.f, v + a * kTickDt);
        x += v * kTickDt;
        gap -= vb * kTickDt;
    }
    return Vec3{carPos.x, carPos.y, 0.f} + dir * x + Vec3{0.f, 0.f, kBallRestZ};
}

Vec3 powerRunUpPoint(const Vec3& carPos, const Vec3& meet) {
    const Vec3 d = flat(meet - carPos);
    const float len = length(d);
    if (len < 1.f) return carPos;
    return Vec3{carPos.x, carPos.y, 0.f} + d * (std::max(0.f, len - 1200.f) / len);
}

} // namespace rlt::shoot
