#pragma once
// Catalogue of known training maps (Steam workshop ids, titles, authors, categories, what they train) and
// small pure helpers about map names. Data only: no SDK, no state. Sources: docs research report on workshop
// maps (Steam GetPublishedFileDetails, SpeedrunTools auto-splitters).

#include <string>
#include <vector>

#include "core/common/Context.h"

namespace rlt {

constexpr int kMapCategoryCount = 7;

// French label: "Anneaux", "Dribble", "Air dribble", "Tir", "Obstacles", "Autre", "Standard".
const char* mapCategoryLabel(MapCategory c);
// Stable key used in commands and persistence: "standard", "rings", "dribble", "airdribble", "shooting",
// "obstacle", "other".
const char* mapCategoryKey(MapCategory c);
// Accepts a key, an index ("3") or a French label (case-insensitive). False when unknown.
bool parseMapCategory(const std::string& text, MapCategory& out);
// What the category trains (French, one sentence).
const char* mapCategoryPurpose(MapCategory c);

struct CatalogMap {
    std::string title;                 // display title (map's own name)
    std::string author;                // map maker (the Steam uploader may differ)
    MapCategory category = MapCategory::Other;
    std::vector<std::string> ids;      // Steam workshop ids (main upload first, then re-uploads)
    std::vector<std::string> aliases;  // extra names matched against file stems (normalized, see stemKey)
    int levels = 0;                    // levels / checkpoints when known (0 = unknown)
    std::string trains;                // French: what the map trains
    std::string profile;               // Kismet profile key ("" = none known)
    bool recommended = false;          // suggested in the help section of the tab
};

class MapCatalog {
public:
    static const std::vector<CatalogMap>& all();
    static const CatalogMap* findById(const std::string& workshopId);
    // Case-insensitive match of a file stem or GetCurrentMap() name against titles and aliases.
    static const CatalogMap* findByStem(const std::string& stemOrMapName);
    // Id first, then stem.
    static const CatalogMap* find(const std::string& idOrStem);
    static std::vector<const CatalogMap*> recommended(MapCategory c);
    static std::vector<const CatalogMap*> byCategory(MapCategory c);
};

// ASCII lower-case (other bytes untouched).
std::string toLowerAscii(std::string s);
// Lower-case ASCII letters and digits only ("Leth's Giant_Rings" -> "lethsgiantrings").
std::string normalizeMapName(const std::string& s);
// Normalized key of a file stem / map name: drops a ".udk/.upk" extension, a "_antifreeze" suffix
// (Workshop Map Loader) and a trailing "_P" (Unreal package convention), then normalizeMapName.
std::string stemKey(const std::string& stemOrFileName);
// Digits only, 4..20 characters (Steam workshop ids).
bool isWorkshopId(const std::string& s);
// Category guessed from words of a title or file name ("ring", "dribble", "air dribble", "aim"...).
MapCategory guessMapCategory(const std::string& name);
// Persistent key of a map: the workshop id when known, otherwise the lower-case stem restricted to
// [a-z0-9_-] (max 64 chars). Never empty for a non-empty input; never starts with '_'.
std::string mapStatsKey(const std::string& workshopId, const std::string& mapName);
// Readable title from a file stem: "Leths_Giant-Rings_P" -> "Leths Giant Rings".
std::string prettifyMapName(const std::string& stem);

// Hint telling which other RL Trainer features help on a map category (shown in the Maps tab).
struct MapHint {
    std::string tab;   // French tab title, e.g. "Affichage"
    std::string text;  // French, short, imperative
};
std::vector<MapHint> mapCategoryHints(MapCategory c);

} // namespace rlt
