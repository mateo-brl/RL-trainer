#pragma once
// Common result types: every graded attempt (a touch, a speedflip, a kickoff, a ring course...) is reported
// to the engine as an AttemptResult. The coach module aggregates them (history, records, weaknesses, plan).

#include <string>
#include <vector>

namespace rlt {

struct Metric {
    std::string key;    // stable id, e.g. "speed_diff"
    std::string label;  // French label, e.g. "Écart de vitesse"
    float value = 0.f;
    std::string unit;   // "uu/s", "ms", "°", "%", "s" ...
    float ideal = 0.f;  // optional reference value (0 when unused)
    bool hasIdeal = false;
};

// A coaching tip. `code` is a stable id used to count recurring mistakes (e.g. "ft.too_fast").
struct Tip {
    std::string code;
    std::string text;   // French, one short sentence, imperative ("Relâche le boost avant le contact.")
    float weight = 1.f; // how many points this mistake cost (used to rank tips)
};

enum class Verdict : int { None = 0, Fail = 1, Ok = 2, Good = 3, Perfect = 4 };

inline Verdict verdictFromScore(float score) {
    if (score >= 90.f) return Verdict::Perfect;
    if (score >= 70.f) return Verdict::Good;
    if (score >= 45.f) return Verdict::Ok;
    return Verdict::Fail;
}

inline const char* verdictLabel(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return "Parfait";
    case Verdict::Good: return "Bien";
    case Verdict::Ok: return "Correct";
    case Verdict::Fail: return "Raté";
    default: return "";
    }
}

struct AttemptResult {
    std::string module;     // "firsttouch", "mechanics", "kickoff", "courses", ...
    std::string skill;      // stable skill id, e.g. "first_touch.ground", "speedflip", "kickoff.diagonal"
    std::string title;      // French display name, e.g. "Speedflip"
    float score = 0.f;      // 0..100
    bool success = false;   // did the attempt meet the drill/mechanic goal
    Verdict verdict = Verdict::None;
    std::vector<Metric> metrics;
    std::vector<Tip> tips;  // sorted by weight, most important first
    float time = 0.f;       // game time of the attempt (Snapshot::time)
    float valueForRecord = 0.f;  // value compared for personal bests (e.g. time in s, or score)
    bool lowerIsBetter = false;  // true for times
    bool hasRecordValue = false;
};

} // namespace rlt
