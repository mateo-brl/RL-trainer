#pragma once
// Arbitration contract: one attempt is reported once, by the most specific mechanic.
//
// Two grades conflict when their windows overlap ([tStart - 0.1 s, tEnd]). The winner is:
//   1. the strong grade when exactly one of them is weak (incomplete signature),
//   2. otherwise the higher priority family: flip_reset > ceiling_shot > double_tap > pinch > flick > air_dribble
//      > aerial_touch > power_shot.
// New grades are held while a more specific detector is engaged on an attempt that started before the grade ended
// (it may still claim the same touches), then released in order. A grade losing against an already released one
// is dropped. Nothing is held longer than 4 s.

#include <array>
#include <vector>

#include "core/modules/ballmech/Detector.h"

namespace rlt::bm {

struct EngagedState {
    std::array<bool, kFamilyCount> on{};
    std::array<float, kFamilyCount> since{};
};

class Arbiter {
public:
    static constexpr float kSlack = 0.1f;
    static constexpr float kMaxHold = 4.f;
    static constexpr float kMemory = 3.f;

    void reset();
    void clearHeld() { held_.clear(); }
    // A detector produced a grade at game time `now`.
    void submit(MechGrade g, float now);
    // Moves releasable grades into `out` (oldest first).
    void release(float now, const EngagedState& engaged, std::vector<MechGrade>& out);
    size_t heldCount() const { return held_.size(); }

    static bool overlaps(const MechGrade& a, const MechGrade& b);
    // True when `a` wins the conflict against `b`.
    static bool beats(const MechGrade& a, const MechGrade& b);

private:
    struct Held {
        MechGrade g;
        float since = 0.f;
    };
    struct Recent {
        Family family = Family::Aerial;
        float tStart = 0.f, tEnd = 0.f, at = 0.f;
        bool weak = false;
    };
    std::vector<Held> held_;
    std::vector<Recent> recent_;
};

} // namespace rlt::bm
