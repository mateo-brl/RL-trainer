// Tests of core/common/Controls (key labels, action aliases, binding normalisation, prompts).
#include "tests/TestFramework.h"
#include "tests/profile/ProfileTestUtil.h"

#include "core/common/Controls.h"

using namespace rlt;
using namespace rlt_pf_test;

TEST(controls_xbox_labels) {
    CHECK_EQ(keyLabel("XboxTypeS_A"), "A");
    CHECK_EQ(keyLabel("XboxTypeS_B"), "B");
    CHECK_EQ(keyLabel("XboxTypeS_X"), "X");
    CHECK_EQ(keyLabel("XboxTypeS_Y"), "Y");
    CHECK_EQ(keyLabel("XboxTypeS_LeftShoulder"), "LB");
    CHECK_EQ(keyLabel("XboxTypeS_RightShoulder"), "RB");
    CHECK_EQ(keyLabel("XboxTypeS_LeftTrigger"), "LT");
    CHECK_EQ(keyLabel("XboxTypeS_RightTrigger"), "RT");
    CHECK_EQ(keyLabel("XboxTypeS_LeftTriggerAxis"), "LT");
    CHECK_EQ(keyLabel("XboxTypeS_RightTriggerAxis"), "RT");
    CHECK_EQ(keyLabel("XboxTypeS_LeftThumbStick"), "L3");
    CHECK_EQ(keyLabel("XboxTypeS_RightThumbStick"), "R3");
    CHECK_EQ(keyLabel("XboxTypeS_Back"), "Back");
    CHECK_EQ(keyLabel("XboxTypeS_Start"), "Start");
    CHECK_EQ(keyLabel("XboxTypeS_DPad_Up"), "D-pad haut");
    CHECK_EQ(keyLabel("XboxTypeS_DPad_Down"), "D-pad bas");
    CHECK_EQ(keyLabel("XboxTypeS_DPad_Left"), "D-pad gauche");
    CHECK_EQ(keyLabel("XboxTypeS_DPad_Right"), "D-pad droite");
    // Explicit Xbox style == Auto == old overload.
    CHECK_EQ(keyLabel("XboxTypeS_A", KeyStyle::Xbox), "A");
    CHECK_EQ(keyLabel("XboxTypeS_A", KeyStyle::Auto), keyLabel("XboxTypeS_A"));
    // Unknown gamepad key: prefix removed (old behaviour kept).
    CHECK_EQ(keyLabel("XboxTypeS_Guide"), "Guide");
}

TEST(controls_playstation_labels) {
    const KeyStyle ps = KeyStyle::PlayStation;
    CHECK_EQ(keyLabel("XboxTypeS_A", ps), "Croix");
    CHECK_EQ(keyLabel("XboxTypeS_B", ps), "Rond");
    CHECK_EQ(keyLabel("XboxTypeS_X", ps), "Carré");
    CHECK_EQ(keyLabel("XboxTypeS_Y", ps), "Triangle");
    CHECK_EQ(keyLabel("XboxTypeS_LeftShoulder", ps), "L1");
    CHECK_EQ(keyLabel("XboxTypeS_RightShoulder", ps), "R1");
    CHECK_EQ(keyLabel("XboxTypeS_LeftTriggerAxis", ps), "L2");
    CHECK_EQ(keyLabel("XboxTypeS_RightTrigger", ps), "R2");
    CHECK_EQ(keyLabel("XboxTypeS_LeftThumbStick", ps), "L3");
    CHECK_EQ(keyLabel("XboxTypeS_RightThumbStick", ps), "R3");
    CHECK_EQ(keyLabel("XboxTypeS_Back", ps), "Share");
    CHECK_EQ(keyLabel("XboxTypeS_Start", ps), "Options");
    CHECK_EQ(keyLabel("XboxTypeS_DPad_Up", ps), "D-pad haut");
    // Keyboard keys do not change with the gamepad style.
    CHECK_EQ(keyLabel("SpaceBar", ps), "Espace");
    CHECK_EQ(keyLabel("F5", ps), "F5");
}

