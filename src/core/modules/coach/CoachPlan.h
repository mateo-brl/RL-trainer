#pragma once
// "Plan conseillé": ranks the practised skills by need and recommends a few exercises with a French reason.
//   need = weakness (1 - recent average / 100)
//        + spacing weight x days since last practice (spaced practice, capped)
//        + mistake weight x frequency of the main mistake (only when it repeats)
//        + low-sample bonus (few attempts: the level is still unknown)
// Then a greedy pick favours variety: one exercise per family first, never the same exercise twice, and
// the starter plan fills the remaining slots (also the whole plan for a new player).

#include <string>
#include <vector>

#include "core/modules/coach/CoachTypes.h"

namespace rlt {
namespace coach {

struct PlanCandidate {
    std::string skill;
    std::string title;
    int attempts = 0;
    float recentAverage = 0.f;  // last 20 scores
    float recentSuccess = 0.f;  // last 10 attempts, 0..1
    int daysSince = -1;         // -1 when unknown
    MistakeSummary mistake;
    std::string metricReason;  // SkillTracker::metricReason()
    Trend trend = Trend::Unknown;
};

struct PlanWeights {
    float spacing = 0.08f;  // per day without practice
    int maxSpacingDays = 10;
    float mistake = 0.4f;  // x (count / window) of the main mistake
    int minMistakeCount = 2;
    float lowSample = 0.3f;  // full bonus with 0 attempts, 0 at `lowSampleCount`
    int lowSampleCount = 10;
};

struct NeedParts {
    float weakness = 0.f;
    float spacing = 0.f;
    float mistake = 0.f;
    float sample = 0.f;
    float total() const { return weakness + spacing + mistake + sample; }
};

NeedParts computeNeed(const PlanCandidate& c, const PlanWeights& w);
// French reason built from the dominant part of the need.
std::string planReason(const PlanCandidate& c, const NeedParts& parts);
std::vector<PlanItem> buildPlan(const std::vector<PlanCandidate>& candidates, int size, const PlanWeights& w);

} // namespace coach
} // namespace rlt
