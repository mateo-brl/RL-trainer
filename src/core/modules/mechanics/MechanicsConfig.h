#pragma once
// Settings of the "Mécaniques" module (Détection et notation des mécaniques (speedflip, wavedash, flip reset...).)
// Every field is declared in MechanicsModule::describeSettings (cvar prefix "rlt_me_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct MechanicsConfig {
    bool enabled = true;
};

} // namespace rlt
