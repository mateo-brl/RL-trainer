// CoursesModule: record values reported to the coach (rings and slalom: finish time, lower is better, only for a
// finished record-eligible run; roof dribble: time held, higher is better; catch: none; never the score) and the
// idle shortcuts that never take the activity from another module's drill (TickContext::otherActivityRunning).

#include "tests/TestFramework.h"

#include <algorithm>
#include <cmath>

#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

std::string code42(courses::CourseType t = courses::CourseType::Aerial, int d = 50) {
    return courses::encodeCourseCode(42, t, d);
}

void manualDifficulty(Harness& h, int d) {
    h.set("rlt_co_difficulty_mode", "5");
    h.set("rlt_co_difficulty", std::to_string(d));
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
    pts.push_back(courses::toF(last.center + last.normal * 400.0 +
                               (lastMissed ? last.up * (last.radius + 150.0) : courses::DVec3{})));
    return pts;
}

// Drives the car forward at `speed` with the ball on its roof, for `seconds`.
void dribbleFor(Harness& h, float speed, float seconds) {
    const int ticks = static_cast<int>(seconds * 120.f);
    for (int i = 0; i < ticks && h.mod.phase() == CoursesPhase::Running; ++i) {
        const Vec3 fwd = h.snap.car.basis.fwd;
        h.snap.car.pos = h.snap.car.pos + fwd * (speed / 120.f);
        h.snap.car.vel = fwd * speed;
        h.snap.ball.pos = courses::roofSpawn(h.snap.car.pos, h.snap.car.basis, h.snap.car.hitboxOffset,
                                             h.snap.car.hitboxExtent, h.snap.ball.radius);
        h.snap.ball.vel = h.snap.car.vel;
        h.tick();
    }
}

// The finish time reported as the record value is the "time" metric (penalties included).
bool recordIsFinishTime(const AttemptResult& r) {
    const Metric* t = findMetric(r, "time");
    return t && std::fabs(r.valueForRecord - t->value) < 1e-4f && r.lowerIsBetter && t->value > 1.f &&
           std::fabs(r.valueForRecord - r.score) > 1e-3f;
}

} // namespace

// ---- Rings and slalom: the finish time ------------------------------------------------------------------------
TEST(courses_record_rings_value_is_the_finish_time) {
    Harness h;
    h.set("rlt_co_chain", "1");  // always the same course
    h.keepLog = true;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 2000.f, false);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    if (!r) return;
    CHECK(r->hasRecordValue);
    CHECK(recordIsFinishTime(*r));
    const float first = r->valueForRecord;
    // Same time as the personal best of the code and as the last attempt shown in the tab.
    CHECK(h.mod.personalBests().size() == 1u && std::fabs(h.mod.personalBests()[0].time - first) < 1e-3f);
    CHECK_NEAR(h.mod.lastAttempt().time, first, 1e-4);
    // A slower clean run is still eligible (the coach keeps the best), but not a personal best of the code.
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    h.log.clear();
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 1400.f, false);
    const AttemptResult* slow = lastResult(h);
    CHECK(slow && slow->success && slow->hasRecordValue && recordIsFinishTime(*slow));
    CHECK(slow && slow->valueForRecord > first);
    CHECK(!h.mod.lastAttempt().newRecord);
    CHECK(std::fabs(h.mod.personalBests()[0].time - first) < 1e-3f);
}

TEST(courses_record_failed_rings_runs_have_no_record_value) {
    // Strict policy: the first miss fails the run.
    Harness s;
    s.set("rlt_co_miss_policy", "2");
    s.keepLog = true;
    s.command("rings", {"air", code42()});
    s.runCountdown();
    s.followPath(pathMissing(s, {0}), 2000.f, false);
    const AttemptResult* rs = lastResult(s);
    CHECK(rs && !rs->success);
    CHECK(rs && !rs->hasRecordValue);
    // Penalty mode, every ring given up: reaches the end but fails.
    Harness p;
    p.keepLog = true;
    p.command("rings", {"air", code42()});
    p.runCountdown();
    std::vector<size_t> all;
    for (size_t i = 0; i < p.mod.course().rings.size(); ++i) all.push_back(i);
    p.followPath(pathMissing(p, all), 2000.f, false);
    const AttemptResult* rp = lastResult(p);
    CHECK(rp && !rp->success);
    CHECK(rp && !rp->hasRecordValue);
    // Air dribble: the ball touches the floor.
    Harness a;
    a.keepLog = true;
    a.command("rings", {"airdribble", code42(courses::CourseType::AirDribble, 30)});
    a.runCountdown();
    for (int i = 0; i < 20; ++i) a.tick();
    a.snap.ball.pos.z = 92.f;
    a.tick();
    const AttemptResult* ra = lastResult(a);
    CHECK(ra && !ra->success && ra->skill == "course.rings.airdribble");
    CHECK(ra && !ra->hasRecordValue);
}

