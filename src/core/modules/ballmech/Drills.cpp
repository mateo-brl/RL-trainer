#include "core/modules/ballmech/Drills.h"

#include <algorithm>
#include <cmath>

#include "core/common/Pickup.h"
#include "core/common/Text.h"
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
    d.releaseVel = m(d.releaseVel);
    d.target = m(d.target);
    d.meetPoint = m(d.meetPoint);
    for (auto& g : d.guide) g = m(g);
    d.side = -d.side;
    int yaw = d.car.rot.yaw + 32768;
    while (yaw >= 32768) yaw -= 65536;
    d.car.rot.yaw = yaw;
}
} // namespace

int playerLevel(float level) { return 1 + static_cast<int>(clamp01(level) * 9.f + 0.5f); }

float levelOfPlayer(int n) { return clamp01(static_cast<float>(n - 1) / 9.f); }

int drillMinScore(int successScore, float level) {
    const int v = successScore - 25 + static_cast<int>(std::lround(45.f * clamp01(level)));
    return std::max(0, std::min(100, v));
}

float drillTimeFactor(float level) { return 1.5f - 0.5f * clamp01(level); }

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

BallPlacement ceilingReleaseAim(const CarState& car, float apex) {
    const float T = apex;
    const Vec3 f = normalize(flat(car.vel), normalize(flat(car.basis.fwd), Vec3{1.f, 0.f, 0.f}));
    const Vec3 pc = car.pos + car.vel * T + Vec3{0.f, 0.f, 0.5f * arena::kGravityZ * T * T};  // passive fall
    const Vec3 hc = pc + car.basis.fwd * car.hitboxOffset.x + car.basis.up * car.hitboxOffset.z;
    Vec3 meet = hc + f * std::max(260.f, 0.30f * length(flat(car.vel)));
    meet.x = clampf(meet.x, -arena::kSideWallX + 300.f, arena::kSideWallX - 300.f);  // never in a wall
    meet.y = clampf(meet.y, -arena::kBackWallY + 300.f, arena::kBackWallY - 300.f);
    meet.z = hc.z;
    BallPlacement b;
    b.pos = {meet.x, meet.y, meet.z + 0.5f * arena::kGravityZ * T * T};
    b.vel = {0.f, 0.f, -arena::kGravityZ * T};  // apex T s later, at `meet`
    return b;
}

void ballisticStart(const Vec3& meetPos, const Vec3& meetVel, float t, Vec3& startPos, Vec3& startVel) {
    const Vec3 g{0.f, 0.f, arena::kGravityZ};
    startVel = meetVel - g * t;
    startPos = meetPos - startVel * t - g * (0.5f * t * t);
}

// ---- Drill starts ---------------------------------------------------------------------------------------------
DrillMode defaultMode(Mech m) { return m == Mech::Ceiling ? DrillMode::Full : DrillMode::Short; }

const char* modeLabel(Mech m, DrillMode mode) {
    if (mode == DrillMode::Full) {
        switch (m) {
        case Mech::Flick:
        case Mech::Musty:
        case Mech::Breezi:
        case Mech::AirDribble: return "Parcours complet (ramasse la balle)";
        case Mech::AirRoll: return "Parcours complet (depuis le sol)";
        case Mech::Aerial: return "Parcours complet (centre en travers)";
        default: return "Parcours complet";
        }
    }
    switch (m) {
    case Mech::Ceiling: return "Départ au plafond";
    case Mech::FlipReset: return "Départ sous la balle";
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi:
    case Mech::AirDribble: return "Balle déjà sur le toit";
    case Mech::DoubleTap: return "Balle envoyée sur le mur";
    case Mech::AirRoll: return "Départ en l'air";
    case Mech::Aerial: return "Balle lancée devant toi";
    case Mech::Pinch: return "Balle qui roule vers le mur";
    case Mech::PowerShot: return "Balle devant toi";
    default: return "Exercice court";
    }
}

const char* modeId(DrillMode mode) { return mode == DrillMode::Full ? "parcours" : "court"; }

bool modeFromId(const std::string& id, DrillMode& out) {
    if (id == "parcours" || id == "complet" || id == "full") {
        out = DrillMode::Full;
        return true;
    }
    if (id == "court" || id == "short") {
        out = DrillMode::Short;
        return true;
    }
    return false;
}

// ---- Level criteria ---------------------------------------------------------------------------------------------
float ceilingSpeedNeeded(float level) { return 1200.f + 1000.f * clamp01(level); }

float flickPowerNeeded(Mech m, float level) {
    const float L = clamp01(level);
    if (m == Mech::Breezi) {
        // The tornado alone below kBzTornadoLevel, then 1500 at niveau 3 up to 2400 at niveau 10.
        const float n = static_cast<float>(std::max(playerLevel(L), kBzTornadoLevel));
        return 1500.f + 900.f * (n - static_cast<float>(kBzTornadoLevel)) / (10.f - static_cast<float>(kBzTornadoLevel));
    }
    return 1500.f + 900.f * L;
}

float flickDribbleSpeed(float level) { return 900.f + 400.f * clamp01(level); }

float airDribbleSeconds(float level) {
    const int n = playerLevel(level);
    if (n <= 3) return 0.f;  // niveau 1: pop and one air touch; 2-3: two air touches (the detector's own success)
    if (n <= 5) return 0.8f;
    return 0.8f + 2.2f * static_cast<float>(n - 5) / 5.f;
}

float pinchSpeedNeeded(float level) { return 3000.f + 1500.f * clamp01(level); }

float airRollSpeedNeeded(float level) {
    const int n = playerLevel(level);
    if (n < kArTargetLevel) return 1000.f;
    return 1200.f + 1000.f * static_cast<float>(n - kArTargetLevel) / static_cast<float>(10 - kArTargetLevel);
}

