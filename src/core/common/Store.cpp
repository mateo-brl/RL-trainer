#include "core/common/Store.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace rlt {

std::string storeEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '=': out += "\\e"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::string storeUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            switch (n) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 'e': out.push_back('='); break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back(n);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

void Store::setFloat(const std::string& key, float value) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    data_[key] = buf;
}

void Store::setDouble(const std::string& key, double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    data_[key] = buf;
}

double Store::getDouble(const std::string& key, double def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try {
        size_t used = 0;
        const double v = std::stod(it->second, &used);
        return (used == it->second.size() && std::isfinite(v)) ? v : def;
    } catch (...) {
        return def;
    }
}

void Store::setList(const std::string& key, const std::vector<std::string>& values) {
    erasePrefix(key + ".");
    data_[key + ".count"] = std::to_string(values.size());
    for (size_t i = 0; i < values.size(); ++i) data_[key + "." + std::to_string(i)] = values[i];
}

std::string Store::getString(const std::string& key, const std::string& def) const {
    auto it = data_.find(key);
    return it == data_.end() ? def : it->second;
}

long long Store::getInt(const std::string& key, long long def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try {
        size_t used = 0;
        const long long v = std::stoll(it->second, &used);
        return used == it->second.size() ? v : def;
    } catch (...) {
        return def;
    }
}

float Store::getFloat(const std::string& key, float def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try {
        size_t used = 0;
        const float v = std::stof(it->second, &used);
        return (used == it->second.size() && std::isfinite(v)) ? v : def;
    } catch (...) {
        return def;
    }
}

bool Store::getBool(const std::string& key, bool def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    return it->second == "1" || it->second == "true";
}

std::vector<std::string> Store::getList(const std::string& key) const {
    std::vector<std::string> out;
    const long long n = getInt(key + ".count", 0);
    if (n <= 0 || n > 100000) return out;
    out.reserve(static_cast<size_t>(n));
    for (long long i = 0; i < n; ++i) out.push_back(getString(key + "." + std::to_string(i)));
    return out;
}

void Store::erasePrefix(const std::string& prefix) {
    auto it = data_.lower_bound(prefix);
    while (it != data_.end() && it->first.compare(0, prefix.size(), prefix) == 0) it = data_.erase(it);
}

std::vector<std::string> Store::keysWithPrefix(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto it = data_.lower_bound(prefix); it != data_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        out.push_back(it->first);
    return out;
}

std::string Store::serialize() const {
    std::string out = "# RL Trainer progress v1\n";
    for (const auto& kv : data_) {
        out += storeEscape(kv.first);
        out.push_back('=');
        out += storeEscape(kv.second);
        out.push_back('\n');
    }
    return out;
}

int Store::parse(const std::string& text) {
    int count = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        data_[storeUnescape(line.substr(0, eq))] = storeUnescape(line.substr(eq + 1));
        ++count;
    }
    return count;
}

} // namespace rlt
