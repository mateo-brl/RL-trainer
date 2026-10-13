#include "core/common/Controls.h"

#include <cctype>
#include <cstring>

namespace rlt {

namespace {

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameNameC(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

bool startsWithNoCase(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (lowerAscii(s[i]) != lowerAscii(prefix[i])) return false;
    return true;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// ---- Key names -------------------------------------------------------------------------------------------
struct KeyName {
    const char* key;      // Unreal key name
    const char* label;    // French label (Xbox style for gamepad keys)
    const char* psLabel;  // PlayStation label for gamepad keys, nullptr = same as label
    KeyDevice device;
};

constexpr KeyDevice kKb = KeyDevice::Keyboard;
constexpr KeyDevice kMs = KeyDevice::Mouse;
constexpr KeyDevice kPad = KeyDevice::Gamepad;

const KeyName kKeys[] = {
    // Gamepad (RL reports every pad, including PlayStation ones, with XboxTypeS_* names).
    {"XboxTypeS_A", "A", "Croix", kPad},
    {"XboxTypeS_B", "B", "Rond", kPad},
    {"XboxTypeS_X", "X", "Carré", kPad},
    {"XboxTypeS_Y", "Y", "Triangle", kPad},
    {"XboxTypeS_LeftShoulder", "LB", "L1", kPad},
    {"XboxTypeS_RightShoulder", "RB", "R1", kPad},
    {"XboxTypeS_LeftTrigger", "LT", "L2", kPad},
    {"XboxTypeS_RightTrigger", "RT", "R2", kPad},
    {"XboxTypeS_LeftTriggerAxis", "LT", "L2", kPad},
    {"XboxTypeS_RightTriggerAxis", "RT", "R2", kPad},
    {"XboxTypeS_LeftThumbStick", "L3", nullptr, kPad},
    {"XboxTypeS_RightThumbStick", "R3", nullptr, kPad},
    {"XboxTypeS_Back", "Back", "Share", kPad},
    {"XboxTypeS_Start", "Start", "Options", kPad},
    {"XboxTypeS_DPad_Up", "D-pad haut", nullptr, kPad},
    {"XboxTypeS_DPad_Down", "D-pad bas", nullptr, kPad},
    {"XboxTypeS_DPad_Left", "D-pad gauche", nullptr, kPad},
    {"XboxTypeS_DPad_Right", "D-pad droite", nullptr, kPad},
    {"XboxTypeS_LeftX", "Stick gauche (horizontal)", nullptr, kPad},
    {"XboxTypeS_LeftY", "Stick gauche (vertical)", nullptr, kPad},
    {"XboxTypeS_RightX", "Stick droit (horizontal)", nullptr, kPad},
    {"XboxTypeS_RightY", "Stick droit (vertical)", nullptr, kPad},
    // Mouse.
    {"LeftMouseButton", "Clic gauche", nullptr, kMs},
    {"RightMouseButton", "Clic droit", nullptr, kMs},
    {"MiddleMouseButton", "Clic molette", nullptr, kMs},
    {"ThumbMouseButton", "Bouton souris 4", nullptr, kMs},
    {"ThumbMouseButton2", "Bouton souris 5", nullptr, kMs},
    {"MouseScrollUp", "Molette haut", nullptr, kMs},
    {"MouseScrollDown", "Molette bas", nullptr, kMs},
    {"MouseX", "Souris (horizontal)", nullptr, kMs},
    {"MouseY", "Souris (vertical)", nullptr, kMs},
    // Keyboard: special keys.
    {"SpaceBar", "Espace", nullptr, kKb},
    {"Enter", "Entrée", nullptr, kKb},
    {"Escape", "Échap", nullptr, kKb},
    {"BackSpace", "Retour arrière", nullptr, kKb},
    {"Tab", "Tab", nullptr, kKb},
    {"CapsLock", "Verr. Maj", nullptr, kKb},
    {"LeftShift", "Maj gauche", nullptr, kKb},
    {"RightShift", "Maj droite", nullptr, kKb},
    {"LeftControl", "Ctrl gauche", nullptr, kKb},
    {"RightControl", "Ctrl droit", nullptr, kKb},
    {"LeftAlt", "Alt", nullptr, kKb},
    {"RightAlt", "Alt Gr", nullptr, kKb},
    {"Pause", "Pause", nullptr, kKb},
    {"ScrollLock", "Arrêt défil", nullptr, kKb},
    {"Insert", "Inser", nullptr, kKb},
    {"Delete", "Suppr", nullptr, kKb},
    {"Home", "Début", nullptr, kKb},
    {"End", "Fin", nullptr, kKb},
    {"PageUp", "Page haut", nullptr, kKb},
    {"PageDown", "Page bas", nullptr, kKb},
    {"Up", "Flèche haut", nullptr, kKb},
    {"Down", "Flèche bas", nullptr, kKb},
    {"Left", "Flèche gauche", nullptr, kKb},
    {"Right", "Flèche droite", nullptr, kKb},
    {"Tilde", "²", nullptr, kKb},
    {"Semicolon", ";", nullptr, kKb},
    {"Equals", "=", nullptr, kKb},
    {"Comma", ",", nullptr, kKb},
    {"Underscore", "-", nullptr, kKb},
    {"Period", ".", nullptr, kKb},
    {"Slash", "/", nullptr, kKb},
    {"LeftBracket", "[", nullptr, kKb},
    {"RightBracket", "]", nullptr, kKb},
    {"Backslash", "\\", nullptr, kKb},
    {"Quote", "'", nullptr, kKb},
    // Keyboard: digits row.
    {"Zero", "0", nullptr, kKb},
    {"One", "1", nullptr, kKb},
    {"Two", "2", nullptr, kKb},
    {"Three", "3", nullptr, kKb},
    {"Four", "4", nullptr, kKb},
    {"Five", "5", nullptr, kKb},
    {"Six", "6", nullptr, kKb},
    {"Seven", "7", nullptr, kKb},
    {"Eight", "8", nullptr, kKb},
    {"Nine", "9", nullptr, kKb},
    // Keyboard: numeric keypad.
    {"NumPadZero", "Pavé num. 0", nullptr, kKb},
    {"NumPadOne", "Pavé num. 1", nullptr, kKb},
    {"NumPadTwo", "Pavé num. 2", nullptr, kKb},
    {"NumPadThree", "Pavé num. 3", nullptr, kKb},
    {"NumPadFour", "Pavé num. 4", nullptr, kKb},
    {"NumPadFive", "Pavé num. 5", nullptr, kKb},
    {"NumPadSix", "Pavé num. 6", nullptr, kKb},
    {"NumPadSeven", "Pavé num. 7", nullptr, kKb},
    {"NumPadEight", "Pavé num. 8", nullptr, kKb},
    {"NumPadNine", "Pavé num. 9", nullptr, kKb},
    {"Multiply", "Pavé num. *", nullptr, kKb},
    {"Divide", "Pavé num. /", nullptr, kKb},
    {"Add", "Pavé num. +", nullptr, kKb},
    {"Subtract", "Pavé num. -", nullptr, kKb},
    {"Decimal", "Pavé num. .", nullptr, kKb},
    {"NumLock", "Verr. num", nullptr, kKb},
};

const KeyName* findKey(const std::string& key) {
    for (const KeyName& k : kKeys)
        if (sameNameC(key, k.key)) return &k;
    return nullptr;
}

// "F1".."F24" -> number, 0 otherwise.
int functionKeyNumber(const std::string& key) {
    if (key.size() < 2 || key.size() > 3 || lowerAscii(key[0]) != 'f') return 0;
    int n = 0;
    for (size_t i = 1; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) return 0;
        n = n * 10 + (key[i] - '0');
    }
    return (n >= 1 && n <= 24) ? n : 0;
}

bool isLetterKey(const std::string& key) {
    return key.size() == 1 && std::isalpha(static_cast<unsigned char>(key[0]));
}

// ---- RL binding action names -------------------------------------------------------------------------------
struct ActionName {
    const char* name;
    const char* label;
};

// Names seen in RL bindings (StreamAPIPlugin tables, InputLogger) plus the aliases accepted for GameAction.
const ActionName kRlActions[] = {
    {"ThrottleForward", "Accélérer"},
    {"ThrottleReverse", "Freiner / reculer"},
    {"SteerRight", "Tourner à droite"},
    {"SteerLeft", "Tourner à gauche"},
    {"YawRight", "Lacet droite (en l'air)"},
    {"YawLeft", "Lacet gauche (en l'air)"},
    {"PitchUp", "Nez vers le haut (en l'air)"},
    {"PitchDown", "Nez vers le bas (en l'air)"},
    {"RollRight", "Air roll droit"},
    {"RollLeft", "Air roll gauche"},
    {"Jump", "Saut"},
    {"Boost", "Boost"},
    {"Handbrake", "Glissade (powerslide)"},
    {"ToggleRoll", "Air roll libre"},
    {"SecondaryCamera", "Caméra balle"},
    {"RearCamera", "Caméra arrière"},
    {"ResetTraining", "Reset du tir"},
    {"LookUp", "Regarder en haut"},
    {"LookDown", "Regarder en bas"},
    {"LookRight", "Regarder à droite"},
    {"LookLeft", "Regarder à gauche"},
    {"SwivelUp", "Pivot caméra haut"},
    {"SwivelDown", "Pivot caméra bas"},
    {"SwivelRight", "Pivot caméra droite"},
    {"SwivelLeft", "Pivot caméra gauche"},
    {"ToggleScoreboard", "Tableau des scores"},
    {"PushToTalk", "Parler (micro)"},
    {"ChatPreset1", "Message rapide 1"},
    {"ChatPreset2", "Message rapide 2"},
    {"ChatPreset3", "Message rapide 3"},
    {"ChatPreset4", "Message rapide 4"},
    {"AutoSaveReplay", "Sauvegarder le replay"},
    {"UsePickup", "Utiliser le bonus (Rumble)"},
    {"NextPickup", "Bonus suivant (Rumble)"},
    {"ResetMouseCenter", "Recentrer la souris"},
    {"MouseSteerLeft", "Souris : tourner à gauche"},
    {"MouseSteerRight", "Souris : tourner à droite"},
    {"MouseThrottleForward", "Souris : accélérer"},
    {"MouseThrottleReverse", "Souris : reculer"},
    // Aliases (older names or names used by other tools).
    {"Throttle", "Accélérer"},
    {"Reverse", "Freiner / reculer"},
    {"Brake", "Freiner / reculer"},
    {"Powerslide", "Glissade (powerslide)"},
    {"AirRoll", "Air roll libre"},
    {"FreeAirRoll", "Air roll libre"},
    {"AirRollLeft", "Air roll gauche"},
    {"AirRollRight", "Air roll droit"},
    {"BallCam", "Caméra balle"},
    {"ToggleBallCam", "Caméra balle"},
    {"FocusOnBall", "Caméra balle"},
    {"ResetShot", "Reset du tir"},
    {"ResetTrainingShot", "Reset du tir"},
};

const ActionName* findAction(const std::string& name) {
    for (const ActionName& a : kRlActions)
        if (sameNameC(name, a.name)) return &a;
    return nullptr;
}

// Aliases per GameAction (main RL name first), nullptr-terminated.
constexpr int kMaxAliases = 6;
const char* const kAliases[static_cast<int>(GameAction::Count)][kMaxAliases] = {
    /* Throttle     */ {"ThrottleForward", "Throttle", "MouseThrottleForward", nullptr, nullptr, nullptr},
    /* Reverse      */ {"ThrottleReverse", "Reverse", "Brake", "MouseThrottleReverse", nullptr, nullptr},
    /* Jump         */ {"Jump", nullptr, nullptr, nullptr, nullptr, nullptr},
    /* Boost        */ {"Boost", nullptr, nullptr, nullptr, nullptr, nullptr},
    /* Powerslide   */ {"Handbrake", "Powerslide", nullptr, nullptr, nullptr, nullptr},
    /* AirRoll      */ {"ToggleRoll", "AirRoll", "FreeAirRoll", nullptr, nullptr, nullptr},
    /* AirRollLeft  */ {"RollLeft", "AirRollLeft", nullptr, nullptr, nullptr, nullptr},
    /* AirRollRight */ {"RollRight", "AirRollRight", nullptr, nullptr, nullptr, nullptr},
    /* BallCam      */ {"SecondaryCamera", "BallCam", "ToggleBallCam", "FocusOnBall", nullptr, nullptr},
    /* ResetShot    */ {"ResetTraining", "ResetShot", "ResetTrainingShot", nullptr, nullptr, nullptr},
};

bool validAction(GameAction a) { return static_cast<int>(a) >= 0 && a < GameAction::Count; }

bool containsName(const std::vector<std::string>& list, const std::string& s) {
    for (const auto& x : list)
        if (sameName(x, s)) return true;
    return false;
}

} // namespace

bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

// ---- Actions ---------------------------------------------------------------------------------------------
const char* actionName(GameAction a) {
    switch (a) {
    case GameAction::Throttle: return "Accélérer";
    case GameAction::Reverse: return "Freiner / reculer";
    case GameAction::Jump: return "Saut";
    case GameAction::Boost: return "Boost";
    case GameAction::Powerslide: return "Glissade";
    case GameAction::AirRoll: return "Air roll";
    case GameAction::AirRollLeft: return "Air roll gauche";
    case GameAction::AirRollRight: return "Air roll droit";
    case GameAction::BallCam: return "Caméra balle";
    case GameAction::ResetShot: return "Reset";
    default: return "";
    }
}

const char* actionBindingName(GameAction a) {
    if (!validAction(a)) return "";
    return kAliases[static_cast<int>(a)][0];
}

std::vector<std::string> actionBindingAliases(GameAction a) {
    std::vector<std::string> out;
    if (!validAction(a)) return out;
    for (const char* n : kAliases[static_cast<int>(a)])
        if (n) out.emplace_back(n);
    return out;
}

bool actionMatches(GameAction a, const std::string& rlAction) {
    if (!validAction(a) || rlAction.empty()) return false;
    for (const char* n : kAliases[static_cast<int>(a)])
        if (n && sameNameC(rlAction, n)) return true;
    return false;
}

bool gameActionFromBinding(const std::string& rlAction, GameAction& out) {
    for (int i = 0; i < static_cast<int>(GameAction::Count); ++i) {
        const GameAction a = static_cast<GameAction>(i);
        if (actionMatches(a, rlAction)) {
            out = a;
            return true;
        }
    }
    return false;
}

std::string rlActionLabel(const std::string& rlAction) {
    const ActionName* a = findAction(rlAction);
    return a ? std::string(a->label) : rlAction;
}

bool looksLikeActionName(const std::string& s) { return findAction(s) != nullptr; }

// ---- Keys ------------------------------------------------------------------------------------------------
KeyDevice keyDevice(const std::string& key) {
    if (key.empty()) return KeyDevice::Unknown;
    if (const KeyName* k = findKey(key)) return k->device;
    if (startsWithNoCase(key, "XboxTypeS_") || startsWithNoCase(key, "Gamepad")) return KeyDevice::Gamepad;
    if (functionKeyNumber(key) > 0 || isLetterKey(key)) return KeyDevice::Keyboard;
    return KeyDevice::Unknown;
}

bool looksLikeKeyName(const std::string& s) { return keyDevice(s) != KeyDevice::Unknown; }

std::string keyLabel(const std::string& key, KeyStyle style) {
    if (key.empty() || sameNameC(key, "None")) return {};
    if (const KeyName* k = findKey(key)) {
        if (style == KeyStyle::PlayStation && k->psLabel) return k->psLabel;
        return k->label;
    }
    if (const int f = functionKeyNumber(key)) return "F" + std::to_string(f);
    if (isLetterKey(key)) return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key[0]))));
    const char* xbox = "XboxTypeS_";
    if (startsWithNoCase(key, xbox)) return key.substr(std::strlen(xbox));
    return key;
}

