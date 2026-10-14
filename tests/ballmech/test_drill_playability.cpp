// Drill playability (audit fix 10): every drill is checked with the core ball physics and simple car models (fall
// from the ceiling, wall climb, ground and air reach) for a player who reacts in 0.3 to 0.5 s, and the full
// sequences ("parcours complet") are played by scripted players through the module, asserting SUCCESS.
#include <functional>
#include <optional>

#include "core/common/Pickup.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Parcours.h"
#include "core/modules/ballmech/Tips.h"
#include "core/modules/ballmech/Tutorials.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Playability.h"
#include "tests/ballmech/Sim.h"

// The ballmech test binary does not link the prediction module: compile its ball physics here (rlt::pr only).
#include "core/modules/prediction/BallPhysics.cpp"

using namespace rlt;
using namespace rlt::bm;
using namespace bmtest;

namespace {

DrillSetup setupAt(Mech m, int niveau, int team, Rng& rng, DrillMode mode = DrillMode::Short, int pickupNiveau = 1,
                   const std::string& variant = "") {
    CarState car;
    SetupOptions o;
    o.mode = mode;
    o.pickupNiveau = pickupNiveau;
    o.flickVariant = variant;
    return makeDrillSetup(m, levelOfPlayer(niveau), team, car, 91.25f, rng, o);
}

std::string levelArg(int n) { return std::to_string(levelOfPlayer(n)); }

// Renumbers a scenario so it continues right after the harness' latest snapshot.
std::vector<SimTick> continueFrom(const Harness& h, std::vector<SimTick> ticks) {
    if (ticks.empty()) return ticks;
    const int64_t offset = h.latest.frame + 1 - ticks.front().s.frame;
    for (auto& t : ticks) {
        t.s.frame += offset;
        t.s.time = static_cast<float>(t.s.frame) / kTickRate;
    }
    return ticks;
}

void warmUp(Harness& h, int team = 0) {
    Sim sim;
    sim.s.car.team = team;
    orient(sim.s.car, team == 0 ? PI / 2 : -PI / 2, 0, 0);
    sim.s.ball.pos = {0, team == 0 ? 1000.f : -1000.f, 93};
    sim.step(10);
    h.run(sim.out);
}

// Drives the module tick by tick from its latest snapshot. `f` edits the next snapshot (positions are moved by the
// caller: the harness never applies the module's placements).
struct Script {
    Harness& h;
    Snapshot s;
    int64_t touchFrame = 0;
    explicit Script(Harness& hh) : h(hh), s(hh.latest) { s.in = Inputs{}; }
    const Actions& step(const std::function<void(Snapshot&)>& f = nullptr, bool touch = false, bool sdk = false) {
        s.frame += 1;
        s.time = static_cast<float>(s.frame) / kTickRate;
        s.in = Inputs{};
        if (f) f(s);
        if (touch) s.car.lastBallTouchFrame = static_cast<int>(s.frame);
        SimTick t;
        t.s = s;
        t.touched = touch;
        t.newContact = true;
        t.sdkReset = sdk;
        h.tick(t);
        return h.last;
    }
    void place(const CarPlacement& c) {
        s.car.pos = c.pos;
        s.car.vel = c.vel;
        s.car.rot = c.rot;
        s.car.basis = basisFromRot(c.rot);
        s.car.angVel = {};
    }
    void setRot(const Rot& r) {
        s.car.rot = r;
        s.car.basis = basisFromRot(r);
    }
};

void ground(CarState& c) {
    c.onGround = true;
    c.wheelContacts = 4;
    c.jumped = false;
    c.hasFlip = true;
}
void air(CarState& c) {
    c.onGround = false;
    c.wheelContacts = 0;
}

bool stepOk(const BallMechModule::UiState& u, int i) {
    return i < static_cast<int>(u.drill.steps.size()) && u.drill.steps[static_cast<size_t>(i)].find(" : OK") != std::string::npos;
}

DrawList drawHud(Harness& h, bool compact) {
    DrawList dl;
    DrawContext dc{h.settings, h.game, h.profile, h.latest, true, true, 1.f, compact};
    ViewContext v;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    h.mod.draw(v, dc, dl);
    return dl;
}

int mainLineCount(const DrawPanel& p) {
    int n = 0;
    for (const auto& l : p.lines) n += l.detail ? 0 : 1;
    return n;
}

bool noDash(const std::string& s) {
    return s.find("\xE2\x80\x94") == std::string::npos && s.find("\xE2\x80\x93") == std::string::npos;
}

} // namespace

// =============================================================================================================
// Ceiling shot
// =============================================================================================================
TEST(bm_play_ceiling_short_is_playable) {
    // "Départ au plafond": the ball waits high, flipping in front of it hits it, doing nothing never does.
    for (int n = 1; n <= 10; ++n) {
        Rng rng(100 + static_cast<uint64_t>(n));
        for (int k = 0; k < 50; ++k) {
            const int team = k & 1;
            const DrillSetup d = setupAt(Mech::Ceiling, n, team, rng);
            const std::string what = "niveau " + std::to_string(n);
            CHECK_MSG(d.ball.pos.z >= (n == 1 ? 1300.f : 650.f), what);
            CHECK_MSG(d.armCeiling && std::fabs(d.car.pos.z - 2027.f) < 1.f, what);
            if (k >= 6) continue;  // the flip windows (heavy): a few seeds per level, both teams
            for (float leave : {0.f, 0.1f, 0.25f}) {
                const pb::FlipWindow w = pb::ceilingFlipWindow(d.car.pos, d.car.vel, d.car.rot, d.ball.pos, d.ball.vel, leave, 0.3f);
                CHECK_MSG(w.passive < 0.f, what + " passive contact");
                const float need = n == 1 ? 0.40f : (n <= 4 ? 0.33f : 0.20f);
                CHECK_MSG(w.len >= need, what + " window " + std::to_string(w.len));
                CHECK_MSG(w.first >= 0.3f, what);
            }
        }
    }
    // The ball is met at the car's height, between 1300 and 1800 uu.
    for (int n : {1, 4, 7, 10}) {
        Rng rng(7);
        const DrillSetup d = setupAt(Mech::Ceiling, n, 0, rng);
        const float tm = n == 1 ? 1.f : (n <= 3 ? 1.1f : (n <= 6 ? 1.2f : (n <= 9 ? 1.35f : 1.45f)));
        const auto ball = pb::simBall(d.ball.pos, d.ball.vel, tm + 0.1f);
        const float z = ball[static_cast<size_t>(tm * kTickRate)].p.z;
        CHECK_MSG(z >= 1300.f && z <= 1800.f, std::to_string(z));
    }
    // Readability of the first levels: slow motion and the 3D guide.
    Rng rng(3);
    const DrillSetup d1 = setupAt(Mech::Ceiling, 1, 0, rng);
    CHECK_NEAR(d1.gameSpeed, 0.75f, 1e-6f);
    CHECK(d1.hasMeet && d1.guideCount >= 2);
    CHECK_NEAR(setupAt(Mech::Ceiling, 2, 0, rng).gameSpeed, 0.85f, 1e-6f);
    CHECK_NEAR(setupAt(Mech::Ceiling, 3, 0, rng).gameSpeed, 1.f, 1e-6f);
    CHECK(!setupAt(Mech::Ceiling, 5, 0, rng).hasMeet);
}

TEST(bm_play_ceiling_short_immediate_drop_is_graded) {
    // The car leaves the ceiling on the first tick after GO: the armed detector still grades the drop.
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling", "court"});
    CHECK(!h.mod.uiState().drill.full);
    h.skipReady();
    CHECK(h.last.setCar.has_value() && h.last.setBall.has_value());
    if (!h.last.setCar || !h.last.setBall) return;
    CHECK(h.last.setGameSpeed.has_value() && std::fabs(*h.last.setGameSpeed - 0.75f) < 1e-6f);
    const CarPlacement c0 = *h.last.setCar;
    const BallPlacement b0 = *h.last.setBall;
    Script sc(h);
    sc.place(c0);
    sc.s.ball.pos = b0.pos;
    sc.s.ball.vel = b0.vel;
    air(sc.s.car);
    sc.s.car.hasFlip = true;
    auto fly = [](Snapshot& x) {
        x.car.vel.z += bm::arena::kGravityZ * DT;
        x.car.pos += x.car.vel * DT;
        x.ball.vel.z += bm::arena::kGravityZ * DT;
        x.ball.pos += x.ball.vel * DT;
    };
    // Fall until the reaction is over and the ball is near, then flip into it.
    for (int i = 0; i < 300 && (i < 48 || distance(sc.s.car.pos, sc.s.ball.pos) > 230.f); ++i) sc.step(fly);
    sc.step([&](Snapshot& x) { fly(x); });
    sc.step([&](Snapshot& x) {
        fly(x);
        x.in.jump = true;
        x.in.dodgeForward = 1.f;
        x.in.pitch = -1.f;
        x.car.dodging = true;
        x.car.hasFlip = false;
        x.car.dodgeDir = {1.f, 0.f, 0.f};
    });
    sc.step([&](Snapshot& x) { fly(x); });
    sc.step([&](Snapshot& x) { fly(x); }, true);
    sc.s.ball.vel = {0.f, 1800.f, 300.f};
    for (int i = 0; i < 40; ++i) sc.step(fly);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.ceiling");
        CHECK(h.results[0].success);
    }
}

