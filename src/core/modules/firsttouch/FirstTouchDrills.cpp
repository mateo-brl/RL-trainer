#include "core/modules/firsttouch/FirstTouchDrills.h"

#include <cmath>

#include "core/common/Controls.h"

namespace rlt::ft {

namespace {
constexpr float kBallZ = 93.15f;      // ball centre resting on the floor
constexpr float kCarRestZ = 17.01f;   // car origin at rest on the floor (Octane)
constexpr float kGravity = 650.f;

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

const char* drillDescription(DrillType t) {
    switch (t) {
    case DrillType::RollIn:
        return "La balle roule vers toi. Amortis-la sans la repousser : freine, recule ou fais demi-tour pour "
               "caler ta vitesse sur la sienne.";
    case DrillType::RollAway:
        return "La balle part devant toi. Rattrape-la et touche-la à la même vitesse qu'elle pour la garder "
               "collée au capot.";
    case DrillType::Pass:
        return "Une passe arrive par le côté. Coupe sa trajectoire et accompagne-la dans sa direction. "
               "Au niveau élevé, la passe rebondit.";
    case DrillType::Lob:
        return "Un lob retombe devant toi. Place-toi dessous et amortis-la sur le capot ou juste après le rebond.";
    case DrillType::Wall:
        return "La balle tombe du mur latéral le plus proche. Lis la sortie du mur et récupère-la proprement.";
    case DrillType::High:
        return "Une balle haute devant toi. Va la chercher en l'air, touche-la par-dessous et garde-la près de toi.";
    case DrillType::Mix:
        return "Un exercice différent à chaque essai : la meilleure façon de rendre ta première touche fiable en match.";
    }
    return "";
}

std::string drillInstruction(DrillType t, const PlayerProfile& profile, KeyStyle style) {
    switch (t) {
    case DrillType::RollIn:
        return "Freine avant la balle, sans " + actionPrompt(profile, GameAction::Boost, style) + ", et laisse-la venir.";
    case DrillType::RollAway: return "Rattrape la balle, puis roule à sa vitesse avant de la toucher.";
    case DrillType::Pass: return "Tourne dans le sens de la passe et touche-la en roulant avec elle.";
    case DrillType::Lob: return "Place-toi où la balle tombe et laisse-la tomber sur ton capot.";
    case DrillType::Wall: return "Attends la balle où elle retombe et récupère-la au sol.";
    case DrillType::High:
        return "Place-toi sous la balle, saute avec " + actionPrompt(profile, GameAction::Jump, style) +
               " et touche-la par-dessous.";
    case DrillType::Mix: return "Regarde d'où vient la balle, puis touche-la en douceur.";
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
    return "Débutant : commence par Roulante vers toi, puis Roulante qui s'éloigne. Passe aux exercices "
           "Intermédiaire quand tu tiens le niveau 5.";
}

std::vector<std::string> drillSteps(DrillType t) {
    switch (t) {
    case DrillType::RollIn:
        return {"Avance vers la balle sans boost.", "Ralentis jusqu'à presque t'arrêter avant qu'elle arrive.",
                "Laisse-la toucher ton nez : elle s'arrête au lieu de rebondir loin.",
                "Repars doucement avec la balle devant toi."};
    case DrillType::RollAway:
        return {"Accélère pour rattraper la balle.", "Ralentis quand tu arrives juste derrière elle.",
                "Touche-la avec le nez en roulant à sa vitesse : elle reste devant toi.",
                "Garde la même vitesse qu'elle."};
    case DrillType::Pass:
        return {"Regarde d'où vient la passe et où elle va.", "Tourne pour rouler dans la même direction que la balle.",
                "Touche-la quand vous allez dans le même sens : de travers, elle part sur le côté.",
                "Continue avec elle devant toi."};
    case DrillType::Lob:
        return {"Regarde où la balle va tomber.", "Va à cet endroit et attends-la presque à l'arrêt.",
                "Laisse-la tomber sur ton capot, ou touche-la juste après son rebond.", "Repars doucement avec elle."};
    case DrillType::Wall:
        return {"Regarde la balle quitter le mur.", "Place-toi où elle va retomber, sans foncer.",
                "Touche-la doucement après son rebond au sol.", "Garde-la devant toi."};
    case DrillType::High:
        return {"Place-toi sous la balle, un peu en arrière.", "Saute quand elle arrive à ta portée.",
                "Touche-la par-dessous avec le nez, en douceur.", "Retombe avec la balle devant toi."};
    case DrillType::Mix:
        return {"Regarde d'où vient la balle.", "Choisis : freiner, la rattraper ou te placer dessous.",
                "Touche-la en douceur.", "Garde-la devant toi."};
    }
    return {};
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

CarPlacement carPlacementFor(const DrillPose& pose, bool fullBoost) {
    CarPlacement c;
    c.pos = pose.pos;
    c.rot = rotFromYawDeg(pose.yawDeg);
    c.vel = Vec3{};
    c.angVel = Vec3{};
    if (fullBoost) c.boost = 100.f;
    return c;
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

// ---- Spawns ---------------------------------------------------------------------------------------------
DrillSpawn makeSpawn(DrillType type, const DrillPose& pose, float level, Rng& rng) {
    const float l = clamp01(level);
    DrillSpawn out;
    out.type = type == DrillType::Mix ? DrillType::RollIn : type;
    BallPlacement& b = out.ball;
    const Vec3& P = pose.pos;
    switch (out.type) {
    case DrillType::RollIn: {
        // Ball rolling at the car from the front, wider angles and faster with the level.
        const float dist = band(rng, 1500.f, 1900.f, 1700.f, 2500.f, l);
        const float lat = randomSign(rng) * rng.range(0.f, lerpLevel(150.f, 700.f, l));
        Vec3 s = P + pose.fwd * dist + pose.right * lat;
        s.z = kBallZ;
        s = clampBallToArena(s);
        const float miss = rng.range(-1.f, 1.f) * lerpLevel(0.f, 260.f, l);
        const Vec3 target = P + pose.right * miss;
        const Vec3 dir = normalize(flat(target - s), -pose.fwd);
        const float speed = band(rng, 350.f, 800.f, 550.f, 1400.f, l);
        b.pos = s;
        b.vel = dir * speed;
        break;
    }
    case DrillType::RollAway: {
        // Ball rolling away in front of the car: chase it and match its speed.
        const float side = randomSign(rng);
        Vec3 s = P + pose.fwd * band(rng, 350.f, 450.f, 500.f, 650.f, l) + pose.right * side * rng.range(0.f, lerpLevel(0.f, 300.f, l));
        s.z = kBallZ;
        s = clampBallToArena(s);
        const float angle = side * rng.range(lerpLevel(0.f, 10.f, l), lerpLevel(8.f, 35.f, l));
        const float speed = band(rng, 450.f, 800.f, 650.f, 1500.f, l);
        b.pos = s;
        b.vel = dirInPose(pose, angle) * speed;
        break;
    }
    case DrillType::Pass: {
        // Pass crossing in front of the car, meeting point ahead; bouncing pass at high levels.
        const float side = randomSign(rng);
        const Vec3 meet = P + pose.fwd * band(rng, 900.f, 1000.f, 1100.f, 1500.f, l);
        const float flight = lerpLevel(2.2f, 1.5f, l);
        const float speed = band(rng, 400.f, 700.f, 700.f, 1300.f, l);
        const float beta = rng.range(-lerpLevel(0.f, 20.f, l), lerpLevel(5.f, 35.f, l)) * kDegToRad;
        const Vec3 dir = normalize(pose.right * (-side * std::cos(beta)) + pose.fwd * std::sin(beta), pose.right * -side);
        Vec3 s = meet - dir * std::min(2600.f, speed * flight);
        s.z = kBallZ;
        b.pos = clampBallToArena(s);
        b.vel = dir * speed;
        if (l > 0.5f) b.vel.z = rng.range(0.f, (l - 0.5f) * 2.f * 450.f);
        break;
    }
    case DrillType::Lob: {
        // Lob thrown from the ground so that it lands in front of the car after an apex.
        const Vec3 land = P + pose.fwd * band(rng, 800.f, 1000.f, 900.f, 1400.f, l) +
                          pose.right * randomSign(rng) * rng.range(0.f, lerpLevel(0.f, 400.f, l));
        const float apex = band(rng, 450.f, 700.f, 700.f, 1250.f, l);
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
        // Ball high on the nearest side wall, pushed off it: it falls back into the field.
        const float sigma = P.x >= 0.f ? 1.f : -1.f;
        const float ySign = pose.fwd.y >= 0.f ? 1.f : -1.f;
        const float y = clampf(P.y + ySign * band(rng, 700.f, 900.f, 900.f, 1500.f, l), -3600.f, 3600.f);
        const float z = band(rng, 450.f, 700.f, 600.f, 1100.f, l);
        b.pos = Vec3{sigma * 3985.f, y, z};
        b.vel = Vec3{-sigma * band(rng, 150.f, 250.f, 250.f, 600.f, l),
                     -ySign * rng.range(0.f, lerpLevel(0.f, 400.f, l)),
                     band(rng, 0.f, 100.f, 100.f, 400.f, l)};
        break;
    }
    case DrillType::High: {
        // High ball in front of the car, still rising: aerial reception.
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

DrillType pickMixType(Rng& rng, DrillType previous) {
    for (int tries = 0; tries < 4; ++tries) {
        const auto t = static_cast<DrillType>(rng.rangeInt(0, kDrillKinds - 1));
        if (t != previous) return t;
    }
    return static_cast<DrillType>((static_cast<int>(previous) + 1) % kDrillKinds);
}

} // namespace rlt::ft
