#pragma once
// Small pure helpers of the coach module: dates, statistics, number and text formatting, the feedback
// fading rule. No state, no allocation-heavy work: everything is unit-tested in tests/coach.

#include <cstddef>
#include <string>
#include <vector>

#include "core/modules/coach/CoachTypes.h"

namespace rlt {
namespace coach {

// ---- Dates ("YYYY-MM-DD", the player's local date from GameContext::localDate) ------------------------
// Day number since 1970-01-01. Returns false for anything that is not a valid calendar date.
bool dayNumber(const std::string& date, long& out);
std::string dateFromDayNumber(long day);
bool validDate(const std::string& date);
// Whole days from `from` to `to` (negative when `to` is earlier). Returns false when a date is invalid.
bool daysBetween(const std::string& from, const std::string& to, int& out);
// The day before / after.
std::string shiftDate(const std::string& date, int days);

// ---- Statistics ---------------------------------------------------------------------------------------
// Least-squares slope of `values` against their index (points per attempt). 0 with fewer than 2 values.
float linearSlope(const std::vector<float>& values);
// Mean of the last `n` values (all when n == 0 or n >= size). 0 when empty.
float meanOfLast(const std::vector<float>& values, size_t n);
// Trend from a slope: needs at least `minPoints` points, |slope| >= threshold to be Up/Down.
Trend classifyTrend(float slope, size_t points, float threshold, size_t minPoints = 5);

// ---- Formatting ---------------------------------------------------------------------------------------
// "1.92 s", "+260 uu/s", "35°", "87 %": decimals from the magnitude (0 above 100, 1 above 10, else 2).
// Degrees stick to the number ("38°", folded to "38 deg" on the HUD); other units follow a space.
std::string formatValue(float value, const std::string& unit, bool withSign = false);
// Decimals formatValue would pick for `value` in `unit`.
int autoDecimals(float value, const std::string& unit);
// Same layout as formatValue with an explicit number of decimals (0..4).
std::string formatFixedValue(float value, const std::string& unit, int decimals, bool withSign = false);
// Personal record values. Times (unit "s") keep their centiseconds and switch to m:ss.cc from one minute
// ("12.34 s", "2:04.62"), so a record never loses the precision that made it a record. Other units use
// formatValue. `extraDecimals` adds precision (0..2, used to tell two close records apart).
std::string formatRecord(float value, const std::string& unit, int extraDecimals = 0);
// New and previous record with the same precision, adding decimals (up to 3) until the two texts differ:
// 124.62 / 124.80 s -> "2:04.62" / "2:04.80", 12.341 / 12.344 s -> "12.341 s" / "12.344 s".
void formatRecordPair(float value, float previous, const std::string& unit, std::string& valueText,
                      std::string& previousText);
// Score as an integer string "82".
std::string formatScore(float score);
// Number of UTF-8 code points.
size_t utf8Length(const std::string& s);
// Greedy word wrap on code points; never returns an empty vector for a non-empty input.
std::vector<std::string> wrapText(const std::string& text, size_t maxChars);
// Lower-case first letter for ASCII (used to embed labels in sentences).
std::string lowerFirst(const std::string& s);

// ---- Sanitizing (persisted fields) --------------------------------------------------------------------
// Skill ids / tip codes: printable ASCII without separators, bounded length. Empty when nothing is left.
std::string sanitizeId(const std::string& s, size_t maxLen = 64);
// Free text stored in tab-separated records: tabs and new lines become spaces, bounded length.
std::string sanitizeField(const std::string& s, size_t maxLen = 160);
std::vector<std::string> split(const std::string& s, char sep);

// ---- Feedback fading (guidance hypothesis, Winstein & Schmidt 1990) -----------------------------------
struct FadingRule {
    bool enabled = true;
    float rate = 0.8f;  // fade when the recent success rate is strictly above this
    int window = 10;    // attempts needed before fading starts
    int every = 3;      // while fading, show every Nth success
};
// Decides whether the card of an attempt is shown. `prevCount` / `prevRate` describe the attempts BEFORE this
// one (at most `rule.window`). `hiddenSuccesses` is the per-skill counter of successes since the last shown
// success while fading; it is updated.
bool shouldShowCard(const FadingRule& rule, int prevCount, float prevRate, bool success, bool newRecord,
                    int& hiddenSuccesses);

} // namespace coach
} // namespace rlt
