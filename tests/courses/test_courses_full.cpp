// "Parcours complet" of every Parcours exercise (cross-cutting fix 2): each full sequence played through by a
// scripted player who reacts 0.4 s after GO, at niveau 1, ends in SUCCESS; a missing step fails the attempt with its
// name and tip (never silently); a missed pickup only lowers the pickup's own level.

#include "tests/TestFramework.h"

#include <algorithm>
#include <cmath>

#include "core/common/Pickup.h"
#include "core/modules/courses/Tutorials.h"
#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

constexpr int kReactionTicks = 48;  // 0.4 s
constexpr float kDt = 1.f / 120.f;

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

bool running(const Harness& h) { return h.mod.phase() == CoursesPhase::Running; }

Basis frame(const Vec3& fwd, const Vec3& upHint) {
    Basis b;
    b.fwd = normalize(fwd, Vec3{1.f, 0.f, 0.f});
    b.up = normalize(upHint - b.fwd * dot(upHint, b.fwd), Vec3{0.f, 0.f, 1.f});
    b.right = cross(b.up, b.fwd);
    return b;
}

// Car with its hitbox centre at `c`.
void pose(Harness& h, const Vec3& c, const Basis& b, const Vec3& v, bool ground, int wheels) {
    h.snap.car.basis = b;
    h.snap.car.pos = c - b.toWorld(h.snap.car.hitboxOffset);
    h.snap.car.vel = v;
    h.snap.car.onGround = ground;
    h.snap.car.wheelContacts = wheels;
}

// Starts the full sequence of `kind` and runs the get-ready phase (throttle pressed).
void startFull(Harness& h, const char* kind) {
    h.set("rlt_co_chain", "3");
    h.keepLog = true;
    h.command("mode", {kind, "parcours"});
    const std::string k = kind;
    h.command(k == "air" || k == "ground" || k == "airdribble" ? "rings" : "control", {kind});
    h.runCountdown();
}

void wait(Harness& h, int ticks) {
    for (int i = 0; i < ticks && running(h); ++i) h.tick();
}

// Moves the car hitbox centre in a straight line to `to` at `speed`, basis kept, extra per-tick setup in `each`.
template <class F>
void moveTo(Harness& h, const Vec3& to, float speed, const Basis& b, bool ground, int wheels, F each) {
    for (int guard = 0; guard < 2400 && running(h); ++guard) {
        const Vec3 c = h.carCentre();
        const Vec3 d = to - c;
        const float l = length(d);
        if (l < 1e-3f) break;
        const float step = std::min(l, speed * kDt);
        const Vec3 dir = d / l;
        pose(h, c + dir * step, b, dir * speed, ground, wheels);
        each();
        h.tick();
        if (step >= l) break;
    }
}

// Shared pickup at niveau 1: the player waits 0.4 s, rolls slowly to the resting ball and gets it on the roof (the
// climb up the hood is not simulated, it is unverified in game), then carries it still on the roof for `carry` s.
void scriptedPickup(Harness& h, float carrySpeed, float carry) {
    CHECK(h.mod.steps().indexOf(pickup::kStepRoof) >= 0);
    wait(h, kReactionTicks);
    const Basis b = h.snap.car.basis;
    const Vec3 ball = h.snap.ball.pos;
    // Drive until the bumper reaches the ball.
    for (int guard = 0; guard < 600 && running(h); ++guard) {
        const Vec3 c = h.carCentre();
        if (dot(ball - c, b.fwd) < 59.f + 91.f) break;
        pose(h, c + b.fwd * (400.f * kDt), b, b.fwd * 400.f, true, 4);
        h.tick();
    }
    // Ball on the roof, carried.
    Vec3 bp = h.snap.ball.pos;
    bp = courses::roofSpawn(h.snap.car.pos, b, h.snap.car.hitboxOffset, h.snap.car.hitboxExtent, h.snap.ball.radius);
    const int n = static_cast<int>(carry * 120.f);
    for (int i = 0; i < n && running(h); ++i) {
        bp = bp + b.fwd * (carrySpeed * kDt);
        h.ballOnRoofAt(bp, b.fwd * carrySpeed);
        h.tick();
    }
}

} // namespace

