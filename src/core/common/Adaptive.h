#pragma once
// Adaptive difficulty shared by every drill: a weighted up/down staircase (Kaernbach 1991).
// After a success the level rises by `stepUp`, after a failure it drops by `stepDown`; with
// stepDown / stepUp = p / (1 - p) the level converges where the success rate is p (default 0.75,
// the "challenging but doable" zone for motor learning).

#include <algorithm>
#include <string>

#include "core/common/Store.h"

namespace rlt {

class Staircase {
public:
    explicit Staircase(float targetSuccess = 0.75f, float step = 0.03f, float level = 0.2f)
        : target_(targetSuccess), step_(step), level_(level) {}

    float level() const { return level_; }  // 0 (easiest) .. 1 (hardest)
    void setLevel(float l) { level_ = std::max(0.f, std::min(1.f, l)); }
    float target() const { return target_; }
    void setTarget(float p) { target_ = std::max(0.5f, std::min(0.95f, p)); }

    int attempts() const { return attempts_; }
    int successes() const { return successes_; }
    int streak() const { return streak_; }          // > 0 successes in a row, < 0 failures in a row
    float recentRate() const { return recent_; }     // exponential moving average of success

    void record(bool success) {
        ++attempts_;
        if (success) {
            ++successes_;
            streak_ = streak_ > 0 ? streak_ + 1 : 1;
            level_ += stepUp();
        } else {
            streak_ = streak_ < 0 ? streak_ - 1 : -1;
            level_ -= stepDown();
        }
        level_ = std::max(0.f, std::min(1.f, level_));
        recent_ = recent_ * 0.8f + (success ? 0.2f : 0.f);
    }

    // Equilibrium: target * stepUp == (1 - target) * stepDown.
    float stepUp() const { return step_; }
    float stepDown() const { return step_ * target_ / (1.f - target_); }

    void save(Store& st, const std::string& key) const {
        st.setFloat(key + ".level", level_);
        st.setInt(key + ".attempts", attempts_);
        st.setInt(key + ".successes", successes_);
        st.setFloat(key + ".recent", recent_);
    }
    void load(const Store& st, const std::string& key) {
        setLevel(st.getFloat(key + ".level", level_));
        attempts_ = static_cast<int>(st.getInt(key + ".attempts", 0));
        successes_ = static_cast<int>(st.getInt(key + ".successes", 0));
        recent_ = st.getFloat(key + ".recent", 0.5f);
        streak_ = 0;
    }

private:
    float target_;
    float step_;
    float level_;
    int attempts_ = 0;
    int successes_ = 0;
    int streak_ = 0;
    float recent_ = 0.5f;
};

} // namespace rlt
