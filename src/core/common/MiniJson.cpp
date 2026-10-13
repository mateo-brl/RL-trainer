#include "core/common/MiniJson.h"

#include <cctype>
#include <cstdlib>

namespace rlt::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}

    bool parseDocument(Value& out) {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        return i_ == t_.size();
    }

private:
    static constexpr int kMaxDepth = 64;

    void skipWs() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
    }

    bool literal(const char* word) {
        size_t k = 0;
        while (word[k]) {
            if (i_ + k >= t_.size() || t_[i_ + k] != word[k]) return false;
            ++k;
        }
        i_ += k;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(unsigned& out) {
        if (i_ + 4 > t_.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[i_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (i_ >= t_.size() || t_[i_] != '"') return false;
        ++i_;
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= t_.size()) return false;
            const char e = t_[i_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                    i_ += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool parseNumber(double& out) {
        const size_t start = i_;
        if (i_ < t_.size() && (t_[i_] == '-' || t_[i_] == '+')) ++i_;
        while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || t_[i_] == '.' || t_[i_] == 'e' ||
                                  t_[i_] == 'E' || t_[i_] == '-' || t_[i_] == '+'))
            ++i_;
        if (i_ == start) return false;
        const std::string num = t_.substr(start, i_ - start);
        char* end = nullptr;
        out = std::strtod(num.c_str(), &end);
        return end && *end == '\0';
    }

    bool parseValue(Value& v, int depth) {
        if (depth > kMaxDepth) return false;
        skipWs();
        if (i_ >= t_.size()) return false;
        const char c = t_[i_];
        if (c == '{') {
            ++i_;
            v.type = Value::Type::Object;
            skipWs();
            if (i_ < t_.size() && t_[i_] == '}') {
                ++i_;
                return true;
            }
            while (true) {
                skipWs();
                std::string key;
                if (!parseString(key)) return false;
                skipWs();
                if (i_ >= t_.size() || t_[i_] != ':') return false;
                ++i_;
                Value member;
                if (!parseValue(member, depth + 1)) return false;
                v.obj[key] = std::move(member);
                skipWs();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == '}') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++i_;
            v.type = Value::Type::Array;
            skipWs();
            if (i_ < t_.size() && t_[i_] == ']') {
                ++i_;
                return true;
            }
            while (true) {
                Value item;
                if (!parseValue(item, depth + 1)) return false;
                v.arr.push_back(std::move(item));
                skipWs();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == ']') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            return parseString(v.s);
        }
        if (literal("true")) {
            v.type = Value::Type::Bool;
            v.b = true;
            return true;
        }
        if (literal("false")) {
            v.type = Value::Type::Bool;
            v.b = false;
            return true;
        }
        if (literal("null")) {
            v.type = Value::Type::Null;
            return true;
        }
        v.type = Value::Type::Number;
        return parseNumber(v.n);
    }

    const std::string& t_;
    size_t i_ = 0;
};

} // namespace

const Value& Value::operator[](const std::string& key) const {
    static const Value kNull;
    if (type != Type::Object) return kNull;
    auto it = obj.find(key);
    return it == obj.end() ? kNull : it->second;
}

bool parse(const std::string& text, Value& out) {
    Value v;
    Parser p(text);
    if (!p.parseDocument(v)) {
        out = Value{};
        return false;
    }
    out = std::move(v);
    return true;
}

} // namespace rlt::json

namespace rlt {

bool parseLatestRelease(const std::string& text, ReleaseInfo& out) {
    json::Value root;
    if (!json::parse(text, root) || !root.isObject()) return false;
    const std::string tag = root["tag_name"].str();
    if (tag.empty()) return false;
    ReleaseInfo r;
    r.tag = tag;
    r.version = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? tag.substr(1) : tag;
    r.pageUrl = root["html_url"].str();
    r.notes = root["body"].str();
    const json::Value& assets = root["assets"];
    if (assets.isArray()) {
        for (const auto& a : assets.arr) {
            const std::string name = a["name"].str();
            const std::string url = a["browser_download_url"].str();
            if (name == "rltrainer.dll") r.dllUrl = url;
            else if (name == "RLTrainer-Setup.exe") r.setupUrl = url;
        }
    }
    out = std::move(r);
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](std::string s) {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
        const size_t dash = s.find('-');
        if (dash != std::string::npos) s.erase(dash);
        std::vector<long> out;
        long cur = 0;
        bool any = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                cur = cur * 10 + (c - '0');
                any = true;
            } else if (c == '.') {
                out.push_back(any ? cur : 0);
                cur = 0;
                any = false;
            } else {
                break;
            }
        }
        out.push_back(any ? cur : 0);
        return out;
    };
    const auto pa = parts(a), pb = parts(b);
    const size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        const long x = i < pa.size() ? pa[i] : 0;
        const long y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

} // namespace rlt
