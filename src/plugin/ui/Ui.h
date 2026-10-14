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

struct ImVec4;

namespace rlt {

class ProfileService;
class MapsService;
class UpdateService;

// Menu key (rlt_menu_key) as the glue applied it on the game thread.
struct MenuKeyStatus {
    bool checked = false;  // false until the glue applied the setting once
    bool bound = false;    // `key` runs rlt_menu
    std::string key;       // key as applied ("" = none)
    std::string text;      // status sentence shown in the HUD page
};

struct UiContext {
    Engine& engine;
    std::shared_ptr<CVarManagerWrapper> cvars;
    std::shared_ptr<GameWrapper> game;  // only through runOnGame()
    ProfileService* profileService = nullptr;
    MapsService* mapsService = nullptr;
    bool showAdvanced = false;
    bool* configDirty = nullptr;  // set when a cvar changed: the plugin writes config.cfg a bit later
    UpdateService* updateService = nullptr;
    // Executes Actions produced by an engine call made from runOnGame (game thread only), exactly like the glue
    // does for hooks: world actions, chat, toasts, console commands, settings resets.
    std::function<void(Actions&)> applyActions;
    std::shared_ptr<bool> alive;  // false once the plugin unloads: pending runOnGame calls are dropped
    // Current menu key status (thread-safe copy provided by the glue). Unset: no status to show.
    std::function<MenuKeyStatus()> menuKeyStatus;

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
// Resets the listed cvars to their schema default (names missing from the schema are skipped).
void resetCvars(const UiContext& ctx, const std::vector<std::string>& cvars);

void helpMarker(const char* text);           // "(?)" with tooltip
void textColored(const Color& c, const std::string& text);
void separatorText(const std::string& text); // bold-ish section title with a separator
std::string percent(float ratio01);
// Progress bar with its value written to the right of the bar, never over the fill (light text on the amber
// fill is unreadable). width > 0: bar width in pixels; width <= 0: the rest of the line minus -width.
void progressBar(float fraction01, const std::string& value, float width = 0.f);

// Menu theme: dark neutral surfaces, rounded corners, one amber accent. Pushed around RL Trainer's own drawing
// only and popped right after, so BakkesMod's windows keep their look. Calls must be paired; pushing twice
// (window, then renderUi inside it) is fine.
void pushTheme();
void popTheme();
ImVec4 accentColor();      // amber #FFC740, same as the HUD panel titles
ImVec4 accentSelectedBg(); // background of a selected item (sidebar, filter buttons)
// Opens a page of the menu on the next frame (UiTab id, e.g. "hud").
void goToPage(const char* id);

} // namespace ui

// Page registry: each feature implements one render function (src/plugin/ui/tabs/*.cpp). The menu lists the
// pages in a left sidebar, under the header `group` ("" = no header).
struct UiTab {
    const char* id;
    const char* title;
    void (*render)(UiContext&);
    const char* group = "";
};

void renderHomeTab(UiContext& ctx);
void renderHudTab(UiContext& ctx);
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

// Whole UI (sidebar and page). Used by the F2 settings page and by the standalone window.
void renderUi(UiContext& ctx);

} // namespace rlt
