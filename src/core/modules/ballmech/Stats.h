#pragma once
// Per-mechanic statistics (persisted: attempts, successes, average / best score, last scores) plus records
// (flip resets, longest air dribble, hardest flick, fastest pinch / power shot) and session counters.
// Store keys: "ballmech.stats.<id>.*", "ballmech.records.*".

#include <array>
#include <string>
#include <vector>

#include "core/common/Store.h"
#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

struct MechStats {
    static constexpr size_t kRecent = 30;
    int attempts = 0;
    int successes = 0;
    float scoreSum = 0.f;
    float best = 0.f;
    std::vector<float> recent;  // last scores, oldest first (<= kRecent)
    int sessionAttempts = 0;
    int sessionSuccesses = 0;

    void record(float score, bool success);
    float average() const { return attempts > 0 ? scoreSum / static_cast<float>(attempts) : 0.f; }
    float rate() const { return attempts > 0 ? static_cast<float>(successes) / static_cast<float>(attempts) : 0.f; }
};

struct Records {
    int flipResets = 0;          // every flip reset ever counted
    float airDribbleSeconds = 0.f;
    float flickSpeed = 0.f;      // uu/s of the ball after the hardest flick
    float pinchSpeed = 0.f;
    float powerShotSpeed = 0.f;
};

class Stats {
public:
    MechStats& at(Mech m) { return mech_[index(m)]; }
    const MechStats& at(Mech m) const { return mech_[index(m)]; }
    Records records;
    int sessionResets = 0;

    void clear();
    void save(Store& store) const;
    void load(const Store& store);

private:
    static size_t index(Mech m) {
        const int i = static_cast<int>(m);
        return static_cast<size_t>(i >= 0 && i < kMechCount ? i : 0);
    }
    std::array<MechStats, kMechCount> mech_{};
};

} // namespace rlt::bm
