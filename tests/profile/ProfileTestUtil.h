#pragma once
// Shared helpers for the profile module tests: realistic profiles (RL default bindings), a tick context
// harness and a fake activity host.

#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/common/Settings.h"
#include "core/modules/profile/ProfileModule.h"

namespace rlt_pf_test {

using namespace rlt;

inline void addBinding(std::vector<KeyBinding>& list, const std::string& action, const std::string& key) {
    list.push_back({action, key});
}

// Rocket League default gamepad bindings (StreamAPIPlugin defaults), no directional air roll.
inline PlayerProfile gamepadProfile() {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.carBodyId = 23;
    p.carBodyName = "Octane";
    auto& g = p.gamepadBindings;
    addBinding(g, "ThrottleForward", "XboxTypeS_RightTriggerAxis");
    addBinding(g, "ThrottleReverse", "XboxTypeS_LeftTriggerAxis");
    addBinding(g, "Jump", "XboxTypeS_A");
    addBinding(g, "Boost", "XboxTypeS_B");
    addBinding(g, "Handbrake", "XboxTypeS_X");
    addBinding(g, "ToggleRoll", "XboxTypeS_X");
    addBinding(g, "SecondaryCamera", "XboxTypeS_Y");
    addBinding(g, "ResetTraining", "XboxTypeS_RightShoulder");
    addBinding(g, "ChatPreset1", "XboxTypeS_DPad_Up");
    addBinding(g, "ChatPreset2", "XboxTypeS_DPad_Left");
    addBinding(g, "ChatPreset3", "XboxTypeS_DPad_Right");
    addBinding(g, "ChatPreset4", "XboxTypeS_DPad_Down");
    addBinding(g, "ToggleScoreboard", "XboxTypeS_Back");
    // The PC list is also filled (keyboard defaults).
    auto& k = p.pcBindings;
    addBinding(k, "ThrottleForward", "W");
    addBinding(k, "ThrottleReverse", "S");
    addBinding(k, "SteerLeft", "A");
    addBinding(k, "SteerRight", "D");
    addBinding(k, "Jump", "RightMouseButton");
    addBinding(k, "Boost", "LeftMouseButton");
    addBinding(k, "Handbrake", "LeftShift");
    addBinding(k, "ToggleRoll", "LeftShift");
    addBinding(k, "SecondaryCamera", "SpaceBar");
    addBinding(k, "ResetTraining", "BackSpace");
    addBinding(k, "ChatPreset1", "One");
    addBinding(k, "ChatPreset2", "Two");
    addBinding(k, "ChatPreset3", "Three");
    addBinding(k, "ChatPreset4", "Four");
    addBinding(k, "ToggleScoreboard", "Tab");
    return p;
}

inline PlayerProfile keyboardProfile() {
    PlayerProfile p = gamepadProfile();
    p.usingGamepad = false;
    return p;
}

// A "pro-like" gamepad setup: air roll left on LB, free air roll on RB (separate from powerslide on X).
inline PlayerProfile proGamepadProfile() {
    PlayerProfile p = gamepadProfile();
    p.gamepadBindings.clear();
    auto& g = p.gamepadBindings;
    addBinding(g, "ThrottleForward", "XboxTypeS_RightTriggerAxis");
    addBinding(g, "ThrottleReverse", "XboxTypeS_LeftTriggerAxis");
    addBinding(g, "Jump", "XboxTypeS_A");
    addBinding(g, "Boost", "XboxTypeS_B");
    addBinding(g, "Handbrake", "XboxTypeS_X");
    addBinding(g, "ToggleRoll", "XboxTypeS_RightShoulder");
    addBinding(g, "RollLeft", "XboxTypeS_LeftShoulder");
    addBinding(g, "SecondaryCamera", "XboxTypeS_Y");
    addBinding(g, "ResetTraining", "XboxTypeS_Back");
    return p;
}

struct Harness {
    Settings settings;
    SettingsSchema schema;
    GameContext game;
    Snapshot snap;
    TickHistory history{8};
    PlayerProfile profile;

    Harness() {
        describeGeneralSettings(schema);
        ProfileModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.worldControlAllowed = true;
    }
    TickContext ctx() const { return TickContext{snap, history, settings, game, profile, true, false, true}; }
    void set(const std::string& cvar, const std::string& value) { schema.apply(settings, cvar, value); }
};

class FakeHost : public ActivityHost {
public:
    bool requestActivity(Module* m, const std::string& name) override {
        ++requests;
        owner = m;
        activity = name;
        return true;
    }
    void releaseActivity(Module* m) override {
        ++releases;
        if (owner == m) owner = nullptr;
    }
    bool ownsActivity(const Module* m) const override { return owner == m; }

    int requests = 0;
    int releases = 0;
    const Module* owner = nullptr;
    std::string activity;
};

inline bool hasWorldControl(const Actions& a) {
    return a.setBall || a.setCar || a.setBoost || a.overrideInputs || a.freezeCar || a.setGameSpeed ||
           a.countdownMessage || a.goMessage;
}

inline bool containsText(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

inline bool hasEmDash(const std::string& s) { return containsText(s, "\xE2\x80\x94"); }

} // namespace rlt_pf_test
