#pragma once
// In-game update: asks GitHub for the latest release, downloads rltrainer.dll next to the loaded one and swaps
// the files (a loaded DLL can be renamed on Windows, not overwritten). The new version loads at the next game
// start. HTTP callbacks run on a BakkesMod worker thread: they only touch this object's state and files.

#include <memory>
#include <mutex>
#include <string>

#include "core/common/MiniJson.h"
#include "plugin/Sdk.h"

namespace rlt {

class UpdateService {
public:
    enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

    struct State {
        Phase phase = Phase::Idle;
        std::string current;     // running version
        std::string latest;      // latest published version (when known)
        std::string pageUrl;     // release page
        std::string message;     // French status text for the UI
        float progress = 0.f;    // 0..1 while downloading
    };

    UpdateService(std::shared_ptr<GameWrapper> gw, std::string repoSlug, std::string currentVersion);
    ~UpdateService() { *alive_ = false; }

    // Removes leftovers of a previous update (rltrainer.dll.old, .download). Call from onLoad.
    void cleanup();
    void checkAsync();
    void downloadAsync();
    State state() const;

private:
    std::filesystem::path pluginDir() const;
    void setState(Phase p, const std::string& msg);

    std::shared_ptr<GameWrapper> gw_;
    std::string repo_;
    mutable std::mutex mutex_;
    State state_;
    ReleaseInfo release_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace rlt
