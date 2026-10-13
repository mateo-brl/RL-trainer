// Control drill helpers: ball on roof, roof keeper, fall side, catch plans, landing prediction.

#include "tests/TestFramework.h"

#include "core/modules/courses/ControlDrills.h"
#include "core/modules/courses/CourseGen.h"

using namespace rlt;
using namespace rlt::courses;

namespace {
const Vec3 kOctaneExtent{118.01f, 84.20f, 36.16f};
const Vec3 kOctaneOffset{13.88f, 0.f, 20.75f};
} // namespace

TEST(courses_roof_spawn_and_detection) {
    const Basis flat = basisFromRot(rotFromYawDeg(90.f));
    const Vec3 car{0.f, 0.f, 17.f};
    const Vec3 spawn = roofSpawn(car, flat, kOctaneOffset, kOctaneExtent, 91.25f);
    CHECK_NEAR(spawn.z, 17.f + 20.75f + 36.16f / 2.f + 91.25f + 2.f, 1e-3);
    CHECK_NEAR(spawn.y, 13.88f, 1e-3);  // forward = +Y for yaw 90
    CHECK_NEAR(spawn.x, 0.f, 1e-3);
    CHECK(ballOnRoof(spawn, car, flat, kOctaneOffset, kOctaneExtent, 91.25f));
    CHECK(!ballOnRoof(spawn + Vec3{0.f, 0.f, 120.f}, car, flat, kOctaneOffset, kOctaneExtent, 91.25f));  // popped
    CHECK(!ballOnRoof(spawn + Vec3{150.f, 0.f, 0.f}, car, flat, kOctaneOffset, kOctaneExtent, 91.25f));  // slid off
    CHECK(!ballOnRoof(spawn - Vec3{0.f, 0.f, 40.f}, car, flat, kOctaneOffset, kOctaneExtent, 91.25f));   // inside the car
    // On the hood (front edge of the hitbox + margin): still on the roof.
    CHECK(ballOnRoof(spawn + Vec3{0.f, 60.f, 0.f}, car, flat, kOctaneOffset, kOctaneExtent, 91.25f));
    // Nose up: the roof faces -X.
    const Basis nose = basisFromRot(Rot{16384, 0, 0});
    const Vec3 sp2 = roofSpawn({0.f, 0.f, 500.f}, nose, kOctaneOffset, kOctaneExtent, 91.25f);
    CHECK(sp2.x < -100.f);
    CHECK_NEAR(sp2.z, 500.f + 13.88f, 1e-2);
    CHECK(ballOnRoof(sp2, {0.f, 0.f, 500.f}, nose, kOctaneOffset, kOctaneExtent, 91.25f));
    // State overload checks validity.
    CarState c;
    BallState b;
    c.valid = true;
    c.pos = car;
    c.basis = flat;
    b.valid = true;
    b.pos = spawn;
    CHECK(ballOnRoof(b, c));
    b.valid = false;
    CHECK(!ballOnRoof(b, c));
    b.valid = true;
    b.pos = {0.f, 0.f, 95.f};
    CHECK(ballOnFloor(b));
    b.pos.z = 150.f;
    CHECK(!ballOnFloor(b));
}

TEST(courses_roof_keeper_tolerance_and_floor) {
    RoofKeeper k;
    k.reset(0.3f);
    float t = 0.f;
    const float dt = 1.f / 120.f;
    for (int i = 0; i < 240; ++i) {  // 2 s on the roof
        t += dt;
        k.update(true, false, t, dt);
    }
    CHECK(!k.lost());
    CHECK_NEAR(k.heldTime(), 2.f, 1e-3);
    for (int i = 0; i < 24; ++i) {  // 0.2 s pop: tolerated
        t += dt;
        k.update(false, false, t, dt);
    }
    CHECK(!k.lost());
    for (int i = 0; i < 12; ++i) {
        t += dt;
        k.update(true, false, t, dt);
    }
    CHECK(!k.lost());
    CHECK_NEAR(k.offRoofFor(), 0.f, 1e-6);
    const float heldBefore = k.heldTime();
    int ticks = 0;
    while (!k.lost() && ticks < 200) {  // off the roof for good: lost after the tolerance
        t += dt;
        k.update(false, false, t, dt);
        ++ticks;
    }
    CHECK(k.lost());
    CHECK(k.loss() == RoofKeeper::Loss::OffRoof);
    CHECK(ticks >= 36 && ticks <= 38);  // 0.3 s at 120 Hz
    CHECK_NEAR(k.heldTime(), heldBefore, 1e-6);  // the time held stops at the last roof contact
    // Floor touch loses at once.
    RoofKeeper f;
    f.reset(0.3f);
    f.update(true, false, dt, dt);
    f.update(false, true, 2 * dt, dt);
    CHECK(f.lost());
    CHECK(f.loss() == RoofKeeper::Loss::Floor);
    CHECK(f.everOnRoof());
}

