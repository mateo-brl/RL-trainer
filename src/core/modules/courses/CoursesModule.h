#pragma once
// Parcours: virtual training maps drawn over plain freeplay (no download).
//  - RINGS ("Anneaux"): seeded courses of rings the car flies through (aerial), arches on the floor and the
//    side walls (ground & walls), or rings the BALL must go through while the player air dribbles. Shareable
//    course codes, splits and personal best per code, time penalty or fail on a missed ring.
//  - CONTROL ("Contrôle"): roof dribble keep-up, dribble slalom through gates, catch (ball dropped from the
//    sky or coming off a side wall, to settle on the roof).
// Difficulty is adaptive (one staircase per exercise, 75 % success target) or chosen by the player.
// Commands: "rlt courses <cmd>", see onCommand and the tab.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
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
    std::string title;          // "Anneaux aériens"
    std::string phaseLabel;     // "Compte à rebours", "En cours", "Résultat"
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
    int attempts = 0, successes = 0;
    float recentRate = 0.f;
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
    static const char* kindKey(CoursesKind k);    // command / persistence key: "air", "ground", ...
    static const char* kindSkill(CoursesKind k);  // AttemptResult skill: "course.rings.air", ...
    static const char* kindHelp(CoursesKind k);   // one French sentence for the tab
    static bool kindUsesCourse(CoursesKind k);
    static bool kindIsRings(CoursesKind k);
    static constexpr int kMaxPbs = 20;
    static constexpr int kMaxSplits = 64;

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
    void startExercise(CoursesKind kind, const std::string& code, const TickContext& ctx, Actions& out);
    bool buildCourse(CoursesKind kind, uint64_t seed, int pct, bool fromCode);
    void prepareControl(const TickContext& ctx);
    void restartAttempt();
    void nextAttempt(const TickContext& ctx, Actions& out);
    void stopExercise(bool release);
    void resetTransient();
    uint64_t newSeed(const TickContext& ctx);

    // Tick phases
    void applyStartPose(const TickContext& ctx, Actions& out, bool release);
    void countdownTick(const TickContext& ctx, Actions& out);
    void runningTick(const TickContext& ctx, Actions& out);
    void ringsTick(const TickContext& ctx, Actions& out, double t);
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

    // Control drills
    courses::CatchPlan catchPlan_;
    courses::RoofKeeper keeper_;
    Vec3 dribbleCarPos_{0.f, -3600.f, 17.f};
    float dribbleTarget_ = 10.f;
    float dribbleMinSpeed_ = 300.f;
    bool targetReached_ = false;
    bool wasOnRoof_ = false;
    Vec3 departRelVel_, departRelPos_;
    float catchOnRoof_ = 0.f;
    int touches_ = 0;
    bool haveFirstTouch_ = false;
    float firstTouchRel_ = 0.f;

    // Running samples
    int64_t startFrame_ = 0;
    int64_t lastFrame_ = -1;
    bool havePrev_ = false;
    courses::DVec3 prevPos_, prevVel_;
    double prevT_ = 0.0;
    float runTime_ = 0.f;
    float lastBoost_ = -1.f;
    float boostUsed_ = 0.f;
    bool boostEmptied_ = false;
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
    std::string resultHint_;

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