namespace {
struct CeilingRun {
    bool released = false;
    Vec3 releaseVel;
    int heldTicks = 0, liveTicks = 0, setBallAfterRelease = 0, releaseTicks = 0;
    std::vector<int> order;  // step indexes in the order they turned OK
    size_t resultsBeforeDrop = 0;
    Vec3 releasePos;
    CarState carAtRelease;
    float flipWindow = 0.f, passive = -1.f, touchAfter = -1.f;  // flip-only window, passive contact, touch (s)
};

// Scripted player for the ceiling full sequence: drives to the side wall, climbs, rides the ceiling, lets go (with a
// jump on the leave tick when `jumpOff`), falls without input and flips into the released ball after its reaction.
CeilingRun playCeilingRoute(Harness& h, bool jumpOff) {
    CeilingRun r;
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return r;
    const CarPlacement c0 = *h.last.setCar;
    const Vec3 hang = h.last.setBall->pos;
    const float side = c0.pos.x > 0.f ? 1.f : -1.f;
    const float sign = c0.pos.y > -100.f && h.latest.car.team == 1 ? -1.f : (h.latest.car.team == 1 ? -1.f : 1.f);
    Script sc(h);
    sc.place(c0);
    ground(sc.s.car);
    sc.s.ball.pos = hang;
    sc.s.ball.vel = {};
    auto watch = [&]() {
        const auto u = h.mod.uiState();
        for (int i = 0; i < static_cast<int>(u.drill.steps.size()); ++i)
            if (stepOk(u, i) && std::find(r.order.begin(), r.order.end(), i) == r.order.end()) r.order.push_back(i);
        if (!r.released) {
            if (h.last.setBall && length(h.last.setBall->vel) < 1e-3f) ++r.heldTicks;
            if (h.last.setBall && length(h.last.setBall->vel) > 1.f) {
                r.released = true;
                r.releasePos = h.last.setBall->pos;
                r.releaseVel = h.last.setBall->vel;
                ++r.releaseTicks;
            }
        } else if (h.last.setBall) {
            ++r.setBallAfterRelease;
        }
        ++r.liveTicks;
    };
    auto held = [&](Snapshot& x) {
        if (!r.released) {
            x.ball.pos = hang;
            x.ball.vel = {};
        } else {
            x.ball.vel.z += bm::arena::kGravityZ * DT;
            x.ball.pos += x.ball.vel * DT;
        }
    };
    // 1) Floor: 0.4 s reaction (rolling), then boost toward the wall.
    sc.s.car.vel = flat(sc.s.car.basis.fwd) * 600.f;
    for (int i = 0; std::fabs(sc.s.car.pos.x) < 3750.f && i < 600; ++i) {
        sc.step([&](Snapshot& x) {
            if (i > 48) x.car.vel = flat(x.car.basis.fwd) * 1400.f, x.in.throttle = 1.f, x.in.boost = true;
            x.car.pos += x.car.vel * DT;
            held(x);
        });
        watch();
    }
    // 2) Wall: wheels on the side wall, climbing.
    sc.s.car.pos = {side * 4079.f, sc.s.car.pos.y, 300.f};
    // Wheels on the wall: written for blue (yaw 90, roll side * 90), turned around for orange (the side flips too).
    const float teamSign = sc.s.car.team == 1 ? -1.f : 1.f;
    sc.setRot(rotDeg(72.6f, teamSign * 90.f, side * teamSign * 90.f));
    sc.s.car.vel = {0.f, 250.f * (sc.s.car.team == 1 ? -1.f : 1.f), 1300.f};
    for (int i = 0; sc.s.car.pos.z < 1850.f && i < 400; ++i) {
        sc.step([&](Snapshot& x) {
            x.in.throttle = 1.f;
            x.car.pos += x.car.vel * DT;
            held(x);
        });
        watch();
    }
    // 3) Ceiling: wheels up, nose inward.
    const float yawIn = std::atan2(0.34f * (sc.s.car.team == 1 ? -1.f : 1.f), -side * 0.94f) * kRadToDeg;
    sc.s.car.pos = {side * 3500.f, sc.s.car.pos.y, 2027.f};
    sc.setRot(rotDeg(0.f, yawIn, 180.f));
    sc.s.car.vel = flat(sc.s.car.basis.fwd) * 1300.f;
    for (int i = 0; i < 20; ++i) {
        sc.step([&](Snapshot& x) {
            x.in.throttle = 1.f;
            x.car.pos += x.car.vel * DT;
            held(x);
        });
        watch();
    }
    r.resultsBeforeDrop = h.results.size();
    // 4) Let go (no jump unless asked), fall until the step validates and the ball is released.
    air(sc.s.car);
    for (int i = 0; i < 60 && !r.released && h.mod.uiState().drill.phase == "À toi !"; ++i) {
        sc.step([&](Snapshot& x) {
            if (i == 0 && jumpOff) x.in.jump = true;
            x.car.vel.z += bm::arena::kGravityZ * DT;
            x.car.pos += x.car.vel * DT;
            held(x);
        });
        watch();
    }
    if (jumpOff || !r.released) return r;
    // 5) Passive fall from the release (core ball physics, the auditors' fall and flip model): after the reaction,
    // flip in the middle of the flip-only window, the touch comes from the car's own path.
    r.carAtRelease = sc.s.car;
    sc.s.ball.pos = r.releasePos;
    sc.s.ball.vel = r.releaseVel;
    const pb::FlipWindow w = pb::ceilingFlipWindow(sc.s.car.pos, sc.s.car.vel, sc.s.car.rot, r.releasePos, r.releaseVel,
                                                   0.f, pb::kReact);
    r.flipWindow = w.len;
    r.passive = w.passive;
    if (w.first < 0.f) return r;
    const float tf = 0.5f * (w.first + w.last);
    const auto carPath = pb::ceilingPath(sc.s.car.pos, sc.s.car.vel, sc.s.car.basis, 0.f, tf, 2.f);
    const auto ballPath = pb::simBall(r.releasePos, r.releaseVel, 2.f);
    const int flipTick = static_cast<int>(std::lround(tf * kTickRate));
    for (size_t i = 1; i < carPath.size() && i < ballPath.size(); ++i) {
        const bool touch = pb::touching(carPath[i], sc.s.car.basis, ballPath[i].p);
        sc.step(
            [&](Snapshot& x) {
                x.car.vel = (carPath[i] - x.car.pos) / DT;
                x.car.pos = carPath[i];
                x.ball.pos = ballPath[i].p;
                x.ball.vel = ballPath[i].v;
                if (static_cast<int>(i) == flipTick) {
                    x.in.jump = true;
                    x.in.dodgeForward = 1.f;
                    x.in.pitch = -1.f;
                    x.car.dodging = true;
                    x.car.hasFlip = false;
                    x.car.dodgeDir = {1.f, 0.f, 0.f};
                }
            },
            touch);
        watch();
        if (touch) {
            r.touchAfter = static_cast<float>(i) * kTickDt;
            break;
        }
    }
    if (r.touchAfter < 0.f) return r;
    sc.s.ball.vel = {0.f, 2200.f * sign, 300.f};
    for (int i = 0; i < 40; ++i) {
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.vel.z += bm::arena::kGravityZ * DT;
            x.ball.pos += x.ball.vel * DT;
        });
        watch();
    }
    return r;
}
} // namespace

TEST(bm_play_roof_start_waits_for_the_throttle_release) {
    // Ball balanced on a stopped car (air dribble, départ court, niveau 1): the player presses the throttle to start the
    // countdown and keeps it held (the kickoff habit). GO waits until he lets go, the HUD says so, and GO comes on the
    // release tick: the held throttle never throws the ball off the roof. Same on the automatic restarts.
    auto releaseLine = [](Harness& h) {
        bool found = false;
        for (const auto& p : drawHud(h, true).panels)
            for (const auto& l : p.lines)
                found = found || (!l.detail && l.text.find("Lâche l'accélérateur pour partir") != std::string::npos);
        return found;
    };
    Harness h;
    warmUp(h);
    h.command("drill", {"airdribble", "court"});
    CHECK(!h.mod.uiState().drill.full);
    h.throttleTick(0.f);
    for (int i = 0; i < 6 * 120; ++i) {
        h.throttleTick(1.f);
        CHECK(h.mod.uiState().drill.phase != "À toi !");
        CHECK(h.last.freezeCar);
    }
    CHECK(releaseLine(h));
    h.throttleTick(0.f);
    CHECK(h.mod.uiState().drill.phase == "À toi !");
    CHECK(h.last.setCar.has_value() && !h.last.freezeCar);
    // Automatic restart (here the reset shot key) with the throttle still held: the short hold waits too.
    h.event(GameEventKind::ResetShot);
    for (int i = 0; i < 4 * 120; ++i) {
        h.throttleTick(1.f);
        CHECK(h.mod.uiState().drill.phase != "À toi !");
    }
    CHECK(releaseLine(h));
    h.throttleTick(0.f);
    CHECK(h.mod.uiState().drill.phase == "À toi !");
    // A start where the throttle does not matter (pinch, car moving) is not held back.
    Harness g;
    warmUp(g);
    g.command("drill", {"pinch"});
    g.throttleTick(0.f);
    int go = -1;
    for (int i = 0; i < 6 * 120 && go < 0; ++i) {
        g.throttleTick(1.f);
        if (g.mod.uiState().drill.phase == "À toi !") go = i;
    }
    CHECK(go >= 0 && go <= static_cast<int>(3.1f * kTickRate));
}

TEST(bm_play_ceiling_route_steps) {
    // Default start of the ceiling shot: the full sequence. Steps 1 to 5 in order, the ball held still until the
    // car leaves the ceiling (niveaux 1-3), then relaunched once in front of the falling car (apex 0.8 s later), and
    // a success for a player who just falls and flips after a 0.4 s reaction.
    for (int team = 0; team <= 1; ++team) {
        Harness h;
        warmUp(h, team);
        h.command("drill", {"ceiling"});
        CHECK(h.mod.uiState().drill.full);
        CHECK(h.mod.uiState().drill.modeLabel == "Parcours complet");
        const CeilingRun r = playCeilingRoute(h, false);
        CHECK_EQ(r.resultsBeforeDrop, 0u);  // nothing ends the attempt on the way up (no landing, no other mechanic)
        CHECK(r.released);
        CHECK_EQ(r.releaseTicks, 1);
        CHECK_EQ(r.setBallAfterRelease, 0);
        CHECK(r.heldTicks > 300);  // every tick from GO until the release
        CHECK(std::fabs(r.releaseVel.x) < 1e-3f && std::fabs(r.releaseVel.y) < 1e-3f &&
              std::fabs(r.releaseVel.z - 0.8f * 650.f) < 1e-2f);
        // Relaunched ahead of the car along its fall, below it (it rises to the car's height).
        const Vec3 rel = r.releasePos - r.carAtRelease.pos;
        CHECK(dot(flat(rel), normalize(flat(r.carAtRelease.vel))) > 260.f && rel.z < 0.f);
        CHECK_MSG(r.flipWindow >= 0.30f, std::to_string(r.flipWindow));
        CHECK(r.passive < 0.f);
        CHECK(r.touchAfter > pb::kReact);
        CHECK(r.order.size() >= 4);
        if (r.order.size() >= 4) {
            CHECK_EQ(r.order[0], 0);
            CHECK_EQ(r.order[1], 1);
            CHECK_EQ(r.order[2], 2);
            CHECK_EQ(r.order.back(), 4);
        }
        CHECK_EQ(h.results.size(), 1u);
        if (!h.results.empty()) {
            CHECK(h.results[0].skill == "ball.ceiling");
            CHECK_MSG(h.results[0].success, h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code);
        }
        CHECK_EQ(h.mod.uiState().drill.successes, 1);
    }
}

TEST(bm_play_ceiling_route_jump_off_fails) {
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling"});
    playCeilingRoute(h, true);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(!h.results[0].tips.empty() && h.results[0].tips[0].code == "bm.ceil_jumped_off");
    }
    const auto u = h.mod.uiState();
    CHECK(u.drill.missing.find("Lâche-toi") != std::string::npos);
}

TEST(bm_play_ceiling_route_touch_frees_the_ball) {
    // A hit on the hanging ball is never erased: from the touch tick on, nothing re-places the ball.
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling"});
    h.skipReady();
    if (!h.last.setBall) return;
    const Vec3 hang = h.last.setBall->pos;
    Script sc(h);
    ground(sc.s.car);
    sc.s.ball.pos = hang;
    sc.s.ball.vel = {};
    for (int i = 0; i < 30; ++i) {
        const Actions& a = sc.step([&](Snapshot& x) { x.ball.pos = hang; });
        CHECK(a.setBall.has_value());
    }
    int after = 0;
    sc.step([&](Snapshot& x) { x.ball.vel = {300.f, 200.f, 400.f}; }, true);
    after += h.last.setBall ? 1 : 0;
    for (int i = 0; i < 60; ++i) {
        sc.step([&](Snapshot& x) { x.ball.pos += x.ball.vel * DT; });
        after += h.last.setBall ? 1 : 0;
    }
    CHECK_EQ(after, 0);
}

TEST(bm_play_ceiling_route_reach) {
    // Niveaux 1-3 (the default start of a new player): a car cannot drive on the ceiling, so where it drops is set by
    // its heading on the wall. Whatever the climb (heading 0 to 35 deg off the wall normal, boost let go at z 300 to
    // 2100, start speed 0 or 600, 0.4 s reaction, 0.12 s on the ceiling), the ball relaunched 0.3 s after the car left
    // the ceiling gives a flip-only window of at least 0.30 s after the reaction, and a passive fall never touches it.
    Rng rng(3);
    const DrillSetup d = setupAt(Mech::Ceiling, 1, 0, rng, DrillMode::Full);
    CHECK(d.releaseStep == "ceil.drop" && d.releaseApex > 0.f);
    int cases = 0;
    for (float phi = 0.f; phi <= 35.f; phi += 5.f)
        for (float zc : {300.f, 600.f, 900.f, 1200.f, 2100.f})
            for (float v0 : {0.f, 600.f})
                for (int team = 0; team <= 1; ++team) {
                    const pb::Climb c = pb::climb(940.f, phi, v0, pb::kReact, zc);
                    if (c.tDrop < 0.f) continue;
                    // 0.3 s of fall (0.1 s at half gravity while the wheels let go): the "Lâche-toi" step validates.
                    Vec3 p = c.dropPos + Vec3{0.f, -1600.f, 0.f}, v = c.dropVel;
                    for (int i = 1; i <= 36; ++i) {
                        v.z += (static_cast<float>(i) * kTickDt <= 0.1f ? -325.f : -650.f) * kTickDt;
                        p += v * kTickDt;
                    }
                    const float m = team == 0 ? 1.f : -1.f;  // orange: the same climb on the other side
                    CarState car;
                    car.pos = {m * p.x, m * p.y, p.z};
                    car.vel = {m * v.x, m * v.y, v.z};
                    car.rot = rotDeg(0.f, std::atan2(car.vel.y, car.vel.x) * kRadToDeg, 180.f);
                    car.basis = basisFromRot(car.rot);
                    const BallPlacement b = ceilingReleaseAim(car, d.releaseApex);
                    const pb::FlipWindow w = pb::ceilingFlipWindow(car.pos, car.vel, car.rot, b.pos, b.vel, 0.f, pb::kReact);
                    const std::string what = "heading " + std::to_string(phi) + " boost until " + std::to_string(zc) +
                                             " v0 " + std::to_string(v0) + " team " + std::to_string(team);
                    CHECK_MSG(w.len >= 0.30f, what + " window " + std::to_string(w.len));
                    CHECK_MSG(w.passive < 0.f, what + " passive contact");
                    CHECK_MSG(std::fabs(b.pos.x) < 4096.f - 200.f && std::fabs(b.pos.y) < 5120.f - 200.f && b.pos.z > 200.f, what);
                    ++cases;
                }
    CHECK(cases >= 150);
}

