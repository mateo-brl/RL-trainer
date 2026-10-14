#pragma once
// Parcours: virtual training maps drawn over plain freeplay (no download).
//  - RINGS ("Anneaux"): seeded courses of rings the car flies through (aerial), arches on the floor and the
//    side walls (ground & walls), or rings the BALL must go through while the player air dribbles. Shareable
//    course codes, splits and personal best per code, time penalty or fail on a missed ring.
//  - CONTROL ("Contrôle"): roof dribble keep-up, dribble slalom through gates, catch (ball dropped from the
//    sky or coming off a side wall, to settle on the roof).
// Difficulty is adaptive (one staircase per exercise, 75 % success target) or chosen by the player.
// Record values reported to the coach (AttemptResult): rings and slalom report the finish time (lower is better)
// of a finished, record-eligible run only, the roof dribble the time held (higher is better), the catch none.
// Every exercise also has a "parcours complet" (full sequence): the steps before the skill, checked one by one with
// the shared StepTracker (take-off, wall climb, pickup of a ball on the floor, reading a lob...). It has its own
// staircase; the pickup ("Ramassage", shared helper) has one more, so a missed pickup never lowers the exercise.
// Start of every attempt: the car (and the ball) are held still, re-placed on every tick; the first attempt waits for
// the throttle then counts 3 s down, the next ones count down `countdown` seconds (ReadyPhase).
// Commands: "rlt courses <cmd>", see onCommand and the tab.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Hold.h"
#include "core/common/Module.h"
#include "core/common/Pickup.h"
#include "core/common/Rng.h"
#include "core/common/Steps.h"
#include "core/modules/courses/ControlDrills.h"
#include "core/modules/courses/CourseCode.h"
#include "core/modules/courses/CourseGen.h"
#include "core/modules/courses/CourseRun.h"
#include "core/modules/courses/CoursesConfig.h"

namespace rlt {

enum class CoursesKind : int { RingsAir = 0, RingsGround, RingsAirDribble, Dribble, Slalom, Catch };
constexpr int kCoursesKindCount = 6;

enum class CoursesPhase : int { Idle = 0, Setup, Countdown, Running, Result };

// ---- Plain read-only views for the UI (copied under the engine lock) ---------------------------------------
struct CoursesStatus {
    bool active = false;
    CoursesKind kind = CoursesKind::RingsAir;
    CoursesPhase phase = CoursesPhase::Idle;
    bool full = false;          // "parcours complet" attempt
    std::string title;          // "Anneaux aériens" (or the full sequence title)
    std::string phaseLabel;     // "Compte à rebours", "En cours", "Résultat"
    std::string stepLine;       // full sequence: current step ("2/5 Lève le nez"), empty otherwise
    std::string code;           // course code (empty for the roof dribble and the catch)
    int difficulty = 0;         // 0..100 of the current attempt
    bool fromCode = false;      // played from a code (difficulty fixed, no adaptation)
    float time = 0.f;           // current attempt time (penalties included)
    int next = 0, total = 0;    // next ring index (0-based) and ring count
    int misses = 0;
    bool hasPb = false;
    float pbTime = 0.f;
    float parTime = 0.f;
    float estBoost = 0.f;
};

struct CoursesPbView {
    std::string code;
    std::string typeLabel;
    int difficulty = 0;
    float time = 0.f;           // 0 = never finished
    int attempts = 0;
    int finishes = 0;
    std::string date;
    int rings = 0;              // splits stored with the best time
};

struct CoursesLevelView {
    CoursesKind kind = CoursesKind::RingsAir;
    std::string title;
    int level = 0;              // staircase level 0..100
    int levelNumber = 1;        // shown to the player: niveau 1..10
    int attempts = 0, successes = 0;
    float recentRate = 0.f;
    bool fullMode = false;      // the exercise starts with its "parcours complet"
    int fullLevel = 0;          // staircase of the full sequence, 0..100
    int fullLevelNumber = 1;
};

struct CoursesLastAttempt {
    bool valid = false;
    CoursesKind kind = CoursesKind::RingsAir;
    std::string title;
    std::string code;
    bool success = false;
    float score = 0.f;
    std::string verdict;
    std::string headline;       // "Terminé en 23.45 - nouveau record !" / "Raté : la balle a touché le sol"
    float time = 0.f;
    bool newRecord = false;
    std::vector<float> segments;  // time spent per ring (rings courses), for the tab plot
};

class CoursesModule : public Module {
public:
    CoursesModule();
    CoursesModule(const CoursesModule&) = delete;  // run_ points into course_
    CoursesModule& operator=(const CoursesModule&) = delete;

