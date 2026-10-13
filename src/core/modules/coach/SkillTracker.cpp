#include "core/modules/coach/SkillTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachUtil.h"

namespace rlt {

namespace {

template <class T>
void pushBounded(std::vector<T>& v, T value, size_t cap) {
    v.push_back(std::move(value));
    if (v.size() > cap) v.erase(v.begin(), v.begin() + static_cast<long>(v.size() - cap));
}

std::string fixed(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

// Scores keep one decimal; other values keep 5 significant digits.
std::string joinValues(const std::vector<float>& v, bool scores) {
    std::string s;
    char buf[48];
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s.push_back(',');
        if (scores) s += fixed(static_cast<double>(v[i]), 1);
        else {
            std::snprintf(buf, sizeof(buf), "%.5g", static_cast<double>(v[i]));
            s += buf;
        }
    }
    return s;
}

std::vector<float> parseScores(const std::string& s, float lo, float hi, size_t cap) {
    std::vector<float> out;
    if (s.empty()) return out;
    for (const auto& part : coach::split(s, ',')) {
        float f = 0.f;
        if (!parseFloat(part, f)) continue;
        out.push_back(std::clamp(f, lo, hi));
    }
    if (out.size() > cap) out.erase(out.begin(), out.begin() + static_cast<long>(out.size() - cap));
    return out;
}

std::string joinFlags(const std::vector<char>& v) {
    std::string s;
    s.reserve(v.size());
    for (char c : v) s.push_back(c ? '1' : '0');
    return s;
}

std::vector<char> parseFlags(const std::string& s, size_t cap) {
    std::vector<char> out;
    for (char c : s)
        if (c == '0' || c == '1') out.push_back(c == '1' ? 1 : 0);
    if (out.size() > cap) out.erase(out.begin(), out.begin() + static_cast<long>(out.size() - cap));
    return out;
}

} // namespace

// ---- Adding attempts -----------------------------------------------------------------------------------
SkillTracker::RecordUpdate SkillTracker::add(const AttemptResult& r, const std::string& date, int session) {
    RecordUpdate upd;
    const float score = std::isfinite(r.score) ? std::clamp(r.score, 0.f, 100.f) : 0.f;
    // Name for ids outside the catalog: never from a failed attempt ("<mech> : raté", "Balle perdue..."), a
    // provisional one from a non-failed attempt, final from the first success.
    if (!r.title.empty() && r.verdict != Verdict::Fail && (title_.empty() || (!titleFromSuccess_ && r.success))) {
        title_ = coach::sanitizeField(r.title, 60);
        titleFromSuccess_ = r.success;
    }
    if (!r.module.empty()) module_ = coach::sanitizeId(r.module, 32);
    ++attempts_;
    if (r.success) ++successes_;
    scoreSum_ += static_cast<double>(score);
    best_ = attempts_ == 1 ? score : std::max(best_, score);
    pushBounded(scores_, score, kScoreHistory);
    pushBounded(outcomes_, static_cast<char>(r.success ? 1 : 0), kScoreHistory);
    if (!date.empty()) lastDate_ = date;
    lastSession_ = session;

    addTips(r.tips);
    addMetrics(r.metrics, r.success);

    // Personal record: every attempt with a finite record value counts, successful or not (the producers decide
    // what is eligible through hasRecordValue). Only a placeholder is ignored: a failed attempt reporting a value
    // <= 0 (e.g. a drill failed before anything was measured), which would otherwise become an absurd record.
    const bool placeholder = !r.success && std::isfinite(r.valueForRecord) && r.valueForRecord <= 0.f;
    if (r.hasRecordValue && std::isfinite(r.valueForRecord) && !placeholder) {
        const float v = r.valueForRecord;
        upd.value = v;
        if (!hasRecord_ || lowerIsBetter_ != r.lowerIsBetter) {
            hasRecord_ = true;
            lowerIsBetter_ = r.lowerIsBetter;
            record_ = v;
            recordUnit_ = unitForRecord(r);
            upd.firstRecord = true;
        } else if (r.lowerIsBetter ? v < record_ : v > record_) {
            upd.previous = record_;
            upd.newRecord = true;
            record_ = v;
            recordUnit_ = unitForRecord(r);
        }
    }
    return upd;
}

void SkillTracker::addTips(const std::vector<Tip>& tips) {
    // Most important tips first (the producer sorts them, but do not rely on it).
    std::vector<const Tip*> sorted;
    sorted.reserve(tips.size());
    for (const auto& t : tips) sorted.push_back(&t);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Tip* a, const Tip* b) { return a->weight > b->weight; });

    std::vector<std::string> codes;
    for (const Tip* t : sorted) {
        if (codes.size() >= kTipsPerAttempt) break;
        const std::string code = coach::sanitizeId(t->code, 48);
        if (code.empty() || std::find(codes.begin(), codes.end(), code) != codes.end()) continue;
        codes.push_back(code);
        // Latest text for the code, most recent first, bounded.
        auto it = std::find_if(tipTexts_.begin(), tipTexts_.end(), [&](const auto& p) { return p.first == code; });
        if (it != tipTexts_.end()) tipTexts_.erase(it);
        tipTexts_.insert(tipTexts_.begin(), {code, coach::sanitizeField(t->text, 160)});
        if (tipTexts_.size() > kMaxTipTexts) tipTexts_.pop_back();
    }
    pushBounded(tipWindow_, std::move(codes), kTipWindow);
}

