#include "plugin/services/MapsService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "bakkesmod/wrappers/kismet/SequenceVariableWrapper.h"
#include "bakkesmod/wrappers/kismet/SequenceWrapper.h"
#include "core/modules/maps/KismetProfiles.h"
#include "core/modules/maps/MapCatalog.h"
#include "core/modules/maps/MapScan.h"

namespace rlt {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr size_t kMaxKismetVars = 256;
constexpr size_t kMaxKismetString = 256;
constexpr int kMaxKismetAttempts = 10;
constexpr auto kKismetFirstDelay = std::chrono::milliseconds(1000);  // variables are not all set at InitGame
constexpr auto kKismetRetryDelay = std::chrono::milliseconds(2000);
constexpr auto kLaunchBindWindow = std::chrono::seconds(120);
constexpr auto kCvarSyncPeriod = std::chrono::seconds(2);

InstalledMap toInstalled(const ScannedMap& s) {
    InstalledMap m;
    m.id = s.id;
    m.title = s.title;
    m.path = s.path;
    m.stem = s.stem;
    m.source = s.source;
    m.category = s.category;
    m.folder = s.folder;
    m.inCatalog = s.inCatalog;
    m.nonAscii = s.nonAscii;
    m.size = s.size;
    return m;
}
} // namespace

// Cached wrappers of the loaded map's named Kismet variables (pointers into the level: cleared on unload).
struct MapsService::KismetCache {
    std::map<std::string, SequenceVariableWrapper> vars;
    uintptr_t sequence = 0;
    bool cached = false;
    bool waiting = false;
    std::chrono::steady_clock::time_point waitStart{};
    std::chrono::steady_clock::time_point nextTry{};
    int attempts = 0;
    bool categoryDone = false;
};

MapsService::MapsService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars)
    : gw_(std::move(gw)), cvars_(std::move(cvars)), kismet_(std::make_unique<KismetCache>()) {}

MapsService::~MapsService() {
    stop_ = true;
    std::lock_guard<std::mutex> tl(threadMutex_);
    if (worker_.joinable()) worker_.join();
}

void MapsService::init() {
    try {
        paths_.exeDir = fs::current_path();  // Binaries/Win64 while the plugin loads: never re-read later
    } catch (...) {
        paths_.exeDir.clear();
    }
    try {
        paths_.dataDir = gw_->GetDataFolder();
    } catch (...) {
        paths_.dataDir.clear();
    }
    steam_ = gw_->IsUsingSteamVersion();
    paths_.steam = steam_;
    rescan(MapScanOptions{});
}

// ---- Stock maps ----------------------------------------------------------------------------------------
bool MapsService::isStockMap(const std::string& mapName) {
    syncScanOptionsFromCvars();
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

// ---- Current map identity ------------------------------------------------------------------------------
void MapsService::describeCurrentMap(const std::string& mapName, GameContext& ctx) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const std::string key = stemKey(mapName);
    const InstalledMap* found = nullptr;
    if (hasLastLaunched_ && !key.empty() && stemKey(lastLaunched_.stem) == key) found = &lastLaunched_;
    if (!found && !key.empty()) {
        for (const auto& m : maps_)
            if (stemKey(m.stem) == key) {
                found = &m;
                break;
            }
    }
    if (!found && hasLastLaunched_) {
        // Renamed package (Workshop Map Loader "antifreeze", legacy swaps): the first custom map loaded
        // shortly after our launch is the one we launched.
        if (!launchBound_ && mapName != launchFromMap_ &&
            std::chrono::steady_clock::now() - launchTime_ < kLaunchBindWindow) {
            launchBound_ = true;
            launchMapName_ = mapName;
        }
        if (launchBound_ && launchMapName_ == mapName) found = &lastLaunched_;
    }

    if (found) {
        ctx.workshopId = isWorkshopId(found->id) ? found->id : std::string();
        ctx.mapTitle = found->title;
        ctx.category = found->category;
    } else if (const CatalogMap* c = MapCatalog::findByStem(mapName)) {
        ctx.workshopId = c->ids.empty() ? std::string() : c->ids.front();
        ctx.mapTitle = c->title;
        ctx.category = c->category;
    } else {
        ctx.mapTitle = prettifyMapName(mapName);
        ctx.category = guessMapCategory(mapName);
    }
    if ((ctx.category == MapCategory::Other || ctx.category == MapCategory::Standard) &&
        kismetCategory_ != MapCategory::Other)
        ctx.category = kismetCategory_;
    const auto it = overrides_.find(mapStatsKey(ctx.workshopId, mapName));
    if (it != overrides_.end()) ctx.category = it->second;
}

void MapsService::setCategoryOverrides(const std::map<std::string, MapCategory>& overrides) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (overrides_ != overrides) overrides_ = overrides;
}

