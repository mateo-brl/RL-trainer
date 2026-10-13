#include "core/common/Controls.h"

namespace rlt {

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
    switch (a) {
    case GameAction::Throttle: return "Throttle";
    case GameAction::Reverse: return "Reverse";
    case GameAction::Jump: return "Jump";
    case GameAction::Boost: return "Boost";
    case GameAction::Powerslide: return "Handbrake";
    case GameAction::AirRoll: return "AirRoll";
    case GameAction::AirRollLeft: return "AirRollLeft";
    case GameAction::AirRollRight: return "AirRollRight";
    case GameAction::BallCam: return "SecondaryCamera";
    case GameAction::ResetShot: return "ResetShot";
    default: return "";
    }
}

std::string keyLabel(const std::string& key) {
    const std::string xbox = "XboxTypeS_";
    if (key.rfind(xbox, 0) == 0) return key.substr(xbox.size());
    if (key == "SpaceBar") return "Espace";
    if (key == "LeftMouseButton") return "Clic gauche";
    if (key == "RightMouseButton") return "Clic droit";
    if (key == "LeftShift") return "Maj gauche";
    return key;
}

std::string actionKeyLabel(const PlayerProfile& profile, GameAction a) {
    const auto& list = profile.usingGamepad ? profile.gamepadBindings : profile.pcBindings;
    const std::string name = actionBindingName(a);
    for (const auto& b : list)
        if (b.action == name) return keyLabel(b.key);
    return {};
}

std::string actionPrompt(const PlayerProfile& profile, GameAction a) {
    const std::string k = actionKeyLabel(profile, a);
    return k.empty() ? std::string(actionName(a)) : std::string(actionName(a)) + " (" + k + ")";
}

} // namespace rlt