void SkillTracker::addMetrics(const std::vector<Metric>& metrics, bool success) {
    for (const auto& m : metrics) {
        if (!std::isfinite(m.value)) continue;
        const std::string key = coach::sanitizeId(m.key.empty() ? m.label : m.key, 32);
        if (key.empty()) continue;
        auto it = std::find_if(metrics_.begin(), metrics_.end(), [&](const MetricTrack& t) { return t.key == key; });
        if (it == metrics_.end()) {
            if (metrics_.size() >= kMaxMetrics) {
                // Forget the metric not seen for the longest time.
                auto oldest = std::min_element(
                    metrics_.begin(), metrics_.end(),
                    [](const MetricTrack& a, const MetricTrack& b) { return a.lastSeen < b.lastSeen; });
                metrics_.erase(oldest);
            }
            metrics_.push_back(MetricTrack{});
            it = metrics_.end() - 1;
            it->key = key;
        }
        it->label = coach::sanitizeField(m.label.empty() ? m.key : m.label, 48);
        it->unit = coach::sanitizeField(m.unit, 12);
        it->hasIdeal = m.hasIdeal && std::isfinite(m.ideal);
        it->ideal = it->hasIdeal ? m.ideal : 0.f;
        it->lastSeen = attempts_;
        pushBounded(it->values, m.value, kMetricWindow);
        pushBounded(it->success, static_cast<char>(success ? 1 : 0), kMetricWindow);
    }
}

std::string SkillTracker::unitForRecord(const AttemptResult& r) const {
    const float v = r.valueForRecord;
    for (const auto& m : r.metrics)
        if (std::fabs(m.value - v) <= 1e-4f * std::max(1.f, std::fabs(v))) return coach::sanitizeField(m.unit, 12);
    return r.lowerIsBetter ? std::string("s") : std::string();
}

// ---- Queries -------------------------------------------------------------------------------------------
std::string SkillTracker::title() const {
    std::string known;
    if (coach::catalogSkillTitle(id_, known)) return known;
    return title_.empty() ? coach::defaultSkillTitle(id_) : title_;
}

float SkillTracker::averageRecent(size_t n) const { return coach::meanOfLast(scores_, n); }

float SkillTracker::trendSlope() const {
    if (scores_.size() <= kRecentWindow) return coach::linearSlope(scores_);
    const std::vector<float> last(scores_.end() - static_cast<long>(kRecentWindow), scores_.end());
    return coach::linearSlope(last);
}

Trend SkillTracker::trend(float threshold) const {
    return coach::classifyTrend(trendSlope(), std::min(scores_.size(), kRecentWindow), threshold);
}

float SkillTracker::recentSuccessRate(size_t n, int* count) const {
    const size_t used = std::min(n, outcomes_.size());
    if (count) *count = static_cast<int>(used);
    if (used == 0) return 0.f;
    int ok = 0;
    for (size_t i = outcomes_.size() - used; i < outcomes_.size(); ++i) ok += outcomes_[i] ? 1 : 0;
    return static_cast<float>(ok) / static_cast<float>(used);
}

std::string SkillTracker::formatRecordValue(float v) const { return coach::formatRecord(v, recordUnit_); }

void SkillTracker::formatRecordPair(float value, float previous, std::string& valueText,
                                    std::string& previousText) const {
    coach::formatRecordPair(value, previous, recordUnit_, valueText, previousText);
}

