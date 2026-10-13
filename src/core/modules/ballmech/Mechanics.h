#pragma once
// The eleven graded ball mechanics (stable ids used by commands, skills and persistence) and their French texts.

#include <array>
#include <string>

#include "core/modules/ballmech/Detector.h"

namespace rlt::bm {

enum class Mech : int {
    FlipReset = 0,
    Ceiling,
    Flick,
    Musty,
    Breezi,
    DoubleTap,
    AirDribble,
    AirRoll,
    Aerial,
    Pinch,
    PowerShot,
    Count
};
constexpr int kMechCount = static_cast<int>(Mech::Count);

struct MechInfo {
    Mech mech;
    const char* id;          // "flipreset" (commands, persistence)
    const char* skill;       // "ball.flipreset" (AttemptResult::skill)
    const char* title;       // "Flip reset"
    const char* what;        // French: what it is
    const char* key;         // French: the key to succeed ({jump}, {boost}, {slide}, {airroll} = player's buttons)
    const char* drillHint;   // French instruction shown during the drill
    Family family;           // detector family that grades it
    float drillTimeout;      // seconds per drill attempt
    bool airborneDrill;      // the drill attempt fails when the car lands back on the floor
    std::array<const char*, 4> hudKeys;  // metrics shown first in the HUD ("" = none)
};

const MechInfo& mechInfo(Mech m);
inline const MechInfo& mechInfo(int i) { return mechInfo(static_cast<Mech>(i)); }
bool mechFromId(const std::string& id, Mech& out);

// Mechanic graded by a detector grade (musty / breezi / air roll shots get their own entries).
Mech mechOf(const MechGrade& g);
// French title of a grade, e.g. "Flick 45°", "Pinch mural (Kuxir)", "Flip reset : double touche".
std::string gradeTitle(const MechGrade& g);

} // namespace rlt::bm
