#include "core/modules/mechanics/Drills.h"

#include <algorithm>
#include <cmath>

#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

namespace {

constexpr float kBallZ = 92.75f;
// Floor placement height: slightly above rest, the car settles during the countdown (kickoff module: kKoCarPlaceZ).
constexpr float kPlaceZ = 20.f;

int toUnr(float deg) { return static_cast<int>(std::lround(deg * 65536.f / 360.f)); }
Rot rotDeg(float pitch, float yaw, float roll) { return {toUnr(pitch), toUnr(yaw), toUnr(roll)}; }

// Rotation of 180 degrees around Z (orange side).
Vec3 flipXY(const Vec3& v) { return {-v.x, -v.y, v.z}; }

void mirrorForOrange(DrillSetup& d) {
    d.mirrored = true;
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
    // endY stays in blue axes: the module compares it with the mirrored y.
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

Vec3 heading(float yawDeg, float speed) {
    return {std::cos(yawDeg * kDegToRad) * speed, std::sin(yawDeg * kDegToRad) * speed, 0.f};
}

// Straight lane on the floor with crossbars every 1000 uu (chain wavedash).
void lane(DrillSetup& d, float x, float y0, float y1, float halfWidth) {
    d.guide = {{x - halfWidth, y0, 5.f}, {x - halfWidth, y1, 5.f}, {x + halfWidth, y1, 5.f}, {x + halfWidth, y0, 5.f}};
    for (float y = y0 + 1000.f; y < y1; y += 1000.f) {
        d.ticks.push_back({x - halfWidth, y, 5.f});
        d.ticks.push_back({x + halfWidth, y, 5.f});
    }
}

// Band on a side wall between two heights (wall dash, recovery "parcours complet").
void wallBand(DrillSetup& d, float wallX, float z0, float z1) {
    const float gx = wallX < 0.f ? wallX + 5.f : wallX - 5.f;
    d.guide = {{gx, -2500.f, z0}, {gx, 2500.f, z0}, {gx, 2500.f, z1}, {gx, -2500.f, z1}};
}

// Score asked by the drill: none on the first levels (the pass conditions decide alone), then up to `hard`.
float passFrom(float level, float hard) { return hard * clamp01((level - 0.2f) / 0.8f); }

// Position of the level inside a range of niveaux (0 at niveau a, 1 at niveau b).
float within(float level, int a, int b) { return clamp01((level - levelOf(a)) / std::max(1e-3f, levelOf(b) - levelOf(a))); }

const KickoffSpot kSpots[5] = {{-2048.f, -2560.f, 45.f, "diagonale droite", 0},
                               {2048.f, -2560.f, 135.f, "diagonale gauche", 0},
                               {-256.f, -3840.f, 90.f, "décalé droite", 1},
                               {256.f, -3840.f, 90.f, "décalé gauche", 1},
                               {0.f, -4608.f, 90.f, "centre", 2}};

void kickoffPlacement(DrillSetup& d, const KickoffSpot& sp) {
    placeCar(d, {sp.x, sp.y, kPlaceZ}, sp.yaw, {}, 33.33f);
    placeBall(d, {0.f, 0.f, kBallZ});
    d.guide = {{sp.x, sp.y, 5.f}, {0.f, 0.f, 5.f}};  // guide line from the spot to the ball
    d.kickoff = true;
    d.kickoffRules = true;
    d.spotKind = sp.kind;
    d.variant = sp.name;
}

} // namespace

const KickoffSpot& kickoffSpot(int index) { return kSpots[std::max(0, std::min(4, index))]; }

float kickoffTimeToBall(int kind, float t) {
    // KickoffData.cpp:173-175, Débutant -> Standard.
    static constexpr float beginner[3] = {2.40f, 2.65f, 2.95f};
    static constexpr float solid[3] = {1.95f, 2.20f, 2.48f};
    const int k = std::max(0, std::min(2, kind));
    return lerpf(beginner[k], solid[k], clamp01(t));
}

float hitboxGap(const CarState& car, const Vec3& ball, const Vec3& halfExtent, const Vec3& offset, float ballRadius) {
    const Basis& b = car.basis;
    const Vec3 c = car.pos + b.fwd * offset.x + b.right * offset.y + b.up * offset.z;
    const Vec3 d = ball - c;
    const float ex = std::max(0.f, std::fabs(dot(d, b.fwd)) - halfExtent.x);
    const float ey = std::max(0.f, std::fabs(dot(d, b.right)) - halfExtent.y);
    const float ez = std::max(0.f, std::fabs(dot(d, b.up)) - halfExtent.z);
    return std::sqrt(ex * ex + ey * ey + ez * ez) - ballRadius;
}

float rearWheelTimeToFloor(const CarState& car) {
    if (!car.valid || car.vel.z >= -1.f) return 1e9f;
    const Basis& b = car.basis;
    // Octane rear wheels (RocketSim): 33.75 uu behind, 29.5 uu to the side, contact 17 uu under the centre.
    float h = 1e9f;
    for (float side : {-29.5f, 29.5f}) h = std::min(h, (car.pos + b.fwd * -33.75f + b.right * side - b.up * 17.f).z);
    h = std::max(0.f, h);
    const float v = -car.vel.z;
    return (std::sqrt(v * v + 2.f * -k::kGravityZ * h) - v) / -k::kGravityZ;
}

DrillSetup makeDrill(Focus f, float level, int attempt, int team, Rng& rng, bool full) {
    DrillSetup d;
    d.focus = f;
    d.full = full;
    level = clamp01(level);
    auto L = [level](float easy, float hard) { return levelLerp(level, easy, hard); };
    const int alt = (attempt % 2 == 0) ? 1 : -1;
    d.side = alt;
    // Parked ball: far corner of the opponent half, away from every drill lane.
    d.ball.pos = {3300.f, 4300.f, kBallZ};
    d.memo = focusInfo(f).memo;
    // Top levels (8 to 10): the full grading must agree too. Below, the pass conditions and the score decide.
    d.requireDetector = level >= 0.8f;
    if (full) {
        d.variant = "parcours complet";
        d.countdown = 2.f;
        d.passScore = 0.f;
        d.requireDetector = false;
    }

    switch (f) {
    case Focus::SpeedFlip: {
        const int n = displayLevel(level);
        d.stages = 3;
        if (full) {
            // A real kickoff with no help: random spot, match boost, ball at the centre.
            kickoffPlacement(d, kSpots[rng.rangeInt(0, 4)]);
            d.variant = std::string("parcours complet, ") + d.variant;
            d.countdown = 3.f;
            d.timeLimit = 4.5f;
            d.goal = "Parcours complet : un vrai kickoff, de la touche au boost jusqu'à la balle";
            break;
        }
        // Decomposed: levels 1-3 train the diagonal flip and its cancel alone (no ball), levels 4-6 the kickoff from
        // the diagonals (passing close to the ball is enough at 4 and 5), levels 7-10 every kickoff spot.
        d.stage = n <= 3 ? 1 : (n <= 6 ? 2 : 3);
        d.timeLimit = 4.5f;
        d.passScore = passFrom(level, 85.f);
        if (d.stage == 1) {
            placeCar(d, {0.f, -3500.f, kPlaceZ}, 90.f, {}, 100.f);
            d.guide = {{0.f, -3500.f, 5.f}, {0.f, -1000.f, 5.f}};
            d.variant = "ligne droite";
            d.goal = "Étape 1 sur 3 : flip en diagonale annulé, sans balle";
            d.memo = "Petit saut, flip en diagonale, stick vers toi";
            d.steps = {{"Boost et accélérateur", Cue::Boost, "SF_NO_BOOST"},
                       {"Petit saut", Cue::Jump, "SF_NO_JUMP"},
                       {"Flip en diagonale", Cue::Dodge, "SF_NO_FLIP"},
                       {"Stick vers toi tout de suite", Cue::Cancel, "SF_NO_CANCEL"},
                       {"Roues au sol", Cue::Land, "SF_BAD_LANDING"}};
            d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_SIDE_FLIP"});
            d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BACK_FLIP"});
            d.checks.push_back({"jumpToDodgeTicks", -1e9f, L(36.f, 14.f), "", "SF_JUMP_LONG"});
            // Degrees around the target angle (30 by default). At level 1 the flip must still be at least 5 degrees
            // off the nose (the detector's own diagonal threshold), so a plain front flip is never praised.
            d.checks.push_back({"angleErrorDeg", -L(25.f, 8.f), L(28.f, 8.f), "SF_ANGLE_STRAIGHT", "SF_ANGLE_WIDE"});
            d.checks.push_back({"cancelTicks", 0.f, L(20.f, 6.f), "SF_NO_CANCEL", "SF_LATE_CANCEL"});
            d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BAD_LANDING"});
            break;
        }
        const int pool = d.stage == 2 ? 2 : (n <= 8 ? 4 : 5);
        kickoffPlacement(d, kSpots[attempt % pool]);
        d.countdown = 2.f;
        d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_SIDE_FLIP"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BACK_FLIP"});
        d.steps = {{"Boost et accélérateur", Cue::Boost, "SF_NO_BOOST"},
                   {"Tourne un peu vers la balle", Cue::Steer, "SF_NO_JUMP"},
                   {"Petit saut", Cue::Jump, "SF_NO_JUMP"},
                   {"Flip en diagonale", Cue::Dodge, "SF_NO_FLIP"},
                   {"Stick vers toi tout de suite", Cue::Cancel, "SF_NO_CANCEL"},
                   {"Roues au sol, fonce", Cue::Touch, "DRILL_NO_TOUCH"}};
        if (d.stage == 2) {
            const float t = within(level, 4, 6);
            // Niveaux 4 and 5: passing close to the ball counts (the small steer before the jump comes with practice).
            d.reachGap = n <= 4 ? 150.f : (n == 5 ? 80.f : 0.f);
            d.goal = d.reachGap > 0.f ? "Étape 2 sur 3 : speedflip au kickoff, passe tout près de la balle"
                                      : "Étape 2 sur 3 : speedflip au kickoff, touche la balle";
            d.memo = "Au GO : boost et tourne un peu vers la balle, puis le speedflip";
            d.steps[0].text = "Au GO : boost et tourne un peu vers la balle";
            d.steps.erase(d.steps.begin() + 1);
            d.checks.push_back({"kickoffFirstJumpTicks", lerpf(30.f, 40.f, t), lerpf(105.f, 95.f, t), "KO_FIRST_JUMP_EARLY",
                                "KO_FIRST_JUMP_LATE", true});
            d.checks.push_back({"jumpToDodgeTicks", -1e9f, lerpf(30.f, 20.f, t), "", "SF_JUMP_LONG"});
            d.checks.push_back({"angleErrorDeg", -lerpf(20.f, 12.f, t), lerpf(20.f, 12.f, t), "SF_ANGLE_STRAIGHT", "SF_ANGLE_WIDE"});
            d.checks.push_back({"cancelTicks", 0.f, lerpf(12.f, 9.f, t), "SF_NO_CANCEL", "SF_LATE_CANCEL"});
            d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BAD_LANDING"});
            d.checks.push_back({"headingErrorDeg", -1e9f, lerpf(35.f, 20.f, t), "", "SF_HEADING_OFF"});
            d.checks.push_back({"boostRatio", lerpf(0.5f, 0.7f, t), 1e9f, "SF_BOOST_RELEASED", ""});
        } else {
            const float t = within(level, 7, 10);
            d.goal = "Étape 3 sur 3 : speedflip au kickoff, toutes les positions";
            d.requireDetector = true;
            d.passScore = lerpf(70.f, 85.f, t);
            d.checks.push_back({"kickoffFirstJumpTicks", lerpf(40.f, 50.f, t), lerpf(95.f, 80.f, t), "KO_FIRST_JUMP_EARLY",
                                "KO_FIRST_JUMP_LATE", true});
            d.checks.push_back({"jumpToDodgeTicks", -1e9f, lerpf(20.f, 14.f, t), "", "SF_JUMP_LONG"});
            d.checks.push_back({"angleErrorDeg", -lerpf(12.f, 8.f, t), lerpf(12.f, 8.f, t), "SF_ANGLE_STRAIGHT", "SF_ANGLE_WIDE"});
            d.checks.push_back({"cancelTicks", 0.f, lerpf(8.f, 6.f, t), "SF_NO_CANCEL", "SF_LATE_CANCEL"});
            d.checks.push_back({nullptr, -1e9f, 1e9f, "SF_BAD_LANDING"});
            d.checks.push_back({"headingErrorDeg", -1e9f, lerpf(20.f, 12.f, t), "", "SF_HEADING_OFF"});
            d.checks.push_back({"boostRatio", lerpf(0.7f, 0.9f, t), 1e9f, "SF_BOOST_RELEASED", ""});
            d.checks.push_back({"timeToBall", -1e9f, kickoffTimeToBall(d.spotKind, t), "", "SF_SLOW_TO_BALL", true});
        }
        break;
    }
    case Focus::HalfFlip: {
        if (full) {
            // Still car facing its own goal, the ball behind it: the player reverses himself.
            placeCar(d, {0.f, -1500.f, kPlaceZ}, -90.f, {}, 33.f);
            placeBall(d, {0.f, 0.f, kBallZ});
            d.timeLimit = 6.f;
            d.goal = "Parcours complet : recule, half-flip, puis touche la balle";
            break;
        }
        // Held facing its own goal, reversing toward the ball from GO.
        placeCar(d, {0.f, -1500.f, kPlaceZ}, -90.f, {0.f, L(300.f, 1100.f), 0.f}, 33.f);
        placeBall(d, {0.f, L(0.f, 700.f), kBallZ});
        d.timeLimit = L(5.f, 3.5f);
        d.passScore = passFrom(level, 80.f);
        d.needTouch = true;  // the half-flip only counts when the car then reaches the ball
        d.touchWindow = 2.5f;
        d.variant = "marche arrière";
        d.goal = "Tu recules vers la balle : demi-tour express, puis touche-la";
        d.steps = {{"Saute, puis flip arrière", Cue::Dodge, "HF_NO_FLIP"},
                   {"Nez en l'air : stick vers l'avant", Cue::Cancel, "HF_NO_CANCEL"},
                   {"Air roll, roues au sol", Cue::Land, "HF_BAD_LANDING"},
                   {"Fonce sur la balle", Cue::Touch, "DRILL_NO_TOUCH"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "HF_WRONG_FLIP"});
        d.checks.push_back({"jumpToDodgeTicks", -1e9f, L(20.f, 14.f), "", "HF_HIGH_JUMP"});
        d.checks.push_back({"cancelTicks", 0.f, 1e9f, "HF_NO_CANCEL", ""});
        d.checks.push_back({"cancelTicks", L(4.f, 10.f), L(48.f, 36.f), "HF_EARLY_CANCEL", "HF_LATE_CANCEL"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "HF_BAD_LANDING"});
        d.checks.push_back({"headingChangeDeg", L(120.f, 155.f), 1e9f, "HF_HEADING_OFF", ""});
        d.checks.push_back({"timeToBall", -1e9f, L(5.f, 1.9f), "", "HF_SLOW_TO_BALL", true});
        break;
    }
    case Focus::FlipCancel: {
        if (full) {
            placeCar(d, {0.f, -3500.f, kPlaceZ}, 90.f, {}, 33.f);
            d.timeLimit = 5.f;
            d.guide = {{0.f, -3500.f, 5.f}, {0.f, 0.f, 5.f}};
            d.goal = "Parcours complet : prends de la vitesse, puis flip avant annulé";
            break;
        }
        placeCar(d, {0.f, -3000.f, kPlaceZ}, 90.f, {0.f, L(800.f, 1400.f), 0.f}, 33.f);
        d.timeLimit = L(4.f, 3.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "flip avant";
        d.goal = "Flip avant annulé : la voiture reste à plat";
        d.steps = {{"Roule tout droit", Cue::Throttle, "FC_NO_FLIP"},
                   {"Petit saut", Cue::Jump, "FC_NO_FLIP"},
                   {"Flip avant", Cue::Dodge, "FC_NO_FLIP"},
                   {"Stick vers toi tout de suite", Cue::Cancel, "FC_NO_CANCEL"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "FC_WRONG_FLIP"});
        d.checks.push_back({"cancelTicks", 0.f, L(20.f, 6.f), "FC_NO_CANCEL", "FC_LATE"});
        d.checks.push_back({"cancelStrength", L(0.5f, 0.9f), 1e9f, "FC_WEAK", ""});
        d.checks.push_back({"landedOnWheels", 0.5f, 1e9f, "FC_BAD_LANDING", ""});
        break;
    }
    case Focus::Wavedash: {
        if (full) {
            // From the floor: the player makes the small hop himself.
            placeCar(d, {0.f, -3000.f, kPlaceZ}, 90.f, {0.f, L(800.f, 1200.f), 0.f}, 0.f);
            d.timeLimit = 3.f;
            d.goal = "Parcours complet : petit saut, nez levé, flip au ras du sol";
            break;
        }
        // Held flat in the air, dropped on GO: the wheels touch about 1 s after GO at level 1 (0.6 s at level 10).
        placeCar(d, {0.f, -3000.f, L(350.f, 200.f)}, 90.f, {0.f, L(800.f, 1300.f), L(0.f, -100.f)}, 0.f);
        d.timeLimit = L(3.5f, 2.5f);
        d.passScore = passFrom(level, 80.f);
        d.nowCue = displayLevel(level) <= 3 ? 0.25f : 0.f;
        d.variant = "chute";
        d.goal = "Wavedash à la fin d'une chute";
        d.steps = {{"Lève un peu le nez", Cue::PitchBack, "WD_NO_FLIP"},
                   {"Au ras du sol : flip avant", Cue::Dodge, "WD_NO_FLIP"},
                   {"Garde l'accélérateur", Cue::None, "WD_NO_FLIP"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_LATE_JUMPED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_NO_STICK"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_CRASH_LANDING"});
        d.checks.push_back({"firstContactTicks", -1e9f, L(20.f, 10.f), "", "WD_TOO_EARLY"});
        d.checks.push_back({"pitchAtContactDeg", L(-15.f, -3.f), L(40.f, 25.f), "WD_NOSE_DOWN", "WD_NOSE_UP"});
        d.checks.push_back({"gainRatio", L(0.25f, 0.5f), 1e9f, "WD_LOW_GAIN", ""});
        break;
    }
    case Focus::ChainDash: {
        if (full) {
            placeCar(d, {0.f, -4200.f, kPlaceZ}, 90.f, {}, 0.f);
            lane(d, 0.f, -4200.f, 3800.f, 160.f);
            d.timeLimit = 7.f;
            d.endY = 3800.f;
            d.timed = true;
            d.goal = "Parcours complet : pars à l'arrêt et enchaîne les wavedashes sans boost";
            break;
        }
        placeCar(d, {0.f, -4200.f, kPlaceZ}, 90.f, {0.f, L(1000.f, 300.f), 0.f}, 0.f);
        lane(d, 0.f, -4200.f, 3800.f, 160.f);
        d.timeLimit = L(8.f, 4.5f);
        d.endY = 3800.f;  // end of the lane, well before the back wall
        d.passScore = passFrom(level, 75.f);
        d.timed = true;
        d.variant = "ligne droite";
        const int dashes = static_cast<int>(std::lround(L(2.f, 4.f)));
        d.goal = "Enchaîne au moins " + std::to_string(dashes) + " wavedashes sans boost";
        d.steps = {{"Petit saut, nez un peu levé", Cue::Jump, "WD_NO_FLIP"},
                   {"Flip avant au ras du sol", Cue::Dodge, "WD_NO_FLIP"},
                   {"Powerslide gardé, ressaute", Cue::Slide, "DRILL_CHAIN_SHORT"},
                   {"Pas de boost", Cue::None, "DRILL_CHAIN_SHORT"}};
        d.checks.push_back({"dashes", static_cast<float>(dashes), 1e9f, "DRILL_CHAIN_SHORT", ""});
        d.checks.push_back({"boostUsed", -1e9f, L(10.f, 0.f) + 0.01f, "", "CHAIN_BOOST_USED"});
        d.checks.push_back({"avgGapTicks", -1e9f, L(24.f, 10.f), "", "CHAIN_GROUND_TIME"});
        d.checks.push_back({"maxSpeed", L(1500.f, 2200.f), 1e9f, "CHAIN_SLOW", ""});
        break;
    }
    case Focus::ZapDash: {
        if (full) {
            placeCar(d, {0.f, -3000.f, kPlaceZ}, 90.f, {0.f, L(1000.f, 1400.f), 0.f}, 0.f);
            d.timeLimit = 3.f;
            d.goal = "Parcours complet : petit saut, nez baissé, flip au rebond";
            break;
        }
        // Held nose a little down in the air, dropped on GO: contact 0.8 s after GO at level 1 (0.34 s at level 10).
        placeCar(d, {0.f, -3000.f, L(300.f, 140.f)}, 90.f, {0.f, L(1000.f, 1400.f), L(-100.f, -250.f)}, 0.f, -10.f);
        d.timeLimit = L(3.5f, 2.5f);
        d.passScore = passFrom(level, 80.f);
        d.variant = "réception";
        d.goal = "Zap dash : flip avant au rebond de la réception";
        d.steps = {{"Nez un peu vers le bas", Cue::Land, "ZAP_NO_FLIP"},
                   {"Au rebond : flip avant", Cue::Dodge, "ZAP_NO_FLIP"},
                   {"Garde l'accélérateur", Cue::None, "ZAP_NO_FLIP"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ZAP_NO_BOUNCE"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_LATE_JUMPED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_NO_STICK"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WD_CRASH_LANDING"});
        d.checks.push_back({"gainRatio", L(0.25f, 0.5f), 1e9f, "WD_LOW_GAIN", ""});
        break;
    }
    case Focus::WallDash: {
        if (full) {
            // On the floor, heading for the side wall and forward: the player climbs it himself.
            const float sx = alt > 0 ? 1.f : -1.f;  // +1: wall x = -4096
            const float yaw = alt > 0 ? 135.f : 45.f;
            placeCar(d, {-3000.f * sx, -3000.f, kPlaceZ}, yaw, heading(yaw, 1000.f), 33.f);
            wallBand(d, -4096.f * sx, 350.f, 650.f);
            d.timeLimit = L(8.f, 6.f);
            d.runDashes = static_cast<int>(std::lround(L(1.f, 3.f)));
            d.variant = alt > 0 ? "parcours complet, mur droit" : "parcours complet, mur gauche";
            d.goal = "Parcours complet : monte sur le mur, longe-le, puis wall dash";
            break;
        }
        // Wall at x = -4096 (car up = +X, roll -90) or x = +4096 (up = -X, roll +90). Roll sign UNVERIFIED in game.
        const float wallX = alt > 0 ? -4096.f : 4096.f;
        const float carX = alt > 0 ? wallX + kPlaceZ : wallX - kPlaceZ;
        const float z = L(700.f, 500.f);
        placeCar(d, {carX, -2500.f, z}, 90.f, {0.f, L(600.f, 1000.f), 0.f}, 0.f, 0.f, alt > 0 ? -90.f : 90.f);
        wallBand(d, wallX, z - 150.f, z + 150.f);
        d.timeLimit = L(6.f, 4.f);
        d.passScore = passFrom(level, 80.f);
        d.runDashes = static_cast<int>(std::lround(L(1.f, 3.f)));
        d.variant = alt > 0 ? "mur droit" : "mur gauche";
        d.goal = d.runDashes > 1 ? "Enchaîne " + std::to_string(d.runDashes) + " wall dashes sans tomber du mur"
                                 : "Un wall dash qui accroche le mur";
        d.steps = {{"Tape saut", Cue::Jump, "WALL_NO_DASH"},
                   {"Flip vers l'avant, côté mur", Cue::Dodge, "WALL_NO_DASH"},
                   {"Recommence", Cue::None, "WALL_NO_DASH"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "WALL_FELL_OFF"});
        d.checks.push_back({"jumpToDodgeTicks", -1e9f, L(30.f, 11.f), "", "WALL_TAP_SLOW"});
        d.checks.push_back({"gainRatio", L(0.2f, 0.5f), 1e9f, "WALL_LOW_GAIN", ""});
        break;
    }
    case Focus::FastAerial: {
        if (full) {
            // Lob from the centre toward the player (apex about 1600 high, 2.15 s after GO, above y -1938).
            placeCar(d, {0.f, -4600.f, kPlaceZ}, 90.f, {}, 100.f);
            placeBall(d, {0.f, 0.f, kBallZ}, {0.f, -900.f, 1400.f});
            d.target = {0.f, -1938.f, 1600.f};
            d.timeLimit = 5.5f;
            d.goal = "Parcours complet : place-toi sous la balle, décolle vite, touche-la en l'air";
            break;
        }
        placeCar(d, {0.f, -2500.f, kPlaceZ}, 90.f, {}, 100.f);
        d.needTouch = true;
        d.variant = "balle haute";
        d.steps = {{"Saut tenu, boost, stick vers toi", Cue::Jump, "FA_NO_JUMP"},
                   {"Lâche, rappuie, stick au centre", Cue::DoubleJump, "FA_NO_DOUBLE_JUMP"},
                   {"Vise la balle", Cue::Touch, "DRILL_NO_TOUCH"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "FA_ACCIDENTAL_DODGE"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "FA_NO_DOUBLE_JUMP"});
        if (displayLevel(level) <= 5) {
            // The ball waits, still in the air, until the car touches it: 1400 ahead and 800 high at level 1,
            // 1100 / 1100 at level 5.
            const float t = within(level, 1, 5);
            placeBall(d, {0.f, -2500.f + lerpf(1400.f, 1100.f, t), lerpf(800.f, 1100.f, t)});
            d.hangBall = true;
            d.timeLimit = lerpf(4.f, 3.5f, t);
            d.touchWindow = 3.5f;
            d.passScore = 0.f;
            d.requireDetector = false;
            d.goal = "Décolle vite et touche la balle qui t'attend en l'air";
            d.checks.push_back({"timeToBall", -1e9f, lerpf(3.5f, 2.5f, t), "", "DRILL_SLOW_TO_BALL"});
        } else {
            // Released from rest on GO, 1500 high: touch it before it falls under 5 m (7 m at level 10).
            const float t = within(level, 6, 10);
            placeBall(d, {0.f, -1100.f, 1500.f});
            d.timeLimit = 3.f;
            d.touchWindow = 3.f;
            d.passScore = lerpf(60.f, 85.f, t);
            d.goal = "Décolle vite et touche la balle avant qu'elle tombe";
            d.checks.push_back({"ballZAtTouch", lerpf(500.f, 700.f, t), 1e9f, "FA_TOUCH_LOW", ""});
        }
        break;
    }
    case Focus::Stall: {
        if (full) {
            placeCar(d, {0.f, -1000.f, kPlaceZ}, 90.f, {}, 33.f);
            d.timeLimit = 3.f;
            d.goal = "Parcours complet : un seul saut, puis le stall en haut du saut";
            break;
        }
        // Held in the air, released rising on GO: the floor comes 2.3 s after GO at level 1 (1.6 s at level 10).
        placeCar(d, {0.f, -1000.f, L(1000.f, 500.f)}, 90.f, {0.f, 500.f, L(300.f, 200.f)}, 33.f);
        d.timeLimit = L(4.f, 3.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "en l'air";
        d.goal = "Fais flotter la voiture avec un stall";
        d.steps = {{"Stick d'un côté, air roll de l'autre", Cue::Roll, "ST_NO_STALL"},
                   {"Saut", Cue::Dodge, "ST_NO_STALL"},
                   {"Laisse flotter", Cue::None, "ST_SHORT_HOVER"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_NO_FLIP"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_YAW_ROLL_MISMATCH"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "ST_PITCH_LEAK"});
        d.checks.push_back({"hoverTicks", L(8.f, 40.f), 1e9f, "ST_SHORT_HOVER", ""});
        break;
    }
    case Focus::Helicopter: {
        if (full) {
            placeCar(d, {0.f, -1000.f, kPlaceZ}, 90.f, {}, 100.f);
            d.timeLimit = 5.f;
            d.goal = "Parcours complet : décolle, tourne, puis retombe sur les roues";
            break;
        }
        placeCar(d, {0.f, -1000.f, 1000.f}, 90.f, {0.f, 300.f, 450.f}, 33.f);
        d.timeLimit = L(4.5f, 3.5f);
        d.passScore = passFrom(level, 85.f);
        d.needLanding = level >= 0.8f;  // top levels: back on the wheels after the spin
        d.variant = "en l'air";
        d.goal = "Fais tourner la voiture en hélicoptère";
        d.steps = {{"Air roll d'un côté", Cue::Roll, "AS_NO_SPIN"},
                   {"Stick du même côté", Cue::Yaw, "AS_NO_SPIN"},
                   {"Garde, puis roues au sol", Cue::None, "AS_SHORT"}};
        d.checks.push_back({"durationTicks", L(60.f, 150.f), 1e9f, "AS_SHORT", ""});
        d.checks.push_back({"axisStability", L(0.5f, 0.85f), 1e9f, "AS_UNSTABLE_AXIS", ""});
        d.checks.push_back({"avgSpinRate", L(1.5f, 3.5f), 1e9f, "AS_SLOW_SPIN", ""});
        if (d.needLanding) d.checks.push_back({"landedOnWheels", 0.5f, 1e9f, "RC_CRASHED", ""});
        break;
    }
    case Focus::Recovery: {
        if (full) {
            // Toward the side wall: climb it, jump off it, land on the wheels.
            const float sx = alt > 0 ? 1.f : -1.f;
            placeCar(d, {-2500.f * sx, -2000.f, kPlaceZ}, alt > 0 ? 180.f : 0.f, {}, 50.f);
            wallBand(d, -4096.f * sx, L(400.f, 700.f), L(400.f, 700.f) + 200.f);
            d.timeLimit = 8.f;
            d.variant = alt > 0 ? "parcours complet, mur droit" : "parcours complet, mur gauche";
            d.goal = "Parcours complet : monte sur le mur, saute, retombe sur les roues";
            break;
        }
        // Random throw kept inside the field (no wall or goal before the landing at any level).
        const float yaw = rng.range(-180.f, 180.f);
        const float pitch = L(20.f, 160.f) * (rng.chance(0.5f) ? 1.f : -1.f);
        const float roll = L(30.f, 150.f) * (rng.chance(0.5f) ? 1.f : -1.f);
        const float dir = rng.range(-kPi, kPi);
        const float hs = L(400.f, 900.f);
        placeCar(d, {rng.range(-1000.f, 1000.f), rng.range(-2000.f, -200.f), L(500.f, 900.f)}, yaw,
                 {std::cos(dir) * hs, std::sin(dir) * hs, L(400.f, 700.f)}, 33.f, pitch, roll);
        const float w = L(0.f, 4.f);
        d.car.angVel = {rng.range(-w, w), rng.range(-w, w), rng.range(-w, w)};
        d.countdown = 1.5f;
        d.timeLimit = L(5.f, 4.f);
        d.passScore = passFrom(level, 85.f);
        d.variant = "lancer aléatoire";
        d.goal = "Retombe sur les roues sans perdre ta vitesse";
        d.steps = {{"Roues vers le sol", Cue::Land, "RC_TILTED"},
                   {"Nez dans le sens du mouvement", Cue::None, "RC_TILTED"},
                   {"Lâche tout, accélère", Cue::None, "RC_TILTED"}};
        d.checks.push_back({nullptr, -1e9f, 1e9f, "RC_CRASHED"});
        d.checks.push_back({nullptr, -1e9f, 1e9f, "RC_ON_ROOF"});
        d.checks.push_back({"tiltAtTouchDeg", -1e9f, L(45.f, 15.f), "", "RC_TILTED"});
        d.checks.push_back({"velocityAlignDeg", -1e9f, L(90.f, 30.f), "", "RC_MISALIGNED"});
        d.checks.push_back({"speedRetained", L(0.4f, 0.85f), 1e9f, "RC_SPEED_LOST", ""});
        break;
    }
    case Focus::Powerslide: {
        if (full) {
            placeCar(d, {0.f, -3500.f, kPlaceZ}, 90.f, {}, 33.f);
            placeBall(d, {static_cast<float>(alt) * 1200.f, -1500.f, kBallZ});
            d.timeLimit = 6.f;
            d.variant = alt > 0 ? "parcours complet, balle à gauche" : "parcours complet, balle à droite";
            d.goal = "Parcours complet : prends de la vitesse, puis virage serré vers la balle";
            break;
        }
        placeCar(d, {0.f, -2500.f, kPlaceZ}, 90.f, {0.f, L(900.f, 1600.f), 0.f}, 33.f);
        placeBall(d, {static_cast<float>(alt) * L(1000.f, 700.f), -1700.f, kBallZ});
        d.countdown = 1.5f;
        d.timeLimit = L(4.5f, 3.5f);
        d.passScore = passFrom(level, 80.f);
        d.needTouch = true;
        d.touchWindow = L(3.f, 2.f);
        d.variant = alt > 0 ? "balle à gauche" : "balle à droite";
        d.goal = "Vire serré vers la balle sans perdre ta vitesse, puis touche-la";
        d.steps = {{"Tourne vers la balle", Cue::Steer, "PS_NO_SLIDE"},
                   {"Powerslide court", Cue::Slide, "PS_NO_SLIDE"},
                   {"Relâche, nez sur la balle", Cue::None, "PS_NO_SLIDE"},
                   {"Touche la balle", Cue::Touch, "DRILL_NO_TOUCH"}};
        d.checks.push_back({"aimErrorDeg", -1e9f, L(25.f, 10.f), "", "PS_NOT_AIMED"});
        d.checks.push_back({"speedRetained", L(0.5f, 0.85f), 1e9f, "PS_SPEED_LOSS", ""});
        d.checks.push_back({"exitSlipDeg", -1e9f, L(40.f, 12.f), "", "PS_EXIT_SLIP"});
        break;
    }
    case Focus::Boost: {
        if (full) {
            placeCar(d, {-3500.f, -4500.f, kPlaceZ}, 45.f, {}, 33.f);
            d.timeLimit = 8.f;
            d.timed = true;
            d.goal = "Parcours complet : pars à l'arrêt, accélérateur, boost, puis lâche-le au max";
            break;
        }
        // Below supersonic on GO: boost is needed to get there, and wasted once there.
        const float v0 = L(1600.f, 1450.f);
        placeCar(d, {-3500.f, -4500.f, kPlaceZ}, 45.f, heading(45.f, v0), 50.f);
        d.countdown = 1.5f;
        d.timeLimit = 4.5f;  // before the side wall, a gentle turn is allowed
        d.passScore = 0.f;
        d.requireDetector = false;  // the window is judged on its own numbers
        d.boostWindow = true;
        d.boostNeeded = (k::kSupersonicStartSpeed - v0) / k::kBoostAccelGround * k::kBoostUsedPerSecond;
        d.timed = true;
        d.variant = "vers le supersonique";
        d.goal = "Deviens supersonique, puis restes-y sans gaspiller de boost";
        d.steps = {{"Accélérateur toujours", Cue::Throttle, "SM_SLOW_TO_SUPERSONIC"},
                   {"Boost jusqu'au max", Cue::Boost, "SM_SLOW_TO_SUPERSONIC"},
                   {"Lâche le boost au max", Cue::None, "SM_BOOST_AT_MAX"}};
        d.checks.push_back({"timeToSupersonic", -1e9f, L(3.f, 2.f), "", "SM_SLOW_TO_SUPERSONIC"});
        d.checks.push_back({"boostWastedAtMax", -1e9f, L(5.f, 1.5f), "", "SM_BOOST_AT_MAX"});
        d.checks.push_back({"supersonicAfterRatio", L(0.8f, 0.9f), 1e9f, "SM_NOT_SUPERSONIC", ""});
        d.checks.push_back({"boostUsed", -1e9f, d.boostNeeded + L(15.f, 4.f), "", "SM_TOO_MUCH_BOOST"});
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
