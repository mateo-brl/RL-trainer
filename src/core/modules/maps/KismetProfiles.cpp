#include "core/modules/maps/KismetProfiles.h"

#include <cmath>
#include <cstdio>

#include "core/modules/maps/MapCatalog.h"

namespace rlt {

namespace {
constexpr size_t kMaxValueText = 64;  // long strings (map logs) are cut for display and comparison
} // namespace

std::vector<KismetVarSpec> KismetProfile::signature() const {
    std::vector<KismetVarSpec> s;
    if (!startBool.empty()) s.push_back({startBool, KismetVar::Type::Bool});
    if (!levelInt.empty()) s.push_back({levelInt, KismetVar::Type::Int});
    if (!counterInt.empty()) s.push_back({counterInt, KismetVar::Type::Int});
    if (!timerString.empty()) s.push_back({timerString, KismetVar::Type::String});
    for (const auto& e : extraSignature) s.push_back(e);
    return s;
}

namespace {

std::vector<KismetProfile> buildProfiles() {
    using C = MapCategory;
    using T = KismetVar::Type;
    std::vector<KismetProfile> v;
    {
        // Lethamyr's rings maps (Giant, Neon verified in SpeedrunTools; Tiny/Lava/Medieval assumed identical).
        KismetProfile p;
        p.key = "leth_rings";
        p.label = "Anneaux de Lethamyr";
        p.category = C::Rings;
        p.ids = {"2025564083", "2541758604", "2104185189", "2468375059", "2815095306"};
        p.startBool = "Timing";
        p.levelInt = "Level";
        p.deathsInt = "Deaths";
        p.secondsVar = "Seconds";
        p.extraSignature = {{"Deaths", T::Int}};
        p.finalLevel = 20;
        p.finishOnWrap = true;
        v.push_back(p);
    }
    {
        // Speed Jump: Rings 3 [Timer Update]: map timer + 14 level splits.
        KismetProfile p;
        p.key = "sjr3";
        p.label = "Speed Jump: Rings 3";
        p.category = C::Rings;
        p.ids = {"2779629941", "1671658424"};
        p.timerString = "displaytimer";
        p.levelInt = "Level";
        p.finalLevel = 15;
        p.finishOnReach = true;
        v.push_back(p);
    }
    {
        // Air Dribble Hoops: "Timer" bool set by the speedrun mode, 21 levels.
        KismetProfile p;
        p.key = "ad_hoops";
        p.label = "Air Dribble Hoops";
        p.category = C::AirDribble;
        p.ids = {"2100268170"};
        p.startBool = "Timer";
        p.levelInt = "Level";
        p.finalLevel = 21;
        p.finishOnWrap = true;
        p.finishOnGoal = true;
        v.push_back(p);
    }
    {
        // Panic's Air Race: checkpoint counter (+ ring counter), start by own timing.
        KismetProfile p;
        p.key = "panic_air_race";
        p.label = "Panic's Air Race";
        p.category = C::Rings;
        p.ids = {"2204381361"};
        p.levelInt = "Player1CPCount";
        p.extraSignature = {{"Player1Count", T::Int}};
        v.push_back(p);
    }
    {
        // Dribble 2 Overhaul: Timing + Level (30 levels), last goal ends the run. Same variables as the
        // generic family below, so only matched by workshop id.
        KismetProfile p;
        p.key = "dribble2_overhaul";
        p.label = "Dribble 2 Overhaul";
        p.category = C::Dribble;
        p.ids = {"1694288506"};
        p.bySignature = false;
        p.startBool = "Timing";
        p.levelInt = "Level";
        p.finalLevel = 30;
        p.finishOnGoal = true;
        p.finishOnWrap = true;
        v.push_back(p);
    }
    {
        KismetProfile p;
        // Unknown map with the usual Timing + Level pair: splits only. Without a known last level, no event
        // proves the course is finished, so runs end as abandoned unless the player pins the final level.
        p.key = "timing_level";
        p.label = "Générique (Timing + Level)";
        p.category = C::Other;
        p.startBool = "Timing";
        p.levelInt = "Level";
        v.push_back(p);
    }
    {
        // Speed Jump: Rings 2 [Timer Update]: map timer only ("00:00" = start).
        KismetProfile p;
        p.key = "sjr2";
        p.label = "Speed Jump: Rings 2";
        p.category = C::Rings;
        p.ids = {"2734626907", "1454827766"};
        p.timerString = "displaytimer";
        v.push_back(p);
    }
    {
        // Speed Jump: Rings 1: ring counter, run starts when it becomes 1, 162 rings.
        KismetProfile p;
        p.key = "sjr1";
        p.label = "Speed Jump: Rings 1";
        p.category = C::Rings;
        p.ids = {"1768760178"};
        p.counterInt = "Rings";
        p.finalLevel = 162;
        p.finishOnReach = true;
        v.push_back(p);
    }
    {
        // Lethamyr's Egyptian Tomb: ankh counter as checkpoints.
        KismetProfile p;
        p.key = "egyptian_tomb";
        p.label = "Egyptian Tomb (ankhs)";
        p.category = C::Obstacle;
        p.levelInt = "ankh";
        v.push_back(p);
    }
    return v;
}

} // namespace

const std::vector<KismetProfile>& kismetProfiles() {
    static const std::vector<KismetProfile> profiles = buildProfiles();
    return profiles;
}

const KismetProfile* findKismetProfile(const std::string& key) {
    for (const auto& p : kismetProfiles())
        if (p.key == key) return &p;
    return nullptr;
}

const KismetProfile* kismetProfileById(const std::string& workshopId) {
    if (workshopId.empty()) return nullptr;
    for (const auto& p : kismetProfiles())
        for (const auto& id : p.ids)
            if (id == workshopId) return &p;
    return nullptr;
}

bool kismetSignatureMatches(const KismetProfile& p, const std::vector<KismetVar>& vars) {
    const auto sig = p.signature();
    if (sig.empty()) return false;
    for (const auto& s : sig) {
        const KismetVar* v = findKismetVar(vars, s.name);
        if (!v || v->type != s.type) return false;
    }
    return true;
}

const KismetProfile* kismetProfileBySignature(const std::vector<KismetVar>& vars) {
    const KismetProfile* best = nullptr;
    size_t bestSize = 0;
    for (const auto& p : kismetProfiles()) {
        if (!p.bySignature) continue;
        if (!kismetSignatureMatches(p, vars)) continue;
        const size_t n = p.signature().size();
        if (n > bestSize) {
            best = &p;
            bestSize = n;
        }
    }
    return best;
}

// ---- Variable access -----------------------------------------------------------------------------------
const KismetVar* findKismetVar(const std::vector<KismetVar>& vars, const std::string& name) {
    if (name.empty()) return nullptr;
    for (const auto& v : vars)
        if (v.name == name) return &v;
    return nullptr;
}

bool kismetInt(const std::vector<KismetVar>& vars, const std::string& name, int& out) {
    const KismetVar* v = findKismetVar(vars, name);
    if (!v || v->type != KismetVar::Type::Int) return false;
    out = v->i;
    return true;
}

bool kismetBool(const std::vector<KismetVar>& vars, const std::string& name, bool& out) {
    const KismetVar* v = findKismetVar(vars, name);
    if (!v || v->type != KismetVar::Type::Bool) return false;
    out = v->b;
    return true;
}

bool kismetString(const std::vector<KismetVar>& vars, const std::string& name, std::string& out) {
    const KismetVar* v = findKismetVar(vars, name);
    if (!v || v->type != KismetVar::Type::String) return false;
    out = v->s;
    return true;
}

bool kismetNumber(const std::vector<KismetVar>& vars, const std::string& name, float& out) {
    const KismetVar* v = findKismetVar(vars, name);
    if (!v) return false;
    if (v->type == KismetVar::Type::Int) out = static_cast<float>(v->i);
    else if (v->type == KismetVar::Type::Float) out = v->f;
    else return false;
    return true;
}

std::string kismetValueText(const KismetVar& v) {
    switch (v.type) {
    case KismetVar::Type::Int: return std::to_string(v.i);
    case KismetVar::Type::Float: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v.f));
        return buf;
    }
    case KismetVar::Type::Bool: return v.b ? "vrai" : "faux";
    case KismetVar::Type::String: return v.s.size() > kMaxValueText ? v.s.substr(0, kMaxValueText) : v.s;
    case KismetVar::Type::Other: break;
    }
    return "?";
}

