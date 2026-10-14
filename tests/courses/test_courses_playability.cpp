// Playability of the Parcours drills (drill audit of v1.0.6): the beginner levels a Diamond player can do the first
// time he plays them, checked with a scripted player who reacts 0.4 s after GO, and the regression of each broken
// setup. Assertions are on SUCCESS, not only on a result being there.

#include "tests/TestFramework.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/modules/courses/CourseCode.h"
#include "core/modules/courses/CourseGen.h"
#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

constexpr int kReactionTicks = 48;  // 0.4 s

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

void manualDifficulty(Harness& h, int pct) {
    h.set("rlt_co_difficulty_mode", "5");
    h.set("rlt_co_difficulty", std::to_string(pct));
}

void waitTicks(Harness& h, int n) {
    for (int i = 0; i < n && h.mod.phase() == CoursesPhase::Running; ++i) h.tick();
}

// Lowest point of a ring opening the object centre can cross (ring standing up, `objRadius` of clearance).
double lowestPass(const courses::Ring& r, double objRadius) {
    const double tilt = std::sqrt(std::max(0.0, 1.0 - r.normal.z * r.normal.z));
    return r.center.z - (r.radius - objRadius) * tilt;
}

// Ground arc radius through two consecutive rings (chord and change of travel direction).
double arcRadius(const courses::Ring& a, const courses::Ring& b) {
    const double chord = courses::length(b.center - a.center);
    const double cosA = std::clamp(courses::dot(a.normal, b.normal), -1.0, 1.0);
    const double alpha = std::acos(cosA);
    return alpha < 1e-6 ? 1e12 : chord / (2.0 * std::sin(alpha / 2.0));
}

} // namespace

// ---- Anneaux aériens: the Initiation ------------------------------------------------------------------------------
TEST(courses_aerial_initiation_is_passable_without_flying) {
    using namespace courses;
    CHECK_EQ(beginnerStage(CourseType::Aerial, 0), 1);
    CHECK_EQ(beginnerStage(CourseType::Aerial, 10), 2);
    CHECK_EQ(beginnerStage(CourseType::Aerial, 20), 3);
    CHECK_EQ(beginnerStage(CourseType::Aerial, 30), 0);  // niveau 4: the regular courses
    CHECK_EQ(beginnerStage(CourseType::Aerial, 0, 1), 0);
    const double groundTurn = 1.25 / maxGroundCurvature(1200.0);
    for (uint64_t s = 1; s <= 500; ++s) {
        const Course c = generateCourse(s * 7919 + 3, CourseType::Aerial, 0);
        CHECK_EQ(c.rings.size(), 4u);
        if (c.rings.empty()) continue;
        CHECK(length(c.start.carVel) < 1e-9);  // released at rest
        const Ring& f = c.rings[0];
        CHECK(std::hypot(f.center.x - c.start.carPos.x, f.center.y - c.start.carPos.y) >= kAerialFirstRingMin - 1.0);
        for (size_t i = 0; i < c.rings.size(); ++i) {
            const Ring& r = c.rings[i];
            CHECK(r.radius >= 600.0 - 1e-9);
            CHECK(r.center.z - (r.radius - c.objRadius) <= 230.0);  // the car passes on the floor
            CHECK(r.center.z + r.radius < phys::kCeilingZ - 150.0);
            if (i == 0) continue;
            // Turns: less than half the boost acceleration at 1100 uu/s, and wide enough to drive on the floor.
            const DVec3 bend = (r.normal - c.rings[i - 1].normal) * (1100.0 * 1100.0 / length(r.center - c.rings[i - 1].center));
            CHECK(length(bend) <= 0.5 * phys::kBoostAccelAir);
            CHECK(arcRadius(c.rings[i - 1], r) >= groundTurn);
        }
    }
    // Niveau 2: a single held jump is enough (hitbox centre peak about 270); niveau 3: the first real aerial.
    int needFlight = 0, courses3 = 0;
    for (uint64_t s = 1; s <= 300; ++s) {
        const Course c2 = generateCourse(s * 7919 + 3, CourseType::Aerial, 10);
        CHECK_EQ(c2.rings.size(), 5u);
        for (const Ring& r : c2.rings) CHECK(lowestPass(r, c2.objRadius) <= 200.0 + 1e-6);
        const Course c3 = generateCourse(s * 7919 + 3, CourseType::Aerial, 20);
        CHECK_EQ(c3.rings.size(), 6u);
        ++courses3;
        bool fly = false;
        for (const Ring& r : c3.rings) fly = fly || lowestPass(r, c3.objRadius) > 270.0;
        if (fly) ++needFlight;
        for (const Course* c : {&c2, &c3}) {
            if (c->rings.empty()) continue;
            CHECK(std::hypot(c->rings[0].center.x - c->start.carPos.x, c->rings[0].center.y - c->start.carPos.y) >=
                  kAerialFirstRingMin - 1.0);
        }
    }
    CHECK(needFlight * 10 >= courses3 * 8);
    // Regular courses from niveau 4: ring 1 also at least 2500 uu ahead.
    for (uint64_t s = 1; s <= 200; ++s)
        for (int pct : {30, 60, 100}) {
            const Course c = generateCourse(s * 31 + 7, CourseType::Aerial, pct);
            CHECK(!c.rings.empty());
            if (!c.rings.empty())
                CHECK(std::hypot(c.rings[0].center.x - c.start.carPos.x, c.rings[0].center.y - c.start.carPos.y) >=
                      kAerialFirstRingMin - 1.0);
        }
    // A fresh module starts the aerial rings at Initiation 1.
    Harness h;
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), 0);
    // Regression: version 1 at difficulty 0, almost every ring needed a real flight (lowest point above 300).
    int high = 0, total = 0;
    for (uint64_t s = 1; s <= 200; ++s) {
        const Course v1 = generateCourse(s * 7919 + 3, CourseType::Aerial, 0, nullptr, 1);
        for (const Ring& r : v1.rings) {
            ++total;
            if (lowestPass(r, v1.objRadius) > 300.0) ++high;
        }
    }
    CHECK(high * 10 >= total * 9);
}

