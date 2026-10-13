#pragma once
// Settings of the "Coach & progression" module (Feedback, records, historique, plan d'entraînement adaptatif.)
// Every field is declared in CoachModule::describeSettings (cvar prefix "rlt_coach_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct CoachConfig {
    bool enabled = true;
};

} // namespace rlt