// =============================================================================================================
// Flip reset
// =============================================================================================================
TEST(bm_play_flipreset_not_free) {
    // With no input the car meets the ball side or roof first: never a free reset.
    for (int n = 1; n <= 10; ++n) {
        Rng rng(200 + static_cast<uint64_t>(n));
        for (int k = 0; k < 60; ++k) {
            const DrillSetup d = setupAt(Mech::FlipReset, n, k & 1, rng);
            const Basis B = basisFromRot(d.car.rot);
            Vec3 v = d.car.vel + B.up * bm::arena::kJumpImpulse;  // the scripted double jump gives it back
            Vec3 p = d.car.pos;
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 3.f);
            for (size_t i = 1; i < ball.size(); ++i) {
                v.z += bm::arena::kGravityZ * kTickDt;
                p += v * kTickDt;
                if (!pb::touching(p, B, ball[i].p)) continue;
                const Vec3 hc = p + B.fwd * 13.88f + B.up * 20.75f;
                CHECK_MSG(dot(-B.up, normalize(ball[i].p - hc)) < 0.5f, "niveau " + std::to_string(n));
                break;
            }
        }
    }
}

TEST(bm_play_flipreset_reachable) {
    // A player who reacts in 0.3 s and air rolls (5.5 rad/s) the way the hint says has the wheels on the ball at least
    // 0.3 s before the contact, arrives softly, high enough to use the reset.
    for (int n = 1; n <= 7; ++n) {
        Rng rng(300 + static_cast<uint64_t>(n));
        for (int k = 0; k < 40; ++k) {
            const DrillSetup d = setupAt(Mech::FlipReset, n, k & 1, rng);
            const Vec3 toBall = normalize(d.ball.pos - d.car.pos);
            // The roll that brings the wheels to the ball, the short way (the hint at niveaux 1-4).
            float best = -2.f, bestRoll = 0.f;
            for (float a = -180.f; a <= 180.f; a += 1.f) {
                Rot r = d.car.rot;
                r.roll += static_cast<int>(std::lround(a * 65536.f / 360.f));
                const float al = dot(-basisFromRot(r).up, toBall);
                if (al > best) best = al, bestRoll = a;
            }
            if (d.airRollHint != 0 && std::fabs(bestRoll) < 179.f) CHECK(bestRoll * static_cast<float>(d.airRollHint) > 0.f);
            Vec3 v = d.car.vel + basisFromRot(d.car.rot).up * bm::arena::kJumpImpulse, p = d.car.pos;
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 3.f);
            float alignedAt = -1.f;
            bool contact = false;
            for (size_t i = 1; i < ball.size() && !contact; ++i) {
                const float t = static_cast<float>(i) * kTickDt;
                const float rolled = clampf((t - 0.3f) * 5.5f * kRadToDeg, 0.f, std::fabs(bestRoll)) * (bestRoll < 0.f ? -1.f : 1.f);
                Rot r = d.car.rot;
                r.roll += static_cast<int>(std::lround(rolled * 65536.f / 360.f));
                const Basis Bt = basisFromRot(r);
                v.z += bm::arena::kGravityZ * kTickDt;
                p += v * kTickDt;
                const Vec3 hc = p + Bt.fwd * 13.88f + Bt.up * 20.75f;
                if (alignedAt < 0.f && dot(-Bt.up, normalize(ball[i].p - hc)) > 0.85f) alignedAt = t;
                if (pb::touching(p, Bt, ball[i].p)) {
                    contact = true;
                    const std::string what = "niveau " + std::to_string(n);
                    CHECK_MSG(alignedAt >= 0.f && t - alignedAt >= 0.3f, what);
                    CHECK_MSG(length(v - ball[i].v) < 700.f, what);
                    CHECK_MSG(ball[i].p.z > 900.f, what + " z " + std::to_string(ball[i].p.z));
                }
            }
            CHECK(contact);
        }
    }
    // Slow motion at niveaux 1-2 of the short start.
    Rng rng(1);
    CHECK_NEAR(setupAt(Mech::FlipReset, 1, 0, rng).gameSpeed, 0.75f, 1e-6f);
    CHECK_NEAR(setupAt(Mech::FlipReset, 2, 0, rng).gameSpeed, 0.85f, 1e-6f);
    CHECK_NEAR(setupAt(Mech::FlipReset, 5, 0, rng).gameSpeed, 1.f, 1e-6f);
}

namespace {
// Scripted full sequence of the flip reset at niveau 5: takeoff after 0.4 s, straight under the ball, wheels up,
// reset (game event), 0.3 s later a flip into the ball. `landEarly`: comes back down before the reset.
void playFlipResetRoute(Harness& h, bool landEarly) {
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    const CarPlacement c0 = *h.last.setCar;
    const BallPlacement b0 = *h.last.setBall;
    const float sign = h.latest.car.team == 1 ? -1.f : 1.f;
    Script sc(h);
    sc.place(c0);
    ground(sc.s.car);
    sc.s.ball.pos = b0.pos;
    sc.s.ball.vel = b0.vel;
    const Vec3 g{0.f, 0.f, bm::arena::kGravityZ};
    auto ballFly = [&](Snapshot& x) {
        x.ball.vel += g * DT;
        x.ball.pos += x.ball.vel * DT;
    };
    for (int i = 0; i < 48; ++i) sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
    });
    // Takeoff toward a point just under the ball, met 1.7 s after GO.
    const float tMeet = 1.7f - 48.f / kTickRate;
    Vec3 bp = sc.s.ball.pos, bv = sc.s.ball.vel;
    for (int i = 0; i < static_cast<int>(tMeet * kTickRate); ++i) {
        bv += g * DT;
        bp += bv * DT;
    }
    const Vec3 meet = bp - Vec3{0.f, 0.f, 125.f};
    air(sc.s.car);
    sc.s.car.jumped = true;
    sc.setRot(rotDeg(0.f, sign > 0.f ? 90.f : -90.f, 180.f));  // wheels up, toward the ball above
    sc.s.car.vel = (meet - sc.s.car.pos) / tMeet;
    const int n = static_cast<int>(tMeet * kTickRate);
    for (int i = 0; i < n; ++i) {
        if (landEarly && i == 40) {
            // Back on the floor before the reset.
            sc.s.car.pos = {sc.s.car.pos.x, sc.s.car.pos.y, 17.f};
            sc.s.car.vel = {0.f, 500.f * sign, 0.f};
            sc.setRot(rotDeg(0.f, sign > 0.f ? 90.f : -90.f, 0.f));
            ground(sc.s.car);
        }
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            ballFly(x);
        });
        if (landEarly && i >= 40) {
            sc.s.car.vel = {0.f, 0.f, 0.f};
        }
    }
    if (landEarly) {
        for (int i = 0; i < 200; ++i) sc.step([&](Snapshot& x) { ballFly(x); });
        return;
    }
    // Contact: the game reports the reset; the car rides along the ball for 0.3 s, then flips into it.
    sc.s.car.vel = sc.s.ball.vel;
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
        x.car.hasFlip = true;
    }, false, true);
    for (int i = 0; i < 36; ++i) sc.step([&](Snapshot& x) {
        x.car.vel = x.ball.vel;
        x.car.pos += x.car.vel * DT;
        ballFly(x);
    });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
        x.in.jump = true;
        x.in.dodgeForward = 1.f;
        x.in.pitch = -1.f;
        x.car.dodging = true;
        x.car.hasFlip = false;
        x.car.dodgeDir = {1.f, 0.f, 0.f};
    });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
    });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
    }, true);
    sc.s.ball.vel = {0.f, 2400.f * sign, 150.f};
    for (int i = 0; i < 40; ++i) sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        ballFly(x);
    });
}
} // namespace

TEST(bm_play_flipreset_route_steps) {
    Harness h;
    warmUp(h);
    h.command("level", {"flipreset", levelArg(5)});
    h.command("drill", {"flipreset", "parcours"});
    CHECK(h.mod.uiState().drill.full);
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 5u);
    playFlipResetRoute(h, false);
    CHECK_EQ(h.countSkill("ball.flipreset"), 1);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK_MSG(h.results[0].success, h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code);
    const auto u = h.mod.uiState();
    for (int i = 0; i < 5; ++i) CHECK_MSG(stepOk(u, i), u.drill.steps.size() > static_cast<size_t>(i) ? u.drill.steps[static_cast<size_t>(i)] : "");

    // Landing before the reset: DRILL_LANDED, naming the missing step.
    Harness l;
    warmUp(l);
    l.command("level", {"flipreset", levelArg(5)});
    l.command("drill", {"flipreset", "parcours"});
    playFlipResetRoute(l, true);
    CHECK_EQ(l.results.size(), 1u);
    if (!l.results.empty()) {
        CHECK(!l.results[0].success);
        CHECK(!l.results[0].tips.empty() && l.results[0].tips[0].code == "bm.drill_landed");
    }
}

// =============================================================================================================
// Air dribble
// =============================================================================================================
TEST(bm_play_airdribble_wall_ball_rises) {
    Rng rng(5);
    int walls = 0;
    for (int k = 0; k < 200 && walls < 12; ++k) {
        const DrillSetup d = setupAt(Mech::AirDribble, 6 + k % 5, k & 1, rng);
        if (d.side == 0.f) continue;
        ++walls;
        const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 3.f);
        float zmax = 0.f, drift = 0.f, tLow = 99.f;
        for (size_t i = 0; i < ball.size(); ++i) {
            zmax = std::max(zmax, ball[i].p.z);
            if (ball[i].p.z >= 300.f) drift = std::max(drift, std::fabs(std::fabs(ball[i].p.x) - 4003.f));
            if (i > 10 && ball[i].p.z < 300.f && tLow > 90.f) tLow = static_cast<float>(i) * kTickDt;
        }
        CHECK_MSG(drift <= 5.f, std::to_string(drift));
        CHECK(zmax >= 1200.f);
        CHECK(tLow >= 2.5f);
        // Car on the wall below the ball, wheels on the wall, nose up (both teams).
        const Basis B = basisFromRot(d.car.rot);
        CHECK(B.up.x * d.side < -0.99f);
        CHECK(B.fwd.z > 0.9f);
    }
    CHECK(walls >= 6);
}

TEST(bm_play_airdribble_niv1) {
    // Niveau 1: car stopped, the ball balanced on the roof; a pop and one air touch (weak grade) is a success, the
    // score ignored; the same grade fails at niveau 2.
    Rng rng(9);
    const DrillSetup d = setupAt(Mech::AirDribble, 1, 0, rng);
    CHECK(length(d.car.vel) < 1e-3f && length(d.ball.vel) < 1e-3f);
    Snapshot s;
    s.car.valid = s.ball.valid = true;
    s.car.pos = d.car.pos;
    s.car.rot = d.car.rot;
    s.car.basis = basisFromRot(d.car.rot);
    s.ball.pos = d.ball.pos;
    CHECK(carryGeometry(s));
    CHECK(carriedOnRoof(s));
    MechGrade g;
    g.family = Family::AirDribble;
    g.variant = "ground_to_air_bump";
    g.weak = g.drillOnly = true;
    g.score = 0.2f;
    g.metrics = {makeMetric("touches", "", 2.f, ""), makeMetric("air_touches", "", 1.f, ""),
                 makeMetric("height_gain", "", 300.f, "uu"), makeMetric("duration", "", 0.3f, "s")};
    g.errors = {"AD_SHORT"};
    CHECK(judgeDrill(Mech::AirDribble, g, levelOfPlayer(1), drillMinScore(50, levelOfPlayer(1)), 4000.f).success);
    CHECK(!judgeDrill(Mech::AirDribble, g, levelOfPlayer(2), drillMinScore(50, levelOfPlayer(2)), 4000.f).success);
    g.metrics[2].value = 120.f;  // the ball barely left the roof
    const DrillVerdict low = judgeDrill(Mech::AirDribble, g, levelOfPlayer(1), 25, 4000.f);
    CHECK(!low.success && !low.extraErrors.empty() && low.extraErrors[0] == "AD_LOW_POP");
    // In the drill: pop and one touch in the air.
    Harness h;
    warmUp(h);
    h.command("drill", {"airdribble"});
    h.skipReady();
    h.run(continueFrom(h, bmtest::airDribbleScenario(AD::Short)));
    h.idle(60);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.airdribble");
        CHECK(h.results[0].success);
    }
}