TEST(courses_aerial_initiation_run_with_a_late_start) {
    // Niveau 1 on the floor, niveau 2 with single jumps: the player waits 0.4 s after GO, then goes through every
    // ring at 1000 uu/s, never higher than a single jump.
    for (int pct : {0, 10}) {
        Harness h;
        manualDifficulty(h, pct);
        h.set("rlt_co_chain", "3");
        h.keepLog = true;
        h.command("rings", {"air"});
        h.runCountdown();
        CHECK(h.mod.phase() == CoursesPhase::Running);
        CHECK(length(h.snap.car.vel) < 1e-3f);  // released at rest
        waitTicks(h, kReactionTicks);
        std::vector<Vec3> pts{h.carCentre()};
        for (const auto& r : h.mod.course().rings) {
            const float z = std::max(38.f, static_cast<float>(r.center.z - (r.radius - 70.0)));
            CHECK(z <= 250.f);
            pts.push_back({static_cast<float>(r.center.x), static_cast<float>(r.center.y), z});
        }
        const auto& last = h.mod.course().rings.back();
        pts.push_back(pts.back() + courses::toF(last.normal) * 400.f);
        h.followPath(pts, 1000.f, false);
        const AttemptResult* res = lastResult(h);
        CHECK_MSG(res && res->success, std::to_string(pct));
        CHECK(h.mod.run().misses() == 0);
    }
}

