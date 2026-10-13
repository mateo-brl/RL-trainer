#include "core/modules/mechanics/Drills.h"

#include <cmath>

#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

namespace {

constexpr float kBallZ = 92.75f;
constexpr float kRestZ = k::kCarRestZ;

int toUnr(float deg) { return static_cast<int>(std::lround(deg * 65536.f / 360.f)); }
Rot rotDeg(float pitch, float yaw, float roll) { return {toUnr(pitch), toUnr(yaw), toUnr(roll)}; }

// Rotation of 180 degrees around Z (orange side).
Vec3 flipXY(const Vec3& v) { return {-v.x, -v.y, v.z}; }

void mirrorForOrange(DrillSetup& d) {
    d.car.pos = flipXY(d.car.pos);
    d.car.vel = flipXY(d.car.vel);
    d.car.angVel = flipXY(d.car.angVel);
    d.car.rot.yaw += toUnr(180.f);
    if (d.car.rot.yaw > 32768) d.car.rot.yaw -= 65536;
    d.ball.pos = flipXY(d.ball.pos);
    d.ball.vel = flipXY(d.ball.vel);
    d.ball.angVel = flipXY(d.ball.angVel);
    for (Vec3& p : d.guide) p = flipXY(p);
    for (Vec3& p : d.ticks) p = flipXY(p);
    d.target = flipXY(d.target);
}

void placeCar(DrillSetup& d, Vec3 pos, float yawDeg, Vec3 vel, float boost, float pitchDeg = 0.f, float rollDeg = 0.f) {
    d.car.pos = pos;
    d.car.rot = rotDeg(pitchDeg, yawDeg, rollDeg);
    d.car.vel = vel;
    d.car.angVel = {};
    d.car.boost = boost;
}

void placeBall(DrillSetup& d, Vec3 pos, Vec3 vel = {}) {
    d.placeBall = true;
    d.ball.pos = pos;
    d.ball.vel = vel;
    d.ball.angVel = {};
    d.hasTarget = true;
    d.target = pos;
}

// Straight lane on the floor with crossbars every 1000 uu (chain wavedash).
void lane(DrillSetup& d, float x, float y0, float y1, float halfWidth) {
    d.guide = {{x - halfWidth, y0, 5.f}, {x - halfWidth, y1, 5.f}, {x + halfWidth, y1, 5.f}, {x + halfWidth, y0, 5.f}};
    for (float y = y0 + 1000.f; y < y1; y += 1000.f) {
        d.ticks.push_back({x - halfWidth, y, 5.f});
        d.ticks.push_back({x + halfWidth, y, 5.f});
    }
}

} // namespace

DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng) {
    DrillSetup d;
    d.focus = f;
    level = clamp01(level);
    auto L = [level](float easy, float hard) { return levelLerp(level, easy, hard); };
    const int alt = (attempt % 2 == 0) ? 1 : -1;
    // Parked ball: far corner of the opponent half, away from every drill lane.
    d.ball.pos = {3300.f, 4300.f, kBallZ};

    switch (f) {
    case Focus::SpeedFlip: {
        struct Spot {
            float x, y, yaw;
            const char* name;
        };
        static const Spot spots[5] = {{-2048.f, -2560.f, 45.f, "diagonale droite"},
                                      {2048.f, -2560.f, 135.f, "diagonale gauche"},
                                      {-256.f, -3840.f, 90.f, "décalé droite"},
                                      {256.f, -3840.f, 90.f, "décalé gauche"},
                                      {0.f, -4608.f, 90.f, "centre"}};
        const int pool = level < 0.34f ? 2 : (level < 0.67f ? 4 : 5);
        const Spot& sp = spots[attempt % pool];
        placeCar(d, {sp.x, sp.y, kRestZ}, sp.yaw, {}, 33.f);
        placeBall(d, {0.f, 0.f, kBallZ});
        d.guide = {{sp.x, sp.y, 5.f}, {0.f, 0.f, 5.f}};
        d.countdown = 1.f;
        d.kickoff = true;
        d.timeLimit = 4.f;
        d.passScore = L(60.f, 85.f);
        d.variant = sp.name;
        d.goal = "Speedflip vers la balle : flip à ~30°, annulation < 50 ms, boost tenu.";
        break;
    }
    case Focus::HalfFlip: {
        const float speed = L(300.f, 1100.f);
        placeCar(d, {0.f, -1500.f, kRestZ}, -90.f, {0.f, speed, 0.f}, 33.f);  // facing own goal, reversing
        placeBall(d, {0.f, L(0.f, 700.f), kBallZ});
        d.timeLimit = 3.5f;
        d.passScore = L(55.f, 80.f);
        d.variant = "marche arrière";
        d.goal = "Tu recules vers la balle : half-flip pour faire demi-tour, puis fonce dessus.";
        break;
    }
    case Focus::FlipCancel: {
        placeCar(d, {0.f, -3000.f, kRestZ}, 90.f, {0.f, L(800.f, 1400.f), 0.f}, 33.f);
        d.timeLimit = 3.f;
        d.passScore = L(60.f, 85.f);
        d.variant = "flip avant";
        d.goal = "Flip avant puis tire le stick en arrière tout de suite (< 50 ms) pour l'annuler.";
        break;
    }
    case Focus::Wavedash: {
        placeCar(d, {0.f, -3000.f, L(170.f, 110.f)}, 90.f, {0.f, L(600.f, 1300.f), -100.f}, 0.f);
        d.timeLimit = 2.5f;
        d.passScore = L(55.f, 80.f);
        d.variant = "chute";
        d.goal = "Wavedash à la réception : nez cabré de 15-25°, flip quand les roues arrière touchent.";
        break;
    }
    case Focus::ChainDash: {
        placeCar(d, {0.f, -4200.f, kRestZ}, 90.f, {0.f, L(800.f, 300.f), 0.f}, 0.f);
        lane(d, 0.f, -4200.f, 3800.f, 160.f);
        d.timeLimit = L(6.f, 4.5f);
        d.passScore = L(55.f, 75.f);
        d.variant = "ligne droite";
        d.goal = "Enchaîne les wavedashes sans boost jusqu'au supersonique, powerslide maintenu.";
        break;
    }
    case Focus::ZapDash: {
        placeCar(d, {0.f, -3000.f, L(140.f, 100.f)}, 90.f, {0.f, L(1000.f, 1400.f), -250.f}, 0.f, -10.f);
        d.timeLimit = 2.5f;
        d.passScore = L(55.f, 80.f);
        d.variant = "réception";
        d.goal = "Atterris le nez un peu baissé, puis flip avant dès le rebond, sans sauter.";
        break;
    }
    case Focus::WallDash: {
        // Wall at x = -4096 (car up = +X, roll -90) or x = +4096 (up = -X, roll +90). Roll sign UNVERIFIED in game.
        const float wallX = alt > 0 ? -4096.f : 4096.f;
        const float carX = alt > 0 ? wallX + kRestZ : wallX - kRestZ;
        placeCar(d, {carX, -2500.f, 500.f}, 90.f, {0.f, L(600.f, 1000.f), 0.f}, 0.f, 0.f, alt > 0 ? -90.f : 90.f);
        const float gx = alt > 0 ? wallX + 5.f : wallX - 5.f;
        d.guide = {{gx, -2500.f, 350.f}, {gx, 2500.f, 350.f}, {gx, 2500.f, 650.f}, {gx, -2500.f, 650.f}};
        d.timeLimit = 4.f;
        d.passScore = L(55.f, 80.f);
        d.endOnResult = false;
        d.variant = alt > 0 ? "mur droit" : "mur gauche";
        d.goal = "Wall dash : double tap saut très rapide, flip en diagonale vers le mur, enchaîne.";
        break;
    }
    case Focus::FastAerial: {
        placeCar(d, {0.f, -2500.f, kRestZ}, 90.f, {}, 100.f);
        const float h = L(900.f, 1500.f);
        // Thrown up so that its apex (height h) comes 1.3 s after the placement.
        const float vz = 650.f * 1.3f;
        const float z0 = std::max(kBallZ + 10.f, h - vz * vz / (2.f * 650.f));
        placeBall(d, {0.f, L(-1700.f, -1100.f), z0}, {0.f, 0.f, vz});
        d.target = {0.f, d.ball.pos.y, h};
        d.timeLimit = 3.f;
        d.passScore = L(60.f, 85.f);
        d.needTouch = true;
        d.variant = "balle haute";
        d.goal = "Fast aerial : saut tenu 0,2 s + boost, double saut immédiat, touche la balle au plus vite.";
        break;
    }
    case Focus::Stall: {
        placeCar(d, {0.f, -1000.f, L(400.f, 900.f)}, 90.f, {0.f, 500.f, 200.f}, 33.f);
        d.timeLimit = 3.f;
        d.passScore = L(55.f, 85.f);
        d.variant = "en l'air";
        d.goal = "Stall : stick à fond d'un côté, air roll à fond de l'autre, puis saut.";
        break;
    }
    case Focus::Helicopter: {
        placeCar(d, {0.f, -1000.f, 1000.f}, 90.f, {0.f, 300.f, 450.f}, 33.f);
        d.timeLimit = 3.5f;
        d.passScore = L(55.f, 85.f);
        d.variant = "en l'air";
        d.goal = "Hélico : air roll et yaw du même côté pendant plus d'1 s, axe bien stable.";
        break;
    }
    case Focus::Recovery: {
        const float yaw = rng.range(-180.f, 180.f);
        const float pitch = L(20.f, 160.f) * (rng.chance(0.5f) ? 1.f : -1.f);
        const float roll = L(30.f, 150.f) * (rng.chance(0.5f) ? 1.f : -1.f);
        const float dir = rng.range(-kPi, kPi);
        const float hs = L(400.f, 1100.f);
        placeCar(d, {rng.range(-1500.f, 1500.f), rng.range(-2500.f, 0.f), L(500.f, 900.f)}, yaw,
                 {std::cos(dir) * hs, std::sin(dir) * hs, L(400.f, 700.f)}, 33.f, pitch, roll);
        const float w = L(0.f, 4.f);
        d.car.angVel = {rng.range(-w, w), rng.range(-w, w), rng.range(-w, w)};
        d.timeLimit = 4.f;
        d.passScore = L(60.f, 85.f);
        d.variant = "lancer aléatoire";
        d.goal = "Retombe sur les roues, nez dans le sens du mouvement (ou wavedash si tu as ton flip).";
        break;
    }
    case Focus::Powerslide: {
        placeCar(d, {0.f, -2500.f, kRestZ}, 90.f, {0.f, L(1000.f, 1600.f), 0.f}, 33.f);
        placeBall(d, {static_cast<float>(alt) * L(1000.f, 700.f), -1700.f, kBallZ});
        d.timeLimit = 3.5f;
        d.passScore = L(60.f, 80.f);
        d.variant = alt > 0 ? "balle à gauche" : "balle à droite";
        d.goal = "Powerslide court pour viser la balle sur le côté sans perdre de vitesse.";
        break;
    }
    case Focus::Boost: {
        const float v = 2250.f;
        placeCar(d, {-3000.f, -4000.f, kRestZ}, 45.f, {v * 0.7071f, v * 0.7071f, 0.f}, 40.f);
        d.timeLimit = 5.f;
        d.passScore = L(60.f, 85.f);
        d.boostWindow = true;
        d.variant = "supersonique";
        d.goal = "Reste supersonique 5 s en dépensant moins de 15 de boost (petits taps).";
        break;
    }
    default: break;
    }
    if (team == 1) mirrorForOrange(d);
    return d;
}

} // namespace rlt::mech