TEST(courses_fall_side_classification) {
    CHECK(classifyFall({400.f, 20.f, 0.f}, {}) == FallSide::Front);
    CHECK(classifyFall({-300.f, 50.f, 0.f}, {}) == FallSide::Back);
    CHECK(classifyFall({50.f, -350.f, 0.f}, {}) == FallSide::Side);
    CHECK(classifyFall({50.f, 80.f, 500.f}, {}) == FallSide::Up);
    // Slow drift: from the position on the roof.
    CHECK(classifyFall({10.f, 5.f, 0.f}, {70.f, 10.f, 0.f}) == FallSide::Front);
    CHECK(classifyFall({10.f, 5.f, 0.f}, {-70.f, 10.f, 0.f}) == FallSide::Back);
    CHECK(classifyFall({10.f, 5.f, 0.f}, {5.f, 60.f, 0.f}) == FallSide::Side);
    CHECK(classifyFall({0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}) == FallSide::None);
}

TEST(courses_catch_plans_inside_field_and_reachable) {
    int wall = 0, drop = 0, total = 0;
    for (int variant = 0; variant <= 2; ++variant)
        for (double d : {0.0, 0.5, 1.0})
            for (uint64_t s = 0; s < 400; ++s) {
                const CatchPlan p = planCatch(s * 7919 + 13, d, variant);
                ++total;
                if (p.fromWall) ++wall;
                else ++drop;
                CHECK(std::fabs(p.ballPos.x) <= 4096.f - 91.f && std::fabs(p.ballPos.y) <= 5120.f - 91.f);
                CHECK(p.ballPos.z > 500.f && p.ballPos.z < 2044.f - 91.f);
                CHECK(std::fabs(p.landing.x) <= 4096.f - 300.f && std::fabs(p.landing.y) <= 4600.f);
                CHECK(std::fabs(p.landing.x) + std::fabs(p.landing.y) <= 8064.f - 300.f);
                CHECK(std::fabs(p.carPos.x) <= 3500.f && std::fabs(p.carPos.y) <= 4000.f);
                CHECK_NEAR(p.carPos.z, 17.f, 1e-3);
                CHECK(p.fallTime > 0.5f && p.fallTime < 3.f);
                CHECK(p.timeLimit > p.fallTime + 1.f);
                // Reachable: the landing spot is at most ~2 s of full acceleration away.
                CHECK(distance(flat(p.landing), flat(p.carPos)) < 2600.f);
                if (variant == 1) CHECK(!p.fromWall);
                if (variant == 2) CHECK(p.fromWall);
            }
    CHECK(wall > 0 && drop > 0);
    // Variant 0 mixes both (about 35 % off the wall).
    int mixWall = 0;
    for (uint64_t s = 0; s < 1000; ++s) mixWall += planCatch(s, 0.5, 0).fromWall ? 1 : 0;
    CHECK(mixWall > 250 && mixWall < 450);
    // Deterministic and harder with difficulty.
    const CatchPlan a = planCatch(99, 0.3, 1), b = planCatch(99, 0.3, 1);
    CHECK(a.ballPos == b.ballPos && a.carPos == b.carPos);
    float hEasy = 0.f, hHard = 0.f;
    for (uint64_t s = 0; s < 200; ++s) {
        hEasy += planCatch(s, 0.0, 1).ballPos.z;
        hHard += planCatch(s, 1.0, 1).ballPos.z;
    }
    CHECK(hHard > hEasy * 1.5f);
}

TEST(courses_landing_prediction) {
    Vec3 out;
    float t = 0.f;
    // Dropped from 1000 with no horizontal speed: lands straight below, t ~ sqrt(2 * 852 / 650).
    CHECK(predictDownCrossing({100.f, 200.f, 1000.f}, {0.f, 0.f, 0.f}, 148.f, 4.f, out, t));
    CHECK_NEAR(out.x, 100.f, 1e-3);
    CHECK_NEAR(out.z, 148.f, 0.5);
    CHECK_NEAR(t, std::sqrt(2.f * 852.f / 650.f), 0.03);
    // Moving sideways: lands further along x (drag shortens it slightly).
    CHECK(predictDownCrossing({0.f, 0.f, 1000.f}, {500.f, 0.f, 0.f}, 148.f, 4.f, out, t));
    CHECK(out.x > 500.f * t * 0.95f && out.x < 500.f * t);
    // Already below the plane and going down: never crosses it.
    CHECK(!predictDownCrossing({0.f, 0.f, 100.f}, {0.f, 0.f, -100.f}, 148.f, 4.f, out, t));
    // Rising ball: crosses on the way down.
    CHECK(predictDownCrossing({0.f, 0.f, 200.f}, {0.f, 0.f, 600.f}, 148.f, 4.f, out, t));
    CHECK(t > 1.5f);
}
