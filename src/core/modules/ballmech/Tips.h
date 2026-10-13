#pragma once
// French coaching tips of the ball mechanics, keyed by the detectors' error codes. Each tip is one short imperative
// sentence; "{jump}", "{boost}", "{slide}" and "{airroll}" are replaced by the player's own buttons.
// Stable tip codes (for the coach's recurring-mistake counts): "bm." + lower-case error code, e.g. "bm.fr_no_reset".

#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Grade.h"

namespace rlt::bm {

// Template text of an error code (nullptr when unknown).
const char* tipTemplate(const std::string& error);
// How many points this mistake typically costs (ranking weight).
float tipWeight(const std::string& error);
// "bm.fr_no_reset" for "FR_NO_RESET".
std::string tipCode(const std::string& error);

// Replaces the button placeholders with the player's bindings ("le saut (A)", "l'air roll gauche (LB)"), in the
// button style the player chose (Settings::profile.buttonStyle, see Controls.h). The style has no default on purpose:
// every caller passes keyStyleFromIndex(settings.profile.buttonStyle), so PlayStation players never see Xbox names.
std::string fillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style);
std::string airRollPrompt(const PlayerProfile& profile, KeyStyle style);

// Tips for a list of error codes, sorted by weight (stable for equal weights), duplicates removed.
std::vector<Tip> buildTips(const std::vector<std::string>& errors, const PlayerProfile& profile, KeyStyle style);

} // namespace rlt::bm
