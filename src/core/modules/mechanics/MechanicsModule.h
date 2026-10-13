#pragma once
// Mécas mouvement : détection et notation automatiques des mécaniques de déplacement (speedflip, half-flip,
// flip cancel, wavedash, chain wavedash, zap dash, wall dash, fast aerial, stall, hélico, réception, powerslide,
// gestion du boost), mode focus et exercices en freeplay avec difficulté adaptative.
//
// Flow: onTick feeds the MovementCoach (detection hub) with each snapshot; every grade passes the family toggles
// and the map category (both skipped for the family the player focuses on or drills) and the focus filter, then
// becomes an AttemptResult (Actions::results), updates the stats and the HUD panel. Drills own the world-control
// activity: placement, frozen countdown, slow motion, auto-reset. The game speed is only restored while owning the
// activity (the engine drops it otherwise): a restore that could not be routed stays pending (see appliedSpeed_).

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Controls.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/modules/mechanics/Drills.h"
#include "core/modules/mechanics/MechStats.h"
#include "core/modules/mechanics/MechanicsConfig.h"
#include "core/modules/mechanics/MovementCoach.h"
#include "core/modules/mechanics/Presentation.h"

namespace rlt {

class MechanicsModule : public Module {
public:
    // ---- Plain read models for the UI (copy them under Engine::lock()) ----------------------------------
    struct Status {
        bool enabled = true;
        mech::Focus focus = mech::Focus::None;
        bool drillActive = false;
        std::string phase;     // French phase label
        std::string message;   // last drill message
        std::string goal;
        std::string variant;
        float timeLeft = 0.f;
        float level = 0.f;     // 0..1
        int drillAttempts = 0, drillSuccesses = 0;
        bool worldControl = false;
        float gameSpeed = 1.f; // game speed this module applies (1 when it does not control the world)
        // The activity was taken away while the game was slowed down: the restore may not have reached the game
        // (the UI offers the "restore_speed" command).
        bool speedRestorePending = false;
        int chainDashes = 0;
        int sessionAttempts = 0;
    };
    struct StatRow {
        mech::Focus focus = mech::Focus::None;
        std::string id, title, group, what, key;
        bool enabled = true;   // family toggle
        int attempts = 0, successes = 0;
        int sessionAttempts = 0, sessionSuccesses = 0;
        float average = 0.f, best = 0.f;
        bool hasPb = false;
        std::string pbLabel, pbText;
        std::string topErrorTip;
        std::vector<float> recent;
        float level = 0.f;
    };
    struct LastAttempt {
        bool valid = false;
        AttemptResult result;
        std::string variant;   // French
        std::string headline;  // top tip, or praise when clean
        std::vector<mech::MetricLine> lines;
        bool drillAttempt = false;
        float time = 0.f;      // game time of the attempt
    };

