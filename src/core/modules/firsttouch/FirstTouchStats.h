#pragma once
// Aggregated first-touch statistics (session and all-time), bounded histories for the HUD and the UI tab.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "core/common/Store.h"
#include "core/common/Units.h"
#include "core/modules/firsttouch/FirstTouchAnalysis.h"

namespace rlt::ft {

// Fixed-capacity ring buffer (no allocation after construction). Index 0 = oldest.
template <class T, size_t N>
class Ring {
public:
    void push(const T& v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }
    void clear() {
        head_ = 0;
        size_ = 0;
    }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    static constexpr size_t capacity() { return N; }
    const T& at(size_t i) const { return buf_[(head_ + N - size_ + i) % N]; }
    const T& newest() const { return at(size_ - 1); }
    std::vector<T> toVector() const {
        std::vector<T> v;
        v.reserve(size_);
        for (size_t i = 0; i < size_; ++i) v.push_back(at(i));
        return v;
    }

private:
    std::array<T, N> buf_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

// One contact point of the heatmap, in the approach frame (x along the car's approach, y right, z up).
struct HeatPoint {
    float x = 0.f, y = 0.f, z = 0.f;
    float score = 0.f;
    int type = 0;  // TouchType
};

struct TouchStats {
    int graded = 0;              // graded first touches
    int successes = 0;
    float sumScore = 0.f;
    float bestScore = 0.f;
    int goodZone = 0;            // |speed diff| <= good zone
    int streak = 0;              // current streak of successes
    int bestStreak = 0;
    double sumSignedDiff = 0.0;  // sum of signed speed differences (uu/s)
    std::array<int, kTouchTypeCount> typeCount{};      // graded touches per type; Dribble = dribble contacts
    std::array<float, kTouchTypeCount> typeSumScore{};
    int dribbleContacts = 0;
    int dribbleChains = 0;       // chains of at least 2 contacts
    int bestChainContacts = 0;
    float bestChainSeconds = 0.f;
    float dribbleSeconds = 0.f;  // total time spent in chains
    int flipResets = 0;

    void addGraded(TouchType type, float score, bool success, float speedDiff, bool inGoodZone);
    void addDribbleContact();
    void addChain(int contacts, float seconds);

    float averageScore() const { return graded > 0 ? sumScore / static_cast<float>(graded) : 0.f; }
    float successRate() const { return graded > 0 ? static_cast<float>(successes) / static_cast<float>(graded) : 0.f; }
    float goodZoneRate() const { return graded > 0 ? static_cast<float>(goodZone) / static_cast<float>(graded) : 0.f; }
    float meanSignedDiff() const { return graded > 0 ? static_cast<float>(sumSignedDiff / graded) : 0.f; }
    float typeAverage(TouchType t) const;

    void save(Store& st, const std::string& prefix) const;
    void load(const Store& st, const std::string& prefix);
};

// "Tendance : tu arrives trop vite en moyenne (+5.1 km/h)." Empty when fewer than `minTouches` touches.
std::string trendText(const TouchStats& s, SpeedUnit unit, float threshold, int minTouches = 5);

// Short label of a tip code for the "à travailler" line ("vitesse trop élevée", ...).
const char* tipShortLabel(const std::string& code);

// Most frequent code in `codes` when it appears at least `minCount` times (bandwidth feedback), else "".
std::string persistentCode(const std::vector<std::string>& codes, int minCount = 3);

} // namespace rlt::ft