float aerialSpeedNeeded(float level) { return 1200.f + 1300.f * clamp01(level); }
float powerShotSpeedNeeded(float level) { return 2000.f + 1200.f * clamp01(level); }
float doubleTapSpeedNeeded(float level) { return 800.f + 1000.f * clamp01(level); }

float drillReadyTime(float level, float setting) {
    const float s = std::max(0.f, setting);
    return playerLevel(level) <= 3 ? std::max(1.5f, s) : s;
}

// ---- Setups -----------------------------------------------------------------------------------------------------
namespace {

constexpr float kCeilCarZ = arena::kCeilingZ - arena::kCarRestZ;  // 2027: car origin with the wheels on the ceiling

// Position of a car leaving the ceiling at `t` seconds (no input): 0.1 s at half gravity while the wheels let go,
// then free fall. Same model as the auditors' probe (the flip windows of the spec were measured with it).
Vec3 ceilingFall(const Vec3& pos, const Vec3& vel, float t) {
    Vec3 p = pos, v = vel;
    const int n = static_cast<int>(std::lround(t * kTickRate));
    for (int i = 1; i <= n; ++i) {
        const float ti = static_cast<float>(i) * kTickDt;
        v.z += (ti <= 0.1f ? 0.5f : 1.f) * arena::kGravityZ * kTickDt;
        p += v * kTickDt;
    }
    return p;
}

// Ceiling, "Départ au plafond": the car waits on the flat ceiling just past the wall curve, upside down, nose toward
// mid-field and the attacked goal. The ball is lobbed so that it reaches its apex right in front of the nose when
// the car (falling without input) is at the same height: flipping at that moment hits it, doing nothing never does.
void ceilingShort(DrillSetup& d, int n, Rng& rng) {
    const float side = randSign(rng);
    const int g = n == 1 ? 0 : (n <= 3 ? 1 : (n <= 6 ? 2 : (n <= 9 ? 3 : 4)));
    constexpr float kTm[] = {1.00f, 1.10f, 1.20f, 1.35f, 1.45f};   // ideal flip time after GO
    constexpr float kLead[] = {260.f, 280.f, 300.f, 320.f, 340.f};  // ball centre ahead of the hitbox centre
    constexpr float kSpeed[] = {500.f, 500.f, 600.f, 700.f, 800.f};
    constexpr float kLateral[] = {0.f, 0.f, 50.f, 80.f, 100.f};
    const float tm = kTm[g];
    const float yaw = std::atan2(0.866f, -0.5f * side) * kRadToDeg;
    d.car.pos = {side * 3350.f, rng.range(-1200.f, -400.f), kCeilCarZ};
    d.car.rot = rotDeg(0.f, yaw, 180.f);
    const Basis b = basisFromRot(d.car.rot);
    const Vec3 f = normalize(flat(b.fwd));
    d.car.vel = f * kSpeed[g];
    d.car.boost = 100.f;
    const Vec3 p = ceilingFall(d.car.pos, d.car.vel, tm);
    const Vec3 hc = p + b.fwd * 13.88f + b.up * 20.75f;  // Octane hitbox centre (the drill is tuned for it)
    const Vec3 right{f.y, -f.x, 0.f};
    Vec3 meet = hc + f * kLead[g] + right * (kLateral[g] * randSign(rng));
    meet.z = hc.z;
    const float vz0 = -arena::kGravityZ * tm;  // apex at tm
    d.ball.pos = {meet.x, meet.y, meet.z - vz0 * tm - 0.5f * arena::kGravityZ * tm * tm};
    d.ball.vel = {0.f, 0.f, vz0};
    d.armCeiling = true;
    d.side = side;
    d.gameSpeed = n == 1 ? 0.75f : (n == 2 ? 0.85f : 1.f);
    if (n <= 4) {
        d.guideCount = static_cast<int>(d.guide.size());
        for (int i = 0; i < d.guideCount; ++i) {
            const float t = tm * static_cast<float>(i) / static_cast<float>(d.guideCount - 1);
            d.guide[static_cast<size_t>(i)] = d.ball.pos + d.ball.vel * t + Vec3{0.f, 0.f, 0.5f * arena::kGravityZ * t * t};
        }
        d.hasMeet = true;
        d.meetPoint = meet;
    }
    d.label = "Départ au plafond";
}

// Ceiling, "Parcours complet": from the floor, 1200 uu from the side wall, heading 20 deg off the wall normal toward
// the attacked goal. The ball hangs in the air and is let go at the step of the level (niveaux 1-3: when the car
// leaves the ceiling, so it waits however long the climb takes, and it is relaunched from the car's real fall).
void ceilingFull(DrillSetup& d, int n, Rng& rng) {
    const float side = randSign(rng);
    const float yc = rng.range(-2000.f, -1200.f);
    groundCar(d, {side * 2900.f, yc, arena::kCarRestZ}, std::atan2(0.34f, side * 0.94f) * kRadToDeg, n <= 6 ? 600.f : 0.f);
    d.car.boost = 100.f;
    d.hangBall = true;
    if (n <= 3) {
        d.ball.pos = {side * 2300.f, yc + 1500.f, 1450.f};
        d.releaseStep = "ceil.drop";
        d.releaseVel = {-side * 600.f, 0.f, 250.f};
        d.releaseApex = 0.8f;  // relaunched in front of the car when it leaves the ceiling (BallMechModule::fullTick)
    } else if (n <= 6) {
        d.ball.pos = {side * 2500.f, yc + 1500.f, 1500.f};
        d.releaseStep = "ceil.ceiling";
        d.releaseVel = {-side * 600.f, 150.f, 350.f};
    } else {
        d.ball.pos = {side * 2900.f, yc + 1200.f, 1000.f};
        d.releaseStep = "ceil.wall";
        d.releaseVel = {-side * 450.f, 200.f, 1050.f};
    }
    d.ball.vel = {};
    d.side = side;
    d.label = "Parcours complet";
}

// Flip reset, "Départ sous la balle": the car flies toward the ball on a straight relative line (both fall the same),
// but turned around its nose so a passive contact is a side or roof touch: the player rolls the wheels to the ball.
void flipResetShort(DrillSetup& d, int n, float L, float r, Rng& rng) {
    const float vRel = n == 1 ? 300.f : (n <= 4 ? 350.f : (n <= 7 ? 450.f : 550.f));
    const float tContact = n == 1 ? 1.6f : (n <= 4 ? 1.4f : (n <= 7 ? 1.2f : 1.0f));
    // Ball popped up at 250 uu/s from high enough to be met at about 950 to 1150 uu (the spec's 1000-1150 start
    // was met at 570-720 at niveau 1 after the longer approach: too low to convert the reset).
    const float vz0 = 250.f;
    const float z0 = 1000.f - vz0 * tContact - 0.5f * arena::kGravityZ * tContact * tContact + rng.range(0.f, 150.f);
    const Vec3 ball{rng.range(-1.f, 1.f) * (200.f + 600.f * L), rng.range(1300.f, 2000.f), z0};
    const float e = rng.range(35.f, 55.f) * kDegToRad;
    const float a = rng.range(-1.f, 1.f) * (10.f + 30.f * L) * kDegToRad;
    const Vec3 dir{std::sin(a) * std::cos(e), std::cos(a) * std::cos(e), std::sin(e)};
    const float startDist = 112.f + vRel * tContact;
    const float contactDist = r + 20.f;
    d.ball.pos = ball;
    d.ball.vel = {rng.range(-1.f, 1.f) * (50.f + 150.f * L), rng.range(0.f, 100.f), vz0};
    d.car.pos = ball - dir * startDist;
    const Vec3 carVel = d.ball.vel + dir * ((startDist - contactDist) / tContact);
    // Underside toward the ball (upside down, nose pitched toward the floor), then rolled around the nose.
    const float yaw = std::atan2(dir.y, dir.x) * kRadToDeg + (n >= 8 ? randSign(rng) * 30.f : 0.f);
    const float pitch = -std::atan2(length2D(dir), dir.z) * kRadToDeg;
    const float rollOff = n <= 2 ? 90.f : (n <= 4 ? 120.f : 180.f);
    const float rollSign = randSign(rng);
    const float roll = 180.f + rollSign * rollOff;
    d.car.rot = rotDeg(pitch, yaw, roll);
    const Basis b = basisFromRot(d.car.rot);
    d.car.vel = carVel - b.up * arena::kJumpImpulse;  // the scripted double jump adds it back (rolled basis)
    d.car.boost = 100.f - 60.f * L;
    d.burnFlip = true;
    if (n < kFrConvertLevel) {
        // Which air roll brings the wheels to the ball first: compare a small roll either way.
        auto align = [&](float rollDeg) { return dot(-basisFromRot(rotDeg(pitch, yaw, rollDeg)).up, dir); };
        d.airRollHint = align(roll + 10.f) >= align(roll - 10.f) ? 1 : -1;
    }
    d.gameSpeed = n == 1 ? 0.75f : (n == 2 ? 0.85f : 1.f);
    d.label = n < kFrConvertLevel ? "Obtenir le reset"
                                  : (n < kFrTargetLevel ? "Reset puis double touche" : "Reset puis tir cadré");
}

// Flip reset, "Parcours complet": from the floor, the ball waits high in front (niveaux 1-3), is popped (4-6) or
// comes off the side wall (7-10).
void flipResetFull(DrillSetup& d, int n, Rng& rng) {
    const float x0 = rng.range(-1.f, 1.f) * 600.f;
    groundCar(d, {x0, -1800.f, arena::kCarRestZ}, 90.f, n <= 3 ? 0.f : 800.f);
    d.car.boost = 100.f;
    if (n <= 3) {
        d.hangBall = true;
        d.ball.pos = {x0, 200.f, 1250.f};
        d.ball.vel = {};
        d.releaseDist = 700.f;
        d.releaseVel = {0.f, 0.f, 200.f};
    } else if (n <= 6) {
        d.ball.pos = {x0, 0.f, 93.15f};
        d.ball.vel = {0.f, 100.f, 1400.f};
    } else {
        const float side = randSign(rng);
        d.ball.pos = {side * 3990.f, 300.f, 500.f};
        d.ball.vel = {-side * 500.f, 150.f, 1250.f};
        d.side = side;
    }
    d.label = "Parcours complet";
}

void pickupStart(DrillSetup& d, const Vec3& carPos, int pickupNiveau, float r, Rng& rng, bool chase = false) {
    const pickup::Setup su =
        chase ? pickup::makeChaseSetup(carPos, 90.f, pickupNiveau, rng, r) : pickup::makeSetup(carPos, 90.f, pickupNiveau, rng, r);
    d.car = su.car;
    d.ball = su.ball;
    d.pickup = true;
    d.pickupRolling = su.rolling;
    d.pickupChase = su.chase;
}

} // namespace

DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float r, Rng& rng,
                          const std::string& flickVariant) {
    SetupOptions o;
    o.flickVariant = flickVariant;
    return makeDrillSetup(m, level, team, car, r, rng, o);
}

DrillSetup makeDrillSetup(Mech m, float level, int team, const CarState& car, float r, Rng& rng,
                          const SetupOptions& opt) {
    const float L = clamp01(level);
    const int n = playerLevel(L);
    const bool full = opt.mode == DrillMode::Full;
    const int pickupNiveau = std::max(1, std::min(10, opt.pickupNiveau));
    DrillSetup d;
    d.mode = opt.mode;
    d.car.boost = 100.f;
    switch (m) {
    case Mech::FlipReset:
        if (full) flipResetFull(d, n, rng);
        else flipResetShort(d, n, L, r, rng);
        break;
    case Mech::Ceiling:
        if (full) ceilingFull(d, n, rng);
        else ceilingShort(d, n, rng);
        break;
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi: {
        if (full) {
            // Pickup at speed from 5500 uu of the goal line: the car drives after the ball rolling ahead (like a match).
            pickupStart(d, {rng.range(-1.f, 1.f) * 300.f, arena::kBackWallY - 5500.f, arena::kCarRestZ}, pickupNiveau, r,
                        rng, true);
            d.label = "Ramassage en course";
        } else {
            // Dribble start, like a match: from midfield toward the goal centre, the ball riding on the roof at the car's
            // speed (900 uu/s at niveau 1, 1300 at niveau 10: the throttle alone holds it, never supersonic).
            const float speed = flickDribbleSpeed(L);
            const float dist = kFlickDistMin + (kFlickDistMax - kFlickDistMin) * L + rng.range(0.f, kFlickDistSpread);
            const Vec3 pos{rng.range(-1.f, 1.f) * (200.f + 800.f * L), arena::kBackWallY - dist, arena::kCarRestZ};
            const float jitter = n <= 4 ? 0.f : rng.range(-1.f, 1.f) * (5.f + 10.f * L);
            groundCar(d, pos, std::atan2(arena::kBackWallY - pos.y, -pos.x) * kRadToDeg + jitter, speed);
            const Basis b = basisFromRot(d.car.rot);
            d.ball.pos = roofBallPos(pos, b, car, r);
            d.ball.vel = d.car.vel;
            d.label = "Balle sur le toit, en dribble";
        }
        d.car.boost = 100.f - 50.f * L;
        d.flickTarget = m == Mech::Musty ? "musty" : (m == Mech::Breezi ? "breezi" : opt.flickVariant);
        break;
    }
    case Mech::DoubleTap: {
        if (!full && n < kDtFullLevel) {
            // Read mode: the drill sends the ball to the backboard; the car starts right behind it, already rolling.
            const bool close = n <= 3;
            const float x0 = n == 1 ? 0.f : rng.range(-1.f, 1.f) * (close ? 400.f : 800.f);
            const Vec3 start{x0, close ? 3300.f : 3000.f, 300.f};
            const Vec3 target{close ? x0 : x0 * 0.5f + rng.range(-200.f, 200.f), arena::kBackWallY - r - 5.f,
                              close ? 1000.f : rng.range(1000.f, 1300.f)};
            const float speed = close ? 1300.f : 1500.f;
            const float t = std::max(0.5f, distance(start, target) / speed);
            d.ball.pos = start;
            d.ball.vel = (target - start) / t + Vec3{0.f, 0.f, -0.5f * arena::kGravityZ * t};
            groundCar(d, {x0, close ? 2600.f : 2200.f, arena::kCarRestZ}, 90.f, close ? 1000.f : 800.f);
            d.doubleTapRead = true;
            d.label = "Lecture du rebond";
        } else {
            const float x0 = rng.range(-1.f, 1.f) * 800.f;
            d.ball.pos = {x0, 3000.f, 250.f};
            d.ball.vel = full ? Vec3{0.f, 600.f, 1100.f} : Vec3{rng.range(-100.f, 100.f), 500.f + rng.range(0.f, 200.f), 1100.f};
            groundCar(d, {x0, 1600.f, arena::kCarRestZ}, 90.f, 900.f);
            d.label = full ? "Parcours complet" : "Double tap complet";
        }
        d.car.boost = full ? 100.f : 100.f - 50.f * L;
        break;
    }
    case Mech::AirDribble: {
        if (full) {
            pickupStart(d, {rng.range(-1.f, 1.f) * 1500.f, rng.range(-2500.f, -1500.f), arena::kCarRestZ}, pickupNiveau, r, rng);
            d.label = d.pickupRolling ? "Ramassage, balle qui arrive" : "Ramassage, balle posée";
            d.car.boost = 100.f;
            break;
        }
        const bool wall = n >= kAdWallLevel && rng.chance(0.5f);
        if (!wall) {
            // Niveau 1: car stopped, the ball balanced on the roof waits for the pop.
            const float speed = n == 1 ? 0.f : (n <= 4 ? 350.f : clampf(500.f + 300.f * (L - 0.45f) / 0.55f, 500.f, 800.f));
            const Vec3 pos{rng.range(-1.f, 1.f) * 1500.f, rng.range(-2500.f, -1000.f), arena::kCarRestZ};
            groundCar(d, pos, 90.f + (n == 1 ? 0.f : rng.range(-1.f, 1.f) * 10.f), speed);
            const Basis b = basisFromRot(d.car.rot);
            d.ball.pos = roofBallPos(pos, b, car, r);
            d.ball.vel = d.car.vel;
            d.car.boost = 100.f - 50.f * L;
            d.label = speed > 0.f ? "Depuis le sol, en roulant" : "Depuis le sol, voiture arrêtée";
        } else {
            // The ball already rides up the side wall, the car on the wall right below it, wheels on the wall.
            const float side = randSign(rng);
            const float y = rng.range(-2200.f, -1400.f);
            // Pressed on the wall at 8 uu/s, under the bounce threshold (10): the ball stays on the wall (at 20 it bounces
            // off and drifts 35 uu away in the core physics).
            d.ball.pos = {side * 4002.f, y, 600.f};
            d.ball.vel = {side * 8.f, 300.f, 950.f};
            d.car.pos = {side * 4079.f, y, 350.f};
            d.car.rot = rotDeg(72.6f, 90.f, side * 90.f);
            d.car.vel = basisFromRot(d.car.rot).fwd * 1000.f;
            d.car.boost = 100.f;
            d.side = side;
            d.label = "Depuis le mur";
        }
        break;
    }
    case Mech::AirRoll: {
        if (full) {
            // From the ground: the ball is popped in front, the car rolls toward it.
            d.ball.pos = {0.f, 3000.f, 93.15f};
            d.ball.vel = {0.f, -100.f, 1250.f};
            groundCar(d, {0.f, 1400.f, arena::kCarRestZ}, 90.f, 600.f);
            d.label = "Parcours complet";
            break;
        }
        const float side = randSign(rng);
        d.car.pos = {rng.range(-400.f, 400.f), 1600.f, 500.f};
        // Niveaux 1-3: the nose already points at the ball. From niveau 4 the ball is 250 uu to the side: with the car
        // rolled, the stick turns the nose sideways, which is what the air roll is for.
        const float offset = n <= 3 ? rng.range(-200.f, 200.f) : randSign(rng) * 250.f;
        d.ball.pos = {d.car.pos.x + offset, 2700.f, 1150.f};
        d.ball.vel = {0.f, 0.f, 300.f};
        const Vec3 to = d.ball.pos - d.car.pos;
        const float yaw = n <= 3 ? std::atan2(to.y, to.x) * kRadToDeg : 90.f;
        d.car.rot = rotDeg(15.f, yaw, side * (n <= 5 ? 90.f : 135.f));
        d.car.vel = {0.f, 700.f, 250.f};
        d.label = side > 0.f ? "Voiture penchée à droite" : "Voiture penchée à gauche";
        break;
    }
    case Mech::Aerial: {
        if (!full && n <= 6) {
            // The ball is popped straight up in front, the goal behind it.
            const bool low = n <= 3;
            const float x0 = n == 1 ? 0.f : rng.range(-1.f, 1.f) * (low ? 400.f : 800.f);
            d.ball.pos = {x0, 3000.f, 93.15f};
            d.ball.vel = {0.f, -150.f, low ? 1100.f : 1300.f};
            groundCar(d, {x0, 1300.f, arena::kCarRestZ}, 90.f, n == 1 ? 0.f : 500.f);
            d.label = "Balle lancée devant toi";
            break;
        }
        const float side = randSign(rng);
        const Vec3 start{side * 3000.f, 3700.f, 150.f};
        const Vec3 target{side * rng.range(0.f, 300.f), 3500.f + rng.range(-300.f, 200.f), 850.f + rng.range(0.f, 350.f) + 200.f * L};
        // A centre from the side: 2.4 s in the full sequence, 2.1 s down to 1.72 s at niveaux 7-10 (the lateral speed
        // stays under about 1750 uu/s).
        const float t = full ? 2.4f : std::max(1.72f, 2.1f - 0.4f * (L - 0.65f) / 0.35f);
        d.ball.pos = start;
        d.ball.vel = (target - start) / t + Vec3{0.f, 0.f, -0.5f * arena::kGravityZ * t};
        d.target = target;
        groundCar(d, {full ? 0.f : rng.range(-500.f, 500.f), 1300.f, arena::kCarRestZ}, 90.f, full ? 0.f : 300.f);
        d.label = side > 0.f ? "Centre depuis la droite" : "Centre depuis la gauche";
        break;
    }
    case Mech::Pinch: {
        const float side = randSign(rng);
        d.side = side;
        if (full) {
            // The ball rolls toward the side wall: read where it meets the wall and pick the moment to go.
            const float y0 = rng.range(-800.f, 0.f);
            d.ball.pos = {side * 1200.f, y0, r + 2.f};
            d.ball.vel = {side * 1200.f, 300.f, 0.f};
            d.ball.angVel = pickup::rollingSpin(d.ball.vel, r);
            groundCar(d, {0.f, y0 - 1800.f, arena::kCarRestZ}, 90.f, 0.f);
            d.label = "Parcours complet";
            break;
        }
        const float y = rng.range(-600.f, 600.f);
        d.ball.pos = {side * 3650.f, y, r + 2.f};
        if (n <= 3) {
            // Slow roll toward the wall, the car already rolling fast, aimed where the ball meets the wall.
            d.ball.vel = {side * 350.f, 400.f, 0.f};
            const float tWall = (arena::kSideWallX - r - 2.f - 3650.f) / 350.f;
            const Vec3 contact{side * (arena::kSideWallX - r - 2.f), y + 400.f * tWall, r + 2.f};
            const Vec3 carPos{side * 2500.f, y - 1200.f, arena::kCarRestZ};
            const Vec3 aim = contact - carPos;
            groundCar(d, carPos, std::atan2(aim.y, aim.x) * kRadToDeg, 1200.f);
        } else {
            d.ball.vel = {side * (450.f + 300.f * L), 500.f + 300.f * L, 0.f};
            const Vec3 carPos{side * 2500.f, y - 1100.f, arena::kCarRestZ};
            const Vec3 aim = d.ball.pos + d.ball.vel * 0.8f - carPos;
            groundCar(d, carPos, std::atan2(aim.y, aim.x) * kRadToDeg, 900.f);
        }
        d.ball.angVel = pickup::rollingSpin(d.ball.vel, r);
        d.label = "Balle vers le mur";
        break;
    }
    case Mech::PowerShot: {
        if (full) {
            // The ball rests off the car's line: line up behind it first.
            const float side = randSign(rng);
            d.ball.pos = {side * 1200.f, 2000.f, r + 2.f};
            groundCar(d, {0.f, -1500.f, arena::kCarRestZ}, 90.f, 0.f);
            d.side = side;
            d.car.boost = 100.f;
            d.label = "Parcours complet";
            break;
        }
        const float x0 = rng.range(-1.f, 1.f) * 800.f;
        if (n == 1) {
            // Resting ball straight ahead on the line to the goal centre, car stopped.
            d.ball.pos = {0.f, 2500.f, r + 2.f};
            groundCar(d, {0.f, 0.f, arena::kCarRestZ}, 90.f, 0.f);
            d.label = "Balle posée devant toi";
        } else if (n < kPsSideLevel) {
            d.ball.pos = {x0, 3000.f, r + 2.f};
            d.ball.vel = {rng.range(-100.f, 100.f), -(300.f + 400.f * static_cast<float>(n - 2) / 3.f), 0.f};
            groundCar(d, {x0 + rng.range(-200.f, 200.f), 200.f, arena::kCarRestZ}, 90.f, 300.f);
            d.label = "Balle de face";
        } else {
            const float side = randSign(rng);
            d.ball.pos = {x0 + side * 1500.f, 2600.f, r + 2.f};
            d.ball.vel = {-side * (500.f + 400.f * L), -150.f, 0.f};
            groundCar(d, {x0, 900.f, arena::kCarRestZ}, 90.f, 400.f);
            d.label = "Balle de côté (glissade)";
        }
        d.ball.angVel = pickup::rollingSpin(d.ball.vel, r);
        d.car.boost = std::max(60.f, 100.f - 67.f * L);
        break;
    }
    default: break;
    }
    mirrorForTeam(d, team);
    return d;
}

