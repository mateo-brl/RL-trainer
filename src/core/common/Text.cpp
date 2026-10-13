#include "core/common/Text.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rlt {

namespace {
struct Fold {
    const char* from;  // UTF-8 sequence
    const char* to;    // ASCII replacement
};
// Two- and three-byte sequences for the letters French text actually uses.
const Fold kFolds[] = {
    {"\xC3\xA0", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA4", "a"}, {"\xC3\xA7", "c"}, {"\xC3\xA8", "e"},
    {"\xC3\xA9", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
    {"\xC3\xB4", "o"}, {"\xC3\xB6", "o"}, {"\xC3\xB9", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"},
    {"\xC3\xBF", "y"}, {"\xC3\x80", "A"}, {"\xC3\x82", "A"}, {"\xC3\x87", "C"}, {"\xC3\x88", "E"},
    {"\xC3\x89", "E"}, {"\xC3\x8A", "E"}, {"\xC3\x8B", "E"}, {"\xC3\x8E", "I"}, {"\xC3\x8F", "I"},
    {"\xC3\x94", "O"}, {"\xC3\x99", "U"}, {"\xC3\x9B", "U"}, {"\xC3\x9C", "U"}, {"\xC5\x93", "oe"},
    {"\xC5\x92", "OE"}, {"\xC3\xA6", "ae"}, {"\xC2\xB0", " deg"}, {"\xC2\xAB", "\""}, {"\xC2\xBB", "\""},
    {"\xC2\xA0", " "}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x98", "'"}, {"\xE2\x80\x9C", "\""},
    {"\xE2\x80\x9D", "\""}, {"\xE2\x80\xA6", "..."}, {"\xE2\x80\x93", "-"}, {"\xE2\x86\x92", "->"},
    {"\xC3\x97", "x"},
};
} // namespace

std::string foldAccents(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        bool matched = false;
        for (const Fold& f : kFolds) {
            const size_t n = std::strlen(f.from);
            if (s.compare(i, n, f.from) == 0) {
                out += f.to;
                i += n;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        // Unknown multi-byte sequence: skip the lead byte and its continuation bytes.
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return out;
}

std::string formatTime(float seconds, bool compact) {
    if (!std::isfinite(seconds) || seconds < 0.f) seconds = 0.f;
    const int totalCs = static_cast<int>(std::lround(seconds * 100.f));
    const int minutes = totalCs / 6000;
    const int secs = (totalCs / 100) % 60;
    const int cs = totalCs % 100;
    char buf[32];
    if (compact && minutes == 0) std::snprintf(buf, sizeof(buf), "%d.%02d", secs, cs);
    else std::snprintf(buf, sizeof(buf), "%d:%02d.%02d", minutes, secs, cs);
    return buf;
}

std::string formatClock(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.f) seconds = 0.f;
    const int total = static_cast<int>(seconds);
    const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%dh%02d:%02d", h, m, s);
    else std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string formatNumber(float value, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
    // Avoid "-0.00".
    std::string out = buf;
    bool allZero = true;
    for (char c : out)
        if (c != '-' && c != '0' && c != '.') allZero = false;
    if (allZero && !out.empty() && out[0] == '-') out.erase(0, 1);
    return out;
}

std::string formatSpeed(float uus, SpeedUnit unit, int decimals, float deadbandUus) {
    if (std::fabs(uus) < deadbandUus) uus = 0.f;
    return formatNumber(convertSpeed(uus, unit), decimals) + " " + unitSuffix(unit);
}

std::string formatSignedSpeed(float uus, SpeedUnit unit, int decimals, float deadbandUus) {
    if (std::fabs(uus) <= deadbandUus) uus = 0.f;
    std::string n = formatNumber(convertSpeed(uus, unit), decimals);
    if (uus > 0.f) n = "+" + n;
    return n + " " + unitSuffix(unit);
}

std::string formatPercent(float ratio01) {
    const int p = static_cast<int>(std::lround(ratio01 * 100.f));
    return std::to_string(p) + " %";
}

} // namespace rlt