// ---- Anneaux sol et murs ----------------------------------------------------------------------------------------
TEST(courses_ground_beginner_courses_stay_on_the_floor) {
    using namespace courses;
    const double groundTurn = 1.25 / maxGroundCurvature(1200.0);
    int courses = 0;
    for (int dp = 0; dp <= kGroundFloorOnlyPct; dp += 5)
        for (uint64_t s = 1; s <= 72; ++s) {
            const Course c = generateCourse(s * 7919 + 3 + static_cast<uint64_t>(dp), CourseType::GroundWall, dp);
            ++courses;
            CHECK(!c.rings.empty());
            for (size_t i = 0; i < c.rings.size(); ++i) {
                const Ring& r = c.rings[i];
                CHECK(r.surface == Surface::Floor);
                CHECK(r.radius >= 450.0 - 1e-9);
                if (i > 0) CHECK(arcRadius(c.rings[i - 1], r) >= groundTurn);
            }
        }
    CHECK(courses >= 500);
    // From niveau 4 the wall arches come (the wall driving of the ceiling shot route).
    for (int dp : {40, 50, 70, 100}) {
        int withWall = 0, total = 0;
        for (uint64_t s = 1; s <= 500; ++s) {
            const Course c = generateCourse(s * 7919 + 3, CourseType::GroundWall, dp);
            ++total;
            bool wall = false;
            for (const Ring& r : c.rings) {
                if (r.surface != Surface::WallPosX && r.surface != Surface::WallNegX) continue;
                wall = true;
                CHECK(std::fabs(r.center.x) > phys::kArenaX - 300.0);  // standing on the wall (drawn with "MUR")
            }
            if (wall) ++withWall;
        }
        CHECK_MSG(withWall * 5 >= total, std::to_string(dp));
    }
    Harness h;
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsGround), 0);
    // The next wall arch is tagged "MUR" with a dashed line to where the player turns up the wall.
    uint64_t seed = 0;
    for (uint64_t s = 1; s < 400 && !seed; ++s) {
        const Course c = generateCourse(s, CourseType::GroundWall, 60);
        for (size_t i = 0; i < c.rings.size() && i < 4 && !seed; ++i)
            if (c.rings[i].surface != Surface::Floor) seed = s;  // a wall arch among the rings drawn at the start
    }
    CHECK(seed != 0);
    Harness g;
    g.command("rings", {"ground", encodeCourseCode(seed, CourseType::GroundWall, 60)});
    g.tick();
    ViewContext view;
    view.valid = true;
    view.camPos = {0.f, -5000.f, 500.f};
    view.camBasis = basisFromRot(rotFromYawDeg(90.f));
    DrawContext dctx{g.settings, g.game, g.profile, g.snap, true, true, 1.f};
    DrawList dl;
    g.mod.draw(view, dctx, dl);
    bool mur = false;
    for (const auto& t : dl.texts3D) mur = mur || t.text == "MUR";
    CHECK(mur);
}

// ---- Air dribble rings ----------------------------------------------------------------------------------------------
TEST(courses_air_dribble_first_ring_matches_the_start_mode) {
    using namespace courses;
    for (int dp : {0, 25, 50})
        for (uint64_t s = 1; s <= 300; ++s) {
            const uint64_t base = (s * 7919 + 3) & 0x00FFFFFFFFFFFFFFULL;
            const Course roof = generateCourse(base & ~kSeedPoppedStart, CourseType::AirDribble, dp);
            CHECK(!roof.rings.empty());
            if (roof.rings.empty()) continue;
            CHECK(roof.start.ballOnRoof);
            CHECK(length(roof.start.carVel) < 1e-9);  // roof start: car at rest
            const DVec3 rb = roof.start.carPos + roofBallOffset();
            CHECK(length(roof.start.ballPos - rb) < 1e-6);
            const double dy = roof.rings[0].center.y - rb.y, dz = roof.rings[0].center.z - rb.z;
            CHECK_MSG(dy >= 1300.0 - 1.0 && dy <= 1700.0 + 1.0, std::to_string(dy));
            CHECK_MSG(dz >= 350.0 - 1.0 && dz <= 450.0 + 1.0, std::to_string(dz));
            const Course pop = generateCourse(base | kSeedPoppedStart, CourseType::AirDribble, dp);
            CHECK(!pop.rings.empty());
            if (pop.rings.empty()) continue;
            CHECK(!pop.start.ballOnRoof);
            CHECK(length(pop.start.ballPos - (pop.start.carPos + DVec3{0, 650, 280})) < 1e-6);
            const double py = pop.rings[0].center.y - pop.start.ballPos.y, pz = pop.rings[0].center.z - pop.start.ballPos.z;
            CHECK(py >= 1100.0 - 1.0 && py <= 1500.0 + 1.0);
            CHECK(pz >= 300.0 - 1.0 && pz <= 450.0 + 1.0);
        }
    // Niveau 1: one ring 15 m ahead, 4.5 m above the roof ball; a ball carried on the roof passes under its opening.
    const Course n1 = generateCourse(5, CourseType::AirDribble, 0);
    CHECK_EQ(n1.rings.size(), 1u);
    if (!n1.rings.empty()) {
        const DVec3 rb = n1.start.ballPos;
        CHECK_NEAR(n1.rings[0].center.y - rb.y, 1500.0, 1.0);
        CHECK_NEAR(n1.rings[0].center.z - rb.z, 450.0, 1.0);
        CHECK(lowestPass(n1.rings[0], n1.objRadius) > rb.z + 40.0);
    }
    CHECK_EQ(generateCourse(5, CourseType::AirDribble, 10).rings.size(), 2u);
    // Regression: version 1 measured ring 1 from the popped ball whatever the start (about 19.6 m ahead of the car and
    // 5 m above the roof ball with the default roof start).
    double dy = 0, dz = 0;
    for (uint64_t s = 1; s <= 200; ++s) {
        const Course v1 = generateCourse(s * 7919 + 3, CourseType::AirDribble, 0, nullptr, 1);
        dy += v1.rings[0].center.y - v1.start.carPos.y;
        dz += v1.rings[0].center.z - (v1.start.carPos.z + roofBallOffset().z);
    }
    CHECK(dy / 200.0 > 1850.0 && dy / 200.0 < 2050.0);
    CHECK(dz / 200.0 > 450.0 && dz / 200.0 < 560.0);
}

