#pragma once
// The engine owns every module, the settings, the tick history and the activity arbitration.
// The BakkesMod glue calls it from hooks (game thread) and the UI reads it under lock() (render thread).

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Context.h"
#include "core/common/DrawList.h"
#include "core/common/History.h"
#include "core/common/Module.h"
#include "core/common/Settings.h"
#include "core/common/Store.h"

namespace rlt {

class Engine : public ActivityHost {
public:
    Engine();
    ~Engine() override;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- Threading -------------------------------------------------------------------------------------
    // Every public method below takes the lock itself; UI code that reads module state takes it explicitly.
    std::unique_lock<std::recursive_mutex> lock() const { return std::unique_lock<std::recursive_mutex>(mutex_); }

    // ---- Settings --------------------------------------------------------------------------------------
    const SettingsSchema& schema() const { return schema_; }
    const Settings& settings() const { return settings_; }
    // Applies one cvar value (called by the glue on cvar change). Returns false for unknown/invalid values.
    bool applySetting(const std::string& cvar, const std::string& value);
    // Current value of a setting as cvar string ("" when unknown).
    std::string settingValue(const std::string& cvar) const;

    // ---- Context and profile ---------------------------------------------------------------------------
    void setContext(const GameContext& ctx, Actions& out);
    const GameContext& context() const { return context_; }
    void setProfile(const PlayerProfile& profile, Actions& out);
    const PlayerProfile& profile() const { return profile_; }

    // ---- Game-thread entry points ----------------------------------------------------------------------
    // One physics tick (local car). Returns world-control actions only from the activity owner.
    void tick(const Snapshot& snap, Actions& out);
    // Touch reported by a hook (Car_TA.OnHitBall). Resolved into a TouchEvent at the next tick.
    void reportTouch(int64_t frame, bool hasHitInfo, const Vec3& hitLocation, const Vec3& hitNormal);
    void gameEvent(const GameEvent& ev, Actions& out);
    void kismet(const std::vector<KismetVar>& vars, Actions& out);
    void draw(const ViewContext& view, DrawList& out);

    // ---- Commands --------------------------------------------------------------------------------------
    void post(Command cmd);     // thread-safe, executed by pump()
    void pump(Actions& out);    // game thread (called from tick, draw and a periodic timer)
    // Parses a console line "<module> <name> args..." into a Command. Returns false when empty.
    static bool parseCommandLine(const std::vector<std::string>& words, Command& out);

    // ---- Activities ------------------------------------------------------------------------------------
    bool requestActivity(Module* m, const std::string& name) override;
    void releaseActivity(Module* m) override;
    bool ownsActivity(const Module* m) const override;
    std::string activityOwner() const { return activityOwner_; }
    std::string activityName() const { return activityName_; }
    void stopActivity();  // stops whatever runs (UI "Stop" button, context change)

    // ---- Persistence -----------------------------------------------------------------------------------
    void save(Store& store) const;
    void load(const Store& store);
    bool consumeSaveRequest();

    // ---- Introspection ---------------------------------------------------------------------------------
    const std::vector<std::unique_ptr<Module>>& modules() const { return modules_; }
    Module* find(const std::string& id) const;
    template <class M>
    M* get() const {
        for (const auto& m : modules_)
            if (auto* p = dynamic_cast<M*>(m.get())) return p;
        return nullptr;
    }
    const TickHistory& history() const { return history_; }
    bool hasSnapshot() const { return hasSnapshot_; }
    const Snapshot& latest() const { return latest_; }
    bool worldControlAllowed() const { return worldControlAllowed_; }
    // True while a drill changed the game speed (the glue restores it on unload).
    bool gameSpeedModified() const { return gameSpeedModified_; }

private:
    TickContext makeTickContext(const Module* m) const;
    DrawContext makeDrawContext(const Module* m, const ViewContext& view) const;
    // Calls `fn(module, actions)` for every module, filters world control, routes results/events.
    template <class Fn>
    void dispatch(Actions& out, Fn&& fn, const Module* skip = nullptr);
    void route(Module* source, Actions&& a, Actions& out, int depth);
    void resolveTouches(Actions& out);
    void notifySettings();
    void recomputeWorldControl(Actions& out);
    void markWorldRestore();              // the activity ended: restore speed / boost rules soon
    void flushWorldRestore(Actions& out); // writes the pending restore into `out`

    mutable std::recursive_mutex mutex_;
    std::vector<std::unique_ptr<Module>> modules_;
    SettingsSchema schema_;
    Settings settings_;
    GameContext context_;
    PlayerProfile profile_;
    TickHistory history_{600};
    Snapshot latest_;
    bool hasSnapshot_ = false;
    bool worldControlAllowed_ = false;

    std::string activityOwner_;
    std::string activityName_;

    struct PendingTouch {
        int64_t frame;
        bool hasHit;
        Vec3 loc, normal;
    };
    std::vector<PendingTouch> pendingTouches_;
    int64_t lastTouchFrame_ = -1000000;
    int lastSeenTouchFrame_ = -1;

    bool gameSpeedModified_ = false;
    bool boostRulesModified_ = false;
    bool pendingSpeedReset_ = false;
    bool pendingBoostRestore_ = false;

    std::deque<Command> commands_;
    bool saveRequested_ = false;
};

} // namespace rlt
