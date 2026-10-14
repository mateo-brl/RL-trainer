// Kickoff for a beginner: tips in plain words, a compact HUD (3 main lines without ms, degrees or uu), defaults that
// let a beginner win against the default ghost while the top levels stay demanding.
#include <cctype>

#include "core/common/Text.h"
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

ViewContext screen() {
    ViewContext v;
    v.valid = true;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    return v;
}

struct PanelText {
    size_t panels = 0;
    std::vector<std::string> main;    // lines the compact HUD keeps
    std::vector<std::string> detail;  // lines it hides
};

PanelText panelText(Harness& h) {
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f, true};
    h.mod.draw(screen(), dc, dl);
    PanelText t;
    t.panels = dl.panels.size();
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines) (l.detail ? t.detail : t.main).push_back(l.text);
    return t;
}

// Measures the compact HUD must not show: milliseconds, degrees (folded to " deg" without accents), uu.
bool technical(const std::string& s) {
    return s.find(" ms") != std::string::npos || s.find("\xC2\xB0") != std::string::npos ||
           s.find(" deg") != std::string::npos || s.find(" uu") != std::string::npos;
}

void checkCompact(const PanelText& t, const char* phase) {
    CHECK_MSG(t.panels == 1, phase);
    CHECK_MSG(!t.main.empty() && t.main.size() <= 3, std::string(phase) + ": " + std::to_string(t.main.size()));
    for (const auto& l : t.main) CHECK_MSG(!technical(l) && !hasEmDash(l), std::string(phase) + ": " + l);
}

// A beginner's kickoff: front flip, late start (0.15 s), contact 20 uu off centre.
Script beginnerKickoff() {
    Script s;
    s.kind = Script::FrontFlip;
    s.reaction = 0.15f;
    s.contactLateral = 20.f;
    return s;
}

bool hasTip(const KickoffGrade& g, KickoffTip t) {
    for (const auto& x : g.tips)
        if (x.tip == t) return true;
    return false;
}

} // namespace

TEST(ko_tips_in_plain_words) {
    for (int i = 0; i < static_cast<int>(KickoffTip::Count); ++i) {
        const auto t = static_cast<KickoffTip>(i);
        const std::string text = kickoffTipText(t);
        CHECK_MSG(text.size() > 10 && text.size() <= 72, text);  // one HUD line
        CHECK_MSG(!technical(text) && !hasEmDash(text), text);
        bool digit = false;
        for (unsigned char c : text) digit = digit || std::isdigit(c);
        CHECK_MSG(!digit, text);  // no measure at all in the main tip
        // The measured thresholds stay available for the tab.
        CHECK(std::string(kickoffTipDetail(t)).size() > 10);
        CHECK(!hasEmDash(kickoffTipDetail(t)));
    }
    CHECK(std::string(kickoffTipText(KickoffTip::SlowDodge)).find("juste après le saut") != std::string::npos);
    CHECK(std::string(kickoffTipText(KickoffTip::LateCancel)).find("tire le stick vers toi") != std::string::npos);
    CHECK(std::string(kickoffTipDetail(KickoffTip::LateCancel)).find("0,1 s") != std::string::npos);
}

TEST(ko_defaults_suit_a_beginner) {
    Harness h;
    CHECK(h.mod.effectiveGhostMode() == KickoffGhostMode::Adaptive);
    CHECK_EQ(h.settings.kickoff.gradeTarget, static_cast<int>(KickoffLevel::Standard));
    CHECK_NEAR(h.mod.adaptiveLevel(), kKoAdaptiveStart, 1e-6);
    h.set("rlt_ko_auto_repeat", "0");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"diagonal_left"});
    const int64_t go = h.runCountdown(f);
    CHECK_NEAR(h.mod.ghost().arrivalTime(), 2.40f, 1e-4);  // a new player races the beginner time
    std::vector<Actions> acts;
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, beginnerKickoff(), go, 4.f), f, &acts);
    const KickoffResultView& r = h.mod.lastResult();
    CHECK(r.valid && r.metrics.touched && r.hasDuel);
    CHECK(kickoffDuelWon(r.duel.outcome));  // the beginner can win
    bool success = false;
    for (const auto& a : acts)
        for (const auto& x : a.results) success = success || x.success;
    CHECK(success);
    CHECK(r.grade.total >= 70);
    // One clear tip for what went wrong: the late start.
    CHECK(!r.grade.tips.empty() && r.grade.tips.front().tip == KickoffTip::SlowReaction);
    // The win raised the adaptive ghost.
    CHECK(h.mod.adaptiveLevel() > kKoAdaptiveStart);

    // The highest levels stay demanding: the same kickoff loses clearly against the Pro and Elite ghosts.
    for (const char* ghost : {"3", "4"}) {
        Harness hard;
        hard.set("rlt_ko_ghost", ghost);
        hard.set("rlt_ko_auto_repeat", "0");
        int64_t g = 0;
        hard.tick(makeSnap(++g));
        hard.command("start", {"diagonal_left"});
        const int64_t go2 = hard.runCountdown(g);
        playAttempt(hard, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, beginnerKickoff(), go2, 4.f), g);
        CHECK_MSG(hard.mod.lastResult().duel.outcome == KickoffDuel::GhostClear, ghost);
    }
}

