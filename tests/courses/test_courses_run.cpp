// CourseRun: ordered rings, sub-tick splits, miss policies (penalty / come back / strict), skips, wrong way.

#include "tests/TestFramework.h"

#include <cmath>

#include "core/common/Adaptive.h"
#include "core/common/Rng.h"
#include "core/modules/courses/CourseRun.h"

using namespace rlt;
using namespace rlt::courses;

namespace {

// Three rings on the +Y axis at y = 1000, 2000, 3000, z = 500, radius 300.
Course lineCourse() {
    Course c;
    c.objRadius = 50;
    for (int i = 0; i < 3; ++i) c.rings.push_back(makeDisk({0, 1000.0 * (i + 1), 500}, {0, 1, 0}, 300));
    c.rings.back().flags |= kFlagFinal;
    c.parTime = 3.0;
    return c;
}

// Straight flight along +Y at x offset `x`, sampled at 120 Hz; returns the last event.
RunEvent fly(CourseRun& run, double x, double fromY, double toY, double v, double& t, std::vector<RunEvent>* events = nullptr) {
    const double dt = phys::kTickDt;
    DVec3 p{x, fromY, 500};
    RunEvent last = RunEvent::None;
    while (p.y < toY && run.active()) {
        const DVec3 q = p + DVec3{0, v * dt, 0};
        const RunEvent e = run.step(p, q, t, t + dt);
        if (e != RunEvent::None) {
            last = e;
            if (events) events->push_back(e);
        }
        p = q;
        t += dt;
    }
    return last;
}

} // namespace

TEST(courses_run_sub_tick_splits) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Penalty, true, 2.0);
    double t = 0;
    std::vector<RunEvent> ev;
    const RunEvent last = fly(run, 0, 0, 3500, 1500, t, &ev);
    CHECK(run.finished());
    CHECK(last == RunEvent::Finished);
    CHECK_EQ(ev.size(), 3u);
    CHECK_EQ(run.splits().size(), 3u);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(run.splits()[static_cast<size_t>(i)].time, 1000.0 * (i + 1) / 1500.0, 1e-6);
    CHECK_NEAR(run.finishTime(), 2.0, 1e-6);
    CHECK_EQ(run.misses(), 0);
    CHECK_EQ(run.ringsPassed(), 3);
    CHECK_NEAR(run.penaltyTime(), 0.0, 1e-12);
    // No more events after the finish.
    CHECK(run.step({0, 3500, 500}, {0, 3510, 500}, 3, 3.01) == RunEvent::None);
}

TEST(courses_run_backward_never_counts) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Penalty, true, 2.0);
    CHECK(run.step({0, 1010, 500}, {0, 990, 500}, 0, 0.01) == RunEvent::WrongWay);
    CHECK_EQ(run.nextRing(), 0);
    CHECK_EQ(run.wrongWays(), 1);
    CHECK_EQ(run.misses(), 0);
}

TEST(courses_run_penalty_policy_gives_up_and_goes_on) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Penalty, true, 2.0);
    double t = 0;
    // Ring 0 missed by 400 uu (near miss), then back on the axis for rings 1 and 2.
    CHECK(fly(run, 400, 0, 1500, 1500, t) == RunEvent::Missed);
    CHECK_EQ(run.nextRing(), 1);
    CHECK(run.ringMissed(0));
    CHECK_NEAR(run.penaltyTime(), 2.0, 1e-12);
    fly(run, 0, 1500, 3500, 1500, t);
    CHECK(run.finished());
    CHECK_EQ(run.misses(), 1);
    CHECK_EQ(run.ringsPassed(), 2);
    // Final time = crossing time of ring 2 (2.0 s) + one penalty.
    CHECK_NEAR(run.finishTime(), 2.0 + 2.0, 1e-6);
    CHECK(run.splits()[0].missed && !run.splits()[1].missed);
}

TEST(courses_run_far_plane_crossing_is_not_a_miss) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Strict, true, 2.0);
    double t = 0;
    // Crossing the plane of ring 0 two kilometres to the side is not an attempt at the ring.
    CHECK(fly(run, 2000, 0, 1500, 1500, t) == RunEvent::None);
    CHECK(run.active());
    CHECK_EQ(run.misses(), 0);
}

TEST(courses_run_come_back_policy) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::ComeBack, true, 2.0);
    double t = 0;
    CHECK(fly(run, 400, 0, 1500, 1500, t) == RunEvent::Missed);
    CHECK_EQ(run.nextRing(), 0);  // still waiting for ring 0
    CHECK_EQ(run.misses(), 1);
    CHECK_NEAR(run.penaltyTime(), 0.0, 1e-12);
    // Skipping ring 0 and going through ring 1: counted as a skip, still waiting for ring 0.
    CourseRun skip;
    skip.reset(&c, MissPolicy::ComeBack, true, 2.0);
    CHECK(skip.step({0, 1990, 500}, {0, 2010, 500}, 0, 0.01) == RunEvent::Skipped);
    CHECK_EQ(skip.nextRing(), 0);
    // Then the player comes back through ring 0: passes.
    CHECK(skip.step({0, 990, 500}, {0, 1010, 500}, 1, 1.01) == RunEvent::Passed);
    CHECK_EQ(skip.nextRing(), 1);
}

