#pragma once
// Player-facing names of game actions and of the player's own buttons, built from the PlayerProfile
// (the player's real Rocket League bindings). Every module uses this to write prompts such as
// "Saut (A)" with the button the player actually uses.
//
// Button style: the SDK cannot tell an Xbox pad from a PlayStation pad (both report "XboxTypeS_*" keys), so
// the player picks the style in the "Ma config" tab (Settings::profile.buttonStyle). Modules that want
// PlayStation names when chosen call the overloads taking a KeyStyle:
//     actionPrompt(ctx.profile, GameAction::Jump, keyStyleFromIndex(ctx.settings.profile.buttonStyle))
// The overloads without a style use KeyStyle::Auto (Xbox names on a gamepad).

#include <string>
#include <utility>
#include <vector>

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

// Gamepad label style. Auto = Xbox labels (A, B, LB, RT...) until the platform can be detected.
enum class KeyStyle : int { Auto = 0, Xbox = 1, PlayStation = 2 };

// Converts the "Style des boutons" setting index (0 Auto, 1 Xbox, 2 PlayStation) to a KeyStyle.
inline KeyStyle keyStyleFromIndex(int index) {
    return index == 1 ? KeyStyle::Xbox : (index == 2 ? KeyStyle::PlayStation : KeyStyle::Auto);
}

// Which device an Unreal key name belongs to.
enum class KeyDevice : int { Unknown = 0, Keyboard = 1, Mouse = 2, Gamepad = 3 };

// French name of the action: "Saut", "Boost", "Glissade", "Air roll gauche"...
const char* actionName(GameAction a);

// Main Unreal action name used in Rocket League bindings for this action (e.g. "Jump", "Handbrake",
// "ToggleRoll", "SecondaryCamera"), for lookups. See actionBindingAliases for every accepted spelling.
const char* actionBindingName(GameAction a);

// Every binding action name accepted for this action, main name first (e.g. ball cam: "SecondaryCamera",
// "BallCam", ...). Comparisons are case-insensitive.
std::vector<std::string> actionBindingAliases(GameAction a);

// True when the RL binding action name `rlAction` means `a` (any alias, case-insensitive).
bool actionMatches(GameAction a, const std::string& rlAction);

// Maps an RL binding action name to a GameAction. Returns false when it is not one of them.
bool gameActionFromBinding(const std::string& rlAction, GameAction& out);

// Every key bound to the action, best first: keys of the device in use (gamepad keys when `gamepad`,
// keyboard/mouse keys otherwise), from both binding lists (the gamepad list may be empty and the PC list may
// hold gamepad keys), without duplicates.
std::vector<std::string> actionKeys(const PlayerProfile& profile, GameAction a, bool gamepad);

// Label of the player's button/key for this action on the device in use (gamepad or keyboard/mouse),
// e.g. "A", "RB", "Espace", "Clic droit". Empty when the action is not bound.
std::string actionKeyLabel(const PlayerProfile& profile, GameAction a);
std::string actionKeyLabel(const PlayerProfile& profile, GameAction a, KeyStyle style);

// Short readable label for an Unreal key name ("XboxTypeS_A" -> "A", "SpaceBar" -> "Espace").
// Unknown names are returned unchanged ("XboxTypeS_" prefix removed).
std::string keyLabel(const std::string& unrealKey);
std::string keyLabel(const std::string& unrealKey, KeyStyle style);

// "Saut (A)" or just "Saut" when unknown.
std::string actionPrompt(const PlayerProfile& profile, GameAction a);
std::string actionPrompt(const PlayerProfile& profile, GameAction a, KeyStyle style);

// ---- Key and binding helpers (used by the profile module and the glue) --------------------------------------

// Device of an Unreal key name (case-insensitive). Unknown for anything that is not a key name.
KeyDevice keyDevice(const std::string& unrealKey);
inline bool isGamepadKey(const std::string& unrealKey) { return keyDevice(unrealKey) == KeyDevice::Gamepad; }
// True for a recognised key name (keyboard, mouse or gamepad).
bool looksLikeKeyName(const std::string& s);
// True for a known Rocket League binding action name (any spelling we know).
bool looksLikeActionName(const std::string& s);
// French label of an RL binding action name ("ThrottleForward" -> "Accélérer"); `rlAction` itself when unknown.
std::string rlActionLabel(const std::string& rlAction);

// Converts raw SDK binding pairs into KeyBinding{action, key}. The SDK gives (key, action) pairs, but the
// order is detected from the content (which side holds key names) and each pair whose "action" is clearly a
// key while its "key" is clearly an action is swapped. Empty and "None" keys are dropped, duplicates removed,
// and the result is capped at `maxEntries`.
std::vector<KeyBinding> normalizeBindings(const std::vector<std::pair<std::string, std::string>>& pairs,
                                          size_t maxEntries = 512);

// Case-insensitive ASCII comparison of two names (keys and actions are Unreal FNames: case-insensitive).
bool sameName(const std::string& a, const std::string& b);

} // namespace rlt