TEST(courses_full_air_takeoff_scripted) {
    Harness h;
    startFull(h, "air");
    CHECK(h.mod.fullRun());
    CHECK(running(h));
    CHECK(length(h.snap.car.vel) < 1e-3f);
    CHECK_EQ(h.mod.steps().size(), 5);
    const Vec3 rc = courses::toF(h.mod.course().rings[0].center);
    wait(h, kReactionTicks);
    const Basis flat = h.snap.car.basis;
    // Drive up to 1200 uu/s until 13 m before the ring.
    float v = 0.f;
    while (running(h) && rc.y - h.carCentre().y > 1300.f) {
        v = std::min(1200.f, v + 1500.f * kDt);
        pose(h, h.carCentre() + Vec3{0.f, v * kDt, 0.f}, flat, {0.f, v, 0.f}, true, 4);
        h.tick();
    }
    // Jump, lift the nose over 0.3 s, boost on a straight line through the ring centre.
    const Vec3 from = h.carCentre();
    const Vec3 dir = normalize(rc - from);
    const Vec3 past = rc + dir * 300.f;
    float t = 0.f;
    while (running(h) && dot(past - h.carCentre(), dir) > 0.f) {
        t += kDt;
        const float pitch = std::min(40.f, 40.f * t / 0.3f) * kDegToRad;
        const Basis b = frame({0.f, std::cos(pitch), std::sin(pitch)}, {0.f, 0.f, 1.f});
        h.snap.in.jump = t < 0.2f;
        h.snap.car.jumped = true;
        h.snap.in.boost = t > 0.3f;
        pose(h, h.carCentre() + dir * (1200.f * kDt), b, dir * 1200.f, false, 0);
        h.tick();
    }
    // Nose back down, drop to the floor and land on the wheels.
    h.snap.in.boost = false;
    const Basis level = frame({0.f, 1.f, 0.f}, {0.f, 0.f, 1.f});
    moveTo(h, h.carCentre() + Vec3{0.f, 600.f, 0.f} - Vec3{0.f, 0.f, h.carCentre().z - 38.f}, 1200.f, level, false, 0, [] {});
    pose(h, h.carCentre(), level, {0.f, 900.f, 0.f}, true, 4);
    wait(h, 3);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    if (r) CHECK_EQ(r->skill, std::string("course.rings.air.full"));
    CHECK(h.mod.lastAttempt().headline.find("Parcours complet réussi") == 0);
}

TEST(courses_full_air_takeoff_missing_jump_names_the_step) {
    // The player never jumps: the attempt fails on "Saute" with its tip (a timeout is never silent), and the full
    // sequence's own staircase records it.
    Store st;
    st.setFloat("courses.stair_full.air.level", 0.5f);
    st.setInt("courses.stair_full.air.attempts", 3);
    Harness h;
    h.mod.load(st);
    startFull(h, "air");
    CHECK_EQ(h.mod.fullDifficultyFor(CoursesKind::RingsAir), 50);
    const Basis flat = h.snap.car.basis;
    for (int i = 0; i < 6 * 120 && running(h); ++i) {
        pose(h, h.carCentre() + Vec3{0.f, 300.f * kDt, 0.f}, flat, {0.f, 300.f, 0.f}, true, 4);
        h.tick();
    }
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success);
    if (r) {
        CHECK(hasTip(*r, "courses.full_air_no_jump"));
        CHECK(!r->tips.empty() && !r->tips[0].text.empty());
    }
    CHECK(h.mod.lastAttempt().headline.find("Il manquait : Saute") != std::string::npos);
    CHECK(h.mod.fullDifficultyFor(CoursesKind::RingsAir) < 50);
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), 0);  // the normal level is not touched
}

TEST(courses_full_wall_climb_scripted) {
    Harness h;
    startFull(h, "ground");
    CHECK(running(h));
    const auto& ring = h.mod.course().rings[0];
    const float side = ring.center.x > 0.0 ? 1.f : -1.f;
    const Vec3 rc = courses::toF(ring.center), rn = courses::toF(ring.normal);
    wait(h, kReactionTicks);
    // Toward the wall in biais (the start heading), up to the curve at the bottom of the wall.
    const Basis start = h.snap.car.basis;
    while (running(h) && side * h.snap.car.pos.x < 4096.f - 300.f) {
        pose(h, h.carCentre() + start.fwd * (1000.f * kDt), start, start.fwd * 1000.f, true, 4);
        h.tick();
    }
    // On the wall: roof toward the field, through the arch, then 3 m further.
    const Basis wall = frame(rn, {-side, 0.f, 0.f});
    Vec3 c = h.carCentre();
    pose(h, {side * (4096.f - 38.f), c.y, 120.f}, wall, wall.fwd * 1000.f, true, 4);
    h.tick();
    moveTo(h, Vec3{side * (4096.f - 38.f), rc.y, rc.z}, 1000.f, wall, true, 4, [] {});
    moveTo(h, Vec3{side * (4096.f - 38.f), rc.y + rn.y * 300.f, rc.z + rn.z * 300.f}, 1000.f, wall, true, 4, [] {});
    // Back down the wall, then on the floor, rolling.
    const Basis downWall = frame({0.f, 0.6f, -0.8f}, {-side, 0.f, 0.f});
    c = h.carCentre();
    moveTo(h, Vec3{c.x, c.y + 450.f, 120.f}, 1000.f, downWall, true, 4, [] {});
    const Basis floor = frame({-side * 0.6f, 0.8f, 0.f}, {0.f, 0.f, 1.f});
    c = h.carCentre();
    pose(h, {c.x - side * 120.f, c.y + 60.f, 38.f}, floor, floor.fwd * 900.f, true, 4);
    wait(h, 3);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
}

