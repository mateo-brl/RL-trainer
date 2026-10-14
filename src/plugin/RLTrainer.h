#pragma once
// BakkesMod entry point: wires hooks, drawable, cvars, timers and the ImGui windows to the core Engine.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "core/engine/Engine.h"
#include "plugin/GameBridge.h"
#include "plugin/Renderer.h"
#include "plugin/Sdk.h"
#include "plugin/services/MapsService.h"
#include "plugin/services/ProfileService.h"
#include "plugin/services/UpdateService.h"
#include "plugin/ui/Ui.h"

class RLTrainer : public BakkesMod::Plugin::BakkesModPlugin,
                  public BakkesMod::Plugin::PluginSettingsWindow,
                  public BakkesMod::Plugin::PluginWindow {
public:
    void onLoad() override;
    void onUnload() override;

    // F2 > Plugins > RL Trainer
    void RenderSettings() override;
    std::string GetPluginName() override;
    void SetImGuiContext(uintptr_t ctx) override;

    // Standalone window: console "togglemenu rltrainer"
    void Render() override;
    std::string GetMenuName() override;
    std::string GetMenuTitle() override;
    bool ShouldBlockInput() override;
    bool IsActiveOverlay() override;
    void OnOpen() override;
    void OnClose() override;

private:
    void registerCvars();
    void registerHooks();
    void hookPre(const std::string& name, std::function<void(std::string)> fn);
    void hookPost(const std::string& name, std::function<void(std::string)> fn);
    void registerCommands();
    void schedulePeriodic();
    void periodic();
    void applyMenuKey();

    void onVehicleInput(CarWrapper car, void* params);
    void onHitBall(CarWrapper car, void* params);
    void onDraw(CanvasWrapper canvas);
    void sendEvent(rlt::GameEventKind kind, const std::string& detail = "");
    void refreshContext();
    void refreshProfile(bool force);
    void run(rlt::Actions& actions, void* params = nullptr);

    std::filesystem::path dataDir() const;
    void loadProgress();
    void saveProgress();

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::unique_ptr<rlt::Engine> engine_;
    std::unique_ptr<rlt::GameBridge> bridge_;
    std::unique_ptr<rlt::ProfileService> profile_;
    std::unique_ptr<rlt::MapsService> maps_;
    std::unique_ptr<rlt::UpdateService> updates_;
    std::unique_ptr<rlt::UiContext> ui_;
    rlt::Renderer renderer_;

    bool windowOpen_ = false;
    bool configDirty_ = false;
    float configDirtyTimer_ = 0.f;
    float saveTimer_ = 0.f;
    float profileTimer_ = 0.f;
    int kismetCounter_ = 0;
    bool lastPaused_ = false;
    int64_t lastResetFrame_ = -1000;
    std::chrono::steady_clock::time_point lastDraw_{};
    bool hasLastDraw_ = false;
    std::vector<std::string> hookedPre_;
    std::vector<std::string> hookedPost_;

    // Menu key (rlt_menu_key). The cvar callback (any thread) raises the flag, periodic() applies it on the game
    // thread; the UI reads the status through UiContext::menuKeyStatus.
    std::atomic<bool> menuKeyDirty_{true};  // true: applied once menuKeyWait_ is over
    float menuKeyWait_ = 5.f;               // seconds before the first bind (game thread)
    std::string menuKeyBound_;              // game thread: key whose bind runs rlt_menu ("" = none)
    std::mutex menuKeyMutex_;
    rlt::MenuKeyStatus menuKeyStatus_;      // guarded by menuKeyMutex_
};
