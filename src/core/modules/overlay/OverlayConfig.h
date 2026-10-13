#pragma once
// Settings of the "Affichage" module (HUD de base : écart de vitesse, chrono de session, cercle de contact, sphère.)
// Every field is declared in OverlayModule::describeSettings (cvar prefix "rlt_ov_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct OverlayConfig {
    bool enabled = true;
};

} // namespace rlt