TEST(courses_full_wall_climb_straight_into_the_wall_fails_with_the_tip) {
    Harness h;
    startFull(h, "ground");
    const float side = h.mod.course().rings[0].center.x > 0.0 ? 1.f : -1.f;
    wait(h, kReactionTicks);
    const Basis straight = frame({side, 0.f, 0.f}, {0.f, 0.f, 1.f});  // face to the wall
    for (int i = 0; i < 4 * 120 && running(h); ++i) {
        pose(h, h.carCentre() + straight.fwd * (1000.f * kDt), straight, straight.fwd * 1000.f, true, 4);
        h.tick();
    }
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success);
    if (r) CHECK(hasTip(*r, "courses.full_wall_straight"));
}

TEST(courses_full_air_dribble_scripted) {
    Harness h;
    startFull(h, "airdribble");
    CHECK(running(h));
    CHECK_EQ(h.mod.pickupNiveau(), 1);
    CHECK(h.snap.ball.pos.z < 100.f);  // niveau 1 of the pickup: the ball rests on the floor ahead
    scriptedPickup(h, 600.f, 2.f);
    CHECK(h.mod.steps().done(pickup::kStepRoof));
    CHECK(h.mod.steps().done(pickup::kStepSettle));
    CHECK(h.mod.steps().done("ad.carry"));
    // Pop: the car jumps, the ball leaves the roof upward.
    const Basis b = h.snap.car.basis;
    Vec3 bp = h.snap.ball.pos;
    Vec3 c = h.carCentre();
    for (int i = 0; i < 12 && running(h); ++i) {
        h.snap.in.jump = true;
        h.snap.car.jumped = true;
        c = c + Vec3{0.f, 600.f * kDt, 300.f * kDt};
        bp = bp + Vec3{0.f, 600.f * kDt, 450.f * kDt};
        pose(h, c, b, {0.f, 600.f, 300.f}, false, 0);
        h.snap.ball.pos = bp;
        h.snap.ball.vel = {0.f, 600.f, 450.f};
        h.tick();
    }
    h.snap.in.jump = false;
    CHECK(h.mod.steps().done("ad.pop"));
    // Follow under the ball with the boost and push it through the ring.
    const Vec3 rc = courses::toF(h.mod.course().rings[0].center), rn = courses::toF(h.mod.course().rings[0].normal);
    const Vec3 target = rc + rn * 300.f;
    for (int guard = 0; guard < 600 && running(h); ++guard) {
        const Vec3 d = target - bp;
        if (length(d) < 10.f) break;
        const Vec3 dir = normalize(d);
        bp = bp + dir * (900.f * kDt);
        h.snap.ball.pos = bp;
        h.snap.ball.vel = dir * 900.f;
        h.snap.in.boost = true;
        pose(h, bp - dir * 150.f - Vec3{0.f, 0.f, 100.f}, b, dir * 900.f, false, 0);
        h.tick();
    }
    wait(h, 2);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    CHECK(h.mod.pickupNiveau() >= 1);
}