TEST(courses_air_dribble_niveau_1_pop_and_push) {
    // Roof start at rest: the player waits 0.4 s, pops the ball and pushes it through the ring: success. Carrying it
    // on the roof under the ring is not a pass.
    for (int push : {1, 0}) {
        Harness h;
        h.set("rlt_co_chain", "3");
        h.keepLog = true;
        h.command("rings", {"airdribble"});
        CHECK_EQ(h.mod.course().version, 2u);
        CHECK(h.mod.course().start.ballOnRoof);
        CHECK_EQ(h.mod.course().rings.size(), 1u);
        h.runCountdown();
        CHECK(length(h.snap.car.vel) < 1e-3f);
        waitTicks(h, kReactionTicks);
        CHECK(h.mod.phase() == CoursesPhase::Running);  // a ball resting on a still roof stays there
        const auto& r = h.mod.course().rings[0];
        const Vec3 c = courses::toF(r.center), n = courses::toF(r.normal);
        if (push) {
            const Vec3 b = h.snap.ball.pos;
            h.followPath({b, b + Vec3{0.f, 300.f, 200.f}, c, c + n * 400.f}, 900.f, true);
        } else {
            const Vec3 b = h.snap.ball.pos;
            h.followPathCarry({b, Vec3{c.x, c.y + 400.f, b.z}}, 700.f, Harness::Carry::BallOnRoof);
        }
        const AttemptResult* res = lastResult(h);
        CHECK(res != nullptr);
        if (res) CHECK_EQ(res->success, push == 1);
        // Auto mode: a pop and push in a normal time moves the level up (the target time counts the pop).
        if (push) CHECK(h.mod.difficultyFor(CoursesKind::RingsAirDribble) > 0);
    }
}

