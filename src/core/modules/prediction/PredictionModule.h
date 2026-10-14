#pragma once
// Prédiction : trajectoire de la balle (rebonds, atterrissage, zone de première touche), conseiller d'interception
// ("Arrive à X km/h dans 1.2 s") et alerte de but. Analysis only: the module never moves the ball or the car and
// never requests the world-control activity.
//
// Performance contract: the path is simulated in onTick only every `recomputeTicks` ticks, or at once when the ball
// leaves the cached prediction (touch, reset, teleport). draw() only reads the cache. With no physics ticks
// (replays, local car demolished) the path is computed at draw time from the camera view, at most every 6 frames,
// without the intercept part.
//
// Off by default since v1.0.8. Saved settings of the previous version (config.cfg, which still holds the old "on")
// are moved once on the first tick (flag "prediction.defaults_v2" in the Store): only a value still equal to the old
// default changes.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Module.h"
#include "core/common/Units.h"
#include "core/modules/prediction/BallPath.h"
#include "core/modules/prediction/Intercept.h"
#include "core/modules/prediction/PredictionConfig.h"

namespace rlt {

// Live state for the UI tab (plain copy, take it under the engine lock).
struct PredictionStatus {
    bool enabled = false;
    bool hidden = false;          // "toggle" / "hide" command
    bool active = false;          // a prediction is available
    bool fromView = false;        // computed from the camera view (replay, no physics ticks)
    bool replay = false;          // fromView because of a replay (team unknown), not a stale tick cache
    bool frozen = false;          // the ball hangs still in the air (training pack before the first move)
    pr::ArenaMode arena = pr::ArenaMode::Soccar;
    float horizon = 3.f;
    bool ballAirborne = false;
    float ballHeight = 0.f;       // ball centre z (uu)
    float ballSpeed = 0.f;        // uu/s
    bool hasLanding = false;
    float landingIn = 0.f;        // seconds from now
    Vec3 landingPos;
    bool hasApex = false;
    float apexIn = 0.f;
    float apexHeight = 0.f;
    bool hasZone = false;
    float zoneIn = 0.f;
    float zoneHeight = 0.f;
    int bounces = 0;              // upcoming bounces with a marker
    bool hasIntercept = false;
    float interceptIn = 0.f;
    float interceptBallSpeed = 0.f;  // horizontal ball speed at the intercept (uu/s): the speed to match
    float interceptCarSpeed = 0.f;   // estimated car speed on arrival at full throttle
    float interceptHeight = 0.f;
    pr::JumpKind interceptJump = pr::JumpKind::None;
    int goalSide = 0;             // +1 orange goal (+Y), -1 blue goal (-Y), 0 none
    int goalForPlayer = 0;        // +1 opponent goal, -1 own goal, 0 team unknown (same team as the HUD)
    float goalIn = 0.f;
    SpeedUnit unit = SpeedUnit::Kph;  // resolved display unit
    long long recomputes = 0;         // tick-mode predictions
    long long forcedRecomputes = 0;   // triggered by an unexpected ball velocity / position change
    long long viewRecomputes = 0;     // draw-time predictions (replays)
};

// Measured accuracy: predicted ball position 1 s ahead compared with where the ball really was.
struct PredictionAccuracy {
    int count = 0;
    float meanError = 0.f;        // uu
    float maxError = 0.f;
    float lastError = 0.f;
    int disturbed = 0;            // probes spoilt by something the model cannot know (another car, a reset...)
    std::vector<float> recent;    // oldest first, at most PredictionModule::kAccuracyHistory values
};

class PredictionModule : public Module {
public:
    static constexpr int kAccuracyHistory = 60;
    static constexpr int kProbeTicks = 120;              // accuracy probe 1 s ahead
    static constexpr float kDisturbedError = 250.f;      // uu: above, the probe counts as disturbed
    static constexpr float kTeleportCheck = 40.f;        // uu off the prediction: recompute now
    static constexpr float kTeleportDistance = 300.f;    // uu off the prediction in one tick: teleport
    static constexpr int kViewRecomputeFrames = 6;       // draw-time prediction at most every 6 frames
    static constexpr int kStaleDrawFrames = 10;          // frames without a tick before checking the view
    static constexpr float kStaleMoveDistance = 60.f;    // uu: the visible ball left the cached path
    static constexpr int kStaleMaxFrames = 600;          // frames without a tick: use the view anyway
    static constexpr int kAirborneHoldTicks = 30;        // "only airborne" keeps showing 0.25 s after landing
    static constexpr float kMinInterceptDisplay = 0.1f;  // s: closer intercepts are "already there"
    static constexpr float kMaxInterceptCarZ = 300.f;    // no intercept advice while the car flies higher
    static constexpr int kFrozenTicks = 2;               // ticks without moving before a hanging ball is "frozen"
    static constexpr float kFrozenMove = 0.01f;          // uu moved in one tick below which the ball did not move
    static constexpr float kFrozenSpeed = 1.f;           // uu/s: a frozen ball reports (almost) no velocity
    static constexpr int kProbeCooldownTicks = 60;       // no new probe for 0.5 s after an outside disturbance
    static constexpr float kLocalTouchMargin = 80.f;     // uu around ball + hitbox: the local car may be touching
    static constexpr float kCloseColorDistance = 80.f;   // RGB distance under which two colours blend together

    PredictionModule() = default;

