#include "plugin/services/ProfileService.h"

namespace rlt {

ProfileService::ProfileService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars)
    : gw_(std::move(gw)), cvars_(std::move(cvars)) {}

PlayerProfile ProfileService::read(const PlayerProfile& previous) const {
    PlayerProfile p = previous;
    p.valid = true;
    p.metric = gw_->GetbMetric() != 0;

    PlayerControllerWrapper pc = gw_->GetPlayerController();
    if (!pc.IsNull()) p.usingGamepad = pc.GetbUsingGamepad();

    SettingsWrapper settings = gw_->GetSettings();
    const GamepadSettings pad = settings.GetGamepadSettings();
    if (pad.DodgeInputThreshold > 0.f) p.dodgeDeadzone = pad.DodgeInputThreshold;
    p.controllerDeadzone = pad.ControllerDeadzone;
    p.steeringSensitivity = pad.SteeringSensitivity;
    p.airControlSensitivity = pad.AirControlSensitivity;

    const ProfileCameraSettings cam = settings.GetCameraSettings();
    if (cam.FOV > 1.f) {
        p.camera.fov = cam.FOV;
        p.camera.height = cam.Height;
        p.camera.pitch = cam.Pitch;
        p.camera.distance = cam.Distance;
        p.camera.stiffness = cam.Stiffness;
        p.camera.swivelSpeed = cam.SwivelSpeed;
        p.camera.transitionSpeed = cam.TransitionSpeed;
    }

    // The SDK returns (key, action) pairs (InputLogger, StreamAPIPlugin). KeyBinding is {action, key}.
    p.pcBindings.clear();
    for (const auto& kv : settings.GetAllPCBindings()) p.pcBindings.push_back({kv.second, kv.first});
    p.gamepadBindings.clear();
    for (const auto& kv : settings.GetAllGamepadBindings()) p.gamepadBindings.push_back({kv.second, kv.first});

    CarWrapper car = gw_->GetLocalCar();
    if (!car.IsNull()) {
        Vec3 extent = sdk::toVec(car.GetLocalCollisionExtent());
        if (extent.x > 1.f && extent.y > 1.f && extent.z > 1.f) {
            if (extent.x < 90.f) extent = extent * 2.f;  // SDK gives half extents; profile stores full sizes
            p.hitboxExtent = extent;
            p.hitboxOffset = sdk::toVec(car.GetLocalCollisionOffset());
        }
        p.carBodyId = car.GetLoadoutBody();
    }

    const Vector2 screen = gw_->GetScreenSize();
    if (screen.X > 0 && screen.Y > 0) {
        p.screenWidth = static_cast<float>(screen.X);
        p.screenHeight = static_cast<float>(screen.Y);
    }
    p.playerId = gw_->GetUniqueID().GetIdString();
    return p;
}

bool ProfileService::quickChanged(const PlayerProfile& current) const {
    PlayerControllerWrapper pc = gw_->GetPlayerController();
    if (!pc.IsNull() && pc.GetbUsingGamepad() != current.usingGamepad) return true;
    CarWrapper car = gw_->GetLocalCar();
    if (!car.IsNull() && car.GetLoadoutBody() != current.carBodyId) return true;
    if ((gw_->GetbMetric() != 0) != current.metric) return true;
    return false;
}

std::string ProfileService::currentBind(const std::string& key) const { return cvars_->getBindStringForKey(key); }

bool ProfileService::bindIfFree(const std::string& key, const std::string& command) const {
    if (!cvars_->getBindStringForKey(key).empty()) return false;
    cvars_->setBind(key, command);
    return true;
}

} // namespace rlt