// ---- Slalom ---------------------------------------------------------------------------------------------------------
TEST(courses_slalom_requires_a_weave_and_starts_at_rest) {
    using namespace courses;
    for (int dp : {0, 25, 50})
        for (uint64_t s = 1; s <= 300; ++s) {
            const Course c = generateCourse(s * 7919 + 3, CourseType::DribbleGates, dp);
            CHECK(c.rings.size() >= 5u);
            if (c.rings.size() < 2) continue;
            CHECK(length(c.start.carVel) < 1e-9);
            const double lane = c.start.carPos.x;
            const double d = dp / 100.0;
            for (const Ring& g : c.rings) CHECK(std::fabs(g.center.x - lane) - (g.radius - phys::kBallRadius) >= kSlalomWeaveMin - 1e-6);
            CHECK_NEAR(std::fabs(c.rings[0].center.y - c.start.carPos.y), kSlalomFirstGate, 1.0);
            const double vRef = c.estLength / c.parTime;
            const double spacing = std::fabs(c.rings[1].center.y - c.rings[0].center.y);
            const double weave = dlerp(kSlalomWeaveMin, kSlalomWeaveMax, d);
            CHECK(vRef * vRef * weave * (phys::kPi / spacing) * (phys::kPi / spacing) <= dlerp(300, 700, d) + 1e-6);
        }
    // Regression: version 1 asked a sideways move of only 50 to 67 uu, and started rolling at 300 uu/s.
    double need = 0;
    for (uint64_t s = 1; s <= 100; ++s) {
        const Course v1 = generateCourse(s * 7919 + 3, CourseType::DribbleGates, 0, nullptr, 1);
        double m = 0;
        for (const Ring& g : v1.rings) m = std::max(m, std::fabs(g.center.x - v1.start.carPos.x) - (g.radius - phys::kBallRadius));
        need += m;
        CHECK(length(v1.start.carVel) > 250.0);
    }
    CHECK(need / 100.0 < 80.0);
}

TEST(courses_slalom_niveau_1_weave_succeeds_and_straight_fails) {
    for (int weave : {1, 0}) {
        Harness h;
        h.set("rlt_co_chain", "3");
        h.keepLog = true;
        h.command("control", {"slalom"});
        h.runCountdown();
        CHECK(length(h.snap.car.vel) < 1e-3f);  // start at rest with the ball on the roof
        waitTicks(h, kReactionTicks);
        CHECK(h.mod.phase() == CoursesPhase::Running);
        const auto& c = h.mod.course();
        const Vec3 b = h.snap.ball.pos;
        const float dirY = c.rings[0].center.y > b.y ? 1.f : -1.f;
        std::vector<Vec3> pts{b};
        for (const auto& g : c.rings)
            pts.push_back({weave ? static_cast<float>(g.center.x) : b.x, static_cast<float>(g.center.y), b.z});
        pts.push_back(pts.back() + Vec3{0.f, dirY * 400.f, 0.f});
        const float vRef = static_cast<float>(c.estLength / c.parTime);
        h.followPathCarry(pts, vRef, Harness::Carry::BallOnRoof);
        const AttemptResult* res = lastResult(h);
        CHECK(res != nullptr);
        if (res) CHECK_EQ(res->success, weave == 1);
    }
}

// ---- Dribble sur le toit ------------------------------------------------------------------------------------------
TEST(courses_dribble_beginner_level_is_reachable_standing_still) {
    Harness h;
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::Dribble), 0);
    h.set("rlt_co_chain", "3");
    h.keepLog = true;
    h.command("control", {"dribble"});
    CHECK_NEAR(h.mod.dribbleTarget(), 5.f, 1e-6);
    CHECK_NEAR(h.mod.dribbleMinSpeed(), 0.f, 1e-6);
    h.runCountdown();
    const Vec3 roof = h.snap.ball.pos;
    waitTicks(h, 120);
    // A small hop: 50 uu above the roof for 0.4 s, within the 0.5 s of niveau 1.
    for (int i = 0; i < 48 && h.mod.phase() == CoursesPhase::Running; ++i) {
        h.snap.ball.pos = roof + Vec3{0.f, 0.f, 50.f};
        h.tick();
    }
    CHECK(h.mod.phase() == CoursesPhase::Running);
    h.snap.ball.pos = roof;
    waitTicks(h, static_cast<int>(5.2f * 120.f) - 168);
    CHECK(h.mod.phase() == CoursesPhase::Running);  // the target is reached, the run goes on for the record
    h.snap.ball.pos.z = 92.f;                        // ball dropped after 5.2 s
    h.tick();
    const AttemptResult* res = lastResult(h);
    CHECK(res && res->success);
    if (res) CHECK(!hasTip(*res, "courses.dribble_slow"));
    const Metric* held = res ? findMetric(*res, "held") : nullptr;
    CHECK(held && held->value >= 5.f);
    // Niveau 2 is the old start: 8 s at 250 uu/s on average.
    const auto p2 = CoursesModule::dribbleParams(6, 0.3f);
    CHECK_NEAR(p2.target, 8.f, 1e-4);
    CHECK_NEAR(p2.minSpeed, 250.f, 1e-3);
    CHECK_EQ(CoursesModule::levelNumber(6), 2);
}

