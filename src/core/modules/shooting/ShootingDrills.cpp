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
    const float speed = moving ? (fixedSpeed >= 0.f ? fixedSpeed : lerpf(400.f, 1100.f, d)) : 0.f;
    sp.carReleaseVel = flat(b.fwd) * speed;
    sp.ballReleaseVel = sp.carReleaseVel;
    return sp;
}

Spawn targetSpawn(TargetSetup setup, float level, const GoalFrame& goal, const RoofParams& roof, Rng& rng) {
    const float d = clamp01(level);
    const float s = goal.sign;
    switch (setup) {
    case TargetSetup::Bouncing: {
        const float by = s * (kGoalLineY - lerpf(1600.f, 2300.f, d));
        const float xr = lerpf(200.f, 1200.f, d);
        const Vec3 ballPos{rng.range(-xr, xr), by, lerpf(500.f, 900.f, d)};
        Vec3 carPos{ballPos.x * 0.5f + rng.range(-300.f, 300.f), by - s * lerpf(1500.f, 2000.f, d), kCarRestZ};
        carPos = clampToField(carPos, 300.f);
        carPos.z = kCarRestZ;
        Spawn sp = facing(carPos, normalize(flat(ballPos - carPos), {0.f, s, 0.f}), goal);
        sp.ball = heldBall(ballPos);
        sp.ballReleaseVel = {rng.range(-250.f, 250.f) * d, -s * rng.range(0.f, lerpf(100.f, 500.f, d)),
                             lerpf(0.f, 300.f, d)};
        return sp;
    }
    case TargetSetup::Pass: {
        const float side = rng.chance(0.5f) ? 1.f : -1.f;
        const Vec3 ballPos{side * lerpf(2600.f, 3300.f, d), s * (kGoalLineY - lerpf(1100.f, 1600.f, d)), kBallRestZ};
        Vec3 carPos{rng.range(-500.f, 500.f), s * (kGoalLineY - lerpf(3300.f, 3900.f, d)), kCarRestZ};
        Spawn sp = facing(carPos, {0.f, s, 0.f}, goal);
        sp.ball = heldBall(ballPos);
        const float lift = d > 0.5f ? lerpf(0.f, 700.f, (d - 0.5f) * 2.f) : 0.f;
        sp.ballReleaseVel = {-side * lerpf(800.f, 1500.f, d), -s * rng.range(0.f, 300.f), lift};
        if (lift <= 0.f) sp.ballReleaseAngVel = rollingSpin(sp.ballReleaseVel, roof.ballRadius);
        sp.carReleaseVel = flat(basisFromRot(sp.car.rot).fwd) * lerpf(600.f, 1000.f, d);
        return sp;
    }
    case TargetSetup::Roof: {
        const bool moving = d > 0.3f && rng.chance(0.6f);
        return flickSpawn(level, moving, -1.f, goal, roof, rng);
    }
    case TargetSetup::Rolling:
    case TargetSetup::Mix:
    default: {
        const float side = rng.chance(0.5f) ? 1.f : -1.f;
        const Vec3 ballPos{side * rng.range(lerpf(200.f, 600.f, d), lerpf(900.f, 2200.f, d)),
                           s * (kGoalLineY - lerpf(3000.f, 3800.f, d)), kBallRestZ};
        const Vec3 aim{-side * rng.range(0.f, 500.f), s * (kGoalLineY - lerpf(1400.f, 1000.f, d)), kBallRestZ};
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
