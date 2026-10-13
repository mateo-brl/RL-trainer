#include "plugin/services/MapsService.h"

#include <algorithm>
#include <cctype>

namespace rlt {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

MapsService::MapsService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars)
    : gw_(std::move(gw)), cvars_(std::move(cvars)) {}

void MapsService::init() {}

bool MapsService::isStockMap(const std::string& mapName) {
    if (mapName.empty()) return true;
    if (stockMaps_.empty()) {
        MapListWrapper list = gw_->GetMapListWrapper();
        if (!list.IsNull()) {
            ArrayWrapper<MapDataWrapper> maps = list.GetSortedMaps();
            for (int i = 0; i < maps.Count(); ++i) {
                MapDataWrapper m = maps.Get(i);
                if (!m.IsNull()) stockMaps_.push_back(lower(m.GetName()));
            }
        }
    }
    if (stockMaps_.empty()) return true;  // cannot tell: assume stock (safe: no workshop profile)
    return std::find(stockMaps_.begin(), stockMaps_.end(), lower(mapName)) != stockMaps_.end();
}

void MapsService::describeCurrentMap(const std::string& mapName, GameContext& ctx) const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (hasLastLaunched_ && (lower(lastLaunched_.stem) == lower(mapName) || ctx.isCustomMap)) {
        ctx.workshopId = lastLaunched_.id;
        ctx.mapTitle = lastLaunched_.title;
        ctx.category = lastLaunched_.category;
    }
}

std::vector<InstalledMap> MapsService::installedMaps() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return maps_;
}

void MapsService::rescan(const std::string& extraFolders) { (void)extraFolders; }

bool MapsService::scanning() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return scanning_;
}

void MapsService::launch(const InstalledMap& map) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        lastLaunched_ = map;
        hasLastLaunched_ = true;
    }
    cvars_->executeCommand("load_workshop \"" + map.path + "\"", false);
}

std::vector<KismetVar> MapsService::readKismet() { return {}; }

void MapsService::clearKismetCache() {}

} // namespace rlt
