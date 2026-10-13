#pragma once
// Affichage: the HUD basics of First Touch Trainer (AkumaRL v1.0.5) at full parity, improved.
//  - speed differential car/ball (XY, XYZ, XY + Z, closing speed), units, editable colour zones;
//  - session timer (game time from physics ticks, h:mm:ss, resets with the map or on command);
//  - touch zone: a small circle drawn ON the ball surface (static / velocity driven / keep it up / auto), smoothed,
//    hidden behind the ball, also in replays;
//  - centre sphere;
//  - training time statistics (total, per day, sessions) persisted under "overlay.".
// Analysis and display only: the module never requests the activity nor moves the ball or the car.
//
// Commands ("rlt overlay <cmd>"): speed, reset_timer, preset_original, reset_stats.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/common/Units.h"
#include "core/modules/overlay/OverlayConfig.h"
#include "core/modules/overlay/OverlayMath.h"

namespace rlt {

// Training time recorded for one local date ("YYYY-MM-DD"), as shown by the UI. The module counts whole physics ticks
// internally (exact over hundreds of hours); `seconds` is only the converted view.
struct OverlayDay {
    std::string date;
    float seconds = 0.f;
};

// Plain copy of the module state for the UI (read under the engine lock, then released).
struct OverlayStatus {
    // Speed differential, as drawn on the last frame.
    bool speedAvailable = false;     // a local car and the ball exist (never in replays)
    float diff = 0.f;                // uu/s, deadband applied
    float zDiff = 0.f;               // XY + Z mode only
    bool hasZ = false;
    int zone = 0;                    // ovl::SpeedZone
    int mode = 0;                    // ovl::SpeedMode
    SpeedUnit unit = SpeedUnit::Kph; // resolved unit (never Auto)
    bool lastTouchAvailable = false;
    float lastTouchDiff = 0.f;
    int lastTouchZone = 0;

    // Session timer and training time.
    float sessionSeconds = 0.f;
    float todaySeconds = 0.f;
    float totalSeconds = 0.f;
    int sessions = 0;                // finished sessions of at least 30 s
    float longestSession = 0.f;      // includes the running session

    // Touch zone and sphere.
    bool ballValid = false;
    bool ballInAir = false;
    bool carBelowBall = false;
    bool carKnown = false;           // false in replays without a local car: the "car below" test is skipped
    int effectiveBehavior = 0;       // ovl::ZoneBehavior after resolving Auto
    float tiltDeg = 0.f;             // current (smoothed) tilt of the zone from straight down
    bool zoneVisible = false;
    bool sphereVisible = false;
    bool replay = false;
};

class OverlayModule : public Module {
public:
    // Settings sections (French UI titles) and the cvars the tab needs by name.
    static constexpr const char* kSectionActivation = "Activation";
    static constexpr const char* kSectionSpeed = "Écart de vitesse";
    static constexpr const char* kSectionTimer = "Chrono de session";
    static constexpr const char* kSectionZone = "Cercle de contact";
    static constexpr const char* kSectionSphere = "Sphère centrale";
    static constexpr const char* kSectionColors = "Couleurs des zones";
    static constexpr const char* kCvarZoneColor = "rlt_ov_zone_color";
    static constexpr const char* kCvarZoneMatch = "rlt_ov_zone_match";
    static constexpr const char* kCvarSphereColor = "rlt_ov_sphere_color";
    static constexpr const char* kCvarSphereMatch = "rlt_ov_sphere_match";

    static constexpr int kMaxDays = 60;                 // persisted per-day history
    static constexpr float kMinCountedSession = 30.f;   // shorter sessions are not counted as sessions

    OverlayModule();

    const char* id() const override { return "overlay"; }
    const char* title() const override { return "Affichage"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    // ---- Read accessors (UI, tests) --------------------------------------------------------------------------
    const OverlayConfig& config() const { return cfg_; }
    OverlayStatus status() const;
    float sessionSeconds() const { return ticksToSeconds(sessionTicks_); }
    float totalSeconds() const { return ticksToSeconds(totalTicks_); }
    // The newest `maxCount` recorded days, in chronological order (oldest first).
    std::vector<OverlayDay> recentDays(size_t maxCount) const;
    // Current smoothed zone direction (unit vector from the ball centre).
    Vec3 zoneDirection() const { return dir_; }

    // Text of the "speed" command (also used by the UI preview). French, accents kept (the glue folds them).
    std::string speedChatText(const TickContext& ctx) const;

    // Tests: replaces the wall clock (seconds) used to smooth the zone in replays, where no physics tick runs.
    void setClock(std::function<double()> clock) { clock_ = std::move(clock); }

    // Time accounting helpers (public for tests): whole ticks <-> seconds, and the locale independent text used to
    // persist seconds ("1234.567", exact to the tick) with its parser (also reads older "%.6g" / "%.1f" values).
    static float ticksToSeconds(int64_t ticks) { return static_cast<float>(static_cast<double>(ticks) / kTickRate); }
    static int64_t secondsToTicks(double seconds);
    static std::string secondsText(int64_t ticks);
    static bool parseSecondsText(const std::string& text, int64_t& ticks);

private:
    struct Kinematics {
        Vec3 carPos, carVel, ballPos, ballVel;
    };
    struct DayTicks {
        std::string date;
        int64_t ticks = 0;
    };

    bool speedKinematics(const ViewContext& view, const DrawContext& ctx, Kinematics& k) const;
    float frameDt(const DrawContext& ctx);
    void drawSpeed(const ViewContext& view, const ovl::SpeedDiff& diff, ovl::SpeedZone zone, SpeedUnit unit,
                   float scale, DrawList& out) const;
    void drawTimer(const ViewContext& view, float scale, DrawList& out) const;
    void drawBallOverlays(const ViewContext& view, const DrawContext& ctx, float dt, bool haveSpeed,
                          ovl::SpeedZone zone, DrawList& out);
    void addTrainingTime(int64_t ticks, const std::string& date);
    void switchDay(const std::string& date);
    void noteDate(const std::string& date);
    int findDay(const std::string& date) const;
    void endSession(Actions& out);
    void resetTransient();

    OverlayConfig cfg_;
    OverlayStatus status_;

    // Session timer (game time from ticks). Counted in whole physics ticks: a float accumulator of 1/120 s steps
    // drifts after an hour and stops moving past ~72 h.
    int64_t sessionTicks_ = 0;
    int64_t lastTickFrame_ = -1;
    bool sessionKnown_ = false;
    GameMode sessionMode_ = GameMode::Menu;
    std::string sessionMap_;

    // Training time statistics (persisted, in ticks).
    int64_t totalTicks_ = 0;
    int sessions_ = 0;
    int64_t longestTicks_ = 0;
    std::vector<DayTicks> days_;     // chronological, unique dates, at most kMaxDays
    std::string todayDate_;          // date `todayIdx_` was resolved for (addTrainingTime cache)
    int todayIdx_ = -1;
    std::string knownDate_;          // newest valid local date seen in any callback ("today" for the UI)

    // Last touch: car/ball state just before the newest contact, re-evaluated with the current mode.
    bool hasLastTouch_ = false;
    Kinematics lastTouch_;

    // Touch zone smoothing.
    bool hasDir_ = false;
    bool snapNext_ = false;
    Vec3 dir_{0.f, 0.f, -1.f};
    bool hasLastBall_ = false;
    Vec3 lastBallPos_;
    float lastDrawSnapTime_ = -1.f;
    double lastClock_ = -1.0;
    std::function<double()> clock_;
};

} // namespace rlt