bool gradeMatchesDrill(Mech drill, const MechGrade& g) { return mechInfo(drill).family == g.family; }

DrillVerdict judgeDrill(Mech drill, const MechGrade& g, float level, int minScore, float pinchSpeed) {
    (void)pinchSpeed;  // the pinch drill has its own thresholds; rlt_bm_pinch_speed stays for free play
    const float L = clamp01(level);
    const int n = playerLevel(L);
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
    // A level that asks less than the detector computes the verdict itself (fix 6).
    auto override = [&v](bool ok, const char* code) {
        v.extraErrors.clear();
        v.success = ok;
        if (!ok && code) v.extraErrors.push_back(code);
    };
    const bool onTarget = !g.projection.valid || g.projection.onTarget;
    // Flick family: judged like a shot in a match, the ball's own speed (it already rides at the dribble speed), toward
    // the goal at niveaux 1 and 2, on target from niveau 3.
    auto flickShot = [&]() {
        need(g.value("ball_speed_after") >= flickPowerNeeded(drill, L), "DRILL_NEED_POWER");
        if (n >= kFlickTargetLevel) need(onTarget, "DRILL_NEED_TARGET");
        else need(!g.projection.valid || g.projection.angleErrDeg <= kFlickDirectionDeg, "DRILL_NEED_DIRECTION");
    };
    switch (drill) {
    case Mech::FlipReset: {
        const bool obtained = g.variant != "no_reset";
        if (n < kFrConvertLevel) {
            v.success = obtained;
            v.extraErrors.clear();
        } else if (obtained && !g.success) {
            v.extraErrors.push_back("DRILL_NEED_CONVERT");
        }
        if (n >= kFrTargetLevel) need(onTarget, "DRILL_NEED_TARGET");
        break;
    }
    case Mech::Ceiling:
        need(g.value("ball_speed_after") >= ceilingSpeedNeeded(L), "DRILL_NEED_POWER");
        if (n >= kTargetLevel) need(onTarget, "DRILL_NEED_TARGET");
        break;
    case Mech::Breezi:
        if (n < kBzTornadoLevel) {
            // The tornado alone: a graded breezi (classified with its spin) or the module's own tornado check. A failed
            // flick grade carries the requested variant without being one (no roll measure): not enough.
            override((g.variant == "breezi" && g.has("roll_deg")) || g.variant == "tornado", "FL_BREEZI_NO_SPIN");
            break;
        }
        flickShot();
        break;
    case Mech::Flick:
    case Mech::Musty: flickShot(); break;
    case Mech::DoubleTap:
        // First step: touching the rebound counts even when the ball does not go on target.
        if (n < kDtTargetLevel && g.hasShot && !g.success) {
            v.success = scoreOk;
            if (!scoreOk) v.extraErrors.push_back("DRILL_NEED_SCORE");
        } else if (g.hasShot && !g.success) {
            v.extraErrors.push_back("DRILL_NEED_TARGET");  // the rebound was touched, not on target
        }
        need(g.value("ball_speed_after") >= doubleTapSpeedNeeded(L), "DRILL_NEED_POWER");
        // From niveau 7 the player makes the first shot himself: high on the backboard.
        if (n >= kDtFullLevel && g.variant != "read") need(g.value("backboard_height") >= 750.f, "DT_LOW_BACKBOARD");
        break;
    case Mech::AirDribble:
        if (n == 1) {
            // Pop plus one air touch (the weak grade): the ball must have left the roof, the score does not count.
            override(g.value("air_touches") >= 1.f && g.value("height_gain") >= 250.f, "AD_LOW_POP");
            break;
        }
        need(g.value("duration") >= airDribbleSeconds(L), "DRILL_NEED_DURATION");
        if (n >= kAdTargetLevel && g.has("finished_on_target"))
            need(g.value("finished_on_target") >= 0.5f, "DRILL_NEED_TARGET");
        break;
    case Mech::AirRoll: {
        const bool rolled = g.variant == "air_roll_shot";
        const bool fast = g.value("ball_speed_after") >= airRollSpeedNeeded(L);
        if (n < kArTargetLevel) {
            // Niveau 1: the air roll pattern and a firm hit, no target and no score (fix 6).
            override(rolled && fast, rolled ? "DRILL_NEED_POWER" : "AE_NO_AIR_ROLL");
            break;
        }
        if (!rolled) {
            v.success = false;
            v.extraErrors.push_back("AE_NO_AIR_ROLL");
        }
        need(fast, "DRILL_NEED_POWER");
        break;
    }
    case Mech::Aerial:
        need(g.value("ball_speed_after") >= aerialSpeedNeeded(L), "DRILL_NEED_POWER");
        break;
    case Mech::Pinch: {
        // The drill bar starts under the detector's free play threshold (fix 6): a real pinch (ball within 120 uu of
        // the surface, car pushing into it) at the level's exit speed, on target from niveau 6. The score mostly
        // re-measures exit speed and accuracy, which the level already sets: not applied.
        const bool real = g.value("surface_gap", 1e9f) <= 120.f && g.value("push_speed") >= 300.f;
        const bool fast = g.value("exit_speed") >= pinchSpeedNeeded(L);
        override(real && fast, real ? "DRILL_NEED_POWER" : "PI_NOT_PINCHED");
        if (n >= kTargetLevel) need(onTarget, "DRILL_NEED_TARGET");
        break;
    }
    case Mech::PowerShot:
        need(g.value("exit_speed") >= powerShotSpeedNeeded(L), "DRILL_NEED_POWER");
        break;
    default: break;
    }
    return v;
}

