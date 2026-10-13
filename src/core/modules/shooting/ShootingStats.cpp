#include "core/modules/shooting/ShootingStats.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "core/common/Units.h"

namespace rlt::shoot {

namespace {

void pushBounded(std::vector<float>& v, float x, size_t cap) {
    v.push_back(x);
    if (v.size() > cap) v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() - cap));
}

float safeFloat(float v, float lo, float hi) {
    if (!std::isfinite(v)) return lo;
    return std::max(lo, std::min(hi, v));
}

int safeInt(long long v) { return static_cast<int>(std::max(0LL, std::min(v, 1000000000LL))); }

std::string zoneKey(int i) { return std::string("shooting.zone.") + zoneId(static_cast<Zone>(i)); }
std::string flickKey(int i) { return std::string("shooting.flick.") + flickTypeId(static_cast<FlickType>(i)); }

void saveZone(Store& st, const std::string& k, const ZoneStats& z) {
    st.setInt(k + ".attempts", z.attempts);
    st.setInt(k + ".hits", z.hits);
    st.setFloat(k + ".score", z.scoreSum);
}

ZoneStats loadZone(const Store& st, const std::string& k) {
    ZoneStats z;
    z.attempts = safeInt(st.getInt(k + ".attempts", 0));
    z.hits = std::min(z.attempts, safeInt(st.getInt(k + ".hits", 0)));
    z.scoreSum = safeFloat(st.getFloat(k + ".score", 0.f), 0.f, 100.f * static_cast<float>(z.attempts));
    return z;
}

} // namespace

ShootingStats::FlickFlags ShootingStats::addFlick(FlickType category, float powerUus, float accuracy100, bool success) {
    FlickFlags f;
    const int i = std::clamp(static_cast<int>(category), 0, kFlickTypeCount - 1);
    for (auto* rec : {&flickAll[static_cast<size_t>(i)], &flickSession[static_cast<size_t>(i)]}) {
        ++rec->count;
        if (success) ++rec->successes;
    }
    FlickRecord& all = flickAll[static_cast<size_t>(i)];
    FlickRecord& ses = flickSession[static_cast<size_t>(i)];
    if (powerUus > all.bestPower) {
        f.powerAll = all.bestPower > 0.f; // the first flick of a type is not announced as a record
        all.bestPower = powerUus;
    }
    if (powerUus > ses.bestPower) {
        f.powerSession = true;
        ses.bestPower = powerUus;
    }
    const float prevBestAcc = bestFlickAccuracy();
    if (accuracy100 > all.bestAccuracy) all.bestAccuracy = accuracy100;
    if (accuracy100 > ses.bestAccuracy) ses.bestAccuracy = accuracy100;
    f.accuracyAll = prevBestAcc > 0.f && accuracy100 > prevBestAcc;
    pushBounded(flickPowersKph, powerUus * kUusToKph, kHistory);
    return f;
}

bool ShootingStats::addTarget(Zone zone, bool hit, float score100) {
    const size_t i = static_cast<size_t>(std::clamp(static_cast<int>(zone), 0, kZoneCount - 1));
    zoneAll[i].add(hit, score100);
    zoneSession[i].add(hit, score100);
    pushBounded(targetScores, score100, kHistory);
    bool beaten = false;
    if (hit) {
        ++streak;
        bestStreakSession = std::max(bestStreakSession, streak);
        if (streak > bestStreakAll) {
            beaten = bestStreakAll > 0;
            bestStreakAll = streak;
        }
    } else {
        streak = 0;
    }
    return beaten;
}

ShootingStats::PowerFlags ShootingStats::addPower(float speedUus, bool success) {
    PowerFlags f;
    for (auto* p : {&powerAll, &powerSession}) {
        ++p->count;
        if (success) ++p->successes;
    }
    if (speedUus > powerAll.best) {
        f.all = powerAll.best > 0.f;
        powerAll.best = speedUus;
    }
    if (speedUus > powerSession.best) {
        f.session = true;
        powerSession.best = speedUus;
    }
    pushBounded(shotPowersKph, speedUus * kUusToKph, kHistory);
    return f;
}

float ShootingStats::bestFlickAccuracy() const {
    float best = 0.f;
    for (const auto& r : flickAll) best = std::max(best, r.bestAccuracy);
    return best;
}

int ShootingStats::sessionTargetAttempts() const {
    int n = 0;
    for (const auto& z : zoneSession) n += z.attempts;
    return n;
}

void ShootingStats::resetSession() {
    flickSession = {};
    zoneSession = {};
    powerSession = {};
    streak = 0;
    bestStreakSession = 0;
    flickPowersKph.clear();
    shotPowersKph.clear();
    targetScores.clear();
}

void ShootingStats::resetAll() {
    resetSession();
    flickAll = {};
    zoneAll = {};
    zonePrev = {};
    powerAll = {};
    bestStreakAll = 0;
}

void ShootingStats::save(Store& st) const {
    for (int i = 0; i < kFlickTypeCount; ++i) {
        const FlickRecord& r = flickAll[static_cast<size_t>(i)];
        const std::string k = flickKey(i);
        st.setFloat(k + ".best", r.bestPower);
        st.setFloat(k + ".acc", r.bestAccuracy);
        st.setInt(k + ".count", r.count);
        st.setInt(k + ".ok", r.successes);
    }
    const bool sessionHasTargets = sessionTargetAttempts() > 0;
    for (int i = 0; i < kZoneCount; ++i) {
        const std::string k = zoneKey(i);
        saveZone(st, k, zoneAll[static_cast<size_t>(i)]);
        // "Previous session" = this session once it has targets, else the one loaded at start.
        saveZone(st, k + ".prev",
                 sessionHasTargets ? zoneSession[static_cast<size_t>(i)] : zonePrev[static_cast<size_t>(i)]);
    }
    st.setFloat("shooting.power.best", powerAll.best);
    st.setInt("shooting.power.count", powerAll.count);
    st.setInt("shooting.power.ok", powerAll.successes);
    st.setInt("shooting.target.beststreak", bestStreakAll);
}

void ShootingStats::load(const Store& st) {
    for (int i = 0; i < kFlickTypeCount; ++i) {
        FlickRecord& r = flickAll[static_cast<size_t>(i)];
        const std::string k = flickKey(i);
        r.bestPower = safeFloat(st.getFloat(k + ".best", 0.f), 0.f, 10000.f);
        r.bestAccuracy = safeFloat(st.getFloat(k + ".acc", 0.f), 0.f, 100.f);
        r.count = safeInt(st.getInt(k + ".count", 0));
        r.successes = std::min(r.count, safeInt(st.getInt(k + ".ok", 0)));
    }
    for (int i = 0; i < kZoneCount; ++i) {
        const std::string k = zoneKey(i);
        zoneAll[static_cast<size_t>(i)] = loadZone(st, k);
        zonePrev[static_cast<size_t>(i)] = loadZone(st, k + ".prev");
    }
    powerAll.best = safeFloat(st.getFloat("shooting.power.best", 0.f), 0.f, 10000.f);
    powerAll.count = safeInt(st.getInt("shooting.power.count", 0));
    powerAll.successes = std::min(powerAll.count, safeInt(st.getInt("shooting.power.ok", 0)));
    bestStreakAll = safeInt(st.getInt("shooting.target.beststreak", 0));
}

} // namespace rlt::shoot
