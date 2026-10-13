#pragma once
// Records and statistics of the shooting module: best flick per type, per zone accuracy (session, previous
// session, all time), power records, target streaks. Persisted under "shooting." keys with bounded sizes.

#include <array>
#include <vector>

#include "core/common/Store.h"
#include "core/modules/shooting/FlickAnalyzer.h"
#include "core/modules/shooting/ShootGeometry.h"

namespace rlt::shoot {

struct FlickRecord {
    float bestPower = 0.f; // uu/s
    float bestAccuracy = 0.f; // 0..100
    int count = 0;
    int successes = 0;
};

struct ZoneStats {
    int attempts = 0;
    int hits = 0;
    float scoreSum = 0.f; // sum of the 0..100 scores
    float hitRate() const { return attempts > 0 ? static_cast<float>(hits) / static_cast<float>(attempts) : 0.f; }
    float avgScore() const { return attempts > 0 ? scoreSum / static_cast<float>(attempts) : 0.f; }
    void add(bool hit, float score) {
        ++attempts;
        if (hit) ++hits;
        scoreSum += score;
    }
};

struct PowerStats {
    float best = 0.f; // uu/s
    int count = 0;
    int successes = 0;
};

class ShootingStats {
public:
    static constexpr size_t kHistory = 60; // session histories (UI plots)

    struct FlickFlags {
        bool powerAll = false, powerSession = false, accuracyAll = false;
    };
    FlickFlags addFlick(FlickType category, float powerUus, float accuracy100, bool success);
    // Returns true when the all-time best streak was beaten.
    bool addTarget(Zone zone, bool hit, float score100);
    struct PowerFlags {
        bool all = false, session = false;
    };
    PowerFlags addPower(float speedUus, bool success);

    void resetSession();
    void resetAll();
    void save(Store& st) const;
    void load(const Store& st);

    float bestFlickAccuracy() const;
    int sessionTargetAttempts() const;

    std::array<FlickRecord, kFlickTypeCount> flickAll{}, flickSession{};
    std::array<ZoneStats, kZoneCount> zoneAll{}, zoneSession{}, zonePrev{};
    PowerStats powerAll, powerSession;
    int streak = 0, bestStreakAll = 0, bestStreakSession = 0;
    std::vector<float> flickPowersKph; // session history
    std::vector<float> shotPowersKph; // power mode, session history
    std::vector<float> targetScores; // target mode, session history
};

} // namespace rlt::shoot
