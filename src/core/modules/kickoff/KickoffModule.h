#pragma once
// Kickoff: kickoff drill in freeplay (5 soccar spawns, 3-2-1-GO countdown with the car frozen, ghost opponent from
// the mirrored spawn, 50/50 resolution, grade 0..100 with French tips, split times, per-spawn stats and personal
// bests) and passive analysis of kickoffs outside the drill (freeplay reset, offline matches).
//
// Drill state machine (one rep):
//   Placing (1 tick: place car + ball, zero inputs, limited boost rules)
//   -> Countdown (freeze, re-pin, boost set to the start value, 33 by default, game's 3/2/1 messages; every boost
//      pad respawned once the car has been on its spawn for kKoPadResetDelayTicks, see placeRep)
//   -> Live (GO: release, record until the touch or the timeout, ghost hit) -> Done (result shown, then next rep).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/modules/kickoff/KickoffConfig.h"
#include "core/modules/kickoff/KickoffData.h"
#include "core/modules/kickoff/KickoffGhost.h"
#include "core/modules/kickoff/KickoffGrading.h"
#include "core/modules/kickoff/KickoffRecorder.h"
#include "core/modules/kickoff/KickoffStats.h"

namespace rlt {

enum class KickoffPhase : int { Idle = 0, Placing = 1, Countdown = 2, Live = 3, Done = 4 };
enum class KickoffMode : int { Chosen = 0, Random = 1, Sequence = 2, Weakness = 3 };
enum class KickoffGhostMode : int {
    Off = 0,
    Beginner = 1,
    Standard = 2,
    Pro = 3,
    Elite = 4,
    PersonalBest = 5,
    Adaptive = 6
};
constexpr int kKoGhostModeCount = 7;
// Adaptive ghost: level 0 = Débutant time .. 1 = Élite (kickoffBenchmarkAt). A player who never raced it starts at
// the beginner time; each win adds 0.04, each loss removes 0.06 (60 % of wins targeted by default).
constexpr float kKoAdaptiveStart = 0.f;
constexpr float kKoAdaptiveStep = 0.04f;
// Ticks between a teleport onto the spawn and the boost pad reset of the rep (0.25 s, well inside the shortest
// countdown of 1 s). A reset sent on the teleport tick came too early: the pads the teleport ran over were still
// missing at GO (see KickoffModule::placeRep).
constexpr int64_t kKoPadResetDelayTicks = 30;

const char* kickoffModeName(KickoffMode m);            // "Position choisie", "Aléatoire", "Série des 5", "Faiblesse"
const char* kickoffGhostModeName(KickoffGhostMode g);  // "Désactivé", "Débutant", ... "Mon record", "Adaptatif"
bool kickoffGhostModeFromKey(const std::string& key,
                             KickoffGhostMode& out);  // pro|elite|standard|beginner|pb|off|adaptive

// Copy of the last graded attempt (HUD card and UI).
struct KickoffResultView {
    bool valid = false;
    bool passive = false;    // analysed outside the drill
    KickoffMetrics metrics;  // `along` cleared
    KickoffGrade grade;
    bool hasDuel = false;
    KickoffDuelResult duel;
    std::string opponent;  // "fantôme Pro", "adversaire"
    bool newPb = false;
    float previousBest = 0.f;
    std::vector<KickoffSplit> splits;
    float gameTime = 0.f;  // Snapshot::time of the result
};

// Live state for the UI tab.
struct KickoffStatusView {
    bool enabled = true;
    bool running = false;
    KickoffPhase phase = KickoffPhase::Idle;
    KickoffMode mode = KickoffMode::Random;
    KickoffSpawn spawn = KickoffSpawn::DiagonalLeft;
    KickoffTeam team = KickoffTeam::Blue;
    KickoffGhostMode ghost = KickoffGhostMode::Pro;
    bool ghostOverridden = false;  // set by the console "ghost" command (until the setting changes)
    bool ghostFallback = false;    // "Mon record" without a record yet: Pro ghost used
    float ghostArrive = 0.f;
    int countdown = 0;      // 3, 2, 1 during the countdown
    float liveTime = 0.f;   // s since GO (frozen at the touch)
    int sequenceIndex = 0;  // reps done in the current series
    int sequenceTotal = kKoSpawnCount;
    int sessionAttempts = 0;
    float sessionAvgScore = 0.f;
    float adaptiveLevel = 0.f;
    std::string message;  // last notice ("Exercice indisponible ici...")
};

class KickoffModule : public Module {
public:
    const char* id() const override { return "kickoff"; }
    const char* title() const override { return "Kickoff"; }

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

    // ---- Read accessors (the UI copies them under the engine lock) --------------------------------------------
    const KickoffConfig& config() const { return cfg_; }
    KickoffStatusView status() const;
    const KickoffResultView& lastResult() const { return last_; }
    std::array<KickoffSpawnStats, kKoSpawnCount> spawnStats() const;
    const KickoffStats& stats() const { return stats_; }
    float adaptiveLevel() const { return staircase_.level(); }

    // ---- Introspection (tests, HUD) ---------------------------------------------------------------------------
    bool running() const { return running_; }
    KickoffPhase phase() const { return phase_; }
    KickoffMode mode() const { return mode_; }
    KickoffSpawn currentSpawn() const { return spawn_; }
    KickoffTeam currentTeam() const { return team_; }
    int64_t placeFrame() const { return placeFrame_; }
    int64_t nominalGoFrame() const { return nominalGo_; }
    int64_t goFrame() const { return goFrame_; }
    bool ghostActive() const { return ghostActive_; }
    const KickoffGhost& ghost() const { return ghost_; }
    bool passiveLive() const { return passivePhase_ == PassivePhase::Live; }
    bool padResetPending() const { return padResetPending_; }
    KickoffGhostMode effectiveGhostMode() const;

private:
    enum class PassivePhase : int { Waiting = 0, Armed = 1, Live = 2 };