const char* kismetTypeLabel(KismetVar::Type t) {
    switch (t) {
    case KismetVar::Type::Int: return "int";
    case KismetVar::Type::Float: return "float";
    case KismetVar::Type::Bool: return "bool";
    case KismetVar::Type::String: return "texte";
    case KismetVar::Type::Other: break;
    }
    return "autre";
}

// ---- Timer text ----------------------------------------------------------------------------------------
bool parseTimerText(const std::string& text, float& out) {
    size_t b = 0, e = text.size();
    while (b < e && text[b] == ' ') ++b;
    while (e > b && text[e - 1] == ' ') --e;
    if (b >= e) return false;
    int groups[3] = {0, 0, 0};
    int groupDigits[3] = {0, 0, 0};
    int n = 0;
    float fraction = 0.f;
    size_t i = b;
    while (i < e) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            if (groupDigits[n] >= 3) return false;
            groups[n] = groups[n] * 10 + (c - '0');
            ++groupDigits[n];
            ++i;
        } else if (c == ':') {
            if (groupDigits[n] == 0 || n >= 2) return false;
            ++n;
            ++i;
        } else if (c == '.' || c == ',') {
            // Fraction of a second, only after the last group.
            ++i;
            float scale = 0.1f;
            int digits = 0;
            while (i < e && text[i] >= '0' && text[i] <= '9') {
                if (digits < 3) fraction += scale * static_cast<float>(text[i] - '0');
                scale *= 0.1f;
                ++digits;
                ++i;
            }
            if (digits == 0 || i != e) return false;
        } else {
            return false;
        }
    }
    if (n == 0) return false;                      // needs at least one ':'
    for (int k = 1; k <= n; ++k)
        if (groupDigits[k] != 2 || groups[k] >= 60) return false;
    if (groupDigits[0] == 0) return false;
    float seconds = 0.f;
    if (n == 1) seconds = static_cast<float>(groups[0] * 60 + groups[1]);
    else seconds = static_cast<float>(groups[0] * 3600 + groups[1] * 60 + groups[2]);
    out = seconds + fraction;
    return true;
}