TEST(ko_speedflip_details_only_when_relevant) {
    // Early diagonal flip without a cancel: a normal flip at the Standard level, a failed speedflip from Pro up.
    KickoffMetrics m;
    m.spawn = KickoffSpawn::DiagonalRight;
    m.touched = true;
    m.timeToTouch = 2.10f;
    m.speedToBall = 2100.f;
    m.reaction = 0.f;
    m.boostHeldRatio = 1.f;
    m.padsPicked = 1;
    m.firstJump = 0.45f;
    m.dodge = 0.56f;
    m.dodgeSide = -0.4f;
    m.dodgeForward = 0.9f;
    m.dodgeAngleDeg = -24.f;
    m.cancel = -1.f;
    m.landing = 1.5f;
    m.dodges = 1;
    m.type = KickoffType::DiagonalFlip;
    const KickoffGrade standard = kickoffGrade(m, KickoffLevel::Standard, nullptr);
    CHECK(standard.speedflipAttempt);
    CHECK(!hasTip(standard, KickoffTip::NoCancel) && !hasTip(standard, KickoffTip::LateLanding));
    CHECK(!hasTip(standard, KickoffTip::TrySpeedflip));
    const KickoffGrade pro = kickoffGrade(m, KickoffLevel::Pro, nullptr);
    CHECK(hasTip(pro, KickoffTip::NoCancel));
    // A real speedflip (cancelled) is judged at any level.
    m.cancel = 0.80f;  // 0.24 s after the flip: late
    m.type = KickoffType::Speedflip;
    CHECK(hasTip(kickoffGrade(m, KickoffLevel::Standard, nullptr), KickoffTip::LateCancel));
}

TEST(ko_adaptive_level_starts_easy_until_played) {
    // The level is saved even when the adaptive ghost was never raced: such a save does not count.
    Store unplayed;
    unplayed.setFloat("kickoff.adaptive.level", 0.5f);
    unplayed.setInt("kickoff.adaptive.attempts", 0);
    KickoffModule a;
    a.load(unplayed);
    CHECK_NEAR(a.adaptiveLevel(), kKoAdaptiveStart, 1e-6);
    Store played = unplayed;
    played.setInt("kickoff.adaptive.attempts", 7);
    KickoffModule b;
    b.load(played);
    CHECK_NEAR(b.adaptiveLevel(), 0.5f, 1e-6);
}

TEST(ko_hud_compact_budget) {
    Harness h;
    h.set("rlt_ko_auto_repeat", "0");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"diagonal_left"});
    for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
    checkCompact(panelText(h), "countdown");
    const int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, beginnerKickoff(), go, 4.f);
    for (int i = 0; i < 60; ++i) h.tick(sim.snaps[static_cast<size_t>(i)]);
    PanelText live = panelText(h);
    checkCompact(live, "live");
    CHECK(!live.main.empty() && live.main[0].find("Chrono") == 0);
    CHECK(!live.detail.empty());  // mode, ghost, split times
    int64_t frame = sim.snaps[59].frame;
    for (size_t i = 60; i < sim.snaps.size() && h.mod.phase() != KickoffPhase::Done; ++i) {
        h.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        frame = sim.snaps[i].frame;
    }
    CHECK(h.mod.phase() == KickoffPhase::Done);
    const PanelText done = panelText(h);
    checkCompact(done, "result");
    // Result (verdict, score, time, 50/50), the one tip in words, what comes next.
    CHECK_EQ(done.main.size(), size_t(3));
    if (done.main.size() == 3) {
        CHECK(done.main[0].find("/100") != std::string::npos);
        CHECK(done.main[0].find("kickoff gagn") != std::string::npos);
        CHECK(done.main[1] == hudText(kickoffTipText(h.mod.lastResult().grade.tips.front().tip), false));
        CHECK(done.main[2].find("Reset") != std::string::npos);
    }
    bool splits = false, type = false;
    for (const auto& l : done.detail) {
        splits = splits || l.find("25 %") != std::string::npos;
        type = type || l.find("Type : ") == 0;
    }
    CHECK(splits && type);
    // Next rep: the tip of the last attempt stays as a reminder during the countdown.
    h.command("next");
    for (int i = 0; i < 10; ++i) h.tick(restingSnap(++frame, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
    const PanelText again = panelText(h);
    checkCompact(again, "countdown 2");
    bool reminder = false;
    for (const auto& l : again.main)
        reminder = reminder || l == hudText(kickoffTipText(h.mod.lastResult().grade.tips.front().tip), false);
    CHECK(reminder);

    // Passive card: result and tip only.
    Harness p;
    int64_t g = 0;
    for (int i = 0; i < 5; ++i) p.tick(restingSnap(++g, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
    const SimOut ps = simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, beginnerKickoff(), g + 1, 4.f);
    for (size_t i = 0; i < ps.snaps.size() && i < static_cast<size_t>(ps.touchIndex) + 4; ++i)
        p.tick(ps.snaps[i], static_cast<int>(i) == ps.touchIndex);
    CHECK(p.mod.lastResult().valid && p.mod.lastResult().passive);
    const PanelText card = panelText(p);
    checkCompact(card, "passive");
    CHECK(card.main.size() <= 2);
}
