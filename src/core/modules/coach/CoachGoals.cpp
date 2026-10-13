#include "core/modules/coach/CoachGoals.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachUtil.h"

namespace rlt {
namespace coach {

const std::vector<GoalPreset>& goalPresets() {
    static const std::vector<GoalPreset> presets = {
        {"ft70", GoalKind::ScoreAtLeast, "first_touch", 20, 70.f, "20 premières touches notées 70 ou plus"},
        {"speedflip10", GoalKind::Successes, "mech.speedflip", 10, 0.f, "10 speedflips réussis"},
        {"kickoff_pb", GoalKind::Record, "kickoff", 1, 0.f, "Battre ton record de kickoff"},
        {"attempts50", GoalKind::Attempts, "", 50, 0.f, "50 essais dans la session"},
        {"score80", GoalKind::ScoreAtLeast, "", 15, 80.f, "15 essais notés 80 ou plus"},
        {"record1", GoalKind::Record, "", 1, 0.f, "Battre un de tes records"},
        {"kickoff10", GoalKind::ScoreAtLeast, "kickoff", 10, 70.f, "10 kickoffs notés 70 ou plus"},
        {"halfflip10", GoalKind::Successes, "mech.halfflip", 10, 0.f, "10 half-flips réussis"},
        {"wavedash10", GoalKind::Successes, "mech.wavedash", 10, 0.f, "10 wavedashs réussis"},
        {"aerial10", GoalKind::Successes, "ball.aerial", 10, 0.f, "10 aériennes réussies"},
        {"airdribble5", GoalKind::Successes, "ball.airdribble", 5, 0.f, "5 air dribbles réussis"},
        {"rings3", GoalKind::Successes, "course.rings", 3, 0.f, "3 parcours d'anneaux réussis"},
        {"control5", GoalKind::Successes, "course.control", 5, 0.f, "5 parcours de contrôle réussis"},
        {"flick10", GoalKind::Successes, "shoot.flick", 10, 0.f, "10 flicks réussis"},
    };
    return presets;
}

bool makePresetGoal(const std::string& id, Goal& out) {
    for (const auto& p : goalPresets()) {
        if (id != p.id) continue;
        Goal g;
        g.kind = p.kind;
        g.scope = p.scope;
        g.target = p.target;
        g.minScore = p.minScore;
        g.label = p.label;
        g.preset = p.id;
        out = g;
        return true;
    }
    return false;
}

std::string scopeLabel(const std::string& scope) {
    if (scope.empty() || scope == "any" || scope == "*") return "tous les exercices";
    if (scope.find('.') == std::string::npos) return familyLabel(scope);
    if (scope == "course.rings") return "Parcours d'anneaux";
    if (scope == "course.control") return "Parcours de contrôle";
    if (scope == "shoot.flick") return "Flicks";
    if (scope == "shoot.target") return "Tirs sur cibles";
    return defaultSkillTitle(scope);
}

std::string customGoalLabel(GoalKind kind, const std::string& scope, int target, float minScore,
                            const std::string& scopeTitle) {
    const bool any = scope.empty() || scope == "any" || scope == "*";
    const std::string what = scopeTitle.empty() ? scopeLabel(scope) : scopeTitle;
    const std::string n = std::to_string(target);
    std::string label;
    switch (kind) {
    case GoalKind::Attempts: label = n + (target > 1 ? " essais" : " essai"); break;
    case GoalKind::Successes: label = n + (target > 1 ? " réussites" : " réussite"); break;
    case GoalKind::ScoreAtLeast:
        label = n + (target > 1 ? " essais notés " : " essai noté ") + formatScore(minScore) + " ou plus";
        break;
    case GoalKind::Record:
        label = target > 1 ? "Battre " + n + " records"
                           : (any ? std::string("Battre un de tes records") : std::string("Battre ton record"));
        break;
    }
    if (!any) label += " : " + what;
    else if (kind == GoalKind::Attempts) label += " dans la session";
    return label;
}

bool parseCustomGoal(const std::vector<std::string>& args, Goal& out, std::string& error,
                     const std::string& scopeTitle) {
    if (args.size() < 3) {
        error = "Format : goal_add custom <attempts|success|score|record> <compétence|any> <nombre> [note mini]";
        return false;
    }
    Goal g;
    const std::string& k = args[0];
    if (k == "attempts" || k == "essais") g.kind = GoalKind::Attempts;
    else if (k == "success" || k == "successes" || k == "reussites") g.kind = GoalKind::Successes;
    else if (k == "score" || k == "note") g.kind = GoalKind::ScoreAtLeast;
    else if (k == "record" || k == "records") g.kind = GoalKind::Record;
    else {
        error = "Type d'objectif inconnu : " + k + " (attempts, success, score ou record).";
        return false;
    }
    const std::string scope = args[1] == "any" || args[1] == "*" ? std::string() : sanitizeId(args[1], 64);
    if (scope.empty() && args[1] != "any" && args[1] != "*") {
        error = "Compétence invalide : " + args[1] + ".";
        return false;
    }
    char* end = nullptr;
    const long target = std::strtol(args[2].c_str(), &end, 10);
    if (args[2].empty() || end != args[2].c_str() + args[2].size() || target < 1 || target > 1000) {
        error = "Nombre invalide : " + args[2] + " (entre 1 et 1000).";
        return false;
    }
    g.scope = scope;
    g.target = static_cast<int>(target);
    if (g.kind == GoalKind::ScoreAtLeast) {
        g.minScore = 70.f;
        if (args.size() >= 4) {
            const double s = std::strtod(args[3].c_str(), &end);
            if (args[3].empty() || end != args[3].c_str() + args[3].size() || !std::isfinite(s) || s < 0.0 ||
                s > 100.0) {
                error = "Note minimale invalide : " + args[3] + " (entre 0 et 100).";
                return false;
            }
            g.minScore = static_cast<float>(s);
        }
    }
    g.preset = "custom";
    g.label = customGoalLabel(g.kind, g.scope, g.target, g.minScore, scopeTitle);
    out = g;
    return true;
}

bool scopeMatches(const std::string& scope, const std::string& skill) {
    if (scope.empty() || scope == "any" || scope == "*") return true;
    if (skill == scope) return true;
    return skill.size() > scope.size() && skill.compare(0, scope.size(), scope) == 0 && skill[scope.size()] == '.';
}

bool applyResultToGoal(Goal& g, const std::string& skill, float score, bool success, bool newRecord) {
    if (g.done || !scopeMatches(g.scope, skill)) return false;
    bool counts = false;
    switch (g.kind) {
    case GoalKind::Attempts: counts = true; break;
    case GoalKind::Successes: counts = success; break;
    case GoalKind::ScoreAtLeast: counts = score >= g.minScore; break;
    case GoalKind::Record: counts = newRecord; break;
    }
    if (!counts) return false;
    ++g.progress;
    if (g.progress >= g.target) {
        g.progress = g.target;
        g.done = true;
    }
    return true;
}

std::string encodeGoal(const Goal& g) {
    return std::to_string(static_cast<int>(g.kind)) + "\t" + g.scope + "\t" + std::to_string(g.target) + "\t" +
           formatScore(g.minScore) + "\t" + sanitizeField(g.label, 80) + "\t" + sanitizeId(g.preset, 24);
}

bool decodeGoal(const std::string& line, Goal& out) {
    const auto f = split(line, '\t');
    if (f.size() != 6) return false;
    char* end = nullptr;
    const long kind = std::strtol(f[0].c_str(), &end, 10);
    if (f[0].empty() || *end != '\0' || kind < 0 || kind > 3) return false;
    const long target = std::strtol(f[2].c_str(), &end, 10);
    if (f[2].empty() || *end != '\0' || target < 1 || target > 1000) return false;
    const double minScore = std::strtod(f[3].c_str(), &end);
    if (f[3].empty() || *end != '\0' || !std::isfinite(minScore)) return false;
    Goal g;
    g.kind = static_cast<GoalKind>(kind);
    g.scope = sanitizeId(f[1], 64);
    g.target = static_cast<int>(target);
    g.minScore = static_cast<float>(std::clamp(minScore, 0.0, 100.0));
    g.label = sanitizeField(f[4], 80);
    g.preset = sanitizeId(f[5], 24);
    if (g.label.empty()) g.label = customGoalLabel(g.kind, g.scope, g.target, g.minScore, std::string());
    out = g;
    return true;
}

} // namespace coach
} // namespace rlt
