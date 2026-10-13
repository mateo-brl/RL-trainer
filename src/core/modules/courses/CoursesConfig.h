#pragma once
// Settings of the "Parcours" module (Maps virtuelles : parcours d'anneaux (vol, sol, air dribble) et parcours de contrôle (dribble, slalom, rattrapages).)
// Every field is declared in CoursesModule::describeSettings (cvar prefix "rlt_co_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct CoursesConfig {
    bool enabled = true;
};

} // namespace rlt