std::vector<MistakeSummary> SkillTracker::mistakes() const {
    struct Count {
        std::string code;
        int count = 0;
        size_t last = 0;  // index of the latest attempt with the code
        size_t rank = 0;  // position in that attempt (0 = heaviest tip)
    };
    std::vector<Count> counts;
    for (size_t i = 0; i < tipWindow_.size(); ++i) {
        for (size_t k = 0; k < tipWindow_[i].size(); ++k) {
            const std::string& code = tipWindow_[i][k];
            auto it = std::find_if(counts.begin(), counts.end(), [&](const Count& c) { return c.code == code; });
            if (it == counts.end()) counts.push_back({code, 1, i, k});
            else {
                ++it->count;
                it->last = i;
                it->rank = k;
            }
        }
    }
    // Most frequent first; ties: seen most recently, then the heavier tip of that attempt, then the code.
    std::sort(counts.begin(), counts.end(), [](const Count& a, const Count& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.last != b.last) return a.last > b.last;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.code < b.code;
    });
    std::vector<MistakeSummary> out;
    out.reserve(counts.size());
    for (const auto& c : counts) {
        MistakeSummary m;
        m.code = c.code;
        m.count = c.count;
        m.window = static_cast<int>(tipWindow_.size());
        auto it = std::find_if(tipTexts_.begin(), tipTexts_.end(), [&](const auto& p) { return p.first == c.code; });
        if (it != tipTexts_.end()) m.text = it->second;
        out.push_back(std::move(m));
    }
    return out;
}

MistakeSummary SkillTracker::mainMistake() const {
    auto all = mistakes();
    return all.empty() ? MistakeSummary{} : all.front();
}

int SkillTracker::codeCount(const std::string& code, size_t lastN) const {
    const size_t used = std::min(lastN, tipWindow_.size());
    int n = 0;
    for (size_t i = tipWindow_.size() - used; i < tipWindow_.size(); ++i)
        if (std::find(tipWindow_[i].begin(), tipWindow_[i].end(), code) != tipWindow_[i].end()) ++n;
    return n;
}

std::string SkillTracker::metricReason() const {
    const MetricTrack* best = nullptr;
    float bestScore = 0.f;
    float bestMean = 0.f;
    for (const auto& m : metrics_) {
        if (!m.hasIdeal || m.values.size() < 5) continue;
        double signedSum = 0.0, absSum = 0.0, okAbs = 0.0, koAbs = 0.0;
        int nOk = 0, nKo = 0;
        for (size_t i = 0; i < m.values.size(); ++i) {
            const double d = static_cast<double>(m.values[i]) - static_cast<double>(m.ideal);
            signedSum += d;
            absSum += std::fabs(d);
            if (i < m.success.size() && m.success[i]) {
                okAbs += std::fabs(d);
                ++nOk;
            } else {
                koAbs += std::fabs(d);
                ++nKo;
            }
        }
        const double n = static_cast<double>(m.values.size());
        const double meanAbs = absSum / n;
        if (meanAbs <= 1e-6) continue;
        // Bias: 1 when every value is on the same side of the ideal.
        const double bias = std::fabs(signedSum / n) / meanAbs;
        if (bias < 0.6) continue;
        // Does this metric separate failures from successes? Unknown (assumed yes) without both kinds.
        double ratio = 1.5;
        if (nOk >= 2 && nKo >= 2) {
            const double okMean = okAbs / nOk, koMean = koAbs / nKo;
            ratio = koMean / std::max(okMean, 1e-3 * std::max(1.0, std::fabs(static_cast<double>(m.ideal))));
        }
        if (ratio < 1.3) continue;
        const float score = static_cast<float>(bias * std::min(ratio, 5.0));
        if (score > bestScore) {
            bestScore = score;
            best = &m;
            bestMean = static_cast<float>(signedSum / n) + m.ideal;
        }
    }
    if (!best) return std::string();
    const bool zeroIdeal = std::fabs(best->ideal) < 1e-6f;
    return best->label + " : " + coach::formatValue(bestMean, best->unit, zeroIdeal) + " en moyenne, vise " +
           coach::formatValue(best->ideal, best->unit);
}

// ---- Persistence ---------------------------------------------------------------------------------------
void SkillTracker::save(Store& st, const std::string& p) const {
    st.setString(p + "id", id_);
    st.setString(p + "title", title_);
    st.setBool(p + "titleok", titleFromSuccess_);
    st.setString(p + "module", module_);
    st.setInt(p + "att", attempts_);
    st.setInt(p + "succ", successes_);
    st.setString(p + "sum", fixed(scoreSum_, 2));
    st.setFloat(p + "best", best_);
    st.setString(p + "scores", joinValues(scores_, true));
    st.setString(p + "outcomes", joinFlags(outcomes_));
    st.setBool(p + "rec", hasRecord_);
    if (hasRecord_) {
        st.setFloat(p + "recv", record_);
        st.setBool(p + "lib", lowerIsBetter_);
        st.setString(p + "unit", recordUnit_);
    }
    st.setString(p + "date", lastDate_);
    st.setInt(p + "sess", lastSession_);

    // Tip window: attempts separated by ';', codes by ','.
    std::string tips;
    for (size_t i = 0; i < tipWindow_.size(); ++i) {
        if (i) tips.push_back(';');
        for (size_t k = 0; k < tipWindow_[i].size(); ++k) {
            if (k) tips.push_back(',');
            tips += tipWindow_[i][k];
        }
    }
    st.setInt(p + "tipn", static_cast<long long>(tipWindow_.size()));
    st.setString(p + "tips", tips);
    std::vector<std::string> texts;
    for (const auto& t : tipTexts_) texts.push_back(t.first + "\t" + t.second);
    st.setList(p + "tt", texts);

    std::vector<std::string> metrics;
    for (const auto& m : metrics_) {
        metrics.push_back(m.key + "\t" + m.label + "\t" + m.unit + "\t" + fixed(static_cast<double>(m.ideal), 4) +
                          "\t" + (m.hasIdeal ? "1" : "0") + "\t" + std::to_string(m.lastSeen) + "\t" +
                          joinValues(m.values, false) + "\t" + joinFlags(m.success));
    }
    st.setList(p + "m", metrics);
}

