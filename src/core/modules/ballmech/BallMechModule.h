#pragma once
// Mécas balle: automatic detection and grading of ball / aerial mechanics (flip reset and its use, ceiling shot,
// flicks incl. musty / breezi / reverse, double tap, air dribble, air roll shots, aerial accuracy, pinches, power
// shots), a live HUD panel, freeplay drills with adaptive difficulty, and per-mechanic statistics.
//
// Data flow: the engine resolves touches 2 ticks late, so the detection pipeline processes each snapshot 2 ticks
// after it was taken (see bm::Pipeline). Released grades become AttemptResults (module "ballmech", skill
// "ball.<id>"). Flip resets are counted from the game event (detail "sdk") and from our own detector, de-duplicated
// within 0.3 s; a reset found only by the detector is broadcast as GameEvent{FlipReset, "detecteur"}.
//
// Drills (see Drills.h, Parcours.h): every attempt starts with a get-ready hold (car and ball re-placed with zero
// velocity on every tick; the first attempt waits for the throttle, then counts 3, 2, 1). A drill starts either with
// the mechanic alone (short start) or with the full sequence ("parcours complet": shared step tracker, hanging ball,
// shared "Ramassage" start whose failures feed their own staircase).
//
// Commands ("rlt ballmech <cmd>"): drill <mech> [variante] [court|parcours], stop, next, level <mech> <0-100>,
// mode <mech> <court|parcours>, variant <flick>, reset_stats, reset_session.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Controls.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/modules/ballmech/BallMechConfig.h"

namespace rlt {

namespace bm {
struct MechGrade;
} // namespace bm

class BallMechModule : public Module {
public:
    BallMechModule();
    ~BallMechModule() override;

