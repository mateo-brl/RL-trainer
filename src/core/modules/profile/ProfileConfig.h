#pragma once
// Settings of the "Ma config" module (automatic detection of the player's Rocket League configuration:
// bindings, camera, deadzones, car hitbox) and of the plugin hotkeys it proposes.
// Every field is declared in ProfileModule::describeSettings (cvar prefix "rlt_pf_").
// Other modules may read `buttonStyle` (0 Auto, 1 Xbox, 2 PlayStation) through keyStyleFromIndex() in
// core/common/Controls.h to print PlayStation button names when the player chose them.

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct ProfileConfig {
    bool enabled = true;
    int buttonStyle = 0;             // 0 Auto (Xbox names), 1 Xbox, 2 PlayStation
    bool toastOnChange = true;       // one notification when the config is detected or changes

    // Advice shown in the tab (never applied automatically).
    bool cameraAdvice = true;
    bool deadzoneAdvice = true;
    bool bindingAdvice = true;
    float adviceMargin = 0.f;        // widens every "usual range" by this fraction of its width (advanced)

    // Automatic hotkeys proposed for plugin commands (applied only after a click in the tab).
    bool hotkeyMenu = true;          // "rlt_menu"
    bool hotkeyStop = true;          // "rlt engine stop"
    bool hotkeyFirstTouch = false;   // "rlt firsttouch drill mix"
    bool hotkeyKickoff = false;      // "rlt kickoff start random"
    bool hotkeyGamepad = false;      // also consider free gamepad buttons (advanced)

    // In-game display.
    bool showHitbox = false;         // wireframe of the car hitbox
    Color hitboxColor{0, 220, 255, 200};
    float hitboxWidth = 2.f;
};

} // namespace rlt
