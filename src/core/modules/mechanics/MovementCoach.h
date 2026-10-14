#pragma once
// MovementCoach: the detection hub. Feed one Snapshot per physics tick, get zero or more grades back.
//
// Arbitration (one report per dodge): a dodge claimed by the fast aerial detector (accidental flip) is not
// graded again; otherwise the closed dodge record goes to the first detector that accepts it, in this order:
// stall, wavedash family, speedflip, half-flip, flip cancel. A dash that ends a long aerial is reported inside
// the recovery grade (unless the player trains dashes). Chains and combos are aggregates of graded parts; a kickoff
// speedflip waiting for the ball touch is handed to the combo detector as soon as it lands (speed zap).

#include <vector>

#include "core/modules/mechanics/AirDetectors.h"
#include "core/modules/mechanics/BoostDetector.h"
#include "core/modules/mechanics/DodgeTracker.h"
#include "core/modules/mechanics/FlipDetectors.h"
#include "core/modules/mechanics/GroundDetectors.h"
#include "core/modules/mechanics/MotionTracker.h"

namespace rlt::mech {

class MovementCoach {
public:
    DetectConfig cfg;

    // Drops every transient state (teleport, drill placement, map change). A manual kickoff phase is kept.
    void reset();
    // One physics tick. Appends this tick's grades to `out` (not filtered by settings or focus).
    void step(const Snapshot& s, std::vector<MechGrade>& out);
    // Local car touched the ball (game time of the touch).
    void onBallTouch(float time) { ko_.onTouch(time); }
    // Emits what is still pending (chain in progress, kickoff speedflip waiting for the ball; the boost window
    // when `boostWindow` is set). Used when a drill attempt times out and before a reset shot drops the state.
    void flushPending(const Snapshot& s, std::vector<MechGrade>& out, bool boostWindow);

    KickoffTracker& kickoff() { return ko_; }
    const KickoffTracker& kickoff() const { return ko_; }
    const MotionTracker& motion() const { return mt_; }
    const DodgeTracker& dodges() const { return dt_; }
    int chainCount() const { return chain_.count(); }
    float chainMaxSpeed() const { return chain_.maxSpeed(); }
    int spinTicks() const { return spin_.activeTicks(); }
    int boostWindowTicks() const { return sm_.ticks(); }

private:
    static bool isDash(Mech m) { return m == Mech::Wavedash || m == Mech::ZapDash || m == Mech::WallDash; }

    MotionTracker mt_;
    DodgeTracker dt_;
    KickoffTracker ko_;
    std::vector<DodgeRecord> finished_;
    Snapshot prev_;
    WavedashDetector wd_;
    ChainWavedashDetector chain_;
    SpeedFlipDetector sf_;
    HalfFlipDetector hf_;
    FlipCancelDetector fc_;
    ComboDetector combo_;
    FastAerialDetector fa_;
    StallDetector st_;
    AirSpinDetector spin_;
    RecoveryDetector rec_;
    PowerslideDetector ps_;
    SpeedManagementAnalyzer sm_;
};

} // namespace rlt::mech
