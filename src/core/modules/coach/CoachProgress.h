#pragma once
// Session and day bookkeeping of the coach: the current session (attempts, average, records, time, best and
// weakest skill), the bounded history of closed sessions, the per-day log and the daily streak.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "core/common/Store.h"
#include "core/modules/coach/CoachTypes.h"

namespace rlt {

class SessionTracker {
public:
    static constexpr size_t kMaxSkills = 64;

    bool active() const { return active_; }
    void start(int index, const std::string& date);
    void stop() { *this = SessionTracker{}; }

    void add(const std::string& skill, const std::string& title, bool success, float score, bool record);
    void addTime(float seconds);

    int index() const { return index_; }
    const std::string& date() const { return date_; }
    int attempts() const { return attempts_; }
    int successes() const { return successes_; }
    int records() const { return records_; }
    float seconds() const { return seconds_; }
    float average() const { return attempts_ > 0 ? static_cast<float>(scoreSum_ / attempts_) : 0.f; }

    // Title of the best / weakest skill of the session by average, among skills with >= minAttempts attempts.
    // With a single qualifying skill it is "strong" when its average is >= 60, else "weak". Empty when none.
    void strongWeak(int minAttempts, std::string& strong, std::string& weak) const;
    SessionRecord toRecord(int minAttempts = 3) const;

private:
    struct SkillStat {
        std::string title;
        int attempts = 0;
        double scoreSum = 0.0;
    };

    bool active_ = false;
    int index_ = 0;
    std::string date_;
    int attempts_ = 0;
    int successes_ = 0;
    double scoreSum_ = 0.0;
    int records_ = 0;
    float seconds_ = 0.f;
    std::map<std::string, SkillStat> perSkill_;
};

// "Session : 42 essais en 25:10, moyenne 71, 2 records, point fort Kickoff, à travailler Wavedash, objectifs 2/3.
//  Série : 3 jours." (goals only when goalsTotal > 0, streak only when > 0)
std::string sessionSummaryText(const SessionRecord& s, int streak, int goalsDone = 0, int goalsTotal = 0);

struct DayStats {
    int attempts = 0;
    int successes = 0;
    double scoreSum = 0.0;
};

class DailyLog {
public:
    static constexpr size_t kMaxDays = 400;

    // Ignores invalid dates (the glue may not have provided one yet).
    void add(const std::string& date, bool success, float score);
    const DayStats* day(const std::string& date) const;
    int attemptsOn(const std::string& date) const;
    // Consecutive days with >= minAttempts attempts, ending today (or yesterday while today is not done yet).
    int streak(const std::string& today, int minAttempts) const;
    // The last `n` days up to `today` (oldest first, days without attempts included).
    std::vector<DayPoint> lastDays(const std::string& today, int n) const;
    size_t size() const { return days_.size(); }
    void clear() { days_.clear(); }

    void save(Store& st, const std::string& key) const;
    void load(const Store& st, const std::string& key);

private:
    std::map<std::string, DayStats> days_;  // ISO dates sort chronologically
};

// Session history persistence (bounded by the caller).
std::string encodeSession(const SessionRecord& s);
bool decodeSession(const std::string& line, SessionRecord& out);

} // namespace rlt
