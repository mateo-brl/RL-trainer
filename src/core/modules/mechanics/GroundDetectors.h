#pragma once
// Ground and wall movement: wavedash family (wavedash, zap dash, wall dash, ceiling dash), chain wavedash,
// powerslide turns.

#include <string>
#include <vector>

#include "core/modules/mechanics/DetectorContext.h"

namespace rlt::mech {

// Grades quick-landing dodges (dodge -> >= 3 wheels within 20 ticks), classified by landing surface and origin.
class WavedashDetector {
public:
    static constexpr int kMaxLandTicks = 20;  // UNVERIFIED in game
    static constexpr int kZapMaxBounceTicks = 20;
    // Returns true when the record was graded (it then belongs to the wavedash family).
    bool onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out);
    // Late press (jump became a new jump) and centered stick (double jump instead of a dash), drill intent only.
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
};

// Consecutive dashes where each takeoff follows the previous landing within the gap.
class ChainWavedashDetector {
public:
    void onGrade(const MechGrade& g, const Ctx& c, std::vector<MechGrade>& out);
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    void flush(const Ctx& c, std::vector<MechGrade>& out);
    void reset() { count_ = 0; }
    int count() const { return count_; }
    float maxSpeed() const { return count_ > 0 ? maxSpeed_ : 0.f; }

private:
    int count_ = 0, failures_ = 0;
    int64_t startTick_ = 0, lastLanding_ = 0, supersonicTick_ = -1;
    int boostTicks_ = 0, handbrakeTicks_ = 0, observedTicks_ = 0;
    float maxSpeed_ = 0.f, scoreSum_ = 0.f, gainSum_ = 0.f, gapSum_ = 0.f;
    bool wall_ = false;
};

class PowerslideDetector {
public:
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    void reset() { state_ = 0; }

private:
    int state_ = 0;
    int64_t start_ = 0, releaseTick_ = 0;
    float speedIn_ = 0.f, heading_ = 0.f, exitSlip_ = 0.f;
    Vec3 lastFwd_;
    int throttleTicks_ = 0, slideTicks_ = 0;
};

} // namespace rlt::mech
