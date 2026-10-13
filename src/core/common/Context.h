#pragma once
// Data passed from the glue to the engine besides per-tick snapshots: game context, player profile,
// touches, discrete game events, console/UI commands, camera view for drawing, Kismet variables.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt {

enum class GameMode : int {
    Menu = 0,
    Freeplay = 1,        // standard freeplay (world control allowed)
    CustomTraining = 2,  // training pack / editor play test
    Workshop = 3,        // custom map loaded offline (load_workshop): analysis only by default
    OfflineMatch = 4,    // exhibition / local match
    Replay = 5,
    Online = 6,          // everything off
    Unknown = 7,
};

inline const char* gameModeLabel(GameMode m) {
    switch (m) {
    case GameMode::Menu: return "Menu";
    case GameMode::Freeplay: return "Freeplay";
    case GameMode::CustomTraining: return "Entraînement personnalisé";
    case GameMode::Workshop: return "Map workshop";
    case GameMode::OfflineMatch: return "Match hors ligne";
    case GameMode::Replay: return "Replay";
    case GameMode::Online: return "En ligne";
    default: return "Inconnu";
    }
}

// Category of the loaded map, decided by the maps module (catalogue, Kismet signature or user choice).
enum class MapCategory : int { Standard = 0, Rings = 1, Dribble = 2, AirDribble = 3, Shooting = 4, Obstacle = 5, Other = 6 };

struct GameContext {
    GameMode mode = GameMode::Menu;
    std::string mapName;         // GameWrapper::GetCurrentMap()
    bool isCustomMap = false;    // not in the stock map list
    std::string workshopId;      // when known (last launched by the maps tab, or catalogue match)
    std::string mapTitle;        // display title when known
    MapCategory category = MapCategory::Standard;
    bool paused = false;
    // Computed by the engine from mode + user overrides: may modules teleport the ball/car?
    bool worldControlAllowed = false;
};

struct CameraProfile {
    float fov = 110.f, height = 100.f, pitch = -4.f, distance = 270.f, stiffness = 0.5f, swivelSpeed = 5.f,
          transitionSpeed = 1.f;
    bool ballCam = true;
};

struct KeyBinding {
    std::string action;  // RL action name, e.g. "Jump", "Boost", "Handbrake", "AirRoll", "ResetShot"
    std::string key;     // Unreal key name, e.g. "SpaceBar", "XboxTypeS_A", "LeftMouseButton"
};

// What the plugin detected about the player's own Rocket League configuration.
struct PlayerProfile {
    bool valid = false;
    bool usingGamepad = false;
    bool metric = true;                  // GameWrapper::GetbMetric
    float dodgeDeadzone = 0.5f;          // GamepadSettings::DodgeInputThreshold
    float controllerDeadzone = 0.1f;
    float steeringSensitivity = 1.f;
    float airControlSensitivity = 1.f;
    CameraProfile camera;
    std::vector<KeyBinding> pcBindings;
    std::vector<KeyBinding> gamepadBindings;
    Vec3 hitboxExtent{118.01f, 84.20f, 36.16f};
    Vec3 hitboxOffset{13.88f, 0.f, 20.75f};
    int carBodyId = 0;
    std::string carBodyName;             // e.g. "Octane"
    std::string playerName;
    std::string playerId;                // UniqueID string, used to separate profiles
    float screenWidth = 1920.f, screenHeight = 1080.f;
};

// A car-ball contact of the local car, resolved by the engine from the tick history (2 ticks later):
// `pre` is the state at the start of the touch tick (before the hit), `post1` the next tick and `post`
// two ticks after `pre` (the hit impulse is fully applied there; use `post` for "after the touch").
struct TouchEvent {
    int64_t frame = 0;
    Snapshot pre;
    Snapshot post1;
    Snapshot post;
    bool hasHitInfo = false;   // HitLocation/HitNormal from the OnHitBall hook
    Vec3 hitLocation;          // world contact point
    Vec3 hitNormal;            // from the car towards the ball
    bool newContact = true;    // false when the car was already touching the ball within the last 6 ticks
    int ticksSincePrevTouch = 1 << 30;
};

enum class GameEventKind : int {
    ContextChanged = 0,  // GameContext updated (mode or map)
    ResetShot,           // player pressed "reset shot" (PlayerResetTraining)
    Goal,                // ball exploded in a goal
    FlipReset,           // engine reported a flip reset for the local car
    CarSpawned,          // car (re)spawned / respawn after demo
    MapUnloaded,         // leaving freeplay / workshop map
    Paused,
    Unpaused,
    ProfileChanged,      // PlayerProfile refreshed
};

struct GameEvent {
    GameEventKind kind = GameEventKind::ContextChanged;
    int64_t frame = 0;
    std::string detail;
};

// Command coming from the UI (ImGui) or the console (notifiers): "<module> <name> [args...]".
struct Command {
    std::string module;  // module id, or "engine"
    std::string name;
    std::vector<std::string> args;
};

// Camera and screen at draw time. Positions for drawing use the interpolated (visual) actor locations.
struct ViewContext {
    bool valid = false;
    Vec3 camPos;
    Basis camBasis;            // fwd = view direction
    float fovDeg = 90.f;       // horizontal FOV
    float screenW = 1920.f, screenH = 1080.f;
    Vec3 carVisualPos;
    Basis carVisualBasis;
    Vec3 carVel;
    bool carValid = false;
    Vec3 ballVisualPos;
    Vec3 ballVel;              // also valid in replays (no physics ticks there)
    bool ballValid = false;
    float ballRadius = 91.25f;
    float uiScale = 1.f;       // user HUD scale (general settings) times display scale
};

// One named Kismet variable of the loaded map (read-only snapshot).
struct KismetVar {
    enum class Type : int { Int, Float, Bool, String, Other };
    std::string name;
    Type type = Type::Other;
    int i = 0;
    float f = 0.f;
    bool b = false;
    std::string s;
};

} // namespace rlt
