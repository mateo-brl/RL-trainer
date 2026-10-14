// Match conditions of every drill rep: at PLACE (the tick the car and ball are placed, drill owning the activity) the
// module asks for limited boost rules; every boost pad is respawned kKoPadResetDelayTicks later, once the car has
// stayed on its spawn (a reset on the teleport tick came before the pickups made by the teleport, see
// KickoffModule::placeRep). The engine restores the player's rules when the activity ends, so the module never emits
// a restore itself. The unlimited-boost heuristic stays as a safety net.
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

bool limitedRules(const Actions& a) { return a.boostRules && !a.boostRules->unlimited && !a.boostRules->noBoost; }

bool touchesBoostWorld(const Actions& a) { return a.boostRules.has_value() || a.resetPickups || a.restoreBoostRules; }

std::string panelText(Harness& h) {
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f};
    h.mod.draw(view, dc, dl);
    std::string all;
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines) all += l.text + "\n";
    return all;
}

// Frames of the ticks logged from index `from` on whose actions match `pred`.
template <class Pred>
std::vector<int64_t> framesWhere(const Harness& h, size_t from, Pred pred) {
    std::vector<int64_t> out;
    for (size_t i = from; i < h.log.size(); ++i)
        if (pred(h.log[i])) out.push_back(h.logFrames[i]);
    return out;
}

bool resets(const Actions& a) { return a.resetPickups; }

} // namespace

