#pragma once
// Reads the player's own Rocket League configuration through the SDK (bindings, controller, camera,
// units, car hitbox, body name) into a core PlayerProfile. Game thread only (also from UiContext::runOnGame).
//
// Camera: the LIVE camera settings (CameraWrapper, then the car's PRI) are preferred, because RL can override
// the saved camera per input device; the saved SettingsWrapper values are the fallback.
// Bindings: both lists are normalised by core normalizeBindings() (pairs are (key, action), order auto-detected).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Context.h"
#include "plugin/Sdk.h"

namespace rlt {

class ProfileService {
public:
    ProfileService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars);

    // True only inside a local game (freeplay, training, offline match, workshop map). In the main menu the
    // player controller is not a real in-game controller: GetPRI() returns a non-null garbage pointer and reading
    // it crashes the game, so read() and quickChanged() do nothing outside a game.
    bool canRead() const;

    // Reads everything available right now. Fields that cannot be read keep their previous value. The car body
    // (id, name) is only updated together with a readable hitbox, so a known body always means a measured hitbox.
    PlayerProfile read(const PlayerProfile& previous) const;

    // Change detection (controller switch, car change, unit change, ball cam toggle, camera, gamepad settings,
    // bindings, hitbox ready after spawn): true when a full read is worth it. Cheap enough for every 3 s.
    bool quickChanged(const PlayerProfile& current) const;

    // Binds `key` to `command` in BakkesMod (used by the auto-hotkey feature). Returns false when the key
    // already has a BakkesMod bind (or when the key or the command is empty).
    bool bindIfFree(const std::string& key, const std::string& command) const;
    // Current BakkesMod bind of `key`, trimmed (spaces and surrounding quotes removed); "" when free.
    std::string currentBind(const std::string& key) const;
    // Removes the BakkesMod bind of `key` only when it still runs `command`. Returns true when removed.
    bool unbindIfMatches(const std::string& key, const std::string& command) const;
    // currentBind() for several keys: (key, bind) pairs in the same order.
    std::vector<std::pair<std::string, std::string>> currentBinds(const std::vector<std::string>& keys) const;

private:
    bool readCamera(CameraProfile& out) const;
    bool readBallCam(CarWrapper& car, bool& out) const;
    std::string readBodyName(int bodyId) const;

    std::shared_ptr<GameWrapper> gw_;
    std::shared_ptr<CVarManagerWrapper> cvars_;
};

} // namespace rlt
