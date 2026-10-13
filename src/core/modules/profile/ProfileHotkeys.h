#pragma once
// Automatic hotkeys for plugin commands. A key is proposed only when it is bound in NEITHER Rocket League
// binding list (PC and gamepad, any action, even unknown ones) and has no BakkesMod bind. BakkesMod binds are
// only readable through the SDK, so the UI reads them on the game thread and hands them to the module
// ("bmbinds" command). Applying the proposal always needs a click in the tab (never automatic).

#include <map>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/modules/profile/ProfileConfig.h"

namespace rlt::pf {

struct HotkeyCommand {
    std::string id;       // "menu", "stop", "firsttouch", "kickoff"
    std::string command;  // BakkesMod console command bound to the key
    std::string label;    // French: "Ouvrir RL Trainer"
};

// Commands to propose, in priority order, according to the settings.
std::vector<HotkeyCommand> hotkeyCommands(const ProfileConfig& cfg);
// Every command this module may bind (used to validate persisted undo entries).
const std::vector<HotkeyCommand>& allHotkeyCommands();
bool isKnownHotkeyCommand(const std::string& command);

// Candidate keys in preference order: function keys (F1, F2 and F6 excluded: BakkesMod menu and console)
// then the numeric keypad. Gamepad candidates (D-pad, Back) are added when `includeGamepad`.
std::vector<std::string> hotkeyCandidates(bool includeGamepad);
// Keys never proposed whatever the bindings say.
bool isReservedHotkey(const std::string& key);

enum class HotkeyState : int { Proposed = 0, Applied = 1, NoFreeKey = 2 };

struct HotkeyProposal {
    std::string id, command, label;
    std::string key;      // Unreal key name, empty when no key is free
    std::string keyText;  // "F5", "Pavé num. 1"
    HotkeyState state = HotkeyState::NoFreeKey;
    bool verified = false;  // BakkesMod bind of the key known to be free (or ours)
    std::string note;     // French status text
    // A key applied earlier for this command that cannot stay as it is (empty when none).
    std::string previousKey;
    bool previousMissing = false;   // BakkesMod reports it free (bind removed outside RL Trainer): re-proposed
    bool previousConflict = false;  // the game now uses it while the BakkesMod bind may still run our command
};

struct AppliedHotkey {
    std::string key;
    std::string command;
};

// State of an undo-list entry, as the tab shows it.
enum class AppliedState : int {
    Active = 0,    // BakkesMod still runs our command on the key, the game does not use it
    Unknown,       // BakkesMod bind not checked yet
    Missing,       // BakkesMod reports the key free: the bind was removed outside RL Trainer
    Rebound,       // the player rebound the key to another command: "Annuler" leaves it alone
    GameConflict,  // the game now uses the key too, and BakkesMod may still run our command on it
};

struct AppliedStatus {
    std::string key, command;
    AppliedState state = AppliedState::Unknown;
};

// Lower-cased key -> BakkesMod bind string ("" = free). Missing keys are "not checked yet".
using BakkesBinds = std::map<std::string, std::string>;
std::string bindMapKey(const std::string& key);

// True when `key` appears in either RL binding list (any action, case-insensitive).
bool keyUsedByGame(const PlayerProfile& p, const std::string& key);

// Deterministic choice: commands keep a key already applied (undo list or BakkesMod bind equal to the command)
// when it is still free in the game and BakkesMod still runs the command there (or was not checked yet); the
// others take the first free candidate in order, trying first the keys applied earlier for the same command
// (a bind removed outside RL Trainer is proposed again on the same key). Unchecked candidates (no BakkesMod
// info yet) are proposed only when `allowUnchecked`, flagged verified=false.
std::vector<HotkeyProposal> pickHotkeys(const PlayerProfile& p, const std::vector<HotkeyCommand>& commands,
                                        const std::vector<std::string>& candidates, const BakkesBinds& bakkes,
                                        const std::vector<AppliedHotkey>& applied, bool allowUnchecked);

// Status of each undo-list entry. Game conflicts are only reported when `bindingsKnown`.
std::vector<AppliedStatus> appliedStatus(const PlayerProfile& p, const BakkesBinds& bakkes,
                                         const std::vector<AppliedHotkey>& applied, bool bindingsKnown);
// True when BakkesMod is known to have no bind on `key`.
bool bakkesKnownFree(const BakkesBinds& bakkes, const std::string& key);

} // namespace rlt::pf
