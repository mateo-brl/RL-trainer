// Pure helpers of the coach: dates, statistics, formatting, sanitizing and the feedback fading rule.

#include "tests/coach/CoachTestUtil.h"

#include "core/common/Text.h"

#include "core/modules/coach/CoachUtil.h"

using namespace rlt;
using namespace rlt::coach;

TEST(coach_util_dates_roundtrip_and_validation) {
    long n = -1;
    CHECK(dayNumber("1970-01-01", n));
    CHECK_EQ(n, 0L);
    CHECK(dayNumber("2026-10-13", n));
    CHECK_EQ(dateFromDayNumber(n), std::string("2026-10-13"));
    CHECK(dayNumber("2000-02-29", n));   // leap year (divisible by 400)
    CHECK(!dayNumber("1900-02-29", n));  // not a leap year, and before 1970
    CHECK(!dayNumber("2026-02-29", n));
    CHECK(!dayNumber("2026-02-30", n));
    CHECK(!dayNumber("2026-13-01", n));
    CHECK(!dayNumber("2026-00-10", n));
    CHECK(!dayNumber("2026-1-01", n));
    CHECK(!dayNumber("2026/10/13", n));
    CHECK(!dayNumber("", n));
    CHECK(!dayNumber("abcd-ef-gh", n));
    CHECK(validDate("2028-02-29"));
    CHECK(!validDate("2027-02-29"));
}

TEST(coach_util_days_between_and_shift) {
    int d = 0;
    CHECK(daysBetween("2026-10-13", "2026-10-17", d));
    CHECK_EQ(d, 4);
    CHECK(daysBetween("2028-02-28", "2028-03-01", d));
    CHECK_EQ(d, 2);
    CHECK(daysBetween("2026-12-31", "2027-01-01", d));
    CHECK_EQ(d, 1);
    CHECK(daysBetween("2026-10-17", "2026-10-13", d));
    CHECK_EQ(d, -4);
    CHECK(!daysBetween("", "2026-10-13", d));
    CHECK_EQ(shiftDate("2026-03-01", -1), std::string("2026-02-28"));
    CHECK_EQ(shiftDate("2026-12-31", 1), std::string("2027-01-01"));
    CHECK_EQ(shiftDate("bad", 1), std::string());
}

TEST(coach_util_slope_and_mean) {
    CHECK_NEAR(linearSlope({}), 0.f, 1e-6f);
    CHECK_NEAR(linearSlope({50.f}), 0.f, 1e-6f);
    CHECK_NEAR(linearSlope({60.f, 60.f, 60.f, 60.f}), 0.f, 1e-6f);
    std::vector<float> rising;
    for (int i = 0; i < 20; ++i) rising.push_back(30.f + 2.f * static_cast<float>(i));
    CHECK_NEAR(linearSlope(rising), 2.f, 1e-4f);
    std::vector<float> falling(rising.rbegin(), rising.rend());
    CHECK_NEAR(linearSlope(falling), -2.f, 1e-4f);
    // Noisy but flat: zig-zag around 70.
    std::vector<float> zig;
    for (int i = 0; i < 20; ++i) zig.push_back(i % 2 ? 75.f : 65.f);
    CHECK(std::fabs(linearSlope(zig)) < 0.2f);

    CHECK_NEAR(meanOfLast({}, 5), 0.f, 1e-6f);
    CHECK_NEAR(meanOfLast({10.f, 20.f, 30.f, 40.f}, 2), 35.f, 1e-5f);
    CHECK_NEAR(meanOfLast({10.f, 20.f, 30.f, 40.f}, 0), 25.f, 1e-5f);
    CHECK_NEAR(meanOfLast({10.f, 20.f}, 10), 15.f, 1e-5f);
}

TEST(coach_util_trend_classification) {
    CHECK(classifyTrend(2.f, 4, 0.5f) == Trend::Unknown);  // not enough points
    CHECK(classifyTrend(2.f, 5, 0.5f) == Trend::Up);
    CHECK(classifyTrend(0.5f, 20, 0.5f) == Trend::Up);
    CHECK(classifyTrend(0.49f, 20, 0.5f) == Trend::Stable);
    CHECK(classifyTrend(-0.49f, 20, 0.5f) == Trend::Stable);
    CHECK(classifyTrend(-0.5f, 20, 0.5f) == Trend::Down);
    CHECK_EQ(std::string(trendLabel(Trend::Up)), std::string("en progrès"));
    CHECK_EQ(std::string(trendLabel(Trend::Down)), std::string("en baisse"));
    CHECK_EQ(std::string(trendLabel(Trend::Stable)), std::string("stable"));
}

