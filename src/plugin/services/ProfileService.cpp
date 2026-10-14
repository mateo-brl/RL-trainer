#include "plugin/services/ProfileService.h"

#include <cmath>

#include "core/common/Controls.h"

namespace rlt {

namespace {

bool inRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

// Rejects garbage (menu cameras, zeroed structs) before trusting camera values.
bool plausibleCamera(const ProfileCameraSettings& c) {
    return inRange(c.FOV, 60.f, 130.f) && inRange(c.Distance, 50.f, 500.f) && inRange(c.Height, 0.f, 300.f) &&
           inRange(c.Pitch, -60.f, 15.f) && inRange(c.Stiffness, 0.f, 1.01f) && inRange(c.SwivelSpeed, 0.1f, 11.f) &&
           inRange(c.TransitionSpeed, 0.1f, 3.f);
}

CameraProfile toCameraProfile(const ProfileCameraSettings& c, bool ballCam) {
    CameraProfile p;
    p.fov = c.FOV;
    p.height = c.Height;
    p.pitch = c.Pitch;
    p.distance = c.Distance;
    p.stiffness = c.Stiffness;
    p.swivelSpeed = c.SwivelSpeed;
    p.transitionSpeed = c.TransitionSpeed;
    p.ballCam = ballCam;
    return p;
}

bool sameCamera(const CameraProfile& a, const CameraProfile& b) {
    const auto eq = [](float x, float y, float tol) { return std::fabs(x - y) <= tol; };
    return eq(a.fov, b.fov, 0.5f) && eq(a.height, b.height, 0.5f) && eq(a.pitch, b.pitch, 0.5f) &&
           eq(a.distance, b.distance, 0.5f) && eq(a.stiffness, b.stiffness, 0.005f) &&
           eq(a.swivelSpeed, b.swivelSpeed, 0.05f) && eq(a.transitionSpeed, b.transitionSpeed, 0.05f);
}

bool sameBindings(const std::vector<KeyBinding>& a, const std::vector<KeyBinding>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].action != b[i].action || a[i].key != b[i].key) return false;
    return true;
}

// Full size from the SDK value (half extents, see physics.md 7.2). Returns false when not ready (0 at spawn).
bool hitboxSize(CarWrapper& car, Vec3& extent, Vec3& offset) {
    Vec3 e = sdk::toVec(car.GetLocalCollisionExtent());
    if (!(e.x > 1.f && e.y > 1.f && e.z > 1.f) || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z))
        return false;
    if (e.x < 90.f) e = e * 2.f;
    extent = e;
    offset = sdk::toVec(car.GetLocalCollisionOffset());
    return true;
}

std::string trimBind(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    if (b - a >= 2 && s[a] == '"' && s[b - 1] == '"') {
        ++a;
        --b;
    }
    return s.substr(a, b - a);
}

} // namespace

ProfileService::ProfileService(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars)
    : gw_(std::move(gw)), cvars_(std::move(cvars)) {}

bool ProfileService::canRead() const {
    if (!gw_ || gw_->IsInOnlineGame() || gw_->IsInReplay()) return false;
    return gw_->IsInFreeplay() || gw_->IsInCustomTraining() || gw_->IsInGame();
}

bool ProfileService::readBallCam(CarWrapper& car, bool& out) const {
    // The car's own PRI, never PlayerController::GetPRI() (see canRead()).
    if (car.IsNull()) return false;
    PriWrapper pri = car.GetPRI();
    if (pri.IsNull()) return false;
    out = pri.GetbUsingSecondaryCamera() != 0;
    return true;
}

