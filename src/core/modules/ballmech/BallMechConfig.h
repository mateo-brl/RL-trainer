#pragma once
// Settings of the "Mécas balle" module (Détection et notation des mécas avec balle (flip reset, ceiling shot, flicks, double tap, air dribble...).)
// Every field is declared in BallMechModule::describeSettings (cvar prefix "rlt_bm_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct BallMechConfig {
    bool enabled = true;
};

} // namespace rlt
