// Per-map stats book: bounds, ordering, favorites, persistence.
#include <cmath>

#include "tests/TestFramework.h"

#include "core/modules/maps/MapStats.h"

using namespace rlt;

TEST(maps_stats_get_mark_and_order) {
    MapStatsBook b;
    b.get("a").title = "Alpha";
    b.get("b").title = "Bravo";
    b.get("c").title = "Charlie";
    b.markPlayed("b", "2026-10-01");
    b.markPlayed("a", "2026-10-02");
    const auto order = b.byRecent();
    CHECK(order.size() == 3);
    CHECK(order[0]->key == "a" && order[1]->key == "b" && order[2]->key == "c");
    CHECK(order[0]->lastDate == "2026-10-02");
    CHECK(b.find("zzz") == nullptr);
    CHECK(b.erase("c") && !b.erase("c"));
    CHECK(b.size() == 2);
}

TEST(maps_stats_bounded_to_100_most_recent) {
    MapStatsBook b;
    for (int i = 0; i < 150; ++i) b.markPlayed("map" + std::to_string(i), "2026-10-13");
    CHECK(b.size() == MapStatsBook::kMaxMaps);
    CHECK(b.find("map0") == nullptr);    // oldest evicted
    CHECK(b.find("map49") == nullptr);
    CHECK(b.find("map50") != nullptr);
    CHECK(b.find("map149") != nullptr);
    // Favorites survive eviction when possible.
    MapStatsBook f;
    f.markPlayed("fav", "");
    f.setFavorite("fav", true);
    for (int i = 0; i < 120; ++i) f.markPlayed("m" + std::to_string(i), "");
    CHECK(f.find("fav") != nullptr);
    CHECK(f.size() == MapStatsBook::kMaxMaps);
}

TEST(maps_stats_runs_and_favorites_bounded) {
    MapStats s;
    for (int i = 0; i < 30; ++i) MapStatsBook::addRun(s, static_cast<float>(i));
    CHECK(s.lastRuns.size() == MapStatsBook::kMaxRuns);
    CHECK(s.lastRuns.front() == 10.f && s.lastRuns.back() == 29.f);
    MapStatsBook::addRun(s, -1.f);  // ignored
    MapStatsBook::addRun(s, std::nanf(""));
    CHECK(s.lastRuns.back() == 29.f);

    MapStatsBook b;
    CHECK(b.setFavorite("x", true));
    CHECK(!b.setFavorite("x", true));  // no duplicate
    CHECK(b.isFavorite("x"));
    CHECK(b.setFavorite("x", false));
    CHECK(!b.setFavorite("x", false));
    CHECK(!b.setFavorite("", true));
    for (int i = 0; i < 250; ++i) b.setFavorite("f" + std::to_string(i), true);
    CHECK(b.favorites().size() == MapStatsBook::kMaxFavorites);
    CHECK(!b.isFavorite("f0") && b.isFavorite("f249"));
}

TEST(maps_stats_float_list_codec) {
    CHECK(encodeFloatList({}) == "");
    CHECK(encodeFloatList({1.5f, 2.25f}) == "1.500,2.250");
    const auto v = decodeFloatList("1.5,abc,,2.25,-3,inf,4", 10);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1.5f && v[1] == 2.25f && v[2] == 4.f);
    CHECK(decodeFloatList("1,2,3,4,5", 2).size() == 2);
    CHECK(decodeFloatList("", 5).empty());
}

TEST(maps_stats_key_validation) {
    CHECK(isValidMapKey("2025564083"));
    CHECK(isValidMapKey("my_map-2"));
    CHECK(!isValidMapKey(""));
    CHECK(!isValidMapKey("_index"));
    CHECK(!isValidMapKey("Upper"));
    CHECK(!isValidMapKey("a.b"));
    CHECK(!isValidMapKey(std::string(65, 'a')));
}

