#include "core/modules/ballmech/Drills.h"

#include <algorithm>
#include <cmath>

#include "core/modules/ballmech/Primitives.h"

namespace rlt::bm {

namespace {
float randSign(Rng& rng) { return rng.chance(0.5f) ? 1.f : -1.f; }

// Ball resting on the roof centre of a car placed at `pos` with `basis` (2 uu above the contact).
Vec3 roofBallPos(const Vec3& pos, const Basis& basis, const CarState& car, float r) {
    const float roof = car.hitboxOffset.z + car.hitboxExtent.z * 0.5f + r + 2.f;
    return pos + basis.up * roof + basis.fwd * car.hitboxOffset.x;
}

void groundCar(DrillSetup& d, const Vec3& pos, float yawDeg, float speed) {
    d.car.pos = pos;
    d.car.rot = rotDeg(0.f, yawDeg, 0.f);
    const Basis b = basisFromRot(d.car.rot);
    d.car.vel = flat(b.fwd) * speed;
}

void mirrorForTeam(DrillSetup& d, int team) {
    if (attackSign(team) > 0.f) return;
    auto m = [](const Vec3& v) { return Vec3{-v.x, -v.y, v.z}; };
    d.car.pos = m(d.car.pos);
    d.car.vel = m(d.car.vel);
    d.car.angVel = m(d.car.angVel);
    d.ball.pos = m(d.ball.pos);
    d.ball.vel = m(d.ball.vel);
    d.ball.angVel = m(d.ball.angVel);
    int yaw = d.car.rot.yaw + 32768;
    while (yaw >= 32768) yaw -= 65536;
    d.car.rot.yaw = yaw;
}
} // namespace

Rot rotDeg(float pitchDeg, float yawDeg, float rollDeg) {
    auto wrap = [](float a) {
        a = std::fmod(a + 180.f, 360.f);
        if (a < 0.f) a += 360.f;
        return a - 180.f;
    };
    const float k = 65536.f / 360.f;
    return {static_cast<int>(std::lround(clampf(pitchDeg, -89.9f, 89.9f) * k)), static_cast<int>(std::lround(wrap(yawDeg) * k)),
            static_cast<int>(std::lround(wrap(rollDeg) * k))};
}

void ballisticStart(const Vec3& meetPos, const Vec3& meetVel, float t, Vec3& startPos, Vec3& startVel) {
    const Vec3 g{0.f, 0.f, arena::kGravityZ};
    startVel = meetVel - g * t;
    startPos = meetPos - startVel * t - g * (0.5f * t * t);
}

DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float r, Rng& rng,
                          const std::string& flickVariant) {
    const float L = clamp01(level);
    DrillSetup d;
    d.car.boost = 100.f;
    switch (m) {
    case Mech::FlipReset: {
        // Ball popped high in front, car below it with momentum toward the ball, flip already spent.
        const Vec3 ball{rng.range(-1.f, 1.f) * (200.f + 1000.f * L), rng.range(1200.f, 2400.f), rng.range(850.f, 1050.f)};
        const float e = rng.range(35.f, 55.f) * kDegToRad;
        const float a = rng.range(-1.f, 1.f) * (10.f + 30.f * L) * kDegToRad;
        const Vec3 dir{std::sin(a) * std::cos(e), std::cos(a) * std::cos(e), std::sin(e)};
        const float startDist = 450.f + 450.f * L;
        const float timeToContact = 1.0f + 0.4f * L;
        const float contactDist = r + 20.f;
        d.ball.pos = ball;
        d.ball.vel = {rng.range(-1.f, 1.f) * (80.f + 200.f * L), rng.range(0.f, 150.f + 250.f * L), rng.range(150.f, 300.f)};
        d.car.pos = ball - dir * startDist;
        const Vec3 carVel = d.ball.vel + dir * ((startDist - contactDist) / timeToContact);
        // Wheels (underside) facing the ball: upside down, nose pitched down toward it, plus a level-dependent error.
        const float yaw = std::atan2(dir.y, dir.x) * kRadToDeg + randSign(rng) * 25.f * L * rng.uniform();
        const float pitch = -std::atan2(length2D(dir), dir.z) * kRadToDeg;
        const float roll = 180.f + randSign(rng) * 60.f * L * rng.uniform();
        d.car.rot = rotDeg(pitch, yaw, roll);
        const Basis b = basisFromRot(d.car.rot);
        d.car.vel = carVel - b.up * arena::kJumpImpulse;  // the scripted double jump adds it back
        d.car.boost = 100.f - 60.f * L;
        d.burnFlip = true;
        d.label = L < 0.35f ? "Obtenir le reset" : (L < 0.7f ? "Reset puis double touche" : "Reset puis tir cadré");
        break;
    }
    case Mech::Ceiling: {
        const float x0 = rng.range(-1.f, 1.f) * (300.f + 1200.f * L);
        const float y0 = rng.range(-800.f, 200.f);
        const float vc = 700.f + 400.f * L;
        d.car.pos = {x0, y0, arena::kCeilingZ - arena::kCarRestZ};
        d.car.rot = rotDeg(0.f, 90.f, 180.f);
        d.car.vel = {0.f, vc, 0.f};
        const float tFall = rng.range(1.3f, 1.6f);
        const Vec3 meetCar{x0, y0 + vc * tFall, d.car.pos.z + 0.5f * arena::kGravityZ * tFall * tFall};
        const Vec3 meetBall = meetCar + Vec3{rng.range(-1.f, 1.f) * 600.f * L, 150.f, -30.f};
        const Vec3 meetVel{0.f, rng.range(100.f, 300.f), -(100.f + 300.f * L)};
        ballisticStart(meetBall, meetVel, tFall, d.ball.pos, d.ball.vel);
        d.ball.pos.z = clampf(d.ball.pos.z, 150.f, 1800.f);
        d.car.boost = 100.f - 50.f * L;
        d.label = "Chute du plafond";
        break;
    }
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi: {
        const float speed = m == Mech::Flick ? 500.f + 900.f * L : (m == Mech::Musty ? 600.f + 400.f * L : 700.f + 400.f * L);
        const Vec3 pos{rng.range(-1.f, 1.f) * (300.f + 900.f * L), rng.range(500.f, 1500.f), arena::kCarRestZ};
        groundCar(d, pos, 90.f + rng.range(-1.f, 1.f) * (5.f + 10.f * L), speed);
        const Basis b = basisFromRot(d.car.rot);
        d.ball.pos = roofBallPos(pos, b, car, r);
        d.ball.vel = d.car.vel;
        d.car.boost = 100.f - 50.f * L;
        d.flickTarget = m == Mech::Musty ? "musty" : (m == Mech::Breezi ? "breezi" : flickVariant);
        d.label = "Balle sur le toit";
        break;
    }
    case Mech::DoubleTap: {
        if (L < 0.6f) {
            const float x0 = rng.range(-1.f, 1.f) * 1500.f * L;
            const Vec3 start{x0, 2500.f, 300.f};
            const Vec3 target{x0 * 0.5f + rng.range(-200.f, 200.f), arena::kBackWallY - r - 5.f, rng.range(900.f, 1300.f)};
            const float speed = 1400.f + 600.f * L;
            const float t = std::max(0.5f, distance(start, target) / speed);
            d.ball.pos = start;
            d.ball.vel = (target - start) / t + Vec3{0.f, 0.f, -0.5f * arena::kGravityZ * t};
            groundCar(d, {x0, 2500.f - (1000.f + 1200.f * L), arena::kCarRestZ}, 90.f, 500.f);
            d.doubleTapRead = true;
            d.label = "Lecture du rebond";
        } else {
            const float x0 = rng.range(-1.f, 1.f) * 800.f;
            d.ball.pos = {x0, 3000.f, 250.f};
            d.ball.vel = {rng.range(-100.f, 100.f), 500.f + rng.range(0.f, 200.f), 1100.f};
            groundCar(d, {x0, 1600.f, arena::kCarRestZ}, 90.f, 900.f);
            d.label = "Double tap complet";
        }
        d.car.boost = 100.f - 50.f * L;
        break;
    }
    case Mech::AirDribble: {
        const bool wall = L >= 0.5f && rng.chance(0.5f);
        if (!wall) {
            const Vec3 pos{rng.range(-1.f, 1.f) * 1500.f, rng.range(-2500.f, -1000.f), arena::kCarRestZ};
            groundCar(d, pos, 90.f + rng.range(-1.f, 1.f) * 10.f, 500.f + 300.f * L);
            const Basis b = basisFromRot(d.car.rot);
            d.ball.pos = roofBallPos(pos, b, car, r);
            d.ball.vel = d.car.vel;
            d.label = "Depuis le sol";
        } else {
            const float side = randSign(rng);
            const float y = rng.range(-2200.f, -1400.f);
            groundCar(d, {3300.f * side, y, arena::kCarRestZ}, 90.f - 35.f * side, 1000.f);
            d.ball.pos = {3750.f * side, y + 450.f, 200.f};
            d.ball.vel = {500.f * side, 700.f, 250.f};
            d.label = "Depuis le mur";
        }
        d.car.boost = 100.f - 50.f * L;
        break;
    }
    case Mech::AirRoll: {
        const float side = randSign(rng);
        d.car.pos = {rng.range(-600.f, 600.f), 1300.f, 500.f};
        d.ball.pos = {d.car.pos.x + rng.range(-300.f, 300.f), 2700.f + rng.range(0.f, 500.f), 950.f + rng.range(0.f, 250.f)};
        const Vec3 to = d.ball.pos - d.car.pos;
        d.car.rot = rotDeg(15.f, std::atan2(to.y, to.x) * kRadToDeg, side * (90.f + 80.f * L));
        d.car.vel = normalize(flat(to)) * 800.f + Vec3{0.f, 0.f, 250.f};
        d.ball.vel = {0.f, 0.f, 120.f};
        d.label = side > 0.f ? "Voiture penchée à droite" : "Voiture penchée à gauche";
        break;
    }
    case Mech::Aerial: {
        const float side = randSign(rng);
        const Vec3 start{side * 3000.f, 3700.f, 150.f};
        const Vec3 target{side * rng.range(0.f, 300.f), 3500.f + rng.range(-300.f, 200.f), 850.f + rng.range(0.f, 350.f) + 200.f * L};
        const float t = 1.7f - 0.4f * L;
        d.ball.pos = start;
        d.ball.vel = (target - start) / t + Vec3{0.f, 0.f, -0.5f * arena::kGravityZ * t};
        groundCar(d, {rng.range(-500.f, 500.f), 1300.f, arena::kCarRestZ}, 90.f, 300.f);
        d.label = side > 0.f ? "Centre depuis la droite" : "Centre depuis la gauche";
        break;
    }
    case Mech::Pinch: {
        const float side = randSign(rng);
        const float y = rng.range(-600.f, 600.f);
        d.ball.pos = {side * 3650.f, y, r + 2.f};
        d.ball.vel = {side * (450.f + 300.f * L), 500.f + 300.f * L, 0.f};
        const Vec3 carPos{side * 2500.f, y - 1100.f, arena::kCarRestZ};
        const Vec3 aim = d.ball.pos + d.ball.vel * 0.8f - carPos;
        groundCar(d, carPos, std::atan2(aim.y, aim.x) * kRadToDeg, 900.f);
        d.label = "Balle vers le mur";
        break;
    }
    case Mech::PowerShot: {
        const float x0 = rng.range(-1.f, 1.f) * 800.f;
        if (L < 0.5f) {
            d.ball.pos = {x0, 3000.f, r + 2.f};
            d.ball.vel = {rng.range(-100.f, 100.f), -(400.f + 600.f * L), 0.f};
            groundCar(d, {x0 + rng.range(-200.f, 200.f), 200.f, arena::kCarRestZ}, 90.f, 300.f);
            d.label = "Balle de face";
        } else {
            const float side = randSign(rng);
            d.ball.pos = {x0 + side * 1500.f, 2600.f, r + 2.f};
            d.ball.vel = {-side * (500.f + 400.f * L), -150.f, 0.f};
            groundCar(d, {x0, 900.f, arena::kCarRestZ}, 90.f, 400.f);
            d.label = "Balle de côté (glissade)";
        }
        d.car.boost = 100.f - 67.f * L;
        break;
    }
    default: break;
    }
    mirrorForTeam(d, team);
    return d;
}