TEST(courses_record_teleport_or_cheap_penalty_finish_has_no_record_value) {
    // A teleport during the run: finished, never a record (neither for the code nor for the coach).
    Harness t;
    t.keepLog = true;
    t.command("rings", {"air", code42()});
    t.runCountdown();
    const auto& q0 = t.mod.course().rings[0];
    t.carAt(courses::toF(q0.center - q0.normal * 200.0), {0.f, 0.f, 0.f});  // jump next to the first ring
    t.tick();
    t.followPath(t.ringPath(t.carCentre()), 2000.f, false);
    const AttemptResult* rt = lastResult(t);
    CHECK(rt && rt->success);
    CHECK(rt && !rt->hasRecordValue);
    CHECK(!t.mod.personalBests().empty() && t.mod.personalBests()[0].time == 0.f);
    // One ring given up with a 0.5 s penalty: a success, no record.
    Harness c;
    c.set("rlt_co_miss_penalty", "0.5");
    c.keepLog = true;
    c.command("rings", {"air", code42()});
    c.runCountdown();
    c.followPath(pathMissing(c, {4}), 2000.f, false);
    const AttemptResult* rc = lastResult(c);
    CHECK(rc && rc->success);
    CHECK(rc && !rc->hasRecordValue);
    // With the default 2 s penalty the same run is a fair time: the record value includes the penalty.
    Harness f;
    f.keepLog = true;
    f.command("rings", {"air", code42()});
    f.runCountdown();
    f.followPath(pathMissing(f, {4}), 2000.f, false);
    const AttemptResult* rf = lastResult(f);
    CHECK(rf && rf->success && rf->hasRecordValue && recordIsFinishTime(*rf));
    CHECK(rf && std::fabs(rf->valueForRecord - static_cast<float>(f.mod.run().finishTime())) < 1e-4f);
    CHECK_NEAR(f.mod.run().penaltyTime(), 2.0, 1e-6);
    CHECK(!f.mod.personalBests().empty() && rf &&
          std::fabs(f.mod.personalBests()[0].time - rf->valueForRecord) < 1e-3f);
}

TEST(courses_record_slalom_value_is_the_finish_time) {
    const std::string code = courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30);
    Harness h;
    h.keepLog = true;
    h.command("control", {"slalom", code});
    h.runCountdown();
    h.followPathCarry(h.ringPath(h.snap.ball.pos), 900.f, Harness::Carry::BallOnRoof);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success && r->skill == "course.control.slalom");
    CHECK(r && r->hasRecordValue && recordIsFinishTime(*r));
    CHECK(r && !h.mod.personalBests().empty() &&
          std::fabs(h.mod.personalBests()[0].time - r->valueForRecord) < 1e-3f);
    // Ball dropped on the floor: failed, no record value.
    Harness f;
    f.keepLog = true;
    f.command("control", {"slalom", code});
    f.runCountdown();
    for (int i = 0; i < 20; ++i) f.tick();
    f.snap.ball.pos.z = 92.f;
    f.tick();
    const AttemptResult* rf = lastResult(f);
    CHECK(rf && !rf->success && rf->skill == "course.control.slalom");
    CHECK(rf && !rf->hasRecordValue);
}

