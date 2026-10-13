#pragma once
// Settings of the "Tirs & flicks" module (Mode flick (balle sur le toit) et tirs de précision avec cibles dans le but.)
// Every field is declared in ShootingModule::describeSettings (cvar prefix "rlt_sh_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct ShootingConfig {
    bool enabled = true;
};

} // namespace rlt
