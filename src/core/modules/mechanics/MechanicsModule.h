#pragma once
// Mécas mouvement : détection et notation automatiques des mécaniques de déplacement (speedflip, half-flip,
// flip cancel, wavedash, chain wavedash, zap dash, wall dash, fast aerial, stall, hélico, réception, powerslide,
// gestion du boost), mode focus et exercices en freeplay avec difficulté adaptative.
//
// Flow: onTick feeds the MovementCoach (detection hub) with each snapshot; every grade passes the family toggles
// and the map category (both skipped for the family the player focuses on or drills) and the focus filter, then
// becomes an AttemptResult (Actions::results), updates the stats and the HUD panel. Drills own the world-control
// activity: placement, get-ready phase, slow motion, auto-reset. Every attempt starts held: car (and ball) put back at
// the start pose with zero velocity on EVERY tick (gravity would make an air start fall during the countdown), on the
// first attempt until the player presses the throttle and then 3 s, after an automatic reset for the drill's own
// countdown (2 s, 1.5 s for the short drills); on GO they get their launch velocity. A drill attempt counts when it
// meets the pass conditions of its level (Drills.h, the first one missed gives the single tip), the score the level
// asks for and, at the top levels, the detector's own success; the result moves the family's Staircase (new player:
// level 1/10). A time limit without a result is never silent: it records a failed attempt with the tip of the first
// step that was missing. The "parcours complet" of a family (drill command "drill <id> full") grades every step with
// the shared StepTracker and has its own staircase.
// The HUD panel keeps three main lines (result, one tip, the action to do now), the rest is detail. On its own
// normal stop the module routes the speed
// restore as the owner, then gives the activity back; when the activity ends any other way (stop button, handover,
// map unloaded, world control lost) the engine puts the game speed back to 1 itself.
// Taking the activity for the focus slow motion or for "restore_speed" never stops another module's exercise: the
// wish waits until that exercise ends (TickContext::otherActivityRunning). Only a drill started by the player does.
// Demo ("demo <id>", Demos.h): the module takes the activity, holds the car at the demo's start pose for a short
// countdown, sets the slow motion, then drives the car with Actions::overrideInputs on every tick of the timeline and
// shows the current step on the HUD. Detection is off during the demo (and until the car is back on its wheels), so
// the demo never counts as the player's attempt. It ends on its own (speed restored, activity released, "Rejouer la
// démo" / "À toi" offered), or on Reset, "stop", the engine's stop or a map change.
// Saved defaults: BakkesMod keeps every cvar in config.cfg, so a default changed for a new version is moved once on
// the first tick (flag "mechanics.defaults_v2" in the Store): only a value still equal to the old default changes.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Controls.h"
#include "core/common/HangingBall.h"
#include "core/common/Hold.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/common/Steps.h"
#include "core/modules/mechanics/Demos.h"
#include "core/modules/mechanics/Drills.h"
#include "core/modules/mechanics/FullSequence.h"
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
        std::string goal;      // plain goal of the level ("Étape 1 sur 3 : ...")
        std::string variant;
        float timeLeft = 0.f;
        float level = 0.f;     // 0..1
        int levelNumber = 1;   // 1..10, as shown to the player
        std::vector<std::string> steps;   // what to do, in order
        int currentStep = -1;             // live: index of the next action in `steps` (-1 when no attempt runs)
        std::vector<std::string> checks;  // pass conditions of the level, with their numbers
        int drillAttempts = 0, drillSuccesses = 0;
        bool full = false;                // "parcours complet" running
        bool waitingForThrottle = false;  // get-ready phase: the car is held until the player presses the throttle
        int failStreak = 0;               // drill attempts failed in a row (3+: the slow motion is suggested)
        bool worldControl = false;
        float gameSpeed = 1.f; // game speed this module applies (1 when it does not control the world)
        // A focus slow motion or a "restore_speed" waits for another module's exercise to end.
        bool speedWaiting = false;
        int chainDashes = 0;
        int sessionAttempts = 0;
        // Automatic demo ("Voir la démo").
        bool demoActive = false;
        bool demoEnded = false;           // the last demo played to its end a moment ago: "Rejouer" / "À toi"
        mech::Focus demoFocus = mech::Focus::None;  // running demo, or the one that just ended
        std::string demoPhase;            // "Mise en place", "Démo en cours"
        std::string demoVariant;
        std::vector<std::string> demoSteps;
        int demoStep = -1;                // live: index in demoSteps (-1 before GO)
    };
    struct StatRow {
        mech::Focus focus = mech::Focus::None;
        std::string id, title, group, what, key;
        mech::Tier tier = mech::Tier::Beginner;
        std::string memo, tech, progression;
        std::vector<std::string> steps;
        // Tutorial with the player's own buttons (MechCatalog.h tutorial()).
        std::string intro, full, level1;
        struct TutoStep {
            std::string action, why, ok;
        };
        std::vector<TutoStep> tutorial;
        std::vector<std::string> panel;  // HUD lines of the drill, in order
        float fullLevel = 0.f;
        int fullLevelNumber = 1;
        bool enabled = true;   // family toggle
        int attempts = 0, successes = 0;
        int sessionAttempts = 0, sessionSuccesses = 0;
        float average = 0.f, best = 0.f;
        bool hasPb = false;
        std::string pbLabel, pbText;
        std::string topErrorTip;
        std::vector<float> recent;
        float level = 0.f;
        int levelNumber = 1;   // 1..10
        // What a drill success needs (with the numbers) at level 1, level 10 and the player's level.
        std::vector<std::string> checksFirst, checksLast, checksNow;
        bool hasDemo = false;  // "Voir la démo" available (Demos.h)
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
    bool drillFull() const { return drill_.active && drill_.full; }
    mech::Focus drillFocus() const { return drill_.focus; }
    // Staircase level of the "parcours complet" of a family.
    float fullLevel(mech::Focus f) const;
    const StepTracker& fullTracker() const { return tracker_; }
    float appliedGameSpeed() const { return appliedSpeed_; }
    bool speedWaiting() const { return focusSpeedWanted_ || restoreWanted_; }
    float level(mech::Focus f) const;
    const mech::MechStats& stats() const { return stats_; }
    const mech::DetectConfig& detectConfig() const { return coach_.cfg; }
    const mech::DrillSetup& drillSetup() const { return setup_; }
    bool kickoffLive() const { return coach_.kickoff().live(); }
    bool demoActive() const { return demo_.active; }
    mech::Focus demoFocus() const { return demo_.focus; }
    // Script of the running demo (built when its countdown starts).
    const mech::DemoScript& demoScript() const { return demo_.script; }
    // Ticks since the demo's GO (-1 before GO or without a demo).
    int demoTick() const { return demo_.active && demo_.phase == DemoPhase::Playing ? demo_.tick : -1; }

private:
    // Countdown = get-ready phase (car held: waiting for the throttle, then the countdown).
    enum class Phase : int { Setup, Countdown, Running, Showing };
    // How the next attempt starts: First = wait for the throttle then 3 s (drill start, or after an attempt where the
    // player did nothing), Auto = the drill's own countdown, Manual = retry or reset by the player (3 s).
    enum class Start : int { First, Auto, Manual };
    enum class HeldFor : int { None, Touch, Landing };
    enum class DemoPhase : int { Setup, Countdown, Playing };
    struct Demo {
        bool active = false;
        mech::Focus focus = mech::Focus::None;
        DemoPhase phase = DemoPhase::Setup;
        mech::DemoScript script;
        int64_t goFrame = 0;
        int tick = 0;  // ticks since GO
    };
    struct Drill {
        bool active = false;
        bool full = false;        // "parcours complet"
        mech::Focus focus = mech::Focus::None;
        Phase phase = Phase::Setup;
        Start nextStart = Start::First;
        float startTime = 0.f;    // GO / start of the attempt
        int64_t goFrame = 0;
        float showUntil = 0.f;
        float touchTime = -1.f;   // first ball touch of the attempt (or the reach of a near-miss level)
        float touchBallZ = 0.f;   // ball height at that touch
        float minGap = 1e9f;      // closest hitbox to ball distance of the attempt (uu)
        bool reached = false;     // near-miss level: the car passed close enough to the ball
        bool anyInput = false;    // the player touched the controls during the attempt
        int attempt = 0;          // index (alternates sides / spots)
        int attempts = 0, successes = 0;
        int failStreak = 0;
        int floorTicks = 0;
        int runOk = 0;            // wall dash run: successful wall dashes so far
        uint32_t cues = 0;        // mech::Cue bits seen since the start of the attempt (live step)
        bool timedOut = false;    // the attempt ended at the time limit without a result
        bool newStage = false;    // this attempt starts a new step of the progression (until its result)
        std::string missing;      // step that was missing (timeout / failed step), for the HUD
        std::string message;
    };

    bool familyEnabled(mech::Focus f) const;
    // The player asked for this family (focus or running drill): graded even when its toggle is off.
    bool trained(mech::Focus f) const { return f != mech::Focus::None && (f == focus_ || (drill_.active && f == drill_.focus)); }
    static bool categoryAllows(mech::Mech m, MapCategory c);
    void applyDetectConfig();
    SpeedUnit speedUnit() const { return resolveUnit(SpeedUnit::Auto, profile_.metric); }

    void setFocus(mech::Focus f, const TickContext& ctx, Actions& out);
    void startDrill(mech::Focus f, const TickContext& ctx, Actions& out, bool full = false);
    void stopWorld(const TickContext& ctx, Actions& out);
    void runWorld(const TickContext& ctx, Actions& out);
    void beginAttempt(const TickContext& ctx, Actions& out, Start start);
    // GO tick: launch velocities, no hold (the player's inputs already count), detectors and step tracker started.
    void launch(const TickContext& ctx, Actions& out);
    // Running tick before the detectors: hanging ball, reach of the ball, inputs seen.
    void runAttempt(const TickContext& ctx, Actions& out);
    // Running tick after the detectors: step tracker of the "parcours complet".
    void runFull(const TickContext& ctx, Actions& out);
    void finishFull(const TickContext& ctx, Actions& out);
    // A drill attempt ended without a grade (time limit, car fell off the wall...): one failed attempt, recorded, with
    // the tip of `code` (fix 5).
    void recordMiss(const char* code, const TickContext& ctx, Actions& out);
    // The drill attempt is over: staircase, counters, message, Showing phase.
    void closeDrillAttempt(bool ok, float t);
    Staircase& staircase(mech::Focus f, bool full);
    float levelFor(mech::Focus f, bool full) const { return full ? fullLevel(f) : level(f); }
    // Error code when nothing happened before the time limit, by family.
    static const char* defaultMiss(mech::Focus f);
    // Live progress of the attempt (which step the player has reached).
    void trackCues(const Snapshot& s);
    bool cueSeen(mech::Cue c) const { return (drill_.cues >> static_cast<unsigned>(c)) & 1u; }
    // Index of the next action of the running attempt in setup_.steps (steps.size() when all are done).
    int currentStep() const;
    void afterDetection(const TickContext& ctx, Actions& out);
    // Emits what the detectors still hold (kickoff speedflip waiting for the ball, chain in progress) and the
    // drill grade waiting for a touch, before a reset drops them.
    void flushBeforeReset(const TickContext& ctx, Actions& out);
    // Drill grade that waited for a ball touch that never came: graded as a miss.
    void releaseHeld(const TickContext& ctx, Actions& out);
    // Routes a game speed (only called while owning the activity with world control allowed).
    void emitSpeed(float speed, Actions& out);
    // Takes the activity for the focus slow motion, or remembers the wish while another module's exercise runs.
    void takeFocusSpeed(const TickContext& ctx, Actions& out);
    // "restore_speed": same rule (never stops another module's exercise, applied once it ends).
    void restoreSpeed(const TickContext& ctx, Actions& out);
    // Demo: start (command), one tick of it (owner, world control allowed), normal end, state dropped on any stop.
    void startDemo(mech::Focus f, const TickContext& ctx, Actions& out);
    void runDemo(const TickContext& ctx, Actions& out);
    void finishDemo(const TickContext& ctx, Actions& out);
    void clearDemo();
    bool showDemoEnd(float now) const;
    void drawDemo(float now, DrawPanel& p) const;
    // One-time move of the previous version's saved defaults (flag "mechanics.defaults_v2").
    void migrateDefaults(Actions& out);
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
    // Adaptive level of every family: a new player starts at the lowest level (shown as 1/10).
    static Staircase freshLevel() { return Staircase(0.75f, 0.03f, 0.f); }
    static std::array<Staircase, mech::kFocusCount> freshLevels() {
        std::array<Staircase, mech::kFocusCount> a;
        a.fill(freshLevel());
        return a;
    }
    std::array<Staircase, mech::kFocusCount> levels_ = freshLevels();
    std::array<Staircase, mech::kFocusCount> fullLevels_ = freshLevels();  // "parcours complet" of each family
    mech::Focus focus_ = mech::Focus::None;
    Rng rng_{0x6d65636861ULL};

    Drill drill_;
    mech::DrillSetup setup_;
    ReadyPhase ready_;                     // get-ready phase of the attempt
    HoldPose hold_;                        // start pose held during it
    HangingBall hanging_;                  // fast aerial: the ball waiting in the air
    StepTracker tracker_;                  // "parcours complet"
    std::shared_ptr<mech::FullLive> live_; // what its step checks read
    Snapshot prevSnap_;
    bool hasPrevSnap_ = false;
    std::optional<mech::MechGrade> held_;  // drill grade waiting for the ball touch or the landing
    HeldFor heldFor_ = HeldFor::None;
    float heldTime_ = 0.f;

    bool focusSpeedOwned_ = false;   // activity held only for the focus slow motion
    // Slow motion asked for the focus (slider change or focus command) without owning the activity: taken on the
    // next tick, or once another module's exercise ends (never stolen from it). Dropped on a map change.
    bool focusSpeedWanted_ = false;
    bool restoreWanted_ = false;     // "restore_speed" waiting for another module's exercise to end
    bool focusFromDrill_ = false;    // focus_ was set by the running drill (cleared when the drill stops)
    // Last speed routed as the owner (what the game runs at because of this module). Back to 1 whenever the activity
    // ends: restored by this module on its normal stop, by the engine otherwise.
    float appliedSpeed_ = 1.f;
    bool pendingRelease_ = false;    // release the activity on the next tick (after the speed restore was routed)
    int resetTicks_ = 0;             // ticks to skip after a placement (teleport)
    bool worldControl_ = false;
    std::string mapKey_;
    float nowTime_ = 0.f;
    int resultsSinceSave_ = 0;

    LastAttempt last_;

    Demo demo_;
    mech::DemoPilot pilot_;
    mech::Focus demoDone_ = mech::Focus::None;  // last demo that played to its end ("Rejouer" / "À toi")
    float demoDoneTime_ = 0.f;
    bool quietUntilGrounded_ = false;  // after a demo: no detection until the car is back on its wheels
    bool defaultsV2_ = false;          // the one-time move of the old saved defaults ran
};

} // namespace rlt