// =============================================================================================================
// Flick family and the shared pickup
// =============================================================================================================
TEST(bm_play_flick_family_dribble_start) {
    // Like a match: the ball on the roof of a car already driving toward the attacked goal from midfield, 4500 to
    // 5500 uu of the goal line, at 900 uu/s (niveau 1) up to 1300 (niveau 10), a speed the throttle alone holds.
    for (Mech m : {Mech::Flick, Mech::Musty, Mech::Breezi}) {
        for (int n = 1; n <= 10; ++n) {
            for (int team : {0, 1}) {
                Rng rng(11 + static_cast<uint64_t>(n));
                const DrillSetup d = setupAt(m, n, team, rng);
                const std::string what = std::string(mechInfo(m).id) + " niveau " + std::to_string(n);
                const float v = length(d.car.vel);
                const float sign = team == 1 ? -1.f : 1.f;
                const float dist = bm::arena::kBackWallY - sign * d.car.pos.y;
                CHECK_NEAR(v, flickDribbleSpeed(levelOfPlayer(n)), 1e-2f);
                CHECK_MSG(v >= 899.f && v <= 1301.f && pb::throttleAccel(v) > 100.f, what);
                CHECK_MSG(dist >= 4499.f && dist <= 5501.f, what + " " + std::to_string(dist));
                CHECK_MSG(distance(d.ball.vel, d.car.vel) < 1e-3f, what);
                const Vec3 toGoal = normalize(flat(Vec3{0.f, sign * bm::arena::kBackWallY, 0.f} - d.car.pos));
                CHECK_MSG(dot(normalize(flat(d.car.vel)), toGoal) > std::cos(16.f * kDegToRad), what);
                // Several seconds of dribble before the 12 m line at that speed.
                CHECK_MSG((dist - kFlickLateDist) / v >= (n == 1 ? 3.6f : 2.9f), what);
                Snapshot s;
                s.car.valid = s.ball.valid = true;
                s.car.pos = d.car.pos;
                s.car.rot = d.car.rot;
                s.car.basis = basisFromRot(d.car.rot);
                s.car.vel = d.car.vel;
                s.ball.pos = d.ball.pos;
                s.ball.vel = d.ball.vel;
                CHECK_MSG(carryGeometry(s), what);
                const Vec3 L = s.car.basis.toLocal(d.ball.pos - d.car.pos);
                CHECK_NEAR(L.x, s.car.hitboxOffset.x, 5.f);
                CHECK(d.label == "Balle sur le toit, en dribble");
            }
        }
    }
}

// The shot asked from the dribble start is a real shot and a reachable one: at the bar speed a ball sent toward the
// goal centre scores from the 12 m line and from 25 m (core physics, best elevation), and the ball stays on the roof
// of a car holding its speed until the 12 m line (core physics for the ball, the roof as a plane under it with the
// ball-vs-arena friction of the core physics: car-ball contacts are not in it).
TEST(bm_play_flick_dribble_with_the_core_physics) {
    for (int n = 1; n <= 10; ++n) {
        const float L = levelOfPlayer(n);
        const float bar = flickPowerNeeded(Mech::Flick, L);
        CHECK(bar >= 1499.f && bar <= 2401.f);
        for (float dist : {kFlickLateDist, 2500.f}) {
            const Vec3 start{0.f, bm::arena::kBackWallY - dist, 160.f};
            bool scored = false;
            for (float e = 0.f; e <= 40.f && !scored; e += 2.5f) {
                const auto path = pb::simBall(start, Vec3{0.f, std::cos(e * kDegToRad), std::sin(e * kDegToRad)} * bar, 4.f);
                for (size_t i = 1; i < path.size(); ++i) {
                    if (path[i - 1].p.y < bm::arena::kBackWallY && path[i].p.y >= bm::arena::kBackWallY) {
                        scored = std::fabs(path[i].p.x) < 856.f && path[i].p.z < 615.f;
                        break;
                    }
                }
            }
            CHECK_MSG(scored, std::to_string(n) + " " + std::to_string(dist));
        }
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng rng(seed);
            const DrillSetup d = setupAt(Mech::Flick, n, 0, rng);
            CarState car;
            const Basis B = basisFromRot(d.car.rot);
            const float rest = car.hitboxOffset.z + car.hitboxExtent.z * 0.5f + 91.25f;
            pr::SimBall b;
            b.pos = d.ball.pos;
            b.vel = d.ball.vel;
            Vec3 carPos = d.car.pos;
            float t = 0.f;
            const float until = (bm::arena::kBackWallY - d.car.pos.y - kFlickLateDist) / length(d.car.vel);
            bool kept = true;
            for (; t < until && kept; t += kTickDt) {
                pr::stepBall(b);
                carPos += d.car.vel * kTickDt;
                const Vec3 l = B.toLocal(b.pos - carPos);
                if (l.z < rest && std::fabs(l.x - car.hitboxOffset.x) <= car.hitboxExtent.x * 0.5f &&
                    std::fabs(l.y) <= car.hitboxExtent.y * 0.5f) {
                    b.pos += B.up * (rest - l.z);
                    if (b.vel.z < 0.f) {
                        const float jn = -b.vel.z;
                        b.vel.z = 0.f;
                        const Vec3 rel = flat(b.vel - d.car.vel);
                        const float r = length(rel);
                        if (r > 1e-4f) b.vel -= rel * (std::min(r, 0.35f * jn) / r);
                    }
                }
                Snapshot s;
                s.car.valid = s.ball.valid = true;
                s.car.pos = carPos;
                s.car.rot = d.car.rot;
                s.car.basis = B;
                s.car.vel = d.car.vel;
                s.ball.pos = b.pos;
                s.ball.vel = b.vel;
                kept = carriedOnRoof(s);
            }
            CHECK_MSG(kept && t >= until - 0.02f, std::to_string(n) + ": " + std::to_string(t) + " / " + std::to_string(until));
        }
    }
}

TEST(bm_play_flick_carry_margin) {
    // The ball stays in the carry box at least 0.6 s for a player who reacts after 0.3 s, coasting or at full
    // throttle (the ball on the roof accelerates 5/7 of the car's acceleration relative to it), niveaux 1-4.
    CarState car;
    const float back = -(car.hitboxExtent.x * 0.5f + 50.f), front = car.hitboxExtent.x * 0.5f + 137.f;
    for (Mech m : {Mech::Flick, Mech::Musty, Mech::Breezi}) {
        for (int n = 1; n <= 4; ++n) {
            Rng rng(1);
            const float v0 = length(setupAt(m, n, 0, rng).car.vel);
            for (bool throttle : {false, true}) {
                float x = 0.f, vr = 0.f, vc = v0, t = 0.f;
                while (t < 3.f) {
                    float a = (t < 0.3f || !throttle) ? (vc > 0.f ? -525.f : 0.f) : pb::throttleAccel(vc);
                    vc = std::max(0.f, vc + a * kTickDt);
                    vr += -5.f / 7.f * a * kTickDt;
                    x += vr * kTickDt;
                    t += kTickDt;
                    if (x < back || x > front) break;
                }
                CHECK_MSG(t >= 0.6f, std::string(mechInfo(m).id) + " niveau " + std::to_string(n));
            }
        }
    }
}

TEST(bm_play_musty_and_breezi_levels) {
    MechGrade g;
    g.family = Family::Flick;
    g.variant = "musty";
    g.success = true;
    g.score = 0.5f;
    g.metrics = {makeMetric("ball_dv", "", 800.f, "uu/s"), makeMetric("ball_speed_after", "", 1600.f, "uu/s")};
    CHECK(judgeDrill(Mech::Musty, g, levelOfPlayer(1), drillMinScore(50, levelOfPlayer(1)), 4000.f).success);
    const DrillVerdict hard = judgeDrill(Mech::Musty, g, levelOfPlayer(4), drillMinScore(50, levelOfPlayer(4)), 4000.f);
    CHECK(!hard.success && !hard.extraErrors.empty() && hard.extraErrors[0] == "DRILL_NEED_POWER");
    // Like a shot: toward the goal at niveaux 1-2, on target from niveau 3.
    g.projection.valid = true;
    g.projection.onTarget = false;
    g.projection.angleErrDeg = 40.f;
    const DrillVerdict wide = judgeDrill(Mech::Musty, g, levelOfPlayer(1), drillMinScore(50, levelOfPlayer(1)), 4000.f);
    CHECK(!wide.success && !wide.extraErrors.empty() && wide.extraErrors[0] == "DRILL_NEED_DIRECTION");
    g.projection.angleErrDeg = 10.f;  // toward the goal, over the bar
    CHECK(judgeDrill(Mech::Musty, g, levelOfPlayer(2), drillMinScore(50, levelOfPlayer(2)), 4000.f).success);
    g.metrics[1] = makeMetric("ball_speed_after", "", 2400.f, "uu/s");
    const DrillVerdict off = judgeDrill(Mech::Musty, g, levelOfPlayer(3), drillMinScore(50, levelOfPlayer(3)), 4000.f);
    CHECK(!off.success && !off.extraErrors.empty() && off.extraErrors[0] == "DRILL_NEED_TARGET");
    CHECK(tipTemplate("DRILL_NEED_DIRECTION") != nullptr && tipTemplate("FL_TOO_CLOSE") != nullptr);
    // Breezi: tornado alone at niveaux 1-2, then 1500 at niveau 3 up to 2400.
    CHECK_NEAR(flickPowerNeeded(Mech::Breezi, levelOfPlayer(3)), 1500.f, 1e-3f);
    CHECK_NEAR(flickPowerNeeded(Mech::Breezi, 1.f), 2400.f, 1e-3f);
    CHECK_NEAR(flickPowerNeeded(Mech::Flick, 0.f), 1500.f, 1e-3f);
    Rng rng(4);
    for (int n = 1; n <= 10; ++n) {
        const float v = length(setupAt(Mech::Breezi, n, 0, rng).car.vel);
        CHECK(v >= 899.f && v <= 1301.f);  // never supersonic
    }
}

namespace {
// Breezi short start: ball on the roof of a stopped car, a held jump, a spin of 260 deg in 0.7 s with the air roll
// held and the ball kept close, no flip, landing.
void playTornado(Harness& h) {
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    sc.s.ball.pos = h.last.setBall->pos;
    sc.s.ball.vel = sc.s.car.vel;  // dribble start: the ball rides with the car
    const Vec3 offset = sc.s.ball.pos - sc.s.car.pos;
    for (int i = 0; i < 48; ++i)  // reaction, the speed held, ball resting on the roof
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + offset;
            x.ball.vel = x.car.vel;
        });
    const float spin = 260.f * kDegToRad / 0.7f;
    const int airTicks = static_cast<int>(0.7f * kTickRate);
    for (int i = 0; i < airTicks; ++i) {
        sc.step([&](Snapshot& x) {
            air(x.car);
            x.car.jumped = true;
            x.in.jump = i < 30;
            x.in.roll = 1.f;
            x.car.vel = {0.f, 0.f, i < airTicks / 2 ? 300.f : -300.f};
            x.car.pos += x.car.vel * DT;
            x.car.angVel = x.car.basis.fwd * spin;
            x.ball.pos = x.car.pos + offset;
            x.ball.vel = x.car.vel;
        });
    }
    for (int i = 0; i < 90; ++i) {
        sc.step([&](Snapshot& x) {
            ground(x.car);
            x.car.pos.z = bm::arena::kCarRestZ;
            x.car.vel = {};
            x.car.angVel = {};
            x.ball.pos = x.car.pos + offset;
            x.ball.vel = {};
        });
    }
}
} // namespace

TEST(bm_play_breezi_tornado_step) {
    Harness h;
    warmUp(h);
    h.command("drill", {"breezi"});
    playTornado(h);
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.breezi");
        CHECK(h.results[0].success);
        CHECK(h.results[0].title == "Tornade (breezi)");
    }
    // Niveau 3: the same jump without the flip is a failure with a tip, never a silent timeout.
    Harness x;
    warmUp(x);
    x.command("level", {"breezi", levelArg(3)});
    x.command("drill", {"breezi"});
    playTornado(x);
    CHECK_EQ(x.results.size(), 1u);
    if (!x.results.empty()) {
        CHECK(!x.results[0].success);
        CHECK(hasTip(x.results[0], "bm.fl_no_contact") || hasTip(x.results[0], "bm.fl_breezi_no_spin"));
    }
}

