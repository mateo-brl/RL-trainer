#include "core/modules/coach/CoachUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rlt {
namespace coach {

// ---- Dates ---------------------------------------------------------------------------------------------
namespace {

// Howard Hinnant's civil calendar algorithms (proleptic Gregorian).
long daysFromCivil(long y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(long z, long& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = yoe + era * 400 + (m <= 2 ? 1 : 0);
}

bool digits(const std::string& s, size_t from, size_t count, int& out) {
    out = 0;
    for (size_t i = from; i < from + count; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        out = out * 10 + (c - '0');
    }
    return true;
}

} // namespace

bool dayNumber(const std::string& date, long& out) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    int y = 0, m = 0, d = 0;
    if (!digits(date, 0, 4, y) || !digits(date, 5, 2, m) || !digits(date, 8, 2, d)) return false;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    const long n = daysFromCivil(y, m, d);
    long ry = 0;
    int rm = 0, rd = 0;
    civilFromDays(n, ry, rm, rd);
    if (ry != y || rm != m || rd != d) return false;  // e.g. 2026-02-30
    out = n;
    return true;
}

std::string dateFromDayNumber(long day) {
    long y = 0;
    int m = 0, d = 0;
    civilFromDays(day, y, m, d);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04ld-%02d-%02d", y, m, d);
    return buf;
}

bool validDate(const std::string& date) {
    long n = 0;
    return dayNumber(date, n);
}

bool daysBetween(const std::string& from, const std::string& to, int& out) {
    long a = 0, b = 0;
    if (!dayNumber(from, a) || !dayNumber(to, b)) return false;
    out = static_cast<int>(b - a);
    return true;
}

std::string shiftDate(const std::string& date, int days) {
    long n = 0;
    if (!dayNumber(date, n)) return std::string();
    return dateFromDayNumber(n + days);
}

// ---- Statistics ----------------------------------------------------------------------------------------
float linearSlope(const std::vector<float>& values) {
    const size_t n = values.size();
    if (n < 2) return 0.f;
    double sx = 0.0, sy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sx += static_cast<double>(i);
        sy += static_cast<double>(values[i]);
    }
    const double mx = sx / static_cast<double>(n), my = sy / static_cast<double>(n);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double dx = static_cast<double>(i) - mx;
        num += dx * (static_cast<double>(values[i]) - my);
        den += dx * dx;
    }
    return den > 0.0 ? static_cast<float>(num / den) : 0.f;
}

float meanOfLast(const std::vector<float>& values, size_t n) {
    if (values.empty()) return 0.f;
    const size_t count = (n == 0 || n >= values.size()) ? values.size() : n;
    double sum = 0.0;
    for (size_t i = values.size() - count; i < values.size(); ++i) sum += static_cast<double>(values[i]);
    return static_cast<float>(sum / static_cast<double>(count));
}

Trend classifyTrend(float slope, size_t points, float threshold, size_t minPoints) {
    if (points < minPoints || points < 2) return Trend::Unknown;
    if (slope >= threshold) return Trend::Up;
    if (slope <= -threshold) return Trend::Down;
    return Trend::Stable;
}

// ---- Formatting ----------------------------------------------------------------------------------------
int autoDecimals(float value, const std::string& unit) {
    if (!std::isfinite(value)) return 0;
    if (unit == "%" || unit == "°" || unit == "deg" || unit == "pts") return 0;
    if (std::fabs(value - std::round(value)) < 1e-4f) return 0;  // "0 uu/s", "2 s"
    const float a = std::fabs(value);
    return a >= 100.f ? 0 : (a >= 10.f ? 1 : 2);
}

std::string formatFixedValue(float value, const std::string& unit, int decimals, bool withSign) {
    if (!std::isfinite(value)) value = 0.f;
    if (decimals < 0) decimals = 0;
    if (decimals > 4) decimals = 4;
    // Avoid "-0" and "+0.00" style noise.
    const float scale = std::pow(10.f, static_cast<float>(decimals));
    if (std::round(std::fabs(value) * scale) == 0.f) value = 0.f;
    char buf[48];
    if (withSign && value > 0.f) std::snprintf(buf, sizeof(buf), "+%.*f", decimals, static_cast<double>(value));
    else std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
    std::string s = buf;
    if (unit == "°") s += unit;  // French typography: "38°"
    else if (!unit.empty()) s += " " + unit;
    return s;
}

std::string formatValue(float value, const std::string& unit, bool withSign) {
    return formatFixedValue(value, unit, autoDecimals(value, unit), withSign);
}

