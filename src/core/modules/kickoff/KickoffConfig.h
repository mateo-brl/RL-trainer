#pragma once
// Settings of the "Kickoff" module (Entraînement au kickoff avec chrono, fantôme et notes.)
// Every field is declared in KickoffModule::describeSettings (cvar prefix "rlt_ko_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct KickoffConfig {
    bool enabled = true;
};

} // namespace rlt
