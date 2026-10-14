#pragma once
// Coach & progression: the brain that turns every graded attempt (AttemptResult reported by any module) into
// progress. Per-skill statistics and records, recurring mistakes, a feedback card on the HUD with feedback
// fading, sessions with a chat summary, daily streak, session goals and an adaptive training plan.
// The coach never controls the world: it only reads results and context and emits chat, toasts and a HUD panel.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/coach/CoachConfig.h"
#include "core/modules/coach/CoachPlan.h"
#include "core/modules/coach/CoachProgress.h"
#include "core/modules/coach/CoachTypes.h"
#include "core/modules/coach/SkillTracker.h"

namespace rlt {

class CoachModule : public Module {
public:
    static constexpr size_t kMaxSkills = 120;         // skills kept in the progress file
    static constexpr size_t kMaxSessionHistory = 30;  // closed sessions kept
    static constexpr int kFadingWindow = 10;          // attempts looked at by the feedback fading rule
    static constexpr int kSaveEveryResults = 20;      // periodic save request

    const char* id() const override { return "coach"; }
    const char* title() const override { return "Coach & progression"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onResult(const AttemptResult& result, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    // ---- Read accessors for the UI (render thread: call under Engine::lock() and copy) -----------------
    const CoachConfig& config() const { return cfg_; }
    CoachOverview overview() const;
    // Every skill with at least one attempt, ordered by family then title (cached, rebuilt when data changes).
    const std::vector<SkillView>& skills() const { return views_; }
    bool skill(const std::string& id, SkillView& out) const;
    const std::vector<PlanItem>& plan() const { return plan_; }
    const std::vector<Goal>& goals() const { return goals_; }
    // Main mistake of each skill that repeats (>= 2 times), most frequent first.
    std::vector<MistakeView> topMistakes(size_t n) const;
    const std::vector<SessionRecord>& sessionHistory() const { return history_; }
    std::vector<DayPoint> recentDays(int n) const;
    const FeedbackCard& card() const { return card_; }
    // Summary of the running session (same text as the chat summary), empty without attempts.
    std::string currentSessionSummary() const;
    // Plan as chat lines ("1. Speedflip : <raison> -> rlt mechanics drill speedflip").
    std::vector<std::string> planLines() const;
    const std::string& today() const { return today_; }
    // Changes whenever skills(), plan(), topMistakes(), sessionHistory() or recentDays() may have changed, so the
    // UI can skip copying them on frames where nothing happened.
    unsigned revision() const { return revision_; }

    // Card opacity at game time `now`: 1 while shown, fading to 0 during the last part of its duration.
    float cardAlpha(float now) const;

private:
    SkillTracker& tracker(const std::string& skill);
    bool setToday(const std::string& date);  // true when the date changed
    void startSession();
    void closeSession(Actions& out);
    // Rebuilds what the UI reads every frame (plan, skill views, streak) after stats, date or settings changed,
    // so the render thread only copies under the engine lock.
    void refreshDerived();
    SkillView makeView(const std::string& id, const SkillTracker& t) const;
    void buildCard(const AttemptResult& r, const SkillTracker& t, const SkillTracker::RecordUpdate& rec, float now,
                   const std::string& goalLine, bool goalReached);
    void addGoal(const Goal& g, Actions& out);
    Goal focusGoal() const;
    // French label of a goal from its definition (preset label, else built from the stable skill names).
    std::string goalLabel(const Goal& g) const;
    void resetProgress();
    void say(Actions& out, const std::string& text) const;
    void toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const;
    coach::PlanWeights weights() const;
    std::string titleFor(const std::string& scope) const;
    int goalsDone() const;
    int goalsTotal() const { return static_cast<int>(goals_.size()); }

    CoachConfig cfg_;
    std::map<std::string, SkillTracker> skills_;
    DailyLog days_;
    SessionTracker session_;
    std::vector<SessionRecord> history_;
    std::vector<Goal> goals_;
    std::vector<PlanItem> plan_;
    std::vector<SkillView> views_;
    int streak_ = 0;
    unsigned revision_ = 0;
    FeedbackCard card_;
    std::string today_;
    int sessionCount_ = 0;
    int bestStreak_ = 0;
    int totalAttempts_ = 0;
    int unsavedResults_ = 0;
    int breaksShown_ = 0;  // break reminders already shown this session

    // Session boundaries: only a real map / mode change closes a session (not a date or category refresh).
    bool contextSeen_ = false;
    GameMode lastMode_ = GameMode::Menu;
    std::string lastMap_;
    int64_t lastTickFrame_ = -1;
    float lastTime_ = 0.f;  // latest game time seen (card timing when a result arrives without a snapshot)
};

} // namespace rlt
