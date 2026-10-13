#include "core/modules/coach/CoachPlan.h"

#include <algorithm>

#include "core/common/Text.h"
#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachUtil.h"

namespace rlt {
namespace coach {

namespace {

std::string daysText(int days) { return std::to_string(days) + (days > 1 ? " jours" : " jour"); }

} // namespace

NeedParts computeNeed(const PlanCandidate& c, const PlanWeights& w) {
    NeedParts p;
    p.weakness = std::clamp(1.f - c.recentAverage / 100.f, 0.f, 1.f);
    if (c.daysSince > 0)
        p.spacing = w.spacing * static_cast<float>(std::min(c.daysSince, std::max(1, w.maxSpacingDays)));
    if (c.mistake.valid() && c.mistake.count >= w.minMistakeCount && c.mistake.window > 0)
        p.mistake = w.mistake * static_cast<float>(c.mistake.count) / static_cast<float>(c.mistake.window);
    if (w.lowSampleCount > 0 && c.attempts < w.lowSampleCount)
        p.sample = w.lowSample * (1.f - static_cast<float>(c.attempts) / static_cast<float>(w.lowSampleCount));
    return p;
}

std::string planReason(const PlanCandidate& c, const NeedParts& p) {
    enum Part { Weakness, Spacing, Mistake, Sample } dominant = Weakness;
    float best = p.weakness;
    if (p.spacing > best) {
        best = p.spacing;
        dominant = Spacing;
    }
    if (p.mistake > best) {
        best = p.mistake;
        dominant = Mistake;
    }
    if (p.sample > best) dominant = Sample;

    std::string r;
    switch (dominant) {
    case Spacing: r = "Pas pratiqué depuis " + daysText(c.daysSince) + " : un rappel aide à mémoriser"; break;
    case Mistake:
        r = "Erreur fréquente (" + std::to_string(c.mistake.count) + " essais sur " + std::to_string(c.mistake.window) +
            ")";
        r += c.mistake.text.empty() ? std::string(" : ") + c.mistake.code : " : " + c.mistake.text;
        break;
    case Sample:
        r = "Seulement " + std::to_string(c.attempts) + (c.attempts > 1 ? " essais" : " essai") +
            " : continue pour mesurer ton niveau";
        break;
    case Weakness:
        if (!c.metricReason.empty()) {
            r = c.metricReason;
        } else {
            r = "Moyenne récente " + formatScore(c.recentAverage) + "/100";
            if (c.trend == Trend::Down) r += ", en baisse";
            if (c.recentSuccess < 0.5f) r += ", réussite " + formatPercent(c.recentSuccess);
        }
        break;
    }
    if (dominant != Spacing && c.daysSince >= 3) r += " (pas pratiqué depuis " + daysText(c.daysSince) + ")";
    return r;
}

std::vector<PlanItem> buildPlan(const std::vector<PlanCandidate>& candidates, int size, const PlanWeights& w) {
    std::vector<PlanItem> plan;
    if (size <= 0) return plan;
    const size_t want = static_cast<size_t>(size);

    std::vector<PlanItem> ranked;
    ranked.reserve(candidates.size());
    std::vector<int> attempts;
    for (const auto& c : candidates) {
        PlanItem it;
        it.launch = launchForSkill(c.skill);
        if (!it.launch.valid()) continue;
        const NeedParts parts = computeNeed(c, w);
        it.skill = c.skill;
        it.title = c.title.empty() ? defaultSkillTitle(c.skill) : c.title;
        it.family = skillFamily(c.skill);
        it.need = parts.total();
        it.reason = planReason(c, parts);
        ranked.push_back(std::move(it));
        attempts.push_back(c.attempts);
    }
    // Highest need first; ties: fewer attempts, then id (deterministic).
    std::vector<size_t> order(ranked.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (ranked[a].need != ranked[b].need) return ranked[a].need > ranked[b].need;
        if (attempts[a] != attempts[b]) return attempts[a] < attempts[b];
        return ranked[a].skill < ranked[b].skill;
    });

    std::vector<std::string> families, keys;
    auto used = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    auto take = [&](const PlanItem& it) {
        plan.push_back(it);
        families.push_back(it.family);
        keys.push_back(it.launch.key());
    };
    // Pass 1: one exercise per family. Pass 2: other exercises of already chosen families.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t idx : order) {
            if (plan.size() >= want) break;
            const PlanItem& it = ranked[idx];
            if (used(keys, it.launch.key())) continue;
            if (pass == 0 && used(families, it.family)) continue;
            take(it);
        }
    }
    // Starter exercises fill the remaining slots (new families first).
    const std::vector<PlanItem> starter = starterPlan();
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& it : starter) {
            if (plan.size() >= want) break;
            if (used(keys, it.launch.key())) continue;
            if (pass == 0 && used(families, it.family)) continue;
            take(it);
        }
    }
    return plan;
}

} // namespace coach
} // namespace rlt