    const char* id() const override { return "mechanics"; }
    const char* title() const override { return "Mécas mouvement"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onProfileChanged(const PlayerProfile& profile) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void onActivityStopped() override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    // ---- Read accessors (UI, tests) ---------------------------------------------------------------------
    const MechanicsConfig& config() const { return cfg_; }
    Status status() const;
    std::vector<StatRow> statRows() const;
    const LastAttempt& lastAttempt() const { return last_; }
    mech::Focus focus() const { return focus_; }
    bool drillActive() const { return drill_.active; }
    mech::Focus drillFocus() const { return drill_.focus; }
    float appliedGameSpeed() const { return appliedSpeed_; }
    bool speedRestorePending() const { return pendingSpeedRestore_; }
    float level(mech::Focus f) const;
    const mech::MechStats& stats() const { return stats_; }
    const mech::DetectConfig& detectConfig() const { return coach_.cfg; }
    const mech::DrillSetup& drillSetup() const { return setup_; }

private:
    enum class Phase : int { Setup, Countdown, Running, Showing };
    struct Drill {
        bool active = false;
        mech::Focus focus = mech::Focus::None;
        Phase phase = Phase::Setup;
        float phaseEnd = 0.f;     // countdown end (game time)
        float startTime = 0.f;    // GO / start of the attempt
        float showUntil = 0.f;
        float touchTime = -1.f;   // first ball touch of the attempt
        int attempt = 0;          // index (alternates sides / spots)
        int attempts = 0, successes = 0;
        int floorTicks = 0;
        std::string message;
    };

    bool familyEnabled(mech::Focus f) const;
    // The player asked for this family (focus or running drill): graded even when its toggle is off.
    bool trained(mech::Focus f) const { return f != mech::Focus::None && (f == focus_ || (drill_.active && f == drill_.focus)); }
    static bool categoryAllows(mech::Mech m, MapCategory c);
    void applyDetectConfig();
    SpeedUnit speedUnit() const { return resolveUnit(SpeedUnit::Auto, profile_.metric); }

    void setFocus(mech::Focus f, const TickContext& ctx, Actions& out);
    void startDrill(mech::Focus f, const TickContext& ctx, Actions& out);
    void stopWorld(const TickContext& ctx, Actions& out);
    void runWorld(const TickContext& ctx, Actions& out);
    void beginAttempt(const TickContext& ctx, Actions& out);
    void afterDetection(const TickContext& ctx, Actions& out);
    // Emits what the detectors still hold (kickoff speedflip waiting for the ball, chain in progress) and the
    // drill grade waiting for a touch, before a reset drops them.
    void flushBeforeReset(const TickContext& ctx, Actions& out);
    // Drill grade that waited for a ball touch that never came: graded as a miss.
    void releaseHeld(const TickContext& ctx, Actions& out);
    // Routes a game speed (only called while owning the activity with world control allowed).
    void emitSpeed(float speed, Actions& out);
    void restoreSpeed(const TickContext& ctx, Actions& out);
    void handleGrade(mech::MechGrade& g, const TickContext& ctx, Actions& out);
    void emitAttempt(const mech::MechGrade& g, const TickContext& ctx, Actions& out, bool drillAttempt);
    void toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const;

    MechanicsConfig cfg_;
    PlayerProfile profile_;
    KeyStyle keyStyle_ = KeyStyle::Auto;  // Xbox / PlayStation labels (profile module setting)
    float deadzone_ = mech::k::kDodgeDeadzoneDefault;
    mech::MovementCoach coach_;
    std::vector<mech::MechGrade> grades_;  // reused every tick
    mech::MechStats stats_;
    std::array<Staircase, mech::kFocusCount> levels_;
    mech::Focus focus_ = mech::Focus::None;
    Rng rng_{0x6d65636861ULL};

    Drill drill_;
    mech::DrillSetup setup_;
    std::optional<mech::MechGrade> held_;  // fast aerial drill: waits for the ball touch
    float heldTime_ = 0.f;

    bool focusSpeedOwned_ = false;   // activity held only for the focus slow motion
    bool focusSpeedWanted_ = false;  // slow motion turned on while a focus runs: ask for the activity once
    bool focusSpeedLost_ = false;    // another module took the focus slow motion: a slider change does not take it back
    bool focusFromDrill_ = false;    // focus_ was set by the running drill (cleared when the drill stops)
    // Game speed bookkeeping. The engine drops world control (game speed included) from a module that does not
    // own the activity, so a restore only counts once it was routed as the owner:
    //   appliedSpeed_        last speed routed as the owner (what the game runs at because of this module);
    //   pendingSpeedRestore_ the activity was lost while appliedSpeed_ != 1: the next activity of this module
    //                        sends its speed first (1 for a normal drill), and the UI offers restore_speed.
    float appliedSpeed_ = 1.f;
    bool pendingRelease_ = false;    // release the activity on the next tick (after the speed restore was routed)
    bool pendingSpeedRestore_ = false;
    int resetTicks_ = 0;             // ticks to skip after a placement (teleport)
    bool worldControl_ = false;
    std::string mapKey_;
    float nowTime_ = 0.f;
    int resultsSinceSave_ = 0;

    LastAttempt last_;
};

} // namespace rlt