namespace {

// "12.34 s" below one minute, "2:04.62" above, with 2 or 3 decimals.
std::string formatRecordTime(float seconds, int decimals) {
    if (!std::isfinite(seconds) || seconds < 0.f) seconds = 0.f;
    decimals = decimals < 2 ? 2 : (decimals > 3 ? 3 : decimals);
    const long long scale = decimals == 2 ? 100 : 1000;
    const long long total = std::llround(static_cast<double>(seconds) * static_cast<double>(scale));
    const long long minutes = total / (60 * scale);
    const long long rest = total - minutes * 60 * scale;
    char buf[48];
    if (minutes > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld.%0*lld", minutes, rest / scale, decimals, rest % scale);
    else std::snprintf(buf, sizeof(buf), "%lld.%0*lld s", rest / scale, decimals, rest % scale);
    return buf;
}

} // namespace

std::string formatRecord(float value, const std::string& unit, int extraDecimals) {
    const int extra = extraDecimals < 0 ? 0 : (extraDecimals > 2 ? 2 : extraDecimals);
    if (unit == "s") return formatRecordTime(value, 2 + extra);
    const int base = autoDecimals(value, unit);
    return extra == 0 ? formatValue(value, unit) : formatFixedValue(value, unit, std::min(3, base + extra));
}

void formatRecordPair(float value, float previous, const std::string& unit, std::string& valueText,
                      std::string& previousText) {
    if (unit == "s") {
        for (int d = 2; d <= 3; ++d) {
            valueText = formatRecordTime(value, d);
            previousText = formatRecordTime(previous, d);
            if (valueText != previousText) return;
        }
        return;
    }
    // Same precision for both values, then more decimals while they still print the same.
    const int base = std::max(autoDecimals(value, unit), autoDecimals(previous, unit));
    for (int d = base; d <= std::max(base, 3); ++d) {
        valueText = formatFixedValue(value, unit, d);
        previousText = formatFixedValue(previous, unit, d);
        if (valueText != previousText) return;
    }
}

std::string formatScore(float score) {
    if (!std::isfinite(score)) score = 0.f;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(score)));
    return buf;
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::vector<std::string> wrapText(const std::string& text, size_t maxChars) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    if (maxChars < 8) maxChars = 8;
    std::string line;
    size_t lineLen = 0;
    size_t i = 0;
    while (i < text.size()) {
        // Next word (up to a space).
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const size_t wl = utf8Length(word);
        if (!word.empty()) {
            if (lineLen > 0 && lineLen + 1 + wl > maxChars) {
                lines.push_back(line);
                line.clear();
                lineLen = 0;
            }
            if (lineLen > 0) {
                line += ' ';
                ++lineLen;
            }
            line += word;
            lineLen += wl;
        }
        i = j + 1;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

std::string lowerFirst(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    if (upper(r[0])) {
        // Keep acronyms ("KPH", "PB") untouched.
        if (!(r.size() > 1 && upper(r[1]))) r[0] = static_cast<char>(r[0] - 'A' + 'a');
    } else if (r.size() > 1 && static_cast<unsigned char>(r[0]) == 0xC3) {
        // Latin-1 capitals in UTF-8 ("É" = C3 89 -> "é" = C3 A9), except the multiplication sign.
        const unsigned char c = static_cast<unsigned char>(r[1]);
        if (c >= 0x80 && c <= 0x9E && c != 0x97) r[1] = static_cast<char>(c + 0x20);
    }
    return r;
}

// ---- Sanitizing ----------------------------------------------------------------------------------------
std::string sanitizeId(const std::string& s, size_t maxLen) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (out.size() >= maxLen) break;
        if (c <= ' ' || c >= 0x7F) {
            if (!out.empty() && out.back() != '_') out.push_back('_');
            continue;
        }
        if (c == ',' || c == ';' || c == '|' || c == '=' || c == '\\') {
            out.push_back('_');
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

std::string sanitizeField(const std::string& s, size_t maxLen) {
    std::string out;
    out.reserve(s.size() < maxLen ? s.size() : maxLen);
    for (char c : s) {
        if (out.size() >= maxLen) break;
        out.push_back((c == '\t' || c == '\n' || c == '\r') ? ' ' : c);
    }
    // Never cut a UTF-8 sequence in half.
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0) {
        size_t start = out.size() - 1;
        while (start > 0 && (static_cast<unsigned char>(out[start]) & 0xC0) == 0x80) --start;
        const unsigned char lead = static_cast<unsigned char>(out[start]);
        const size_t need = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1));
        if (out.size() - start >= need) break;
        out.resize(start);
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t i = 0;
    while (true) {
        const size_t j = s.find(sep, i);
        if (j == std::string::npos) {
            out.push_back(s.substr(i));
            break;
        }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// ---- Feedback fading -----------------------------------------------------------------------------------
bool shouldShowCard(const FadingRule& rule, int prevCount, float prevRate, bool success, bool newRecord,
                    int& hiddenSuccesses) {
    const bool fading = rule.enabled && prevCount >= rule.window && prevRate > rule.rate;
    if (!fading) {
        hiddenSuccesses = 0;
        return true;
    }
    if (newRecord) {
        hiddenSuccesses = 0;
        return true;
    }
    if (!success) return true;  // always explain a failure
    const int every = rule.every < 1 ? 1 : rule.every;
    ++hiddenSuccesses;
    if (hiddenSuccesses >= every) {
        hiddenSuccesses = 0;
        return true;
    }
    return false;
}

} // namespace coach
} // namespace rlt
