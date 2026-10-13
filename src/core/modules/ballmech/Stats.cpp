#include "core/modules/ballmech/Stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rlt::bm {

namespace {
float sane(float v, float lo, float hi) {
    if (!std::isfinite(v)) return lo;
    return std::max(lo, std::min(hi, v));
}
} // namespace

void MechStats::record(float score, bool success) {
    score = sane(score, 0.f, 100.f);
    ++attempts;
    ++sessionAttempts;
    if (success) {
        ++successes;
        ++sessionSuccesses;
    }
    scoreSum += score;
    best = std::max(best, score);
    recent.push_back(score);
    if (recent.size() > kRecent) recent.erase(recent.begin(), recent.begin() + static_cast<long>(recent.size() - kRecent));
}

void Stats::clear() {
    for (auto& m : mech_) m = MechStats{};
    records = Records{};
    sessionResets = 0;
}

void Stats::save(Store& st) const {
    for (int i = 0; i < kMechCount; ++i) {
        const MechStats& m = mech_[static_cast<size_t>(i)];
        const std::string k = std::string("ballmech.stats.") + mechInfo(i).id;
        st.setInt(k + ".attempts", m.attempts);
        st.setInt(k + ".successes", m.successes);
        st.setFloat(k + ".score_sum", m.scoreSum);
        st.setFloat(k + ".best", m.best);
        std::vector<std::string> recent;
        recent.reserve(m.recent.size());
        for (float v : m.recent) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
            recent.emplace_back(buf);
        }
        st.setList(k + ".recent", recent);
    }
    st.setInt("ballmech.records.flip_resets", records.flipResets);
    st.setFloat("ballmech.records.air_dribble_s", records.airDribbleSeconds);
    st.setFloat("ballmech.records.flick_speed", records.flickSpeed);
    st.setFloat("ballmech.records.pinch_speed", records.pinchSpeed);
    st.setFloat("ballmech.records.power_speed", records.powerShotSpeed);
}

void Stats::load(const Store& st) {
    for (int i = 0; i < kMechCount; ++i) {
        MechStats& m = mech_[static_cast<size_t>(i)];
        const std::string k = std::string("ballmech.stats.") + mechInfo(i).id;
        m = MechStats{};
        m.attempts = static_cast<int>(std::max(0LL, std::min(100000000LL, st.getInt(k + ".attempts", 0))));
        m.successes = static_cast<int>(std::max(0LL, std::min<long long>(m.attempts, st.getInt(k + ".successes", 0))));
        m.scoreSum = sane(st.getFloat(k + ".score_sum", 0.f), 0.f, 100.f * static_cast<float>(m.attempts));
        m.best = sane(st.getFloat(k + ".best", 0.f), 0.f, 100.f);
        const auto recent = st.getList(k + ".recent");
        const size_t first = recent.size() > MechStats::kRecent ? recent.size() - MechStats::kRecent : 0;
        for (size_t j = first; j < recent.size(); ++j) {
            char* end = nullptr;
            const float v = std::strtof(recent[j].c_str(), &end);
            if (end != recent[j].c_str()) m.recent.push_back(sane(v, 0.f, 100.f));
        }
    }
    records.flipResets = static_cast<int>(std::max(0LL, std::min(100000000LL, st.getInt("ballmech.records.flip_resets", 0))));
    records.airDribbleSeconds = sane(st.getFloat("ballmech.records.air_dribble_s", 0.f), 0.f, 600.f);
    records.flickSpeed = sane(st.getFloat("ballmech.records.flick_speed", 0.f), 0.f, 6000.f);
    records.pinchSpeed = sane(st.getFloat("ballmech.records.pinch_speed", 0.f), 0.f, 6000.f);
    records.powerShotSpeed = sane(st.getFloat("ballmech.records.power_speed", 0.f), 0.f, 6000.f);
}

} // namespace rlt::bm