std::string drillAction(Mech m, float level, const std::string& flickTarget) {
    const int n = playerLevel(level);
    switch (m) {
    case Mech::FlipReset:
        if (n < kFrConvertLevel) return "1. Air roll : roues vers la balle  2. Pose les 4 roues dessus";
        if (n < kFrTargetLevel) return "1. Pose les 4 roues sur la balle  2. Flippe dedans";
        return "1. Reset  2. Nez vers le but  3. Flippe dans la balle";
    case Mech::Ceiling:
        return n < kTargetLevel ? "1. Laisse-toi tomber, sans sauter  2. Flippe dans la balle"
                                : "1. Tombe sans sauter  2. Flippe dans la balle vers le but";
    case Mech::Flick: {
        // The car already drives with the ball on the roof: the throttle keeps the speed.
        const std::string first = "1. Garde ta vitesse  ";
        std::string a;
        if (flickTarget == "front") a = first + "2. Saute  3. Flip avant";
        else if (flickTarget == "45") a = "1. Saute  2. Huitième de tour  3. Flip en diagonale";
        else if (flickTarget == "90") a = "1. Saute  2. Quart de tour  3. Flip dans la balle";
        else if (flickTarget == "backflip") a = first + "2. Saute  3. Flip arrière";
        else if (flickTarget == "side") a = first + "2. Saute  3. Flip sur le côté";
        else if (flickTarget == "reverse") a = "1. Saute  2. Retourne la voiture  3. Flip arrière";
        else a = first + "2. Saute  3. Flippe dans la balle";
        return a + ", vers le but";
    }
    case Mech::Musty: return "1. Saute  2. Stick vers l'avant : nez en bas  3. Flip arrière";
    case Mech::Breezi:
        return n < kBzTornadoLevel ? "1. Saut maintenu  2. Air roll + stick opposé : tornade"
                                   : "1. Saut maintenu  2. Tornade  3. Flip arrière quand le nez remonte";
    case Mech::DoubleTap:
        if (n < kDtTargetLevel) return "1. Suis la balle vers le mur  2. Touche le rebond";
        if (n < kDtFullLevel) return "1. Suis la balle vers le mur  2. Reprends le rebond vers le but";
        return "1. Tire haut sur le mur  2. Suis la balle  3. Reprends le rebond";
    case Mech::AirDribble:
        if (n == 1) return "1. Saute en levant un peu le nez  2. Touche-la une fois en l'air";
        return n < kAdTargetLevel ? "1. Saute avec la balle  2. Petites touches avec le nez"
                                  : "1. Saute avec la balle  2. Petites touches  3. Finis vers le but";
    case Mech::AirRoll:
        return n < kArTargetLevel ? "1. Tiens l'air roll, un seul sens  2. Frappe la balle au centre"
                                  : "1. Tiens l'air roll, un seul sens  2. Frappe au centre, vers le but";
    case Mech::Aerial: return "1. Saute, nez vers la balle, boost  2. Garde le boost jusqu'au contact";
    case Mech::Pinch:
        return n < kTargetLevel ? "Frappe la balle quand elle touche le mur"
                                : "Frappe la balle contre le mur, vers le but";
    case Mech::PowerShot:
        return n < kPsSideLevel ? "1. Boost tout droit  2. Flippe juste avant la balle"
                                : "1. Glisse pour te tourner  2. Flippe dans la balle";
    default: return "";
    }
}

