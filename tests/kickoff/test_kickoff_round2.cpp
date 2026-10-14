// Player feedback on v1.0.7 (Champion 1), kickoff drill:
//  - "quand il me téléporte en arrière il me fait prendre les boosts donc je n'ai pas de boost pour aller au
//    kickoff": every pad is respawned once the car has stayed on its spawn (not on the teleport tick), and the car has
//    33 boost at GO, rep after rep, after a manual reset and when the car is moved during the countdown;
//  - "j'ai deux timers, celui du jeu et celui que tu rajoutes": the game's countdown is the only one by default (no big
//    number, no countdown line in the panel, compact or full);
//  - lines to the ball off by default (the ghost box stays);
//  - saved configs holding the old defaults are moved once (defaults_v3).
#include <algorithm>
#include <cmath>
#include <utility>

#include "core/common/HudLayout.h"
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

namespace {

constexpr float kBoost33 = 100.f / 3.f;

float distToSegment2D(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = flat(b - a);
    const Vec3 ap = flat(p - a);
    const float len2 = dot(ab, ab);
    const float t = len2 > 1e-6f ? std::clamp(dot(ap, ab) / len2, 0.f, 1.f) : 0.f;
    return length(flat(p - (a + ab * t)));
}

// Model of the boost pads during the drill, built from the player's report on v1.0.7: the actions of a tick run in
// the SetVehicleInput pre-hook (glue order: car with its boost, pad reset, boost amount), then the physics step moves
// the car; a teleport takes every available pad whose pickup radius (kKoSmallPadRadius / kKoBigPadRadius) its
// straight move crosses. Small pads come back 4 s after a pickup, big ones 10 s (RLBot "Useful Game Values"). The
// car's own driving is not modelled: a reset before GO respawns those pads anyway.
struct PadWorld {
    std::vector<int64_t> backAt = std::vector<int64_t>(kickoffSoccarPads().size(), 0);
    float hookBoost = 0.f;  // tank right after the hook of the last tick (what GO starts with on the GO tick)
    int teleportPickups = 0;

    void step(const Snapshot& sn, const Actions& a) {
        float boost = sn.car.boost;
        if (a.setCar && a.setCar->boost) boost = *a.setCar->boost;
        if (a.resetPickups) std::fill(backAt.begin(), backAt.end(), 0);
        if (a.setBoost) boost = *a.setBoost;
        hookBoost = boost;
        if (!a.setCar || !sn.car.valid) return;
        const auto& pads = kickoffSoccarPads();
        for (size_t i = 0; i < pads.size(); ++i) {
            const float r = pads[i].big ? kKoBigPadRadius : kKoSmallPadRadius;
            if (sn.frame < backAt[i] || distToSegment2D(pads[i].pos, sn.car.pos, a.setCar->pos) >= r) continue;
            backAt[i] = sn.frame + (pads[i].big ? 10 : 4) * 120;
            ++teleportPickups;
        }
    }
    int missingAt(int64_t frame) const {
        int n = 0;
        for (int64_t b : backAt) n += frame < b ? 1 : 0;
        return n;
    }
    int missingOnPath(int64_t frame, KickoffSpawn s, KickoffTeam t) const {
        int n = 0;
        for (const auto& p : kickoffPadsOnPath(kickoffPath(s, t))) n += frame < backAt[static_cast<size_t>(p.padIndex)];
        return n;
    }
};

// Harness + pad model + trace of every tick.
struct Run {
    Harness h;
    PadWorld world;
    std::vector<std::pair<Snapshot, Actions>> trace;

