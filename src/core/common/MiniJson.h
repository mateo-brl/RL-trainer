#pragma once
// Minimal JSON reader (objects, arrays, strings with escapes, numbers, booleans, null). Enough to read the
// GitHub "latest release" answer for the update check. No writer.

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rlt::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    // Member access; returns a static null value when missing or not an object.
    const Value& operator[](const std::string& key) const;
    std::string str(const std::string& def = "") const { return type == Type::String ? s : def; }
};

// Parses `text`. Returns false (and leaves `out` null) on malformed input.
bool parse(const std::string& text, Value& out);

} // namespace rlt::json

namespace rlt {

// What the plugin needs from a GitHub release.
struct ReleaseInfo {
    std::string tag;       // "v1.2.3"
    std::string version;   // "1.2.3"
    std::string pageUrl;   // html_url of the release
    std::string dllUrl;    // browser_download_url of the asset "rltrainer.dll"
    std::string setupUrl;  // browser_download_url of "RLTrainer-Setup.exe" when present
    std::string notes;     // body (release notes)
};

// Parses the JSON of GET /repos/<owner>/<repo>/releases/latest. False when no usable tag.
bool parseLatestRelease(const std::string& json, ReleaseInfo& out);

// Compares dotted versions ("v1.10.2" vs "1.9"): -1, 0 or 1. A leading 'v' and any suffix after '-' are ignored.
int compareVersions(const std::string& a, const std::string& b);

} // namespace rlt