std::string keyLabel(const std::string& key) { return keyLabel(key, KeyStyle::Auto); }

std::vector<std::string> actionKeys(const PlayerProfile& profile, GameAction a, bool gamepad) {
    std::vector<std::string> out;
    if (!validAction(a)) return out;
    const std::vector<KeyBinding>* lists[2] = {gamepad ? &profile.gamepadBindings : &profile.pcBindings,
                                               gamepad ? &profile.pcBindings : &profile.gamepadBindings};
    for (const auto* list : lists) {
        for (const auto& b : *list) {
            if (b.key.empty() || !actionMatches(a, b.action)) continue;
            const bool padKey = keyDevice(b.key) == KeyDevice::Gamepad;
            if (padKey != gamepad) continue;
            if (!containsName(out, b.key)) out.push_back(b.key);
        }
    }
    return out;
}

std::string actionKeyLabel(const PlayerProfile& profile, GameAction a, KeyStyle style) {
    const std::vector<std::string> keys = actionKeys(profile, a, profile.usingGamepad);
    return keys.empty() ? std::string() : keyLabel(keys.front(), style);
}

std::string actionKeyLabel(const PlayerProfile& profile, GameAction a) {
    return actionKeyLabel(profile, a, KeyStyle::Auto);
}

std::string actionPrompt(const PlayerProfile& profile, GameAction a, KeyStyle style) {
    const std::string k = actionKeyLabel(profile, a, style);
    return k.empty() ? std::string(actionName(a)) : std::string(actionName(a)) + " (" + k + ")";
}