    Actions tick(const Snapshot& sn, bool touch = false) {
        Actions a = h.tick(sn, touch);
        world.step(sn, a);
        trace.push_back({sn, a});
        return a;
    }
};

Snapshot carAt(int64_t frame, const Vec3& pos) {
    Snapshot s = makeSnap(frame);
    s.car.pos = pos;
    s.car.boost = 0.f;  // tank used up by the previous attempt
    return s;
}

struct RepCheck {
    KickoffSpawn spawn = KickoffSpawn::BackCenter;
    int64_t place = -1, reset = -1, go = -1;
    int resets = 0;
    bool placeHadReset = false;
    bool boostOk = true;  // 33.3 on the placement (car and tank), every countdown tick and GO
    int missingAtGo = -1, missingOnPathAtGo = -1;
    float boostAtGo = -1.f;
};

// From a tick that leaves the module in Countdown right after a placement (the last trace entry) to the GO tick
// included, the car resting on its spawn. Fills the rep facts from the trace.
RepCheck countdownToGo(Run& r, int64_t& f, float boost = kBoost33) {
    RepCheck c;
    const size_t placeIdx = r.trace.size() - 1;
    c.spawn = r.h.mod.currentSpawn();
    c.place = r.trace[placeIdx].first.frame;
    for (int guard = 0; guard < 2000 && r.h.mod.phase() == KickoffPhase::Countdown; ++guard)
        r.tick(restingSnap(++f, c.spawn, r.h.mod.currentTeam()));
    c.go = r.h.mod.goFrame();
    for (size_t i = placeIdx; i < r.trace.size(); ++i) {
        const Actions& a = r.trace[i].second;
        const int64_t fr = r.trace[i].first.frame;
        if (!a.setBoost || std::fabs(*a.setBoost - boost) > 1e-3f) c.boostOk = false;
        if (fr == c.place && (!a.setCar || !a.setCar->boost || std::fabs(*a.setCar->boost - boost) > 1e-3f))
            c.boostOk = false;
        if (!a.resetPickups) continue;
        ++c.resets;
        c.reset = fr;
        if (fr == c.place) c.placeHadReset = true;
    }
    c.missingAtGo = r.world.missingAt(c.go);
    c.missingOnPathAtGo = r.world.missingOnPath(c.go, c.spawn, r.h.mod.currentTeam());
    c.boostAtGo = r.world.hookBoost;
    return c;
}

// Plays the attempt from the tick after GO until the next placement (or the end of the drill); after the touch the
// car rolls on, then waits where it stopped.
void attemptToNextPlace(Run& r, int64_t& f, const RepCheck& c) {
    const SimOut sim = simulate(c.spawn, r.h.mod.currentTeam(), Script{}, c.go, 4.f);
    Vec3 last = sim.snaps.front().car.pos;
    for (size_t i = 1; i < sim.snaps.size(); ++i) {
        r.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
        last = sim.snaps[i].car.pos;
        if (r.h.mod.phase() != KickoffPhase::Live && r.h.mod.phase() != KickoffPhase::Done) return;
    }
    for (int guard = 0; guard < 2000 && r.h.mod.phase() == KickoffPhase::Done; ++guard) r.tick(carAt(++f, last));
}

bool hasCountdownText(const std::string& t) {
    return t.find("Decompte") != std::string::npos || t.find("Décompte") != std::string::npos ||
           t.find("GO dans") != std::string::npos || t.find("GO !") != std::string::npos;
}

DrawList drawNow(Harness& h, bool compact) {
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    view.ballValid = true;
    view.ballVisualPos = {0.f, 0.f, kKoBallRestZ};
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f, compact};
    h.mod.draw(view, dc, dl);
    for (auto& p : dl.panels) styleHudPanel(p, compact, 1.f, 1.f);  // what the engine does before drawing
    return dl;
}

bool cvarSet(const Actions& a, const std::string& cvar, const std::string& value) {
    for (const auto& c : a.setCvars)
        if (c.first == cvar && c.second == value) return true;
    return false;
}

} // namespace

// ---- Boost pads and boost at GO --------------------------------------------------------------------------------