    const char* id() const override { return "courses"; }
    const char* title() const override { return "Parcours"; }

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

    const CoursesConfig& config() const { return cfg_; }

    // ---- UI accessors (plain copies) ------------------------------------------------------------------------
    CoursesStatus status() const;
    std::vector<CoursesPbView> personalBests() const;  // most recently played first
    std::vector<CoursesLevelView> levels() const;
    const CoursesLastAttempt& lastAttempt() const { return last_; }
    std::string currentCode() const { return code_; }  // last course code (kept after the exercise ends)
    int difficultyFor(CoursesKind k) const;            // difficulty a new attempt would use now
    float dribbleBest() const { return dribbleBest_; }
    float catchBest() const { return catchBest_; }
    int attemptsOf(CoursesKind k) const { return attempts_[static_cast<size_t>(k)]; }
    int successesOf(CoursesKind k) const { return successes_[static_cast<size_t>(k)]; }

    static const char* kindTitle(CoursesKind k);
    static const char* kindFullTitle(CoursesKind k);  // "Parcours complet : décollage", ...
    static const char* kindKey(CoursesKind k);    // command / persistence key: "air", "ground", ...
    static const char* kindSkill(CoursesKind k);  // AttemptResult skill: "course.rings.air", ...
    static const char* kindHelp(CoursesKind k);   // one French sentence for the tab
    static int kindTier(CoursesKind k);           // 0 Débutant, 1 Intermédiaire, 2 Avancé
    static const char* kindTierLabel(CoursesKind k);  // "Débutant", "Intermédiaire", "Avancé"
    static std::vector<CoursesKind> kindsByTier();    // easiest first (Débutant, then Intermédiaire, then Avancé)
    // Difficulty 0..100 as the player reads it: niveau 1..10.
    static int levelNumber(int difficultyPct);
    static bool kindUsesCourse(CoursesKind k);
    static bool kindIsRings(CoursesKind k);
    // The full sequence starts with the shared pickup ("Ramassage"): air dribble rings, roof dribble, slalom.
    static bool kindUsesPickup(CoursesKind k);
    static bool parseKindName(const std::string& raw, CoursesKind& out);

    // ---- Full sequence ("parcours complet") and levels ------------------------------------------------------
    bool fullMode(CoursesKind k) const { return fullMode_[static_cast<size_t>(k)]; }
    int fullDifficultyFor(CoursesKind k) const;     // difficulty a new full sequence would use now
    int pickupNiveau() const { return pickup_.niveau(); }
    bool fullRun() const { return full_; }           // the current attempt is a full sequence
    const StepTracker& steps() const { return steps_; }
    const ReadyPhase& ready() const { return ready_; }
    // Roof dribble parameters at a difficulty (niveau 1: 5 s, any speed, 0.5 s off the roof allowed).
    struct DribbleParams {
        float target = 8.f, minSpeed = 250.f, tolerance = 0.3f;
    };
    static DribbleParams dribbleParams(int difficultyPct, float roofTolerance);
    float dribbleTarget() const { return dribbleTarget_; }
    float dribbleMinSpeed() const { return dribbleMinSpeed_; }
    static constexpr int kMaxPbs = 20;
    static constexpr int kMaxSplits = 64;
    // A finished course is a success only when at most a quarter of its rings were given up (penalty mode).
    static int maxMissesAllowed(int ringCount) { return ringCount > 0 ? ringCount / 4 : 0; }
    // A run with given-up rings can only set a record when each miss costs at least this many seconds.
    static constexpr float kMinPenaltyForRecord = 1.f;
    // Roof dribble: shortest keep-up (s) that counts as a record (module best and AttemptResult record value).
    static constexpr float kMinDribbleRecord = 1.f;

    // ---- Introspection for tests ----------------------------------------------------------------------------
    CoursesPhase phase() const { return phase_; }
    CoursesKind kind() const { return kind_; }
    const courses::Course& course() const { return course_; }
    const courses::CourseRun& run() const { return run_; }
    const courses::CatchPlan& catchPlan() const { return catchPlan_; }
    float runTime() const { return runTime_; }
    float timeLimit() const;

private:
    struct PbEntry {
        std::string code;
        float time = 0.f;             // 0 = never finished
        std::vector<float> splits;    // split times of the best run
        int attempts = 0;
        int finishes = 0;
        std::string date;
        uint64_t lastUse = 0;
    };