bool looksLikeTimer(const std::string& text) {
    float s = 0.f;
    return parseTimerText(text, s);
}

bool isLevelLikeName(const std::string& name) {
    const std::string n = toLowerAscii(name);
    static const char* kWords[] = {"level", "lvl", "checkpoint", "stage", "niveau", "segment", "section",
                                   "ring", "hoop", "gate", "split", "cp"};
    for (const char* w : kWords)
        if (n.find(w) != std::string::npos) return true;
    return false;
}

namespace {
// Counters of time or failures that also step by +1 but are never checkpoints.
bool isTimeOrFailureName(const std::string& name) {
    const std::string n = toLowerAscii(name);
    static const char* kWords[] = {"second", "sec", "time", "minute", "frame", "tick", "death", "score", "touch",
                                   "attempt", "fail", "reset"};
    for (const char* w : kWords)
        if (n.find(w) != std::string::npos) return true;
    return false;
}
} // namespace

// ---- Watcher -------------------------------------------------------------------------------------------
void KismetWatcher::reset() {
    candidates_.clear();
    index_.clear();
    samples_ = 0;
}

void KismetWatcher::update(const std::vector<KismetVar>& vars, float now) {
    ++samples_;
    for (const auto& v : vars) {
        auto it = index_.find(v.name);
        if (it == index_.end()) {
            if (candidates_.size() >= 512) continue;  // bound: maps rarely have more than ~100 named variables
            KismetCandidate c;
            c.name = v.name;
            c.type = v.type;
            c.value = kismetValueText(v);
            c.intValue = v.i;
            c.floatValue = v.f;
            c.boolValue = v.b;
            c.timerLike = v.type == KismetVar::Type::String && looksLikeTimer(v.s);
            index_.emplace(v.name, candidates_.size());
            candidates_.push_back(std::move(c));
            continue;
        }
        KismetCandidate& c = candidates_[it->second];
        if (c.type != v.type) {  // the map reused a name with another type: start over for this variable
            c.type = v.type;
            c.value = kismetValueText(v);
            c.intValue = v.i;
            c.floatValue = v.f;
            c.boolValue = v.b;
            c.plusOneSteps = c.otherChanges = c.resets = c.risingEdges = c.fallingEdges = c.timerTicks = 0;
            c.timerLike = v.type == KismetVar::Type::String && looksLikeTimer(v.s);
            c.lastChange = now;
            continue;
        }
        switch (v.type) {
        case KismetVar::Type::Int:
            if (v.i != c.intValue) {
                if (v.i == c.intValue + 1) ++c.plusOneSteps;
                else if (v.i <= 1 && c.intValue > 1) ++c.resets;
                else ++c.otherChanges;
                c.intValue = v.i;
                c.value = std::to_string(v.i);
                c.lastChange = now;
            }
            break;
        case KismetVar::Type::Bool:
            if (v.b != c.boolValue) {
                if (v.b) ++c.risingEdges;
                else ++c.fallingEdges;
                c.boolValue = v.b;
                c.value = v.b ? "vrai" : "faux";
                c.lastChange = now;
            }
            break;
        case KismetVar::Type::String:
            if (v.s.compare(0, kMaxValueText, c.value) != 0) {
                c.timerLike = looksLikeTimer(v.s);
                if (c.timerLike) ++c.timerTicks;
                else ++c.otherChanges;
                c.value = kismetValueText(v);
                c.lastChange = now;
            }
            break;
        case KismetVar::Type::Float:
            if (std::fabs(v.f - c.floatValue) > 1e-4f) {
                ++c.otherChanges;
                c.floatValue = v.f;
                c.value = kismetValueText(v);
                c.lastChange = now;
            }
            break;
        case KismetVar::Type::Other: break;
        }
    }
}