TEST(ko_round2_pads_back_and_33_boost_at_go_rep_after_rep) {
    Run r;
    r.h.set("rlt_ko_ghost", "0");
    int64_t f = 0;
    r.tick(makeSnap(++f));
    r.h.command("start", {"sequence"});  // the 5 spawns in order, auto repeat
    r.tick(carAt(++f, {0.f, 0.f, kKoCarRestZ}));
    CHECK(r.h.mod.phase() == KickoffPhase::Countdown);
    std::vector<RepCheck> reps;
    for (int rep = 0; rep < kKoSpawnCount; ++rep) {
        const RepCheck c = countdownToGo(r, f);
        reps.push_back(c);
        CHECK(r.h.mod.phase() == KickoffPhase::Live);
        attemptToNextPlace(r, f, c);
    }
    CHECK(!r.h.mod.running());  // series over
    CHECK_EQ(reps.size(), size_t(kKoSpawnCount));
    for (size_t i = 0; i < reps.size(); ++i) {
        const RepCheck& c = reps[i];
        CHECK(c.spawn == kickoffSpawnFromIndex(static_cast<int>(i)));
        CHECK(!c.placeHadReset);  // never on the teleport tick
        CHECK_EQ(c.resets, 1);    // once per rep...
        CHECK_EQ(c.reset, c.place + kKoPadResetDelayTicks);  // ...once the car stayed on its spawn
        CHECK(c.reset < c.go);
        CHECK(c.boostOk);
        CHECK_NEAR(c.boostAtGo, kBoost33, 1e-3);
        CHECK_EQ(c.missingAtGo, 0);  // every pad of the field is there at GO
        CHECK_EQ(c.missingOnPathAtGo, 0);
    }
    // The model does reproduce the report: the teleports ran over pads, and with the old timing (reset on the
    // teleport tick) pads of the route were missing at GO.
    CHECK(r.world.teleportPickups > 0);
    PadWorld old;
    int oldMissingOnPath = 0;
    size_t repIdx = 0;
    for (const auto& [sn, a] : r.trace) {
        Actions b = a;
        b.resetPickups = a.boostRules.has_value();  // v1.0.7: the reset went with the placement
        old.step(sn, b);
        if (repIdx < reps.size() && sn.frame == reps[repIdx].go) {
            oldMissingOnPath += old.missingOnPath(sn.frame, reps[repIdx].spawn, KickoffTeam::Blue);
            ++repIdx;
        }
    }
    CHECK(oldMissingOnPath > 0);
}

TEST(ko_round2_pads_and_boost_after_manual_reset) {
    Run r;
    r.h.set("rlt_ko_ghost", "0");
    r.h.set("rlt_ko_auto_repeat", "0");
    int64_t f = 0;
    r.tick(makeSnap(++f));
    r.h.command("start", {"back"});
    r.tick(carAt(++f, {0.f, 0.f, kKoCarRestZ}));
    const RepCheck first = countdownToGo(r, f);
    CHECK_EQ(first.missingAtGo, 0);
    // Attempt, then the result stays on screen: the car rolled past the ball into the other half.
    const SimOut sim = simulate(KickoffSpawn::BackCenter, KickoffTeam::Blue, Script{}, first.go, 4.f);
    for (size_t i = 1; i < sim.snaps.size() && r.h.mod.phase() == KickoffPhase::Live; ++i) {
        r.tick(sim.snaps[i], static_cast<int>(i) == sim.touchIndex);
        f = sim.snaps[i].frame;
    }
    CHECK(r.h.mod.phase() == KickoffPhase::Done);
    const Vec3 parked{0.f, 1500.f, kKoCarRestZ};
    for (int i = 0; i < 600; ++i) r.tick(carAt(++f, parked));
    CHECK(r.h.mod.phase() == KickoffPhase::Done);  // waiting for Reset
    // Freeplay Reset: the next kickoff is placed on the next tick, from wherever the car is.
    r.h.event(GameEventKind::ResetShot);
    const int before = r.world.teleportPickups;
    const Actions place = r.tick(carAt(++f, parked));
    CHECK(r.h.mod.phase() == KickoffPhase::Countdown);
    CHECK(place.boostRules.has_value() && place.setCar.has_value());
    CHECK(!place.resetPickups);
    CHECK(r.world.teleportPickups > before);  // the move back runs over the pads of the centre line
    const RepCheck c = countdownToGo(r, f);
    CHECK(!c.placeHadReset);
    CHECK_EQ(c.resets, 1);
    CHECK_EQ(c.reset, c.place + kKoPadResetDelayTicks);
    CHECK(c.boostOk);
    CHECK_NEAR(c.boostAtGo, kBoost33, 1e-3);
    CHECK_EQ(c.missingAtGo, 0);
    CHECK_EQ(c.missingOnPathAtGo, 0);
}

