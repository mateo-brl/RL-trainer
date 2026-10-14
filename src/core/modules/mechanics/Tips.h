#pragma once
// French coaching lines for the movement mechanics: one short imperative sentence per error code, in plain words
// (no milliseconds or degrees: the numbers are in the detail lines and the tab), written with the player's real
// buttons (Controls.h actionPrompt) and flip deadzone where it matters.

#include <string>
#include <string_view>

#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

// Tip for one error code of a grade. Never empty (falls back to a generic line).
std::string tipText(std::string_view code, const MechGrade& g, const PlayerProfile& profile, float deadzone,
                    KeyStyle style = KeyStyle::Auto);
// Praise shown when an attempt has no error.
const char* praiseText(Mech m);
// Stable tip code for the coach: "me.<lowercase code>".
std::string tipCode(std::string_view code);
// "Air roll gauche (LB)", or "Air roll (RB) + stick à gauche" when there is no dedicated binding.
// side: -1 left, +1 right, 0 any.
std::string airRollPrompt(const PlayerProfile& profile, int side, KeyStyle style = KeyStyle::Auto);

} // namespace rlt::mech