// ---- Installed maps ------------------------------------------------------------------------------------
// init() runs before the cvars exist (first scan with the default folders). The context refresh calls
// isStockMap every 0.25 s on the game thread: from there, rescan with the player's scan settings as soon as
// they are registered and whenever they change, so maps in extra folders are known without opening the tab.
// Same values as the engine's settings (the glue applies the cvar strings as they are): no flip-flop with
// the tab's ensureScanned.
void MapsService::syncScanOptionsFromCvars() {
    const auto now = std::chrono::steady_clock::now();
    if (now < nextCvarSync_ || !cvars_) return;
    nextCvarSync_ = now + kCvarSyncPeriod;
    try {
        CVarWrapper extra = cvars_->getCvar("rlt_map_extra_folders");
        if (extra.IsNull()) return;  // settings not registered yet
        auto flag = [&](const char* name) {
            CVarWrapper c = cvars_->getCvar(name);
            return c.IsNull() ? true : c.getBoolValue();
        };
        MapScanOptions o;
        o.extraFolders = extra.getStringValue();
        o.steam = flag("rlt_map_scan_steam");
        o.mods = flag("rlt_map_scan_mods");
        o.wml = flag("rlt_map_scan_wml");
        ensureScanned(o);
    } catch (...) {
        // Never let a settings read break the context refresh.
    }
}

std::vector<InstalledMap> MapsService::installedMaps() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return maps_;
}

bool MapsService::scanning() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return scanning_;
}

int MapsService::scanGeneration() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return generation_;
}

std::vector<MapScanRoot> MapsService::scanRoots() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return roots_;
}

bool MapsService::findInstalled(const std::string& idOrStem, InstalledMap& out) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const std::string key = stemKey(idOrStem);
    for (const auto& m : maps_)
        if (m.id == idOrStem || (!key.empty() && stemKey(m.stem) == key)) {
            out = m;
            return true;
        }
    return false;
}

void MapsService::rescan(const std::string& extraFolders) {
    MapScanOptions o;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (hasRequested_) o = requested_;
    }
    o.extraFolders = extraFolders;
    rescan(o);
}

void MapsService::ensureScanned(const MapScanOptions& options) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (hasRequested_ && requested_ == options) return;
    }
    rescan(options);
}

void MapsService::rescan(const MapScanOptions& options) {
    if (stop_) return;
    std::lock_guard<std::mutex> tl(threadMutex_);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        requested_ = options;
        hasRequested_ = true;
        if (scanning_) {
            pendingScan_ = true;  // the running worker scans again with the newest options
            return;
        }
        scanning_ = true;
    }
    if (worker_.joinable()) worker_.join();  // previous worker already finished (scanning_ was false)
    try {
        worker_ = std::thread([this, options, paths = paths_] { scanWorker(options, paths); });
    } catch (...) {
        std::lock_guard<std::mutex> lk(mutex_);
        scanning_ = false;
    }
}

void MapsService::scanWorker(MapScanOptions options, Paths paths) {
    for (;;) {
        std::vector<ScannedMap> found;
        std::vector<MapScanRoot> roots;
        try {
            auto scanOne = [&](const std::string& label, const fs::path& root) {
                MapScanRoot r;
                r.label = label;
                r.path = pathToUtf8(root);
                std::error_code ec;
                r.exists = !root.empty() && fs::is_directory(root, ec) && !ec;
                const size_t before = found.size();
                if (r.exists) scanMapRoot(root, label, found, &stop_);
                r.found = static_cast<int>(found.size() - before);
                roots.push_back(std::move(r));
            };
            const fs::path cooked =
                paths.exeDir.empty() ? fs::path()
                                     : (paths.exeDir / ".." / ".." / "TAGame" / "CookedPCConsole").lexically_normal();
            const fs::path mods = cooked.empty() ? fs::path() : cooked / "mods";
            if (options.steam && paths.steam && !paths.exeDir.empty())
                scanOne("Steam", (paths.exeDir / ".." / ".." / ".." / ".." / "workshop" / "content" / "252950")
                                     .lexically_normal());
            if (options.mods && !mods.empty()) scanOne("Mods", mods);
            if (options.wml && !paths.dataDir.empty()) {
                const fs::path cfg = paths.dataDir / "WorkshopMapLoader" / "workshopmaploader.cfg";
                std::ifstream in(cfg, std::ios::binary);
                if (in) {
                    std::string text(64 * 1024, '\0');
                    in.read(&text[0], static_cast<std::streamsize>(text.size()));
                    text.resize(static_cast<size_t>(in.gcount()));
                    const std::string folder = parseWmlMapsFolder(text);
                    if (!folder.empty()) {
                        const fs::path wml = pathFromUtf8(folder).lexically_normal();
                        if (!wml.empty() && lower(pathToUtf8(wml)) != lower(pathToUtf8(mods.lexically_normal())))
                            scanOne("Workshop Map Loader", wml);
                    }
                }
            }
            for (const auto& extra : splitFolderList(options.extraFolders)) {
                if (stop_) break;
                scanOne("Dossier perso", pathFromUtf8(extra));
            }
            finalizeScan(found);
        } catch (...) {
            // Keep whatever was found: a broken folder must never take the scan (or the game) down.
        }
        std::vector<InstalledMap> maps;
        maps.reserve(found.size());
        for (const auto& s : found) maps.push_back(toInstalled(s));

        std::lock_guard<std::mutex> lk(mutex_);
        if (!stop_) {
            maps_.swap(maps);
            roots_.swap(roots);
            ++generation_;
        }
        if (pendingScan_ && !stop_) {
            pendingScan_ = false;
            options = requested_;
            continue;
        }
        scanning_ = false;
        return;
    }
}

