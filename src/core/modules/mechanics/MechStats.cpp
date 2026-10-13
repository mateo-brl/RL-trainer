#include "core/modules/mechanics/MechStats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rlt::mech {

namespace {
std::string prefix(Mech m) { return std::string("mechanics.stats.") + mechInfo(m).key; }
} // namespace

std::string MechStat::topError() const {
    const std::pair<std::string, int>* top = nullptr;
    for (const auto& e : errors)
        if (!top || e.second > top->second) top = &e;
    return top ? top->first : std::string();
}

bool MechStats::record(Mech m, const AttemptResult& r) {
    if (index(m) < 0 || index(m) >= kMechCount) return false;
    MechStat& s = s_[static_cast<size_t>(index(m))];
    s.attempts++;
    s.sessionAttempts++;
    if (r.success) {
        s.successes++;
        s.sessionSuccesses++;
    }
    s.scoreSum += static_cast<double>(r.score);
    s.best = std::max(s.best, r.score);
    s.recent.push_back(r.score);
    while (s.recent.size() > MechStat::kRecentMax) s.recent.pop_front();
    if (!r.tips.empty()) {
        const std::string& code = r.tips.front().code;
        auto it = std::find_if(s.errors.begin(), s.errors.end(), [&](const auto& e) { return e.first == code; });
        if (it != s.errors.end()) {
            it->second++;
        } else {
            if (s.errors.size() >= MechStat::kErrorsMax) {
                auto low = std::min_element(s.errors.begin(), s.errors.end(),
                                            [](const auto& a, const auto& b) { return a.second < b.second; });
                s.errors.erase(low);
            }
            s.errors.push_back({code, 1});
        }
    }
    bool newPb = false;
    if (r.hasRecordValue) {
        const bool better = !s.hasPb || (r.lowerIsBetter ? r.valueForRecord < s.pb : r.valueForRecord > s.pb);
        if (better) {
            newPb = s.hasPb;
            s.pb = r.valueForRecord;
            s.hasPb = true;
        }
    }
    return newPb;
}

void MechStats::reset() {
    for (auto& s : s_) s = MechStat{};
}

void MechStats::reset(Focus family) {
    for (int i = 0; i < kMechCount; ++i)
        if (familyOf(static_cast<Mech>(i)) == family) s_[static_cast<size_t>(i)] = MechStat{};
}

int MechStats::sessionTotal() const {
    int n = 0;
    for (const auto& s : s_) n += s.sessionAttempts;
    return n;
}

void MechStats::save(Store& st) const {
    for (int i = 0; i < kMechCount; ++i) {
        const Mech m = static_cast<Mech>(i);
        const MechStat& s = s_[static_cast<size_t>(i)];
        const std::string p = prefix(m);
        if (s.attempts == 0) {
            st.erasePrefix(p + ".");
            continue;
        }
        st.setInt(p + ".attempts", s.attempts);
        st.setInt(p + ".successes", s.successes);
        st.setDouble(p + ".scoreSum", s.scoreSum);
        st.setFloat(p + ".best", s.best);
        st.setBool(p + ".hasPb", s.hasPb);
        st.setFloat(p + ".pb", s.pb);
        std::vector<std::string> recent;
        for (float v : s.recent) recent.push_back(std::to_string(static_cast<int>(v + 0.5f)));
        st.setList(p + ".recent", recent);
        std::vector<std::string> errs;
        for (const auto& e : s.errors) errs.push_back(e.first + "=" + std::to_string(e.second));
        st.setList(p + ".errors", errs);
    }
}

void MechStats::load(const Store& st) {
    for (int i = 0; i < kMechCount; ++i) {
        const Mech m = static_cast<Mech>(i);
        MechStat s;
        const std::string p = prefix(m);
        s.attempts = static_cast<int>(std::max(0LL, st.getInt(p + ".attempts", 0)));
        s.successes = static_cast<int>(std::clamp(st.getInt(p + ".successes", 0), 0LL, static_cast<long long>(s.attempts)));
        const double sum = st.getDouble(p + ".scoreSum", 0.0);
        s.scoreSum = std::isfinite(sum) ? std::clamp(sum, 0.0, 100.0 * static_cast<double>(s.attempts)) : 0.0;
        s.best = std::clamp(st.getFloat(p + ".best", 0.f), 0.f, 100.f);
        s.pb = st.getFloat(p + ".pb", 0.f);
        s.hasPb = st.getBool(p + ".hasPb", false) && std::isfinite(s.pb);
        if (!s.hasPb) s.pb = 0.f;
        for (const std::string& v : st.getList(p + ".recent")) {
            if (s.recent.size() >= MechStat::kRecentMax) break;
            s.recent.push_back(std::clamp(static_cast<float>(std::atof(v.c_str())), 0.f, 100.f));
        }
        for (const std::string& e : st.getList(p + ".errors")) {
            if (s.errors.size() >= MechStat::kErrorsMax) break;
            const size_t eq = e.rfind('=');
            if (eq == std::string::npos || eq == 0) continue;
            s.errors.push_back({e.substr(0, eq), std::max(0, std::atoi(e.c_str() + eq + 1))});
        }
        s_[static_cast<size_t>(i)] = std::move(s);
    }
}

} // namespace rlt::mech