std::string drillLevelNote(Mech m, float level, DrillMode mode) {
    const int n = playerLevel(level);
    if (mode == DrillMode::Full && m == Mech::Ceiling)
        return n <= 3 ? "quand tu quittes le plafond, la balle est relancée juste devant toi" : "";
    if (m == Mech::FlipReset && n < kFrConvertLevel) return "poser les 4 roues sur la balle suffit";
    if (m == Mech::DoubleTap && n < kDtTargetLevel) return "toucher le rebond suffit, même si la balle ne va pas au but";
    if (m == Mech::AirDribble && n == 1) return "le saut avec la balle puis une seule touche en l'air suffisent";
    if (m == Mech::Breezi && n < kBzTornadoLevel) return "la tornade seule suffit, sans le flip final";
    if (m == Mech::AirRoll && n < kArTargetLevel) return "le tir n'a pas besoin d'être cadré";
    if (m == Mech::Ceiling && n <= 2) return "le jeu est ralenti";
    if ((m == Mech::Flick || m == Mech::Musty) && n < kFlickTargetLevel)
        return "le tir doit partir vers le but, pas forcément cadré";
    return "";
}

std::string drillProgression(Mech m) {
    switch (m) {
    case Mech::FlipReset:
        return "Niveaux 1 à 4 : poser les 4 roues sur la balle suffit, la voiture part tournée de côté. Niveaux 5 à 7 : "
               "flippe ensuite dans la balle. Niveaux 8 à 10 : le tir doit être cadré. La voiture arrive plus vite et "
               "moins bien tournée quand le niveau monte.";
    case Mech::Ceiling:
        return "La frappe doit être plus forte à chaque niveau. À partir du niveau 6, elle doit être cadrée. En parcours "
               "complet, aux niveaux 1 à 3, la balle est relancée juste devant toi quand tu quittes le plafond. Ensuite elle "
               "part plus tôt et c'est à toi de te placer. Au départ au plafond, "
               "le jeu est ralenti aux niveaux 1 et 2.";
    case Mech::Flick:
    case Mech::Musty:
        return "Comme en match : la balle est sur ton toit et tu roules déjà vers le but depuis le milieu du terrain, un "
               "peu plus vite à chaque niveau. Aux niveaux 1 et 2, le tir doit partir vers le but ; à partir du niveau "
               "3, il doit être cadré. La balle doit partir plus vite à chaque niveau, et le flick partir avant "
               "d'arriver devant le but.";
    case Mech::Breezi:
        return "Niveaux 1 et 2 : la tornade seule suffit. À partir du niveau 3, il faut le flip arrière, cadré, de plus "
               "en plus fort. Tu roules déjà vers le but avec la balle sur le toit, un peu plus vite à chaque niveau.";
    case Mech::DoubleTap:
        return "Niveaux 1 et 2 : toucher le rebond suffit, même si la balle ne va pas au but. Niveaux 3 à 6 : le rebond "
               "repris doit être cadré. Jusqu'au niveau 6 l'exercice envoie la balle sur le mur pour toi, ensuite tu "
               "fais le premier tir.";
    case Mech::AirDribble:
        return "Niveau 1 : saute avec la balle et touche-la une fois en l'air. Niveaux 2 et 3 : deux touches en l'air. "
               "Ensuite, garde-la en l'air de plus en plus longtemps. À partir du niveau 6, certains essais partent du "
               "mur. À partir du niveau 7, finis vers le but.";
    case Mech::AirRoll:
        return "Niveau 1 : le tir n'a pas besoin d'être cadré. Ensuite, il doit être cadré et plus fort à chaque niveau. "
               "À partir du niveau 4, la balle est décalée sur le côté, et la voiture part plus penchée au niveau 6.";
    case Mech::Aerial:
        return "Jusqu'au niveau 6, la balle est lancée droit en l'air devant toi, plus haut à partir du niveau 4. "
               "Ensuite elle arrive de côté, de plus en plus vite. Le tir doit être plus fort à chaque niveau.";
    case Mech::Pinch:
        return "Le pinch doit être plus rapide à chaque niveau. Aux niveaux 1 à 3, la balle roule doucement et tu pars "
               "lancé. À partir du niveau 6, il doit être cadré.";
    case Mech::PowerShot:
        return "Niveau 1 : balle posée. Niveaux 2 à 5 : elle roule vers toi. À partir du niveau 6, elle arrive de côté. "
               "La frappe doit être plus forte à chaque niveau.";
    default: return "";
    }
}

