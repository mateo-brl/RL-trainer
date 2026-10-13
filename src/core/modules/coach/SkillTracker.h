#pragma once
// Statistics of one skill (one AttemptResult::skill id): counts, averages, trend, personal record, recurring
// mistakes (tip codes over the last 30 attempts) and recent metric values. Every history is bounded so the
// progress file stays small. Pure logic, persisted under a key prefix given by the module.

#include <cstddef>
#include <string>
#include <vector>

#include "core/common/Grade.h"
#include "core/common/Store.h"
#include "core/modules/coach/CoachTypes.h"

namespace rlt {

class SkillTracker {
public:
    static constexpr size_t kScoreHistory = 50;   // scores and outcomes kept (sparkline)
    static constexpr size_t kRecentWindow = 20;   // "recent" average and trend
    static constexpr size_t kTipWindow = 30;      // attempts used to count recurring mistakes
    static constexpr size_t kTipsPerAttempt = 3;  // codes kept per attempt
    static constexpr size_t kMaxTipTexts = 12;    // latest text per code
    static constexpr size_t kMaxMetrics = 6;      // metric keys followed per skill
    static constexpr size_t kMetricWindow = 20;   // values kept per metric

    struct RecordUpdate {
        bool newRecord = false;    // strictly beat the previous record
        bool firstRecord = false;  // first record value of this skill (not celebrated)
        float previous = 0.f;
        float value = 0.f;
    };

    SkillTracker() = default;
    explicit SkillTracker(std::string id) : id_(std::move(id)) {}

    // Adds one graded attempt (score already clamped to 0..100 by the caller).
    RecordUpdate add(const AttemptResult& r, const std::string& date, int session);

    const std::string& id() const { return id_; }
    // Stable skill name: the catalog name for ids the coach knows, else the first title of a non-failed
    // attempt (a successful one replaces a provisional one), else a name derived from the id. Result titles
    // are per-attempt headlines ("Flick : raté", "Kickoff (diagonale droite)"), never trusted for known ids.
    std::string title() const;
    const std::string& module() const { return module_; }
    int attempts() const { return attempts_; }
    int successes() const { return successes_; }
    float successRate() const {
        return attempts_ > 0 ? static_cast<float>(successes_) / static_cast<float>(attempts_) : 0.f;
    }
    float averageAll() const { return attempts_ > 0 ? static_cast<float>(scoreSum_ / attempts_) : 0.f; }
    float averageRecent(size_t n = kRecentWindow) const;
    float bestScore() const { return best_; }
    float trendSlope() const;  // points per attempt over the last 20 scores
    Trend trend(float threshold) const;
    // Success rate over the last `n` outcomes; `count` receives how many outcomes were used.
    float recentSuccessRate(size_t n, int* count = nullptr) const;
    const std::vector<float>& scores() const { return scores_; }  // oldest first

    bool hasRecord() const { return hasRecord_; }
    float record() const { return record_; }
    bool lowerIsBetter() const { return lowerIsBetter_; }
    const std::string& recordUnit() const { return recordUnit_; }
    // Record value in the record unit ("1.92 s", "2:04.62", "98.4 km/h").
    std::string formatRecordValue(float v) const;
    // New and previous record with enough decimals to differ ("2:04.62 (avant 2:04.80)").
    void formatRecordPair(float value, float previous, std::string& valueText, std::string& previousText) const;
    std::string recordText() const { return hasRecord_ ? formatRecordValue(record_) : std::string(); }

    const std::string& lastDate() const { return lastDate_; }
    int lastSession() const { return lastSession_; }

    // Most frequent tip code over the last 30 attempts (ties: most recent first).
    MistakeSummary mainMistake() const;
    std::vector<MistakeSummary> mistakes() const;  // ranked, most frequent first
    // How many of the last `lastN` attempts showed `code`.
    int codeCount(const std::string& code, size_t lastN) const;

    // French sentence about the metric that is the most consistently away from its ideal (and that
    // separates failures from successes), empty when nothing stands out.
    std::string metricReason() const;

    // Feedback fading counter (transient, not persisted).
    int& hiddenSuccesses() { return hiddenSuccesses_; }

    void save(Store& st, const std::string& prefix) const;
    // Returns false when the stored entry is unusable (no id).
    bool load(const Store& st, const std::string& prefix);

private:
    struct MetricTrack {
        std::string key, label, unit;
        float ideal = 0.f;
        bool hasIdeal = false;
        std::vector<float> values;  // oldest first, <= kMetricWindow
        std::vector<char> success;  // aligned with values
        int lastSeen = 0;           // attempt number of the latest sample
    };

    void addTips(const std::vector<Tip>& tips);
    void addMetrics(const std::vector<Metric>& metrics, bool success);
    std::string unitForRecord(const AttemptResult& r) const;

    std::string id_;
    std::string title_;       // name from the results (only used for ids outside the catalog)
    bool titleFromSuccess_ = false;  // title_ came from a successful attempt (final)
    std::string module_;
    int attempts_ = 0;
    int successes_ = 0;
    double scoreSum_ = 0.0;
    float best_ = 0.f;
    std::vector<float> scores_;
    std::vector<char> outcomes_;
    bool hasRecord_ = false;
    float record_ = 0.f;
    bool lowerIsBetter_ = false;
    std::string recordUnit_;
    std::string lastDate_;
    int lastSession_ = 0;
    std::vector<std::vector<std::string>> tipWindow_;            // codes per attempt, oldest first
    std::vector<std::pair<std::string, std::string>> tipTexts_;  // code -> latest text, most recent first
    std::vector<MetricTrack> metrics_;
    int hiddenSuccesses_ = 0;
};

} // namespace rlt