TEST(ko_place_limits_boost_and_respawns_pads) {
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_reset_delay", "0.5");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    const Actions cmd = h.command("start", {"diagonal_left"});
    CHECK(!touchesBoostWorld(cmd));  // the start command only arms the drill: nothing is placed yet
    // PLACE: car, ball, limited boost on the same tick, but no pad reset yet (the teleport may still take pads).
    const size_t placeIdx = h.log.size();
    const Actions place = h.tick(makeSnap(++f));
    CHECK(h.mod.phase() == KickoffPhase::Countdown);
    CHECK_EQ(h.mod.placeFrame(), f);
    CHECK(place.setCar.has_value() && place.setBall.has_value());
    CHECK(limitedRules(place));
    CHECK(!place.resetPickups);
    CHECK(!place.restoreBoostRules);
    CHECK(h.mod.padResetPending());
    // Countdown on the spawn: one pad reset, kKoPadResetDelayTicks after the placement, before GO.
    const int64_t go = h.runCountdown(f);
    const std::vector<int64_t> rep1 = framesWhere(h, placeIdx, resets);
    CHECK_EQ(rep1.size(), size_t(1));
    if (!rep1.empty()) CHECK_EQ(rep1[0], h.mod.placeFrame() + kKoPadResetDelayTicks);
    CHECK(!h.mod.padResetPending());
    CHECK(go > h.mod.placeFrame() + kKoPadResetDelayTicks);
    // Live attempt, result, rep 2 placed: no boost rules nor pad reset until the next placement.
    const size_t afterReset = h.log.size();
    playAttempt(h, simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, Script{}, go, 4.f), f);
    for (int guard = 0; guard < 400 && h.mod.phase() == KickoffPhase::Done; ++guard) h.tick(makeSnap(++f));
    CHECK(h.mod.lastResult().valid);
    CHECK(h.mod.phase() == KickoffPhase::Countdown);  // auto repeat: rep 2 placed
    int places = 0;
    for (size_t i = afterReset; i < h.log.size(); ++i) {
        const Actions& a = h.log[i];
        if (!touchesBoostWorld(a)) continue;
        ++places;
        CHECK(i + 1 == h.log.size());  // only the last tick: the placement of rep 2
        CHECK(a.setCar.has_value());
        CHECK(limitedRules(a));
        CHECK(!a.resetPickups);
        CHECK(!a.restoreBoostRules);
    }
    CHECK_EQ(places, 1);
    const int64_t place2 = h.mod.placeFrame();
    const size_t rep2Idx = h.log.size() - 1;
    h.runCountdown(f);
    const std::vector<int64_t> rep2 = framesWhere(h, rep2Idx, resets);
    CHECK_EQ(rep2.size(), size_t(1));
    if (!rep2.empty()) CHECK_EQ(rep2[0], place2 + kKoPadResetDelayTicks);
    // "next" during the countdown and a freeplay reset mid-attempt both place again, with the same requests.
    h.command("next");
    const Actions next = h.tick(makeSnap(++f));
    CHECK(limitedRules(next));
    CHECK(!next.resetPickups);
    const size_t nextIdx = h.log.size() - 1;
    const int64_t go3 = h.runCountdown(f);
    CHECK_EQ(framesWhere(h, nextIdx, resets).size(), size_t(1));
    const SimOut sim = simulate(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue, Script{}, go3, 4.f);
    for (size_t i = 0; i < 30; ++i) h.tick(sim.snaps[i]);
    CHECK(h.mod.phase() == KickoffPhase::Live);
    Snapshot teleported = sim.snaps[30];
    teleported.car.pos = {3000.f, 3000.f, kKoCarRestZ};
    h.tick(teleported);
    CHECK(h.mod.phase() == KickoffPhase::Placing);
    const Actions replace = h.tick(restingSnap(teleported.frame + 1, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
    CHECK(h.mod.phase() == KickoffPhase::Countdown);
    CHECK(limitedRules(replace));
    CHECK(!replace.resetPickups);
    CHECK(h.mod.padResetPending());
    // Stopping the drill: the module asks for nothing, the engine puts the player's rules back, and the pending pad
    // reset is dropped.
    const Actions stop = h.command("stop");
    CHECK(!touchesBoostWorld(stop));
    CHECK(!h.mod.padResetPending());
    for (int64_t k = 2; k < 2 + 2 * kKoPadResetDelayTicks; ++k)
        CHECK(!touchesBoostWorld(h.tick(makeSnap(teleported.frame + k))));
}

TEST(ko_place_boost_rules_only_while_owning) {
    // World control refused: the drill does not start, nothing is requested.
    {
        Harness h;
        h.host.allow = false;
        int64_t f = 0;
        h.tick(makeSnap(++f));
        CHECK(!touchesBoostWorld(h.command("start", {"back"})));
        for (int i = 0; i < 200; ++i) CHECK(!touchesBoostWorld(h.tick(makeSnap(++f))));
        CHECK(!h.mod.running());
    }
    // Another drill takes the activity during the countdown: no placement, boost request nor pad reset afterwards.
    {
        Harness h;
        int64_t f = 0;
        h.tick(makeSnap(++f));
        h.command("start", {"back"});
        CHECK(limitedRules(h.tick(makeSnap(++f))));
        CHECK(h.mod.padResetPending());
        h.host.steal();
        CHECK(!h.mod.padResetPending());
        CHECK(!h.mod.running());
        for (int i = 0; i < 600; ++i) CHECK(!touchesBoostWorld(h.tick(makeSnap(++f))));
    }
    // World control lost while running (context change): the drill stops before placing again.
    {
        Harness h;
        int64_t f = 0;
        h.tick(makeSnap(++f));
        h.command("start", {"back"});
        h.game.worldControlAllowed = false;
        for (int i = 0; i < 10; ++i) CHECK(!touchesBoostWorld(h.tick(makeSnap(++f))));
        CHECK(!h.mod.running());
    }
}

TEST(ko_unlimited_boost_safety_net_text) {
    // Drill rep whose tank never drains (the boost rules could not be applied): still detected, record not counted,
    // and the card says the plugin could not cut it (not "cut it yourself for a realistic kickoff").
    Harness h;
    h.set("rlt_ko_ghost", "0");
    h.set("rlt_ko_auto_repeat", "0");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"back"});
    const int64_t go = h.runCountdown(f);
    SimOut sim = simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, go, 4.f);
    for (auto& s : sim.snaps) s.car.boost = kKoSpawnBoost;
    playAttempt(h, sim, f);
    CHECK(h.mod.phase() == KickoffPhase::Done);
    CHECK(h.mod.lastResult().metrics.boostLooksUnlimited);
    std::string txt = panelText(h);
    CHECK(txt.find("record non compt") != std::string::npos);
    CHECK(txt.find("Pas pu le couper ici") != std::string::npos);
    CHECK(txt.find("Coupe-le pour un kickoff") == std::string::npos);
    CHECK(!hasEmDash(txt));
    // Passive kickoff with unlimited boost (the plugin does not touch the rules outside the drill): the card points
    // to the drill, which cuts it.
    Harness p;
    int64_t g = 0;
    for (int i = 0; i < 5; ++i) p.tick(restingSnap(++g, KickoffSpawn::BackCenter, KickoffTeam::Blue, false));
    SimOut ps = simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, g + 1, 4.f);
    for (auto& s : ps.snaps) s.car.boost = kKoSpawnBoost;
    for (size_t i = 0; i < ps.snaps.size() && i < static_cast<size_t>(ps.touchIndex) + 4; ++i)
        p.tick(ps.snaps[i], static_cast<int>(i) == ps.touchIndex);
    CHECK(p.mod.lastResult().valid && p.mod.lastResult().passive);
    CHECK(p.mod.lastResult().metrics.boostLooksUnlimited);
    txt = panelText(p);
    CHECK(txt.find("record non compt") != std::string::npos);
    CHECK(txt.find("Lance l'exercice : il le coupe") != std::string::npos);
    CHECK(!hasEmDash(txt));
    // A normal (limited) run shows neither line.
    Harness n;
    n.set("rlt_ko_ghost", "0");
    n.set("rlt_ko_auto_repeat", "0");
    int64_t k = 0;
    n.tick(makeSnap(++k));
    n.command("start", {"back"});
    const int64_t go2 = n.runCountdown(k);
    playAttempt(n, simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, go2, 4.f), k);
    CHECK(n.mod.phase() == KickoffPhase::Done);
    CHECK(!n.mod.lastResult().metrics.boostLooksUnlimited);
    txt = panelText(n);
    CHECK(txt.find("illimit") == std::string::npos);
}
