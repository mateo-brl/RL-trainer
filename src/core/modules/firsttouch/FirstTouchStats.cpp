#include "core/modules/firsttouch/FirstTouchStats.h"

#include <algorithm>
#include <cmath>

namespace rlt::ft {

void TouchStats::addGraded(TouchType type, float score, bool success, float speedDiff, bool inGoodZone) {
    ++graded;
    sumScore += score;
    bestScore = std::max(bestScore, score);
    if (success) {
        ++successes;
        ++streak;
        bestStreak = std::max(bestStreak, streak);
    } else {
        streak = 0;
    }
    if (inGoodZone) ++goodZone;
    sumSignedDiff += speedDiff;
    const int t = static_cast<int>(type);
    if (t >= 0 && t < kTouchTypeCount) {
        ++typeCount[static_cast<size_t>(t)];
        typeSumScore[static_cast<size_t>(t)] += score;
    }
}

void TouchStats::addDribbleContact() {
    ++dribbleContacts;
    ++typeCount[static_cast<size_t>(TouchType::Dribble)];
}

void TouchStats::addChain(int contacts, float seconds) {
    if (contacts < 2) return;
    ++dribbleChains;
    bestChainContacts = std::max(bestChainContacts, contacts);
    bestChainSeconds = std::max(bestChainSeconds, seconds);
    dribbleSeconds += std::max(0.f, seconds);
}

float TouchStats::typeAverage(TouchType t) const {
    const auto i = static_cast<size_t>(t);
    if (i >= typeCount.size() || typeCount[i] <= 0 || t == TouchType::Dribble) return 0.f;
    return typeSumScore[i] / static_cast<float>(typeCount[i]);
}

void TouchStats::save(Store& st, const std::string& p) const {
    st.setInt(p + ".graded", graded);
    st.setInt(p + ".successes", successes);
    st.setFloat(p + ".sum_score", sumScore);
    st.setFloat(p + ".best_score", bestScore);
    st.setInt(p + ".good_zone", goodZone);
    st.setInt(p + ".best_streak", bestStreak);
    st.setFloat(p + ".sum_diff", static_cast<float>(sumSignedDiff));
    for (int i = 0; i < kTouchTypeCount; ++i) {
        const std::string k = p + ".type." + touchTypeId(static_cast<TouchType>(i));
        st.setInt(k + ".count", typeCount[static_cast<size_t>(i)]);
        st.setFloat(k + ".sum", typeSumScore[static_cast<size_t>(i)]);
    }
    st.setInt(p + ".dribble_contacts", dribbleContacts);
    st.setInt(p + ".dribble_chains", dribbleChains);
    st.setInt(p + ".best_chain", bestChainContacts);
    st.setFloat(p + ".best_chain_s", bestChainSeconds);
    st.setFloat(p + ".dribble_s", dribbleSeconds);
    st.setInt(p + ".flip_resets", flipResets);
}

void TouchStats::load(const Store& st, const std::string& p) {
    // Bounded: a corrupt or hand-edited value must not wrap to a negative count.
    auto geti = [&](const std::string& k) { return static_cast<int>(std::clamp(st.getInt(k, 0), 0LL, 1000000000LL)); };
    auto getf = [&](const std::string& k) {
        const float f = st.getFloat(k, 0.f);
        return std::isfinite(f) ? f : 0.f;
    };
    *this = TouchStats{};
    graded = geti(p + ".graded");
    successes = std::min(graded, geti(p + ".successes"));
    sumScore = std::max(0.f, getf(p + ".sum_score"));
    bestScore = clampf(getf(p + ".best_score"), 0.f, 100.f);
    goodZone = std::min(graded, geti(p + ".good_zone"));
    bestStreak = geti(p + ".best_streak");
    sumSignedDiff = getf(p + ".sum_diff");
    for (int i = 0; i < kTouchTypeCount; ++i) {
        const std::string k = p + ".type." + touchTypeId(static_cast<TouchType>(i));
        typeCount[static_cast<size_t>(i)] = geti(k + ".count");
        typeSumScore[static_cast<size_t>(i)] = std::max(0.f, getf(k + ".sum"));
    }
    dribbleContacts = geti(p + ".dribble_contacts");
    dribbleChains = geti(p + ".dribble_chains");
    bestChainContacts = geti(p + ".best_chain");
    bestChainSeconds = std::max(0.f, getf(p + ".best_chain_s"));
    dribbleSeconds = std::max(0.f, getf(p + ".dribble_s"));
    flipResets = geti(p + ".flip_resets");
}

std::string trendText(const TouchStats& s, SpeedUnit unit, float threshold, int minTouches) {
    if (s.graded < minTouches) return {};
    const float mean = s.meanSignedDiff();
    if (mean > threshold)
        return "Tendance : tu arrives trop vite en moyenne (" + formatSpeedValue(mean, unit) + ").";
    if (mean < -threshold)
        return "Tendance : tu arrives trop lentement en moyenne (" + formatSpeedValue(mean, unit) + ").";
    return "Tendance : bonne gestion de la vitesse (" + formatSpeedValue(mean, unit) + ").";
}

const char* tipShortLabel(const std::string& code) {
    if (code == "ft.too_fast") return "vitesse trop élevée au contact";
    if (code == "ft.too_slow") return "vitesse trop faible au contact";
    if (code == "ft.head_on") return "amorti des balles qui arrivent de face";
    if (code == "ft.angle") return "angle d'arrivée";
    if (code == "ft.drop_speed") return "balle prise trop bas dans sa chute";
    if (code == "ft.contact_high") return "contact trop haut";
    if (code == "ft.contact_low") return "contact trop bas";
    if (code == "ft.side") return "balle décentrée";
    if (code == "ft.ball_popped") return "la balle rebondit sur toi";
    if (code == "ft.boost") return "boost au moment du contact";
    if (code == "ft.air_contact") return "contact aérien pas assez dessous";
    if (code == "ft.nose_aim") return "nez pas pointé sous la balle";
    if (code == "ft.oversteer") return "trop de braquage après la touche";
    if (code == "ft.lost_control") return "contrôle après la touche";
    return "";
}

std::string persistentCode(const std::vector<std::string>& codes, int minCount) {
    std::string best;
    int bestCount = 0;
    for (size_t i = 0; i < codes.size(); ++i) {
        if (codes[i].empty()) continue;
        const int c = static_cast<int>(std::count(codes.begin(), codes.end(), codes[i]));
        if (c > bestCount) {
            bestCount = c;
            best = codes[i];
        }
    }
    return bestCount >= minCount ? best : std::string();
}

} // namespace rlt::ft
