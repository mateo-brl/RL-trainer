#pragma once
// Custom/workshop map support on the SDK side: stock-map detection, installed maps scan, map launching,
// Kismet variable reading. Owned by the maps feature. Game thread unless stated otherwise.
//
// Threads: the installed-maps scan runs on a worker std::thread (never in Render); its result is swapped in
// under `mutex_`. installedMaps(), scanning(), scanRoots(), rescan(), ensureScanned() are safe from the render
// thread. readKismet(), clearKismetCache(), launch(), reloadMap(), exitToMenu(), backToFreeplay() and
// isStockMap() must run on the game thread (use UiContext::runOnGame from the UI).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/common/Context.h"
#include "plugin/Sdk.h"

namespace rlt {

struct InstalledMap {
    std::string id;         // workshop id (numeric folder name) or lower-case file stem
    std::string title;      // display title (catalogue or folder/file name)
    std::string path;       // absolute path to the .udk/.upk (UTF-8)
    std::string stem;       // file name without extension
    std::string source;     // "Steam", "Mods", "Workshop Map Loader", "Dossier perso"
    MapCategory category = MapCategory::Other;
    std::string folder;     // parent folder name (UTF-8)
    bool inCatalog = false; // known by the RL Trainer catalogue
    bool nonAscii = false;  // path with accents: load_workshop may refuse it
    std::uintmax_t size = 0;
};

// Folder scanned by the last scan (for the UI).
struct MapScanRoot {
    std::string label;      // French
    std::string path;       // UTF-8
    bool exists = false;
    int found = 0;
};

struct MapScanOptions {
    std::string extraFolders;  // ';'-separated
    bool steam = true;
    bool mods = true;
    bool wml = true;
    bool operator==(const MapScanOptions& o) const {
        return extraFolders == o.extraFolders && steam == o.steam && mods == o.mods && wml == o.wml;
    }
    bool operator!=(const MapScanOptions& o) const { return !(*this == o); }
};

// UI-only state of the Maps tab (render thread), kept here so the tab needs no global variable.
struct MapsUiState {
    char search[128] = {};
    int categoryFilter = -1;      // -1 = all, else MapCategory index
    bool favoritesOnly = false;
    int registeredGeneration = -1;
    int finalLevelInput = 0;
    // Installed maps as the tab shows them, rebuilt only when a new scan result is swapped in (no per-frame
    // copy of thousands of entries, no per-frame key building).
    int libraryGeneration = -1;
    std::vector<InstalledMap> library;      // sorted by title (scan order)
    std::vector<std::string> libraryKeys;   // maps module key of each entry
    std::vector<std::string> librarySearch; // lower-case "title stem id" of each entry
    std::set<std::string> libraryKeySet;
};

struct KismetCacheInfo {
    bool cached = false;
    int variables = 0;
    bool recursive = false;   // GetAllSequenceVariables(true) was needed
    int attempts = 0;
};

class MapsService {
public:
    MapsService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars);
    ~MapsService();
    MapsService(const MapsService&) = delete;
    MapsService& operator=(const MapsService&) = delete;

    // Call once from onLoad (captures the game directory) and starts a first scan with default folders.
    void init();

    // Stock arena check for GetCurrentMap() names (cached list from the game). Called by every context
    // refresh: also rescans (worker thread) when the scan settings cvars changed, at most every 2 s, so the
    // player's extra folders count even before the Maps tab is opened.
    bool isStockMap(const std::string& mapName);

    // Workshop id / title / category of the current map when known (last launched, installed maps,
    // catalogue, Kismet signature, player's category choice).
    void describeCurrentMap(const std::string& mapName, GameContext& ctx) const;

    // Installed maps (thread-safe copy for the UI). `rescan` runs a scan on a worker thread.
    std::vector<InstalledMap> installedMaps() const;
    void rescan(const std::string& extraFolders);
    void rescan(const MapScanOptions& options);
    // Rescans only when the options differ from the last request (cheap: call it every UI frame).
    void ensureScanned(const MapScanOptions& options);
    bool scanning() const;
    int scanGeneration() const;            // +1 each time a scan result is swapped in
    std::vector<MapScanRoot> scanRoots() const;
    bool findInstalled(const std::string& idOrStem, InstalledMap& out) const;
    bool usingSteam() const { return steam_; }

    // Launches a map (game thread). Remembers it for describeCurrentMap.
    void launch(const InstalledMap& map);
    void reloadMap();       // "servertravel ?" (full Kismet reset)
    void exitToMenu();      // "start MENU_Main_p?close"
    void backToFreeplay();  // "load_freeplay"
    bool lastLaunched(InstalledMap& out) const;

    // Player's category choices (from the maps module), applied by describeCurrentMap.
    void setCategoryOverrides(const std::map<std::string, MapCategory>& overrides);

    // Reads the cached Kismet variables of the loaded map (game thread). Empty when none.
    std::vector<KismetVar> readKismet();
    void clearKismetCache();
    KismetCacheInfo kismetInfo() const;

    MapsUiState& uiState() { return ui_; }

private:
    struct KismetCache;
    struct Paths {
        std::filesystem::path exeDir;   // .../Binaries/Win64 at plugin load
        std::filesystem::path dataDir;  // BakkesMod data folder
        bool steam = false;
    };

    void scanWorker(MapScanOptions options, Paths paths);
    bool buildKismetCache();
    void syncScanOptionsFromCvars();

    std::shared_ptr<GameWrapper> gw_;
    std::shared_ptr<CVarManagerWrapper> cvars_;
    std::vector<std::string> stockMaps_;
    std::chrono::steady_clock::time_point nextCvarSync_{};
    Paths paths_;
    bool steam_ = false;

    mutable std::mutex mutex_;   // guards everything below shared with the worker / render thread
    std::vector<InstalledMap> maps_;
    std::vector<MapScanRoot> roots_;
    bool scanning_ = false;
    bool pendingScan_ = false;
    bool hasRequested_ = false;
    MapScanOptions requested_;
    int generation_ = 0;
    InstalledMap lastLaunched_;
    bool hasLastLaunched_ = false;
    std::chrono::steady_clock::time_point launchTime_{};
    mutable bool launchBound_ = false;    // first custom map seen after a launch is that launch
    mutable std::string launchMapName_;
    std::string launchFromMap_;           // map loaded when the launch was requested (never bound)
    std::map<std::string, MapCategory> overrides_;
    MapCategory kismetCategory_ = MapCategory::Other;
    KismetCacheInfo kismetInfo_;

    std::mutex threadMutex_;     // serializes worker start / join
    std::thread worker_;
    std::atomic<bool> stop_{false};

    std::unique_ptr<KismetCache> kismet_;  // game thread only
    MapsUiState ui_;                       // render thread only
};

} // namespace rlt
