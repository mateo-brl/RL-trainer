#pragma once
// Settings of the "Parcours" module (Maps virtuelles : anneaux, flicks, contrôle, tir de précision.)
// Every field is declared in CoursesModule::describeSettings (cvar prefix "rlt_co_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct CoursesConfig {
    bool enabled = true;
};

} // namespace rlt
