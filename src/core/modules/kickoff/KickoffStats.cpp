#include "core/modules/kickoff/KickoffStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rlt {

namespace {

std::string joinFloats(const std::vector<float>& v, int decimals) {
    std::string out;
    char buf[32];
    for (size_t i = 0; i < v.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v[i]));
        if (i) out += ' ';
        out += buf;
    }
    return out;
}

std::vector<float> splitFloats(const std::string& s, size_t maxCount) {
    std::vector<float> out;
    const char* p = s.c_str();
    while (*p && out.size() < maxCount) {
        char* end = nullptr;
        const float f = std::strtof(p, &end);
        if (end == p) break;
        if (std::isfinite(f)) out.push_back(f);
        p = end;
    }
    return out;
}

template <class T>
void pushBounded(std::vector<T>& v, T value, size_t cap) {
    v.push_back(value);
    if (v.size() > cap) v.erase(v.begin(), v.begin() + static_cast<long>(v.size() - cap));
}

std::string prefix(int i) { return std::string("kickoff.") + kickoffSpawnInfo(kickoffSpawnFromIndex(i)).key + "."; }

int clampCount(long long v) { return static_cast<int>(std::clamp<long long>(v, 0, 10000000)); }

} // namespace

float KickoffSpawnStats::avgTime() const {
    if (recentTimes.empty()) return 0.f;
    float s = 0.f;
    for (float t : recentTimes) s += t;
    return s / static_cast<float>(recentTimes.size());
}

float KickoffSpawnStats::avgScore() const {
    if (recentScores.empty()) return -1.f;
    float s = 0.f;
    for (int v : recentScores) s += static_cast<float>(v);
    return s / static_cast<float>(recentScores.size());
}

float KickoffSpawnStats::winRate() const {
    return duels > 0 ? static_cast<float>(wins) / static_cast<float>(duels) : 0.f;
}

KickoffType KickoffSpawnStats::favouriteType() const {
    int best = 0;
    KickoffType t = KickoffType::Unknown;
    for (int i = 1; i < kKoTypeCount; ++i)
        if (typeCounts[static_cast<size_t>(i)] > best) {
            best = typeCounts[static_cast<size_t>(i)];
            t = static_cast<KickoffType>(i);
        }
    return t;
}

std::vector<float> kickoffDownsample(const std::vector<float>& along, float arrive, float dt) {
    std::vector<float> out;
    if (along.empty() || dt <= 0.f) return out;
    for (size_t k = 0; k < kKoPbMaxSamples; ++k) {
        const float t = static_cast<float>(k) * dt;
        if (t >= arrive) break;
        const float f = t * kTickRate;
        const size_t i = static_cast<size_t>(f);
        if (i + 1 >= along.size()) {
            out.push_back(along.back());
            continue;
        }
        out.push_back(lerpf(along[i], along[i + 1], f - static_cast<float>(i)));
    }
    return out;
}

bool KickoffStats::record(const KickoffMetrics& m, int score, const KickoffDuelResult* duel, bool allowRecord) {
    KickoffSpawnStats& s = of(m.spawn);
    ++s.attempts;
    ++s.typeCounts[static_cast<size_t>(std::clamp(static_cast<int>(m.type), 0, kKoTypeCount - 1))];
    s.bestScore = std::max(s.bestScore, score);
    pushBounded(s.recentScores, score, kKoRecentCount);
    if (duel) {
        ++s.duels;
        if (kickoffDuelWon(duel->outcome)) ++s.wins;
        else if (duel->outcome == KickoffDuel::Neutral) ++s.neutrals;
        else ++s.losses;
    }
    if (!m.touched) return false;
    ++s.touches;
    if (!allowRecord) return false;
    pushBounded(s.recentTimes, m.timeToTouch, kKoRecentCount);
    if (s.bestTime <= 0.f || m.timeToTouch < s.bestTime) {
        s.bestTime = m.timeToTouch;
        s.bestSpeed = std::max(0.f, m.speedToBall);
        s.pbProfile = kickoffDownsample(m.along, m.timeToTouch);
        return true;
    }
    return false;
}

void KickoffStats::reset() { per_ = {}; }

int KickoffStats::totalAttempts() const {
    int n = 0;
    for (const auto& s : per_) n += s.attempts;
    return n;
}