TEST(courses_full_dribble_scripted_and_pickup_has_its_own_level) {
    {
        Harness h;
        startFull(h, "dribble");
        CHECK(running(h));
        CHECK(h.mod.steps().steps()[0].optional);  // "Mets ton nez sous la balle": an info step
        scriptedPickup(h, 150.f, 1.f);
        CHECK(h.mod.steps().done(pickup::kStepSettle));
        // Niveau 1 of the dribble: 5 s with the ball on the roof, any speed.
        Vec3 bp = h.snap.ball.pos;
        const Vec3 fwd = h.snap.car.basis.fwd;
        for (int i = 0; i < 6 * 120 && running(h); ++i) {
            bp = bp + fwd * (100.f * kDt);
            h.ballOnRoofAt(bp, fwd * 100.f);
            h.tick();
        }
        const AttemptResult* r = lastResult(h);
        CHECK(r && r->success);
    }
    // A missed pickup (the player never moves): fails on the pickup after its 8 s, lowers only the pickup level.
    Store st;
    st.setFloat("courses.stair_full.dribble.level", 0.5f);
    st.setInt("courses.stair_full.dribble.attempts", 4);
    st.setFloat("courses.pickup.level", 0.5f);
    st.setInt("courses.pickup.attempts", 4);
    Harness h;
    h.mod.load(st);
    const int pickupBefore = h.mod.pickupNiveau();
    startFull(h, "dribble");
    wait(h, 10 * 120);
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success);
    if (r) CHECK(hasTip(*r, "pickup.no_roof"));
    // Niveau 6 of the pickup: the ball rolls toward the car, the missing step says so.
    CHECK_MSG(h.mod.lastAttempt().headline == std::string("Raté. Il manquait : ") + pickup::kRoofLabelRolling,
              h.mod.lastAttempt().headline);
    CHECK_EQ(h.mod.fullDifficultyFor(CoursesKind::Dribble), 50);
    CHECK(h.mod.pickupNiveau() < pickupBefore);
}

TEST(courses_full_slalom_scripted) {
    Harness h;
    startFull(h, "slalom");
    CHECK(running(h));
    const auto& course = h.mod.course();
    CHECK(course.rings.size() >= 5u);
    // The pickup happens before the first gate.
    const float dirY = course.rings[0].center.y > h.snap.car.pos.y ? 1.f : -1.f;
    CHECK(dirY * (static_cast<float>(course.rings[0].center.y) - h.snap.ball.pos.y) > 1000.f);
    scriptedPickup(h, 300.f, 1.f);
    CHECK(h.mod.steps().done(pickup::kStepSettle));
    // Weave through the gate centres with the ball on the roof.
    std::vector<Vec3> pts{h.snap.ball.pos};
    for (const auto& g : course.rings) pts.push_back({static_cast<float>(g.center.x), static_cast<float>(g.center.y), h.snap.ball.pos.z});
    pts.push_back(pts.back() + Vec3{0.f, dirY * 400.f, 0.f});
    h.followPathCarry(pts, 700.f, Harness::Carry::BallOnRoof);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    CHECK(h.mod.steps().done("slalom.weave.0"));
}

TEST(courses_full_catch_scripted) {
    Harness h;
    startFull(h, "catch");
    CHECK(running(h));
    CHECK(h.snap.ball.vel.z > 900.f);  // the lob leaves the floor at GO
    Vec3 bp = h.snap.ball.pos, bv = h.snap.ball.vel, cv{};
    bool touched = false, resting = false;
    for (int tick = 0; tick < 12 * 120 && running(h); ++tick) {
        CarState& car = h.snap.car;
        const Vec3 roofRest = courses::roofSpawn(car.pos, car.basis, car.hitboxOffset, car.hitboxExtent, h.snap.ball.radius, 0.f);
        if (resting) {
            bp = roofRest;
            bv = cv;
        } else {
            bv = bv * (1.f - kDt * 0.03f) + Vec3{0.f, 0.f, -650.f * kDt};
            bp = bp + bv * kDt;
            const Vec3 local = car.basis.toLocal(bp - hitboxCenter(car));
            if (std::fabs(local.x) <= 59.f && std::fabs(local.y) <= 42.f && bp.z <= roofRest.z && bv.z < 0.f) {
                const float impact = -bv.z;
                if (!touched) {
                    TouchEvent te;
                    te.pre = h.snap;
                    te.pre.ball.pos = bp;
                    te.pre.ball.vel = bv;
                    te.pre.car.vel = cv;
                    Actions ta;
                    h.mod.onTouch(te, h.ctx(), ta);
                    touched = true;
                }
                bp.z = roofRest.z;
                bv.z = impact * 0.65f;
                bv = bv + car.basis.fwd * (0.12f * impact);
                if (bv.z < 40.f) resting = true;
            }
        }
        if (tick >= kReactionTicks) {
            Vec3 target = bp, land;
            float tl = 0.f;
            if (!resting && courses::predictDownCrossing(bp, bv, roofRest.z, 5.f, land, tl)) target = land;
            const Vec3 hc = hitboxCenter(car);
            const Vec3 to{target.x - hc.x, target.y - hc.y, 0.f};
            const float dist = length(to);
            const Vec3 dir = dist > 1e-3f ? to / dist : Vec3{};
            const float want = std::min(1400.f, std::sqrt(2.f * 3500.f * dist) * 0.8f);
            Vec3 dv = dir * want - cv;
            const float maxDv = (dot(dv, cv) < 0.f ? 3500.f : 1500.f) * kDt;
            if (length(dv) > maxDv) dv = dv * (maxDv / length(dv));
            cv = cv + dv;
            car.pos = car.pos + cv * kDt;
        }
        car.vel = cv;
        h.snap.ball.pos = bp;
        h.snap.ball.vel = bv;
        h.tick(false);
    }
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    if (r && !r->success) std::printf("    %s\n", h.mod.lastAttempt().headline.c_str());
}

