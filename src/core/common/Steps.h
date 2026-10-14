#pragma once
// Step tracker of the "parcours complet" drills (cross-cutting fix 2): an ordered list of steps the player does one
// after the other (go to the wall, climb to the ceiling, drop without jumping...). Each step has a check run every
// tick, an optional hold time (the check must stay true that long), its own time limit, a fail code and a tip for
// when it is the step that was missing. The tracker tells the module how far the player got, so generic checks
// ("landed", "ball lost", grades of another mechanic) can stay off until the step that needs them, builds the
// "Il manquait : ..." message and the HUD lines (validated steps turn green, the current one is highlighted, the
// others are detail lines).
//
// Rules:
// - Steps are validated in order. A step's clock starts when the previous REQUIRED step is validated (at start()
//   for the first one), so an info step in between does not give the next step more time.
// - An optional step (info step) is shown and turns green when its check passes, never blocks and never fails: the
//   next required step is checked on the same ticks, and validating it skips the info step.
// - Several steps can be validated on the same tick (a step already true when it becomes current passes at once),
//   but each check runs at most once per tick, so hold times are never counted twice.
// - The tracker fails on the current required step: its check says so, its time limit passes, or the module calls
//   fail() / failOnTimeout() (landing, ball lost, global time limit). The failure names the step that was missing.
//
// Pure: no Actions, no randomness. Call update() once per tick, after refreshing what the checks read.

#include <functional>
#include <string>
#include <vector>

#include "core/common/DrawList.h"
#include "core/common/Grade.h"
#include "core/common/Snapshot.h"

namespace rlt {

struct TickContext;  // core/common/Module.h

enum class StepState : int { Pending = 0, Done = 1, Failed = 2, Skipped = 3 };

// What a step check sees on each tick.
struct StepInput {
    const Snapshot& snap;
    const TickContext* ctx = nullptr;  // the module's tick context when it passed one (nullptr in pure tests)
    float now = 0.f;                   // snap.time
    float stepTime = 0.f;              // seconds since this step's clock started (end of the previous required step)
    float attemptTime = 0.f;           // seconds since StepTracker::start
};

// What a check returns. A failure may carry its own code and tip; empty means the step's failCode / tip.
struct StepVerdict {
    StepState state = StepState::Pending;
    std::string code;
    std::string tip;

    static StepVerdict pending() { return {}; }
    static StepVerdict done() { return {StepState::Done, {}, {}}; }
    static StepVerdict fail(std::string code = {}, std::string tip = {}) {
        return {StepState::Failed, std::move(code), std::move(tip)};
    }
};

using StepCheck = std::function<StepVerdict(const StepInput&)>;

// Check from a plain condition: true = done, false = not yet (never fails by itself).
StepCheck stepWhen(std::function<bool(const StepInput&)> condition);
StepCheck stepWhenSnap(std::function<bool(const Snapshot&)> condition);

struct Step {
    std::string id;          // stable id, unique in the list ("wall", "ceiling", "pickup.roof"...)
    std::string label;       // French, plain words, short: the HUD line ("Va au mur de côté")
    StepCheck check;         // empty: never validates by itself (an info line, or validated through complete())
    float timeout = 0.f;     // seconds allowed from the step's clock start (0 = no limit of its own)
    float hold = 0.f;        // the check must say done continuously this long (0 = a single tick)
    float holdGrace = 0.f;   // short breaks (each at most this long) do not restart the hold (e.g. 0.3 s off the roof)
    std::string failCode;    // code of the failed attempt when this step is the missing one
    std::string tip;         // French tip when this step is the missing one (imperative, tutoiement)
    bool optional = false;   // info step: turns green when its check passes, never blocks, never fails
    std::string group;       // free tag ("pickup"): tells the module which staircase a failure feeds
};

class StepTracker {
public:
    enum class Status : int { Idle = 0, Running = 1, Complete = 2, Failed = 3 };

    struct Failure {
        int index = -1;          // step that was missing (-1 when the list is empty)
        std::string id;
        std::string label;
        std::string code;        // the check's code, or the step's failCode, or the code given to fail()
        std::string tip;         // same order
        std::string group;
        bool timedOut = false;   // the step's or the attempt's time limit passed
        float time = 0.f;
    };

    // ---- Definition (between attempts) ----
    void clear();                          // no steps, Idle
    void add(Step step);                   // appends; ids should be unique
    void setSteps(std::vector<Step> steps);
    const std::vector<Step>& steps() const { return steps_; }
    int size() const { return static_cast<int>(steps_.size()); }
    bool empty() const { return steps_.empty(); }

    // ---- Attempt ----
    // Starts an attempt at game time `now` (Snapshot::time): every step pending, the first step's clock starts.
    void start(float now);
    // Back to Idle (states cleared, steps kept).
    void reset();
    // Runs the checks for this tick. Returns how many steps were validated on this tick (usually 0).
    int update(const Snapshot& s, const TickContext* ctx = nullptr);
    // Validates the current required step from the outside (the module saw the event itself, e.g. a graded shot).
    // Returns false when not running.
    bool complete(float now);
    // Ends the attempt as failed on the current required step. Empty code / tip: the step's own ones.
    void fail(float now, const std::string& code = {}, const std::string& tip = {}, bool timedOut = false);
    // The attempt's own time limit passed: fails on the first missing step with its code and tip (fix 5: a timeout
    // is never silent).
    void failOnTimeout(float now) { fail(now, {}, {}, true); }