bool ProfileService::readCamera(CameraProfile& out) const {
    if (!canRead()) return false;
    // Live camera first (per-device overrides), only with our own car on the field.
    CarWrapper car = gw_->GetLocalCar();
    if (!car.IsNull()) {
        CameraWrapper cam = gw_->GetCamera();
        if (!cam.IsNull()) {
            const ProfileCameraSettings c = cam.GetCameraSettings();
            if (plausibleCamera(c)) {
                out = toCameraProfile(c, out.ballCam);
                return true;
            }
        }
        PriWrapper pri = car.GetPRI();
        if (!pri.IsNull()) {
            const ProfileCameraSettings c = pri.GetCameraSettings();
            if (plausibleCamera(c)) {
                out = toCameraProfile(c, out.ballCam);
                return true;
            }
        }
    }
    SettingsWrapper settings = gw_->GetSettings();
    const ProfileCameraSettings c = settings.GetCameraSettings();
    if (!plausibleCamera(c)) return false;
    out = toCameraProfile(c, out.ballCam);
    return true;
}

std::string ProfileService::readBodyName(int bodyId) const {
    if (!gw_ || bodyId <= 0) return {};
    ItemsWrapper items = gw_->GetItemsWrapper();
    if (items.IsNull()) return {};
    ProductWrapper product = items.GetProduct(bodyId);
    if (product.IsNull()) return {};
    UnrealStringWrapper label = product.GetLongLabel();
    if (!label.IsNull()) {
        const std::string s = label.ToString();
        if (!s.empty()) return s;
    }
    UnrealStringWrapper shortLabel = product.GetLabel();
    return shortLabel.IsNull() ? std::string() : shortLabel.ToString();
}

PlayerProfile ProfileService::read(const PlayerProfile& previous) const {
    PlayerProfile p = previous;
    if (!canRead()) return p;
    p.valid = true;
    p.metric = gw_->GetbMetric() != 0;

    PlayerControllerWrapper pc = gw_->GetPlayerController();
    if (!pc.IsNull()) p.usingGamepad = pc.GetbUsingGamepad();

    SettingsWrapper settings = gw_->GetSettings();
    const GamepadSettings pad = settings.GetGamepadSettings();
    if (inRange(pad.DodgeInputThreshold, 0.01f, 1.f)) p.dodgeDeadzone = pad.DodgeInputThreshold;
    if (inRange(pad.ControllerDeadzone, 0.f, 1.f)) p.controllerDeadzone = pad.ControllerDeadzone;
    if (inRange(pad.SteeringSensitivity, 0.1f, 20.f)) p.steeringSensitivity = pad.SteeringSensitivity;
    if (inRange(pad.AirControlSensitivity, 0.1f, 20.f)) p.airControlSensitivity = pad.AirControlSensitivity;

    CameraProfile cam = p.camera;
    if (readCamera(cam)) p.camera = cam;
    CarWrapper car = gw_->GetLocalCar();
    bool ballCam = p.camera.ballCam;
    if (readBallCam(car, ballCam)) p.camera.ballCam = ballCam;

    // The SDK returns (key, action) pairs (InputLogger, StreamAPIPlugin); normalizeBindings detects the order anyway.
    // An empty answer (loading screens) keeps the previous lists.
    std::vector<KeyBinding> pcList = normalizeBindings(settings.GetAllPCBindings());
    std::vector<KeyBinding> padList = normalizeBindings(settings.GetAllGamepadBindings());
    if (!pcList.empty() || !padList.empty()) {
        p.pcBindings = std::move(pcList);
        p.gamepadBindings = std::move(padList);
    }

    // Body and hitbox change together: the extent reads 0 on the first tick after a spawn (physics.md 7.2), and a
    // new body paired with the previous (or default Octane) hitbox would be announced with the wrong family.
    // quickChanged() keeps asking for a read while the body differs, so the pair lands on a later read.
    if (!car.IsNull()) {
        const int body = car.GetLoadoutBody();
        Vec3 extent, offset;
        if (body > 0 && hitboxSize(car, extent, offset)) {
            p.hitboxExtent = extent;
            p.hitboxOffset = offset;
            if (body != previous.carBodyId || previous.carBodyName.empty()) p.carBodyName = readBodyName(body);
            p.carBodyId = body;
        }
    }

    const Vector2 screen = gw_->GetScreenSize();
    if (screen.X > 0 && screen.Y > 0) {
        p.screenWidth = static_cast<float>(screen.X);
        p.screenHeight = static_cast<float>(screen.Y);
    }
    UnrealStringWrapper name = gw_->GetPlayerName();
    if (!name.IsNull()) {
        const std::string n = name.ToString();
        if (!n.empty()) p.playerName = n;
    }
    p.playerId = gw_->GetUniqueID().GetIdString();
    return p;
}