TEST(courses_roof_start_waits_for_the_throttle_release) {
    // Dribble sur le toit, niveau 1: the ball balanced on the stopped car. The player presses the throttle to start the
    // countdown and keeps it held (the kickoff habit). GO waits until he lets go, the HUD says so, and the run starts on
    // the release tick with the ball still on the roof. Same on the next attempt (automatic countdown).
    auto hudSays = [](Harness& h, const std::string& text) {
        DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f, true};
        ViewContext view;
        view.valid = true;
        view.screenW = 1920.f;
        view.screenH = 1080.f;
        DrawList dl;
        h.mod.draw(view, dctx, dl);
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines)
                if (l.text == text) return true;
        return false;
    };
    Harness h;
    h.keepLog = true;
    h.command("control", {"dribble"});
    for (int attempt = 0; attempt < 2; ++attempt) {
        h.snap.in.throttle = 0.f;
        h.tick();
        for (int i = 0; i < 6 * 120; ++i) {
            h.snap.in.throttle = 1.f;
            const Actions a = h.tick();
            CHECK(h.mod.phase() != CoursesPhase::Running);
            CHECK(a.freezeCar);
        }
        CHECK(h.mod.ready().waitingRelease(h.snap.time));
        CHECK(hudSays(h, "Lâche l'accélérateur pour partir"));
        h.snap.in.throttle = 0.f;
        const Actions go = h.tick();
        CHECK(h.mod.phase() == CoursesPhase::Running);
        CHECK(go.setCar && go.setBall && !go.freezeCar);
        if (go.setCar && go.setBall) CHECK(go.setBall->pos.z > go.setCar->pos.z + 100.f);  // on the roof
        // The ball falls off: the attempt ends, the next one starts with the automatic countdown.
        waitTicks(h, 30);
        h.snap.ball.pos.z = 92.f;
        h.tick();
        for (int i = 0; i < 1200 && h.mod.phase() != CoursesPhase::Countdown; ++i) h.tick();
        CHECK(h.mod.phase() == CoursesPhase::Countdown);
    }
    // A start without the ball on the roof (rings on the ground) is not held back.
    Harness g;
    g.command("rings", {"ground"});
    g.snap.in.throttle = 0.f;
    g.tick();
    int go = -1;
    for (int i = 0; i < 6 * 120 && go < 0; ++i) {
        g.snap.in.throttle = 1.f;
        g.tick();
        if (g.mod.phase() == CoursesPhase::Running) go = i;
    }
    CHECK(go >= 0 && go <= static_cast<int>(3.1f * 120.f));
}

