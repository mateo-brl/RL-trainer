#pragma once
// Settings of the "Première touche" module (Analyse de chaque touche, note, coach et exercices de première touche.)
// Every field is declared in FirstTouchModule::describeSettings (cvar prefix "rlt_ft_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct FirstTouchConfig {
    bool enabled = true;
};

} // namespace rlt