TEST(controls_keyboard_mouse_labels) {
    CHECK_EQ(keyLabel("SpaceBar"), "Espace");
    CHECK_EQ(keyLabel("LeftShift"), "Maj gauche");
    CHECK_EQ(keyLabel("RightShift"), "Maj droite");
    CHECK_EQ(keyLabel("LeftControl"), "Ctrl gauche");
    CHECK_EQ(keyLabel("RightControl"), "Ctrl droit");
    CHECK_EQ(keyLabel("LeftAlt"), "Alt");
    CHECK_EQ(keyLabel("Enter"), "Entrée");
    CHECK_EQ(keyLabel("Escape"), "Échap");
    CHECK_EQ(keyLabel("BackSpace"), "Retour arrière");
    CHECK_EQ(keyLabel("Tab"), "Tab");
    CHECK_EQ(keyLabel("LeftMouseButton"), "Clic gauche");
    CHECK_EQ(keyLabel("RightMouseButton"), "Clic droit");
    CHECK_EQ(keyLabel("MiddleMouseButton"), "Clic molette");
    CHECK_EQ(keyLabel("ThumbMouseButton"), "Bouton souris 4");
    CHECK_EQ(keyLabel("MouseScrollUp"), "Molette haut");
    CHECK_EQ(keyLabel("F1"), "F1");
    CHECK_EQ(keyLabel("F12"), "F12");
    CHECK_EQ(keyLabel("NumPadOne"), "Pavé num. 1");
    CHECK_EQ(keyLabel("NumPadZero"), "Pavé num. 0");
    CHECK_EQ(keyLabel("Add"), "Pavé num. +");
    CHECK_EQ(keyLabel("Zero"), "0");
    CHECK_EQ(keyLabel("Nine"), "9");
    CHECK_EQ(keyLabel("Q"), "Q");
    CHECK_EQ(keyLabel("q"), "Q");
    CHECK_EQ(keyLabel("Up"), "Flèche haut");
    CHECK_EQ(keyLabel("PageDown"), "Page bas");
    // Case-insensitive (Unreal FNames).
    CHECK_EQ(keyLabel("spacebar"), "Espace");
    CHECK_EQ(keyLabel("XBOXTYPES_A"), "A");
    CHECK_EQ(keyLabel("f7"), "F7");
    // Empty, None, unknown.
    CHECK_EQ(keyLabel(""), "");
    CHECK_EQ(keyLabel("None"), "");
    CHECK_EQ(keyLabel("SomethingNew"), "SomethingNew");
    CHECK_EQ(keyLabel("F25"), "F25");  // not a function key we know: unchanged
}

TEST(controls_key_devices) {
    CHECK(keyDevice("XboxTypeS_A") == KeyDevice::Gamepad);
    CHECK(keyDevice("XboxTypeS_Whatever") == KeyDevice::Gamepad);
    CHECK(keyDevice("LeftMouseButton") == KeyDevice::Mouse);
    CHECK(keyDevice("MouseX") == KeyDevice::Mouse);
    CHECK(keyDevice("SpaceBar") == KeyDevice::Keyboard);
    CHECK(keyDevice("W") == KeyDevice::Keyboard);
    CHECK(keyDevice("F10") == KeyDevice::Keyboard);
    CHECK(keyDevice("NumPadFive") == KeyDevice::Keyboard);
    CHECK(keyDevice("Jump") == KeyDevice::Unknown);
    CHECK(keyDevice("") == KeyDevice::Unknown);
    CHECK(isGamepadKey("XboxTypeS_DPad_Up"));
    CHECK(!isGamepadKey("F5"));
    CHECK(looksLikeKeyName("RightMouseButton"));
    CHECK(!looksLikeKeyName("Boost"));
    CHECK(looksLikeActionName("Boost"));
    CHECK(looksLikeActionName("ToggleRoll"));
    CHECK(looksLikeActionName("secondarycamera"));
    CHECK(!looksLikeActionName("SpaceBar"));
}