TEST(maps_stats_save_load_roundtrip) {
    MapStatsBook b;
    MapStats& s = b.get("2025564083");
    s.title = "Lethamyr's Giant Rings";
    s.category = MapCategory::Rings;
    s.path = "C:\\Steam\\workshop\\2025564083\\GiantRings.udk";
    s.secondsPlayed = 1234.5;
    s.sessions = 3;
    s.attempts = 12;
    s.finishes = 4;
    s.touches = 7;
    s.goals = 2;
    s.hasBest = true;
    s.best = 271.25f;
    s.bestSplits = {10.5f, 22.f, 40.125f};
    s.lastRuns = {300.f, 280.5f, 271.25f};
    s.maxLevel = 20;
    s.pinLevel = "Level";
    s.pinStart = "Timing";
    s.pinTimer = "clock";
    s.pinFinal = 20;
    b.markPlayed("2025564083", "2026-10-13");
    MapStats& o = b.get("my_custom_map");
    o.title = "My = Map";  // '=' is escaped by the store
    o.category = MapCategory::Dribble;
    o.categoryOverride = true;
    b.markPlayed("my_custom_map", "2026-10-12");
    b.setFavorite("my_custom_map", true);

    Store st;
    st.setString("other.key", "kept");
    b.save(st);
    CHECK(st.getString("other.key") == "kept");
    for (const auto& kv : st.raw())
        if (kv.first.rfind("other.", 0) != 0) CHECK_MSG(kv.first.rfind("maps.", 0) == 0, kv.first);

    Store parsed;
    parsed.parse(st.serialize());
    MapStatsBook l;
    l.load(parsed);
    CHECK(l.size() == 2);
    const MapStats* r = l.find("2025564083");
    CHECK(r != nullptr);
    if (r) {
        CHECK(r->title == "Lethamyr's Giant Rings");
        CHECK(r->category == MapCategory::Rings);
        CHECK(!r->categoryOverride);
        CHECK(r->path == s.path);
        CHECK_NEAR(r->secondsPlayed, 1234.5, 0.1);
        CHECK(r->sessions == 3 && r->attempts == 12 && r->finishes == 4 && r->touches == 7 && r->goals == 2);
        CHECK(r->hasBest);
        CHECK_NEAR(r->best, 271.25f, 1e-3f);
        CHECK(r->bestSplits.size() == 3);
        CHECK_NEAR(r->bestSplits[2], 40.125f, 1e-3f);
        CHECK(r->lastRuns.size() == 3);
        CHECK(r->maxLevel == 20);
        CHECK(r->pinLevel == "Level" && r->pinStart == "Timing" && r->pinTimer == "clock" && r->pinFinal == 20);
        CHECK(r->lastDate == "2026-10-13");
    }
    const MapStats* c = l.find("my_custom_map");
    CHECK(c != nullptr && c->title == "My = Map" && c->categoryOverride && c->category == MapCategory::Dribble);
    CHECK(l.isFavorite("my_custom_map"));
    // Order (most recent first) survives.
    CHECK(l.byRecent().front()->key == "my_custom_map");
    // New plays after load get a newer sequence number.
    l.markPlayed("2025564083", "");
    CHECK(l.byRecent().front()->key == "2025564083");
}

TEST(maps_stats_load_rejects_garbage) {
    Store st;
    st.setList("maps._index", {"good", "BAD KEY", "_x", "good", "neg"});
    st.setString("maps.good.title", "Good");
    st.setString("maps.good.secs", "nan");
    st.setString("maps.good.best", "-5");
    st.setString("maps.good.runs", "1,2,x,3");
    st.setString("maps.good.cat", "nonsense");
    st.setString("maps.neg.sessions", "-7");
    st.setString("maps.neg.pin.final", "999999");
    st.setList("maps._fav", {"good", "Nope!"});
    MapStatsBook b;
    b.load(st);
    CHECK(b.size() == 2);
    const MapStats* g = b.find("good");
    CHECK(g != nullptr && g->title == "Good" && g->secondsPlayed == 0.0 && !g->hasBest);
    CHECK(g->lastRuns.size() == 3);
    CHECK(g->category == MapCategory::Other);
    const MapStats* n = b.find("neg");
    CHECK(n != nullptr && n->sessions == 0 && n->pinFinal == 10000);
    CHECK(b.favorites().size() == 1);
    // Loading replaces previous content.
    Store empty;
    b.load(empty);
    CHECK(b.size() == 0 && b.favorites().empty());
}

TEST(maps_stats_save_is_bounded) {
    MapStatsBook b;
    for (int i = 0; i < 100; ++i) {
        MapStats& s = b.get("m" + std::to_string(i));
        s.hasBest = true;
        s.best = 10.f;
        s.bestSplits.assign(500, 1.f);  // more than allowed
        s.title = std::string(2000, 't');
    }
    Store st;
    b.save(st);
    CHECK(st.getList("maps._index").size() == 100);
    CHECK(decodeFloatList(st.getString("maps.m1.splits"), 10000).size() == MapStatsBook::kMaxSplits);
    CHECK(st.getString("maps.m1.title").size() == MapStatsBook::kMaxText);
}
