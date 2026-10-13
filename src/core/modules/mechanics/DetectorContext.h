#pragma once
// Read-only view handed to every detector for one physics tick.

#include "core/modules/mechanics/DodgeTracker.h"
#include "core/modules/mechanics/MechCommon.h"
#include "core/modules/mechanics/MotionTracker.h"

namespace rlt::mech {

struct Ctx {
    const Snapshot& s;   // this tick
    const Snapshot& p;   // previous tick (== s right after a reset)
    const TickEvents& ev;
    const MotionTracker& mt;
    const DodgeTracker& dt;
    const DetectConfig& cfg;
};

} // namespace rlt::mech