    // ---- State ----
    Status status() const { return status_; }
    bool idle() const { return status_ == Status::Idle; }
    bool running() const { return status_ == Status::Running; }
    bool isComplete() const { return status_ == Status::Complete; }
    bool failed() const { return status_ == Status::Failed; }
    bool finished() const { return status_ == Status::Complete || status_ == Status::Failed; }

    // First step neither done nor skipped (may be an info step); size() when every step is through.
    int current() const;
    // First required step not done (pending, or the failed one); size() when none (complete).
    int currentRequired() const;
    const Step* currentStep() const;          // nullptr when none
    StepState state(int index) const;
    StepState state(const std::string& id) const;
    int indexOf(const std::string& id) const;  // -1 when unknown
    bool done(const std::string& id) const;
    // The attempt got that far (not Idle): the step is done or skipped, or it comes no later than the current required
    // step (the current one included). Use it to switch generic checks on only from the step that needs them.
    bool reached(const std::string& id) const;
    // The step was validated by the last update() / complete() call (e.g. release the ball on that tick).
    bool doneThisTick(const std::string& id) const;
    // The current required step is the last required one (grades of other mechanics count only from here).
    bool onLastStep() const;
    // Every step of the group is done (at least one step has that group).
    bool groupDone(const std::string& group) const;
    int doneCount() const;
    int lastDoneIndex() const { return lastDone_; }  // -1 before the first validation
    float lastDoneTime() const { return lastDoneTime_; }

    float startTime() const { return start_; }
    float attemptElapsed(float now) const { return now - start_; }
    // Seconds since the current required step's clock started.
    float stepElapsed(float now) const;
    // Time limit of the current required step minus stepElapsed (1e9 when it has none).
    float timeLeft(float now) const;
    // 0..1: how long the current step's check has held compared with its hold time (0 without hold time).
    float holdProgress() const;

    const Failure& failure() const { return failure_; }

    // "Il manquait : <label>" for the failed step, or the current required step while running; "" otherwise.
    std::string missingText() const;
    // Coaching tip of the missing step (code = fail code; text = its tip, or missingText() when it has none).
    Tip missedTip(float weight = 1.f) const;
    // "2/5 Monte au plafond" for the current step; "" when there is none.
    std::string currentLine() const;
    // "2/5 " prefix of a step (1-based index / count).
    std::string number(int index) const;

private:
    struct Live {
        StepState state = StepState::Pending;
        float held = 0.f;      // seconds the check has said done in a row (breaks within holdGrace excepted)
        float gap = 0.f;       // length of the current break in the hold
        float doneAt = 0.f;
        int64_t doneFrame = -1;
    };

    // Runs one check (hold included). Returns Done when the step validates on this tick.
    StepVerdict evaluate(int i, const Snapshot& s, const TickContext* ctx, float now, float dt);
    void markDone(int i, float now, int64_t frame);
    void finishIfComplete(float now);

    std::vector<Step> steps_;
    std::vector<Live> live_;
    Status status_ = Status::Idle;
    float start_ = 0.f;
    float clock_ = 0.f;          // clock start of the current required step
    float lastTime_ = -1.f;      // time of the previous update (for hold times)
    int64_t tickFrame_ = -1;     // frame of the last update (doneThisTick)
    int lastDone_ = -1;
    float lastDoneTime_ = -1e9f;
    Failure failure_;
};

// ---- HUD ----------------------------------------------------------------------------------------------------
// The HUD font has no check mark glyph: validated steps say "OK" in green. Colours follow the menu (accent #FFC740).
struct StepHudStyle {
    Color done{80, 230, 80, 255};        // validated steps
    Color current{255, 199, 64, 255};    // the step to do now (accent)
    Color upcoming{215, 215, 215, 255};  // steps to come, skipped info steps
    Color failed{255, 80, 60, 255};      // the step that was missing
    bool numbered = true;                // "2/5 " prefix
    bool showDone = true;                // validated steps as detail lines (full HUD only)
    bool showUpcoming = true;            // steps to come as detail lines (full HUD only)
    float flashSeconds = 1.f;            // the step validated last stays a main green line this long (0 = never)
    float detailScale = 0.85f;
    const char* doneMark = "OK";
};

// Appends the step list in order: validated steps "1/5 Va au mur : OK" (green, detail), the current step (accent,
// MAIN line, with a progress bar while its hold time fills), the steps to come (detail). While the current step is
// an info step, the required step after it is a main accent line too (it is what ends the attempt). The step
// validated less than flashSeconds ago stays a main line so the compact HUD shows it turn green. After a failure the
// missing step is a main red line. Main lines from here: usually 1 or 2 (flash + current), 3 only during the flash
// before an info step; set flashSeconds to 0 when the panel has no room.
void appendStepLines(std::vector<PanelLine>& out, const StepTracker& t, float now, const StepHudStyle& style = {});

} // namespace rlt
