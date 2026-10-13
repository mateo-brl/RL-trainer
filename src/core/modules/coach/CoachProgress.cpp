#include "core/modules/coach/CoachProgress.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/common/Text.h"
#include "core/modules/coach/CoachUtil.h"

namespace rlt {

namespace {

bool toInt(const std::string& s, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

bool toDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

std::string fixed(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // namespace

// ---- Session -------------------------------------------------------------------------------------------
void SessionTracker::start(int index, const std::string& date) {
    *this = SessionTracker{};
    active_ = true;
    index_ = index;
    date_ = date;
}

void SessionTracker::add(const std::string& skill, const std::string& title, bool success, float score, bool record) {
    if (!active_) return;
    ++attempts_;
    if (success) ++successes_;
    scoreSum_ += static_cast<double>(score);
    if (record) ++records_;
    auto it = perSkill_.find(skill);
    if (it == perSkill_.end()) {
        if (perSkill_.size() >= kMaxSkills) return;
        it = perSkill_.emplace(skill, SkillStat{}).first;
    }
    it->second.title = title;
    ++it->second.attempts;
    it->second.scoreSum += static_cast<double>(score);
}

void SessionTracker::addTime(float seconds) {
    if (active_ && std::isfinite(seconds) && seconds > 0.f) seconds_ += seconds;
}

void SessionTracker::strongWeak(int minAttempts, std::string& strong, std::string& weak) const {
    strong.clear();
    weak.clear();
    const SkillStat* best = nullptr;
    const SkillStat* worst = nullptr;
    double bestAvg = -1.0, worstAvg = 1e9;
    int qualifying = 0;
    for (const auto& kv : perSkill_) {  // map order: ties resolved by skill id
        const SkillStat& s = kv.second;
        if (s.attempts < std::max(1, minAttempts)) continue;
        ++qualifying;
        const double avg = s.scoreSum / s.attempts;
        if (avg > bestAvg) {
            bestAvg = avg;
            best = &s;
        }
        if (avg < worstAvg) {
            worstAvg = avg;
            worst = &s;
        }
    }
    if (qualifying == 0) return;
    if (qualifying == 1) {
        if (bestAvg >= 60.0) strong = best->title;
        else weak = best->title;
        return;
    }
    strong = best->title;
    if (worst != best) weak = worst->title;
}

SessionRecord SessionTracker::toRecord(int minAttempts) const {
    SessionRecord r;
    r.index = index_;
    r.date = date_;
    r.attempts = attempts_;
    r.successes = successes_;
    r.average = average();
    r.records = records_;
    r.seconds = seconds_;
    strongWeak(minAttempts, r.strong, r.weak);
    return r;
}

std::string sessionSummaryText(const SessionRecord& s, int streak, int goalsDone, int goalsTotal) {
    std::string t = "Session : " + std::to_string(s.attempts) + (s.attempts > 1 ? " essais" : " essai");
    if (s.seconds >= 60.f) t += " en " + formatClock(s.seconds);
    t += ", moyenne " + coach::formatScore(s.average);
    if (s.records > 0) t += ", " + std::to_string(s.records) + (s.records > 1 ? " records" : " record");
    if (!s.strong.empty()) t += ", point fort " + s.strong;
    if (!s.weak.empty()) t += ", à travailler " + s.weak;
    if (goalsTotal > 0) t += ", objectifs " + std::to_string(goalsDone) + "/" + std::to_string(goalsTotal);
    t += ".";
    if (streak > 0) t += " Série : " + std::to_string(streak) + (streak > 1 ? " jours." : " jour.");
    return t;
}

// ---- Days ----------------------------------------------------------------------------------------------
void DailyLog::add(const std::string& date, bool success, float score) {
    if (!coach::validDate(date)) return;
    DayStats& d = days_[date];
    ++d.attempts;
    if (success) ++d.successes;
    d.scoreSum += static_cast<double>(score);
    while (days_.size() > kMaxDays) days_.erase(days_.begin());
}

const DayStats* DailyLog::day(const std::string& date) const {
    auto it = days_.find(date);
    return it == days_.end() ? nullptr : &it->second;
}

int DailyLog::attemptsOn(const std::string& date) const {
    const DayStats* d = day(date);
    return d ? d->attempts : 0;
}

int DailyLog::streak(const std::string& today, int minAttempts) const {
    long t = 0;
    if (!coach::dayNumber(today, t)) return 0;
    const int need = std::max(1, minAttempts);
    auto qualifies = [&](long n) { return attemptsOn(coach::dateFromDayNumber(n)) >= need; };
    long d = qualifies(t) ? t : t - 1;
    int count = 0;
    while (count < static_cast<int>(kMaxDays) && qualifies(d)) {
        ++count;
        --d;
    }
    return count;
}

std::vector<DayPoint> DailyLog::lastDays(const std::string& today, int n) const {
    std::vector<DayPoint> out;
    long t = 0;
    if (n <= 0 || !coach::dayNumber(today, t)) return out;
    out.reserve(static_cast<size_t>(n));
    for (long d = t - n + 1; d <= t; ++d) {
        DayPoint p;
        p.date = coach::dateFromDayNumber(d);
        if (const DayStats* s = day(p.date)) {
            p.attempts = s->attempts;
            p.average = s->attempts > 0 ? static_cast<float>(s->scoreSum / s->attempts) : 0.f;
        }
        out.push_back(std::move(p));
    }
    return out;
}

void DailyLog::save(Store& st, const std::string& key) const {
    std::vector<std::string> lines;
    lines.reserve(days_.size());
    for (const auto& kv : days_)
        lines.push_back(kv.first + "\t" + std::to_string(kv.second.attempts) + "\t" +
                        std::to_string(kv.second.successes) + "\t" + fixed(kv.second.scoreSum, 1));
    st.setList(key, lines);
}

void DailyLog::load(const Store& st, const std::string& key) {
    days_.clear();
    for (const auto& line : st.getList(key)) {
        const auto f = coach::split(line, '\t');
        if (f.size() != 4 || !coach::validDate(f[0])) continue;
        long long a = 0, s = 0;
        double sum = 0.0;
        if (!toInt(f[1], a) || !toInt(f[2], s) || !toDouble(f[3], sum) || a < 0) continue;
        DayStats d;
        d.attempts = static_cast<int>(std::min<long long>(a, 1000000));
        d.successes = static_cast<int>(std::clamp<long long>(s, 0, d.attempts));
        d.scoreSum = std::clamp(sum, 0.0, 100.0 * d.attempts);
        days_[f[0]] = d;
    }
    while (days_.size() > kMaxDays) days_.erase(days_.begin());
}

// ---- Session history -----------------------------------------------------------------------------------
std::string encodeSession(const SessionRecord& s) {
    return std::to_string(s.index) + "\t" + s.date + "\t" + std::to_string(s.attempts) + "\t" +
           std::to_string(s.successes) + "\t" + fixed(static_cast<double>(s.average), 1) + "\t" +
           std::to_string(s.records) + "\t" + fixed(static_cast<double>(s.seconds), 0) + "\t" +
           coach::sanitizeField(s.strong, 60) + "\t" + coach::sanitizeField(s.weak, 60);
}

bool decodeSession(const std::string& line, SessionRecord& out) {
    const auto f = coach::split(line, '\t');
    if (f.size() != 9) return false;
    long long idx = 0, att = 0, succ = 0, rec = 0;
    double avg = 0.0, secs = 0.0;
    if (!toInt(f[0], idx) || !toInt(f[2], att) || !toInt(f[3], succ) || !toDouble(f[4], avg) || !toInt(f[5], rec) ||
        !toDouble(f[6], secs))
        return false;
    if (idx < 0 || att < 0) return false;
    SessionRecord r;
    r.index = static_cast<int>(std::min<long long>(idx, 100000000));
    r.date = coach::validDate(f[1]) ? f[1] : std::string();
    r.attempts = static_cast<int>(std::min<long long>(att, 1000000));
    r.successes = static_cast<int>(std::clamp<long long>(succ, 0, r.attempts));
    r.average = static_cast<float>(std::clamp(avg, 0.0, 100.0));
    r.records = static_cast<int>(std::clamp<long long>(rec, 0, r.attempts));
    r.seconds = static_cast<float>(std::clamp(secs, 0.0, 1.0e7));
    r.strong = coach::sanitizeField(f[7], 60);
    r.weak = coach::sanitizeField(f[8], 60);
    out = r;
    return true;
}

} // namespace rlt
