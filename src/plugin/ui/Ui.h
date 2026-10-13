#pragma once
// ImGui front-end (BakkesMod ImGui 1.75: no tables API, use Columns). Runs on the RENDER thread:
//  - read engine/module state only under `auto lk = ui.engine.lock();` and copy what you need,
//  - change settings only through cvars (ui.setCvar), so they are saved and applied on the game thread,
//  - trigger gameplay through commands (ui.post) or ui.runOnGame(...), never by calling the SDK directly.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/common/Settings.h"
#include "core/engine/Engine.h"
#include "plugin/Sdk.h"

namespace rlt {

class ProfileService;
class MapsService;
class UpdateService;

struct UiContext {
    Engine& engine;
    std::shared_ptr<CVarManagerWrapper> cvars;
    std::shared_ptr<GameWrapper> game;  // only through runOnGame()
    ProfileService* profileService = nullptr;
    MapsService* mapsService = nullptr;
    bool showAdvanced = false;
    bool* configDirty = nullptr;  // set when a cvar changed: the plugin writes config.cfg a bit later
    UpdateService* updateService = nullptr;

    // Sets a cvar from the UI (string form, as in the schema). Marks the config for saving.
    void setCvar(const std::string& name, const std::string& value) const;
    // Posts a command to a module: post("kickoff", "start", {"diagonal"}).
    void post(const std::string& module, const std::string& name, std::vector<std::string> args = {}) const;
    // Runs code on the game thread (next frame).
    void runOnGame(std::function<void()> fn) const;
    // Copy of the current settings (taken under the engine lock).
    Settings settings() const;
};

namespace ui {

// Draws the control for one schema entry (checkbox, slider, colour, combo, text) with its tooltip.
// Returns true when the user changed the value this frame (the cvar has been set).
bool settingWidget(const UiContext& ctx, const SettingDef& def, const Settings& current);
// Every entry of one module section, plus a "Réinitialiser" button resetting that section to defaults.
void settingsSection(const UiContext& ctx, const std::string& module, const std::string& section, bool withReset = true);
// Every section of a module as collapsing headers (the first one open by default).
void moduleSettings(const UiContext& ctx, const std::string& module);
// Resets every setting of a module (or one section when `section` is not empty) to its default.
void resetToDefaults(const UiContext& ctx, const std::string& module, const std::string& section = "");

void helpMarker(const char* text);           // "(?)" with tooltip
void textColored(const Color& c, const std::string& text);
void separatorText(const std::string& text); // bold-ish section title with a separator
std::string percent(float ratio01);

} // namespace ui

// Tab registry: each feature implements one render function (src/plugin/ui/tabs/*.cpp).
struct UiTab {
    const char* id;
    const char* title;
    void (*render)(UiContext&);
};

void renderHomeTab(UiContext& ctx);
void renderOverlayTab(UiContext& ctx);
void renderFirstTouchTab(UiContext& ctx);
void renderPredictionTab(UiContext& ctx);
void renderMechanicsTab(UiContext& ctx);
void renderBallMechTab(UiContext& ctx);
void renderKickoffTab(UiContext& ctx);
void renderCoursesTab(UiContext& ctx);
void renderShootingTab(UiContext& ctx);
void renderMapsTab(UiContext& ctx);
void renderCoachTab(UiContext& ctx);
void renderProfileTab(UiContext& ctx);
void renderGeneralTab(UiContext& ctx);

const std::vector<UiTab>& uiTabs();

// Whole UI (tab bar). Used by the F2 settings page and by the standalone window.
void renderUi(UiContext& ctx);

} // namespace rlt
