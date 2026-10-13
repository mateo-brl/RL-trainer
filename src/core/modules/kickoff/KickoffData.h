#pragma once
// Soccar kickoff facts used by the kickoff module: spawn table (blue side, orange = point reflection),
// straight paths to the ball, boost pads on those paths and benchmark arrival times per level.
// Sources and caveats: research/kickoff.md (RLBot "Useful Game Values", RocketSim RLConst.h, recordings).
//
// Frame: Rocket League is left-handed. +Y points to the orange goal and +X is the LEFT side as seen by blue,
// so a car facing +Y has its right side towards -X (Basis::right from basisFromRot agrees).

#include <string>
#include <vector>

#include "core/common/Math.h"

namespace rlt {

// ---- Constants ---------------------------------------------------------------------------------------------
constexpr float kKoSpawnBoost = 100.f / 3.f;      // RocketSim BOOST_SPAWN_AMOUNT
constexpr float kKoBoostPerSecond = 100.f / 3.f;  // RocketSim BOOST_USED_PER_SECOND
constexpr float kKoCarPlaceZ = 20.f;              // placed slightly above rest, settles during the countdown
constexpr float kKoCarRestZ = 17.f;               // RocketSim CAR_SPAWN_REST_Z (Octane 17.01)
constexpr float kKoBallRestZ = 93.15f;            // ball at rest (RocketSim; RLBot lists 92.75)
constexpr float kKoContactDist = 158.f;           // Octane origin to ball centre (2D) at a grounded nose touch
constexpr float kKoSmallPadRadius = 144.f;        // pickup radius of the car origin (2D)
constexpr float kKoBigPadRadius = 208.f;
constexpr int kKoSpawnCount = 5;
constexpr int kKoLevelCount = 4;

// ---- Spawns ------------------------------------------------------------------------------------------------
// Names are from the owning team's point of view.
enum class KickoffSpawn : int {
    DiagonalLeft = 0,
    DiagonalRight = 1,
    OffCenterLeft = 2,
    OffCenterRight = 3,
    BackCenter = 4
};
enum class KickoffSpawnKind : int { Diagonal = 0, OffCenter = 1, BackCenter = 2 };
enum class KickoffTeam : int { Blue = 0, Orange = 1 };

struct KickoffSpawnInfo {
    KickoffSpawn id;
    KickoffSpawnKind kind;
    const char* key;        // stable id for commands and persistence ("diagonal_left")
    const char* name;       // French UI label ("Diagonale gauche")
    const char* nameLower;  // for titles ("diagonale gauche")
    float x, y;             // blue team, uu
    float yawDeg;           // blue team
};

const KickoffSpawnInfo& kickoffSpawnInfo(KickoffSpawn s);
inline KickoffSpawnKind kickoffKind(KickoffSpawn s) { return kickoffSpawnInfo(s).kind; }
inline int kickoffIndex(KickoffSpawn s) { return static_cast<int>(s); }
// Clamped conversion from an index (settings, persistence).
KickoffSpawn kickoffSpawnFromIndex(int i);
// Accepts the stable keys and a few aliases ("diag_left", "back_center", "centre"...). Case-insensitive.
bool kickoffSpawnFromKey(const std::string& key, KickoffSpawn& out);
// Skill suffix used in AttemptResult::skill ("diagonal", "decale", "centre").
const char* kickoffKindSkill(KickoffSpawnKind k);
const char* kickoffKindName(KickoffSpawnKind k);  // "Diagonale", "Décalé", "Centre"

inline KickoffTeam kickoffOpponent(KickoffTeam t) {
    return t == KickoffTeam::Blue ? KickoffTeam::Orange : KickoffTeam::Blue;
}
// +1 when the team attacks +Y (blue), -1 for orange.
inline float kickoffAttackSign(KickoffTeam t) { return t == KickoffTeam::Blue ? 1.f : -1.f; }

struct KickoffPose {
    Vec3 pos;  // z = placement height
    float yawRad = 0.f;
    Rot rot;  // Unreal rotator (pitch 0, roll 0)
};
// Spawn pose for a team. Orange is the point reflection (x, y) -> (-x, -y), yaw - 180 deg.
KickoffPose kickoffPose(KickoffSpawn s, KickoffTeam team, float z = kKoCarPlaceZ);

// Finds the kickoff spawn (any team) whose 2D position is within `tolerance` of `pos`.
bool kickoffFindSpawn(const Vec3& pos, float tolerance, KickoffSpawn& spawn, KickoffTeam& team);

// ---- Straight path to the ball -----------------------------------------------------------------------------
struct KickoffPath {
    Vec3 start;                  // spawn, z = 0
    Vec3 dir{0.f, 1.f, 0.f};     // unit, 2D
    Vec3 right{-1.f, 0.f, 0.f};  // unit, 2D, right of the travel direction
    float length = 0.f;          // to the contact point (spawn to ball 2D minus kKoContactDist)

    Vec3 pointAt(float s) const {
        Vec3 p = start + dir * s;
        p.z = kKoCarRestZ;
        return p;
    }
    float along(const Vec3& p) const { return dot(flat(p - start), dir); }
    // Signed lateral offset (2D): + = right of the travel direction.
    float lateral(const Vec3& p) const { return dot(flat(p - start), right); }
};
KickoffPath kickoffPath(KickoffSpawn s, KickoffTeam team, float contactDist = kKoContactDist);

// ---- Boost pads ----------------------------------------------------------------------------------------------
struct KickoffPad {
    Vec3 pos;
    bool big = false;
};
const std::vector<KickoffPad>& kickoffSoccarPads();
struct KickoffPadOnPath {
    int padIndex = 0;
    float along = 0.f;
    float lateral = 0.f;
    float amount = 12.f;
};
// Pads a car driving the straight line would pick up (car origin within the pickup radius).
std::vector<KickoffPadOnPath> kickoffPadsOnPath(const KickoffPath& path, float extraTolerance = 0.f);

// ---- Benchmarks: time from GO to the first touch and contact speed ------------------------------------------
enum class KickoffLevel : int { Beginner = 0, Standard = 1, Pro = 2, Elite = 3 };
struct KickoffBenchmark {
    float arrive = 1.95f;         // s from GO to contact
    float contactSpeed = 2200.f;  // uu/s
};
KickoffLevel kickoffLevelFromIndex(int i);
const char* kickoffLevelName(KickoffLevel l);  // "Débutant", "Standard", "Pro", "Élite"
KickoffBenchmark kickoffBenchmark(KickoffSpawnKind k, KickoffLevel l);
inline KickoffBenchmark kickoffBenchmark(KickoffSpawn s, KickoffLevel l) { return kickoffBenchmark(kickoffKind(s), l); }
// Continuous difficulty 0..1 (0 = Débutant, 1/3 = Standard, 2/3 = Pro, 1 = Élite), linear in between.
KickoffBenchmark kickoffBenchmarkAt(KickoffSpawnKind k, float level01);

} // namespace rlt