namespace {
// Scripted pickup: 0.4 s reaction (the car keeps its start speed), then the car drives 250 uu/s faster than the ball
// along its nose (at least 300 uu/s: resting ball, ball rolling toward it, or the pickup at speed of the flicks); the
// ball climbs the hood when the bumper reaches it (put on the roof by script: the climb itself is not in the core
// physics) and rides there, the car keeping its speed (stopped for a resting or incoming ball), until stable.
// Returns false when the drill never got there.
bool doPickup(Script& sc, const BallPlacement& ball0) {
    sc.s.ball.pos = ball0.pos;
    sc.s.ball.vel = ball0.vel;
    for (int i = 0; i < 48; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    const Vec3 fwd = flat(sc.s.car.basis.fwd);
    const float ballAlong = dot(sc.s.ball.vel, fwd);
    sc.s.car.vel = fwd * std::max(300.f, ballAlong + 250.f);
    for (int i = 0; i < 600; ++i) {
        const float gap = dot(sc.s.ball.pos - sc.s.car.pos, fwd);
        if (gap < 91.25f + 73.f) break;
        sc.step([&](Snapshot& x) {
            x.in.throttle = 0.4f;
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    }
    const Vec3 roof = sc.s.car.basis.up * 132.f + sc.s.car.basis.fwd * sc.s.car.hitboxOffset.x;
    if (ballAlong < 1.f) sc.s.car.vel = {};
    for (int i = 0; i < 120; ++i) {
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + roof;
            x.ball.vel = x.car.vel;
        });
    }
    return true;
}
} // namespace

TEST(bm_play_flick_pickup_route) {
    // Flick "parcours complet": pick the resting ball up, wait until it is stable, jump, flick. Success, and the
    // pickup's own staircase records the pickup.
    Harness h;
    warmUp(h);
    const float pickup0 = h.mod.pickupLevel();
    h.command("drill", {"flick", "parcours"});
    CHECK(h.mod.uiState().drill.full);
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 4u);
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    // Niveau 1 of the pickup at speed: the car drives toward the goal, the ball rolls ahead of it, slower.
    CHECK(distance(h.last.setBall->pos, h.last.setCar->pos) > 450.f);
    CHECK(length(h.last.setCar->vel) >= 699.f && length(h.last.setCar->vel) <= 921.f);
    CHECK(dot(h.last.setBall->vel, h.last.setCar->vel) > 0.f && length(h.last.setBall->vel) < length(h.last.setCar->vel));
    CHECK(h.mod.uiState().drill.steps[0].find(pickup::kRoofLabelChase) != std::string::npos);
    doPickup(sc, *h.last.setBall);
    auto u = h.mod.uiState();
    CHECK(stepOk(u, 0) && stepOk(u, 1));
    CHECK(h.mod.pickupLevel() > pickup0);
    // Jump with the ball, flip forward into it.
    const Vec3 roof = sc.s.ball.pos - sc.s.car.pos;
    for (int i = 0; i < 12; ++i)
        sc.step([&](Snapshot& x) {
            air(x.car);
            x.car.jumped = true;
            x.in.jump = i < 6;
            x.car.vel = {0.f, 0.f, 300.f};
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + roof;
            x.ball.vel = x.car.vel;
        });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        x.ball.pos = x.car.pos + roof;
        x.in.jump = true;
        x.in.dodgeForward = 1.f;
        x.in.pitch = -1.f;
        x.car.dodging = true;
        x.car.hasFlip = false;
        x.car.dodgeDir = {1.f, 0.f, 0.f};
    });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        x.ball.pos = x.car.pos + roof;
    }, true);
    const float sign = h.latest.car.team == 1 ? -1.f : 1.f;
    sc.s.ball.vel = {0.f, 1500.f * sign, 600.f};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.flick");
        CHECK_MSG(h.results[0].success, h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code);
    }
    u = h.mod.uiState();
    for (int i = 0; i < 4; ++i) CHECK(stepOk(u, i));
}

TEST(bm_play_musty_pickup_route) {
    // Musty "parcours complet": pickup, jump, nose down (the gauge turns green), backflip into the ball.
    Harness h;
    warmUp(h);
    h.command("drill", {"musty", "parcours"});
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 5u);
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    doPickup(sc, *h.last.setBall);
    const Vec3 roof = sc.s.ball.pos - sc.s.car.pos;
    const float yaw = std::atan2(sc.s.car.basis.fwd.y, sc.s.car.basis.fwd.x) * kRadToDeg;
    bool gaugeGreen = false;
    for (int i = 0; i < 40; ++i) {
        sc.step([&](Snapshot& x) {
            air(x.car);
            x.car.jumped = true;
            x.in.jump = i < 5;
            x.in.pitch = -1.f;  // stick forward: nose down
            x.car.rot = rotDeg(-60.f * std::min(1.f, static_cast<float>(i) / 25.f), yaw, 0.f);
            x.car.basis = basisFromRot(x.car.rot);
            x.car.vel = {0.f, 0.f, 200.f};
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + roof;
            x.ball.vel = x.car.vel;
        });
        for (const auto& p : drawHud(h, true).panels)
            for (const auto& l : p.lines) gaugeGreen = gaugeGreen || (l.text == "Nez vers le bas" && l.color.g > 200);
    }
    CHECK(gaugeGreen);
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        x.ball.pos = x.car.pos + roof;
        x.in.jump = true;
        x.in.dodgeForward = -1.f;
        x.in.pitch = 1.f;
        x.car.dodging = true;
        x.car.hasFlip = false;
        x.car.dodgeDir = {-1.f, 0.f, 0.f};
    });
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        x.ball.pos = x.car.pos + roof;
    }, true);
    const float sign = h.latest.car.team == 1 ? -1.f : 1.f;
    sc.s.ball.vel = {0.f, 1400.f * sign, 900.f};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.musty");
        CHECK_MSG(h.results[0].success, h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code);
    }
    const auto u = h.mod.uiState();
    for (int i = 0; i < 5; ++i) CHECK(stepOk(u, i));
}

TEST(bm_play_pickup_failure_feeds_its_own_staircase) {
    // The player presses the throttle but never gets the ball: R1 fails after 8 s with its tip. Not an attempt of the
    // flick: no result, the flick level does not move, only the pickup's staircase records it.
    Harness h;
    warmUp(h);
    h.command("level", {"flick", "40"});
    h.command("drill", {"flick", "parcours"});
    h.skipReady();
    const float flick0 = h.mod.drillLevel("flick");
    for (int i = 0; i < static_cast<int>(8.5f * kTickRate); ++i) h.throttleTick(0.3f);
    CHECK(h.results.empty());
    CHECK_NEAR(h.mod.drillLevel("flick"), flick0, 1e-6f);
    const auto u = h.mod.uiState();
    CHECK(u.drill.lastVerdict == "Ramassage raté");
    CHECK(u.drill.tip == pickup::kRoofTipChase);  // the flick family picks the ball up at speed
    CHECK(u.drill.missing.find(pickup::kRoofLabelChase) != std::string::npos);
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("ballmech.pickup.attempts", 0), 1);
    CHECK_EQ(st.getInt("ballmech.drill.flick.attempts", 0), 0);
}

TEST(bm_play_pickup_jump_before_stable_fails_pickup) {
    Harness h;
    warmUp(h);
    h.command("drill", {"musty", "parcours"});
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    sc.s.ball.pos = h.last.setBall->pos;
    for (int i = 0; i < 48; ++i) sc.step();
    const Vec3 fwd = flat(sc.s.car.basis.fwd);
    sc.s.car.vel = fwd * 300.f;
    for (int i = 0; i < 600 && dot(sc.s.ball.pos - sc.s.car.pos, fwd) > 164.f; ++i)
        sc.step([&](Snapshot& x) { x.car.pos += x.car.vel * DT; });
    // The ball reaches the roof sliding back (not stable), the player jumps at once.
    const Vec3 roof = sc.s.car.basis.up * 132.f + fwd * 50.f;
    for (int i = 0; i < 64; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + roof;
            x.ball.vel = x.car.vel - fwd * 200.f;
        });
    sc.step([&](Snapshot& x) {
        x.in.jump = true;
        x.car.jumped = true;
    });
    CHECK(h.results.empty());
    const auto u = h.mod.uiState();
    CHECK(u.drill.lastVerdict == "Ramassage raté");
    CHECK(u.drill.tip == pickup::kJumpTip);
}