TEST(coach_util_format_value) {
    CHECK_EQ(formatValue(1.92f, "s"), std::string("1.92 s"));
    CHECK_EQ(formatValue(260.f, "uu/s", true), std::string("+260 uu/s"));
    CHECK_EQ(formatValue(-260.4f, "uu/s", true), std::string("-260 uu/s"));
    CHECK_EQ(formatValue(12.345f, "m"), std::string("12.3 m"));
    CHECK_EQ(formatValue(-0.001f, "", true), std::string("0.00"));
    CHECK_EQ(formatValue(87.6f, "%"), std::string("88 %"));
    CHECK_EQ(formatValue(37.6f, "°"), std::string("38°"));  // no space: folds to "38 deg" on the HUD
    CHECK_EQ(hudText(formatValue(37.6f, "°"), false), std::string("38 deg"));
    CHECK_EQ(formatValue(-12.f, "°", true), std::string("-12°"));
    CHECK_EQ(formatValue(30.f, "deg"), std::string("30 deg"));
    CHECK_EQ(formatValue(std::nanf(""), "s"), std::string("0 s"));
    CHECK_EQ(formatValue(2.f, "s"), std::string("2 s"));
    CHECK_EQ(formatScore(81.5f), std::string("82"));
    CHECK_EQ(formatScore(std::nanf("")), std::string("0"));
}

TEST(coach_util_format_record) {
    // Times keep their centiseconds, m:ss.cc from one minute.
    CHECK_EQ(formatRecord(1.92f, "s"), std::string("1.92 s"));
    CHECK_EQ(formatRecord(2.f, "s"), std::string("2.00 s"));
    CHECK_EQ(formatRecord(12.34f, "s"), std::string("12.34 s"));
    CHECK_EQ(formatRecord(59.996f, "s"), std::string("1:00.00"));
    CHECK_EQ(formatRecord(124.62f, "s"), std::string("2:04.62"));
    CHECK_EQ(formatRecord(124.62f, "s", 1), std::string("2:04.620"));
    CHECK_EQ(formatRecord(std::nanf(""), "s"), std::string("0.00 s"));
    CHECK_EQ(formatRecord(-3.f, "s"), std::string("0.00 s"));
    // Other units: the usual formatting.
    CHECK_EQ(formatRecord(98.44f, "km/h"), std::string("98.4 km/h"));
    CHECK_EQ(formatRecord(2345.6f, "uu/s"), std::string("2346 uu/s"));
    CHECK_EQ(formatRecord(87.f, ""), std::string("87"));
    CHECK_EQ(formatRecord(98.44f, "km/h", 1), std::string("98.44 km/h"));

    std::string a, b;
    // Reviewer case: 124.80 -> 124.62 s printed "125 s (avant 125 s)".
    formatRecordPair(124.62f, 124.80f, "s", a, b);
    CHECK_EQ(a, std::string("2:04.62"));
    CHECK_EQ(b, std::string("2:04.80"));
    formatRecordPair(12.34f, 12.38f, "s", a, b);
    CHECK_EQ(a, std::string("12.34 s"));
    CHECK_EQ(b, std::string("12.38 s"));
    // A gap under a centisecond gets a third decimal.
    formatRecordPair(12.341f, 12.344f, "s", a, b);
    CHECK_EQ(a, std::string("12.341 s"));
    CHECK_EQ(b, std::string("12.344 s"));
    // Same precision for both values, more decimals while they still look the same.
    formatRecordPair(2105.4f, 2105.f, "uu/s", a, b);
    CHECK_EQ(a, std::string("2105.4 uu/s"));
    CHECK_EQ(b, std::string("2105.0 uu/s"));
    formatRecordPair(87.4f, 87.2f, "%", a, b);
    CHECK_EQ(a, std::string("87.4 %"));
    CHECK_EQ(b, std::string("87.2 %"));
    formatRecordPair(101.f, 99.5f, "km/h", a, b);
    CHECK_EQ(a, std::string("101.0 km/h"));
    CHECK_EQ(b, std::string("99.5 km/h"));
    formatRecordPair(85.f, 80.f, "", a, b);
    CHECK_EQ(a, std::string("85"));
    CHECK_EQ(b, std::string("80"));
    // Whatever the values, two different records never print the same text.
    for (int i = 0; i < 2000; ++i) {
        const float prev = 0.5f + 0.173f * static_cast<float>(i);
        const float now = prev - 0.0021f;
        formatRecordPair(now, prev, i % 2 ? "s" : "uu/s", a, b);
        CHECK(a != b);
    }
}

