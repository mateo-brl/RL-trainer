// CoursesModule: regression tests of the review fixes (catch softness, clean slalom gates, penalty-mode success
// and records, come-back return, boost tip, idle restart origin, gate wording, line budget of the drawing).

#include "tests/TestFramework.h"

#include "core/modules/courses/CourseRun.h"
#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

bool logHasChat(const Harness& h, const std::string& needle) {
    for (const auto& a : h.log)
        if (hasChat(a, needle)) return true;
    return false;
}

bool panelHas(const DrawList& dl, const std::string& needle) {
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines)
            if (l.text.find(needle) != std::string::npos) return true;
    return false;
}

void manualDifficulty(Harness& h, int d) {
    h.set("rlt_co_difficulty_mode", "5");
    h.set("rlt_co_difficulty", std::to_string(d));
}

std::string code42(courses::CourseType t = courses::CourseType::Aerial, int d = 50) {
    return courses::encodeCourseCode(42, t, d);
}

// Catch attempt: the ball falls for half a second, first contact with the given velocities just before it and
// the ball `offsetLocal` (car-local, horizontal) from the roof centre, then it either rests on the roof for the
// hold time or drops to the floor.
const AttemptResult* catchAttempt(Harness& h, const Vec3& ballVel, const Vec3& carVel, const Vec3& offsetLocal,
                                  bool settles) {
    manualDifficulty(h, 30);
    h.keepLog = true;
    h.command("control", {"catch"});
    h.runCountdown();
    for (int i = 0; i < 60; ++i) {
        h.snap.ball.pos.z -= 5.f;
        h.tick();
    }
    TouchEvent touch;
    touch.pre = h.snap;
    touch.pre.ball.vel = ballVel;
    touch.pre.car.vel = carVel;
    touch.pre.ball.pos = hitboxCenter(h.snap.car) + h.snap.car.basis.toWorld(offsetLocal + Vec3{0.f, 0.f, 125.f});
    Actions ta;
    h.mod.onTouch(touch, h.ctx(), ta);
    if (settles) {
        for (int i = 0; i < 130 && h.mod.phase() == CoursesPhase::Running; ++i) {
            h.snap.ball.pos = courses::roofSpawn(h.snap.car.pos, h.snap.car.basis, h.snap.car.hitboxOffset,
                                                 h.snap.car.hitboxExtent, h.snap.ball.radius);
            h.tick();
        }
    } else {
        h.snap.ball.pos.z = 92.f;
        h.tick();
    }
    return lastResult(h);
}

// Path of the current course through ring centres, except the listed rings passed 150 uu outside their rim.
std::vector<Vec3> pathMissing(const Harness& h, const std::vector<size_t>& missed) {
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre()};
    for (size_t i = 0; i < rings.size(); ++i) {
        const bool miss = std::find(missed.begin(), missed.end(), i) != missed.end();
        pts.push_back(courses::toF(miss ? rings[i].center + rings[i].up * (rings[i].radius + 150.0) : rings[i].center));
    }
    const auto& last = rings.back();
    const bool lastMissed = std::find(missed.begin(), missed.end(), rings.size() - 1) != missed.end();
    pts.push_back(courses::toF(last.center + last.normal * 400.0 + (lastMissed ? last.up * (last.radius + 150.0) : courses::DVec3{})));
    return pts;
}

// Three rings on +Y at y = 1000, 2000, 3000 (z 500, radius 300), car radius 50.
courses::Course lineCourse() {
    courses::Course c;
    c.objRadius = 50;
    for (int i = 0; i < 3; ++i) c.rings.push_back(courses::makeDisk({0, 1000.0 * (i + 1), 500}, {0, 1, 0}, 300));
    c.rings.back().flags |= courses::kFlagFinal;
    c.parTime = 3.0;
    return c;
}

} // namespace

