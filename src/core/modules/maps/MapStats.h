#pragma once
// Per-map personal stats (time played, sessions, attempts, touches, goals, best run and splits, last runs),
// favorites, last played order and the player's own Kismet "pins". Bounded: 100 most recent maps,
// 20 last runs, 200 splits, 200 favorites. Persisted under "maps.<key>.*".

#include <map>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Store.h"

namespace rlt {

struct MapStats {
    std::string key;                 // workshop id or sanitized lower-case stem (mapStatsKey)
    std::string title;
    MapCategory category = MapCategory::Other;
    bool categoryOverride = false;   // chosen by the player (never replaced by detection)
    std::string path;                // last known map file (UTF-8), for "rlt maps load"
    double secondsPlayed = 0.0;
    int sessions = 0;
    int attempts = 0;                // runs started and ended (finished or abandoned after >= 1 s)
    int finishes = 0;
    int touches = 0;
    int goals = 0;
    bool hasBest = false;
    float best = 0.f;                // seconds
    std::vector<float> bestSplits;   // split times of the best run (seconds since start)
    std::vector<float> lastRuns;     // finished run times, oldest first
    int maxLevel = 0;
    long long lastSeq = 0;           // larger = played more recently (0 = never)
    std::string lastDate;            // "YYYY-MM-DD"
    // Player's own timing variables for this map ("pins").
    std::string pinStart, pinLevel, pinTimer;
    int pinFinal = 0;

    bool hasPins() const { return !pinStart.empty() || !pinLevel.empty() || !pinTimer.empty(); }
};

class MapStatsBook {
public:
    static constexpr size_t kMaxMaps = 100;
    static constexpr size_t kMaxRuns = 20;
    static constexpr size_t kMaxSplits = 200;
    static constexpr size_t kMaxFavorites = 200;
    static constexpr size_t kMaxText = 512;

    // Existing entry or a new one (the least recently played non-favorite is evicted beyond kMaxMaps).
    MapStats& get(const std::string& key);
    MapStats* find(const std::string& key);
    const MapStats* find(const std::string& key) const;
    bool erase(const std::string& key);
    void markPlayed(const std::string& key, const std::string& date);
    // Appends a finished run time (keeps the last kMaxRuns).
    static void addRun(MapStats& s, float seconds);

    size_t size() const { return maps_.size(); }
    // Most recently played first, then by title.
    std::vector<const MapStats*> byRecent() const;

    bool isFavorite(const std::string& key) const;
    // Returns true when the set changed.
    bool setFavorite(const std::string& key, bool on);
    const std::vector<std::string>& favorites() const { return favorites_; }

    void clear();
    void save(Store& store) const;
    void load(const Store& store);

private:
    void evictBeyondLimit(const std::string& keep);

    std::map<std::string, MapStats> maps_;
    std::vector<std::string> favorites_;
    long long seq_ = 0;
};

// "12.345,67.800" <-> floats (finite, >= 0, at most `maxCount`).
std::string encodeFloatList(const std::vector<float>& v);
std::vector<float> decodeFloatList(const std::string& s, size_t maxCount);
// Valid persistent key: 1..64 chars of [a-z0-9_-], not starting with '_' or '-'.
bool isValidMapKey(const std::string& key);

} // namespace rlt