bool gradeMatchesDrill(Mech drill, const MechGrade& g) { return mechInfo(drill).family == g.family; }

DrillVerdict judgeDrill(Mech drill, const MechGrade& g, float level, int minScore, float pinchSpeed) {
    const float L = clamp01(level);
    DrillVerdict v;
    const bool scoreOk = g.score * 100.f + 1e-3f >= static_cast<float>(minScore);
    v.success = g.success && scoreOk;
    if (g.success && !scoreOk) v.extraErrors.push_back("DRILL_NEED_SCORE");
    auto need = [&v](bool ok, const char* code) {
        if (!ok && v.success) {
            v.success = false;
            v.extraErrors.push_back(code);
        }
    };
    const bool onTarget = !g.projection.valid || g.projection.onTarget;
    switch (drill) {
    case Mech::FlipReset: {
        const bool obtained = g.variant != "no_reset";
        if (L < 0.35f) {
            v.success = obtained;
            v.extraErrors.clear();
        } else if (obtained && !g.success) {
            v.extraErrors.push_back("DRILL_NEED_CONVERT");
        }
        if (L >= 0.7f) need(onTarget, "DRILL_NEED_TARGET");
        break;
    }
    case Mech::Ceiling:
        need(g.value("ball_speed_after") >= 1200.f + 1000.f * L, "DRILL_NEED_POWER");
        if (L >= 0.5f) need(onTarget, "DRILL_NEED_TARGET");
        break;
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi:
        need(g.value("ball_dv") >= 700.f + 800.f * L, "DRILL_NEED_POWER");
        if (L >= 0.5f) need(onTarget, "DRILL_NEED_TARGET");
        break;
    case Mech::DoubleTap:
        need(g.value("ball_speed_after") >= 800.f + 1000.f * L, "DRILL_NEED_POWER");
        break;
    case Mech::AirDribble:
        need(g.value("duration") >= 0.8f + 2.2f * L, "DRILL_NEED_DURATION");
        if (L >= 0.6f && g.has("finished_on_target")) need(g.value("finished_on_target") >= 0.5f, "DRILL_NEED_TARGET");
        break;
    case Mech::AirRoll:
        if (g.variant != "air_roll_shot") {
            v.success = false;
            v.extraErrors.push_back("AE_NO_AIR_ROLL");
        }
        need(g.value("ball_speed_after") >= 1200.f + 1000.f * L, "DRILL_NEED_POWER");
        break;
    case Mech::Aerial:
        need(g.value("ball_speed_after") >= 1500.f + 1000.f * L, "DRILL_NEED_POWER");
        break;
    case Mech::Pinch:
        need(g.value("exit_speed") >= pinchSpeed + 1000.f * L, "DRILL_NEED_POWER");
        if (L >= 0.5f) need(onTarget, "DRILL_NEED_TARGET");
        break;
    case Mech::PowerShot:
        need(g.value("exit_speed") >= 2000.f + 1200.f * L, "DRILL_NEED_POWER");
        break;
    default: break;
    }
    return v;
}

} // namespace rlt::bm