// ---- Catch: the unavoidable fall speed is not graded -----------------------------------------------------------
TEST(courses_fix_catch_realistic_fall_speed_is_soft) {
    // Car waiting still under a ball falling at a realistic 950 uu/s: a perfect catch, no "hard touch" advice.
    Harness h;
    const AttemptResult* r = catchAttempt(h, {0.f, 0.f, -950.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true);
    CHECK(r && r->success);
    CHECK(r && r->score > 85.f);
    CHECK(r && !hasTip(*r, "courses.catch_hard_touch"));
    CHECK(r && !hasTip(*r, "courses.catch_jump_into"));
    const Metric* gap = r ? findMetric(*r, "first_touch_speed_gap") : nullptr;
    CHECK(gap && gap->value < 1.f);
    CHECK(r && findMetric(*r, "first_touch_offset") && findMetric(*r, "first_touch_offset")->value < 1.f);
    // Even a very high drop (1300 uu/s) changes nothing: only what the player controls is graded.
    Harness hi;
    const AttemptResult* rh = catchAttempt(hi, {0.f, 0.f, -1300.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true);
    CHECK(rh && r && std::fabs(rh->score - r->score) < 1e-3f);
}

TEST(courses_fix_catch_speed_mismatch_and_jump_are_graded) {
    Harness soft;
    const AttemptResult* rs = catchAttempt(soft, {0.f, 0.f, -950.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true);
    // Car arriving 900 uu/s faster than the ball horizontally: the touch is hard, speed matching is advised.
    Harness fast;
    const AttemptResult* rf = catchAttempt(fast, {0.f, 0.f, -950.f}, {900.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true);
    CHECK(rf && rf->success);
    CHECK(rf && hasTip(*rf, "courses.catch_hard_touch"));
    CHECK(rf && !hasTip(*rf, "courses.catch_jump_into"));
    CHECK(rf && rs && rf->score < rs->score - 10.f);
    // Ball moving sideways at 400 uu/s and the car matching it: soft.
    Harness match;
    const AttemptResult* rm = catchAttempt(match, {400.f, 0.f, -950.f}, {380.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true);
    CHECK(rm && !hasTip(*rm, "courses.catch_hard_touch"));
    CHECK(rm && rm->score > 85.f);
    // Jumping up into the falling ball: the other tip.
    Harness jump;
    const AttemptResult* rj = catchAttempt(jump, {0.f, 0.f, -950.f}, {0.f, 0.f, 700.f}, {0.f, 0.f, 0.f}, true);
    CHECK(rj && hasTip(*rj, "courses.catch_jump_into"));
    CHECK(rj && !hasTip(*rj, "courses.catch_hard_touch"));
    CHECK(rj && findMetric(*rj, "first_touch_jump"));
}

TEST(courses_fix_catch_off_centre_fall_tip) {
    Harness edge;
    const AttemptResult* re = catchAttempt(edge, {0.f, 0.f, -950.f}, {0.f, 0.f, 0.f}, {0.f, 80.f, 0.f}, false);
    CHECK(re && !re->success);
    CHECK(re && hasTip(*re, "courses.catch_off_centre"));
    Harness centre;
    const AttemptResult* rc = catchAttempt(centre, {0.f, 0.f, -950.f}, {0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, false);
    CHECK(rc && !rc->success);
    CHECK(rc && hasTip(*rc, "courses.catch_fell"));
    CHECK(rc && !hasTip(*rc, "courses.catch_off_centre"));
}

// ---- Slalom: a straight drive is not a slalom ------------------------------------------------------------------
TEST(courses_fix_slalom_straight_line_is_not_a_success) {
    int straightOk = 0, weaveOk = 0, total = 0;
    for (uint64_t s = 1; s <= 12; ++s) {
        const std::string code = courses::encodeCourseCode(s * 7919, courses::CourseType::DribbleGates, 15);
        Harness h;
        h.set("rlt_co_chain", "3");
        h.keepLog = true;
        h.command("control", {"slalom", code});
        h.runCountdown();
        const Vec3 b = h.snap.ball.pos;
        const float dirY = h.mod.course().rings[0].center.y > b.y ? 1.f : -1.f;
        h.followPathCarry({b, b + Vec3{0.f, dirY * 9500.f, 0.f}}, 900.f, Harness::Carry::BallOnRoof);
        const AttemptResult* r = lastResult(h);
        ++total;
        CHECK(r != nullptr);
        if (r && r->success) ++straightOk;
        if (r && !r->success) {
            CHECK(r->verdict == Verdict::Fail);
            CHECK(hasTip(*r, "courses.rings_missed"));
            CHECK(h.mod.lastAttempt().headline.find("portes ratées") != std::string::npos);
            CHECK(!h.mod.personalBests().empty() && h.mod.personalBests()[0].time == 0.f);
            CHECK(!logHasChat(h, "Premier chrono"));
        }
        CHECK_EQ(h.mod.run().ringsPassed(), 0);
        CHECK_EQ(h.mod.successesOf(CoursesKind::Slalom), 0);
        // The designed path (ball weaving through the centre of each gate, on the roof) still succeeds.
        Harness w;
        w.set("rlt_co_chain", "3");
        w.keepLog = true;
        w.command("control", {"slalom", code});
        w.runCountdown();
        w.followPathCarry(w.ringPath(w.snap.ball.pos), 900.f, Harness::Carry::BallOnRoof);
        const AttemptResult* rw = lastResult(w);
        if (rw && rw->success) ++weaveOk;
    }
    CHECK_EQ(straightOk, 0);
    CHECK_EQ(weaveOk, total);
}

TEST(courses_fix_slalom_grazes_never_count_even_with_the_setting) {
    Harness h;
    CHECK(h.settings.courses.grazeCounts);  // default on
    h.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    h.runCountdown();
    const auto& g = h.mod.course().rings[0];
    // Ball centre 20 uu inside the post: the ball overlaps the post (a graze), not a pass.
    const courses::DVec3 side = courses::cross(g.up, g.normal);
    const courses::DVec3 p = g.center + side * (g.radius - 20.0);
    h.followPathCarry({h.snap.ball.pos, courses::toF(p - g.normal * 200.0), courses::toF(p + g.normal * 100.0)}, 900.f,
                      Harness::Carry::BallOnRoof);
    CHECK_EQ(h.mod.run().ringsPassed(), 0);
    CHECK_EQ(h.mod.run().misses(), 1);
    // The rings courses still honour the setting.
    Harness a;
    a.command("rings", {"air", code42()});
    a.runCountdown();
    const auto& r0 = a.mod.course().rings[0];
    const courses::DVec3 q = r0.center + r0.up * (r0.radius - 20.0);
    a.followPath({a.carCentre(), courses::toF(q - r0.normal * 200.0), courses::toF(q + r0.normal * 100.0)}, 1500.f, false);
    CHECK_EQ(a.mod.run().ringsPassed(), 1);
    CHECK_EQ(a.mod.run().grazes(), 1);
}

TEST(courses_fix_slalom_ball_must_stay_on_the_roof) {
    for (const float popSpeed : {500.f, 0.f}) {
        Harness h;
        h.keepLog = true;
        h.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
        h.runCountdown();
        // The ball rides 200 uu above the car (popped, never resting): lost after the roof tolerance.
        Vec3 ballPos = h.snap.ball.pos;
        for (int i = 0; i < 12; ++i) {  // resting on the roof first
            ballPos = ballPos + Vec3{0.f, 1.f, 0.f};
            h.ballOnRoofAt(ballPos, {0.f, 120.f, 0.f});
            h.tick();
        }
        CHECK(h.mod.phase() == CoursesPhase::Running);
        int ticks = 0;
        while (h.mod.phase() == CoursesPhase::Running && ticks < 600) {
            ballPos = ballPos + Vec3{0.f, 1.f, 0.f};
            h.ballOnRoofAt(ballPos, {0.f, 120.f, 0.f});
            h.snap.ball.pos.z += 200.f;
            h.snap.ball.vel.z = popSpeed;
            h.tick();
            ++ticks;
        }
        CHECK(h.mod.phase() == CoursesPhase::Result);
        CHECK_NEAR(static_cast<float>(ticks) / 120.f, h.settings.courses.roofTolerance, 0.03);
        CHECK(h.mod.lastAttempt().headline.find("tombée du toit") != std::string::npos);
        const AttemptResult* r = lastResult(h);
        CHECK(r && !r->success);
        // Popped up: "softer touches"; no relative motion at all: the generic roof advice.
        CHECK(r && hasTip(*r, popSpeed > 0.f ? "courses.slalom_pop" : "courses.slalom_off_roof"));
    }
}

// ---- Penalty mode: success and records need the rings ----------------------------------------------------------
TEST(courses_fix_penalty_all_missed_is_neither_success_nor_record) {
    for (const char* penalty : {"2", "0"}) {
        Harness h;
        h.set("rlt_co_chain", "3");
        h.set("rlt_co_miss_penalty", penalty);
        h.keepLog = true;
        h.command("rings", {"air", code42()});
        h.runCountdown();
        std::vector<size_t> all;
        for (size_t i = 0; i < h.mod.course().rings.size(); ++i) all.push_back(i);
        h.followPath(pathMissing(h, all), 2000.f, false);
        CHECK(h.mod.phase() == CoursesPhase::Result);
        const AttemptResult* r = lastResult(h);
        CHECK_MSG(r && !r->success, penalty);
        CHECK(r && r->verdict == Verdict::Fail);
        CHECK(r && r->score <= 40.f);
        CHECK(h.mod.lastAttempt().headline.find("trop d'anneaux ratés") != std::string::npos);
        CHECK(!h.mod.lastAttempt().newRecord);
        const auto pbs = h.mod.personalBests();
        CHECK(pbs.size() == 1u && pbs[0].time == 0.f && pbs[0].finishes == 0);
        CHECK(!logHasChat(h, "Premier chrono") && !logHasChat(h, "Record battu"));
        CHECK_EQ(h.mod.successesOf(CoursesKind::RingsAir), 0);
    }
}

TEST(courses_fix_penalty_success_needs_three_quarters_of_the_rings) {
    const int n = static_cast<int>(courses::generateCourse(42, courses::CourseType::Aerial, 50).rings.size());
    CHECK_EQ(n, 12);
    CHECK_EQ(CoursesModule::maxMissesAllowed(n), 3);
    // 3 rings missed: still a (penalised) success. 4: a failure.
    for (int missed : {3, 4}) {
        Harness h;
        h.set("rlt_co_chain", "3");
        h.keepLog = true;
        h.command("rings", {"air", code42()});
        h.runCountdown();
        std::vector<size_t> list;
        for (int i = 0; i < missed; ++i) list.push_back(static_cast<size_t>(2 * i + 1));
        h.followPath(pathMissing(h, list), 2000.f, false);
        const AttemptResult* r = lastResult(h);
        CHECK(r != nullptr);
        if (!r) continue;
        CHECK_EQ(h.mod.run().misses(), missed);
        CHECK_EQ(r->success, missed == 3);
        if (missed == 3) {
            CHECK(r->score >= 45.f && r->score < 80.f);  // scaled by the 9/12 rings passed
            CHECK(hasTip(*r, "courses.rings_missed"));
        }
    }
}

TEST(courses_fix_no_record_with_misses_and_a_tiny_penalty) {
    // One ring missed with a 0.5 s (or no) penalty: a success, but no record (it would beat every clean run).
    for (const char* penalty : {"0.5", "0"}) {
        Harness h;
        h.set("rlt_co_chain", "1");
        h.set("rlt_co_miss_penalty", penalty);
        h.keepLog = true;
        h.command("rings", {"air", code42()});
        h.runCountdown();
        h.followPath(pathMissing(h, {4}), 2000.f, false);
        const AttemptResult* r = lastResult(h);
        CHECK_MSG(r && r->success, penalty);
        CHECK_MSG(!h.mod.lastAttempt().newRecord, penalty);
        CHECK(h.mod.lastAttempt().headline.find("pas de record") != std::string::npos);
        CHECK(h.mod.personalBests().size() == 1u && h.mod.personalBests()[0].time == 0.f);
        CHECK(!logHasChat(h, "Premier chrono"));
        // A clean run then sets the first record.
        while (h.mod.phase() == CoursesPhase::Result) h.tick();
        h.log.clear();
        h.runCountdown();
        h.followPath(h.ringPath(h.carCentre()), 2000.f, false);
        CHECK(h.mod.lastAttempt().newRecord);
        CHECK(logHasChat(h, "Premier chrono"));
    }
    // With a real penalty (default 2 s), a run with one miss is a fair time and may be a record.
    Harness p;
    p.set("rlt_co_chain", "3");
    p.keepLog = true;
    p.command("rings", {"air", code42()});
    p.runCountdown();
    p.followPath(pathMissing(p, {4}), 2000.f, false);
    CHECK(p.mod.lastAttempt().success && p.mod.lastAttempt().newRecord);
    CHECK(p.mod.personalBests().size() == 1u && p.mod.personalBests()[0].time > 0.f);
}

// ---- Come back: flying back through the missed ring is not "the wrong way" ------------------------------------
TEST(courses_fix_come_back_return_is_not_wrong_way_in_course_run) {
    const courses::Course c = lineCourse();
    courses::CourseRun run;
    run.reset(&c, courses::MissPolicy::ComeBack, true, 2.0);
    CHECK(run.step({400, 990, 500}, {400, 1010, 500}, 0, 0.01) == courses::RunEvent::Missed);
    CHECK(run.awaitingComeBack());
    CHECK(run.step({0, 1010, 500}, {0, 990, 500}, 1, 1.01) == courses::RunEvent::None);  // back through it
    CHECK_EQ(run.wrongWays(), 0);
    CHECK(run.step({0, 990, 500}, {0, 1010, 500}, 2, 2.01) == courses::RunEvent::Passed);
    CHECK(!run.awaitingComeBack());
    // Afterwards a backward crossing is the wrong way again.
    CHECK(run.step({0, 2010, 500}, {0, 1990, 500}, 3, 3.01) == courses::RunEvent::WrongWay);
    CHECK_EQ(run.wrongWays(), 1);
    // After a skip too.
    courses::CourseRun skip;
    skip.reset(&c, courses::MissPolicy::ComeBack, true, 2.0);
    CHECK(skip.step({0, 1990, 500}, {0, 2010, 500}, 0, 0.01) == courses::RunEvent::Skipped);
    CHECK(skip.step({0, 1010, 500}, {0, 990, 500}, 1, 1.01) == courses::RunEvent::None);
    CHECK_EQ(skip.wrongWays(), 0);
    // Penalty and strict policies never wait for a come back.
    courses::CourseRun pen;
    pen.reset(&c, courses::MissPolicy::Penalty, true, 2.0);
    pen.step({400, 990, 500}, {400, 1010, 500}, 0, 0.01);
    CHECK(!pen.awaitingComeBack());
}

TEST(courses_fix_come_back_return_in_the_module) {
    Harness h;
    h.set("rlt_co_miss_policy", "1");
    h.set("rlt_co_chain", "3");
    h.keepLog = true;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    const auto& r0 = rings[0];
    std::vector<Vec3> pts{h.carCentre(), courses::toF(r0.center + r0.up * (r0.radius + 150.0)),
                          courses::toF(r0.center + r0.up * (r0.radius + 150.0) + r0.normal * 400.0),
                          courses::toF(r0.center + r0.normal * 400.0), courses::toF(r0.center - r0.normal * 400.0),
                          courses::toF(r0.center - r0.normal * 600.0), courses::toF(r0.center - r0.normal * 400.0)};
    for (const auto& r : rings) pts.push_back(courses::toF(r.center));
    pts.push_back(courses::toF(rings.back().center + rings.back().normal * 400.0));
    h.followPath(pts, 1500.f, false);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    CHECK_EQ(h.mod.run().wrongWays(), 0);
    CHECK_EQ(h.mod.run().misses(), 1);
    CHECK(r && !hasTip(*r, "courses.rings_wrong_way"));
    CHECK(r && hasTip(*r, "courses.rings_missed"));
}

// ---- Boost tip -------------------------------------------------------------------------------------------------
TEST(courses_fix_boost_tip_only_when_boost_was_spent) {
    // Ground course started with 0 boost and no refill: running "out" of boost is not a mistake.
    Harness g;
    g.set("rlt_co_boost_start", "0");
    g.set("rlt_co_boost_refills", "0");
    g.keepLog = true;
    g.command("rings", {"ground", courses::encodeCourseCode(7, courses::CourseType::GroundWall, 30)});
    g.runCountdown();
    CHECK(g.snap.car.boost == 0.f);
    g.followPath(g.ringPath(g.carCentre()), 1800.f, false);
    const AttemptResult* rg = lastResult(g);
    CHECK(rg && rg->success);
    CHECK(rg && !hasTip(*rg, "courses.rings_boost_out"));
    // Aerial course, real boost emptied: the gliding advice.
    Harness a;
    a.set("rlt_co_boost_start", "60");
    a.keepLog = true;
    a.command("rings", {"air", code42()});
    a.runCountdown();
    a.tick();
    a.snap.car.boost = 0.f;
    a.followPath(a.ringPath(a.carCentre()), 2000.f, false);
    const AttemptResult* ra = lastResult(a);
    CHECK(ra && hasTip(*ra, "courses.rings_boost_out"));
    bool glide = false;
    for (const auto& t : ra ? ra->tips : std::vector<Tip>{}) glide = glide || t.text.find("plané") != std::string::npos;
    CHECK(glide);
    // Ground course, boost spent: advice without gliding.
    Harness s;
    s.keepLog = true;
    s.command("rings", {"ground", courses::encodeCourseCode(7, courses::CourseType::GroundWall, 30)});
    s.runCountdown();
    s.tick();
    s.snap.car.boost = 0.f;
    s.followPath(s.ringPath(s.carCentre()), 1800.f, false);
    const AttemptResult* rs = lastResult(s);
    CHECK(rs && hasTip(*rs, "courses.rings_boost_out"));
    for (const auto& t : rs ? rs->tips : std::vector<Tip>{}) CHECK(t.text.find("plané") == std::string::npos);
    // Slalom: never.
    Harness sl;
    sl.keepLog = true;
    sl.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    sl.runCountdown();
    sl.tick();
    sl.snap.car.boost = 0.f;
    sl.followPathCarry(sl.ringPath(sl.snap.ball.pos), 900.f, Harness::Carry::BallOnRoof);
    const AttemptResult* rsl = lastResult(sl);
    CHECK(rsl && rsl->success);
    CHECK(rsl && !hasTip(*rsl, "courses.rings_boost_out"));
}

// ---- Idle restart keeps the origin of the course ---------------------------------------------------------------
TEST(courses_fix_idle_restart_keeps_generated_origin) {
    Harness h;
    h.set("rlt_co_chain", "3");
    h.command("rings", {"air"});
    const std::string code = h.mod.currentCode();
    CHECK(!h.mod.status().fromCode);
    h.command("stop");
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    const Actions a = h.command("restart");
    CHECK(hasChat(a, "même parcours"));
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    CHECK(!h.mod.status().fromCode);
    CHECK_EQ(h.mod.currentCode(), code);
    const int before = h.mod.difficultyFor(CoursesKind::RingsAir);
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 2500.f, false);
    CHECK(h.mod.lastAttempt().success);
    CHECK(h.mod.difficultyFor(CoursesKind::RingsAir) > before);  // the staircase still adapts
    // A course played from a code stays a code course.
    while (h.mod.phase() != CoursesPhase::Idle) h.tick();
    h.command("play", {code42(courses::CourseType::Aerial, 80)});
    h.command("stop");
    h.command("restart");
    CHECK(h.mod.status().fromCode);
    CHECK_EQ(h.mod.status().difficulty, 80);
    // Smart chain after an idle restart of a generated course: a new course after a success.
    Harness c;
    c.command("rings", {"air"});
    c.command("stop");
    c.command("restart");
    const std::string first = c.mod.currentCode();
    c.runCountdown();
    c.followPath(c.ringPath(c.carCentre()), 2500.f, false);
    CHECK(c.mod.lastAttempt().success);
    while (c.mod.phase() == CoursesPhase::Result) c.tick();
    CHECK(c.mod.currentCode() != first);
}

TEST(courses_fix_idle_restart_after_load_and_for_drills) {
    // Only the last code is known (restored from the save): restart plays it.
    Store st;
    {
        Harness h;
        h.command("play", {code42(courses::CourseType::GroundWall, 35)});
        h.command("stop");
        h.mod.save(st);
    }
    Harness l;
    l.mod.load(st);
    l.command("restart");
    CHECK(l.mod.phase() == CoursesPhase::Setup);
    CHECK(l.mod.kind() == CoursesKind::RingsGround);
    CHECK_EQ(l.mod.currentCode(), code42(courses::CourseType::GroundWall, 35));
    // Catch drill: the same ball again.
    Harness c;
    c.command("control", {"catch"});
    const Vec3 ball = c.mod.catchPlan().ballPos;
    c.command("stop");
    c.command("restart");
    CHECK(c.mod.kind() == CoursesKind::Catch);
    CHECK(c.mod.phase() == CoursesPhase::Setup);
    CHECK(c.mod.catchPlan().ballPos == ball);
    // Refused without the activity, like any start.
    Harness r;
    r.command("rings", {"air", code42()});
    r.command("stop");
    r.host.allow = false;
    CHECK(hasChat(r.command("restart"), "indisponible"));
    CHECK(r.mod.phase() == CoursesPhase::Idle);
}

// ---- Wording: gates are "portes" -------------------------------------------------------------------------------
TEST(courses_fix_slalom_messages_say_porte) {
    Harness h;
    h.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    h.runCountdown();
    const Vec3 b = h.snap.ball.pos;
    const float dirY = h.mod.course().rings[0].center.y > b.y ? 1.f : -1.f;
    const auto& g = h.mod.course().rings[0];
    // Pass 150 uu outside the first gate (near miss), stop right after it.
    const Vec3 out = courses::toF(g.center + courses::cross(g.up, g.normal) * (g.radius + 150.0));
    h.followPathCarry({b, Vec3{out.x, out.y - dirY * 300.f, b.z}, Vec3{out.x, out.y + dirY * 50.f, b.z}}, 900.f,
                      Harness::Carry::BallOnRoof);
    CHECK_EQ(h.mod.run().misses(), 1);
    ViewContext view;
    view.valid = true;
    view.camPos = {0.f, -5000.f, 500.f};
    view.camBasis = basisFromRot(rotFromYawDeg(90.f));
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f};
    DrawList dl;
    h.mod.draw(view, dctx, dl);
    CHECK(panelHas(dl, "Porte 1 ratée"));
    CHECK(panelHas(dl, "Porte 2/"));
    CHECK(!panelHas(dl, "Anneau"));
    // Strict mode fail reason.
    Harness s;
    s.set("rlt_co_miss_policy", "2");
    s.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    s.runCountdown();
    s.followPathCarry({s.snap.ball.pos, Vec3{out.x, out.y - dirY * 300.f, b.z}, Vec3{out.x, out.y + dirY * 50.f, b.z}}, 900.f,
                      Harness::Carry::BallOnRoof);
    CHECK(s.mod.lastAttempt().headline.find("porte ratée") != std::string::npos);
    // Goal reason in lower case after "Raté : ".
    Harness ad;
    ad.command("rings", {"airdribble", code42(courses::CourseType::AirDribble)});
    ad.runCountdown();
    ad.tick();
    ad.event(GameEventKind::Goal);
    CHECK(ad.mod.lastAttempt().headline.find("Raté : la balle est entrée dans un but") == 0);
}

// ---- Drawing: worst-case line budget ---------------------------------------------------------------------------
TEST(courses_fix_draw_worst_case_line_budget) {
    Harness h;
    h.set("rlt_co_lookahead", "8");
    h.command("rings", {"air", courses::encodeCourseCode(99, courses::CourseType::Aerial, 100)});
    CHECK_EQ(h.mod.course().rings.size(), 16u);
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    // Pass rings 0, 1 and 2 (three passed rings drawn), the next one is ring 3.
    h.followPath({h.carCentre(), courses::toF(rings[0].center), courses::toF(rings[1].center), courses::toF(rings[2].center),
                  courses::toF(rings[2].center + rings[2].normal * 60.0)},
                 2000.f, false);
    CHECK_EQ(h.mod.run().nextRing(), 3);
    // Camera above the ceiling looking straight down with the narrowest field of view: every ring is "close"
    // on screen and gets the maximum level of detail (64 segments, passed rings 24).
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    view.fovDeg = 10.f;
    view.camPos = {0.f, 0.f, 2100.f};
    view.camBasis.fwd = {0.f, 0.f, -1.f};
    view.camBasis.right = {0.f, 1.f, 0.f};
    view.camBasis.up = {1.f, 0.f, 0.f};
    view.carValid = true;
    view.carVisualPos = h.carCentre();
    view.ballValid = false;
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f};
    DrawList dl;
    h.mod.draw(view, dctx, dl);
    // 9 rings x 64 + 3 passed x 24 + arrow 3 + floor shadow 2 + guide line 5 = 658.
    CHECK(dl.lines3D.size() >= 640u);
    CHECK(dl.lines3D.size() < 700u);
}