TEST(coach_util_wrap_text_utf8) {
    const std::string tip = "Relâche le boost juste avant le contact pour garder la balle près de toi.";
    const auto lines = wrapText(tip, 30);
    CHECK(lines.size() >= 3);
    std::string joined;
    for (const auto& l : lines) {
        CHECK(utf8Length(l) <= 30);
        joined += (joined.empty() ? "" : " ") + l;
    }
    CHECK_EQ(joined, tip);
    CHECK_EQ(utf8Length("é"), static_cast<size_t>(1));
    CHECK(wrapText("", 20).empty());
    // A single word longer than the limit stays whole.
    const auto big = wrapText("supercalifragilisticexpialidocious", 10);
    CHECK_EQ(big.size(), static_cast<size_t>(1));
}

TEST(coach_util_sanitize_and_split) {
    CHECK_EQ(sanitizeId("mech.speedflip"), std::string("mech.speedflip"));
    CHECK_EQ(sanitizeId("ft.too fast"), std::string("ft.too_fast"));
    CHECK_EQ(sanitizeId("a,b;c|d=e"), std::string("a_b_c_d_e"));
    CHECK_EQ(sanitizeId("   "), std::string());
    CHECK_EQ(sanitizeId(std::string(100, 'x'), 10).size(), static_cast<size_t>(10));
    CHECK_EQ(sanitizeField("a\tb\nc"), std::string("a b c"));
    // Never cut a UTF-8 sequence: "é" is two bytes.
    const std::string cut = sanitizeField("abé", 3);
    CHECK_EQ(cut, std::string("ab"));
    CHECK_EQ(sanitizeField("abé", 4), std::string("abé"));
    const auto parts = split("a;;b", ';');
    CHECK_EQ(parts.size(), static_cast<size_t>(3));
    CHECK_EQ(parts[1], std::string());
    CHECK_EQ(lowerFirst("Écart de vitesse"), std::string("écart de vitesse"));
    CHECK_EQ(lowerFirst("Temps"), std::string("temps"));
    CHECK_EQ(lowerFirst("KPH"), std::string("KPH"));
}

TEST(coach_util_feedback_fading_rule) {
    FadingRule rule;  // enabled, > 0.8 over 10, every 3rd success
    int hidden = 0;
    // Not enough history: always shown.
    CHECK(shouldShowCard(rule, 9, 1.f, true, false, hidden));
    // Rate exactly 0.8 is not "> 80 %".
    CHECK(shouldShowCard(rule, 10, 0.8f, true, false, hidden));
    CHECK_EQ(hidden, 0);
    // Mastered: successes 1 and 2 hidden, 3rd shown, then the cycle restarts.
    CHECK(!shouldShowCard(rule, 10, 0.9f, true, false, hidden));
    CHECK(!shouldShowCard(rule, 10, 0.9f, true, false, hidden));
    CHECK(shouldShowCard(rule, 10, 0.9f, true, false, hidden));
    CHECK_EQ(hidden, 0);
    CHECK(!shouldShowCard(rule, 10, 0.9f, true, false, hidden));
    // Failures are always shown and do not reset the success counter.
    CHECK(shouldShowCard(rule, 10, 0.9f, false, false, hidden));
    CHECK_EQ(hidden, 1);
    // A record is always celebrated.
    CHECK(shouldShowCard(rule, 10, 1.f, true, true, hidden));
    CHECK_EQ(hidden, 0);
    // Disabled: always shown.
    rule.enabled = false;
    CHECK(shouldShowCard(rule, 10, 1.f, true, false, hidden));
    // every = 1 shows every success.
    rule.enabled = true;
    rule.every = 1;
    CHECK(shouldShowCard(rule, 10, 1.f, true, false, hidden));
    rule.every = 0;  // invalid, treated as 1
    CHECK(shouldShowCard(rule, 10, 1.f, true, false, hidden));
}