TEST(ko_round2_pads_reset_again_when_the_car_is_moved_during_the_countdown) {
    // The game's reset during the countdown moves the car away: the re-pin brings it back over the pads, so the
    // reset is armed again and sent once it stayed on the spawn.
    {
        Run r;
        r.h.set("rlt_ko_ghost", "0");
        int64_t f = 0;
        r.tick(makeSnap(++f));
        r.h.command("start", {"diagonal_right"});
        r.tick(carAt(++f, {0.f, 0.f, kKoCarRestZ}));
        const int64_t place = r.h.mod.placeFrame();
        const size_t placeIdx = r.trace.size() - 1;
        while (f < place + 100) r.tick(restingSnap(++f, KickoffSpawn::DiagonalRight, KickoffTeam::Blue));
        CHECK(!r.h.mod.padResetPending());
        const Actions moved = r.tick(carAt(++f, {0.f, -4608.f, kKoCarRestZ}));
        const int64_t movedFrame = f;
        CHECK(moved.setCar.has_value() && !moved.resetPickups);
        CHECK(r.h.mod.padResetPending());
        const RepCheck c = countdownToGo(r, f);
        (void)c;
        std::vector<int64_t> resets;
        for (size_t i = placeIdx; i < r.trace.size(); ++i)
            if (r.trace[i].second.resetPickups) resets.push_back(r.trace[i].first.frame);
        CHECK_EQ(resets.size(), size_t(2));
        if (resets.size() == 2) {
            CHECK_EQ(resets[0], place + kKoPadResetDelayTicks);
            CHECK_EQ(resets[1], movedFrame + kKoPadResetDelayTicks);
        }
        CHECK_EQ(r.world.missingAt(r.h.mod.goFrame()), 0);
        CHECK_NEAR(r.world.hookBoost, kBoost33, 1e-3);
    }
    // Moved back too late for the delay: the reset goes with GO.
    {
        Run r;
        r.h.set("rlt_ko_ghost", "0");
        int64_t f = 0;
        r.tick(makeSnap(++f));
        r.h.command("start", {"back"});
        r.tick(carAt(++f, {0.f, 0.f, kKoCarRestZ}));
        const int64_t go = r.h.mod.goFrame();
        while (f < go - 11) r.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
        r.tick(carAt(++f, {0.f, 1500.f, kKoCarRestZ}));
        CHECK(r.h.mod.padResetPending());
        Actions goTick;
        while (r.h.mod.phase() == KickoffPhase::Countdown) {
            goTick = r.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
            if (r.h.mod.phase() == KickoffPhase::Countdown) CHECK(!goTick.resetPickups);
        }
        CHECK_EQ(f, go);
        CHECK(goTick.goMessage && goTick.resetPickups);
        CHECK(goTick.setBoost && std::fabs(*goTick.setBoost - kBoost33) < 1e-3f);
        CHECK_EQ(r.world.missingAt(go), 0);
    }
    // Car never seen on its spawn during the countdown (despawned): the reset goes with GO.
    {
        Harness h;
        h.set("rlt_ko_ghost", "0");
        int64_t f = 0;
        h.tick(makeSnap(++f));
        h.command("start", {"back"});
        h.tick(makeSnap(++f));
        int earlyResets = 0;
        Actions last;
        while (h.mod.phase() == KickoffPhase::Countdown) {
            Snapshot s = makeSnap(++f);
            s.car.valid = false;
            last = h.tick(s);
            if (h.mod.phase() == KickoffPhase::Countdown) earlyResets += last.resetPickups ? 1 : 0;
        }
        CHECK_EQ(earlyResets, 0);
        CHECK(last.goMessage && last.resetPickups);
    }
}

TEST(ko_round2_spawn_boost_setting_is_the_boost_at_go) {
    // The boost is in game units 0..100 (the glue divides by 100 for the SDK): 50 means half a tank.
    Run r;
    r.h.set("rlt_ko_ghost", "0");
    r.h.set("rlt_ko_spawn_boost", "50");
    int64_t f = 0;
    r.tick(makeSnap(++f));
    r.h.command("start", {"offcenter_left"});
    r.tick(carAt(++f, {0.f, 0.f, kKoCarRestZ}));
    const RepCheck c = countdownToGo(r, f, 50.f);
    CHECK(c.boostOk);
    CHECK_NEAR(c.boostAtGo, 50.f, 1e-3);
    // Default: 33.3, the match value (RocketSim BOOST_SPAWN_AMOUNT).
    Harness d;
    CHECK_NEAR(d.mod.config().spawnBoost, kKoSpawnBoost, 1e-4);
}

// ---- One countdown on screen -----------------------------------------------------------------------------------