    const char* id() const override { return "ballmech"; }
    const char* title() const override { return "Mécas balle"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void onActivityStopped() override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    const BallMechConfig& config() const { return cfg_; }

    // ---- Read accessors for the UI (call under the engine lock, then copy) --------------------------------------
    struct MechView {
        std::string id, title, what, key, drillHint;  // key / hint keep the {jump}-style placeholders
        std::vector<std::string> steps;               // what to do, in order (placeholders kept)
        std::string difficulty;                       // "Débutant", "Intermédiaire", "Avancé"
        int order = 0;                                // position in bm::learningOrder (easiest first)
        std::string progression;                      // how the drill gets harder, plain words
        int mode = 0;                                 // drill start: 0 short, 1 full sequence (bm::DrillMode)
        std::string modeShort, modeFull;              // their names ("Départ au plafond", "Parcours complet")
        int attempts = 0, successes = 0;
        int sessionAttempts = 0, sessionSuccesses = 0;
        float average = 0.f, best = 0.f;              // 0..100
        float drillLevel = 0.f;                       // 0..1
        int levelNumber = 1;                          // the same level as the player sees it, 1..10
        int drillAttempts = 0;
        std::vector<float> recent;                    // last scores, oldest first
        bool detectionEnabled = true;
    };
    struct DrillView {
        bool active = false;
        std::string mechId, title, phase, label, hint;
        std::string action;                           // short step list of the current level (the HUD line)
        std::string tip;                              // main advice after the last attempt ("" = none)
        float level = 0.f;
        int levelNumber = 1;
        int attempts = 0, successes = 0;              // this drill session
        float timeLeft = 0.f;
        std::string lastVerdict;
        bool lastSuccess = false;
        bool full = false;                            // full sequence ("parcours complet")
        std::string modeLabel;
        std::vector<std::string> steps;               // the steps of the full sequence, "1/5 Va au mur : OK"...
        int currentStep = -1;                         // index in `steps` of the step to do now (-1 = none)
        std::string missing;                          // "Il manquait : ..." after a failed step
        bool waitingThrottle = false;                 // first attempt: held until the player presses the throttle
    };
    struct UiState {
        bool enabled = true;
        bool detecting = false;                       // the current game mode is analysed
        int sessionResets = 0, totalResets = 0;
        float bestAirDribble = 0.f, hardestFlick = 0.f, bestPinch = 0.f, bestPowerShot = 0.f;  // s, uu/s
        bool resetAvailable = false;
        bool hasLast = false;
        AttemptResult last;
        std::vector<MechView> mechs;
        DrillView drill;
        std::string flickVariant;
        KeyStyle keyStyle = KeyStyle::Auto;
        int successScore = 50;                        // settings used by the drill criteria (tab details)
        float pinchSpeed = 4000.f;
        float timeoutScale = 1.f;
        int pickupLevel = 1;                          // niveau of the shared "Ramassage" start (1..10)
    };
    UiState uiState() const;

    // ---- Test / introspection helpers ---------------------------------------------------------------------------
    int sessionResets() const;
    int totalResets() const;
    bool drillActive() const { return drill_.phase != DrillPhase::Off; }
    const AttemptResult* lastResult() const { return hasLast_ ? &last_ : nullptr; }
    float drillLevel(const std::string& mechId) const;
    int drillMode(const std::string& mechId) const;  // 0 short, 1 full sequence
    float pickupLevel() const;                       // 0..1, the shared "Ramassage" staircase

private:
    enum class DrillPhase { Off, Ready, Live, Result };
    enum class ReadyKind { First, Auto };  // first attempt: wait for the throttle, then 3 s; then a short hold
    struct Drill {
        DrillPhase phase = DrillPhase::Off;
        int mech = 0;                 // bm::Mech
        bool full = false;            // this attempt runs the full sequence
        float phaseStart = 0.f;       // game time
        int64_t launchFrame = 0;
        int burnStep = -1;            // scripted double jump (flip reset drill), -1 = none
        bool airborneSeen = false;
        float completeAt = -1.f;      // full sequence completed on its checks, waiting for the detector's grade
        bool flickLanded = false;     // breezi: the carried jump already ended on the floor
        bool activity = false;        // the player did something during the attempt
        float failAt = -1.f;          // pending fail (landing / ball lost) after a grace delay
        std::string failCode;
        int idleTimeouts = 0;
        int attempts = 0, successes = 0;
        std::string lastVerdict;
        bool lastSuccess = false;
        bool hasResult = false;       // `result` holds the last attempt of this drill
        AttemptResult result;
        bool resolved = false;
    };

    bool analysing(const GameContext& game) const;
    void syncPipeline(const TickContext& ctx);
    void handleAcquisition(int64_t frame, int source, Actions& out);
    bool countReset(int64_t frame);
    void handleGrade(bm::MechGrade& g, const TickContext& ctx, Actions& out);
    AttemptResult makeResult(const bm::MechGrade& g, int mech, bool success, const std::vector<std::string>& extra,
                             const PlayerProfile& profile) const;
    void publish(AttemptResult r, int mech, const bm::MechGrade* g, const TickContext& ctx, Actions& out);

    // Drills
    bool startDrill(int mech, const TickContext& ctx, Actions& out);
    void stopDrill(Actions* out, bool release);
    void drillTick(const TickContext& ctx, Actions& out);
    void beginAttempt(const TickContext& ctx, ReadyKind kind = ReadyKind::Auto);
    void launch(const TickContext& ctx, Actions& out);
    void fullTick(const TickContext& ctx, Actions& out);
    void stepFailed(const TickContext& ctx, Actions& out);
    void checksSucceeded(const TickContext& ctx, Actions& out);
    void tornadoLanded(const TickContext& ctx, Actions& out);
    bool idleTimeout(const TickContext& ctx, Actions& out);  // true when the attempt was restarted or the drill paused
    void resolveDrill(bool success, const TickContext& ctx);
    void failDrill(const char* code, const TickContext& ctx, Actions& out, bool withPrimary = true);
    float drillTimeout() const;
    const Tip* drillTip() const;      // main advice after the last drill attempt (nullptr = none)
    std::string drillActionNow() const;

    struct Impl;  // detection pipeline, statistics, current drill setup, scratch buffers (BallMechModule.cpp)

    BallMechConfig cfg_;
    KeyStyle keyStyle_ = KeyStyle::Auto;  // player's button style (Settings::profile.buttonStyle)
    std::unique_ptr<Impl> impl_;
    std::array<Staircase, 11> stairs_;   // one adaptive level per mechanic (bm::kMechCount)
    std::array<bool, 11> levelChosen_{};  // level set with the - / + buttons ("level" command): kept even unplayed
    std::array<int, 11> modes_{};         // drill start per mechanic (bm::DrillMode)
    std::array<bool, 11> modeChosen_{};   // start chosen by the player (saved; otherwise the default start)
    bool gameSpeedSet_ = false;           // a drill slowed the game down: put it back at the next launch
    bool touchThisTick_ = false;          // onTouch since the last tick (full sequence checks)
    bool resetThisTick_ = false;          // flip reset game event since the last tick
    Drill drill_;
    std::string flickVariant_;
    Rng rng_;
    uint64_t rngCounter_ = 0;

    // Flip reset ledger (counted resets, de-duplicated within 0.3 s).
    std::array<int64_t, 8> ledger_{};
    int ledgerCount_ = 0;
    int ledgerHead_ = 0;

    // HUD state
    bool hasLast_ = false;
    AttemptResult last_;
    int lastMech_ = 0;
    float lastShownAt_ = -1e9f;     // game time the last result was produced
    bool lastHasPath_ = false;
    bool lastPathOnTarget_ = false;
    int lastPathCount_ = 0;
    std::array<Vec3, 24> lastPath_{};
    float lastTickTime_ = 0.f;
    bool resetAvailable_ = false;
    bool detecting_ = false;
};

} // namespace rlt