TEST(courses_full_hud_shows_the_steps) {
    Harness h;
    h.set("rlt_co_chain", "3");
    h.command("mode", {"catch", "parcours"});
    h.command("control", {"catch"});
    h.tick();
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f, true};
    ViewContext view;
    view.valid = true;
    DrawList dl;
    h.mod.draw(view, dctx, dl);
    CHECK_EQ(dl.panels.size(), 1u);
    if (dl.panels.empty()) return;
    const DrawPanel& p = dl.panels[0];
    CHECK(p.title.find("Parcours complet : rattrape une chandelle") == 0);
    bool ready = false, first = false;
    for (const auto& l : p.lines) {
        ready = ready || l.text == "Appuie sur accélérer quand tu es prêt";
        first = first || (l.text.find("1/5 ") == 0 && !l.detail);
        CHECK(l.text.find("\xE2\x80\x94") == std::string::npos && l.text.find("\xE2\x80\x93") == std::string::npos);
    }
    CHECK(ready && first);
    // Mode back to normal.
    const Actions a = h.command("mode", {"catch", "normal"});
    CHECK(a.requestSave);
    CHECK(!h.mod.fullMode(CoursesKind::Catch));
    Store st;
    h.mod.save(st);
    CHECK_EQ(st.getInt("courses.full.catch", 1), 0);
}

// ---- Tutorials ------------------------------------------------------------------------------------------------------
TEST(courses_tutorials_are_plain_and_complete) {
    auto plain = [](const std::string& s) {
        // No em or en dash anywhere; numbers with units only in "Détails techniques".
        CHECK_MSG(s.find("\xE2\x80\x94") == std::string::npos && s.find("\xE2\x80\x93") == std::string::npos, s);
    };
    auto noUnits = [](const std::string& s) {
        CHECK_MSG(s.find(" uu") == std::string::npos && s.find("km/h") == std::string::npos && s.find(" ms") == std::string::npos, s);
    };
    for (int i = 0; i < kCoursesKindCount; ++i) {
        const auto k = static_cast<CoursesKind>(i);
        const courses::Tutorial& t = courses::tutorial(k);
        CHECK(*t.intro && *t.level1 && *t.full);
        CHECK(t.steps.size() >= 3u);
        CHECK(t.panel.size() >= 3u);
        CHECK(t.fullSteps.size() >= 4u);
        CHECK(!t.tech.empty());
        for (const char* s : {t.intro, t.before, t.level1, t.full}) {
            plain(s);
            noUnits(s);
        }
        for (const auto& st : t.steps)
            for (const char* s : {st.todo, st.why, st.ok}) {
                plain(s);
                noUnits(s);
            }
        for (const char* s : t.panel) plain(s);
        for (const char* s : t.fullSteps) plain(s);
        for (const char* s : t.tech) plain(s);
        plain(CoursesModule::kindHelp(k));
        plain(CoursesModule::kindFullTitle(k));
    }
    plain(courses::lexicon());
    plain(courses::pathAdvice());
    plain(courses::pickupText());
    // Pickup: a ball sliding back means the car outruns it, ease off (never the reversed advice).
    CHECK(std::string(courses::pickupText()).find("accélère un peu si elle glisse vers l'arrière") == std::string::npos);
    CHECK(std::string(courses::pickupText()).find("si elle glisse vers l'arrière, lâche un peu l'accélérateur") !=
          std::string::npos);
    CHECK(std::string(courses::lexicon()).find("Kickoff : l'engagement au centre.") != std::string::npos);
    CHECK(std::string(courses::progressRule()).find("3 essais sur 4 au niveau 4") != std::string::npos);
    // The player's own buttons replace the placeholders.
    PlayerProfile profile;
    const std::string filled = courses::fillButtons("saute avec {jump} puis {boost}", profile, KeyStyle::Xbox);
    CHECK(filled.find('{') == std::string::npos);
}