TEST(bm_play_airdribble_pickup_route) {
    // Air dribble "parcours complet", niveau 1: pick the ball up, pop it, touch it once in the air. Success.
    Harness h;
    warmUp(h);
    h.command("drill", {"airdribble", "parcours"});
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 4u);  // pickup x2, pop, first air touch (niveau 1)
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    doPickup(sc, *h.last.setBall);
    const Vec3 g{0.f, 0.f, bm::arena::kGravityZ};
    // Pop: the jump tick still touches the ball (ground pop), then car and ball rise, the ball faster.
    sc.step([&](Snapshot& x) { x.in.jump = true; }, true);
    sc.s.ball.vel = {0.f, 0.f, 950.f};
    sc.s.car.vel = {0.f, 0.f, 500.f};
    for (int i = 0; i < 60; ++i)
        sc.step([&](Snapshot& x) {
            air(x.car);
            x.car.jumped = true;
            x.in.jump = i < 10;
            x.car.vel += g * DT * 0.3f;
            x.car.pos += x.car.vel * DT;
            x.ball.vel += g * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK(stepOk(h.mod.uiState(), 2));
    // First touch in the air (the car is under the ball), then the ball falls back.
    sc.step([&](Snapshot& x) {
        x.car.pos += x.car.vel * DT;
        x.ball.pos += x.ball.vel * DT;
    }, true);
    sc.s.ball.vel = {0.f, 200.f, 350.f};
    for (int i = 0; i < 300; ++i)
        sc.step([&](Snapshot& x) {
            x.car.vel += g * DT;
            x.car.pos += x.car.vel * DT;
            if (x.car.pos.z < 17.f) {
                x.car.pos.z = 17.f;
                x.car.vel = {};
                ground(x.car);
            }
            x.ball.vel += g * DT;
            x.ball.pos += x.ball.vel * DT;
            if (x.ball.pos.z < 93.f) {
                x.ball.pos.z = 93.f;
                x.ball.vel.z = 0.f;
            }
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.airdribble");
        CHECK_MSG(h.results[0].success, h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code);
    }
    const auto u = h.mod.uiState();
    for (int i = 0; i < 4; ++i) CHECK(stepOk(u, i));
}

TEST(bm_play_breezi_pickup_tornado) {
    // Breezi "parcours complet", niveau 1: pickup, held jump, tornado: success at the spin, no flip needed.
    Harness h;
    warmUp(h);
    h.command("drill", {"breezi", "parcours"});
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 4u);
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    doPickup(sc, *h.last.setBall);
    const Vec3 offset = sc.s.ball.pos - sc.s.car.pos;
    const float spin = 300.f * kDegToRad / 0.7f;
    for (int i = 0; i < 90 && h.results.empty(); ++i)
        sc.step([&](Snapshot& x) {
            air(x.car);
            x.car.jumped = true;
            x.in.jump = i < 30;
            x.in.roll = 1.f;
            x.car.vel = {0.f, 0.f, 250.f};
            x.car.pos += x.car.vel * DT;
            x.car.angVel = x.car.basis.fwd * spin;
            x.ball.pos = x.car.pos + offset;
            x.ball.vel = x.car.vel;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.breezi");
        CHECK(h.results[0].success);
    }
}

// =============================================================================================================
// Double tap, pinch, power shot, air roll, aerial
// =============================================================================================================
TEST(bm_play_doubletap_read_playable) {
    for (int n = 1; n <= 6; ++n) {
        Rng rng(400 + static_cast<uint64_t>(n));
        for (int k = 0; k < 40; ++k) {
            const DrillSetup d = setupAt(Mech::DoubleTap, n, k & 1, rng);
            CHECK(d.doubleTapRead);
            const float sign = (k & 1) ? -1.f : 1.f;
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 6.f);
            int ib = -1;
            for (size_t i = 1; i < ball.size(); ++i)
                if (ball[i - 1].v.y * sign > 0.f && ball[i].v.y * sign <= 0.f && ball[i].p.y * sign > 4800.f) {
                    ib = static_cast<int>(i);
                    break;
                }
            CHECK(ib > 0);
            if (ib <= 0) continue;
            const std::string what = "niveau " + std::to_string(n);
            CHECK_MSG(static_cast<float>(ib) * kTickDt < 2.2f && ball[static_cast<size_t>(ib)].p.z >= 900.f, what);
            int il = ib;
            while (il < static_cast<int>(ball.size()) - 1 && ball[static_cast<size_t>(il)].p.z > 300.f) ++il;
            CHECK_MSG(static_cast<float>(il - ib) * kTickDt >= 1.2f, what);
            if (k >= 8) continue;  // the reach model is heavy: a few seeds
            std::vector<pb::BallSample> after = ball;
            for (int i = 0; i < ib; ++i) after[static_cast<size_t>(i)].p.z = -1e4f;
            const pb::Window w = pb::groundWindow(d.car.pos, d.car.vel, after, pb::kReact, 150.f, 250.f, false);
            CHECK_MSG(w.longest >= 0.8f, what);
            if (n <= 3) {
                // Best case (full boost from GO): the car is near the backboard when the ball bounces.
                float x = 0.f, v = length(d.car.vel);
                for (int i = 0; i < ib; ++i) {
                    v = std::min(2300.f, v + (pb::throttleAccel(v) + pb::kBoostAccel) * kTickDt);
                    x += v * kTickDt;
                }
                Vec3 carAt = d.car.pos + Vec3{0.f, x * sign, 0.f};
                carAt.y = sign * std::min(carAt.y * sign, 5070.f);
                CHECK_MSG(distance(flat(carAt), flat(ball[static_cast<size_t>(ib)].p)) <= 1200.f, what);
            }
        }
    }
    // Score: an off-target rebound touch is a success at niveau 1, a target failure at niveau 3.
    MechGrade g;
    g.family = Family::DoubleTap;
    g.variant = "read";
    g.hasShot = true;
    g.success = false;
    g.score = 0.45f;
    g.projection.valid = true;
    g.projection.onTarget = false;
    g.metrics = {makeMetric("car_dist_at_bounce", "", 900.f, "uu"), makeMetric("ball_speed_after", "", 1600.f, "uu/s")};
    CHECK(judgeDrill(Mech::DoubleTap, g, levelOfPlayer(1), drillMinScore(50, levelOfPlayer(1)), 4000.f).success);
    const DrillVerdict v3 = judgeDrill(Mech::DoubleTap, g, levelOfPlayer(3), drillMinScore(50, levelOfPlayer(3)), 4000.f);
    CHECK(!v3.success && std::find(v3.extraErrors.begin(), v3.extraErrors.end(), "DRILL_NEED_TARGET") != v3.extraErrors.end());
}

TEST(bm_play_pinch_reachable_and_threshold) {
    for (int n = 1; n <= 10; ++n) {
        Rng rng(500 + static_cast<uint64_t>(n));
        for (int k = 0; k < 40; ++k) {
            const DrillSetup d = setupAt(Mech::Pinch, n, k & 1, rng);
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 4.f, d.ball.angVel);
            std::vector<pb::BallSample> band = ball;
            float inBand = 0.f;
            for (auto& b : band) {
                const float gap = bm::arena::kSideWallX - std::fabs(b.p.x) - 91.25f;
                if (gap <= 120.f && gap < b.p.z - 91.25f) inBand += kTickDt;
                else b.p.z = -1e4f;
            }
            const std::string what = "niveau " + std::to_string(n);
            CHECK_MSG(inBand >= 0.8f, what);
            if (k >= 8) continue;
            bool reach = false;
            float sp = 0.f;
            for (size_t i = 12; i < band.size() && !reach; i += 2) {
                if (band[i].p.z < 0.f) continue;
                const Vec3 q = band[i].p;
                reach = pb::groundAerial(d.car.pos, d.car.vel, q.z < 250.f ? Vec3{q.x, q.y, 93.f} : q,
                                         static_cast<float>(i) * kTickDt, pb::kReact, 130.f, q.z < 250.f, &sp);
            }
            CHECK_MSG(reach && sp >= 1900.f, what);
        }
    }
    // The drill bar is under the free play threshold: a real pinch at 3200 with g.success false (4000) passes at
    // niveau 1, fails at niveau 5 (3667) on power; a touch far from the wall fails.
    MechGrade g;
    g.family = Family::Pinch;
    g.variant = "wall_pinch";
    g.success = false;
    g.score = 0.1f;
    g.metrics = {makeMetric("exit_speed", "", 3200.f, "uu/s"), makeMetric("surface_gap", "", 20.f, "uu"),
                 makeMetric("push_speed", "", 900.f, "uu/s")};
    CHECK(judgeDrill(Mech::Pinch, g, levelOfPlayer(1), drillMinScore(50, 0.f), 4000.f).success);
    const DrillVerdict v5 = judgeDrill(Mech::Pinch, g, levelOfPlayer(5), drillMinScore(50, levelOfPlayer(5)), 4000.f);
    CHECK(!v5.success && !v5.extraErrors.empty() && v5.extraErrors[0] == "DRILL_NEED_POWER");
    g.metrics[1].value = 300.f;
    CHECK(!judgeDrill(Mech::Pinch, g, levelOfPlayer(1), 25, 4000.f).success);
    CHECK_NEAR(pinchSpeedNeeded(0.f), 3000.f, 1e-3f);
    CHECK_NEAR(pinchSpeedNeeded(1.f), 4500.f, 1e-3f);
}

TEST(bm_play_powershot_reach) {
    for (int n = 1; n <= 10; ++n) {
        Rng rng(600 + static_cast<uint64_t>(n));
        for (int k = 0; k < 12; ++k) {
            const DrillSetup d = setupAt(Mech::PowerShot, n, k & 1, rng);
            CHECK(d.car.boost.value_or(0.f) >= 60.f);
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 5.f, d.ball.angVel);
            bool ok = false;
            float sp = 0.f, t = 0.f;
            for (size_t i = 12; i < ball.size() && !ok; i += 2) {
                t = static_cast<float>(i) * kTickDt;
                ok = pb::groundAerial(d.car.pos, d.car.vel, ball[i].p, t, pb::kReact, 130.f, true, &sp);
            }
            const std::string what = "niveau " + std::to_string(n);
            CHECK_MSG(ok && t < 2.5f && sp >= 1700.f, what);
        }
    }
    Rng rng(2);
    const DrillSetup d = setupAt(Mech::PowerShot, 1, 0, rng);
    CHECK(length(d.ball.vel) < 1e-3f && length(d.car.vel) < 1e-3f);
    CHECK(std::fabs(d.ball.pos.x) < 1.f && std::fabs(d.car.pos.x) < 1.f);  // on the line to the goal centre
}

TEST(bm_play_airroll_window_and_niv1) {
    for (int n = 1; n <= 10; ++n) {
        Rng rng(700 + static_cast<uint64_t>(n));
        for (int k = 0; k < 40; ++k) {
            const DrillSetup d = setupAt(Mech::AirRoll, n, k & 1, rng);
            auto ball = pb::simBall(d.ball.pos, d.ball.vel, 4.f);
            size_t fall = 11;
            while (fall < ball.size() - 1 && ball[fall].p.z >= 300.f) ++fall;
            ball.resize(fall);
            const pb::Window w = pb::airWindow(d.car.pos, d.car.vel, ball, 0.45f, 140.f, 300.f);
            CHECK_MSG(w.longest >= 0.5f, "niveau " + std::to_string(n));
        }
    }
    MechGrade g;
    g.family = Family::Aerial;
    g.variant = "air_roll_shot";
    g.success = false;
    g.score = 0.f;
    g.projection.valid = true;
    g.projection.onTarget = false;
    g.metrics = {makeMetric("ball_speed_after", "", 1100.f, "uu/s")};
    CHECK(judgeDrill(Mech::AirRoll, g, levelOfPlayer(1), drillMinScore(50, 0.f), 4000.f).success);
    CHECK(!judgeDrill(Mech::AirRoll, g, levelOfPlayer(3), drillMinScore(50, levelOfPlayer(3)), 4000.f).success);
}

TEST(bm_play_aerial_reachable) {
    for (int n = 1; n <= 10; ++n) {
        Rng rng(800 + static_cast<uint64_t>(n));
        for (int k = 0; k < 40; ++k) {
            const DrillSetup d = setupAt(Mech::Aerial, n, k & 1, rng);
            const std::string what = "niveau " + std::to_string(n);
            CHECK_MSG(std::fabs(d.ball.vel.x) <= (n <= 3 ? 200.f : 1750.f), what);
            if (n > 6 || k >= 8) continue;
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 5.f);
            const pb::Window w = pb::groundWindow(d.car.pos, d.car.vel, ball, 0.3f, 140.f, 300.f, false);
            CHECK_MSG(w.first >= 0.f && w.first <= 2.21f && w.longest >= 1.f, what);
        }
    }
}

// ---- Full sequences of the other drills, scripted players ---------------------------------------------------------
namespace {
void dodgeNow(Snapshot& x) {
    x.in.jump = true;
    x.in.dodgeForward = 1.f;
    x.in.pitch = -1.f;
    x.car.dodging = true;
    x.car.hasFlip = false;
    x.car.dodgeDir = {1.f, 0.f, 0.f};
}
void faceDir(Script& sc, const Vec3& dir, float pitchDeg = 0.f, float rollDeg = 0.f) {
    sc.setRot(rotDeg(pitchDeg, std::atan2(dir.y, dir.x) * kRadToDeg, rollDeg));
}
std::string firstTip(const Harness& h) {
    return h.results.empty() || h.results[0].tips.empty() ? std::string() : h.results[0].tips[0].code;
}
} // namespace

TEST(bm_play_powershot_full_route) {
    // Line up behind the ball (it rests off the car's line), speed up, flip just before it.
    Harness h;
    warmUp(h);
    h.command("drill", {"powershot", "parcours"});
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    const Vec3 ball = h.last.setBall->pos;
    sc.s.ball.pos = ball;
    const Vec3 u = normalize(flat(Vec3{0.f, bm::arena::kBackWallY, 0.f} - ball));
    for (int i = 0; i < 48; ++i) sc.step();
    // Drive to a point 1100 uu behind the ball on its line to the goal.
    const Vec3 a = ball - u * 1100.f;
    const Vec3 toA = flat(a - sc.s.car.pos);
    faceDir(sc, toA);
    sc.s.car.vel = normalize(toA) * 1400.f;
    for (int i = 0; i < 600 && length2D(a - sc.s.car.pos) > 15.f; ++i)
        sc.step([&](Snapshot& x) {
            x.in.throttle = 1.f;
            x.car.pos += x.car.vel * DT;
        });
    // Face the ball and accelerate, flip at about two car lengths, touch.
    faceDir(sc, u);
    float v = 1400.f;
    bool dodged = false;
    for (int i = 0; i < 300 && length2D(ball - sc.s.car.pos) > 160.f; ++i) {
        const bool dodge = !dodged && length2D(ball - sc.s.car.pos) < 330.f;
        dodged = dodged || dodge;
        sc.step([&](Snapshot& x) {
            v = std::min(2100.f, v + 1500.f * DT);
            x.car.vel = u * v;
            x.car.pos += x.car.vel * DT;
            x.in.boost = true;
            if (dodge) dodgeNow(x);
        });
    }
    sc.step([&](Snapshot& x) { x.car.pos += x.car.vel * DT; }, true);
    sc.s.ball.vel = u * 3200.f;
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.powershot");
        CHECK_MSG(h.results[0].success, firstTip(h));
    }
    const auto st = h.mod.uiState();
    for (int i = 0; i < 4; ++i) CHECK(stepOk(st, i));
}

TEST(bm_play_airroll_full_route) {
    // From the ground: jump toward the popped ball, hold the air roll one way, hit its centre toward the goal.
    Harness h;
    warmUp(h);
    h.command("drill", {"airroll", "parcours"});
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    const auto ball = pb::simBall(h.last.setBall->pos, h.last.setBall->vel, 4.f);
    size_t k = 0;
    auto ballAt = [&](Snapshot& x) {
        k = std::min(k + 1, ball.size() - 1);
        x.ball.pos = ball[k].p;
        x.ball.vel = ball[k].v;
    };
    sc.s.ball.pos = ball[0].p;
    for (int i = 0; i < 48; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            ballAt(x);
        });
    // Fly straight to just behind the ball, met 1.7 s after GO, air roll held.
    const size_t meet = static_cast<size_t>(1.7f * kTickRate);
    const Vec3 dir = normalize(ball[meet].p - sc.s.car.pos);
    const Vec3 target = ball[meet].p - dir * 150.f;
    const float tFly = static_cast<float>(meet - k) * kTickDt;
    air(sc.s.car);
    sc.s.car.jumped = true;
    faceDir(sc, dir, std::asin(dir.z) * kRadToDeg);
    sc.s.car.vel = (target - sc.s.car.pos) / tFly;
    const int n = static_cast<int>(meet - k);
    for (int i = 0; i < n; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.in.boost = true;
            x.in.roll = i > 6 ? 1.f : 0.f;
            ballAt(x);
        }, i == n - 1);
    sc.s.ball.vel = {0.f, 2000.f, -200.f};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.airroll");
        CHECK_MSG(h.results[0].success, firstTip(h));
    }
    const auto st = h.mod.uiState();
    for (int i = 0; i < 4; ++i) CHECK(stepOk(st, i));
}

