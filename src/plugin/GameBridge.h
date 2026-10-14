#pragma once
// SDK <-> core translation: reads game state into core structs and executes core Actions on the game.
// Every method must be called on the game thread.

#include <memory>
#include <string>

#include "core/common/Actions.h"
#include "core/common/Context.h"
#include "core/common/Snapshot.h"
#include "plugin/Sdk.h"

namespace rlt {

class GameBridge {
public:
    GameBridge(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars);

    // True when `car` is the local player's car.
    bool isLocalCar(CarWrapper car) const;

    // Snapshot of the local car + ball at the start of the current physics tick.
    // `input` is the ControllerInput the game is about to apply (SetVehicleInput params), may be null.
    bool buildSnapshot(CarWrapper car, const ControllerInput* input, Snapshot& out) const;

    // Camera, screen and visual (interpolated) actor positions for drawing.
    ViewContext buildView(CanvasWrapper& canvas, float hudScale) const;

    // Current game mode (online, freeplay, workshop...). `isStockMap` tells if the map is a stock arena.
    GameMode detectMode(bool isStockMap) const;
    // Freeplay, custom training, offline match or workshop map: the only places where the local car and the
    // player controller wrappers are safe to read (menus give non-null garbage, online is off limits).
    bool inLocalGame() const;

    // Applies world/feedback actions. `params` is the SetVehicleInput params (ControllerInput*) when called
    // from that pre-hook, otherwise null (input overrides are then ignored).
    void execute(const Actions& actions, void* params, bool hudAccents);

    // Physics frame counter (EngineTA::GetPhysicsFrame), -1 when unavailable.
    int64_t physicsFrame() const;

    // Puts back the boost rules saved before a drill changed them (also called on unload).
    void restoreBoostRules();

private:
    ServerWrapper currentServer() const;
    void runConsole(const std::string& cmd);

    std::shared_ptr<GameWrapper> gw_;
    std::shared_ptr<CVarManagerWrapper> cvars_;
    bool frozen_ = false;
    // Boost rules of the local car before a drill changed them.
    bool boostSaved_ = false;
    int savedUnlimitedRefCount_ = 0;
    unsigned long savedNoBoost_ = 0;
};

} // namespace rlt