std::string drillCriteria(Mech m, float level, int successScore, float pinchSpeed, SpeedUnit unit) {
    (void)pinchSpeed;
    const float L = clamp01(level);
    const int n = playerLevel(L);
    const std::string score = "score " + std::to_string(drillMinScore(successScore, L)) + "/100 minimum";
    auto speed = [unit](float uus) { return formatSpeed(uus, unit, 0); };
    switch (m) {
    case Mech::FlipReset:
        if (n < kFrConvertLevel) return "reset obtenu (4 roues sur la balle), le score ne compte pas";
        return std::string("reset puis flip dans la balle") + (n >= kFrTargetLevel ? ", tir cadré" : "") + ", " + score;
    case Mech::Ceiling:
        return "frappe en flip sans sauter du plafond, balle à " + speed(ceilingSpeedNeeded(L)) + " ou plus" +
               (n >= kTargetLevel ? ", tir cadré" : "") + ", " + score;
    case Mech::Breezi:
        if (n < kBzTornadoLevel)
            return "tornade d'un peu plus d'un demi-tour (230°), air roll tenu, balle près de toi, sans flip";
        [[fallthrough]];
    case Mech::Flick:
    case Mech::Musty:
        return std::string(mechInfo(m).title) + ", balle à " + speed(flickPowerNeeded(m, L)) + " ou plus" +
               (n >= kFlickTargetLevel ? ", tir cadré" : ", tir vers le but") + ", flick avant 12 m du but, " + score;
    case Mech::DoubleTap:
        return std::string(n < kDtTargetLevel ? "rebond touché" : "rebond repris et cadré") + ", balle à " +
               speed(doubleTapSpeedNeeded(L)) + " ou plus" +
               (n >= kDtFullLevel ? ", premier tir au-dessus de 7,5 m (750 uu)" : "") + ", " + score;
    case Mech::AirDribble:
        if (n == 1) return "saut avec la balle (2,5 m au-dessus du toit) puis une touche en l'air, le score ne compte pas";
        if (n <= 3) return "au moins 3 touches dont 2 en l'air, " + score;
        return "air dribble d'au moins " + formatNumber(airDribbleSeconds(L), 1) + " s" +
               (n >= kAdTargetLevel ? ", fin cadrée" : "") + ", " + score;
    case Mech::AirRoll:
        if (n < kArTargetLevel) return "tir avec air roll, balle à " + speed(airRollSpeedNeeded(L)) + " ou plus, sans cadrage ni score";
        return "tir avec air roll, cadré, balle à " + speed(airRollSpeedNeeded(L)) + " ou plus, " + score;
    case Mech::Aerial: return "tir cadré, balle à " + speed(aerialSpeedNeeded(L)) + " ou plus, " + score;
    case Mech::Pinch:
        return "vrai pinch (balle contre le mur) à " + speed(pinchSpeedNeeded(L)) + " ou plus" +
               (n >= kTargetLevel ? ", cadré" : "");
    case Mech::PowerShot: return "frappe cadrée à " + speed(powerShotSpeedNeeded(L)) + " ou plus, " + score;
    default: return score;
    }
}

} // namespace rlt::bm