TEST(bm_play_aerial_full_route) {
    // A centre from the side: take off in the ball-goal axis, nose up with boost, meet it, hit it on target.
    Harness h;
    warmUp(h);
    h.command("drill", {"aerial", "parcours"});
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    const auto ball = pb::simBall(h.last.setBall->pos, h.last.setBall->vel, 4.f);
    size_t k = 0;
    auto ballAt = [&](Snapshot& x) {
        k = std::min(k + 1, ball.size() - 1);
        x.ball.pos = ball[k].p;
        x.ball.vel = ball[k].v;
    };
    sc.s.ball.pos = ball[0].p;
    for (int i = 0; i < 120; ++i) sc.step(ballAt);  // waits on the ground
    const size_t meet = static_cast<size_t>(2.3f * kTickRate);
    const Vec3 dir = normalize(ball[meet].p - sc.s.car.pos);
    const Vec3 target = ball[meet].p - dir * 150.f;
    const float tFly = static_cast<float>(meet - k) * kTickDt;
    air(sc.s.car);
    sc.s.car.jumped = true;
    faceDir(sc, dir, std::max(35.f, std::asin(dir.z) * kRadToDeg));
    sc.s.car.vel = (target - sc.s.car.pos) / tFly;
    const int n = static_cast<int>(meet - k);
    for (int i = 0; i < n; ++i)
        sc.step([&](Snapshot& x) {
            x.in.jump = i < 3;
            x.in.boost = true;
            x.car.pos += x.car.vel * DT;
            ballAt(x);
        }, i == n - 1);
    // Aimed at the goal centre, under the bar.
    const Vec3 from = sc.s.ball.pos;
    const float t = 0.8f;
    const Vec3 goal{0.f, bm::arena::kBackWallY, 300.f};
    sc.s.ball.vel = {(goal.x - from.x) / t, (goal.y - from.y) / t, (goal.z - from.z + 325.f * t * t) / t};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.vel.z += bm::arena::kGravityZ * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.aerial");
        CHECK_MSG(h.results[0].success, firstTip(h));
    }
    const auto st = h.mod.uiState();
    CHECK(stepOk(st, 0) && stepOk(st, 1) && stepOk(st, 3));
}

TEST(bm_play_pinch_full_route) {
    // The ball rolls toward the side wall: wait, then go fast to the point where it meets the wall, hit it there.
    Harness h;
    warmUp(h);
    h.command("drill", {"pinch", "parcours"});
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    const auto ball = pb::simBall(h.last.setBall->pos, h.last.setBall->vel, 5.f, h.last.setBall->angVel);
    const float side = h.last.setBall->pos.x > 0.f ? 1.f : -1.f;
    size_t k = 0;
    auto ballAt = [&](Snapshot& x) {
        k = std::min(k + 1, ball.size() - 1);
        x.ball.pos = ball[k].p;
        x.ball.vel = ball[k].v;
    };
    sc.s.ball.pos = ball[0].p;
    // Contact when the ball is against the wall (gap under 25 uu).
    size_t hit = 0;
    while (hit < ball.size() - 1 && bm::arena::kSideWallX - std::fabs(ball[hit].p.x) - 91.25f > 20.f) ++hit;
    const Vec3 contact = ball[hit].p;
    // Go at 2000 uu/s on a straight line ending 150 uu before the contact point, timed to arrive with it.
    const Vec3 dir = normalize(flat(contact - sc.s.car.pos));
    const float dist = length2D(contact - sc.s.car.pos) - 150.f;
    const float travel = dist / 2000.f;
    const size_t start = hit - static_cast<size_t>(travel * kTickRate);
    while (k < start) sc.step(ballAt);
    faceDir(sc, dir);
    sc.s.car.vel = dir * 2000.f;
    while (k < hit)
        sc.step([&](Snapshot& x) {
            x.in.boost = true;
            x.car.pos += x.car.vel * DT;
            ballAt(x);
        });
    sc.step([&](Snapshot& x) { x.car.pos += x.car.vel * DT; }, true);
    sc.s.ball.vel = {-side * 700.f, 3500.f * (h.latest.car.team == 1 ? -1.f : 1.f), 400.f};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT * 0.2f;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.pinch");
        CHECK_MSG(h.results[0].success, firstTip(h));
    }
    const auto st = h.mod.uiState();
    CHECK(stepOk(st, 0) && stepOk(st, 2) && stepOk(st, 3));
}

TEST(bm_play_doubletap_full_route) {
    // First shot high on the backboard, follow it in the air, take the rebound toward the goal.
    Harness h;
    warmUp(h);
    h.command("drill", {"doubletap", "parcours"});
    CHECK_EQ(h.mod.uiState().drill.steps.size(), 3u);  // niveau 1: the target step comes at niveau 3
    h.skipReady();
    if (!h.last.setCar || !h.last.setBall) return;
    Script sc(h);
    sc.place(*h.last.setCar);
    ground(sc.s.car);
    sc.s.ball.pos = h.last.setBall->pos;
    sc.s.ball.vel = h.last.setBall->vel;
    const Vec3 g{0.f, 0.f, bm::arena::kGravityZ};
    bool bounced = false;
    auto ballFly = [&](Snapshot& x) {
        x.ball.vel += g * DT;
        x.ball.pos += x.ball.vel * DT;
        if (!bounced && x.ball.pos.y >= bm::arena::kBackWallY - 91.25f) {
            x.ball.vel.y = -0.6f * x.ball.vel.y;
            bounced = true;
        }
    };
    for (int i = 0; i < 48; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            ballFly(x);
        });
    // Meet the ball 1.2 s after GO, hit it high toward the backboard.
    Vec3 bp = sc.s.ball.pos, bv = sc.s.ball.vel;
    const int toMeet = static_cast<int>((1.2f - 0.4f) * kTickRate);
    for (int i = 0; i < toMeet; ++i) {
        bv += g * DT;
        bp += bv * DT;
    }
    const Vec3 dir = normalize(bp - sc.s.car.pos);
    air(sc.s.car);
    sc.s.car.jumped = true;
    faceDir(sc, dir, std::asin(dir.z) * kRadToDeg);
    sc.s.car.vel = (bp - dir * 150.f - sc.s.car.pos) / (static_cast<float>(toMeet) * kTickDt);
    for (int i = 0; i < toMeet; ++i)
        sc.step([&](Snapshot& x) {
            x.in.boost = true;
            x.car.pos += x.car.vel * DT;
            ballFly(x);
        }, i == toMeet - 1);
    sc.s.ball.vel = {0.f, 2000.f, 500.f};
    sc.s.car.vel = {0.f, 1500.f, 250.f};
    // Follow until the rebound comes back, then the second touch toward the goal, under the bar.
    for (int i = 0; i < 300 && !(bounced && distance(sc.s.car.pos, sc.s.ball.pos) < 170.f); ++i)
        sc.step([&](Snapshot& x) {
            x.in.boost = true;
            x.car.pos += x.car.vel * DT;
            ballFly(x);
        });
    CHECK(bounced);
    sc.step([&](Snapshot& x) { x.car.pos += x.car.vel * DT; }, true);
    const Vec3 from = sc.s.ball.pos;
    const float t = 0.25f;
    sc.s.ball.vel = {-from.x / t, (bm::arena::kBackWallY + 50.f - from.y) / t, (300.f - from.z + 325.f * t * t) / t};
    for (int i = 0; i < 30; ++i)
        sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT * 0.3f;
            x.ball.vel += g * DT;
            x.ball.pos += x.ball.vel * DT;
        });
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "ball.doubletap");
        CHECK_MSG(h.results[0].success, firstTip(h));
    }
    const auto st = h.mod.uiState();
    for (int i = 0; i < 3; ++i) CHECK(stepOk(st, i));
}

// =============================================================================================================
// Starts, HUD, texts
// =============================================================================================================
TEST(bm_play_modes_and_level_reset) {
    Harness h;
    warmUp(h);
    CHECK_EQ(h.mod.drillMode("ceiling"), 1);  // the ceiling shot starts with the full sequence
    CHECK_EQ(h.mod.drillMode("flick"), 0);
    h.command("mode", {"ceiling", "court"});
    CHECK_EQ(h.mod.drillMode("ceiling"), 0);
    h.command("mode", {"flick", "parcours"});
    CHECK_EQ(h.mod.drillMode("flick"), 1);
    h.command("mode", {"flick", "nimporte"});
    CHECK_EQ(h.mod.drillMode("flick"), 1);
    Store st;
    h.mod.save(st);
    BallMechModule m;
    m.load(st);
    CHECK_EQ(m.drillMode("ceiling"), 0);
    CHECK_EQ(m.drillMode("flick"), 1);
    CHECK_EQ(m.drillMode("pinch"), 0);
    // "Revenir au niveau 1": the tab sends level <id> 0.
    h.command("level", {"pinch", "70"});
    CHECK(h.mod.drillLevel("pinch") > 0.5f);
    h.command("level", {"pinch", "0.0000"});
    CHECK_NEAR(h.mod.drillLevel("pinch"), 0.f, 1e-6f);
    for (const auto& v : h.mod.uiState().mechs)
        if (v.id == "pinch") CHECK_EQ(v.levelNumber, 1);
}

TEST(bm_play_full_hud_lines) {
    // During a full sequence the compact HUD shows the steps, at most 3 main lines, no dash; the first step turns
    // green when validated.
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling"});
    h.skipReady();
    DrawList dl = drawHud(h, true);
    CHECK_EQ(dl.panels.size(), 1u);
    if (dl.panels.empty()) return;
    CHECK(mainLineCount(dl.panels[0]) <= 3);
    bool first = false;
    for (const auto& l : dl.panels[0].lines) {
        CHECK_MSG(noDash(l.text), l.text);
        first = first || (!l.detail && l.text.rfind("1/5 Va au mur de côté, boost", 0) == 0);
    }
    CHECK(first);
    const CeilingRun r = playCeilingRoute(h, true);
    (void)r;
    for (const auto& p : drawHud(h, true).panels) {
        CHECK(mainLineCount(p) <= 3);
        for (const auto& l : p.lines) CHECK_MSG(noDash(l.text), l.text);
    }
    // Every full sequence: labels in plain words, no dash, no number.
    for (int i = 0; i < kMechCount; ++i)
        for (int n : {1, 5, 10})
            for (const auto& l : parcoursLabels(static_cast<Mech>(i), n)) {
                CHECK_MSG(noDash(l) && l.find(" uu") == std::string::npos && l.size() <= 70, l);
            }
    CHECK_EQ(parcoursLabels(Mech::Ceiling, 1).size(), 5u);
    // Every step that can fail has a tip with a short HUD line (never a failure without advice, fix 5).
    PlayerProfile profile;
    for (int i = 0; i < kMechCount; ++i)
        for (int n = 1; n <= 10; ++n) {
            Rng rng(1);
            const DrillSetup d = setupAt(static_cast<Mech>(i), n, 0, rng, DrillMode::Full);
            ParcoursState ps;
            for (const Step& st : parcoursSteps(static_cast<Mech>(i), n, d, ps)) {
                if (st.optional) continue;
                const auto tips = buildTips({st.failCode}, profile, KeyStyle::Xbox);
                CHECK_MSG(tips.size() == 1u && hudTipText(tips[0]) != tips[0].text, st.id + " " + st.failCode);
            }
        }
    CHECK(parcoursLabels(Mech::Ceiling, 1)[3].find("stick vers toi") != std::string::npos);
}