// ---- Rattrapage ---------------------------------------------------------------------------------------------------
namespace {

// Scripted catch in a small physics model: gravity and drag on the ball; a car that waits `reaction` seconds after
// GO, then drives (1500 uu/s^2, brakes at 3500, no boost) to stop with its roof under the predicted landing point;
// on the roof the ball hops back at 0.65 of the impact speed with a small forward kick, and the car keeps under it.
// Returns the attempt result; `firstImpact` gets the speed of the first impact.
const AttemptResult* scriptedCatch(Harness& h, float& firstImpact) {
    h.keepLog = true;
    h.runCountdown();
    const float dt = 1.f / 120.f;
    Vec3 bp = h.snap.ball.pos, bv = h.snap.ball.vel;
    Vec3 cv{0.f, 0.f, 0.f};
    firstImpact = 0.f;
    bool touched = false, resting = false;
    for (int tick = 0; tick < 12 * 120 && h.mod.phase() == CoursesPhase::Running; ++tick) {
        CarState& car = h.snap.car;
        const Vec3 roofRest = courses::roofSpawn(car.pos, car.basis, car.hitboxOffset, car.hitboxExtent, h.snap.ball.radius, 0.f);
        // Ball.
        if (resting) {
            bp = roofRest;
            bv = cv;
        } else {
            bv = bv * (1.f - dt * 0.03f) + Vec3{0.f, 0.f, -650.f * dt};
            bp = bp + bv * dt;
            const Vec3 local = car.basis.toLocal(bp - hitboxCenter(car));
            const bool over = std::fabs(local.x) <= car.hitboxExtent.x * 0.5f && std::fabs(local.y) <= car.hitboxExtent.y * 0.5f;
            if (over && bp.z <= roofRest.z && bv.z < 0.f) {
                const float impact = -bv.z;
                if (!touched) {
                    firstImpact = impact;
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
                bv.z = impact * courses::kCatchBounce;
                bv = bv + car.basis.fwd * (0.12f * impact);  // forward kick of the car-ball impulse
                if (bv.z < 40.f) resting = true;
            }
        }
        // Car: after the reaction, keep the hitbox centre under where the ball comes down (or under the ball).
        if (tick >= kReactionTicks) {
            Vec3 target = bp;
            Vec3 land;
            float tl = 0.f;
            if (!resting && courses::predictDownCrossing(bp, bv, roofRest.z, 4.f, land, tl)) target = land;
            const Vec3 hc = hitboxCenter(car);
            Vec3 to{target.x - hc.x, target.y - hc.y, 0.f};
            const float dist = length(to);
            const Vec3 dir = dist > 1e-3f ? to / dist : Vec3{};
            const float want = std::min(1400.f, std::sqrt(2.f * 3500.f * dist) * 0.8f);
            Vec3 dv = dir * want - cv;
            const float dvl = length(dv);
            const float maxDv = (dot(dv, cv) < 0.f ? 3500.f : 1500.f) * dt;
            if (dvl > maxDv) dv = dv * (maxDv / dvl);
            cv = cv + dv;
            car.pos = car.pos + cv * dt;
        }
        car.vel = cv;
        h.snap.ball.pos = bp;
        h.snap.ball.vel = bv;
        h.tick(false);
    }
    return lastResult(h);
}

} // namespace

TEST(courses_catch_time_limit_covers_the_hops) {
    using namespace courses;
    for (double d : {0.0, 0.25, 0.5, 1.0})
        for (int variant : {1, 2})
            for (uint64_t s = 0; s < 400; ++s) {
                const CatchPlan p = planCatch(s * 7919 + 13, d, variant, 1.f);
                const float v0 = std::sqrt(p.ballVel.z * p.ballVel.z + 2.f * 650.f * (p.ballPos.z - 148.f));
                float settle = 0.f;
                for (float v = v0 * 0.65f; v >= 228.f; v *= 0.65f) settle += 2.f * v / 650.f;
                CHECK(p.timeLimit >= p.fallTime + settle + 1.f + 0.5f);
                // Niveaux 1 to 3: the roof gets under the landing point in time with a 0.4 s reaction, no boost.
                if (d <= 0.25) {
                    const Vec3 hc = p.carPos + Vec3{std::cos(p.carYawRad), std::sin(p.carYawRad), 0.f} * 13.88f;
                    const float dist = std::hypot(p.landing.x - hc.x, p.landing.y - hc.y);
                    const float reach = 0.4f + std::sqrt(2.f * dist * (1.f / 1500.f + 1.f / 3500.f));
                    CHECK(reach <= p.fallTime - 0.15f);
                }
            }
    // Niveau 1 drop: a small toss, top at 3.5 to 4.5 m, at most two hops.
    for (uint64_t s = 0; s < 200; ++s) {
        const CatchPlan p = planCatch(s, 0.0, 1, 1.f);
        CHECK(p.apex >= 350.f - 1.f && p.apex <= 450.f + 1.f);
        CHECK(catchHopCount(p.impactSpeed) <= 2);
        CHECK(p.fallTime >= 1.4f);
    }
    // Regression: the old limit (fall + 3.0 s at d 0, + 2.6 s at the old auto start) was shorter than a passive
    // catch of a 700 uu drop (fall + hops + 1 s hold).
    const float vOld = std::sqrt(2.f * 650.f * (700.f - 148.f));
    CHECK(catchSettleTime(vOld) + 1.f > 3.0f);
}

TEST(courses_catch_scripted_player_succeeds_at_the_first_niveaux) {
    // Sky drops and wall exits at niveaux 1 to 3, several seeds each: the hops are followed and the ball held.
    float maxImpact = 0.f;
    int runs = 0;
    for (int pct : {0, 10, 25})
        for (int variant : {1, 2})
            for (int k = 0; k < 6; ++k) {
                Harness h;
                manualDifficulty(h, pct);
                h.set("rlt_co_chain", "3");
                h.set("rlt_co_catch_type", std::to_string(variant));
                h.frame += 977 * k;  // another seed
                h.snap.frame = h.frame;
                h.command("control", {"catch"});
                float impact = 0.f;
                const AttemptResult* r = scriptedCatch(h, impact);
                ++runs;
                CHECK(r != nullptr);
                if (!r) continue;
                CHECK_MSG(r->success, std::to_string(pct) + "/" + std::to_string(variant) + "/" + std::to_string(k) + " " +
                                          h.mod.lastAttempt().headline);
                CHECK(h.mod.lastAttempt().headline.find("trop tard") == std::string::npos);
                maxImpact = std::max(maxImpact, impact);
            }
    CHECK_EQ(runs, 36);
    CHECK(maxImpact > 700.f);  // the 820 -> 533 -> 346 -> 225 hops of a 6.6 m drop are covered
}

// ---- Revenir au niveau 1 ------------------------------------------------------------------------------------------
TEST(courses_reset_level_brings_an_old_level_back_to_niveau_1) {
    Store old;
    old.setFloat("courses.stair.air.level", 0.6f);
    old.setInt("courses.stair.air.attempts", 12);
    old.setFloat("courses.stair_full.air.level", 0.4f);
    old.setInt("courses.stair_full.air.attempts", 5);
    Harness h;
    h.mod.load(old);
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), 60);  // levels saved under the old rules stay
    CHECK_EQ(h.mod.fullDifficultyFor(CoursesKind::RingsAir), 40);
    const Actions a = h.command("reset_level", {"air"});
    CHECK(a.requestSave);
    CHECK(hasChat(a, "niveau 1"));
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), 0);
    CHECK_EQ(h.mod.fullDifficultyFor(CoursesKind::RingsAir), 0);
    Store st;
    h.mod.save(st);
    CHECK_NEAR(st.getFloat("courses.stair.air.level", 1.f), 0.f, 1e-6);
    CHECK(!h.command("reset_level", {"nimportequoi"}).chat.empty());
}