TEST(ko_round2_one_countdown_by_default) {
    Harness h;
    CHECK(h.mod.config().nativeCountdown);
    CHECK(!h.mod.config().bigCountdown);
    const SettingDef* big = h.schema.find("rlt_ko_big_countdown");
    CHECK(big && big->defaultValue == "0");
    const SettingDef* native = h.schema.find("rlt_ko_native_countdown");
    CHECK(native && native->defaultValue == "1");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"diagonal_left"});
    std::vector<int> messages;
    bool goMessage = false;
    for (int guard = 0; guard < 600; ++guard) {
        const Actions a = h.tick(restingSnap(++f, KickoffSpawn::DiagonalLeft, KickoffTeam::Blue));
        if (a.countdownMessage) messages.push_back(*a.countdownMessage);
        goMessage = goMessage || a.goMessage;
        const KickoffPhase ph = h.mod.phase();
        if (ph == KickoffPhase::Live && f > h.mod.goFrame() + 70) break;
        if (ph != KickoffPhase::Countdown && ph != KickoffPhase::Live) continue;
        // Every frame of the countdown and the first second of GO: the game's countdown alone.
        for (bool compact : {true, false}) {
            const DrawList dl = drawNow(h, compact);
            CHECK(dl.texts2D.empty());  // no big number, no big GO
            for (const auto& p : dl.panels)
                for (const auto& l : p.lines) CHECK(!hasCountdownText(l.text));
        }
    }
    // The game's own 3, 2, 1, GO is sent.
    CHECK(messages == std::vector<int>({3, 2, 1}));
    CHECK(goMessage);
}

TEST(ko_round2_countdown_options) {
    // Big number on (the game's on too): still no countdown line in the panel.
    {
        Harness h;
        h.set("rlt_ko_big_countdown", "1");
        int64_t f = 0;
        h.tick(makeSnap(++f));
        h.command("start", {"back"});
        for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
        const DrawList dl = drawNow(h, true);
        bool bigThree = false;
        for (const auto& t : dl.texts2D) bigThree = bigThree || t.text == "3";
        CHECK(bigThree);
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines) CHECK(!hasCountdownText(l.text));
    }
    // Both off: the panel counts, on a main line kept by the compact HUD.
    {
        Harness h;
        h.set("rlt_ko_native_countdown", "0");
        int64_t f = 0;
        h.tick(makeSnap(++f));
        h.command("start", {"back"});
        for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::BackCenter, KickoffTeam::Blue));
        const DrawList dl = drawNow(h, true);
        CHECK(dl.texts2D.empty());
        int lines = 0;
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines) lines += l.text.find("Decompte : 3") != std::string::npos ? 1 : 0;
        CHECK_EQ(lines, 1);
    }
}

// ---- No lines by default ---------------------------------------------------------------------------------------

TEST(ko_round2_no_lines_by_default_ghost_box_kept) {
    Harness h;
    CHECK(!h.mod.config().showPath);
    const SettingDef* path = h.schema.find("rlt_ko_show_path");
    CHECK(path && path->defaultValue == "0");
    int64_t f = 0;
    h.tick(makeSnap(++f));
    h.command("start", {"diagonal_right"});
    for (int i = 0; i < 10; ++i) h.tick(restingSnap(++f, KickoffSpawn::DiagonalRight, KickoffTeam::Blue));
    CHECK(h.mod.ghostActive());  // adaptive ghost by default
    DrawList dl = drawNow(h, false);
    CHECK_EQ(dl.lines3D.size(), size_t(12));  // the 12 edges of the ghost box, nothing on the floor
    for (const auto& l : dl.lines3D) CHECK(std::max(l.a.z, l.b.z) > 10.f);
    CHECK_EQ(dl.texts3D.size(), size_t(1));  // "Fantome ..."
    // During the attempt too.
    const int64_t go = h.runCountdown(f);
    const SimOut sim = simulate(KickoffSpawn::DiagonalRight, KickoffTeam::Blue, Script{}, go, 4.f);
    for (size_t i = 0; i < 60; ++i) h.tick(sim.snaps[i]);
    dl = drawNow(h, false);
    CHECK_EQ(dl.lines3D.size(), size_t(12));
    // Option on: the lines come back.
    h.set("rlt_ko_show_path", "1");
    dl = drawNow(h, false);
    CHECK(dl.lines3D.size() > 12 + 1 + 3);
}

// ---- One-time move of the old defaults (v3) --------------------------------------------------------------------

