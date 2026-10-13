#pragma once
// Reads the player's own Rocket League configuration through the SDK (bindings, controller, camera,
// units, car hitbox, body name) into a core PlayerProfile. Game thread only.

#include <memory>

#include "core/common/Context.h"
#include "plugin/Sdk.h"

namespace rlt {

class ProfileService {
public:
    ProfileService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars);

    // Reads everything available right now. Fields that cannot be read keep their previous value.
    PlayerProfile read(const PlayerProfile& previous) const;

    // Lightweight change detection (controller switch, car change): true when a full read is worth it.
    bool quickChanged(const PlayerProfile& current) const;

    // Binds `key` to `command` in BakkesMod (used by the auto-hotkey feature). Returns false when the key
    // already has a BakkesMod bind.
    bool bindIfFree(const std::string& key, const std::string& command) const;
    std::string currentBind(const std::string& key) const;

private:
    std::shared_ptr<GameWrapper> gw_;
    std::shared_ptr<CVarManagerWrapper> cvars_;
};

} // namespace rlt
