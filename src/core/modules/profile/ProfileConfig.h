#pragma once
// Settings of the "Ma config" module (Détection automatique de ta config Rocket League (touches, caméra, voiture).)
// Every field is declared in ProfileModule::describeSettings (cvar prefix "rlt_pf_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct ProfileConfig {
    bool enabled = true;
};

} // namespace rlt
