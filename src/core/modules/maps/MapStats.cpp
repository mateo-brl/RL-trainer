#include "core/modules/maps/MapStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/modules/maps/MapCatalog.h"

namespace rlt {

namespace {
const char* kIndexKey = "maps._index";
const char* kFavKey = "maps._fav";
const char* kSeqKey = "maps._seq";

std::string bounded(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

int nonNegative(long long v) {
    if (v < 0) return 0;
    if (v > 100000000LL) return 100000000;
    return static_cast<int>(v);
}
} // namespace

bool isValidMapKey(const std::string& key) {
    if (key.empty() || key.size() > 64) return false;
    if (key.front() == '_' || key.front() == '-') return false;
    return std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string encodeFloatList(const std::vector<float>& v) {
    std::string out;
    out.reserve(v.size() * 8);
    char buf[32];
    for (size_t i = 0; i < v.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v[i]));
        if (i) out.push_back(',');
        out += buf;
    }
    return out;
}

std::vector<float> decodeFloatList(const std::string& s, size_t maxCount) {
    std::vector<float> out;
    size_t i = 0;
    while (i < s.size() && out.size() < maxCount) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        const std::string tok = s.substr(i, j - i);
        char* end = nullptr;
        const float f = std::strtof(tok.c_str(), &end);
        if (!tok.empty() && end && *end == '\0' && std::isfinite(f) && f >= 0.f) out.push_back(f);
        i = j + 1;
    }
    return out;
}

// ---- Book ----------------------------------------------------------------------------------------------
MapStats& MapStatsBook::get(const std::string& key) {
    auto it = maps_.find(key);
    if (it != maps_.end()) return it->second;
    MapStats s;
    s.key = key;
    auto& ref = maps_.emplace(key, std::move(s)).first->second;
    evictBeyondLimit(key);
    return ref;
}

MapStats* MapStatsBook::find(const std::string& key) {
    auto it = maps_.find(key);
    return it == maps_.end() ? nullptr : &it->second;
}

const MapStats* MapStatsBook::find(const std::string& key) const {
    auto it = maps_.find(key);
    return it == maps_.end() ? nullptr : &it->second;
}

bool MapStatsBook::erase(const std::string& key) { return maps_.erase(key) != 0; }

void MapStatsBook::markPlayed(const std::string& key, const std::string& date) {
    MapStats& s = get(key);
    s.lastSeq = ++seq_;
    if (!date.empty()) s.lastDate = bounded(date, 16);
}

void MapStatsBook::addRun(MapStats& s, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.f) return;
    s.lastRuns.push_back(seconds);
    if (s.lastRuns.size() > kMaxRuns) s.lastRuns.erase(s.lastRuns.begin(), s.lastRuns.end() - kMaxRuns);
}

void MapStatsBook::evictBeyondLimit(const std::string& keep) {
    while (maps_.size() > kMaxMaps) {
        auto victim = maps_.end();
        for (int pass = 0; pass < 2 && victim == maps_.end(); ++pass) {
            for (auto it = maps_.begin(); it != maps_.end(); ++it) {
                if (it->first == keep) continue;
                if (pass == 0 && isFavorite(it->first)) continue;  // keep favorites when possible
                if (victim == maps_.end() || it->second.lastSeq < victim->second.lastSeq) victim = it;
            }
        }
        if (victim == maps_.end()) break;
        maps_.erase(victim);
    }
}

std::vector<const MapStats*> MapStatsBook::byRecent() const {
    std::vector<const MapStats*> v;
    v.reserve(maps_.size());
    for (const auto& kv : maps_) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](const MapStats* a, const MapStats* b) {
        if (a->lastSeq != b->lastSeq) return a->lastSeq > b->lastSeq;
        if (a->title != b->title) return a->title < b->title;
        return a->key < b->key;
    });
    return v;
}

bool MapStatsBook::isFavorite(const std::string& key) const {
    return std::find(favorites_.begin(), favorites_.end(), key) != favorites_.end();
}

bool MapStatsBook::setFavorite(const std::string& key, bool on) {
    if (key.empty()) return false;
    auto it = std::find(favorites_.begin(), favorites_.end(), key);
    if (on) {
        if (it != favorites_.end()) return false;
        if (favorites_.size() >= kMaxFavorites) favorites_.erase(favorites_.begin());  // drop the oldest favorite
        favorites_.push_back(key);
        return true;
    }
    if (it == favorites_.end()) return false;
    favorites_.erase(it);
    return true;
}

void MapStatsBook::clear() {
    maps_.clear();
    favorites_.clear();
    seq_ = 0;
}

