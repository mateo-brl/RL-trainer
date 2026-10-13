#pragma once
// Ordered ring tracker of one attempt: only the next ring (and the one after it, to detect a skip) is tested
// on each segment of the object path (car hitbox centre or ball centre), with sub-tick split times.
//
// Miss policies (setting "Anneau raté"):
//   Penalty  - crossing the next ring's plane close to it but outside (or skipping it) gives the ring up,
//              adds a time penalty and the run goes on with the following ring (ring-map speedrun feel);
//   ComeBack - the miss is counted, the player must go back through the ring (flying back through the missed
//              ring the wrong way to get behind it is the natural way back: it is not a wrong-way pass);
//   Strict   - any miss or skip fails the attempt (time attack).

#include <cstdint>
#include <vector>

#include "core/modules/courses/CourseGen.h"

namespace rlt::courses {

enum class MissPolicy : uint8_t { Penalty = 0, ComeBack = 1, Strict = 2 };
enum class RunEvent : uint8_t { None, Passed, Missed, WrongWay, Skipped, Finished, Failed };

struct RunSplit {
    int ring = 0;
    double time = 0;       // run time including penalties when the ring was passed (or given up)
    PassKind kind = PassKind::None;
    bool missed = false;   // given up with a penalty
    double centring = 0;   // radial offset / radius at the crossing (0 = perfect centre)
};

class CourseRun {
public:
    void reset(const Course* course, MissPolicy policy, bool grazeCounts, double penaltySeconds);

    // Object centre at two consecutive samples with their run times (seconds since the start). When both
    // velocities are given and the gap is longer than 1.5 ticks, the cubic Hermite arc is tested.
    RunEvent step(const DVec3& p0, const DVec3& p1, double t0, double t1, const DVec3* v0 = nullptr,
                  const DVec3* v1 = nullptr);
    void fail() { failed_ = true; }

    int nextRing() const { return next_; }
    int ringCount() const { return course_ ? static_cast<int>(course_->rings.size()) : 0; }
    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
    bool active() const { return course_ && !finished_ && !failed_; }
    int misses() const { return misses_; }
    int grazes() const { return grazes_; }
    int wrongWays() const { return wrongWays_; }
    // ComeBack policy: the next ring was missed or skipped and the player has to come back for it.
    bool awaitingComeBack() const { return comeBack_; }
    int ringsPassed() const;  // passed without being given up
    double penaltyTime() const { return penalty_; }
    // Final time (last split) once finished, otherwise 0.
    double finishTime() const { return finished_ && !splits_.empty() ? splits_.back().time : 0.0; }
    const std::vector<RunSplit>& splits() const { return splits_; }
    // True when ring i was given up (penalty policy).
    bool ringMissed(int i) const;
    const PassResult& lastResult() const { return last_; }

private:
    RunEvent giveUp(int ring, double tCross, const PassResult& pr);
    RunEvent pass(int ring, double tCross, const PassResult& pr);

    const Course* course_ = nullptr;
    MissPolicy policy_ = MissPolicy::Penalty;
    bool grazeCounts_ = true;
    double penaltyPerMiss_ = 2.0;
    int next_ = 0, misses_ = 0, grazes_ = 0, wrongWays_ = 0;
    double penalty_ = 0;
    bool finished_ = false, failed_ = false;
    bool comeBack_ = false;
    std::vector<RunSplit> splits_;
    PassResult last_;
};

} // namespace rlt::courses