// ---- Launching -----------------------------------------------------------------------------------------
void MapsService::launch(const InstalledMap& map) {
    if (map.path.empty() || map.path.find('"') != std::string::npos) return;
    const std::string from = gw_->GetCurrentMap();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        launchFromMap_ = from;
        lastLaunched_ = map;
        hasLastLaunched_ = true;
        launchTime_ = std::chrono::steady_clock::now();
        launchBound_ = false;
        launchMapName_.clear();
    }
    cvars_->executeCommand("load_workshop \"" + map.path + "\"", false);
}

void MapsService::reloadMap() { gw_->ExecuteUnrealCommand("servertravel ?"); }

void MapsService::exitToMenu() { gw_->ExecuteUnrealCommand("start MENU_Main_p?close"); }

void MapsService::backToFreeplay() { cvars_->executeCommand("load_freeplay", false); }

bool MapsService::lastLaunched(InstalledMap& out) const {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!hasLastLaunched_) return false;
    out = lastLaunched_;
    return true;
}

// ---- Kismet (read only) --------------------------------------------------------------------------------
bool MapsService::buildKismetCache() {
    KismetCache& k = *kismet_;
    k.vars.clear();
    ++k.attempts;
    SequenceWrapper seq = gw_->GetMainSequence();
    if (seq.IsNull()) return false;
    bool recursive = false;
    k.vars = seq.GetAllSequenceVariables(false);  // main sequence only (SpeedrunTools, Map Expansion Plugin)
    if (k.vars.empty()) {
        k.vars = seq.GetAllSequenceVariables(true);  // some maps keep their variables in sub-sequences
        recursive = true;
    }
    if (k.vars.empty()) return false;
    k.sequence = seq.memory_address;
    k.cached = true;
    k.categoryDone = false;
    std::lock_guard<std::mutex> lk(mutex_);
    kismetInfo_.cached = true;
    kismetInfo_.variables = static_cast<int>(k.vars.size());
    kismetInfo_.recursive = recursive;
    kismetInfo_.attempts = k.attempts;
    return true;
}

std::vector<KismetVar> MapsService::readKismet() {
    std::vector<KismetVar> out;
    KismetCache& k = *kismet_;
    try {
        const auto now = std::chrono::steady_clock::now();
        if (!k.cached) {
            if (!k.waiting) {
                k.waiting = true;
                k.waitStart = now;
                k.nextTry = now + kKismetFirstDelay;
                return out;
            }
            if (now < k.nextTry || k.attempts >= kMaxKismetAttempts) return out;
            if (!buildKismetCache()) {
                k.nextTry = now + kKismetRetryDelay;
                std::lock_guard<std::mutex> lk(mutex_);
                kismetInfo_.attempts = k.attempts;
                return out;
            }
        }
        // Every read: the cached variables point into the level, so a swapped level must never be read.
        {
            SequenceWrapper seq = gw_->GetMainSequence();
            if (seq.IsNull() || seq.memory_address != k.sequence) {
                clearKismetCache();
                return out;
            }
        }
        out.reserve(std::min(k.vars.size(), kMaxKismetVars));
        for (auto& entry : k.vars) {
            if (out.size() >= kMaxKismetVars) break;
            SequenceVariableWrapper& var = entry.second;
            if (entry.first.empty() || var.IsNull()) continue;
            KismetVar v;
            v.name = entry.first;
            if (var.IsInt()) {
                v.type = KismetVar::Type::Int;
                v.i = var.GetInt();
            } else if (var.IsFloat()) {
                v.type = KismetVar::Type::Float;
                v.f = var.GetFloat();
            } else if (var.IsBool()) {
                v.type = KismetVar::Type::Bool;
                v.b = var.GetBool();
            } else if (var.IsString()) {
                v.type = KismetVar::Type::String;
                v.s = var.GetString();
                if (v.s.size() > kMaxKismetString) v.s.resize(kMaxKismetString);
            } else {
                v.type = KismetVar::Type::Other;
            }
            out.push_back(std::move(v));
        }
        if (!k.categoryDone) {
            k.categoryDone = true;
            const KismetProfile* p = kismetProfileBySignature(out);
            std::lock_guard<std::mutex> lk(mutex_);
            kismetCategory_ = p ? p->category : MapCategory::Other;
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

void MapsService::clearKismetCache() {
    KismetCache& k = *kismet_;
    k.vars.clear();
    k.sequence = 0;
    k.cached = false;
    k.waiting = false;
    k.attempts = 0;
    k.categoryDone = false;
    std::lock_guard<std::mutex> lk(mutex_);
    kismetInfo_ = KismetCacheInfo{};
    kismetCategory_ = MapCategory::Other;
}

KismetCacheInfo MapsService::kismetInfo() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return kismetInfo_;
}

} // namespace rlt
