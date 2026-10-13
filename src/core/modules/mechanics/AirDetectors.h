#pragma once
// Aerial movement: fast aerial / double jump aerial, stall, helicopter / tornado spin, recovery (landing quality).

#include <vector>

#include "core/modules/mechanics/DetectorContext.h"

namespace rlt::mech {

class FastAerialDetector {
public:
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    void reset() { active_ = false; }
    // Dodge reported as an accidental flip of a fast aerial (arbitration: no other grade for it).
    int64_t claimedDodgeTick() const { return claimedDodge_; }

private:
    static constexpr int kNever = -99;
    struct Attempt {
        int64_t jumpTick = 0, pressTick = 0, releaseTick = -1, djTick = -1, dodgeTick = -1;
        int64_t holdTicks = -1;
        int boostStart = kNever;  // ticks after the jump
        bool dodged = false;
        DodgeKind dodgeKind = DodgeKind::Forward;
        float pitchSum = 0.f;
        int pitchSamples = 0;
        float noseAtDJ = 0.f, stickAtDJ = 0.f, startZ = 0.f, heightGain = 0.f, vzEnd = 0.f;
    };
    void emit(const Ctx& c, std::vector<MechGrade>& out);
    Attempt a_;
    bool active_ = false;
    int64_t claimedDodge_ = -1;
};

class StallDetector {
public:
    bool onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out);
    // Jump pressed in the air with stall inputs but no flip left.
    void onTick(const Ctx& c, std::vector<MechGrade>& out);

private:
    int64_t lastNoFlip_ = -100000;
};

class AirSpinDetector {
public:
    static constexpr int kMinTicks = 60;  // 0.5 s, UNVERIFIED
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    void reset() { ticks_ = 0; mode_ = 0; }
    int activeTicks() const { return ticks_; }

private:
    int ticks_ = 0, mode_ = 0;
    bool dodgeSeen_ = false;
    float maxH_ = 0.f;
    int64_t start_ = 0;
    Vec3 axisSum_;
    float rateSum_ = 0.f, angle_ = 0.f;
};

class RecoveryDetector {
public:
    static constexpr int kMinAirTicks = 60;  // 0.5 s
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    void reset() {
        pending_ = false;
        touch_ = false;
        crashed_ = false;
        crashTicks_ = 0;
    }
    // A dash grade that ended the aerial being graded: report it inside the recovery. True when absorbed.
    bool absorbDash(const MechGrade& dash);
    // The dash at this landing is reported on its own: drop the recovery grade of that landing.
    void skipLanding(int64_t landTick) {
        if (pending_ && landTick == landTick_) pending_ = false;
    }

private:
    bool touch_ = false, pending_ = false, flipAvail_ = false, wavedash_ = false, absorbed_ = false;
    bool crashed_ = false;  // this aerial phase already ended on the roof or the side
    int crashTicks_ = 0;
    float touchUpDotN_ = 1.f, touchSpeed_ = 0.f, touchAlign_ = 0.f, speedAfter_ = 0.f;
    float dashScore_ = 0.f, dashGain_ = 0.f;
    bool dashSuccess_ = false;
    int64_t landTick_ = 0, airTicks_ = 0;
    int stableRun_ = 0, settleTicks_ = -1;
};

} // namespace rlt::mech