const KismetCandidate* KismetWatcher::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? nullptr : &candidates_[it->second];
}

const KismetCandidate* KismetWatcher::bestLevelCandidate(bool anyName) const {
    const KismetCandidate* best = nullptr;
    int bestScore = 0;
    for (const auto& c : candidates_) {
        if (c.type != KismetVar::Type::Int || c.plusOneSteps < 1) continue;
        if (isTimeOrFailureName(c.name)) continue;
        if (!anyName && !isLevelLikeName(c.name)) continue;
        const int score = c.plusOneSteps * 2 + c.resets - c.otherChanges * 3;
        if (score > bestScore) {
            best = &c;
            bestScore = score;
        }
    }
    return best;
}

const KismetCandidate* KismetWatcher::timerCandidate() const {
    const KismetCandidate* best = nullptr;
    for (const auto& c : candidates_) {
        if (c.type != KismetVar::Type::String || !c.timerLike || c.timerTicks < 2) continue;
        if (!best || c.timerTicks > best->timerTicks) best = &c;
    }
    return best;
}

const KismetCandidate* KismetWatcher::startCandidate() const {
    const KismetCandidate* best = nullptr;
    for (const auto& c : candidates_) {
        if (c.type != KismetVar::Type::Bool || c.risingEdges < 1) continue;
        if (!best || c.risingEdges > best->risingEdges) best = &c;
    }
    return best;
}

} // namespace rlt
