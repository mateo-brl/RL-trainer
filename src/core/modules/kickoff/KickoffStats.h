#pragma once
// Per-spawn kickoff statistics and personal bests, persisted in the progress Store under "kickoff.<spawn>.*".
// The personal best also keeps a downsampled "distance along the path" profile so the ghost can replay it.

#include <array>
#include <string>
#include <vector>

#include "core/common/Store.h"
#include "core/modules/kickoff/KickoffData.h"
#include "core/modules/kickoff/KickoffGhost.h"
#include "core/modules/kickoff/KickoffRecorder.h"

namespace rlt {

constexpr size_t kKoRecentCount = 10;   // recent times / scores kept per spawn
constexpr size_t kKoPbMaxSamples = 64;  // personal-best profile samples (6.4 s at 0.1 s)
constexpr float kKoPbSampleDt = 0.1f;

struct KickoffSpawnStats {
    int attempts = 0;
    int touches = 0;
    int duels = 0;  // attempts with a ghost (or a real opponent)
    int wins = 0, neutrals = 0, losses = 0;
    float bestTime = 0.f;   // 0 = none
    float bestSpeed = 0.f;  // speed toward the ball at the personal-best touch
    int bestScore = 0;
    std::vector<float> recentTimes;  // last kKoRecentCount touch times (oldest first)
    std::vector<int> recentScores;   // last kKoRecentCount scores (oldest first)
    std::array<int, kKoTypeCount> typeCounts{};
    std::vector<float> pbProfile;  // along at t = i * kKoPbSampleDt for t < bestTime

    float avgTime() const;   // mean of recentTimes, 0 when empty
    float avgScore() const;  // mean of recentScores, -1 when empty
    float winRate() const;   // wins / duels, 0 when no duel
    KickoffType favouriteType() const;
};

class KickoffStats {
public:
    // Records a finished attempt. Returns true when it is a new best time for that spawn.
    bool record(const KickoffMetrics& m, int score, const KickoffDuelResult* duel);
    const KickoffSpawnStats& of(KickoffSpawn s) const { return per_[static_cast<size_t>(kickoffIndex(s))]; }
    KickoffSpawnStats& of(KickoffSpawn s) { return per_[static_cast<size_t>(kickoffIndex(s))]; }
    void reset();
    int totalAttempts() const;
    // Spawn with the worst recent average score; never-played spawns first, ties broken by fewer attempts.
    KickoffSpawn weakest() const;
    // Ghost profile replaying the personal best of `s` (false when there is none).
    bool personalBestProfile(KickoffSpawn s, KickoffGhostProfile& out) const;

    void save(Store& st) const;
    void load(const Store& st);

private:
    std::array<KickoffSpawnStats, kKoSpawnCount> per_{};
};

// Downsamples per-tick along samples to `dt` steps strictly before `arrive` (bounded to kKoPbMaxSamples).
std::vector<float> kickoffDownsample(const std::vector<float>& along, float arrive, float dt = kKoPbSampleDt);

} // namespace rlt
