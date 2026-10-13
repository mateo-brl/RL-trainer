#include "core/common/Color.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace rlt {

Color mix(const Color& a, const Color& b, float t) {
    t = std::max(0.f, std::min(1.f, t));
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseColor(const std::string& text, Color& out) {
    size_t first = 0, last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    const std::string s = text.substr(first, last - first);
    if (s.empty()) return false;

    if (s[0] == '#') {
        const std::string h = s.substr(1);
        if (h.size() != 6 && h.size() != 8) return false;
        int v[4] = {0, 0, 0, 255};
        for (size_t i = 0; i < h.size() / 2; ++i) {
            const int hi = hexVal(h[2 * i]), lo = hexVal(h[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            v[i] = hi * 16 + lo;
        }
        out = Color(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3]));
        return true;
    }

    // "(R, G, B, A)" or "R G B A": keep digits, dots, minus signs; everything else separates numbers.
    std::vector<float> nums;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            try {
                nums.push_back(std::stof(cur));
            } catch (...) {
                nums.clear();
                nums.push_back(-1e9f); // poison
            }
            cur.clear();
        }
    };
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-') cur.push_back(c);
        else flush();
    }
    flush();
    if (nums.size() < 3 || nums.size() > 4) return false;
    for (float n : nums)
        if (n < 0.f || n > 255.f) return false;
    out = Color(nums[0], nums[1], nums[2], nums.size() == 4 ? nums[3] : 255.f);
    return true;
}

std::string toHex(const Color& c) {
    auto b = [](float v) { return static_cast<int>(std::max(0.f, std::min(255.f, v)) + 0.5f); };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
    return buf;
}

} // namespace rlt