std::string actionPrompt(const PlayerProfile& profile, GameAction a) { return actionPrompt(profile, a, KeyStyle::Auto); }

// ---- Binding normalisation -----------------------------------------------------------------------------
std::vector<KeyBinding> normalizeBindings(const std::vector<std::pair<std::string, std::string>>& pairs,
                                          size_t maxEntries) {
    // Decide the global order from the content: count evidence for (key, action) and for (action, key).
    int keyFirst = 0, actionFirst = 0;
    for (const auto& p : pairs) {
        if (looksLikeKeyName(p.first)) ++keyFirst;
        if (looksLikeActionName(p.second)) ++keyFirst;
        if (looksLikeActionName(p.first)) ++actionFirst;
        if (looksLikeKeyName(p.second)) ++actionFirst;
    }
    const bool swapAll = actionFirst > keyFirst;  // ties keep the documented SDK order (key, action)

    std::vector<KeyBinding> out;
    out.reserve(pairs.size() < maxEntries ? pairs.size() : maxEntries);
    for (const auto& p : pairs) {
        if (out.size() >= maxEntries) break;
        std::string key = trimmed(swapAll ? p.second : p.first);
        std::string action = trimmed(swapAll ? p.first : p.second);
        // Per-pair guard: the "action" is clearly a key and the "key" clearly an action.
        if (looksLikeKeyName(action) && looksLikeActionName(key)) std::swap(key, action);
        if (key.empty() || action.empty() || sameNameC(key, "None") || sameNameC(action, "None")) continue;
        bool dup = false;
        for (const auto& b : out)
            if (sameName(b.key, key) && sameName(b.action, action)) {
                dup = true;
                break;
            }
        if (!dup) out.push_back({std::move(action), std::move(key)});
    }
    return out;
}

} // namespace rlt
