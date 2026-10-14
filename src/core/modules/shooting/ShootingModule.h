#pragma once
// Tirs & flicks: three drills that own the world-control activity while they run.
//   - Flicks ("Flicks"): ball resting on the roof (exact spawn), car stopped or driving to the goal; the flick is
//     detected (dodge while the ball is on the roof), classified (front, 45, 90, backflip, delayed, musty, breezi,
//     reverse 45...), measured (power, direction vs the target, height, timing) and graded.
//   - Cibles ("Cibles"): zones inside the opponent goal (top corners, low posts, centre) with four ball setups
//     (rolling, bouncing, pass across the box, ball on the roof) and adaptive difficulty. The hit point is
//     measured on the goal plane; a predicted impact marker follows the ball in flight.
//   - Puissance ("Puissance"): hardest shot challenge on a ball rolling towards the player.
// Outside drills the module can show the predicted impact of the player's shots (freeplay, workshop, replays).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Hold.h"
#include "core/common/Module.h"
#include "core/common/Pickup.h"
#include "core/common/Rng.h"
#include "core/common/Steps.h"
#include "core/modules/shooting/FlickAnalyzer.h"
#include "core/modules/shooting/ShootGeometry.h"
#include "core/modules/shooting/ShootingConfig.h"
#include "core/modules/shooting/ShootingDrills.h"
#include "core/modules/shooting/ShootingGrading.h"
#include "core/modules/shooting/ShootingFull.h"
#include "core/modules/shooting/ShootingStats.h"
#include "core/modules/shooting/ShootingTutorials.h"
#include "core/modules/shooting/ShotTracker.h"

namespace rlt {

enum class ShootMode : int { None = 0, Flick, Targets, Power };
enum class ShootPhase : int { Idle = 0, Setup, Countdown, Live, Result };

const char* shootModeLabel(ShootMode m); // "Flicks", "Cibles", "Puissance"
const char* shootPhaseLabel(ShootPhase p); // "En attente", "Mise en place", ...

// Plain copy of the live state for the UI (copy it under the engine lock).
struct ShootingStatus {
    ShootMode mode = ShootMode::None;
    ShootPhase phase = ShootPhase::Idle;
    int requestedFlick = -1; // FlickType index, -1 = any
    int zoneSelection = shoot::kZoneRandom; // 0..4 or kZoneRandom (Cibles drill and "Zone choisie dans Cibles")
    int currentZone = 4;
    int setupSelection = 0; // TargetSetup (0 = mix) of the Cibles drill
    int currentSetup = 1;
    bool flickZoneActive = false; // flick drill aims at a zone (else the whole goal)
    int flickZone = 4;
    float flickLevel = 0.f;
    std::array<float, shoot::kTargetSetupCount> setupLevels{}; // index = TargetSetup (Mix unused)
    bool hasLastShot = false;
    float lastShotSpeed = 0.f; // uu/s
    bool hasLastResult = false;
    AttemptResult lastResult;
    std::string lastFlick; // "Flick 45° (?)"
    std::string message; // last info line ("Balle non touchée : nouvel essai.")
    int attempts = 0; // graded attempts since the plugin started
    bool settled = false; // ball settled on the roof (flick drill)
    float settledTime = 0.f;
    // Get-ready, moving start, parcours complet.
    std::string readyLine; // "Appuie sur accélérer quand tu es prêt" / "Départ dans 2" ("" when not holding)
    bool movingStart = false; // the current attempt starts with the car moving (ball on the roof)
    bool full = false; // parcours complet running
    int fullKind = 0; // shoot::ShootDrill
    std::vector<std::string> steps; // the steps of the parcours ("2/4 Porte-la en roulant : OK")
    float powerLevel = 0.f;
    float powerGoalKph = 70.f; // what the power drill asks now
    std::array<float, shoot::kShootDrillCount> fullLevels{};
    std::array<int, shoot::kShootDrillCount> fullAttempts{};
    float pickupLevel = 0.f; // shared "Ramassage" staircase
};

class ShootingModule : public Module {
public:
    ShootingModule();

