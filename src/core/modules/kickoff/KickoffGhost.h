#pragma once
// Ghost opponent of the kickoff drill and the 50/50 resolver.
//
// The ghost runs on the straight line from the mirrored spawn (same slot, opponent team) to its contact point.
// It needs no physics: an accel-then-cruise profile through a benchmark (arrival time, contact speed), a
// constant-acceleration fallback, or the replay of a recorded "distance along the path" profile (personal best).
// The duel rule and the ball impulses are heuristics (tunable, UNVERIFIED against real 50/50s, see kickoff.md 5.2).

#include <vector>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"
#include "core/modules/kickoff/KickoffData.h"

namespace rlt {

enum class KickoffGhostModel : int { AccelCruise = 0, ConstAccel = 1, Samples = 2 };

struct KickoffGhostProfile {
    KickoffGhostModel model = KickoffGhostModel::AccelCruise;
    float arrive = 1.95f;         // s from GO to contact
    float contactSpeed = 2200.f;  // uu/s at contact (used by the duel even for Samples)
    float reaction = 0.f;         // s of standstill after GO
    std::vector<float> samples;   // Samples: distance along the path at t = i * sampleDt (t < arrive)
    float sampleDt = 0.1f;

    static KickoffGhostProfile fromBenchmark(const KickoffBenchmark& b) {
        KickoffGhostProfile p;
        p.arrive = b.arrive;
        p.contactSpeed = b.contactSpeed;
        return p;
    }
    // Recorded profile: samples every `dt` seconds before the arrival, then exactly the path end at `arrive`.
    static KickoffGhostProfile fromSamples(std::vector<float> s, float dt, float arrive, float contactSpeed) {
        KickoffGhostProfile p;
        p.model = KickoffGhostModel::Samples;
        p.samples = std::move(s);
        p.sampleDt = dt > 1e-4f ? dt : 0.1f;
        p.arrive = arrive;
        p.contactSpeed = contactSpeed;
        return p;
    }
};

struct KickoffGhostState {
    Vec3 pos;  // car origin (z = rest height)
    Vec3 vel;
    float yawRad = 0.f;
    float along = 0.f;
    bool arrived = false;
};

class KickoffGhost {
public:
    KickoffGhost() = default;
    KickoffGhost(KickoffSpawn spawn, KickoffTeam team, KickoffGhostProfile profile) {
        init(spawn, team, std::move(profile));
    }

    void init(KickoffSpawn spawn, KickoffTeam team, KickoffGhostProfile profile);

    float arrivalTime() const { return arrive_; }
    float contactSpeed() const { return contactSpeed_; }
    const KickoffPath& path() const { return path_; }
    const KickoffGhostProfile& profile() const { return prof_; }
    KickoffGhostModel model() const { return model_; }

    float along(float t) const;       // distance along the path at t (s after GO), clamped to the path length
    float speedAlong(float t) const;  // speed along the path at t
    float timeAt(float s) const;      // inverse of along()
    KickoffGhostState stateAt(float t) const;

private:
    float sampleAlong(float t) const;

    KickoffPath path_{};
    KickoffGhostProfile prof_{};
    KickoffGhostModel model_ = KickoffGhostModel::AccelCruise;
    float arrive_ = 0.f, accel_ = 0.f, vmax_ = 0.f, yaw_ = 0.f, contactSpeed_ = 0.f;
};

// ---- 50/50 resolver ------------------------------------------------------------------------------------------
enum class KickoffDuel : int { PlayerClear = 0, PlayerNarrow = 1, Neutral = 2, GhostNarrow = 3, GhostClear = 4 };
const char* kickoffDuelName(KickoffDuel d);  // "Kickoff gagné (net)"...
inline bool kickoffDuelWon(KickoffDuel d) { return d == KickoffDuel::PlayerClear || d == KickoffDuel::PlayerNarrow; }

struct KickoffDuelParams {
    float clearWindow = 0.10f;   // |dt| beyond which the earlier car wins outright (s, 12 ticks)
    float timeToSpeed = 4000.f;  // uu/s of "power" per second of time advantage inside the window
    float neutralBand = 0.06f;   // relative power difference treated as neutral
    float ghostQuality = 0.96f;  // assumed contact quality of the ghost
};

struct KickoffPlayerContact {
    bool touched = false;
    float time = 0.f;           // s after GO
    float speedToBall = 0.f;    // velocity component toward the ball at contact (uu/s)
    float lateralOffset = 0.f;  // ball centre vs car centre line (uu)
    bool flipIntoBall = false;  // dodge started <= 0.35 s before contact
};

struct KickoffDuelResult {
    KickoffDuel outcome = KickoffDuel::GhostClear;
    float dt = 0.f;  // ghost time - player time (> 0: player first)
    float playerPower = 0.f;
    float ghostPower = 0.f;
};

float kickoffContactQuality(float lateralOffset, bool flipIntoBall);
KickoffDuelResult kickoffResolveDuel(const KickoffPlayerContact& p, float ghostArrive, float ghostSpeed,
                                     const KickoffDuelParams& prm = {});

// Ball velocity change when the (virtual) ghost hits the ball, modelled on RocketSim Ball.cpp: plastic contact
// (6:1 car:ball mass) along a (0.92 fwd, 0.38 up) normal plus the Psyonix extra impulse along the car-to-ball
// direction (z * 0.35, forward component * 0.65). Plausible estimate, UNVERIFIED in game.
float kickoffExtraImpulseFactor(float relSpeed);
Vec3 kickoffCarHitDir(const Vec3& carPos, const Vec3& carFwd, const Vec3& ballPos);
Vec3 kickoffGhostBallImpulse(const Vec3& ghostPos, const Vec3& ghostVel, const Vec3& ballPos, const Vec3& ballVel,
                             float scale = 1.f);
// Exit deflection caused by an off-centre nose hit: atan(lateral / (0.65 * forward distance)).
float kickoffLateralToExitAngleDeg(float lateralOffset, float forwardDist = kKoContactDist);
// Both cars contacted within the clear window: nudge the ball toward the outcome (ballVel = after the player's hit).
Vec3 kickoffDuelBallImpulse(KickoffDuel d, const Vec3& ballVel, const Vec3& ghostDir, const Vec3& ghostImpulseAtRest);

} // namespace rlt
