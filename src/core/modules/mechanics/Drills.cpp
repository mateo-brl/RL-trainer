#include "core/modules/mechanics/Drills.h"

#include <algorithm>
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

// Score asked by the drill: none on the first levels (the pass conditions decide alone), then up to `hard`.
float passFrom(float level, float hard) { return hard * clamp01((level - 0.2f) / 0.8f); }

} // namespace

DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng) {
    DrillSetup d;
    d.focus = f;
    level = clamp01(level);
    auto L = [level](float easy, float hard) { return levelLerp(level, easy, hard); };
    const int alt = (attempt % 2 == 0) ? 1 : -1;
    // Parked ball: far corner of the opponent half, away from every drill lane.
    d.ball.pos = {3300.f, 4300.f, kBallZ};
    d.memo = focusInfo(f).memo;
    // Top levels (8 to 10): the full grading must agree too. Below, the pass conditions and the score decide.
    d.requireDetector = level >= 0.8f;

    switch (f) {
    case Focus::SpeedFlip: {
        // Decomposed: levels 1-3 train the diagonal flip and its cancel alone, levels 4-6 the real kickoff from
        // the diagonals, levels 7-10 every kickoff spot.
        const int n = displayLevel(level);
        d.stages = 3;
        d.stage = n <= 3 ? 1 : (n <= 6 ? 2 : 3);
        d.countdown = 1.f;
        d.timeLimit = 4.f;
        d.passScore = passFrom(level, 85.f);
        if (d.stage == 1) {
            placeCar(d, {0.f, -3500.f, kRestZ}, 90.f, {}, 100.f);
            d.guide = {{0.f, -3500.f, 5.f}, {0.f, -1000.f, 5.f}};
            d.variant = "ligne droite";
            d.goal = "Étape 1 sur 3 : flip en diagonale annulé, sans balle";
            d.memo = "Saute, flip en diagonale, tire le stick vers toi";
            d.steps = {{"Avance avec le boost", Cue::Boost},
                       {"Saute", Cue::Jump},
                       {"Flip en diagonale vers l'avant", Cue::Dodge},
                       {"Tire tout de suite le stick vers toi pour annuler", Cue::Cancel},
                       {"Retombe sur les roues", Cue::Land}};
        } else {
            struct Spot {
                float x, y, yaw;
                const char* name;
            };
            static const Spot spots[5] = {{-2048.f, -2560.f, 45.f, "diagonale droite"},
                                          {2048.f, -2560.f, 135.f, "diagonale gauche"},
                                          {-256.f, -3840.f, 90.f, "décalé droite"},
                                          {256.f, -3840.f, 90.f, "décalé gauche"},
                                          {0.f, -4608.f, 90.f, "centre"}};
            const int pool = d.stage == 2 ? 2 : (n <= 8 ? 4 : 5);
            const Spot& sp = spots[attempt % pool];
            placeCar(d, {sp.x, sp.y, kRestZ}, sp.yaw, {}, 33.f);
            placeBall(d, {0.f, 0.f, kBallZ});
            d.guide = {{sp.x, sp.y, 5.f}, {0.f, 0.f, 5.f}};
            d.kickoff = true;
            d.variant = sp.name;
            d.goal = d.stage == 2 ? "Étape 2 sur 3 : speedflip au kickoff, depuis les diagonales"
                                  : "Étape 3 sur 3 : speedflip au kickoff, toutes les positions";
            d.steps = {{"Au GO : boost et tourne un peu", Cue::Boost},
                       {"Saute", Cue::Jump},
                       {"Flip en diagonale vers l'avant", Cue::Dodge},
                       {"Tire tout de suite le stick vers toi pour annuler", Cue::Cancel},
                       {"Retombe sur les roues et fonce sur la balle", Cue::Touch}};
            // Kickoff timing of the first jump (SpeedFlipTrainer: 55-70 ticks after GO), widened at first.
            d.checks.push_back({"kickoffFirstJumpTicks", L(25.f, 50.f), L(110.f, 80.f), "KO_FIRST_JUMP_EARLY",
                                "KO_FIRST_JUMP_LATE", true});
        }
        d.checks.push_back({"jumpToDodgeTicks", -1e9f, L(36.f, 14.f), "", "SF_JUMP_LONG"});
        // Degrees around the target angle (30 by default). The straight side stays a little tighter: at level 1
        // the flip must still be at least 5 degrees off the nose (the detector's own diagonal threshold), so a
        // plain front flip is never praised as a diagonal one.
        d.checks.push_back({"angleErrorDeg", -L(25.f, 8.f), L(28.f, 8.f), "SF_ANGLE_STRAIGHT", "SF_ANGLE_WIDE"});
        d.checks.push_back({"cancelTicks", 0.f, L(20.f, 6.f), "SF_NO_CANCEL", "SF_LATE_CANCEL"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BAD_LANDING"});
        if (d.stage >= 2) {
            d.checks.push_back({"headingErrorDeg", -1e9f, L(40.f, 12.f), "", "SF_HEADING_OFF"});
            d.checks.push_back({"boostRatio", L(0.4f, 0.9f), 1e9f, "SF_BOOST_RELEASED", ""});
        }
        break;
    }
    case Focus::HalfFlip: {
        const float speed = L(300.f, 1100.f);
        placeCar(d, {0.f, -1500.f, kRestZ}, -90.f, {0.f, speed, 0.f}, 33.f);  // facing own goal, reversing
        placeBall(d, {0.f, L(0.f, 700.f), kBallZ});
        d.timeLimit = L(4.5f, 3.5f);
        d.passScore = passFrom(level, 80.f);
        d.needTouch = true;  // the half-flip only counts when the car then reaches the ball
        d.variant = "marche arrière";
        d.goal = "Tu recules vers la balle : demi-tour express, puis touche-la";
        d.steps = {{"Saute", Cue::Jump},
                   {"Flip arrière : stick vers toi", Cue::Dodge},
                   {"À la verticale, pousse le stick vers l'avant", Cue::Cancel},
                   {"Air roll pour retomber sur les roues", Cue::Land},
                   {"Fonce sur la balle", Cue::Touch}};
        d.checks.push_back({"cancelTicks", 0.f, 1e9f, "HF_NO_CANCEL", ""});
        d.checks.push_back({"cancelTicks", L(4.f, 10.f), L(60.f, 36.f), "HF_EARLY_CANCEL", "HF_LATE_CANCEL"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "HF_BAD_LANDING"});
        d.checks.push_back({"headingChangeDeg", L(110.f, 155.f), 1e9f, "HF_HEADING_OFF", ""});
        break;
    }
    case Focus::FlipCancel: {
        placeCar(d, {0.f, -3000.f, kRestZ}, 90.f, {0.f, L(800.f, 1400.f), 0.f}, 33.f);
        d.timeLimit = L(4.f, 3.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "flip avant";
        d.goal = "Flip avant annulé : la voiture reste à plat";
        d.steps = {{"Saute", Cue::Jump},
                   {"Flip avant", Cue::Dodge},
                   {"Tire tout de suite le stick à fond vers toi", Cue::Cancel},
                   {"Retombe sur les roues", Cue::Land}};
        d.checks.push_back({"cancelTicks", 0.f, L(20.f, 6.f), "FC_NO_CANCEL", "FC_LATE"});
        d.checks.push_back({"cancelStrength", L(0.5f, 0.9f), 1e9f, "FC_WEAK", ""});
        d.checks.push_back({"landedOnWheels", 0.5f, 1e9f, "FC_BAD_LANDING", ""});
        break;
    }
    case Focus::Wavedash: {
        placeCar(d, {0.f, -3000.f, L(170.f, 110.f)}, 90.f, {0.f, L(600.f, 1300.f), -100.f}, 0.f);
        d.timeLimit = L(3.5f, 2.5f);
        d.passScore = passFrom(level, 80.f);
        d.variant = "chute";
        d.goal = "Wavedash à la fin d'une petite chute";
        d.steps = {{"Lève un peu le nez (stick vers toi)", Cue::PitchBack},
                   {"Juste avant que les roues touchent : flip avant", Cue::Dodge},
                   {"Garde l'accélérateur", Cue::None}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_LATE_JUMPED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_NO_STICK"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_CRASH_LANDING"});
        d.checks.push_back({"gainRatio", L(0.25f, 0.5f), 1e9f, "WD_LOW_GAIN", ""});
        break;
    }
    case Focus::ChainDash: {
        placeCar(d, {0.f, -4200.f, kRestZ}, 90.f, {0.f, L(800.f, 300.f), 0.f}, 0.f);
        lane(d, 0.f, -4200.f, 3800.f, 160.f);
        d.timeLimit = L(8.f, 4.5f);
        d.passScore = passFrom(level, 75.f);
        d.timed = true;
        d.variant = "ligne droite";
        const int dashes = static_cast<int>(std::lround(L(2.f, 4.f)));
        d.goal = "Enchaîne au moins " + std::to_string(dashes) + " wavedashes sans boost";
        d.steps = {{"Petit saut", Cue::Jump},
                   {"Flip avant juste avant de toucher le sol", Cue::Dodge},
                   {"Garde le powerslide et relance tout de suite", Cue::None}};
        d.checks.push_back({"dashes", static_cast<float>(dashes), 1e9f, "DRILL_CHAIN_SHORT", ""});
        d.checks.push_back({"boostUsed", -1e9f, L(10.f, 0.f) + 0.01f, "", "CHAIN_BOOST_USED"});
        d.checks.push_back({"maxSpeed", L(1500.f, 2200.f), 1e9f, "CHAIN_GROUND_TIME", ""});
        break;
    }
    case Focus::ZapDash: {
        placeCar(d, {0.f, -3000.f, L(140.f, 100.f)}, 90.f, {0.f, L(1000.f, 1400.f), -250.f}, 0.f, -10.f);
        d.timeLimit = L(3.5f, 2.5f);
        d.passScore = passFrom(level, 80.f);
        d.variant = "réception";
        d.goal = "Zap dash : flip avant dès le rebond de la réception";
        d.steps = {{"Garde le nez un peu baissé jusqu'au sol", Cue::Land},
                   {"Dès le rebond : flip avant, sans sauter", Cue::Dodge},
                   {"Garde l'accélérateur", Cue::None}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ZAP_NO_BOUNCE"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_LATE_JUMPED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_NO_STICK"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_CRASH_LANDING"});
        d.checks.push_back({"gainRatio", L(0.25f, 0.5f), 1e9f, "WD_LOW_GAIN", ""});
        break;
    }
    case Focus::WallDash: {
        // Wall at x = -4096 (car up = +X, roll -90) or x = +4096 (up = -X, roll +90). Roll sign UNVERIFIED in game.
        const float wallX = alt > 0 ? -4096.f : 4096.f;
        const float carX = alt > 0 ? wallX + kRestZ : wallX - kRestZ;
        placeCar(d, {carX, -2500.f, 500.f}, 90.f, {0.f, L(600.f, 1000.f), 0.f}, 0.f, 0.f, alt > 0 ? -90.f : 90.f);
        const float gx = alt > 0 ? wallX + 5.f : wallX - 5.f;
        d.guide = {{gx, -2500.f, 350.f}, {gx, 2500.f, 350.f}, {gx, 2500.f, 650.f}, {gx, -2500.f, 650.f}};
        d.timeLimit = L(5.f, 4.f);
        d.passScore = passFrom(level, 80.f);
        d.endOnResult = false;
        d.variant = alt > 0 ? "mur droit" : "mur gauche";
        d.goal = "Avance sur le mur avec des wall dashes, sans boost";
        d.steps = {{"Tape saut sans le garder", Cue::Jump},
                   {"Tout de suite : flip en diagonale vers le mur", Cue::Dodge},
                   {"Recommence dès que les roues retouchent le mur", Cue::None}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WALL_FELL_OFF"});
        d.checks.push_back({"jumpToDodgeTicks", -1e9f, L(30.f, 11.f), "", "WALL_TAP_SLOW"});
        d.checks.push_back({"gainRatio", L(0.2f, 0.5f), 1e9f, "WALL_LOW_GAIN", ""});
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
        d.timeLimit = L(4.f, 3.f);
        d.passScore = passFrom(level, 85.f);
        d.needTouch = true;
        d.variant = "balle haute";
        d.goal = "Décolle vite et touche la balle en l'air";
        d.steps = {{"Saute en gardant le bouton, boost et stick vers toi", Cue::Jump},
                   {"Lâche puis rappuie tout de suite sur saut", Cue::DoubleJump},
                   {"Garde le boost et vise la balle", Cue::Touch}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "FA_ACCIDENTAL_DODGE"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "FA_NO_DOUBLE_JUMP"});
        d.checks.push_back({"timeToBall", -1e9f, L(3.2f, 1.6f), "", "DRILL_SLOW_TO_BALL", true});
        break;
    }
    case Focus::Stall: {
        // Higher at first: more time to set the inputs before the floor.
        placeCar(d, {0.f, -1000.f, L(900.f, 450.f)}, 90.f, {0.f, 500.f, 200.f}, 33.f);
        d.timeLimit = L(4.f, 3.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "en l'air";
        d.goal = "Fais flotter la voiture avec un stall";
        d.steps = {{"Stick à fond d'un côté, air roll à fond de l'autre", Cue::Roll},
                   {"Appuie sur saut", Cue::Dodge},
                   {"Lâche tout et laisse flotter", Cue::None}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_NO_FLIP"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_YAW_ROLL_MISMATCH"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_PITCH_LEAK"});
        d.checks.push_back({"hoverTicks", L(8.f, 40.f), 1e9f, "ST_SHORT_HOVER", ""});
        break;
    }
    case Focus::Helicopter: {
        placeCar(d, {0.f, -1000.f, 1000.f}, 90.f, {0.f, 300.f, 450.f}, 33.f);
        d.timeLimit = L(4.5f, 3.5f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "en l'air";
        d.goal = "Fais tourner la voiture en hélicoptère";
        d.steps = {{"Air roll à fond d'un côté", Cue::Roll},
                   {"Stick à fond du même côté", Cue::Yaw},
                   {"Garde les deux sans bouger", Cue::None}};
        d.checks.push_back({"durationTicks", L(60.f, 150.f), 1e9f, "AS_SHORT", ""});
        d.checks.push_back({"axisStability", L(0.5f, 0.85f), 1e9f, "AS_UNSTABLE_AXIS", ""});
        d.checks.push_back({"avgSpinRate", L(1.5f, 3.5f), 1e9f, "AS_SLOW_SPIN", ""});
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
        d.timeLimit = L(5.f, 4.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "lancer aléatoire";
        d.goal = "Retombe sur les roues sans perdre ta vitesse";
        d.steps = {{"Roues vers le sol, nez dans le sens du mouvement", Cue::Land},
                   {"Garde l'accélérateur", Cue::None}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "RC_CRASHED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "RC_ON_ROOF"});
        d.checks.push_back({"tiltAtTouchDeg", -1e9f, L(45.f, 15.f), "", "RC_TILTED"});
        d.checks.push_back({"speedRetained", L(0.4f, 0.85f), 1e9f, "RC_SPEED_LOST", ""});
        break;
    }
    case Focus::Powerslide: {
        placeCar(d, {0.f, -2500.f, kRestZ}, 90.f, {0.f, L(900.f, 1600.f), 0.f}, 33.f);
        placeBall(d, {static_cast<float>(alt) * L(1000.f, 700.f), -1700.f, kBallZ});
        d.timeLimit = L(4.5f, 3.5f);
        d.passScore = passFrom(level, 80.f);
        d.variant = alt > 0 ? "balle à gauche" : "balle à droite";
        d.goal = "Vire serré vers la balle sans perdre ta vitesse";
        d.steps = {{"Tourne vers la balle", Cue::Steer},
                   {"Appuie un court instant sur powerslide", Cue::Slide},
                   {"Relâche dès que le nez vise la balle", Cue::None}};
        d.checks.push_back({"speedRetained", L(0.5f, 0.85f), 1e9f, "PS_SPEED_LOSS", ""});
        d.checks.push_back({"exitSlipDeg", -1e9f, L(40.f, 12.f), "", "PS_EXIT_SLIP"});
        break;
    }
    case Focus::Boost: {
        const float v = 2250.f;
        placeCar(d, {-3000.f, -4000.f, kRestZ}, 45.f, {v * 0.7071f, v * 0.7071f, 0.f}, 40.f);
        d.timeLimit = 5.f;
        d.passScore = 0.f;
        d.requireDetector = false;  // the window is judged on its own two numbers
        d.boostWindow = true;
        d.timed = true;
        d.variant = "supersonique";
        d.goal = "Reste supersonique 5 s en dépensant peu de boost";
        d.steps = {{"Garde l'accélérateur appuyé", Cue::None}, {"Petits coups de boost quand tu ralentis", Cue::None}};
        d.checks.push_back({"supersonicRatio", L(0.7f, 0.9f), 1e9f, "SM_NOT_SUPERSONIC", ""});
        d.checks.push_back({"boostUsed", -1e9f, L(30.f, 12.f), "", "SM_TOO_MUCH_BOOST"});
        break;
    }
    default: break;
    }
    if (team == 1) mirrorForOrange(d);
    return d;
}

DrillJudgement judgeDrill(const DrillSetup& d, const MechGrade& g) {
    if (g.hasError("DRILL_NO_TOUCH")) return {false, "DRILL_NO_TOUCH"};
    for (const DrillCheck& c : d.checks) {
        if (!c.key) {
            if (g.hasError(c.lowCode)) return {false, c.lowCode};
            continue;
        }
        const bool has = g.hasMetric(c.key);
        const float v = has ? g.metric(c.key) : 0.f;
        if (c.optional && (!has || v < 0.f)) continue;
        if (!has || v < c.lo) return {false, c.lowCode[0] ? c.lowCode : c.highCode};
        if (v > c.hi) return {false, c.highCode[0] ? c.highCode : c.lowCode};
    }
    return {};
}

int displayLevel(float level) { return 1 + static_cast<int>(std::lround(clamp01(level) * 9.f)); }

} // namespace rlt::mech