bool ProfileService::quickChanged(const PlayerProfile& current) const {
    if (!canRead()) return false;
    if (!current.valid) return true;

    PlayerControllerWrapper pc = gw_->GetPlayerController();
    if (!pc.IsNull() && pc.GetbUsingGamepad() != current.usingGamepad) return true;
    CarWrapper car = gw_->GetLocalCar();
    bool ballCam = current.camera.ballCam;
    if (readBallCam(car, ballCam) && ballCam != current.camera.ballCam) return true;

    if (!car.IsNull()) {
        const int body = car.GetLoadoutBody();
        if (body > 0) {  // same condition as read(): no endless full reads while the body is unreadable
            if (body != current.carBodyId) return true;
            Vec3 extent, offset;
            if (hitboxSize(car, extent, offset) &&
                (length(extent - current.hitboxExtent) > 0.5f || length(offset - current.hitboxOffset) > 0.5f))
                return true;
        }
    }
    if ((gw_->GetbMetric() != 0) != current.metric) return true;

    SettingsWrapper settings = gw_->GetSettings();
    const GamepadSettings pad = settings.GetGamepadSettings();
    const auto moved = [](float now, float was) { return std::isfinite(now) && std::fabs(now - was) > 1e-3f; };
    if ((inRange(pad.DodgeInputThreshold, 0.01f, 1.f) && moved(pad.DodgeInputThreshold, current.dodgeDeadzone)) ||
        (inRange(pad.ControllerDeadzone, 0.f, 1.f) && moved(pad.ControllerDeadzone, current.controllerDeadzone)) ||
        (inRange(pad.SteeringSensitivity, 0.1f, 20.f) && moved(pad.SteeringSensitivity, current.steeringSensitivity)) ||
        (inRange(pad.AirControlSensitivity, 0.1f, 20.f) &&
         moved(pad.AirControlSensitivity, current.airControlSensitivity)))
        return true;

    CameraProfile cam = current.camera;
    if (readCamera(cam) && !sameCamera(cam, current.camera)) return true;

    const std::vector<KeyBinding> pcList = normalizeBindings(settings.GetAllPCBindings());
    const std::vector<KeyBinding> padList = normalizeBindings(settings.GetAllGamepadBindings());
    if ((!pcList.empty() || !padList.empty()) &&
        (!sameBindings(pcList, current.pcBindings) || !sameBindings(padList, current.gamepadBindings)))
        return true;
    return false;
}

std::string ProfileService::currentBind(const std::string& key) const {
    if (!cvars_ || key.empty()) return {};
    return trimBind(cvars_->getBindStringForKey(key));
}

std::vector<std::pair<std::string, std::string>> ProfileService::currentBinds(const std::vector<std::string>& keys) const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(keys.size());
    for (const auto& k : keys) out.emplace_back(k, currentBind(k));
    return out;
}

bool ProfileService::bindIfFree(const std::string& key, const std::string& command) const {
    if (!cvars_ || key.empty() || command.empty()) return false;
    if (!currentBind(key).empty()) return false;
    cvars_->setBind(key, command);
    return true;
}

bool ProfileService::unbindIfMatches(const std::string& key, const std::string& command) const {
    if (!cvars_ || key.empty() || command.empty()) return false;
    if (currentBind(key) != command) return false;
    cvars_->removeBind(key);
    return true;
}

} // namespace rlt