    const char* id() const override { return "shooting"; }
    const char* title() const override { return "Tirs & flicks"; }

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

    // ---- Read accessors (UI under the engine lock, tests) ----
    const ShootingConfig& config() const { return cfg_; }
    ShootingStatus status() const;
    const shoot::ShootingStats& stats() const { return stats_; }
    const Staircase& flickStaircase() const { return flickStair_; }
    const Staircase& targetStaircase(shoot::TargetSetup s) const;
    const Staircase& powerStaircase() const { return powerStair_; }
    const Staircase& fullStaircase(shoot::ShootDrill d) const { return fullStairs_[static_cast<size_t>(d)]; }
    const Staircase& pickupStaircase() const { return pickup_.staircase(); }
    bool fullMode() const { return full_; }
    shoot::ShootDrill fullKind() const { return fullKind_; }
    const StepTracker& fullSteps() const { return steps_; }
    const shoot::FullSetup& fullSetup() const { return fullSetup_; }
    bool movingStart() const { return movingStart_; }
    // Speed the power drill asks now (uu/s): the adaptive bar, or the fixed setting.
    float powerGoal() const;
    ShootMode mode() const { return mode_; }
    ShootPhase phase() const { return phase_; }
    float flickLevel() const;
    float targetLevel(shoot::TargetSetup s) const;
    shoot::Zone currentZone() const { return zone_; }
    shoot::TargetSetup currentSetup() const { return setup_; }
    const shoot::GoalFrame& goal() const { return goal_; }
    const shoot::Spawn& spawn() const { return spawn_; }
    bool hasPrediction() const { return shot_.hasPrediction(); }
    Vec2 prediction() const { return shot_.predicted(); }
    // Tests: deterministic spawns (no reseeding at each drill start).
    void setSeed(uint64_t seed);

private:
    // Drill flow
    void resetStaircases(); // every adaptive level back to niveau 1
    void resetLevel(shoot::ShootDrill d); // "Revenir au niveau 1": one drill and its parcours complet
    bool startDrill(ShootMode m, const TickContext& ctx, Actions& out, bool full = false);
    void stopDrill(bool release);
    void tickDrill(const TickContext& ctx, Actions& out, bool teleported);
    void prepareAttempt(const TickContext& ctx);
    void applyHeld(Actions& out, int64_t frame);
    void release(Actions& out, int64_t frame);
    void tickFlick(const TickContext& ctx, Actions& out);
    void tickTargets(const TickContext& ctx, Actions& out);
    void tickPower(const TickContext& ctx, Actions& out);
    void tickFree(const TickContext& ctx, bool teleported);
    void onFlickReport(const shoot::FlickReport& rep, const TickContext& ctx, Actions& out);
    void onTargetOutcome(const shoot::ShotOutcome& o, const TickContext& ctx, Actions& out);
    void tickAssist(const Snapshot& s, Actions& out);
    bool earlyLossReplay(const Snapshot& s);
    void prepareFull(const TickContext& ctx);
    void beginFull(const Snapshot& s);
    void onFullTouch(const TouchEvent& t, float post);
    void finishFull(const TickContext& ctx, Actions& out);
    void recordFull(bool success);
    float fullLevel() const;
    void finishAttempt(AttemptResult r, const TickContext& ctx, Actions& out);
    void newAttemptLater(const std::string& message);
    void noteHit(const shoot::ShotOutcome& o, int64_t frame, bool good);
    void toastRecord(Actions& out, const std::string& text) const;
    float elapsed(int64_t frame) const { return static_cast<float>(frame - phaseStart_) * kTickDt; }
    shoot::ZoneRect flickTargetRect() const;