    // Lifecycle
    bool acquireActivity(CoursesKind kind, const TickContext& ctx, Actions& out);
    uint64_t newSeedFor(CoursesKind kind, const TickContext& ctx);
    bool refuseIdleShortcut(const TickContext& ctx, Actions& out);
    void startExercise(CoursesKind kind, const std::string& code, const TickContext& ctx, Actions& out);
    void replayLast(const TickContext& ctx, Actions& out);
    bool buildCourse(CoursesKind kind, uint64_t seed, int pct, bool fromCode,
                     uint32_t version = courses::kGeneratorVersion);
    void prepareControl(const TickContext& ctx);
    void restartAttempt();
    void nextAttempt(const TickContext& ctx, Actions& out);
    void stopExercise(bool release);
    void resetTransient();
    uint64_t newSeed(const TickContext& ctx);

    // Tick phases
    // Start pose of the attempt: car placement (velocity given at GO) and ball placement (velocity given at GO).
    void startPose(const TickContext& ctx, CarPlacement& car, std::optional<BallPlacement>& ball) const;
    // Short start with the ball balanced on a stopped car (Dribble, Slalom, air dribble from the roof): GO waits
    // until the throttle is let go.
    bool roofStartAtRest(const TickContext& ctx) const;
    void applyStartPose(const TickContext& ctx, Actions& out, bool release);
    void countdownTick(const TickContext& ctx, Actions& out);
    void runningTick(const TickContext& ctx, Actions& out);
    void ringsTick(const TickContext& ctx, Actions& out, double t, float dt);
    void trackRoof(const Snapshot& s, double t, float dt);
    void dribbleTick(const TickContext& ctx, Actions& out, double t, float dt);
    void catchTick(const TickContext& ctx, Actions& out, double t, float dt);
    void resultTick(const TickContext& ctx, Actions& out);
    void trackCar(const Snapshot& s);
    void parkBallIfNeeded(Actions& out) const;

    // Endings
    void finishRings(const TickContext& ctx, Actions& out);
    void failRun(const TickContext& ctx, Actions& out, const std::string& reason, const std::string& tipCode,
                 const std::string& tipText);
    void endDribble(const TickContext& ctx, Actions& out);
    void endCatch(const TickContext& ctx, Actions& out, bool success, const std::string& reason);
    void enterResult(bool success);

    // Full sequence (CoursesFull.cpp)
    void prepareFull(const TickContext& ctx);    // setup of the attempt (pose, single ring, pickup)
    void buildFullSteps();                       // steps of the attempt (called by restartAttempt)
    void fullRelease(const TickContext& ctx);    // GO tick
    void fullTick(const TickContext& ctx, Actions& out, double t, float dt);
    void endFull(const TickContext& ctx, Actions& out, bool success);
    void drawFullHud(const ViewContext& view, const DrawContext& ctx, std::vector<PanelLine>& lines) const;
    float fullD() const { return static_cast<float>(difficulty_) / 100.f; }
    bool ringStep(const Snapshot& s, double t, bool carObject);  // steps the single-ring run; true when passed
    // Catch: ball on the roof, tiny hops included (kCatchLooseTolUp).
    bool catchLooseOnRoof(const Snapshot& s) const;
    void recordAdaptive(bool success);
    void emitResult(AttemptResult r, const TickContext& ctx, Actions& out);

    // Personal bests
    PbEntry* findPb(const std::string& code);
    const PbEntry* findPb(const std::string& code) const;
    PbEntry& touchPb(const std::string& code, const std::string& date);
    void trimPbs();