TEST(controls_action_names_and_aliases) {
    CHECK_EQ(std::string(actionBindingName(GameAction::Jump)), "Jump");
    CHECK_EQ(std::string(actionBindingName(GameAction::Boost)), "Boost");
    CHECK_EQ(std::string(actionBindingName(GameAction::Powerslide)), "Handbrake");
    CHECK_EQ(std::string(actionBindingName(GameAction::AirRoll)), "ToggleRoll");
    CHECK_EQ(std::string(actionBindingName(GameAction::AirRollLeft)), "RollLeft");
    CHECK_EQ(std::string(actionBindingName(GameAction::AirRollRight)), "RollRight");
    CHECK_EQ(std::string(actionBindingName(GameAction::BallCam)), "SecondaryCamera");
    CHECK_EQ(std::string(actionBindingName(GameAction::ResetShot)), "ResetTraining");
    CHECK_EQ(std::string(actionBindingName(GameAction::Throttle)), "ThrottleForward");
    CHECK_EQ(std::string(actionBindingName(GameAction::Count)), "");

    CHECK(actionMatches(GameAction::BallCam, "SecondaryCamera"));
    CHECK(actionMatches(GameAction::BallCam, "BallCam"));
    CHECK(actionMatches(GameAction::BallCam, "secondarycamera"));
    CHECK(actionMatches(GameAction::AirRoll, "AirRoll"));
    CHECK(actionMatches(GameAction::AirRollLeft, "AirRollLeft"));
    CHECK(actionMatches(GameAction::ResetShot, "ResetShot"));
    CHECK(actionMatches(GameAction::Powerslide, "Powerslide"));
    CHECK(!actionMatches(GameAction::Jump, "Boost"));
    CHECK(!actionMatches(GameAction::Jump, ""));

    const auto aliases = actionBindingAliases(GameAction::BallCam);
    CHECK(aliases.size() >= 2);
    CHECK_EQ(aliases.front(), "SecondaryCamera");

    GameAction a = GameAction::Jump;
    CHECK(gameActionFromBinding("ToggleRoll", a) && a == GameAction::AirRoll);
    CHECK(gameActionFromBinding("RollRight", a) && a == GameAction::AirRollRight);
    CHECK(!gameActionFromBinding("ChatPreset1", a));

    CHECK_EQ(std::string(actionName(GameAction::Jump)), "Saut");
    CHECK_EQ(std::string(actionName(GameAction::AirRollLeft)), "Air roll gauche");
    CHECK_EQ(rlActionLabel("ThrottleForward"), "Accélérer");
    CHECK_EQ(rlActionLabel("ChatPreset2"), "Message rapide 2");
    CHECK_EQ(rlActionLabel("BallOnCarTraining"), "BallOnCarTraining");
}

TEST(controls_normalize_key_action_pairs) {
    // The documented SDK order (key, action).
    std::vector<std::pair<std::string, std::string>> raw = {
        {"XboxTypeS_A", "Jump"}, {"XboxTypeS_B", "Boost"}, {"XboxTypeS_Y", "SecondaryCamera"}, {"None", "RollLeft"},
        {"", "Jump"},            {"XboxTypeS_A", "Jump"},  // duplicate
    };
    const auto b = normalizeBindings(raw);
    CHECK_EQ(b.size(), static_cast<size_t>(3));
    CHECK_EQ(b[0].action, "Jump");
    CHECK_EQ(b[0].key, "XboxTypeS_A");
    CHECK_EQ(b[2].action, "SecondaryCamera");
    CHECK_EQ(b[2].key, "XboxTypeS_Y");
}

TEST(controls_normalize_swapped_pairs) {
    // Whole list in (action, key) order: detected and swapped.
    std::vector<std::pair<std::string, std::string>> raw = {
        {"Jump", "RightMouseButton"}, {"Boost", "LeftMouseButton"}, {"Handbrake", "LeftShift"},
        {"SomeFutureAction", "F"},    {"ChatPreset1", "One"},
    };
    const auto b = normalizeBindings(raw);
    CHECK_EQ(b.size(), static_cast<size_t>(5));
    CHECK_EQ(b[0].action, "Jump");
    CHECK_EQ(b[0].key, "RightMouseButton");
    CHECK_EQ(b[3].action, "SomeFutureAction");  // unknown action, still swapped by the global decision
    CHECK_EQ(b[3].key, "F");
}

TEST(controls_normalize_single_swapped_pair) {
    // Mostly (key, action), one pair reversed: fixed individually.
    std::vector<std::pair<std::string, std::string>> raw = {
        {"XboxTypeS_A", "Jump"}, {"XboxTypeS_B", "Boost"}, {"ToggleRoll", "XboxTypeS_LeftShoulder"},
        {"XboxTypeS_X", "Handbrake"},
    };
    const auto b = normalizeBindings(raw);
    CHECK_EQ(b.size(), static_cast<size_t>(4));
    CHECK_EQ(b[2].action, "ToggleRoll");
    CHECK_EQ(b[2].key, "XboxTypeS_LeftShoulder");
    // Unknown action with an unknown key: left as given (never guess).
    std::vector<std::pair<std::string, std::string>> odd = {{"XboxTypeS_A", "Jump"}, {"Foo", "Bar"}};
    const auto c = normalizeBindings(odd);
    CHECK_EQ(c.size(), static_cast<size_t>(2));
    CHECK_EQ(c[1].key, "Foo");
    CHECK_EQ(c[1].action, "Bar");
}