// ---- Roof dribble: the time held ------------------------------------------------------------------------------
TEST(courses_record_dribble_value_is_the_time_held) {
    Harness h;
    manualDifficulty(h, 50);  // target 24 s: this attempt fails, the time held is still a record value
    h.keepLog = true;
    h.command("control", {"dribble"});
    h.runCountdown();
    dribbleFor(h, 900.f, 3.f);
    h.snap.ball.pos.z = 92.f;
    h.tick();
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success);
    if (!r) return;
    const Metric* held = findMetric(*r, "held");
    CHECK(held && held->value > 2.9f && held->value < 3.1f);
    CHECK(r->hasRecordValue && !r->lowerIsBetter);
    CHECK(held && std::fabs(r->valueForRecord - held->value) < 1e-4f);
    CHECK(std::fabs(r->valueForRecord - r->score) > 1e-3f);
    CHECK(held && std::fabs(h.mod.dribbleBest() - held->value) < 1e-4f);
    // Lost under a second: neither a record value nor a module best.
    const float best = h.mod.dribbleBest();
    Harness s;
    s.keepLog = true;
    s.command("control", {"dribble"});
    s.runCountdown();
    dribbleFor(s, 600.f, 0.5f);
    s.snap.ball.pos.z = 92.f;
    s.tick();
    const AttemptResult* rs = lastResult(s);
    CHECK(rs && !rs->hasRecordValue);
    CHECK(rs && findMetric(*rs, "held") && findMetric(*rs, "held")->value < CoursesModule::kMinDribbleRecord);
    CHECK_NEAR(s.mod.dribbleBest(), 0.f, 1e-6);
    CHECK(best > CoursesModule::kMinDribbleRecord);
}

// ---- Catch: no record value -----------------------------------------------------------------------------------
TEST(courses_record_catch_has_no_record_value) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        Harness h;
        manualDifficulty(h, 30);
        h.keepLog = true;
        h.command("control", {"catch"});
        h.runCountdown();
        for (int i = 0; i < 120; ++i) {
            h.snap.ball.pos.z -= 3.f;
            h.tick();
        }
        TouchEvent touch;
        touch.pre = h.snap;
        touch.pre.ball.vel = {0.f, 0.f, -300.f};
        touch.pre.car.vel = {0.f, 0.f, 0.f};
        Actions ta;
        h.mod.onTouch(touch, h.ctx(), ta);
        if (attempt == 0) {
            for (int i = 0; i < 130 && h.mod.phase() == CoursesPhase::Running; ++i) {
                h.snap.ball.pos = courses::roofSpawn(h.snap.car.pos, h.snap.car.basis, h.snap.car.hitboxOffset,
                                                     h.snap.car.hitboxExtent, h.snap.ball.radius);
                h.tick();
            }
        } else {
            h.snap.ball.pos.z = 92.f;
            h.tick();
        }
        const AttemptResult* r = lastResult(h);
        CHECK(r && r->skill == "course.control.catch");
        CHECK(r && r->success == (attempt == 0));
        CHECK_MSG(r && !r->hasRecordValue, attempt == 0 ? "success" : "failure");
        CHECK(r && r->valueForRecord == 0.f);
        CHECK(!h.mod.lastAttempt().newRecord);
        CHECK(h.mod.lastAttempt().headline.find("record") == std::string::npos);
        if (attempt == 0) CHECK(h.mod.catchBest() > 0.f);  // still kept as the "fastest catch" stat of the tab
    }
}

// ---- Idle shortcuts never take the activity from another module ---------------------------------------------
TEST(courses_idle_shortcuts_do_not_steal_another_drill) {
    Harness h;
    h.command("rings", {"air", code42()});
    CHECK(!h.ctx().otherActivityRunning);  // our own drill
    h.command("stop");
    CHECK(!h.ctx().otherActivityRunning);
    CoursesModule other;
    h.host.requestActivity(&other, "Kickoff");
    CHECK(h.ctx().otherActivityRunning);
    const int requests = h.host.requests;
    for (const char* cmd : {"restart", "next"}) {
        const Actions a = h.command(cmd);
        CHECK_MSG(hasChat(a, "Un autre exercice est en cours"), cmd);
        CHECK_MSG(h.mod.phase() == CoursesPhase::Idle, cmd);
        CHECK_MSG(h.host.owner == &other, cmd);
        CHECK(!a.setCar && !a.setBall);
    }
    CHECK_EQ(h.host.requests, requests);
    // Choosing an exercise is on purpose: it still takes over.
    h.command("control", {"dribble"});
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    CHECK(h.host.ownsActivity(&h.mod));
    CHECK(!h.ctx().otherActivityRunning);
    // Restart and next while our own drill runs still work (no other activity).
    h.command("restart");
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    // Nobody owns the activity: the idle shortcuts start again.
    h.command("stop");
    CHECK(h.host.owner == nullptr);
    h.command("next");
    CHECK(h.mod.phase() == CoursesPhase::Setup && h.mod.kind() == CoursesKind::Dribble);
    h.command("stop");
    h.command("restart");
    CHECK(h.mod.phase() == CoursesPhase::Setup && h.mod.kind() == CoursesKind::Dribble);
}
