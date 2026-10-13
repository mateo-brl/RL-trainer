#pragma once
// Kismet timing profiles of known map families (which named variables tell "run started", "level passed",
// "course finished") and generic heuristics for unknown custom maps. Pure logic on KismetVar snapshots.
// Variable names come from the SpeedrunTools auto-splitters (2022); maps can change them in updates, so
// every id match is validated against the live variables and falls back to a signature match.

#include <map>
#include <string>
#include <vector>

#include "core/common/Context.h"

namespace rlt {

struct KismetVarSpec {
    std::string name;
    KismetVar::Type type = KismetVar::Type::Int;
};

// A timing profile: which variables drive the run timer. Also used for the player's own "pins".
struct KismetProfile {
    std::string key;                 // stable id ("leth_rings", "pin", ...)
    std::string label;               // French display name
    MapCategory category = MapCategory::Other;
    std::vector<std::string> ids;    // workshop ids this profile is known for
    bool bySignature = true;         // may be chosen for unknown maps when all its variables exist

    std::string startBool;           // false -> true starts a run, true -> false ends it
    std::string levelInt;            // +1 = level split
    std::string counterInt;          // ring counter: becomes 1 = run start, +1 = split
    std::string timerString;         // map's own timer text; becomes "0:00"-like = run start
    std::string deathsInt;           // optional, display + tips
    std::string secondsVar;          // optional, map's seconds counter (display)
    std::vector<KismetVarSpec> extraSignature;  // other variables required for a signature match

    int finalLevel = 0;              // level (or counter) value of the last level, 0 = unknown
    bool finishOnWrap = false;       // level goes back to 1 after the final level => finished
    bool finishOnReach = false;      // level/counter reaches finalLevel => finished
    bool finishOnGoal = false;       // goal while at the final level (or final unknown) => finished
    bool finishOnFall = false;       // start bool true -> false ends the run as finished (no level to check)

    // Something to time with at all.
    bool usable() const {
        return !startBool.empty() || !levelInt.empty() || !counterInt.empty() || !timerString.empty();
    }
    // The map itself tells when the run starts (otherwise own timing starts it).
    bool hasKismetStart() const { return !startBool.empty() || !counterInt.empty() || !timerString.empty(); }
    // Variables (with types) that must exist for this profile to apply.
    std::vector<KismetVarSpec> signature() const;
};

// Built-in profiles, most specific first.
const std::vector<KismetProfile>& kismetProfiles();
const KismetProfile* findKismetProfile(const std::string& key);
// Profile known for this workshop id (no validation).
const KismetProfile* kismetProfileById(const std::string& workshopId);
// Every variable of the profile's signature exists with the right type.
bool kismetSignatureMatches(const KismetProfile& p, const std::vector<KismetVar>& vars);
// Best profile (most variables matched) among those allowed by signature. nullptr when none.
const KismetProfile* kismetProfileBySignature(const std::vector<KismetVar>& vars);

// Lookup helpers (linear: maps have tens of variables, called at ~30 Hz).
const KismetVar* findKismetVar(const std::vector<KismetVar>& vars, const std::string& name);
bool kismetInt(const std::vector<KismetVar>& vars, const std::string& name, int& out);
bool kismetBool(const std::vector<KismetVar>& vars, const std::string& name, bool& out);
bool kismetString(const std::vector<KismetVar>& vars, const std::string& name, std::string& out);
// Int or float as a number.
bool kismetNumber(const std::vector<KismetVar>& vars, const std::string& name, float& out);
std::string kismetValueText(const KismetVar& v);
const char* kismetTypeLabel(KismetVar::Type t);  // "int", "float", "bool", "texte", "autre"

// Map timer text: "m:ss", "mm:ss", "h:mm:ss", optional ".cc" fraction. Seconds in `out`.
bool parseTimerText(const std::string& text, float& out);
bool looksLikeTimer(const std::string& text);
// Name suggests a level / checkpoint counter ("Level", "Checkpoint", "Stage", "Rings", "CP"...).
bool isLevelLikeName(const std::string& name);

// Generic heuristics on any custom map: watches every variable and counts typical patterns.
struct KismetCandidate {
    std::string name;
    KismetVar::Type type = KismetVar::Type::Other;
    std::string value;        // current value as text
    int intValue = 0;
    float floatValue = 0.f;
    bool boolValue = false;
    int plusOneSteps = 0;     // int stepped by exactly +1
    int otherChanges = 0;     // any other change
    int resets = 0;           // int went back to 0 or 1 from a higher value
    int risingEdges = 0;      // bool false -> true
    int fallingEdges = 0;     // bool true -> false
    bool timerLike = false;   // string currently looks like m:ss
    int timerTicks = 0;       // timer string changed while looking like a timer
    float lastChange = -1.f;  // game time of the last change
};

class KismetWatcher {
public:
    void reset();
    // Feeds one snapshot of all variables (game time `now`).
    void update(const std::vector<KismetVar>& vars, float now);

    const std::vector<KismetCandidate>& candidates() const { return candidates_; }
    const KismetCandidate* find(const std::string& name) const;
    size_t variableCount() const { return candidates_.size(); }
    int samples() const { return samples_; }

    // Best level/checkpoint counter: an int with +1 steps and a level-like name (or any name when
    // `anyName`), fewer erratic changes. nullptr when none qualifies yet.
    const KismetCandidate* bestLevelCandidate(bool anyName = false) const;
    // String variable that looks like a running timer (changed at least twice).
    const KismetCandidate* timerCandidate() const;
    // Bool with at least one false -> true edge, most edges first.
    const KismetCandidate* startCandidate() const;

private:
    std::vector<KismetCandidate> candidates_;
    std::map<std::string, size_t> index_;
    int samples_ = 0;
};

} // namespace rlt