    // Drawing
    void drawCourse(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawCatchMarker(const ViewContext& view, DrawList& out) const;
    void drawHud(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;

    void flash(const std::string& text, Color c, float seconds = 1.2f);
    void recordAttemptCounters(bool success);
    bool gateCourse() const { return kind_ == CoursesKind::Slalom; }  // gates ("porte") instead of rings
    courses::MissPolicy missPolicy() const;
    Vec3 parkedBallPos() const { return parkPos_; }

    CoursesConfig cfg_;

    // Exercise state
    CoursesKind kind_ = CoursesKind::RingsAir;
    CoursesPhase phase_ = CoursesPhase::Idle;
    int phaseTicks_ = 0;
    int countdownTotal_ = 0;
    int lastCountdownMsg_ = 0;
    bool fromCode_ = false;
    int difficulty_ = 0;
    uint64_t seed_ = 0;
    std::string code_;
    courses::Course course_;
    courses::CourseRun run_;
    Vec3 parkPos_{3300.f, 4400.f, 1940.f};

    // Get-ready phase: the first attempt waits for the throttle, then 3 s; the next ones count `countdown` seconds.
    ReadyPhase ready_;
    bool firstAttempt_ = true;

    // Control drills
    courses::CatchPlan catchPlan_;
    courses::RoofKeeper keeper_;
    Vec3 dribbleCarPos_{0.f, -3600.f, 17.f};
    float dribbleTarget_ = 10.f;
    float dribbleMinSpeed_ = 300.f;
    float dribbleTolerance_ = 0.3f;
    float catchGap_ = 0.f;        // current break (s) of the catch hold, within kCatchHopGrace
    bool catchHopFwd_ = false;    // the ball hops ahead of the roof (HUD: follow it forward)
    bool targetReached_ = false;
    bool wasOnRoof_ = false;
    Vec3 departRelVel_, departRelPos_;
    float catchOnRoof_ = 0.f;
    int touches_ = 0;
    bool haveFirstTouch_ = false;
    // First catch contact: only the part of the closing speed the player controls is graded. The fall speed of
    // the ball is unavoidable for a car waiting on the ground; the horizontal mismatch and a car jumping up into
    // the ball are not.
    float firstTouchFlat_ = 0.f;    // |horizontal (ball - car) velocity| before the contact
    float firstTouchJump_ = 0.f;    // upward car speed into the ball before the contact
    float firstTouchOffset_ = 0.f;  // horizontal distance from the roof centre to the ball at the contact

    // Running samples
    int64_t startFrame_ = 0;
    int64_t lastFrame_ = -1;
    bool havePrev_ = false;
    courses::DVec3 prevPos_, prevVel_;
    double prevT_ = 0.0;
    float runTime_ = 0.f;
    float lastBoost_ = -1.f;
    float boostUsed_ = 0.f;
    bool hadBoost_ = false;       // the tank held real boost (> 5) at some point of the run
    bool boostEmptied_ = false;   // ...and was then emptied
    double speedSum_ = 0.0;
    int speedSamples_ = 0;
    int teleports_ = 0;
    bool hasDelta_ = false;
    float lastDelta_ = 0.f;
    bool ballOnRoofNow_ = false;

    // HUD feedback
    int flashTicks_ = 0;
    std::string flashText_;
    Color flashColor_ = colors::White;
    std::string resultHint_;   // main tip of the last attempt
    std::string resultMedal_;  // "médaille d'or" and the like, empty without a medal (finished rings and slalom)

    // Full sequence ("parcours complet")
    std::array<bool, kCoursesKindCount> fullMode_{};
    std::array<Staircase, kCoursesKindCount> fullStairs_;
    bool full_ = false;
    StepTracker steps_;
    pickup::Progress pickup_;
    pickup::Setup pickupSetup_;
    Vec3 fullCarPos_;              // start pose of the full sequence (car at rest)
    float fullYawDeg_ = 90.f;
    Vec3 fullBallPos_, fullBallVel_;  // catch: ball resting before the lob and its launch velocity
    int fullSide_ = 1;             // wall climb: +1 / -1 wall; slalom: lane sign helper
    bool fullStarted_ = false;     // the skill part began (after the pickup): ring run, keeper
    bool ringFrozen_ = false;      // air dribble: ring placed at the pop
    bool fullAirborne_ = false;    // air: the car left the ground
    float popJumpTime_ = -1.f;     // air dribble: run time of the jump with the ball on the roof
    float boostAtPop_ = 0.f;
    bool ringPassed_ = false;
    bool ringMissed_ = false;
    bool ballWasHigh_ = false;     // catch: the lob left the floor (floor contact = fail from then on)
    float fullSpeedSum_ = 0.f;
    int fullSpeedSamples_ = 0;
    float fullStartT_ = 0.f;       // run time when the skill part began
    float bodyOnFloor_ = 0.f;      // take-off: seconds with the car body on the floor, not on its wheels
    bool lowTouch_ = false;        // dribble: first touch low on the nose and slow

    // Results and progress
    CoursesLastAttempt last_;
    bool lastSuccess_ = false;
    std::vector<PbEntry> pbs_;
    std::array<Staircase, kCoursesKindCount> stairs_;
    std::array<int, kCoursesKindCount> attempts_{};
    std::array<int, kCoursesKindCount> successes_{};
    float dribbleBest_ = 0.f;
    float catchBest_ = 0.f;
    uint64_t useSeq_ = 0;

    // Context
    std::string lastMapName_;
    GameMode lastMode_ = GameMode::Menu;
    bool haveContext_ = false;
    Rng rng_;
    uint64_t seedSalt_ = 0x5EED5EED12345678ULL;
};

} // namespace rlt