// ---- Persistence ---------------------------------------------------------------------------------------
void MapStatsBook::save(Store& store) const {
    store.erasePrefix("maps.");
    std::vector<std::string> index;
    const auto recent = byRecent();
    for (const MapStats* s : recent) {
        if (index.size() >= kMaxMaps) break;
        if (!isValidMapKey(s->key)) continue;
        index.push_back(s->key);
        const std::string p = "maps." + s->key + ".";
        if (!s->title.empty()) store.setString(p + "title", bounded(s->title, kMaxText));
        store.setString(p + "cat", mapCategoryKey(s->category));
        if (s->categoryOverride) store.setBool(p + "catov", true);
        if (!s->path.empty()) store.setString(p + "path", bounded(s->path, kMaxText));
        store.setFloat(p + "secs", static_cast<float>(s->secondsPlayed));
        store.setInt(p + "sessions", s->sessions);
        store.setInt(p + "attempts", s->attempts);
        store.setInt(p + "finishes", s->finishes);
        store.setInt(p + "touches", s->touches);
        store.setInt(p + "goals", s->goals);
        if (s->hasBest) {
            store.setFloat(p + "best", s->best);
            std::vector<float> splits = s->bestSplits;
            if (splits.size() > kMaxSplits) splits.resize(kMaxSplits);
            if (!splits.empty()) store.setString(p + "splits", encodeFloatList(splits));
        }
        if (!s->lastRuns.empty()) store.setString(p + "runs", encodeFloatList(s->lastRuns));
        if (s->maxLevel > 0) store.setInt(p + "maxlevel", s->maxLevel);
        store.setInt(p + "seq", s->lastSeq);
        if (!s->lastDate.empty()) store.setString(p + "date", s->lastDate);
        if (!s->pinStart.empty()) store.setString(p + "pin.start", bounded(s->pinStart, 64));
        if (!s->pinLevel.empty()) store.setString(p + "pin.level", bounded(s->pinLevel, 64));
        if (!s->pinTimer.empty()) store.setString(p + "pin.timer", bounded(s->pinTimer, 64));
        if (s->pinFinal > 0) store.setInt(p + "pin.final", s->pinFinal);
    }
    store.setList(kIndexKey, index);
    std::vector<std::string> favs;
    for (const auto& f : favorites_)
        if (isValidMapKey(f) && favs.size() < kMaxFavorites) favs.push_back(f);
    store.setList(kFavKey, favs);
    store.setInt(kSeqKey, seq_);
}

void MapStatsBook::load(const Store& store) {
    clear();
    seq_ = std::max<long long>(0, store.getInt(kSeqKey, 0));
    const auto index = store.getList(kIndexKey);
    for (const auto& key : index) {
        if (maps_.size() >= kMaxMaps) break;
        if (!isValidMapKey(key) || maps_.count(key)) continue;
        const std::string p = "maps." + key + ".";
        MapStats s;
        s.key = key;
        s.title = bounded(store.getString(p + "title"), kMaxText);
        MapCategory c = MapCategory::Other;
        if (parseMapCategory(store.getString(p + "cat"), c)) s.category = c;
        s.categoryOverride = store.getBool(p + "catov", false);
        s.path = bounded(store.getString(p + "path"), kMaxText);
        const float secs = store.getFloat(p + "secs", 0.f);
        s.secondsPlayed = (std::isfinite(secs) && secs > 0.f) ? secs : 0.0;
        s.sessions = nonNegative(store.getInt(p + "sessions", 0));
        s.attempts = nonNegative(store.getInt(p + "attempts", 0));
        s.finishes = nonNegative(store.getInt(p + "finishes", 0));
        s.touches = nonNegative(store.getInt(p + "touches", 0));
        s.goals = nonNegative(store.getInt(p + "goals", 0));
        if (store.has(p + "best")) {
            const float b = store.getFloat(p + "best", -1.f);
            if (std::isfinite(b) && b > 0.f) {
                s.hasBest = true;
                s.best = b;
                s.bestSplits = decodeFloatList(store.getString(p + "splits"), kMaxSplits);
            }
        }
        s.lastRuns = decodeFloatList(store.getString(p + "runs"), 1000);
        if (s.lastRuns.size() > kMaxRuns) s.lastRuns.erase(s.lastRuns.begin(), s.lastRuns.end() - kMaxRuns);
        s.maxLevel = nonNegative(store.getInt(p + "maxlevel", 0));
        s.lastSeq = std::max<long long>(0, store.getInt(p + "seq", 0));
        s.lastDate = bounded(store.getString(p + "date"), 16);
        s.pinStart = bounded(store.getString(p + "pin.start"), 64);
        s.pinLevel = bounded(store.getString(p + "pin.level"), 64);
        s.pinTimer = bounded(store.getString(p + "pin.timer"), 64);
        s.pinFinal = std::min(nonNegative(store.getInt(p + "pin.final", 0)), 10000);
        seq_ = std::max(seq_, s.lastSeq);
        maps_.emplace(key, std::move(s));
    }
    for (const auto& f : store.getList(kFavKey)) {
        if (favorites_.size() >= kMaxFavorites) break;
        if (isValidMapKey(f) && !isFavorite(f)) favorites_.push_back(f);
    }
}

} // namespace rlt
