#pragma once
// Settings of the "Maps" module (Maps workshop : catalogue, profils automatiques, chronos et stats par map.)
// Every field is declared in MapsModule::describeSettings (cvar prefix "rlt_map_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct MapsConfig {
    bool enabled = true;
};

} // namespace rlt
