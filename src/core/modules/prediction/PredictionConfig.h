#pragma once
// Settings of the "Prédiction" module (Trajectoire de la balle avec rebonds et point d'interception.)
// Every field is declared in PredictionModule::describeSettings (cvar prefix "rlt_pr_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct PredictionConfig {
    bool enabled = true;
};

} // namespace rlt
