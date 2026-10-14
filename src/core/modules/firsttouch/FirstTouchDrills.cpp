#include "core/modules/firsttouch/FirstTouchDrills.h"

#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/modules/firsttouch/FirstTouchTutorials.h"

namespace rlt::ft {

namespace {
constexpr float kBallZ = 93.15f;      // ball centre resting on the floor
constexpr float kCarRestZ = 17.01f;   // car origin at rest on the floor (Octane)
constexpr float kGravity = 650.f;
constexpr float kArenaX = 4096.f;
constexpr float kArenaY = 5120.f;
constexpr float kCornerPlane = 8064.f;  // |x| + |y| of the corner walls

float lerpLevel(float easy, float hard, float level) { return easy + (hard - easy) * clamp01(level); }

// Uniform value inside the band of the current level: [L(loEasy, loHard), L(hiEasy, hiHard)].
float band(Rng& rng, float loEasy, float loHard, float hiEasy, float hiHard, float level) {
    const float lo = lerpLevel(loEasy, loHard, level);
    const float hi = lerpLevel(hiEasy, hiHard, level);
    return hi > lo ? rng.range(lo, hi) : lo;
}

float randomSign(Rng& rng) { return rng.chance(0.5f) ? 1.f : -1.f; }

// Direction in the pose plane: angle (degrees) from `fwd` towards `right`.
Vec3 dirInPose(const DrillPose& p, float angleDeg) {
    const float a = angleDeg * kDegToRad;
    return normalize(p.fwd * std::cos(a) + p.right * std::sin(a), p.fwd);
}

// Free distance from `pos` along the flat direction `d` (unit) up to the arena walls.
float freeAlong(const Vec3& pos, const Vec3& d) {
    float best = 1e9f;
    auto plane = [&](float nx, float ny, float limit) {
        const float nd = nx * d.x + ny * d.y;
        if (nd <= 1e-4f) return;
        const float t = (limit - (nx * pos.x + ny * pos.y)) / nd;
        best = std::min(best, std::max(0.f, t));
    };
    plane(1.f, 0.f, kArenaX);
    plane(-1.f, 0.f, kArenaX);
    plane(0.f, 1.f, kArenaY);
    plane(0.f, -1.f, kArenaY);
    const float k = 0.70710678f;  // corner planes |x| + |y| = 8064, normalised
    plane(k, k, kCornerPlane * k);
    plane(k, -k, kCornerPlane * k);
    plane(-k, k, kCornerPlane * k);
    plane(-k, -k, kCornerPlane * k);
    return best;
}

// Beta of the pass (degrees, > 0 = the ball also runs the way the car faces): 35 at niveau 1, 0 at niveau 6, -20 (the
// pass comes back toward the car) at niveau 10.
float passBeta(float level) {
    const float n = clamp01(level) * 9.f;  // 0 = niveau 1 .. 9 = niveau 10
    return n <= 5.f ? 35.f * (1.f - n / 5.f) : -20.f * (n - 5.f) / 4.f;
}
} // namespace

const char* drillId(DrillType t) {
    switch (t) {
    case DrillType::RollIn: return "roll_in";
    case DrillType::RollAway: return "roll_away";
    case DrillType::Pass: return "pass";
    case DrillType::Lob: return "lob";
    case DrillType::Wall: return "wall";
    case DrillType::High: return "high";
    case DrillType::Mix: return "mix";
    }
    return "mix";
}

bool parseDrillId(const std::string& id, DrillType& out) {
    for (int i = 0; i <= static_cast<int>(DrillType::Mix); ++i) {
        const auto t = static_cast<DrillType>(i);
        if (id == drillId(t)) {
            out = t;
            return true;
        }
    }
    return false;
}

const char* drillName(DrillType t) {
    switch (t) {
    case DrillType::RollIn: return "Roulante vers toi";
    case DrillType::RollAway: return "Roulante qui s'éloigne";
    case DrillType::Pass: return "Passe latérale";
    case DrillType::Lob: return "Lob / rebond devant toi";
    case DrillType::Wall: return "Chute du mur latéral";
    case DrillType::High: return "Balle haute (réception aérienne)";
    case DrillType::Mix: return "Mix aléatoire";
    }
    return "";
}

const char* drillDescription(DrillType t) { return drillTutorial(t).intro; }

std::string drillInstruction(DrillType t, const PlayerProfile& profile, KeyStyle style) {
    switch (t) {
    case DrillType::RollIn:
        return "Reste face à la balle, sans " + actionPrompt(profile, GameAction::Boost, style) +
               ", puis recule doucement au contact.";
    case DrillType::RollAway: return "Rattrape la balle, lâche l'accélérateur près d'elle et touche-la à sa vitesse.";
    case DrillType::Pass: return "Va vers le point de rencontre et tourne pour rouler dans le sens de la balle.";
    case DrillType::Lob: return "Laisse-la rebondir une fois devant toi, puis touche-la quand elle redescend.";
    case DrillType::Wall: return "Va sous le cercle sans foncer, laisse-la rebondir, puis touche-la doucement.";
    case DrillType::High:
        return "Mets-toi sous la balle, puis saute avec " + actionPrompt(profile, GameAction::Jump, style) +
               " : touche-la en haut de ton saut.";
    case DrillType::Mix: return "Lis le nom de la balle, puis fais le geste de cet exercice.";
    }
    return "";
}

Tier drillTier(DrillType t) {
    switch (t) {
    case DrillType::RollIn:
    case DrillType::RollAway: return Tier::Beginner;
    case DrillType::Pass:
    case DrillType::Lob:
    case DrillType::Wall: return Tier::Intermediate;
    case DrillType::High:
    case DrillType::Mix: return Tier::Advanced;
    }
    return Tier::Intermediate;
}

const char* tierLabel(Tier t) {
    switch (t) {
    case Tier::Beginner: return "Débutant";
    case Tier::Intermediate: return "Intermédiaire";
    case Tier::Advanced: return "Avancé";
    }
    return "";
}

const char* drillAdvice() {
    return "Débutant : commence par Roulante vers toi, puis Roulante qui s'éloigne. Passe à l'exercice suivant quand "
           "tu réussis environ 3 essais sur 4 au niveau 4.";
}

std::vector<std::string> drillSteps(DrillType t) {
    std::vector<std::string> out;
    for (const auto& s : drillTutorial(t).steps) out.emplace_back(s.title);
    return out;
}

// ---- Levels ---------------------------------------------------------------------------------------------
int playerLevel(float level) { return 1 + static_cast<int>(clamp01(level) * 9.f + 0.5f); }

float levelOfPlayer(int n) { return clamp01(static_cast<float>(n - 1) / 9.f); }

DrillCriteria drillCriteria(float level, float successScore, float controlFraction, float timeout) {
    const float l = clamp01(level);
    DrillCriteria c;
    c.firstStep = playerLevel(l) < kControlLevel;
    c.minScore = clampf(successScore - 10.f + 25.f * l, 0.f, 100.f);
    c.minControl = c.firstStep ? 0.f : clampf(controlFraction - 0.25f + 0.35f * l, 0.1f, 1.f);
    c.timeout = timeout * (1.5f - 0.5f * l);
    return c;
}

float drillRepScore(const DrillCriteria& c, const TouchScore& s, const ScoreParams& p) {
    return c.firstStep ? touchOnlyScore(s, p) : s.score;
}

bool drillRepSuccess(const DrillCriteria& c, float repScore, float controlFraction) {
    return repScore >= c.minScore && (c.minControl <= 0.f || controlFraction >= c.minControl);
}

std::string drillRepTip(const DrillCriteria& c, const std::vector<Tip>& tips) {
    for (const auto& t : tips) {
        if (c.firstStep && (t.code == "ft.lost_control" || t.code == "ft.oversteer")) continue;
        return t.text;
    }
    return {};
}

const char* drillGoal(float level) {
    return playerLevel(level) < kControlLevel ? "Objectif : une touche douce."
                                              : "Objectif : touche douce, puis garde la balle.";
}

float freezeSeconds(float level) {
    const int n = playerLevel(level);
    if (n <= 3) return 1.f;
    if (n <= 6) return 0.5f;
    return 0.f;
}

// ---- Poses ----------------------------------------------------------------------------------------------
DrillPose poseFromCar(const CarState& car) {
    DrillPose p;
    Vec3 f = flat(car.basis.fwd);
    if (length(f) < 0.2f) f = flat(car.basis.up) * -1.f;  // nose straight up / down: use the roof direction
    p.fwd = normalize(f, Vec3{1.f, 0.f, 0.f});
    p.right = Vec3{-p.fwd.y, p.fwd.x, 0.f};
    p.yawDeg = std::atan2(p.fwd.y, p.fwd.x) * kRadToDeg;
    p.pos = Vec3{clampf(car.pos.x, -3000.f, 3000.f), clampf(car.pos.y, -4000.f, 4000.f), kCarRestZ};
    return p;
}

DrillPose poseFromYaw(const Vec3& pos, float yawDeg) {
    DrillPose p;
    const float a = yawDeg * kDegToRad;
    p.fwd = Vec3{std::cos(a), std::sin(a), 0.f};
    p.right = Vec3{-p.fwd.y, p.fwd.x, 0.f};
    p.yawDeg = yawDeg;
    p.pos = Vec3{pos.x, pos.y, kCarRestZ};
    return p;
}

CarPlacement carPlacementFor(const DrillPose& pose, bool fullBoost) {
    CarPlacement c;
    c.pos = pose.pos;
    c.rot = rotFromYawDeg(pose.yawDeg);
    c.vel = Vec3{};
    c.angVel = Vec3{};
    if (fullBoost) c.boost = 100.f;
    return c;
}

float freeAhead(const DrillPose& p) { return freeAlong(p.pos, normalize(flat(p.fwd), Vec3{1.f, 0.f, 0.f})); }

DrillPose anchorPose(const DrillPose& p, int team, float anchorY) {
    const float s = teamSign(team);
    return poseFromYaw(Vec3{clampf(p.pos.x, -2000.f, 2000.f), -anchorY * s, kCarRestZ}, s > 0.f ? 90.f : -90.f);
}

DrillPose anchoredPose(const DrillPose& p, int team, float needAhead, float anchorY) {
    const bool nearSideWall = kArenaX - std::fabs(p.pos.x) < kSideWallMargin;
    if (freeAhead(p) >= needAhead && !nearSideWall) return p;
    return anchorPose(p, team, anchorY);
}

DrillPose drillPoseFor(DrillType t, const DrillPose& player, int team) {
    switch (t) {
    case DrillType::RollIn:
    case DrillType::Pass:
    case DrillType::Lob:
    case DrillType::High: return anchoredPose(player, team, kNeedAhead, kAnchorY);
    case DrillType::RollAway: return anchoredPose(player, team, kAwayNeedAhead, kAwayAnchorY);
    case DrillType::Wall:
    case DrillType::Mix: return player;
    }
    return player;
}

Vec3 clampBallToArena(const Vec3& p) {
    Vec3 q = p;
    q.x = clampf(q.x, -3990.f, 3990.f);
    q.y = clampf(q.y, -4990.f, 4990.f);
    q.z = clampf(q.z, kBallZ, 1900.f);
    const float lim = 7880.f;  // |x| + |y| corner planes (ball centre limit 7932)
    const float s = std::fabs(q.x) + std::fabs(q.y);
    if (s > lim) {
        const float k = lim / s;
        q.x *= k;
        q.y *= k;
    }
    return q;
}

Vec3 clampFloorBall(const Vec3& p) {
    Vec3 q = p;
    q.x = clampf(q.x, -3800.f, 3800.f);
    q.y = clampf(q.y, -4900.f, 4900.f);
    const float lim = 7600.f;  // away from the corner ramps too
    const float s = std::fabs(q.x) + std::fabs(q.y);
    if (s > lim) {
        const float k = lim / s;
        q.x *= k;
        q.y *= k;
    }
    q.z = std::max(q.z, kBallZ);
    return q;
}

bool ballOverFrontRoof(const CarState& car, const Vec3& ballPos) {
    if (!car.valid || car.basis.up.z < 0.7f) return false;
    const Vec3 l = car.basis.toLocal(ballPos - car.pos);
    return l.x >= 0.f && l.x <= kRoofZoneAhead && std::fabs(l.y) < kRoofZoneSide && l.z > 0.f;
}

bool wallInReach(const DrillPose& pose, float& sigma) {
    sigma = std::fabs(pose.fwd.x) > 0.2f ? (pose.fwd.x > 0.f ? 1.f : -1.f) : (pose.pos.x >= 0.f ? 1.f : -1.f);
    return kArenaX - sigma * pose.pos.x <= kWallReach;
}

// ---- Spawns ---------------------------------------------------------------------------------------------
DrillSpawn makeSpawn(DrillType type, const DrillPose& pose, float level, Rng& rng, const SpawnContext& ctx) {
    const float l = clamp01(level);
    DrillSpawn out;
    out.type = type == DrillType::Mix ? DrillType::RollIn : type;
    out.carPose = pose;
    BallPlacement& b = out.ball;
    const Vec3& P = pose.pos;
    switch (out.type) {
    case DrillType::RollIn: {
        // Ball rolling at the car from the front: niveau 1 straight at the nose from 1600 uu, wider angles, farther
        // and faster with the level.
        const float dist = band(rng, 1600.f, 1900.f, 1600.f, 2500.f, l);
        const float lat = randomSign(rng) * rng.range(0.f, lerpLevel(100.f, 700.f, l));
        Vec3 s = P + pose.fwd * dist + pose.right * lat;
        s.z = kBallZ;
        s = clampFloorBall(s);
        const float miss = rng.range(-1.f, 1.f) * lerpLevel(0.f, 260.f, l);
        const Vec3 target = P + pose.right * miss;
        const Vec3 dir = normalize(flat(target - s), -pose.fwd);
        const float speed = band(rng, 350.f, 800.f, 450.f, 1400.f, l);
        b.pos = s;
        b.vel = dir * speed;
        break;
    }
    case DrillType::RollAway: {
        // Ball rolling away in front of the car: chase it and match its speed. Niveau 1: 400 uu ahead, straight. An
        // angled ball drifts toward the side with room (never into a side wall before the chase is over).
        float side = randomSign(rng);
        if (freeAlong(P, pose.right * side) < 2500.f && freeAlong(P, pose.right * -side) > freeAlong(P, pose.right * side))
            side = -side;
        Vec3 s = P + pose.fwd * band(rng, 400.f, 500.f, 400.f, 650.f, l) +
                 pose.right * side * rng.range(0.f, lerpLevel(0.f, 300.f, l));
        s.z = kBallZ;
        s = clampFloorBall(s);
        const float angle = side * rng.range(lerpLevel(0.f, 10.f, l), lerpLevel(0.f, 35.f, l));
        const float speed = band(rng, 450.f, 800.f, 550.f, 1500.f, l);
        b.pos = s;
        b.vel = dirInPose(pose, angle) * speed;
        break;
    }
    case DrillType::Pass: {
        // Pass reaching a meeting point ahead of the car. Niveau 1: diagonal pass running partly the way the car
        // faces (beta 35 degrees), slow, 2.6 s to the meeting point; the pass turns square at niveau 6, then comes
        // back toward the car; bouncing pass at high levels.
        const Vec3 meet = P + pose.fwd * band(rng, 1000.f, 1100.f, 1000.f, 1500.f, l);
        const float flight = lerpLevel(2.6f, 1.5f, l);
        const float speed = band(rng, 400.f, 700.f, 500.f, 1300.f, l);
        const float beta = (passBeta(l) + rng.range(-1.f, 1.f) * lerpLevel(0.f, 8.f, l)) * kDegToRad;
        const float travel = std::min(2600.f, speed * flight);
        // The pass comes from the side with room for its whole run (never from inside the floor-to-wall ramp).
        float side = randomSign(rng);
        auto room = [&](float sd) { return freeAlong(meet, pose.right * sd); };
        const float need = travel * std::fabs(std::cos(beta)) + 300.f;
        if (room(side) < need && room(-side) > room(side)) side = -side;
        const Vec3 dir = normalize(pose.right * (-side * std::cos(beta)) + pose.fwd * std::sin(beta), pose.right * -side);
        Vec3 s = meet - dir * travel;
        s.z = kBallZ;
        b.pos = clampFloorBall(s);
        b.vel = dir * speed;
        if (l > 0.5f) b.vel.z = rng.range(0.f, (l - 0.5f) * 2.f * 450.f);
        break;
    }
    case DrillType::Lob: {
        // Lob thrown from the ground so that it lands in front of the car after an apex (niveau 1: 450..550 high).
        const Vec3 land = P + pose.fwd * band(rng, 800.f, 900.f, 1000.f, 1400.f, l) +
                          pose.right * randomSign(rng) * rng.range(0.f, lerpLevel(0.f, 400.f, l));
        const float apex = band(rng, 450.f, 700.f, 550.f, 1250.f, l);
        const float startZ = 150.f;
        const float vz = std::sqrt(2.f * kGravity * (apex - startZ));
        const float tUp = vz / kGravity;
        const float tDown = std::sqrt(2.f * (apex - kBallZ) / kGravity);
        const float spread = lerpLevel(20.f, 180.f, l);
        const Vec3 dirH = dirInPose(pose, 180.f + rng.range(-spread, spread));  // mostly towards the car
        const float vh = band(rng, 0.f, 200.f, 150.f, 600.f, l);
        Vec3 s = land - dirH * (vh * (tUp + tDown));
        s.z = startZ;
        b.pos = clampBallToArena(s);
        b.vel = dirH * vh;
        b.vel.z = vz;
        break;
    }
    case DrillType::Wall: {
        // Ball high on a side wall, pushed off it: it falls back into the field. The drill parks the car parallel to
        // that wall, 1100 uu from it at niveau 1 (1800 at niveau 10), facing the side with more room.
        float sigma = 0.f;
        float dirY = 0.f;
        if (ctx.placeCar) {
            sigma = ctx.wallSide != 0.f ? (ctx.wallSide > 0.f ? 1.f : -1.f) : (P.x >= 0.f ? 1.f : -1.f);
            const float carY = clampf(P.y, -1500.f, 1500.f);
            dirY = carY > 1.f ? -1.f : (carY < -1.f ? 1.f : teamSign(ctx.team));
            out.placesCar = true;
            out.carPose = poseFromYaw(Vec3{sigma * (kArenaX - lerpLevel(1100.f, 1800.f, l)), carY, kCarRestZ},
                                      dirY > 0.f ? 90.f : -90.f);
        } else {
            // The player keeps his car: the wall he faces, only when it is close enough.
            if (!wallInReach(pose, sigma)) {
                out.valid = false;
                b.pos = clampBallToArena(P + Vec3{0.f, 0.f, 300.f});
                break;
            }
            dirY = pose.fwd.y >= 0.f ? 1.f : -1.f;
        }
        const Vec3& C = out.carPose.pos;
        const float y = clampf(C.y + dirY * band(rng, 700.f, 900.f, 900.f, 1500.f, l), -3600.f, 3600.f);
        const float z = band(rng, 400.f, 650.f, 500.f, 1100.f, l);
        b.pos = Vec3{sigma * 3985.f, y, z};
        b.vel = Vec3{-sigma * band(rng, 200.f, 300.f, 250.f, 550.f, l), -dirY * rng.range(0.f, lerpLevel(0.f, 350.f, l)),
                     rng.range(0.f, lerpLevel(50.f, 350.f, l))};
        break;
    }
    case DrillType::High: {
        const int n = playerLevel(l);
        if (n <= 6) {
            // Hanging ball 1000 uu ahead, centre 345 high. Niveaux 1-3: let go by the jump (both rise together and
            // meet at the top of the jump); niveaux 4-6: let go as soon as the car is under it, the player times
            // the jump himself.
            b.pos = P + pose.fwd * kHangAhead;
            b.pos.z = kHangZ;
            b.pos = clampBallToArena(b.pos);
            b.vel = Vec3{};
            if (n <= 3) {
                out.hang = HangRelease::OnJump;
                out.releaseVz = kHangJumpVz;
            } else {
                out.hang = HangRelease::Under;
                out.releaseVz = lerpf(450.f, 365.f, static_cast<float>(n - 4) / 2.f);
            }
            break;
        }
        // Niveaux 7-10: high ball in front of the car, still rising: aerial reception.
        Vec3 s = P + pose.fwd * band(rng, 700.f, 900.f, 900.f, 1400.f, l) +
                 pose.right * randomSign(rng) * rng.range(0.f, lerpLevel(0.f, 300.f, l));
        s.z = band(rng, 350.f, 450.f, 500.f, 800.f, l);
        b.pos = clampBallToArena(s);
        const Vec3 dirH = dirInPose(pose, 180.f + rng.range(-1.f, 1.f) * lerpLevel(0.f, 40.f, l));
        b.vel = dirH * rng.range(0.f, lerpLevel(100.f, 400.f, l));
        b.vel.z = band(rng, 450.f, 650.f, 650.f, 1000.f, l);
        break;
    }
    case DrillType::Mix: break;
    }
    b.angVel = Vec3{};
    return out;
}

// ---- Mix ------------------------------------------------------------------------------------------------
bool mixUnlocked(DrillType t, float level, int successes) {
    if (t == DrillType::Mix) return false;
    return drillTier(t) == Tier::Beginner || playerLevel(level) >= kControlLevel || successes >= 5;
}

float mixWeight(float recentRate) { return 1.f / std::max(0.15f, clamp01(recentRate)); }

DrillType pickMixType(Rng& rng, DrillType previous, const std::array<float, kDrillKinds>& weights) {
    float total = 0.f, totalAll = 0.f;
    for (int i = 0; i < kDrillKinds; ++i) {
        const float w = std::max(0.f, weights[static_cast<size_t>(i)]);
        totalAll += w;
        if (static_cast<DrillType>(i) != previous) total += w;
    }
    if (totalAll <= 0.f) return DrillType::RollIn;
    const bool skipPrev = total > 0.f;
    float r = rng.uniform() * (skipPrev ? total : totalAll);
    for (int i = 0; i < kDrillKinds; ++i) {
        const auto t = static_cast<DrillType>(i);
        const float w = std::max(0.f, weights[static_cast<size_t>(i)]);
        if (w <= 0.f || (skipPrev && t == previous)) continue;
        if (r < w) return t;
        r -= w;
    }
    for (int i = kDrillKinds - 1; i >= 0; --i)  // rounding: the last eligible one
        if (weights[static_cast<size_t>(i)] > 0.f && (!skipPrev || static_cast<DrillType>(i) != previous))
            return static_cast<DrillType>(i);
    return DrillType::RollIn;
}

DrillType pickMixType(Rng& rng, DrillType previous) {
    for (int tries = 0; tries < 4; ++tries) {
        const auto t = static_cast<DrillType>(rng.rangeInt(0, kDrillKinds - 1));
        if (t != previous) return t;
    }
    return static_cast<DrillType>((static_cast<int>(previous) + 1) % kDrillKinds);
}

// ---- Light predictor ------------------------------------------------------------------------------------
BallTrack predictBall(const Vec3& pos0, const Vec3& vel0, const Vec3& angVel0, float seconds) {
    constexpr float dt = kTickDt;
    constexpr float kRadius = 91.25f;
    constexpr float kRest = 0.6f;          // ball vs arena restitution
    constexpr float kFriction = 0.35f;     // Coulomb
    constexpr float kDrag = 0.03f;
    constexpr float kMaxSpin = 6.f;
    const float lever = 0.4f * kRadius;    // I / (m R) of a solid sphere
    BallTrack tr;
    Vec3 p = pos0, v = vel0, w = angVel0;
    const int steps = std::clamp(static_cast<int>(seconds * kTickRate), 1, 1440);
    tr.points.reserve(static_cast<size_t>(steps / 2 + 2));
    tr.points.push_back(p);
    const float wallX = kArenaX - kBallZ, wallY = kArenaY - kBallZ, ceilZ = 2044.f - kBallZ;
    for (int i = 1; i <= steps; ++i) {
        v = v * (1.f - kDrag * dt);
        v.z -= kGravity * dt;
        if (v.z < 0.f && p.z + v.z * dt < kBallZ) {
            const float vn = v.z;
            const bool bounce = -vn > 15.f;
            const float vnOut = bounce ? -kRest * vn : 0.f;
            const float jn = vnOut - vn;
            const Vec3 slip{v.x - kRadius * w.y, v.y + kRadius * w.x, 0.f};
            const float sl = length(slip);
            if (sl > 1e-4f) {
                const float mag = std::min(sl * (2.f / 7.f), kFriction * jn);
                const Vec3 dvt = slip * (-mag / sl);
                v.x += dvt.x;
                v.y += dvt.y;
                w.x += dvt.y / lever;
                w.y -= dvt.x / lever;
            }
            v.z = vnOut;
            if (bounce) {
                tr.floorBounces.push_back(Vec3{p.x, p.y, kBallZ});
                tr.floorBounceTimes.push_back(static_cast<float>(i) * dt);
            }
        }
        if (std::fabs(p.x + v.x * dt) > wallX && v.x * p.x > 0.f) v.x = -kRest * v.x;
        if (std::fabs(p.y + v.y * dt) > wallY && v.y * p.y > 0.f) v.y = -kRest * v.y;
        if (v.z > 0.f && p.z + v.z * dt > ceilZ) v.z = -kRest * v.z;
        const float spin = length(w);
        if (spin > kMaxSpin) w = w * (kMaxSpin / spin);
        p += v * dt;
        if (p.z < kBallZ) p.z = kBallZ;
        if (i % 2 == 0) tr.points.push_back(p);
    }
    return tr;
}

Vec3 trackAt(const BallTrack& track, float t) {
    if (track.points.empty()) return Vec3{};
    const float f = std::max(0.f, t) * kTickRate * 0.5f;
    const size_t i = static_cast<size_t>(f);
    if (i + 1 >= track.points.size()) return track.points.back();
    const float a = f - static_cast<float>(i);
    return track.points[i] + (track.points[i + 1] - track.points[i]) * a;
}

} // namespace rlt::ft
