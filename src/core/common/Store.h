#pragma once
// Tiny persistent key/value store for progress data (records, history, adaptive levels, map stats).
// Text format, one "key=value" per line, keys are "<module>.<name>" (dots allowed), values escaped.
// The glue reads/writes the file <BakkesMod data folder>/rltrainer/progress.txt; the core only sees strings.

#include <map>
#include <string>
#include <vector>

namespace rlt {

class Store {
public:
    void setString(const std::string& key, const std::string& value) { data_[key] = value; }
    void setInt(const std::string& key, long long value) { data_[key] = std::to_string(value); }
    void setFloat(const std::string& key, float value);
    void setBool(const std::string& key, bool value) { data_[key] = value ? "1" : "0"; }
    // Lists are stored as key.count + key.0 .. key.N-1
    void setList(const std::string& key, const std::vector<std::string>& values);

    bool has(const std::string& key) const { return data_.count(key) != 0; }
    std::string getString(const std::string& key, const std::string& def = "") const;
    long long getInt(const std::string& key, long long def = 0) const;
    float getFloat(const std::string& key, float def = 0.f) const;
    bool getBool(const std::string& key, bool def = false) const;
    std::vector<std::string> getList(const std::string& key) const;

    // Removes every key starting with `prefix`.
    void erasePrefix(const std::string& prefix);
    // Keys starting with `prefix`.
    std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

    std::string serialize() const;
    // Merges parsed content into the store. Malformed lines are skipped. Returns the number of keys read.
    int parse(const std::string& text);

    const std::map<std::string, std::string>& raw() const { return data_; }
    bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }

private:
    std::map<std::string, std::string> data_;
};

// Escaping used by the format (newline, backslash, '=' in keys).
std::string storeEscape(const std::string& s);
std::string storeUnescape(const std::string& s);

} // namespace rlt