    const char* id() const override { return "prediction"; }
    const char* title() const override { return "Prédiction"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onProfileChanged(const PlayerProfile& profile) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    // ---- Read accessors (UI and tests)
    const PredictionConfig& config() const { return cfg_; }
    PredictionStatus status() const;
    PredictionAccuracy accuracy() const;
    bool hidden() const { return hidden_; }
    const pr::PredictedPath& path() const { return path_; }
    const pr::PredictedPath& viewPath() const { return viewPath_; }
    const pr::InterceptResult& intercept() const { return intercept_; }
    // The one-time move of the previous version's saved default ran (flag "prediction.defaults_v2").
    bool defaultsMigrated() const { return defaultsV2_; }

    // Collision world for a setting (0 auto, 1 standard, 2 floor only, 3 none), the map and the ball size.
    static pr::ArenaMode chooseArena(int setting, const GameContext& game, float ballRadius);
    // +1 when a ball entering goal `goalSide` (+1 = +Y) scores for `team` (0 blue attacks +Y), -1 own goal,
    // 0 when the team is unknown or there is no goal.
    static int goalRelation(int goalSide, int team);
    // Colour of the path for a goal-bound ball.
    Color goalColor(int goalSide, int team) const;
    // "45 km/h", "28 mph", "1250 uu/s" (no decimals): the unit names French players read in game.
    static std::string formatBallSpeed(float uus, SpeedUnit unit);
    static const char* unitLabel(SpeedUnit unit);
    // "Saut (A)", "Double saut (Croix)", "Aérien (A + B)" with the player's buttons in the chosen style, "" for
    // JumpKind::None.
    static std::string jumpHint(pr::JumpKind kind, const PlayerProfile& profile, KeyStyle style);
    // Goal-bound path label: "But pour toi", "Contre ton camp", or the goal's colour when the team is unknown.
    static std::string goalLabel(int goalSide, int team);
    // True when the path predicts a contact able to change the ball velocity sharply around `ticks` after its
    // start (a bounce, or any contact with a wall, a ramp or the ceiling; plain rolling on the floor does not
    // count). A sharp velocity change elsewhere comes from outside the model (another car).
    static bool pathExpectsImpulse(const pr::PredictedPath& path, int ticks);
    // RGB distance below kCloseColorDistance (alpha ignored).
    static bool colorsClose(const Color& a, const Color& b);
    // Colour of the first-touch zone drawn over a path of colour `pathBase`: the configured colour, or white when
    // it would blend into the path (for example an orange zone on a path going into the orange goal).
    Color zoneColorOver(const Color& pathBase) const;

private:
    // Saved settings of the previous version (config.cfg): turn the old default "on" off once (see migrateDefaults).
    void migrateDefaults(Actions& out);
    void invalidate();
    void updateFrozen(const Snapshot& s, float arenaRadius);
    void onForcedRecompute(const Snapshot& s, int elapsed, float dv, float dp);
    bool localCarNearBall(const Snapshot& s) const;
    void recompute(const TickContext& ctx);
    void startProbe();
    void updateProbe(const Snapshot& s, Actions& out);
    void recordProbe(float error, Actions& out);
    void resetAccuracy();
    void setHidden(bool hidden, const TickContext& ctx, Actions& out);
    void updateViewPath(const ViewContext& view, const DrawContext& ctx);
    void drawPath(const pr::PredictedPath& path, int nowTicks, bool withIntercept, int team, const ViewContext& view,
                  const DrawContext& ctx, DrawList& out) const;
    void drawPanel(const pr::PredictedPath& path, int nowTicks, bool withIntercept, int team, const ViewContext& view,
                   const DrawContext& ctx, DrawList& out) const;

    PredictionConfig cfg_;
    bool defaultsV2_ = false;     // migrateDefaults ran (saved)
    pr::CarReachModel reach_;
    pr::PredictedPath path_;      // tick-mode cache
    pr::PredictedPath viewPath_;  // draw-time cache (replays)
    pr::InterceptResult intercept_;

    int team_ = -1;
    bool metric_ = true;
    bool hidden_ = false;
    bool paused_ = false;
    bool forceRecompute_ = false;
    int64_t lastTickFrame_ = 0;
    int64_t lastAirborneFrame_ = -1000000;
    int drawFramesSinceTick_ = 0;
    int viewFrames_ = 0;
    int viewOffset_ = 0;
    bool lastDrawFromView_ = false;
    bool lastDrawReplay_ = false;  // the last view-mode draw was a replay (team unknown)
    // Hanging ball detection (training packs freeze the ball in the air until the player moves).
    bool hasPrevBall_ = false;
    Vec3 prevBallPos_;
    int64_t prevBallFrame_ = 0;
    int frozenTicks_ = 0;
    bool frozen_ = false;
    long long recomputes_ = 0;
    long long forcedRecomputes_ = 0;
    long long viewRecomputes_ = 0;

    struct Probe {
        bool active = false;
        int64_t startFrame = 0;
        int64_t targetFrame = 0;
        Vec3 predicted;
    };
    Probe probe_;
    int64_t probeBlockedUntil_ = 0;  // no new probe before this frame (after an outside disturbance)
    int accCount_ = 0;
    double accSum_ = 0.0;
    float accMax_ = 0.f;
    float accLast_ = 0.f;
    int accDisturbed_ = 0;
    std::array<float, kAccuracyHistory> accRecent_{};
    int accRecentCount_ = 0;
    int accRecentHead_ = 0;  // next write position
};

} // namespace rlt