bool SkillTracker::load(const Store& st, const std::string& p) {
    *this = SkillTracker{};
    id_ = coach::sanitizeId(st.getString(p + "id"), 64);
    if (id_.empty()) return false;
    title_ = coach::sanitizeField(st.getString(p + "title"), 60);
    titleFromSuccess_ = !title_.empty() && st.getBool(p + "titleok", false);
    module_ = coach::sanitizeId(st.getString(p + "module"), 32);
    attempts_ = static_cast<int>(std::clamp<long long>(st.getInt(p + "att", 0), 0, 100000000));
    successes_ = static_cast<int>(std::clamp<long long>(st.getInt(p + "succ", 0), 0, attempts_));
    float sum = 0.f;
    scoreSum_ =
        parseFloat(st.getString(p + "sum"), sum) ? std::clamp(static_cast<double>(sum), 0.0, 100.0 * attempts_) : 0.0;
    best_ = std::clamp(st.getFloat(p + "best", 0.f), 0.f, 100.f);
    scores_ = parseScores(st.getString(p + "scores"), 0.f, 100.f, kScoreHistory);
    outcomes_ = parseFlags(st.getString(p + "outcomes"), kScoreHistory);
    // Older/edited files: keep at least as many attempts as the history shows.
    attempts_ = std::max(attempts_, static_cast<int>(scores_.size()));
    hasRecord_ = st.getBool(p + "rec", false) && st.has(p + "recv");
    if (hasRecord_) {
        record_ = st.getFloat(p + "recv", 0.f);
        lowerIsBetter_ = st.getBool(p + "lib", false);
        recordUnit_ = coach::sanitizeField(st.getString(p + "unit"), 12);
    }
    const std::string date = st.getString(p + "date");
    lastDate_ = coach::validDate(date) ? date : std::string();
    lastSession_ = static_cast<int>(std::clamp<long long>(st.getInt(p + "sess", 0), 0, 100000000));

    const long long tipn = std::clamp<long long>(st.getInt(p + "tipn", 0), 0, static_cast<long long>(kTipWindow) * 4);
    if (tipn > 0) {
        auto attempts = coach::split(st.getString(p + "tips"), ';');
        for (const auto& a : attempts) {
            std::vector<std::string> codes;
            if (!a.empty()) {
                for (const auto& c : coach::split(a, ',')) {
                    const std::string code = coach::sanitizeId(c, 48);
                    if (!code.empty() && codes.size() < kTipsPerAttempt) codes.push_back(code);
                }
            }
            pushBounded(tipWindow_, std::move(codes), kTipWindow);
        }
    }
    for (const auto& e : st.getList(p + "tt")) {
        if (tipTexts_.size() >= kMaxTipTexts) break;
        const size_t tab = e.find('\t');
        if (tab == std::string::npos) continue;
        const std::string code = coach::sanitizeId(e.substr(0, tab), 48);
        if (code.empty()) continue;
        tipTexts_.push_back({code, coach::sanitizeField(e.substr(tab + 1), 160)});
    }
    for (const auto& e : st.getList(p + "m")) {
        if (metrics_.size() >= kMaxMetrics) break;
        const auto f = coach::split(e, '\t');
        if (f.size() != 8) continue;
        MetricTrack m;
        m.key = coach::sanitizeId(f[0], 32);
        if (m.key.empty()) continue;
        m.label = coach::sanitizeField(f[1], 48);
        m.unit = coach::sanitizeField(f[2], 12);
        float ideal = 0.f;
        m.hasIdeal = f[4] == "1" && parseFloat(f[3], ideal);
        m.ideal = m.hasIdeal ? ideal : 0.f;
        float seen = 0.f;
        m.lastSeen = parseFloat(f[5], seen) ? static_cast<int>(seen) : 0;
        m.values = parseScores(f[6], -1.0e7f, 1.0e7f, kMetricWindow);
        m.success = parseFlags(f[7], kMetricWindow);
        m.success.resize(m.values.size(), 0);
        metrics_.push_back(std::move(m));
    }
    return true;
}

} // namespace rlt
