#pragma once
// Session goals (Locke & Latham: specific goals with feedback beat vague ones): presets, custom goals parsed
// from command arguments, matching of results and persistence of the goal definitions.

#include <string>
#include <vector>

#include "core/modules/coach/CoachTypes.h"

namespace rlt {
namespace coach {

constexpr size_t kMaxGoals = 3;

struct GoalPreset {
    const char* id;
    GoalKind kind;
    const char* scope;  // "" = any skill
    int target;
    float minScore;
    const char* label;  // French
};

const std::vector<GoalPreset>& goalPresets();
// Builds a goal from a preset id ("speedflip10"). False for unknown ids.
bool makePresetGoal(const std::string& id, Goal& out);

// Custom goal from command arguments: <attempts|success|score|record> <scope|any> <target> [minScore].
// `scopeTitle` names the scope in the label (empty: derived from the id). `error` receives a French message.
bool parseCustomGoal(const std::vector<std::string>& args, Goal& out, std::string& error,
                     const std::string& scopeTitle = std::string());
std::string customGoalLabel(GoalKind kind, const std::string& scope, int target, float minScore,
                            const std::string& scopeTitle);
// "Premières touches" for "first_touch", "Parcours d'anneaux" for "course.rings", skill titles otherwise.
std::string scopeLabel(const std::string& scope);

// "" / "any" / "*" match everything; otherwise the exact id or any id below the prefix ("course.rings" matches
// "course.rings.air" but not "course.ringsX").
bool scopeMatches(const std::string& scope, const std::string& skill);

// Counts a result towards a goal (no-op once done). Returns true when the progress changed.
bool applyResultToGoal(Goal& g, const std::string& skill, float score, bool success, bool newRecord);

std::string encodeGoal(const Goal& g);
bool decodeGoal(const std::string& line, Goal& out);

} // namespace coach
} // namespace rlt