TEST(courses_hud_gauge_and_hints_in_plain_words) {
    auto panelText = [](Harness& h) {
        DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f, true};
        ViewContext view;
        view.valid = true;
        DrawList dl;
        h.mod.draw(view, dctx, dl);
        std::string all;
        if (!dl.panels.empty())
            for (const auto& l : dl.panels[0].lines)
                if (!l.detail) all += l.text + "\n";
        return all;
    };
    // Roof dribble: the front / back gauge says what to do.
    Harness d;
    d.command("control", {"dribble"});
    d.runCountdown();
    d.tick();
    CHECK(panelText(d).find("Balle au centre du toit") != std::string::npos);
    const Vec3 roof = d.snap.ball.pos;
    d.snap.ball.pos = roof - d.snap.car.basis.fwd * 40.f;
    d.tick();
    CHECK(panelText(d).find("Balle en arrière : relâche") != std::string::npos);
    d.snap.ball.pos = roof + d.snap.car.basis.fwd * 40.f;
    d.tick();
    CHECK(panelText(d).find("Balle en avant : accélère un peu") != std::string::npos);
    // Near a wall, rolling toward it: turn hint.
    d.snap.ball.pos = roof;
    d.snap.car.pos.y = 3800.f;
    d.snap.ball.pos.y = 3800.f + 13.88f;
    d.snap.car.vel = d.snap.car.basis.fwd * 600.f;
    d.tick();
    CHECK(panelText(d).find("Tourne doucement") != std::string::npos);
    // Catch: the first hint is to go to the circle.
    Harness c;
    c.command("control", {"catch"});
    c.runCountdown();
    c.tick();
    const std::string t = panelText(c);
    CHECK(t.find("Va dans le cercle") != std::string::npos || t.find("Arrête-toi dessous") != std::string::npos ||
          t.find("Ne saute pas") != std::string::npos);
}
