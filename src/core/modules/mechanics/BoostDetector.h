#pragma once
// Boost efficiency: supersonic upkeep and feathering, summarised over a time window.

#include <vector>

#include "core/modules/mechanics/DetectorContext.h"

namespace rlt::mech {

class SpeedManagementAnalyzer {
public:
    void onTick(const Ctx& c, std::vector<MechGrade>& out);
    // Grades the window collected so far (drill end). Requires at least 1 s of data.
    void flush(const Ctx& c, std::vector<MechGrade>& out, bool force);
    void clear() {
        ticks_ = boostTicks_ = wasted_ = atSpeed_ = coast_ = super_ = holds_ = holdSum_ = curHold_ = 0;
        firstSuper_ = -1;
        afterTicks_ = superAfter_ = 0;
    }
    int ticks() const { return ticks_; }

private:
    void emit(const Ctx& c, std::vector<MechGrade>& out);
    int ticks_ = 0, boostTicks_ = 0, wasted_ = 0, atSpeed_ = 0, coast_ = 0, super_ = 0;
    int holds_ = 0, holdSum_ = 0, curHold_ = 0;
    int firstSuper_ = -1;               // window tick of the first supersonic tick
    int afterTicks_ = 0, superAfter_ = 0;  // ticks since then, and how many of them supersonic
};

} // namespace rlt::mech
