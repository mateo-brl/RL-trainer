#pragma once
// Custom/workshop map support on the SDK side: stock-map detection, installed maps scan, map launching,
// Kismet variable reading. Owned by the maps feature. Game thread unless stated otherwise.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "plugin/Sdk.h"

namespace rlt {

struct InstalledMap {
    std::string id;         // workshop id (numeric folder name) or lower-case file stem
    std::string title;      // display title (catalogue or folder/file name)
    std::string path;       // absolute path to the .udk/.upk (UTF-8)
    std::string stem;       // file name without extension
    std::string source;     // "Steam", "Mods", "Dossier perso"
    MapCategory category = MapCategory::Other;
};

class MapsService {
public:
    MapsService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars);

    // Call once from onLoad (captures the game directory).
    void init();

    // Stock arena check for GetCurrentMap() names (cached list from the game).
    bool isStockMap(const std::string& mapName);

    // Workshop id / title / category of the current map when known (last launched, catalogue).
    void describeCurrentMap(const std::string& mapName, GameContext& ctx) const;

    // Installed maps (thread-safe copy for the UI). `rescan` runs a scan on a worker thread.
    std::vector<InstalledMap> installedMaps() const;
    void rescan(const std::string& extraFolders);
    bool scanning() const;

    // Launches a map (game thread). Remembers it for describeCurrentMap.
    void launch(const InstalledMap& map);

    // Reads the cached Kismet variables of the loaded map (game thread). Empty when none.
    std::vector<KismetVar> readKismet();
    void clearKismetCache();

private:
    std::shared_ptr<GameWrapper> gw_;
    std::shared_ptr<CVarManagerWrapper> cvars_;
    std::vector<std::string> stockMaps_;
    mutable std::mutex mutex_;
    std::vector<InstalledMap> maps_;
    bool scanning_ = false;
    InstalledMap lastLaunched_;
    bool hasLastLaunched_ = false;
};

} // namespace rlt