TEST(courses_run_strict_policy_fails) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Strict, true, 2.0);
    CHECK(run.step({700, 990, 500}, {700, 1010, 500}, 0, 0.01) == RunEvent::Failed);  // 400 beyond the rim
    CHECK(run.failed());
    CHECK(!run.active());
    CourseRun skip;
    skip.reset(&c, MissPolicy::Strict, true, 2.0);
    CHECK(skip.step({0, 1990, 500}, {0, 2010, 500}, 0, 0.01) == RunEvent::Failed);
}

TEST(courses_run_penalty_skip_advances_two_rings) {
    const Course c = lineCourse();
    CourseRun run;
    run.reset(&c, MissPolicy::Penalty, true, 3.0);
    CHECK(run.step({0, 1990, 500}, {0, 2010, 500}, 1.0, 1.0 + 1.0 / 120.0) == RunEvent::Skipped);
    CHECK_EQ(run.nextRing(), 2);
    CHECK(run.ringMissed(0));
    CHECK(!run.ringMissed(1));
    CHECK_NEAR(run.penaltyTime(), 3.0, 1e-12);
    CHECK_EQ(run.misses(), 1);
    CHECK_NEAR(run.splits()[1].time, 1.0 + 0.5 / 120.0 + 3.0, 1e-6);
}

TEST(courses_run_graze_setting) {
    const Course c = lineCourse();
    // rho 280 with radius 50: Grazed. Counted when the setting is on...
    CourseRun on;
    on.reset(&c, MissPolicy::Penalty, true, 2.0);
    CHECK(on.step({280, 990, 500}, {280, 1010, 500}, 0, 0.01) == RunEvent::Passed);
    CHECK_EQ(on.grazes(), 1);
    // ...a miss when it is off.
    CourseRun off;
    off.reset(&c, MissPolicy::Penalty, false, 2.0);
    CHECK(off.step({280, 990, 500}, {280, 1010, 500}, 0, 0.01) == RunEvent::Missed);
    CHECK_EQ(off.misses(), 1);
}

TEST(courses_run_hermite_on_long_gap) {
    // Ring on a ballistic arc; samples one second apart: the arc (with velocities) passes, the chord misses.
    Course c;
    c.objRadius = 0;
    c.rings.push_back(makeDisk({0, 1000, 618.75}, {0, 1, 0}, 50));
    const DVec3 p0{0, 0, 200}, v0{0, 2000, 1000};
    const DVec3 p1 = p0 + v0 + DVec3{0, 0, -0.5 * phys::kGravity}, v1 = v0 + DVec3{0, 0, -phys::kGravity};
    CourseRun withVel;
    withVel.reset(&c, MissPolicy::Strict, true, 2.0);
    CHECK(withVel.step(p0, p1, 0, 1, &v0, &v1) == RunEvent::Finished);
    CHECK_NEAR(withVel.finishTime(), 0.5, 1e-9);
    CourseRun chord;
    chord.reset(&c, MissPolicy::Strict, true, 2.0);
    CHECK(chord.step(p0, p1, 0, 1) == RunEvent::Failed);
}

TEST(courses_run_empty_or_null_course) {
    CourseRun run;
    run.reset(nullptr, MissPolicy::Penalty, true, 2.0);
    CHECK(!run.active());
    CHECK(run.step({0, 0, 0}, {0, 10, 0}, 0, 0.1) == RunEvent::None);
    Course empty;
    run.reset(&empty, MissPolicy::Penalty, true, 2.0);
    CHECK(run.step({0, 0, 0}, {0, 10, 0}, 0, 0.1) == RunEvent::None);
    CHECK_EQ(run.ringCount(), 0);
}

TEST(courses_staircase_converges_to_target_success_rate) {
    // The module uses Staircase(0.75, 0.04, 0.25) per exercise. Simulated player:
    // P(success | d) = 1 / (1 + exp((d - 0.6) / 0.08)), whose 75 % point is d75 = 0.6 - 0.08 ln 3 = 0.512.
    Staircase sc(0.75f, 0.04f, 0.25f);
    Rng rng(2024, 7);
    double sumD = 0.0;
    int succ = 0, n = 0;
    for (int i = 0; i < 8000; ++i) {
        const double p = 1.0 / (1.0 + std::exp((static_cast<double>(sc.level()) - 0.6) / 0.08));
        const bool ok = rngUniform01(rng) < p;
        if (i >= 2000) {
            sumD += sc.level();
            succ += ok ? 1 : 0;
            ++n;
        }
        sc.record(ok);
    }
    const double meanD = sumD / n, rate = static_cast<double>(succ) / n;
    CHECK_NEAR(meanD, 0.6 - 0.08 * std::log(3.0), 0.05);
    CHECK(rate > 0.70 && rate < 0.80);
}
