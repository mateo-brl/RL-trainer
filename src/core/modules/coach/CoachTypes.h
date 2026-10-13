#pragma once
// Plain data types of the coach module, shared by the logic (stats, plan, goals, sessions) and the UI tab.
// Everything here is a copyable value type: the UI copies it under the engine lock.

#include <string>
#include <vector>

#include "core/common/Grade.h"

namespace rlt {

// Direction of the recent scores (least-squares slope of the last 20 scores).
enum class Trend : int { Unknown = 0, Down = 1, Stable = 2, Up = 3 };

inline const char* trendLabel(Trend t) {
    switch (t) {
    case Trend::Up: return "en progrès";
    case Trend::Stable: return "stable";
    case Trend::Down: return "en baisse";
    default: return "-";
    }
}

// How to start the exercise that trains a skill: a command of another module, posted by the UI.
struct LaunchSpec {
    std::string module;             // "mechanics"
    std::string command;            // "drill"
    std::vector<std::string> args;  // {"speedflip"}
    std::string label;              // French: "Exercice Speedflip"

    bool valid() const { return !module.empty() && !command.empty(); }
    // Identity used to avoid recommending the same exercise twice: "mechanics drill speedflip".
    std::string key() const {
        std::string k = module + " " + command;
        for (const auto& a : args) k += " " + a;
        return k;
    }
    // Console line the player can type: "rlt mechanics drill speedflip".
    std::string consoleLine() const { return valid() ? "rlt " + key() : std::string(); }
};

// The most frequent mistake (tip code) of a skill over its recent attempts.
struct MistakeSummary {
    std::string code;
    std::string text;  // latest French text for that code
    int count = 0;     // attempts of the window that showed this code
    int window = 0;    // attempts in the window (<= 30)
    bool valid() const { return !code.empty() && count > 0; }
};

// One recommended exercise of the training plan.
struct PlanItem {
    std::string skill;   // skill id that motivated it (starter items use a representative id)
    std::string title;   // French display name
    std::string family;  // prefix of the skill id ("mech", "ball", ...)
    LaunchSpec launch;
    std::string reason;  // French, why this exercise now
    float need = 0.f;    // ranking score (higher = more useful now)
    bool starter = false;
};

enum class GoalKind : int { Attempts = 0, Successes = 1, ScoreAtLeast = 2, Record = 3 };

// A session goal: counts matching results until `target` is reached.
struct Goal {
    GoalKind kind = GoalKind::Attempts;
    std::string scope;  // "" = any skill, else a skill id or a prefix ("first_touch", "course.rings")
    int target = 10;
    float minScore = 0.f;  // ScoreAtLeast only
    std::string label;     // French, e.g. "10 speedflips réussis"
    std::string preset;    // preset id, "custom" or "focus"
    int progress = 0;      // this session
    bool done = false;     // reached this session

    float ratio() const {
        if (target <= 0) return 0.f;
        return progress >= target ? 1.f : static_cast<float>(progress) / static_cast<float>(target);
    }
};

// Feedback card shown on the HUD after a result.
struct FeedbackCard {
    bool active = false;
    float startTime = 0.f;  // game time (Snapshot::time) when it was created
    std::string title;
    Verdict verdict = Verdict::None;
    float score = 0.f;
    bool success = false;
    std::vector<std::string> metricLines;  // "Écart de vitesse : +120 uu/s (idéal 0 uu/s)"
    std::string tip;
    std::vector<std::string> tipLines;  // `tip` wrapped for the HUD (computed once)
    bool tipRecurring = false;          // same code in >= 3 of the last 5 attempts
    std::string recurringNote;          // "Erreur fréquente : 4 des 5 derniers essais"
    bool newRecord = false;
    std::string recordLine;  // "Nouveau record ! 1.92 s (avant 2.05 s)"
    std::string goalLine;    // "Objectif 7/10 : 10 speedflips réussis"
};

// Per-skill statistics for the UI.
struct SkillView {
    std::string id;
    std::string title;
    std::string family;
    std::string familyLabel;
    int attempts = 0;
    int successes = 0;
    float successRate = 0.f;    // all time, 0..1
    float recentSuccess = 0.f;  // last 10 attempts, 0..1
    float averageAll = 0.f;
    float averageRecent = 0.f;  // last 20 attempts
    float bestScore = 0.f;
    Trend trend = Trend::Unknown;
    float trendSlope = 0.f;  // score points per attempt
    bool hasRecord = false;
    float record = 0.f;
    bool lowerIsBetter = false;
    std::string recordText;  // "1.92 s"
    std::string lastDate;    // "YYYY-MM-DD"
    int daysSince = -1;      // -1 when unknown
    int lastSession = 0;
    MistakeSummary mainMistake;
    std::vector<float> scores;  // last 50 scores, oldest first
    bool launchable = false;
    LaunchSpec launch;
};

// A recurring mistake across skills, for the "Erreurs fréquentes" list.
struct MistakeView {
    std::string skill;
    std::string skillTitle;
    MistakeSummary mistake;
};

// Closed session kept in the history.
struct SessionRecord {
    int index = 0;
    std::string date;
    int attempts = 0;
    int successes = 0;
    float average = 0.f;
    int records = 0;
    float seconds = 0.f;
    std::string strong;  // title of the best skill of the session
    std::string weak;    // title of the weakest skill of the session
};

// Header numbers of the tab.
struct CoachOverview {
    std::string today;
    int todayAttempts = 0;
    int todaySuccesses = 0;
    float todayAverage = 0.f;
    int streak = 0;
    int bestStreak = 0;
    int streakMin = 10;
    bool sessionActive = false;
    int sessionIndex = 0;
    int sessionAttempts = 0;
    int sessionSuccesses = 0;
    float sessionAverage = 0.f;
    int sessionRecords = 0;
    float sessionSeconds = 0.f;
    int totalAttempts = 0;
    int skillCount = 0;
};

// Attempts and average for one day (daily chart).
struct DayPoint {
    std::string date;
    int attempts = 0;
    float average = 0.f;
};

} // namespace rlt
