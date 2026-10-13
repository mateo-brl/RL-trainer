#pragma once
// Player-facing names of game actions and of the player's own buttons, built from the PlayerProfile
// (the player's real Rocket League bindings). Every module uses this to write prompts such as
// "Saute (A)" with the button the player actually uses.

#include <string>

#include "core/common/Context.h"

namespace rlt {

enum class GameAction : int {
    Throttle = 0,
    Reverse,
    Jump,
    Boost,
    Powerslide,     // handbrake; also air roll on default bindings
    AirRoll,
    AirRollLeft,
    AirRollRight,
    BallCam,
    ResetShot,      // freeplay / training "reset"
    Count
};

// French name of the action: "Saut", "Boost", "Glissade", "Air roll gauche"...
const char* actionName(GameAction a);

// Unreal action names used in Rocket League bindings for this action (e.g. "Jump"), for lookups.
const char* actionBindingName(GameAction a);

// Label of the player's button/key for this action on the device in use (gamepad or keyboard/mouse),
// e.g. "A", "RB", "Espace", "Clic droit". Empty when the action is not bound.
std::string actionKeyLabel(const PlayerProfile& profile, GameAction a);

// Short readable label for an Unreal key name ("XboxTypeS_A" -> "A", "SpaceBar" -> "Espace").
std::string keyLabel(const std::string& unrealKey);

// "Saut (A)" or just "Saut" when unknown.
std::string actionPrompt(const PlayerProfile& profile, GameAction a);

} // namespace rlt