    // Drill.
    bool startDrill(const TickContext& ctx, Actions& out, KickoffMode mode, std::optional<KickoffSpawn> spawn);
    void stopDrill(bool release, const std::string& message);
    KickoffSpawn pickSpawn();
    void placeRep(const TickContext& ctx, Actions& out);
    void tickCountdown(const TickContext& ctx, Actions& out);
    void enterLive(const TickContext& ctx, Actions& out);
    void tickLive(const TickContext& ctx, Actions& out);
    void tickGhost(const TickContext& ctx, Actions& out, float t);
    void advanceAfterResult(const TickContext& ctx, Actions& out, bool force);
    void restartRep();
    void setupGhost(KickoffGhostMode mode);
    void finalize(const TickContext& ctx, Actions& out, bool passive, std::optional<KickoffDuel> forcedOutcome);
    void finishSeries(Actions& out);
    void feedUpTo(const TickHistory& history, int64_t frame);
    void updateLiveSplits();
    KickoffDuelParams duelParams() const;
    KickoffRecorderParams recorderParams(const PlayerProfile& profile) const;
    bool carTeleported(const Snapshot& sn);
    bool headingToBall(const Snapshot& sn) const;
    void resetRepState();
    void notePadPickups();  // keeps the last pad pickup frame of the attempt being dropped (pad respawn timer)
    void armPadReset(int64_t teleportFrame);  // the car was moved onto its spawn from elsewhere on this tick
    void sendPadReset(Actions& out);           // respawn every boost pad now

    // Passive analysis.
    void tickPassive(const TickContext& ctx, Actions& out);
    void resetPassive();

    // Context.
    void migrateDefaults(Actions& out);
    void resetTransient();
    void notify(Actions& out, const std::string& text, ToastKind kind);

    // Draw helpers (KickoffHud.cpp).
    void drawDrillHud(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawResultCard(const ViewContext& view, const DrawContext& ctx, DrawList& out, bool compactTitle) const;
    void drawWorld(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    float liveTimeAt(int64_t frame) const;

    KickoffConfig cfg_;
    int lastGhostLevelCfg_ = -1;
    // One-time moves of old defaults saved in config.cfg, done on the first tick once config.cfg is loaded. Saved,
    // so each runs once. v2: Pro ghost and Pro grading to Adaptive / Standard. v3: the plugin's big countdown and
    // the lines to the ball turned off (the game's own countdown stays).
    bool defaultsV2_ = false;
    bool defaultsV3_ = false;
    std::optional<KickoffGhostMode> ghostOverride_;

    // Drill state.
    bool running_ = false;
    KickoffPhase phase_ = KickoffPhase::Idle;
    KickoffMode mode_ = KickoffMode::Random;
    KickoffSpawn chosenSpawn_ = KickoffSpawn::DiagonalLeft;
    KickoffSpawn spawn_ = KickoffSpawn::DiagonalLeft;
    KickoffTeam team_ = KickoffTeam::Blue;
    std::optional<KickoffSpawn> repeatSpawn_;  // restart the same spawn (aborted rep)
    int sequenceDone_ = 0;
    std::vector<int> sequenceScores_;
    int64_t lastTickFrame_ = -1;
    int64_t placeFrame_ = 0, nominalGo_ = 0, goFrame_ = 0, doneFrame_ = 0, fedFrame_ = -1;
    bool attemptDone_ = false;
    bool waitingForNext_ = false;  // autoRepeat off: result shown, waiting for Reset / "next"
    bool forceNext_ = false;       // Reset or "next" during the result: next rep right away

    KickoffRecorder recorder_;
    KickoffGhost ghost_;
    KickoffGhost splitRef_;  // reference for live splits (the ghost, or the grade target when no ghost)
    bool ghostActive_ = false;
    bool ghostFallback_ = false;
    KickoffGhostMode repGhostMode_ = KickoffGhostMode::Off;
    bool ghostResolved_ = false;
    std::optional<KickoffDuelResult> duel_;
    std::array<KickoffSplit, 4> liveSplits_{};

    Vec3 lastCarPos_;
    int64_t lastCarFrame_ = 0;
    bool haveLastCar_ = false;
    int invalidTicks_ = 0;
    // Last boost pad pickup seen by an attempt (small pads respawn after 4 s). Cleared when the drill respawns every
    // pad (always before GO): it only matters for passive kickoffs (the plugin does not touch the pads outside the
    // drill).
    int64_t lastPadFrame_ = -1;
    // Pad reset of the current rep: armed by each teleport onto the spawn, sent from padResetFrame_ on, once the car
    // is seen on its spawn (or at GO at the latest).
    bool padResetPending_ = false;
    int64_t padResetFrame_ = 0;

    // Passive analysis state.
    PassivePhase passivePhase_ = PassivePhase::Waiting;
    KickoffSpawn passiveSpawn_ = KickoffSpawn::BackCenter;
    KickoffTeam passiveTeam_ = KickoffTeam::Blue;
    int ballMovingTicks_ = 0;

    // Context tracking (map change resets transient state).
    GameMode lastMode_ = GameMode::Menu;
    std::string lastMap_;
    bool contextKnown_ = false;

    // Progress.
    KickoffStats stats_;
    Staircase staircase_{0.6f, kKoAdaptiveStep, kKoAdaptiveStart};
    Rng rng_{0x6b69636b6f6666ULL};
    int sessionAttempts_ = 0;
    int sessionScoreSum_ = 0;
    KickoffResultView last_;
    std::string message_;
};

} // namespace rlt
