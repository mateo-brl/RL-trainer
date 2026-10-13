#pragma once
// Flip mechanics: speedflip (with kickoff context), half-flip, flip cancel, and the combos built on them
// (speedflip -> zap dash, half-flip -> wavedash).

#include <vector>

#include "core/modules/mechanics/DetectorContext.h"

namespace rlt::mech {

enum class KickoffPhase : int { None = 0, Countdown, Live };

// Kickoff context for speedflip grading (boost at go, first jump timing, time to ball).
// Auto mode: a still car far from a still ball at the centre spot starts a kickoff when it moves (freeplay
// reset, offline match). Manual mode: the speedflip drill drives the phases (frozen countdown, GO).
class KickoffTracker {
public:
    bool live() const { return phase_ == KickoffPhase::Live; }
    KickoffPhase phase() const { return phase_; }
    int64_t startTick() const { return startTick_; }
    bool startedNoBoost() const { return noBoost_; }
    bool startedInAir() const { return inAir_; }
    int64_t firstJumpTicks() const { return firstJumpTicks_; }
    float timeToBall() const { return timeToBall_; }

    void setManual(bool manual) { manual_ = manual; if (!manual) clear(); }
    bool manual() const { return manual_; }
    void countdown() { clear(); phase_ = KickoffPhase::Countdown; }
    void go(const Snapshot& s) { begin(s); }
    void clear();

    void step(const Ctx& c);
    void onTouch(float time) {
        if (live() && timeToBall_ < 0.f) timeToBall_ = std::max(0.f, time - startTime_);
    }

private:
    void begin(const Snapshot& s);
    KickoffPhase phase_ = KickoffPhase::None;
    bool manual_ = false;
    int64_t startTick_ = -1, firstJumpTicks_ = -1;
    float startTime_ = 0.f;
    bool noBoost_ = false, inAir_ = false;
    float timeToBall_ = -1.f;
    int stillTicks_ = 0;
};

class SpeedFlipDetector {
public:
    static constexpr int64_t kKickoffJumpEarlyTicks = 50;  // first jump earlier than this after GO: tip
    static constexpr int64_t kKickoffJumpLateTicks = 80;
    bool onDodge(const DodgeRecord& r, const Ctx& c, const KickoffTracker& ko, std::vector<MechGrade>& out);
    // Kickoff speedflips are emitted once the ball is touched (time to ball known) or after 3 s.
    void onTick(const Ctx& c, const KickoffTracker& ko, std::vector<MechGrade>& out);
    void flush(std::vector<MechGrade>& out);
    bool hasPending() const { return hasPending_; }
    // Kickoff speedflip stored as pending during the last onDodge, handed out once so that the combo detector
    // sees it before the zap dash that may follow it (the grade itself is emitted at the ball touch).
    const MechGrade* takeFreshPending() {
        if (!fresh_ || !hasPending_) return nullptr;
        fresh_ = false;
        return &pending_;
    }

private:
    MechGrade pending_;
    bool hasPending_ = false;
    bool fresh_ = false;
};

class HalfFlipDetector {
public:
    bool onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out);
};

// Any forward / backward flip cancelled within 20 ticks that no other detector claimed.
class FlipCancelDetector {
public:
    static constexpr int kMaxCancelTicks = 20;
    bool onDodge(const DodgeRecord& r, const Ctx& c, std::vector<MechGrade>& out);
};

// A kickoff speedflip can reach onGrade twice: early (pending, see SpeedFlipDetector::takeFreshPending) and when
// it is emitted after the ball touch. The second copy of a speedflip already used by a speed zap is ignored.
class ComboDetector {
public:
    static constexpr int kMaxGapTicks = 45;  // UNVERIFIED
    void onGrade(const MechGrade& g, std::vector<MechGrade>& out);
    void reset() {
        haveSF_ = haveHF_ = false;
        usedSF_ = false;
    }

private:
    static MechGrade combine(Mech m, const MechGrade& a, const MechGrade& b);
    MechGrade lastSpeedflip_, lastHalfFlip_;
    bool haveSF_ = false, haveHF_ = false;
    bool usedSF_ = false;          // usedSFDodgeTick_ is valid
    int64_t usedSFDodgeTick_ = 0;  // dodge of the speedflip of the last speed zap
};

} // namespace rlt::mech
