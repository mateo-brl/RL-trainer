#pragma once
// Text helpers. All user-facing strings in the code base are French, UTF-8 encoded.
// The game canvas and chat may not render UTF-8 accents, so HUD/chat text goes through hudText().

#include <string>

#include "core/common/Units.h"

namespace rlt {

// Replaces common French accented letters (UTF-8) with ASCII ("é" -> "e", "œ" -> "oe", "°" -> " deg")
// and drops any other non-ASCII byte. Used for canvas, chat and toast text.
std::string foldAccents(const std::string& utf8);

// Text for the in-game canvas/chat: folded to ASCII unless `keepAccents` is true.
inline std::string hudText(const std::string& utf8, bool keepAccents) { return keepAccents ? utf8 : foldAccents(utf8); }

// "1:02.35" (m:ss.cc) or "12.35" below one minute when `compact` is true.
std::string formatTime(float seconds, bool compact = true);

// "1h02:05" style session clock (h:mm:ss, hours only when > 0).
std::string formatClock(float seconds);

// Fixed precision number, e.g. formatNumber(3.14159f, 2) == "3.14".
std::string formatNumber(float value, int decimals);

// Speed in the given unit with suffix, e.g. "45.20 KPH". Values in (-deadband, deadband) uu/s print as 0.
std::string formatSpeed(float uus, SpeedUnit unit, int decimals = 2, float deadbandUus = 0.f);

// Signed speed: "+3.20 KPH" / "-1.10 KPH" / "0.00 KPH".
std::string formatSignedSpeed(float uus, SpeedUnit unit, int decimals = 2, float deadbandUus = 1.f);

// Percentage "87 %" (French typography uses a space before %).
std::string formatPercent(float ratio01);

} // namespace rlt
