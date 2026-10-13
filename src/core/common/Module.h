#pragma once
// Base class of every feature module. A module only talks to the outside world through the arguments
// of these callbacks (data in) and the Actions / DrawList it fills (data out). See docs/ARCHITECTURE.md.
//
// Threading: every callback runs on the game thread, serialized by the engine lock. UI code (render
// thread) must take Engine::lock() before reading module state and must never call callbacks directly;
// it posts Commands instead (Engine::post).

#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Context.h"
#include "core/common/DrawList.h"
#include "core/common/Grade.h"
#include "core/common/History.h"
#include "core/common/Settings.h"
#include "core/common/Snapshot.h"
#include "core/common/Store.h"

namespace rlt {

class Module;

// Implemented by the engine: arbitration of the single "activity" (drill) allowed to move the ball/car.
class ActivityHost {
public:
    virtual ~ActivityHost() = default;
    virtual bool requestActivity(Module* m, const std::string& name) = 0;
    virtual void releaseActivity(Module* m) = 0;
    virtual bool ownsActivity(const Module* m) const = 0;
};

// Read-only view of the engine state given to tick-like callbacks.
struct TickContext {
    const Snapshot& snap;            // newest snapshot (default-constructed with valid=false parts before the first tick)
    const TickHistory& history;      // last ~5 s of snapshots, history.back(0) == snap when hasSnapshot
    const Settings& settings;
    const GameContext& game;
    const PlayerProfile& profile;
    bool hasSnapshot = false;        // false until the first tick of the current map
    bool ownsActivity = false;       // this module currently owns the world-control activity
    bool worldControlAllowed = false;
};

struct DrawContext {
    const Settings& settings;
    const GameContext& game;
    const PlayerProfile& profile;
    const Snapshot& snap;            // newest physics snapshot
    bool hasSnapshot = false;
    bool ownsActivity = false;
    float hudScale = 1.f;            // general.hudScale * screen-height factor (1 at 1080p)
};

class Module {
public:
    virtual ~Module() = default;

    virtual const char* id() const = 0;     // stable id: "overlay", "firsttouch", ...
    virtual const char* title() const = 0;  // French display name for the UI tab

    // Settings were (re)applied. Copy what you need; `settings` stays valid only during the call.
    virtual void onSettingsChanged(const Settings& settings) { (void)settings; }
    virtual void onProfileChanged(const PlayerProfile& profile) { (void)profile; }

    // 120 Hz, before physics. Fill `out` (inputs override, drills, feedback).
    virtual void onTick(const TickContext& ctx, Actions& out) { (void)ctx; (void)out; }
    // Local car touched the ball (resolved one tick later with pre/post states).
    virtual void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
        (void)touch; (void)ctx; (void)out;
    }
    virtual void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
        (void)ev; (void)ctx; (void)out;
    }
    // Every AttemptResult reported by any module (after the engine routed it). Used by the coach.
    virtual void onResult(const AttemptResult& result, const TickContext& ctx, Actions& out) {
        (void)result; (void)ctx; (void)out;
    }
    // Named Kismet variables of the loaded custom map (~30 Hz while on a custom map).
    virtual void onKismet(const std::vector<KismetVar>& vars, const TickContext& ctx, Actions& out) {
        (void)vars; (void)ctx; (void)out;
    }
    // Command for this module ("<id> <name> args") from the UI or the console.
    virtual void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) { (void)cmd; (void)ctx; (void)out; }
    // Another module took the activity, or world control is no longer allowed: stop drills now.
    virtual void onActivityStopped() {}

    // Called for every rendered frame (game thread). Only draw, do not change gameplay state here.
    virtual void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
        (void)view; (void)ctx; (void)out;
    }

    // Progress persistence (records, history, adaptive levels). Keys must start with "<id>.".
    virtual void save(Store& store) const { (void)store; }
    virtual void load(const Store& store) { (void)store; }

    // Wiring done by the engine (or by tests with a fake host).
    void attach(ActivityHost* host) { host_ = host; }

protected:
    // Ask for exclusive control of the ball/car (drills). Returns false when world control is not allowed
    // in the current context. Granting stops the previous owner (its onActivityStopped is called).
    bool requestActivity(const std::string& name) { return host_ && host_->requestActivity(this, name); }
    void releaseActivity() {
        if (host_) host_->releaseActivity(this);
    }
    bool ownsActivity() const { return host_ && host_->ownsActivity(this); }

private:
    ActivityHost* host_ = nullptr;
};

} // namespace rlt