TEST(bm_play_tutorial_texts) {
    // Every drill has a tutorial: principle, steps with what / why / when it is right, mistakes, niveau 1, details.
    // Plain words: no dash, numbers and units only in the technical details, buttons through the placeholders.
    PlayerProfile profile;
    auto units = [](const std::string& s) {
        return s.find(" uu") != std::string::npos || s.find("km/h") != std::string::npos || s.find("°") != std::string::npos ||
               s.find(" %") != std::string::npos || s.find(" ms") != std::string::npos;
    };
    auto plain = [&](const char* text, const std::string& what) {
        const std::string filled = fillButtons(text, profile, KeyStyle::Xbox);
        CHECK_MSG(noDash(text) && filled.find('{') == std::string::npos, what + ": " + text);
        CHECK_MSG(!units(text), what + ": " + text);
    };
    for (int i = 0; i < kMechCount; ++i) {
        const Mech m = static_cast<Mech>(i);
        const Tutorial& t = tutorial(m);
        const std::string id = mechInfo(m).id;
        CHECK_MSG(t.principle && std::string(t.principle).size() > 40, id);
        CHECK_MSG(t.steps.size() >= 3, id);
        CHECK_MSG(t.errors.size() >= 2, id);
        CHECK_MSG(t.level1 && *t.level1, id);
        CHECK_MSG(t.tech.size() >= 2, id);
        plain(t.principle, id);
        plain(t.goodToKnow, id);
        plain(t.before, id);
        plain(t.level1, id);
        for (const auto& st : t.steps) {
            plain(st.title, id);
            plain(st.todo, id);
            plain(st.why, id);
            plain(st.ok, id);
            plain(st.note, id);
        }
        for (const auto& b : t.blocks) {
            plain(b.title, id);
            plain(b.text, id);
        }
        for (const auto& e : t.errors) {
            plain(e.name, id);
            plain(e.fix, id);
        }
        for (const char* line : t.tech) CHECK_MSG(noDash(line), id + ": " + line);
    }
    plain(lexicon(), "lexique");
    plain(progressRule(), "règle");
    plain(pickupBlock().text, "ramassage");
    // Pickup: a ball sliding back means the car outruns it, ease off (never the reversed advice).
    {
        PlayerProfile profile;
        const auto tips = buildTips({"PU_UNSTABLE"}, profile, KeyStyle::Xbox);
        CHECK(tips.size() == 1u);
        for (const std::string& text : {std::string(pickupBlock().text), tips.empty() ? std::string() : tips[0].text}) {
            CHECK_MSG(text.find("accélère un peu si elle glisse vers l'arrière") == std::string::npos, text);
            CHECK_MSG(text.find("si elle glisse vers l'arrière, lâche un peu l'accélérateur") != std::string::npos, text);
        }
    }
    // The ceiling shot explains the reversed stick upside down, in the tutorial and on the HUD step.
    const Tutorial& c = tutorial(Mech::Ceiling);
    CHECK(std::string(c.steps[3].note).find("tire le stick vers toi") != std::string::npos);
    CHECK(std::string(c.goodToKnow).find("stick vers toi") != std::string::npos);
    CHECK(std::string(lexicon()).find("Air roll") != std::string::npos);
}

// =============================================================================================================
// Round 2: flicks like a match (dribble start, 12 m line, ball cam hint), predictions off by default, saved
// defaults moved once
// =============================================================================================================
namespace {
bool hudHas(Harness& h, const std::string& sub, bool mainOnly = true) {
    for (const auto& p : drawHud(h, true).panels)
        for (const auto& l : p.lines)
            if ((!mainOnly || !l.detail) && l.text.find(sub) != std::string::npos) return true;
    return false;
}

// Script positioned at GO of a flick dribble start: the car keeps its speed, the ball rides on the roof.
struct Dribble {
    Script sc;
    Vec3 offset;
    Vec3 fwd;
    explicit Dribble(Harness& h) : sc(h) {
        sc.place(*h.last.setCar);
        ground(sc.s.car);
        sc.s.ball.pos = h.last.setBall->pos;
        sc.s.ball.vel = sc.s.car.vel;
        offset = sc.s.ball.pos - sc.s.car.pos;
        fwd = sc.s.car.basis.fwd;
    }
    // One tick; the ball slides `slide` uu forward on the roof; a re-placement by the module is applied.
    bool ride(float slide = 0.f) {
        offset += fwd * slide;
        const Actions& a = sc.step([&](Snapshot& x) {
            x.car.pos += x.car.vel * DT;
            x.ball.pos = x.car.pos + offset;
            x.ball.vel = x.car.vel;
        });
        if (!a.setBall) return false;
        offset = a.setBall->pos - sc.s.car.pos;
        return true;
    }
};
} // namespace

TEST(bm_round2_flick_dribble_assist_hint_and_12m_line) {
    Harness h;
    warmUp(h);
    h.command("drill", {"flick"});
    CHECK(hudHas(h, "ball cam"));  // during the get-ready hold, a main line
    h.skipReady();
    CHECK(h.last.setCar.has_value() && h.last.setBall.has_value());
    if (!h.last.setCar || !h.last.setBall) return;
    CHECK(length(h.last.setCar->vel) > 899.f);
    Dribble d(h);
    const float go = d.sc.s.time;
    // No throttle at first: the ball slides forward a little; for 0.6 s it is put back on the roof (niveau 1).
    int replaced = 0, lastReplaced = -1;
    for (int i = 0; i < 120; ++i) {
        if (d.ride(0.25f)) {
            ++replaced;
            lastReplaced = i;
        }
    }
    CHECK(replaced >= 60);
    CHECK(lastReplaced >= 0 && lastReplaced <= static_cast<int>(kFlickAssistSeconds * kTickRate) + 1);
    CHECK(h.results.empty());
    // No flick: the dribble into the goal mouth ends at the 12 m line, after several seconds, a failure with its tip.
    for (int i = 0; i < 12 * 120 && h.results.empty(); ++i) d.ride();
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(hasTip(h.results[0], "bm.fl_too_close"));
    }
    CHECK(d.sc.s.time - go >= 3.6f);
    CHECK(bm::arena::kBackWallY - d.sc.s.car.pos.y < kFlickLateDist + 20.f);
    CHECK_EQ(h.mod.uiState().drill.attempts, 1);
}

TEST(bm_round2_flick_ball_lost_at_go_is_replayed_twice) {
    // The ball falls off right after GO (the player did not take the speed): replayed without counting, twice in a
    // row at most; the third time it counts.
    Harness h;
    warmUp(h);
    h.command("level", {"flick", levelArg(7)});  // no carry assist above niveau 6
    h.command("drill", {"flick"});
    int toasts = 0;
    for (int k = 0; k < 3; ++k) {
        h.skipReady();
        if (!h.last.setCar || !h.last.setBall) return;
        Dribble d(h);
        for (int i = 0; i < 30; ++i) d.ride();
        Vec3 ballVel = d.sc.s.car.vel + d.fwd * 600.f;
        Vec3 ballPos = d.sc.s.ball.pos;
        for (int i = 0; i < 240 && h.mod.uiState().drill.phase == "À toi !"; ++i) {
            d.sc.step([&](Snapshot& x) {
                x.car.pos += x.car.vel * DT;
                ballVel.z -= 650.f * DT;
                ballPos += ballVel * DT;
                if (ballPos.z < 93.15f) {
                    ballPos.z = 93.15f;
                    ballVel.z = 0.f;
                }
                x.ball.pos = ballPos;
                x.ball.vel = ballVel;
            });
        }
        int now = 0;
        for (const auto& t : h.toasts) now += t.text.find("rejoué") != std::string::npos ? 1 : 0;
        if (k < 2) {
            CHECK(h.results.empty());
            CHECK_EQ(now, toasts + 1);
        }
        toasts = now;
    }
    CHECK_EQ(h.results.size(), 1u);
    if (!h.results.empty()) CHECK(hasTip(h.results[0], "bm.drill_ball_lost"));
    CHECK_EQ(toasts, 2);
}

TEST(bm_round2_flick_parcours_starts_at_speed_far_from_goal) {
    for (Mech m : {Mech::Flick, Mech::Musty, Mech::Breezi}) {
        for (int pu = 1; pu <= 10; ++pu) {
            Rng rng(5 + static_cast<uint64_t>(pu));
            const DrillSetup d = setupAt(m, 1, 0, rng, DrillMode::Full, pu);
            CHECK(d.pickup && d.pickupChase && !d.pickupRolling);
            CHECK_NEAR(bm::arena::kBackWallY - d.car.pos.y, 5500.f, 1.f);
            const float vc = length(d.car.vel), vb = length(d.ball.vel);
            CHECK(vc >= 699.f && vc <= 921.f && vb >= 449.f && vb <= 521.f);
            CHECK(dot(d.ball.vel, d.car.vel) > 0.f);
            // Core physics: the ball keeps rolling ahead; the car at its speed meets it within 2 s, gently, far out.
            const auto ball = pb::simBall(d.ball.pos, d.ball.vel, 3.f, d.ball.angVel);
            const Vec3 f = normalize(d.car.vel);
            bool met = false;
            for (size_t i = 1; i < ball.size() && !met; ++i) {
                const float t = static_cast<float>(i) * kTickDt;
                const Vec3 car = d.car.pos + d.car.vel * t;
                if (dot(ball[i].p - car, f) <= 13.88f + 59.f + 91.25f) {
                    met = true;
                    CHECK(t <= 2.f);
                    const float rel = vc - dot(ball[i].v, f);
                    CHECK_MSG(rel >= 200.f && rel <= 450.f, std::to_string(rel));
                    CHECK(bm::arena::kBackWallY - car.y > 3500.f);
                }
            }
            CHECK(met);
        }
        // The steps name the pickup at speed.
        const auto labels = parcoursLabels(m, 1, "");
        CHECK(!labels.empty() && labels[0].find(pickup::kRoofLabelChase) != std::string::npos);
    }
    // The air dribble keeps its pickup with the car stopped.
    Rng rng(3);
    const DrillSetup ad = setupAt(Mech::AirDribble, 1, 0, rng, DrillMode::Full, 1);
    CHECK(ad.pickup && !ad.pickupChase && length(ad.car.vel) < 1e-3f);
}

TEST(bm_round2_ceiling_guide_and_trajectory_off_by_default) {
    SettingsSchema schema;
    BallMechModule::describeSettings(schema);
    CHECK(schema.find("rlt_bm_draw_trajectory")->defaultValue == "0");
    CHECK(schema.find("rlt_bm_draw_guide")->defaultValue == "0");
    // Ceiling start, niveau 1: the predicted path and its ring are only drawn with "Guide du départ au plafond".
    Harness h;
    warmUp(h);
    h.command("drill", {"ceiling", "court"});
    CHECK(drawHud(h, true).lines3D.empty());
    h.set("rlt_bm_draw_guide", "1");
    CHECK(drawHud(h, true).lines3D.size() >= 15);
    CHECK(drillLevelNote(Mech::Ceiling, 0.f).find("cercle") == std::string::npos);
}

TEST(bm_round2_old_defaults_migrated_once) {
    // A player of the previous version: config.cfg holds the old default (trajectory drawn). The first tick turns it
    // off once; a reload never does it again; a value chosen differently is kept.
    auto oneTick = [](Harness& h) {
        Sim sim;
        sim.step(1);
        h.run(continueFrom(h, sim.out));
        return h.last;
    };
    Harness h;
    h.mod.load(Store{});  // save of the previous version: no flag
    h.set("rlt_bm_draw_trajectory", "1");
    CHECK(!h.mod.defaultsMigrated());
    const Actions first = oneTick(h);
    CHECK_EQ(first.setCvars.size(), 1u);
    if (!first.setCvars.empty()) {
        CHECK(first.setCvars[0].first == "rlt_bm_draw_trajectory" && first.setCvars[0].second == "0");
        h.set(first.setCvars[0].first, first.setCvars[0].second);
    }
    CHECK(first.requestSave);
    CHECK_EQ(first.toasts.size(), 1u);
    CHECK(!first.toasts.empty() && noDash(first.toasts[0].text));
    if (!first.toasts.empty())  // the toast shared by every trajectory (one line once merged by the engine)
        CHECK(first.toasts[0].title == "Trajectoires" && first.toasts[0].migration &&
              first.toasts[0].text.find("page HUD, section Trajectoires") != std::string::npos);
    CHECK(!h.mod.config().drawTrajectory);
    const Actions second = oneTick(h);
    CHECK(second.setCvars.empty() && second.toasts.empty());
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("ballmech.defaults_v2", 0), 1);
    Harness again;
    again.mod.load(st);
    again.set("rlt_bm_draw_trajectory", "1");  // the player turned it back on
    CHECK(oneTick(again).setCvars.empty());
    CHECK(again.mod.config().drawTrajectory);
    Harness chosen;
    chosen.mod.load(Store{});
    chosen.set("rlt_bm_draw_trajectory", "0");
    const Actions kept = oneTick(chosen);
    CHECK(kept.setCvars.empty() && kept.toasts.empty() && kept.requestSave);
    CHECK(chosen.mod.defaultsMigrated());
}