KickoffSpawn KickoffStats::weakest() const {
    int best = 0;
    float bestScore = 1e9f;
    int bestAttempts = 1 << 30;
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const KickoffSpawnStats& s = per_[static_cast<size_t>(i)];
        const float score = s.attempts > 0 ? s.avgScore() : -1.f;
        if (score < bestScore - 1e-4f || (std::fabs(score - bestScore) <= 1e-4f && s.attempts < bestAttempts)) {
            best = i;
            bestScore = score;
            bestAttempts = s.attempts;
        }
    }
    return kickoffSpawnFromIndex(best);
}

bool KickoffStats::personalBestProfile(KickoffSpawn spawn, KickoffGhostProfile& out) const {
    const KickoffSpawnStats& s = of(spawn);
    if (s.bestTime <= 0.f) return false;
    const float speed =
        s.bestSpeed > 100.f ? s.bestSpeed : kickoffBenchmark(spawn, KickoffLevel::Standard).contactSpeed;
    if (s.pbProfile.size() >= 2) {
        out = KickoffGhostProfile::fromSamples(s.pbProfile, kKoPbSampleDt, s.bestTime, speed);
    } else {
        KickoffBenchmark b;
        b.arrive = s.bestTime;
        b.contactSpeed = speed;
        out = KickoffGhostProfile::fromBenchmark(b);
    }
    return true;
}

void KickoffStats::save(Store& st) const {
    st.setInt("kickoff.version", 1);
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const KickoffSpawnStats& s = per_[static_cast<size_t>(i)];
        const std::string p = prefix(i);
        st.setInt(p + "attempts", s.attempts);
        st.setInt(p + "touches", s.touches);
        st.setInt(p + "duels", s.duels);
        st.setInt(p + "wins", s.wins);
        st.setInt(p + "neutrals", s.neutrals);
        st.setInt(p + "losses", s.losses);
        st.setFloat(p + "best_time", s.bestTime);
        st.setFloat(p + "best_speed", s.bestSpeed);
        st.setInt(p + "best_score", s.bestScore);
        st.setString(p + "recent_times", joinFloats(s.recentTimes, 3));
        std::vector<float> scores(s.recentScores.begin(), s.recentScores.end());
        st.setString(p + "recent_scores", joinFloats(scores, 0));
        std::vector<float> types(s.typeCounts.begin(), s.typeCounts.end());
        st.setString(p + "types", joinFloats(types, 0));
        st.setString(p + "pb_profile", joinFloats(s.pbProfile, 1));
    }
}

void KickoffStats::load(const Store& st) {
    for (int i = 0; i < kKoSpawnCount; ++i) {
        KickoffSpawnStats s;
        const std::string p = prefix(i);
        s.attempts = clampCount(st.getInt(p + "attempts", 0));
        s.touches = clampCount(st.getInt(p + "touches", 0));
        s.duels = clampCount(st.getInt(p + "duels", 0));
        s.wins = clampCount(st.getInt(p + "wins", 0));
        s.neutrals = clampCount(st.getInt(p + "neutrals", 0));
        s.losses = clampCount(st.getInt(p + "losses", 0));
        const float bt = st.getFloat(p + "best_time", 0.f);
        s.bestTime = std::isfinite(bt) && bt > 0.f && bt < 30.f ? bt : 0.f;
        const float bs = st.getFloat(p + "best_speed", 0.f);
        s.bestSpeed = std::isfinite(bs) ? std::clamp(bs, 0.f, 6000.f) : 0.f;
        s.bestScore = static_cast<int>(std::clamp<long long>(st.getInt(p + "best_score", 0), 0, 100));
        s.recentTimes = splitFloats(st.getString(p + "recent_times"), kKoRecentCount);
        for (float v : splitFloats(st.getString(p + "recent_scores"), kKoRecentCount))
            s.recentScores.push_back(std::clamp(static_cast<int>(std::lround(v)), 0, 100));
        const std::vector<float> types = splitFloats(st.getString(p + "types"), static_cast<size_t>(kKoTypeCount));
        for (size_t k = 0; k < types.size(); ++k) s.typeCounts[k] = clampCount(std::llround(types[k]));
        s.pbProfile = splitFloats(st.getString(p + "pb_profile"), kKoPbMaxSamples);
        per_[static_cast<size_t>(i)] = std::move(s);
    }
}

} // namespace rlt
