// Catalogue and map-name helpers.
#include <set>

#include "tests/TestFramework.h"

#include "core/modules/maps/KismetProfiles.h"
#include "core/modules/maps/MapCatalog.h"
#include "core/modules/maps/MapsModule.h"

using namespace rlt;

namespace {
bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }
} // namespace

TEST(maps_module_identity) {
    MapsModule m;
    CHECK(std::string(m.id()) == "maps");
    CHECK(std::string(m.title()) == "Maps");
}

TEST(maps_category_labels_keys_roundtrip) {
    for (int i = 0; i < kMapCategoryCount; ++i) {
        const auto c = static_cast<MapCategory>(i);
        MapCategory back = MapCategory::Standard;
        CHECK(parseMapCategory(mapCategoryKey(c), back));
        CHECK(back == c);
        back = MapCategory::Standard;
        CHECK(parseMapCategory(mapCategoryLabel(c), back));
        CHECK(back == c);
        back = MapCategory::Standard;
        CHECK(parseMapCategory(std::to_string(i), back));
        CHECK(back == c);
        CHECK(std::string(mapCategoryPurpose(c)).size() > 5);
    }
    MapCategory c = MapCategory::Standard;
    CHECK(parseMapCategory("AIR DRIBBLE", c) && c == MapCategory::AirDribble);
    CHECK(parseMapCategory("anneaux", c) && c == MapCategory::Rings);
    CHECK(parseMapCategory("Tir", c) && c == MapCategory::Shooting);
    CHECK(!parseMapCategory("", c));
    CHECK(!parseMapCategory("banana", c));
    CHECK(!parseMapCategory("9", c));
    CHECK(std::string(mapCategoryLabel(MapCategory::Rings)) == "Anneaux");
}

TEST(maps_catalog_lookup_by_id) {
    const CatalogMap* giant = MapCatalog::findById("2025564083");
    CHECK(giant != nullptr);
    if (giant) {
        CHECK(giant->title == "Lethamyr's Giant Rings");
        CHECK(giant->category == MapCategory::Rings);
        CHECK(giant->levels == 20);
        CHECK(giant->profile == "leth_rings");
        CHECK(giant->author == "Lethamyr");
    }
    // Re-upload ids map to the same entry.
    const CatalogMap* sjr2a = MapCatalog::findById("2734626907");
    const CatalogMap* sjr2b = MapCatalog::findById("1454827766");
    CHECK(sjr2a != nullptr && sjr2a == sjr2b);
    CHECK(MapCatalog::findById("1694288506") != nullptr);
    CHECK(MapCatalog::findById("1694288506")->category == MapCategory::Dribble);
    CHECK(MapCatalog::findById("2100268170")->category == MapCategory::AirDribble);
    CHECK(MapCatalog::findById("1906378036")->category == MapCategory::Shooting);
    CHECK(MapCatalog::findById("814207936")->category == MapCategory::Obstacle);
    CHECK(MapCatalog::findById("") == nullptr);
    CHECK(MapCatalog::findById("123") == nullptr);
    CHECK(MapCatalog::findById("9999999999") == nullptr);
}

TEST(maps_catalog_lookup_by_stem_is_case_insensitive) {
    const CatalogMap* giant = MapCatalog::findById("2025564083");
    CHECK(MapCatalog::findByStem("GiantRings") == giant);
    CHECK(MapCatalog::findByStem("GIANTRINGS") == giant);
    CHECK(MapCatalog::findByStem("giant_rings_p") == giant);
    CHECK(MapCatalog::findByStem("Giant_Rings_antifreeze.upk") == giant);
    CHECK(MapCatalog::findByStem("LethamyrGiantRingsMap.udk") == giant);
    CHECK(MapCatalog::findByStem("Lethamyr's Giant Rings") == giant);  // title itself
    CHECK(MapCatalog::findByStem("C:\\maps\\2025564083\\GiantRings.udk") == giant);
    CHECK(MapCatalog::findByStem("dribble2overhaul") == MapCatalog::findById("1694288506"));
    CHECK(MapCatalog::findByStem("Air_Dribble_Hoops") == MapCatalog::findById("2100268170"));
    CHECK(MapCatalog::findByStem("Stadium_P") == nullptr);
    CHECK(MapCatalog::findByStem("ab") == nullptr);  // too short to match anything
    CHECK(MapCatalog::findByStem("") == nullptr);
    // find(): id first, then stem.
    CHECK(MapCatalog::find("2025564083") == giant);
    CHECK(MapCatalog::find("giantrings") == giant);
}

