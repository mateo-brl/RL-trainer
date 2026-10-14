#pragma once
// Requests from the core to the game, executed by the plugin glue on the game thread.
// World-changing actions (ball/car placement, input override, game speed) are only executed when the
// emitting module owns the current activity AND the context allows world control (freeplay, see Engine).

#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/common/Context.h"
#include "core/common/Grade.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt {

struct BallPlacement {
    Vec3 pos;
    Vec3 vel;
    Vec3 angVel;
};

struct CarPlacement {
    Vec3 pos;
    Rot rot;
    Vec3 vel;
    Vec3 angVel;
    std::optional<float> boost;  // 0..100, unchanged when empty
};

// Boost rules of the local car during a drill (freeplay "unlimited boost" falsifies kickoff times).
// The glue saves the original rules the first time and the engine restores them when the activity ends.
struct BoostRules {
    bool unlimited = false;
    bool noBoost = false;
};

enum class ToastKind : int { Info = 0, Ok = 1, Warning = 2, Error = 3 };

struct ToastMessage {
    std::string title;
    std::string text;
    ToastKind kind = ToastKind::Info;
    float seconds = 3.5f;
    bool migration = false;  // a saved setting moved to a new default: the engine merges those of one tick into one
};

// Toast of a one-time move of saved settings to new defaults (a module's migrateDefaults).
inline ToastMessage migrationToast(std::string title, std::string text) {
    ToastMessage t;
    t.title = std::move(title);
    t.text = std::move(text);
    t.kind = ToastKind::Info;
    t.seconds = 8.f;
    t.migration = true;
    return t;
}

// The same toast for every trajectory turned off (prediction, guide lines, 3D markers...): they are all listed in
// the HUD page, so the player gets one sentence and one place to look.
inline ToastMessage trajectoriesMigrationToast() {
    return migrationToast("Trajectoires", "Les trajectoires et lignes guides sont coupées par défaut. Tu peux les "
                                          "remettre dans la page HUD, section Trajectoires.");
}

struct ChatMessage {
    std::string text;
    std::string sender = "RL Trainer";
};

// Everything a module may ask for during one callback. Filled by modules, consumed by the glue.
struct Actions {
    // World control (filtered by the engine, see header comment).
    std::optional<BallPlacement> setBall;
    std::optional<CarPlacement> setCar;
    std::optional<float> setBoost;          // 0..100
    std::optional<Inputs> overrideInputs;   // replace the player's inputs for THIS tick (pre-hook only)
    bool freezeCar = false;                 // zero inputs + keep car still for this tick
    std::optional<float> setGameSpeed;      // 1.0 = normal (sv_soccar_gamespeed); restored by the engine on stop
    std::optional<BoostRules> boostRules;   // restored by the engine when the activity ends
    bool resetPickups = false;              // respawn every boost pad (ServerWrapper::ResetPickups)

    // Native freeplay countdown messages ("3", "2", "1", "GO!").
    std::optional<int> countdownMessage;    // 3, 2, 1
    bool goMessage = false;

    // Feedback.
    std::vector<ChatMessage> chat;
    std::vector<ToastMessage> toasts;
    std::vector<AttemptResult> results;     // routed to the coach module by the engine
    std::vector<GameEvent> events;          // broadcast to the OTHER modules (e.g. mechanics -> FlipReset)

    // Allow-listed console commands. The glue refuses anything else:
    //   "load_workshop \"<absolute path>\"", "load_freeplay", "writeconfig",
    //   "unreal:servertravel ?" (reload map), "unreal:start MENU_Main_p?close" (main menu).
    std::vector<std::string> consoleCommands;

    bool requestSave = false;               // persist progress soon

    // Reset settings to their schema defaults: module ids (only the emitting module's own id is honoured)
    // or "<module>/<section>" for one section. Executed by the glue through cvars (so they are saved).
    std::vector<std::string> resetSettings;
    // Set some of the emitting module's OWN settings (cvar name, value in cvar format), saved like a UI change.
    std::vector<std::pair<std::string, std::string>> setCvars;

    // Engine-generated (never set by modules): put back the saved boost rules.
    bool restoreBoostRules = false;

    bool empty() const {
        return !setBall && !setCar && !setBoost && !overrideInputs && !freezeCar && !setGameSpeed && !countdownMessage &&
               !goMessage && chat.empty() && toasts.empty() && results.empty() && events.empty() && consoleCommands.empty() &&
               !requestSave && resetSettings.empty() && setCvars.empty() && !boostRules && !resetPickups &&
               !restoreBoostRules;
    }

    void clearWorldControl() {
        setBall.reset();
        setCar.reset();
        setBoost.reset();
        overrideInputs.reset();
        freezeCar = false;
        setGameSpeed.reset();
        boostRules.reset();
        resetPickups = false;
        countdownMessage.reset();
        goMessage = false;
    }

    // Moves everything from `o` into this (later writes win for single-valued fields).
    void merge(Actions&& o) {
        if (o.setBall) setBall = o.setBall;
        if (o.setCar) setCar = o.setCar;
        if (o.setBoost) setBoost = o.setBoost;
        if (o.overrideInputs) overrideInputs = o.overrideInputs;
        freezeCar = freezeCar || o.freezeCar;
        if (o.setGameSpeed) setGameSpeed = o.setGameSpeed;
        if (o.boostRules) boostRules = o.boostRules;
        resetPickups = resetPickups || o.resetPickups;
        restoreBoostRules = restoreBoostRules || o.restoreBoostRules;
        if (o.countdownMessage) countdownMessage = o.countdownMessage;
        goMessage = goMessage || o.goMessage;
        for (auto& m : o.chat) chat.push_back(std::move(m));
        for (auto& t : o.toasts) toasts.push_back(std::move(t));
        for (auto& r : o.results) results.push_back(std::move(r));
        for (auto& e : o.events) events.push_back(std::move(e));
        for (auto& c : o.consoleCommands) consoleCommands.push_back(std::move(c));
        requestSave = requestSave || o.requestSave;
        for (auto& r : o.resetSettings) resetSettings.push_back(std::move(r));
        for (auto& c : o.setCvars) setCvars.push_back(std::move(c));
    }
};

} // namespace rlt
