#include "core/modules/courses/CourseCode.h"

#include <algorithm>

namespace rlt::courses {

namespace {

constexpr char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int b32Value(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    if (ch == 'O') ch = '0';
    if (ch == 'I' || ch == 'L') ch = '1';
    for (int i = 0; i < 32; ++i)
        if (kB32[i] == ch) return i;
    return -1;
}

// Weighted sum mod 31 of the base32 values of the type, difficulty and seed characters (positions 5..22),
// weights 1..17: every single substitution (except 0 <-> Z) and every swap of two different neighbours changes
// the check character. The fixed "RLT1-" prefix is validated separately.
char codeCheck(const std::string& body) {
    uint32_t sum = 0, w = 1;
    for (size_t i = 5; i < body.size() && i < 23; ++i) {
        if (body[i] == '-') continue;
        const int v = b32Value(body[i]);
        sum += w * static_cast<uint32_t>(v < 0 ? 0 : v);
        ++w;
    }
    return kB32[sum % 31u];
}

std::string prefix() { return "RLT" + std::to_string(kGeneratorVersion) + "-"; }

} // namespace

std::string encodeCourseCode(uint64_t seed, CourseType type, int difficultyPct) {
    std::string s = prefix();
    s += kTypeLetters[static_cast<int>(type)];
    const int dp = std::clamp(difficultyPct, 0, 100);
    s += static_cast<char>('0' + dp / 100);
    s += static_cast<char>('0' + (dp / 10) % 10);
    s += static_cast<char>('0' + dp % 10);
    s += '-';
    char buf[13];
    uint64_t v = seed;
    for (int i = 12; i >= 0; --i) {  // 65 bits >= 64
        buf[i] = kB32[v & 31u];
        v >>= 5;
    }
    s.append(buf, 13);
    s += '-';
    s += codeCheck(s);
    return s;
}

std::optional<DecodedCode> decodeCourseCode(const std::string& code) {
    // RLT1-A050-XXXXXXXXXXXXX-C -> 4 + 1 + 4 + 1 + 13 + 1 + 1 = 25 characters
    if (code.size() != kCodeLength || code.compare(0, 5, prefix()) != 0) return std::nullopt;
    if (code[9] != '-' || code[23] != '-') return std::nullopt;
    if (codeCheck(code.substr(0, 24)) != code[24]) return std::nullopt;
    int t = -1;
    for (int i = 0; i < kCourseTypeCount; ++i)
        if (code[5] == kTypeLetters[i]) t = i;
    if (t < 0) return std::nullopt;
    int dp = 0;
    for (int i = 6; i < 9; ++i) {
        if (code[static_cast<size_t>(i)] < '0' || code[static_cast<size_t>(i)] > '9') return std::nullopt;
        dp = dp * 10 + (code[static_cast<size_t>(i)] - '0');
    }
    if (dp > 100) return std::nullopt;
    uint64_t v = 0;
    for (int i = 10; i < 23; ++i) {
        const int b = b32Value(code[static_cast<size_t>(i)]);
        if (b < 0) return std::nullopt;
        if (i == 10 && b > 15) return std::nullopt;  // 13 * 5 = 65 bits: the top character carries bits 60..63
        v = (v << 5) | static_cast<uint64_t>(b);
    }
    DecodedCode out;
    out.seed = v;
    out.type = static_cast<CourseType>(t);
    out.difficultyPct = dp;
    return out;
}

std::string normalizeCourseCode(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '_' || ch == '"' || ch == '\'') continue;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        out.push_back(ch);
        if (out.size() > 64) break;  // garbage: stop early, the decoder rejects it
    }
    // Crockford look-alikes in the seed and check characters (O -> 0, I/L -> 1), so the check still matches.
    if (out.size() == kCodeLength) {
        for (size_t i = 10; i < kCodeLength; ++i) {
            if (i == 23) continue;
            char& ch = out[i];
            if (ch == 'O') ch = '0';
            else if (ch == 'I' || ch == 'L') ch = '1';
        }
    }
    return out;
}

std::optional<DecodedCode> parseCourseCode(const std::string& raw) { return decodeCourseCode(normalizeCourseCode(raw)); }

} // namespace rlt::courses