    // Drawing helpers (read-only)
    void drawZones(const shoot::GoalFrame& g, shoot::Zone active, float level, const Color& activeColor,
                   const Occluder& occ, DrawList& out) const;
    void drawRect(const shoot::GoalFrame& g, const shoot::ZoneRect& r, const Color& c, float width, float inset,
                  const Occluder& occ, DrawList& out) const;
    void drawMarker(const shoot::GoalFrame& g, const Vec3& world, float radius, const Color& c, const Occluder& occ,
                    DrawList& out) const;
    void drawRoofGuide(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    DrawPanel buildPanel(const ViewContext& view, const DrawContext& ctx) const;

    ShootingConfig cfg_;
    bool settingsSeen_ = false; // first onSettingsChanged applies the zone / setup selections
    shoot::FlickThresholds thresholds_;
    Rng rng_;
    uint64_t seedCounter_ = 0;
    bool fixedSeed_ = false;

    // Drill state
    ShootMode mode_ = ShootMode::None;
    ShootPhase phase_ = ShootPhase::Idle;
    int64_t phaseStart_ = 0;
    int lastCountdownShown_ = -1;
    shoot::Spawn spawn_;
    shoot::GoalFrame goal_;
    int requested_ = -1;
    int zoneSel_ = shoot::kZoneRandom;
    shoot::Zone zone_ = shoot::Zone::Center;
    int setupSel_ = 0;
    shoot::TargetSetup setup_ = shoot::TargetSetup::Rolling;
    bool flickZoneActive_ = false;
    shoot::Zone flickZone_ = shoot::Zone::Center;
    float attemptLevel_ = 0.f;

    Staircase flickStair_;
    std::array<Staircase, shoot::kTargetSetupCount> targetStairs_;
    Staircase powerStair_;

    // Get-ready phase (fix 1): the first attempt of a drill waits for the throttle, then counts down 3 s.
    ReadyPhase ready_;
    bool firstAttempt_ = true;
    // Moving roof start: carry assist just after the release, early losses replayed.
    bool movingStart_ = false;
    bool assistActive_ = false;
    bool offRoof_ = false;         // the ball is off the roof (moving start)
    bool leftRoofEarly_ = false;   // it left the roof in the first second (kEarlyLossReplay)
    // Circles on the floor: next bounces (bouncing), meeting point (pass), run-up point (power).
    std::vector<shoot::FloorBounce> bounceMarks_;
    bool showMeet_ = false;
    Vec3 meet_;
    bool showRunUp_ = false;
    Vec3 runUp_;
    float lastFloorBounce_ = -1.f;
    float prevBallVz_ = 0.f;
    // Parcours complet.
    bool full_ = false;
    shoot::ShootDrill fullKind_ = shoot::ShootDrill::Flick;
    std::array<Staircase, shoot::kShootDrillCount> fullStairs_;
    pickup::Progress pickup_;
    StepTracker steps_;
    shoot::FullState fullState_;
    shoot::FullSetup fullSetup_;
    bool fullRecorded_ = false;
    shoot::FlickAnalyzer flick_;
    shoot::ShotTracker shot_;
    shoot::ShootingStats stats_;

    // Attempt state
    bool touched_ = false;
    bool playerMoved_ = false;
    std::optional<shoot::FlickReport> lastFlickReport_;
    bool powerTouched_ = false;
    int64_t powerFirst_ = 0;
    shoot::PowerShot powerShot_;
    int64_t lastDodgeFrame_ = -1000000;
    int64_t expectJumpUntil_ = -1;
    Snapshot prev_;
    bool hasPrev_ = false;

    // Feedback state (HUD / UI)
    bool hasLastShot_ = false;
    float lastShotSpeed_ = 0.f;
    bool hasLastResult_ = false;
    AttemptResult lastResult_;
    std::string lastFlick_;
    std::string message_;
    int attempts_ = 0;
    bool hasLastHit_ = false;
    bool lastHitGood_ = false;
    Vec3 lastHitWorld_;
    shoot::GoalFrame lastHitGoal_;
    int64_t lastHitFrame_ = 0;
};

} // namespace rlt