TEST(maps_catalog_entries_are_consistent) {
    std::set<std::string> ids;
    int recommendedCount = 0;
    for (const auto& m : MapCatalog::all()) {
        CHECK(!m.title.empty());
        CHECK(!m.trains.empty());
        CHECK(m.category != MapCategory::Standard);
        CHECK(!hasEmDash(m.title) && !hasEmDash(m.trains) && !hasEmDash(m.author));
        for (const auto& id : m.ids) {
            CHECK_MSG(isWorkshopId(id), id);
            CHECK_MSG(ids.insert(id).second, "duplicate id " + id);
        }
        if (!m.profile.empty()) CHECK_MSG(findKismetProfile(m.profile) != nullptr, m.profile);
        if (m.recommended) ++recommendedCount;
        // Every entry can be found back by its own title.
        CHECK_MSG(MapCatalog::findByStem(m.title) == &m, m.title);
    }
    CHECK(MapCatalog::all().size() >= 40);
    CHECK(recommendedCount >= 8);
    for (auto c : {MapCategory::Rings, MapCategory::Dribble, MapCategory::AirDribble, MapCategory::Shooting,
                   MapCategory::Obstacle}) {
        CHECK(!MapCatalog::recommended(c).empty());
        CHECK(!MapCatalog::byCategory(c).empty());
        CHECK(!mapCategoryHints(c).empty());
    }
}

TEST(maps_air_dribble_hint_mentions_contact_circle_auto) {
    bool found = false;
    for (const auto& h : mapCategoryHints(MapCategory::AirDribble))
        if (h.text.find("cercle de contact") != std::string::npos && h.text.find("Auto") != std::string::npos)
            found = true;
    CHECK(found);
    CHECK(!mapCategoryHints(MapCategory::Other).empty());
    for (int i = 0; i < kMapCategoryCount; ++i)
        for (const auto& h : mapCategoryHints(static_cast<MapCategory>(i))) {
            CHECK(!h.tab.empty() && !h.text.empty());
            CHECK(!hasEmDash(h.text));
        }
}

TEST(maps_prediction_hints_match_the_default_off) {
    // The prediction is off by default (round 2): no hint asks to turn it off, and every hint about it says where to
    // turn it on.
    int prediction = 0;
    for (int i = 0; i < kMapCategoryCount; ++i)
        for (const auto& h : mapCategoryHints(static_cast<MapCategory>(i))) {
            if (h.tab != "Prédiction") continue;
            ++prediction;
            CHECK_MSG(h.text.find("coupe la prédiction") == std::string::npos, h.text);
            CHECK_MSG(h.text.find("page HUD, section Trajectoires") != std::string::npos, h.text);
        }
    CHECK_EQ(prediction, 2);  // air dribble and shooting maps
    CHECK_EQ(mapCategoryHints(MapCategory::Rings).size(), size_t(2));
}

TEST(maps_name_helpers) {
    CHECK(toLowerAscii("AbC-É") == std::string("abc-\xC3\x89"));
    CHECK(normalizeMapName("Leth's Giant_Rings 2!") == "lethsgiantrings2");
    CHECK(stemKey("Giant_Rings_P.UDK") == "giantrings");
    CHECK(stemKey("x/y/Map_antifreeze.upk") == "map");
    CHECK(isWorkshopId("2025564083"));
    CHECK(!isWorkshopId("12a4"));
    CHECK(!isWorkshopId("123"));
    CHECK(!isWorkshopId(""));
    CHECK(prettifyMapName("Leths_Giant-Rings_P") == "Leths Giant Rings");
    CHECK(prettifyMapName("LethamyrGiantRings.udk") == "Lethamyr Giant Rings");
    CHECK(prettifyMapName("my_map_antifreeze.upk") == "my map");
    CHECK(prettifyMapName("") == "");
}

TEST(maps_stats_key_rules) {
    CHECK(mapStatsKey("2025564083", "whatever") == "2025564083");
    CHECK(mapStatsKey("", "My Map_P") == "my_map_p");
    CHECK(mapStatsKey("", "Weird=Name.upk") == "weird_name");
    CHECK(mapStatsKey("", "__x") == "x");
    CHECK(mapStatsKey("", "") == "");
    CHECK(mapStatsKey("", "\xC3\xA9\xC3\xA9") == "map");  // only non-ASCII: still a usable key
    CHECK(mapStatsKey("", std::string(200, 'a')).size() == 64);
    CHECK(mapStatsKey("custom", "ignored") == "custom");
    CHECK(mapStatsKey("", "Map_antifreeze") == "map");
}

TEST(maps_guess_category_from_names) {
    CHECK(guessMapCategory("Air Dribble Gauntlet v2") == MapCategory::AirDribble);
    CHECK(guessMapCategory("AirDribbleHoops") == MapCategory::AirDribble);
    CHECK(guessMapCategory("Super Dribbling Course") == MapCategory::Dribble);
    CHECK(guessMapCategory("NeonRings") == MapCategory::Rings);
    CHECK(guessMapCategory("ring of fire") == MapCategory::Rings);
    CHECK(guessMapCategory("Spring Park") == MapCategory::Other);  // "ring" inside a word is not a ring map
    CHECK(guessMapCategory("Obstacle Course 3") == MapCategory::Obstacle);
    CHECK(guessMapCategory("SpeedJump Trials") == MapCategory::Obstacle);
    CHECK(guessMapCategory("Aim Training Pro") == MapCategory::Shooting);
    CHECK(guessMapCategory("pinch_master") == MapCategory::Shooting);
    CHECK(guessMapCategory("Beach Party") == MapCategory::Other);
    CHECK(guessMapCategory("") == MapCategory::Other);
}