TEST(ko_round2_old_display_defaults_migrated_once) {
    // A player of v1.0.7: big countdown and lines on (the old defaults, saved in config.cfg), defaults_v2 done.
    Store v107;
    v107.setInt("kickoff.defaults_v2", 1);
    Harness h;
    h.mod.load(v107);
    h.set("rlt_ko_big_countdown", "1");
    h.set("rlt_ko_show_path", "1");
    h.set("rlt_ko_ghost", "3");  // Pro ghost and Pro grading chosen after the v2 step: untouched
    h.set("rlt_ko_grade_target", "2");
    int64_t f = 0;
    const Actions first = h.tick(makeSnap(++f));
    CHECK_EQ(first.setCvars.size(), size_t(2));
    CHECK(cvarSet(first, "rlt_ko_big_countdown", "0"));
    CHECK(cvarSet(first, "rlt_ko_show_path", "0"));
    CHECK(first.requestSave);
    // The countdown sentence, then the toast shared by every trajectory (the engine merges them into one).
    CHECK_EQ(first.toasts.size(), size_t(2));
    if (first.toasts.size() == 2) {
        CHECK(first.toasts[0].text.find("celui du jeu") != std::string::npos);
        CHECK(first.toasts[0].text.find("lignes") == std::string::npos);
        CHECK(first.toasts[1].title == "Trajectoires");
        CHECK(first.toasts[1].text.find("page HUD, section Trajectoires") != std::string::npos);
        for (const auto& t : first.toasts) {
            CHECK(t.migration);
            CHECK(!hasEmDash(t.text));
            CHECK(t.text.find("\xE2\x80\x93") == std::string::npos);  // no en dash
        }
    }
    // The glue applies the cvars; nothing more on the next ticks.
    for (const auto& c : first.setCvars) h.set(c.first, c.second);
    CHECK(!h.mod.config().bigCountdown && !h.mod.config().showPath);
    const Actions second = h.tick(makeSnap(++f));
    CHECK(second.setCvars.empty() && second.toasts.empty());
    // Saved: a reload with the old values (config.cfg not yet rewritten) does not migrate again.
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("kickoff.defaults_v3", 0), 1);
    CHECK_EQ(st.getInt("kickoff.defaults_v2", 0), 1);
    Harness again;
    again.mod.load(st);
    again.set("rlt_ko_big_countdown", "1");
    again.set("rlt_ko_show_path", "1");
    const Actions reload = again.tick(makeSnap(1));
    CHECK(reload.setCvars.empty() && reload.toasts.empty());
}

TEST(ko_round2_display_migration_keeps_player_choices) {
    // Game countdown turned off by the player: the big number is the only countdown, kept. The lines still go.
    {
        Harness h;
        h.mod.load(Store{});
        h.set("rlt_ko_native_countdown", "0");
        h.set("rlt_ko_big_countdown", "1");
        h.set("rlt_ko_show_path", "1");
        const Actions a = h.tick(makeSnap(1));
        CHECK_EQ(a.setCvars.size(), size_t(1));
        CHECK(cvarSet(a, "rlt_ko_show_path", "0"));
        CHECK_EQ(a.toasts.size(), size_t(1));
        if (!a.toasts.empty())
            CHECK(a.toasts[0].text.find("décompte") == std::string::npos && a.toasts[0].title == "Trajectoires");
    }
    // Already off (new install, or chosen): nothing changed, no toast, only the flag saved.
    {
        Harness h;
        h.mod.load(Store{});
        const Actions a = h.tick(makeSnap(1));
        CHECK(a.setCvars.empty() && a.toasts.empty() && a.requestSave);
        Store st;
        h.mod.save(st);
        CHECK_EQ(st.getInt("kickoff.defaults_v3", 0), 1);
    }
    // Only the big countdown at its old value: only that one moves.
    {
        Harness h;
        h.mod.load(Store{});
        h.set("rlt_ko_big_countdown", "1");
        const Actions a = h.tick(makeSnap(1));
        CHECK_EQ(a.setCvars.size(), size_t(1));
        CHECK(cvarSet(a, "rlt_ko_big_countdown", "0"));
    }
    // Both steps pending (save from before v2): the v2 pair and the v3 display defaults move on the same tick.
    {
        Harness h;
        h.mod.load(Store{});
        h.set("rlt_ko_ghost", "3");
        h.set("rlt_ko_grade_target", "2");
        h.set("rlt_ko_big_countdown", "1");
        h.set("rlt_ko_show_path", "1");
        const Actions a = h.tick(makeSnap(1));
        CHECK_EQ(a.setCvars.size(), size_t(4));
        CHECK(cvarSet(a, "rlt_ko_ghost", "6") && cvarSet(a, "rlt_ko_grade_target", "1"));
        CHECK(cvarSet(a, "rlt_ko_big_countdown", "0") && cvarSet(a, "rlt_ko_show_path", "0"));
        CHECK_EQ(a.toasts.size(), size_t(3));  // ghost and grading, countdown, trajectories
    }
}
