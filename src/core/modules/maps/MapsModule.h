#pragma once
// Maps: workshop / custom maps support. Recognises the loaded map (catalogue, Kismet profile, generic
// heuristics), times runs with splits and personal bests (from the map's Kismet variables when known,
// otherwise own timing), keeps per-map stats, favorites and last played, and shows a HUD panel on custom
// maps. Analysis only: never moves the ball or the car, never writes Kismet variables.

#include <map>
#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/maps/KismetProfiles.h"
#include "core/modules/maps/MapCatalog.h"
#include "core/modules/maps/MapRun.h"
#include "core/modules/maps/MapStats.h"
#include "core/modules/maps/MapsConfig.h"

namespace rlt {

// Where the timing profile of the current map comes from.
enum class ProfileSource : int { None = 0, WorkshopId = 1, Signature = 2, Pins = 3, Auto = 4 };
const char* profileSourceLabel(ProfileSource s);  // French

// Plain copy of the live state, for the UI (read under the engine lock).
struct MapsLiveStatus {
    bool enabled = true;
    bool onCustomMap = false;
    std::string key, workshopId, mapName, title, author, trains;
    MapCategory category = MapCategory::Other;
    std::string categorySource;      // French: "catalogue", "ton choix", ...
    bool categoryOverride = false;
    bool inCatalog = false;
    int catalogLevels = 0;
    std::string profileKey, profileLabel;
    ProfileSource profileSource = ProfileSource::None;
    bool kismetSeen = false;
    int kismetVars = 0;
    RunPhase phase = RunPhase::Idle;
    RunSource runSource = RunSource::None;
    bool ownStart = true;            // own timing starts the runs on this map
    float runTime = 0.f;
    bool hasLevel = false;
    int level = 0;
    int finalLevel = 0;              // last level the run timer uses (profile, pins or catalogue; 0 = unknown)
    bool hasMapTimer = false;
    std::string mapTimer;
    bool hasDeaths = false;
    int deaths = 0;
    std::vector<RunSplit> splits;    // current run
    std::vector<float> pbSplits;
    bool hasPb = false;
    float pb = 0.f;
    bool hasLiveDelta = false;
    float liveDelta = 0.f;
    RunSummary last;
    bool lastWasPb = false;
    double sessionSeconds = 0.0;
    std::vector<MapHint> hints;
    std::string pinStart, pinLevel, pinTimer;
    int pinFinal = 0;
};

struct MapStatsView {
    std::string key, title, path, lastDate;
    MapCategory category = MapCategory::Other;
    bool categoryOverride = false;
    double secondsPlayed = 0.0;
    int sessions = 0, attempts = 0, finishes = 0, touches = 0, goals = 0, maxLevel = 0;
    bool hasBest = false;
    float best = 0.f;
    std::vector<float> lastRuns;
    long long lastSeq = 0;
    bool favorite = false;
};

// Graded result of a finished run (pure, exposed for tests). `prevPb*` describe the record BEFORE this run.
AttemptResult makeMapRunResult(const std::string& key, const std::string& title, MapCategory category,
                               const RunSummary& run, bool hadPb, float prevPb, const std::vector<float>& prevPbSplits,
                               float gameTime);
// "+1.23 s" / "-0.40 s".
std::string formatDeltaSeconds(float delta);

class MapsModule : public Module {
public:
    const char* id() const override { return "maps"; }
    const char* title() const override { return "Maps"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onKismet(const std::vector<KismetVar>& vars, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    const MapsConfig& config() const { return cfg_; }

    // ---- Read accessors for the UI (call under the engine lock, copy, release) -----------------------
    MapsLiveStatus status() const;
    std::vector<MapStatsView> statsList() const;  // most recently played first
    bool statsFor(const std::string& key, MapStatsView& out) const;
    std::vector<KismetCandidate> kismetCandidates() const { return watcher_.candidates(); }
    std::vector<std::string> favorites() const { return book_.favorites(); }
    bool isFavorite(const std::string& key) const { return book_.isFavorite(key); }
    std::map<std::string, MapCategory> categoryOverrides() const;
    // Fills title / category / workshop id of a custom map context from what the module knows (catalogue,
    // Kismet profile, player's choice). Meant for the glue's context refresh (see shared change request).
    void refineContext(GameContext& ctx) const;

private:
    void enterContext(const GameContext& game, float now, Actions& out);
    void leaveMap(float now, Actions& out);
    void resolveProfile(const std::vector<KismetVar>* vars);
    void applyProfile(const KismetProfile* p, ProfileSource src);
    // Copy of `p` that ends runs at the catalogue's last level when the profile counts levels without one.
    KismetProfile withCatalogFinal(const KismetProfile& p) const;
    void refreshIdentity(const GameContext& game);
    void drain(float now, Actions& out);
    void onRunFinished(float now, Actions& out);
    void syncPersonalBest();
    MapCategory resolvedCategory(std::string* source) const;
    std::string resolvedTitle() const;
    int resolvedFinalLevel() const;
    std::string targetKey(const Command& cmd, size_t argIndex) const;
    void toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const;
    DrawPanel buildHud(const ViewContext& view, const DrawContext& ctx, float now) const;

    MapsConfig cfg_;
    bool accents_ = false;

    // Current map
    GameContext lastGame_;
    bool hasLastGame_ = false;
    bool custom_ = false;
    std::string key_, workshopId_, mapName_, ctxTitle_;
    MapCategory ctxCategory_ = MapCategory::Standard;
    const CatalogMap* catalog_ = nullptr;
    std::string date_;
    std::string sessionKey_;
    double sessionSeconds_ = 0.0;
    float lastTickTime_ = 0.f;
    bool hasLastTick_ = false;
    bool movedSinceEnter_ = false;
    float now_ = 0.f;

    // Kismet
    KismetWatcher watcher_;
    ProfileSource profileSource_ = ProfileSource::None;
    bool profileVerified_ = false;    // id profile checked against the live variables
    size_t resolvedVarCount_ = 0;
    bool kismetSeen_ = false;

    RunTracker run_;
    bool lastWasPb_ = false;
    float lastPbDelta_ = 0.f;
    bool hasLastPbDelta_ = false;

    MapStatsBook book_;
    std::map<std::string, std::string> paths_;  // key -> map file (UTF-8) registered by the UI
};

} // namespace rlt