TEST(controls_normalize_caps_and_trims) {
    std::vector<std::pair<std::string, std::string>> raw;
    for (int i = 0; i < 40; ++i) raw.push_back({"F" + std::to_string(i % 12 + 1), "Action" + std::to_string(i)});
    CHECK_EQ(normalizeBindings(raw, 10).size(), static_cast<size_t>(10));
    const auto t = normalizeBindings({{"  SpaceBar ", " Jump "}});
    CHECK_EQ(t.size(), static_cast<size_t>(1));
    CHECK_EQ(t[0].key, "SpaceBar");
    CHECK_EQ(t[0].action, "Jump");
    CHECK(normalizeBindings({}).empty());
}

TEST(controls_action_keys_device_aware) {
    PlayerProfile pad = gamepadProfile();
    CHECK_EQ(actionKeyLabel(pad, GameAction::Jump), "A");
    CHECK_EQ(actionKeyLabel(pad, GameAction::Jump, KeyStyle::PlayStation), "Croix");
    CHECK_EQ(actionKeyLabel(pad, GameAction::BallCam), "Y");
    CHECK_EQ(actionKeyLabel(pad, GameAction::ResetShot), "RB");
    CHECK_EQ(actionKeyLabel(pad, GameAction::Throttle), "RT");
    CHECK_EQ(actionKeyLabel(pad, GameAction::AirRollLeft), "");
    CHECK_EQ(actionPrompt(pad, GameAction::Jump), "Saut (A)");
    CHECK_EQ(actionPrompt(pad, GameAction::Boost, KeyStyle::PlayStation), "Boost (Rond)");
    CHECK_EQ(actionPrompt(pad, GameAction::AirRollLeft), "Air roll gauche");

    PlayerProfile kb = keyboardProfile();
    CHECK_EQ(actionKeyLabel(kb, GameAction::Jump), "Clic droit");
    CHECK_EQ(actionKeyLabel(kb, GameAction::BallCam), "Espace");
    CHECK_EQ(actionKeyLabel(kb, GameAction::Powerslide), "Maj gauche");
    CHECK_EQ(actionPrompt(kb, GameAction::Boost), "Boost (Clic gauche)");

    // Gamepad keys stored in the PC list only (empty gamepad list): still found for a pad player.
    PlayerProfile mixed;
    mixed.usingGamepad = true;
    mixed.pcBindings = {{"Jump", "XboxTypeS_A"}, {"Jump", "SpaceBar"}, {"Boost", "XboxTypeS_RightShoulder"}};
    CHECK_EQ(actionKeyLabel(mixed, GameAction::Jump), "A");
    CHECK_EQ(actionKeyLabel(mixed, GameAction::Boost), "RB");
    const auto keys = actionKeys(mixed, GameAction::Jump, false);
    CHECK_EQ(keys.size(), static_cast<size_t>(1));
    CHECK_EQ(keys[0], "SpaceBar");

    // Aliases in the bindings are understood, duplicates across lists removed.
    PlayerProfile alias;
    alias.usingGamepad = true;
    alias.gamepadBindings = {{"BallCam", "XboxTypeS_Y"}, {"airrollleft", "XboxTypeS_LeftShoulder"}};
    alias.pcBindings = {{"SecondaryCamera", "XboxTypeS_Y"}};
    CHECK_EQ(actionKeys(alias, GameAction::BallCam, true).size(), static_cast<size_t>(1));
    CHECK_EQ(actionKeyLabel(alias, GameAction::AirRollLeft), "LB");

    // Empty profile: no label, prompt is the action name.
    PlayerProfile none;
    CHECK_EQ(actionKeyLabel(none, GameAction::Jump), "");
    CHECK_EQ(actionPrompt(none, GameAction::Jump), "Saut");
}

TEST(controls_key_style_from_index) {
    CHECK(keyStyleFromIndex(0) == KeyStyle::Auto);
    CHECK(keyStyleFromIndex(1) == KeyStyle::Xbox);
    CHECK(keyStyleFromIndex(2) == KeyStyle::PlayStation);
    CHECK(keyStyleFromIndex(7) == KeyStyle::Auto);
    CHECK(keyStyleFromIndex(-1) == KeyStyle::Auto);
}

TEST(controls_same_name) {
    CHECK(sameName("SpaceBar", "spacebar"));
    CHECK(!sameName("SpaceBar", "Space"));
    CHECK(sameName("", ""));
}
