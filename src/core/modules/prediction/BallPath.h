#pragma once
// Predicted ball path (the module's cache) and its analysis: bounces, landing (first floor contact), apex,
// first-touch zone (first point after the apex where the ball centre enters a reachable height band) and goal.
// Everything is computed once per prediction; drawing only reads it.

#include <array>
#include <cstdint>
#include <vector>

#include "core/modules/prediction/BallPhysics.h"

namespace rlt::pr {

constexpr float kMinHorizon = 0.1f;
constexpr float kMaxHorizon = 6.f;
constexpr int kMaxSamples = static_cast<int>(kMaxHorizon * kTickRate) + 1;
constexpr int kMaxBounces = 12;
constexpr float kMinBounceMarkerSpeed = 100.f;  // uu/s of normal impact speed to show a bounce marker

struct PathSample {
    float t = 0.f;            // seconds after the path start
    Vec3 pos;
    Vec3 vel;
    Vec3 normal;              // contact normal when (flags & kStepContact)
    Vec3 point;               // touched surface point when (flags & kStepContact)
    float impactSpeed = 0.f;  // normal speed at impact of the strongest contact
    uint8_t flags = 0;        // StepFlags of the tick that produced this sample
};

struct PathBounce {
    int index = -1;           // sample index
    Vec3 point;               // contact point on the surface
    Vec3 normal;              // surface normal (into the playable volume)
    float impactSpeed = 0.f;
    uint8_t flags = 0;        // StepFlags (floor / wall / ceiling)
};

struct PredictedPath {
    bool valid = false;
    int64_t frame = 0;        // physics frame of `start` (tick mode)
    SimBall start;
    SimParams params;
    ArenaMode arena = ArenaMode::Soccar;
    float horizon = 0.f;
    std::vector<PathSample> samples;  // samples[i] = state (i + 1) ticks after `start`

    // ---- Analysis (analyzePath)
    bool startAirborne = false;  // the ball is in free flight at the start
    int apexIndex = -1;          // -1: already descending (apex = start), -2: apex beyond the horizon
    int landingIndex = -1;       // first floor contact of an airborne ball, -1 = none
    int zoneStart = -1;          // first-touch zone, inclusive sample indices, -1 = none
    int zoneEnd = -1;
    int goalIndex = -1;          // first sample past a goal line, -1 = none
    int goalSide = 0;            // +1 goal at +Y (orange goal), -1 goal at -Y (blue goal)
    std::array<PathBounce, kMaxBounces> bounces{};
    int bounceCount = 0;

    void reset();
    int size() const { return static_cast<int>(samples.size()); }
    // Ball state `ticks` after the start (0 = the start itself), clamped to the last sample.
    Vec3 posAt(int ticks) const;
    Vec3 velAt(int ticks) const;
};

// Simulates `horizon` seconds (clamped to kMinHorizon..kMaxHorizon) from `start`, one sample per tick.
// Reuses the capacity of out.samples (no allocation after the first call). Stops after a goal.
// Non-finite input leaves an invalid path. Spin above the cap is scaled down first.
void simulatePath(const SimBall& start, float horizon, const SimParams& prm, ArenaMode mode, PredictedPath& out);

// Fills the analysis fields. zoneMin / zoneMax: ball centre height band (uu) of the first-touch zone
// (swapped when given in the wrong order).
void analyzePath(PredictedPath& path, float zoneMin, float zoneMax);

// The ball is in the air (not rolling or resting on the floor).
bool ballAirborne(const Vec3& pos, const Vec3& vel, float arenaRadius);

// Index of the sample closest to `p` among the first `maxSearch` samples; -1 when the start is closest.
int closestSample(const PredictedPath& path, const Vec3& p, int maxSearch);

} // namespace rlt::pr
