#pragma once
// "Parcours complet" of the movement drills (cross-cutting fix 2): the player starts from a still car (or the plain
// drive of the move) and does every step himself: reverse before the half-flip, hop before the wavedash, climb the
// wall before the wall dash, take off under a lob before the fast aerial... Each step is checked in order by the shared
// StepTracker (core/common/Steps.h): it turns green when done, the first one missing is named on a failure or a
// timeout, with its tip (never silent, fix 5).
//
// The checks read FullLive, which the module refreshes on every tick of the attempt AFTER the detectors ran: this
// tick's and the previous snapshot, the detectors' live state (jump timeline, dodge record), every grade produced since
// GO (all families) and the first ball touch. Positions are compared in blue-team axes (FullLive::blue).

#include <memory>
#include <vector>

#include "core/common/Steps.h"
#include "core/modules/mechanics/Drills.h"
#include "core/modules/mechanics/MovementCoach.h"

namespace rlt::mech {

struct FullLive {
    Snapshot s;                         // this tick
    Snapshot p;                         // previous tick (== s on the first tick)
    const MovementCoach* coach = nullptr;
    std::vector<MechGrade> grades;      // every grade produced since GO, oldest first
    size_t newGrades = 0;               // index of the first grade produced on this tick
    int64_t goFrame = 0;
    float goTime = 0.f;
    float touchTime = -1.f;             // first ball touch since GO (game time), -1 when none
    float touchBallZ = 0.f;             // ball height at that touch
    bool mirrored = false;              // orange team: world positions are rotated 180 degrees
    int side = 1;                       // the drill's side (+1 / -1), blue axes
    int spotKind = 0;                   // speedflip kickoff spot kind

    int64_t sinceGo() const { return s.frame - goFrame; }
    Vec3 blue(const Vec3& v) const { return mirrored ? Vec3{-v.x, -v.y, v.z} : v; }
    const MotionTracker& motion() const { return coach->motion(); }
    // Dodge record of the dodge that started at `tick` while it is still followed (-1: any active record).
    const DodgeRecord* dodge(int64_t tick = -1) const {
        const DodgeRecord* r = coach ? coach->dodges().active() : nullptr;
        return (r && (tick < 0 || r->tick == tick)) ? r : nullptr;
    }
    bool jumpedNow() const { return s.car.jumped && !p.car.jumped; }
    bool doubleJumpedNow() const { return s.car.doubleJumped && !p.car.doubleJumped; }
    // A dodge record that began on this tick (nullptr otherwise).
    const DodgeRecord* dodgeNow() const {
        const DodgeRecord* r = dodge();
        return r && r->tick == s.frame ? r : nullptr;
    }
    bool landedNow() const { return grounded(s.car) && !grounded(p.car); }
    // Latest grade of `m` produced since GO whose tickStart / dodge is at or after `fromTick` (nullptr when none).
    const MechGrade* gradeSince(Mech m, int64_t fromTick) const {
        for (size_t i = grades.size(); i-- > 0;) {
            const MechGrade& g = grades[i];
            if (g.mech == m && std::max(g.tickStart, g.dodgeTick) >= fromTick) return &g;
        }
        return nullptr;
    }
};

// Steps of the "parcours complet" of a family at `level` (0..1), for the placement `d` (makeDrill with full = true).
// The checks keep `live` alive; their own memory lives in the closures, so build a fresh list for every attempt.
std::vector<Step> fullSteps(Focus f, float level, const DrillSetup& d, const std::shared_ptr<FullLive>& live);

} // namespace rlt::mech
