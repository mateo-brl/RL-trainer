#pragma once
// Per-mechanic progress: attempts, success rate, average and best score, personal best of the mechanic's record
// metric, recent scores (bounded) and the most frequent mistakes (bounded). Persisted under "mechanics.stats.".

#include <array>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Grade.h"
#include "core/common/Store.h"
#include "core/modules/mechanics/MechCatalog.h"

namespace rlt::mech {

struct MechStat {
    static constexpr size_t kRecentMax = 30;
    static constexpr size_t kErrorsMax = 8;

    int attempts = 0, successes = 0;
    double scoreSum = 0.0;  // large accumulator: double in memory and in the store (Store::setDouble)
    float best = 0.f;
    bool hasPb = false;
    float pb = 0.f;
    std::deque<float> recent;                         // last scores, oldest first
    std::vector<std::pair<std::string, int>> errors;  // tip code -> count
    int sessionAttempts = 0, sessionSuccesses = 0;    // not persisted

    float average() const { return attempts > 0 ? static_cast<float>(scoreSum / static_cast<double>(attempts)) : 0.f; }
    float successRate() const { return attempts > 0 ? static_cast<float>(successes) / static_cast<float>(attempts) : 0.f; }
    // Most frequent tip code ("" when none).
    std::string topError() const;
};

class MechStats {
public:
    // Records one attempt. Returns true when it set a new personal best (not on the very first value).
    bool record(Mech m, const AttemptResult& r);
    const MechStat& get(Mech m) const { return s_[static_cast<size_t>(index(m))]; }
    void reset();
    void reset(Focus family);
    int sessionTotal() const;

    void save(Store& st) const;
    void load(const Store& st);

private:
    std::array<MechStat, kMechCount> s_{};
};

} // namespace rlt::mech
