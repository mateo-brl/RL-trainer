// CoursesModule: control drills (roof dribble, slalom, catch), persistence of personal bests, drawing.

#include "tests/TestFramework.h"

#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

// Drives the car forward (+Y, as placed) at `speed` with the ball on its roof, for `seconds`.
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

// The ball slides off the roof with velocity `relLocal` (car-local) relative to the car, until the drill ends.
int slideOff(Harness& h, const Vec3& relLocal, int maxTicks = 600) {
    int n = 0;
    const Vec3 rel = h.snap.car.basis.toWorld(relLocal);
    while (h.mod.phase() == CoursesPhase::Running && n < maxTicks) {
        h.snap.ball.pos = h.snap.ball.pos + (h.snap.car.vel + rel) / 120.f;
        h.snap.ball.vel = h.snap.car.vel + rel;
        h.snap.car.pos = h.snap.car.pos + h.snap.car.vel / 120.f;
        h.tick();
        ++n;
    }
    return n;
}

void manualDifficulty(Harness& h, int d) {
    h.set("rlt_co_difficulty_mode", "5");
    h.set("rlt_co_difficulty", std::to_string(d));
}

} // namespace

TEST(courses_dribble_success_and_record) {
    Harness h;
    manualDifficulty(h, 0);  // target 8 s, min average 250 uu/s
    h.keepLog = true;
    const Actions start = h.command("control", {"dribble"});
    CHECK(hasChat(start, "Dribble sur le toit"));
    CHECK_EQ(h.host.name, std::string("Contrôle"));
    const Actions first = h.tick();
    CHECK(first.setBall && first.setCar);
    if (first.setBall && first.setCar) CHECK(first.setBall->pos.z > first.setCar->pos.z + 120.f);  // ball on the roof
    h.runCountdown();
    dribbleFor(h, 700.f, 9.f);
    CHECK(h.mod.phase() == CoursesPhase::Running);
    slideOff(h, {-400.f, 0.f, 0.f});  // ball slides back: lost after the tolerance
    const AttemptResult* r = lastResult(h);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->skill, std::string("course.control.dribble"));
    CHECK(r->success);
    const Metric* held = findMetric(*r, "held");
    CHECK(held && held->value > 8.9f && held->value < 9.2f);
    CHECK(r->hasRecordValue && !r->lowerIsBetter);
    CHECK_NEAR(h.mod.dribbleBest(), held ? held->value : 0.f, 1e-4);
    CHECK_EQ(h.mod.successesOf(CoursesKind::Dribble), 1);
}

TEST(courses_dribble_failures_give_the_right_tip) {
    struct Case {
        Vec3 rel;
        const char* tip;
    };
    const Case cases[] = {{{400.f, 0.f, 0.f}, "courses.dribble_front"},
                          {{-400.f, 0.f, 0.f}, "courses.dribble_back"},
                          {{0.f, 400.f, 0.f}, "courses.dribble_side"},
                          {{0.f, 0.f, 600.f}, "courses.dribble_pop"}};
    for (const Case& c : cases) {
        Harness h;
        manualDifficulty(h, 50);  // target 24 s
        h.keepLog = true;
        h.command("control", {"dribble"});
        h.runCountdown();
        dribbleFor(h, 900.f, 3.f);
        slideOff(h, c.rel);
        const AttemptResult* r = lastResult(h);
        CHECK(r != nullptr);
        if (!r) continue;
        CHECK(!r->success);
        CHECK(r->score <= 65.f);
        CHECK_MSG(hasTip(*r, c.tip), c.tip);
        CHECK(h.mod.lastAttempt().headline.find("Balle perdue") == 0);
    }
}

TEST(courses_dribble_floor_and_standing_still) {
    // Ball on the floor: lost at once.
    Harness h;
    manualDifficulty(h, 0);
    h.keepLog = true;
    h.command("control", {"dribble"});
    h.runCountdown();
    dribbleFor(h, 600.f, 1.f);
    h.snap.ball.pos.z = 92.f;
    h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Result);
    // Standing still for the whole target: not a success (minimum average speed).
    Harness s;
    manualDifficulty(s, 40);  // target ~20.8 s, min ~550 uu/s
    s.keepLog = true;
    s.command("control", {"dribble"});
    s.runCountdown();
    dribbleFor(s, 0.f, 22.f);
    slideOff(s, {-400.f, 0.f, 0.f});
    const AttemptResult* r = lastResult(s);
    CHECK(r && !r->success);
    CHECK(r && hasTip(*r, "courses.dribble_slow"));
}

TEST(courses_catch_success_and_failures) {
    Harness h;
    manualDifficulty(h, 30);
    h.keepLog = true;
    h.command("control", {"catch"});
    CHECK(h.mod.kind() == CoursesKind::Catch);
    const courses::CatchPlan plan = h.mod.catchPlan();
    const Actions first = h.tick();
    CHECK(first.setBall && first.freezeCar);
    if (first.setBall) {
        CHECK(first.setBall->pos == plan.ballPos);
        CHECK_NEAR(length(first.setBall->vel), 0.f, 1e-4);  // held in place during the countdown
    }
    h.runCountdown();
    CHECK(h.log.back().setBall && h.log.back().setBall->vel == plan.ballVel);  // released with its velocity
    // The ball falls for 1 s, a soft touch, then sits on the roof for the hold time.
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
    for (int i = 0; i < 130 && h.mod.phase() == CoursesPhase::Running; ++i) {
        h.snap.ball.pos = courses::roofSpawn(h.snap.car.pos, h.snap.car.basis, h.snap.car.hitboxOffset,
                                             h.snap.car.hitboxExtent, h.snap.ball.radius);
        h.tick();
    }
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    CHECK(r && r->skill == "course.control.catch");
    CHECK(r && findMetric(*r, "settle_time") && std::fabs(findMetric(*r, "settle_time")->value - 1.0f) < 0.05f);
    CHECK(r && findMetric(*r, "touches") && findMetric(*r, "touches")->value == 1.f);
    CHECK(r && r->score > 70.f);
    CHECK(h.mod.catchBest() > 0.f);

    // Ball dropped on the floor without any touch: fail, "go under it earlier".
    Harness f;
    f.keepLog = true;
    f.command("control", {"catch"});
    f.runCountdown();
    for (int i = 0; i < 30; ++i) f.tick();
    f.snap.ball.pos.z = 92.f;
    f.tick();
    const AttemptResult* fr = lastResult(f);
    CHECK(fr && !fr->success && hasTip(*fr, "courses.catch_no_touch"));

    // Too late: the ball is never on the floor nor on the roof (e.g. stuck on top of the goal).
    Harness l;
    l.keepLog = true;
    l.command("control", {"catch"});
    l.runCountdown();
    l.snap.ball.pos = {0.f, 0.f, 800.f};
    int ticks = 0;
    while (l.mod.phase() == CoursesPhase::Running && ticks < 5000) {
        l.tick();
        ++ticks;
    }
    CHECK_NEAR(static_cast<float>(ticks) / 120.f, l.mod.catchPlan().timeLimit, 0.05);
    CHECK(l.mod.lastAttempt().headline.find("trop tard") != std::string::npos);
}

TEST(courses_catch_new_spawn_each_attempt) {
    Harness h;
    h.command("control", {"catch"});
    const Vec3 a = h.mod.catchPlan().ballPos;
    h.runCountdown();
    h.snap.ball.pos.z = 92.f;
    h.tick();
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    CHECK(!(h.mod.catchPlan().ballPos == a));
}

TEST(courses_slalom_course_with_ball) {
    Harness h;
    h.keepLog = true;
    h.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    CHECK(h.mod.kind() == CoursesKind::Slalom);
    CHECK(!h.mod.course().rings.empty());
    h.runCountdown();
    CHECK(courses::ballOnRoof(h.snap.ball, h.snap.car));  // released with the ball on the roof
    h.followPathCarry(h.ringPath(h.snap.ball.pos), 900.f, Harness::Carry::BallOnRoof);
    const AttemptResult* r = lastResult(h);
    CHECK(r && r->success);
    CHECK(r && r->skill == "course.control.slalom");
    CHECK(r && findMetric(*r, "rings") && findMetric(*r, "rings")->label == "Portes passées");
    CHECK_EQ(h.mod.personalBests().size(), 1u);
}

TEST(courses_save_load_roundtrip) {
    Harness h;
    h.set("rlt_co_chain", "3");
    h.command("rings", {"air", courses::encodeCourseCode(42, courses::CourseType::Aerial, 50)});
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 2200.f, false);
    while (h.mod.phase() != CoursesPhase::Idle) h.tick();
    manualDifficulty(h, 0);
    h.command("control", {"dribble"});
    h.runCountdown();
    dribbleFor(h, 700.f, 5.f);
    slideOff(h, {-400.f, 0.f, 0.f});
    Store st;
    h.mod.save(st);
    CHECK(st.has("courses.pb.count"));
    for (const auto& k : st.raw()) CHECK_MSG(k.first.rfind("courses.", 0) == 0, k.first);

    CoursesModule m2;
    m2.load(st);
    const auto a = h.mod.personalBests(), b = m2.personalBests();
    CHECK_EQ(a.size(), b.size());
    CHECK_EQ(b.size(), 1u);
    if (!a.empty() && !b.empty()) {
        CHECK_EQ(a[0].code, b[0].code);
        CHECK_NEAR(a[0].time, b[0].time, 1e-3);
        CHECK_EQ(a[0].rings, b[0].rings);
        CHECK_EQ(a[0].attempts, b[0].attempts);
        CHECK_EQ(b[0].date, std::string("2026-10-13"));
    }
    CHECK_NEAR(m2.dribbleBest(), h.mod.dribbleBest(), 1e-3);
    CHECK_EQ(m2.attemptsOf(CoursesKind::RingsAir), 1);  // played from a code: counted, not adapted
    CHECK_EQ(m2.levels()[0].level, 25);
    CHECK_EQ(m2.attemptsOf(CoursesKind::Dribble), 1);
    const auto la = h.mod.levels(), lb = m2.levels();
    CHECK_EQ(la.size(), lb.size());
    for (size_t i = 0; i < la.size() && i < lb.size(); ++i) CHECK_EQ(la[i].level, lb[i].level);
    CHECK_EQ(m2.currentCode(), h.mod.currentCode());
    // Saving again gives the same content (stable format).
    Store st2;
    m2.save(st2);
    CHECK_EQ(st2.getString("courses.pb.0"), st.getString("courses.pb.0"));
}

TEST(courses_personal_bests_bounded_and_validated) {
    Store st;
    std::vector<std::string> list;
    for (int i = 1; i <= 30; ++i) {
        const std::string code = courses::encodeCourseCode(static_cast<uint64_t>(i) * 1000, courses::CourseType::Aerial, 40);
        list.push_back(code + "|" + std::to_string(20 + i) + ".5|3|1|2026-10-0" + std::to_string(i % 9 + 1) + "|" +
                       std::to_string(i) + "|1.0;2.0;3.5");
    }
    list.push_back("RLT1-A040-garbage|12|1|1|x|99|");            // invalid code
    list.push_back(courses::encodeCourseCode(5, courses::CourseType::Aerial, 1) + "|nan|1|1|x|99|");  // bad time
    list.push_back("too|short");
    st.setList("courses.pb", list);
    st.setInt("courses.seq", 30);
    CoursesModule m;
    m.load(st);
    const auto pbs = m.personalBests();
    CHECK_EQ(pbs.size(), static_cast<size_t>(CoursesModule::kMaxPbs));
    // The 20 most recently used are kept, newest first.
    if (!pbs.empty()) {
        CHECK_EQ(pbs.front().code, courses::encodeCourseCode(30000, courses::CourseType::Aerial, 40));
        CHECK_EQ(pbs.back().code, courses::encodeCourseCode(11000, courses::CourseType::Aerial, 40));
        CHECK_NEAR(pbs.front().time, 50.5f, 1e-3);
        CHECK_EQ(pbs.front().rings, 3);
        CHECK_EQ(pbs.front().typeLabel, std::string("Anneaux aériens"));
        CHECK_EQ(pbs.front().difficulty, 40);
    }
    // Loading garbage everywhere never crashes and keeps sane values.
    Store junk;
    junk.setString("courses.pb.count", "99999999");
    junk.setString("courses.dribble.best", "-5");
    junk.setString("courses.stair.air.level", "7");
    CoursesModule j;
    j.load(junk);
    CHECK(j.personalBests().empty());
    CHECK_NEAR(j.dribbleBest(), 0.f, 1e-6);
    CHECK(j.levels()[0].level <= 100);
}

TEST(courses_draw_sanity) {
    Harness h;
    h.set("rlt_co_lookahead", "8");
    h.command("rings", {"air", courses::encodeCourseCode(99, courses::CourseType::Aerial, 100)});
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    view.fovDeg = 110.f;
    view.camPos = {0.f, -5000.f, 300.f};
    view.camBasis = basisFromRot(rotFromYawDeg(90.f));
    view.carValid = true;
    view.carVisualPos = {0.f, -4000.f, 17.f};
    view.ballValid = true;
    view.ballVisualPos = {0.f, -4800.f, 300.f};  // right in front of the camera: occludes what is behind
    view.ballRadius = 91.25f;
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f};
    // Idle: nothing at all.
    {
        Harness idle;
        DrawList dl;
        DrawContext ic{idle.settings, idle.game, idle.profile, idle.snap, true, false, 1.f};
        idle.mod.draw(view, ic, dl);
        CHECK(dl.empty());
    }
    h.tick();  // countdown
    DrawList dl;
    h.mod.draw(view, dctx, dl);
    CHECK(!dl.lines3D.empty());
    CHECK(dl.lines3D.size() < 1500u);
    CHECK_EQ(dl.panels.size(), 1u);
    if (!dl.panels.empty()) {
        CHECK_NEAR(dl.panels[0].pos.x, 0.01f * 1920.f, 1e-3);
        CHECK_NEAR(dl.panels[0].pos.y, 0.12f * 1080.f, 1e-3);
        bool countdown = false, code = false;
        for (const auto& l : dl.panels[0].lines) {
            countdown = countdown || l.text.find("Départ dans") != std::string::npos;
            code = code || l.text.find("RLT1-A100") != std::string::npos;
        }
        CHECK(countdown && code);
    }
    bool occluded = false;
    for (const auto& l : dl.lines3D) occluded = occluded || l.occluder.enabled;
    CHECK(occluded);
    bool numbered = false;
    for (const auto& t : dl.texts3D) numbered = numbered || t.text == "1/16";
    CHECK(numbered);
    // Running: guide line and the time line.
    h.runCountdown();
    h.tick();
    DrawList run;
    h.mod.draw(view, dctx, run);
    CHECK(run.lines3D.size() < 1500u);
    bool timeLine = false;
    for (const auto& l : run.panels[0].lines) timeLine = timeLine || l.text.find("Temps ") == 0;
    CHECK(timeLine);
    // HUD off and a camera looking away: no panel, nothing behind the camera.
    h.set("rlt_co_hud", "0");
    ViewContext away = view;
    away.camPos = {0.f, 5000.f, 300.f};
    away.camBasis = basisFromRot(rotFromYawDeg(90.f));  // looking at the back wall, the course behind
    DrawList none;
    h.mod.draw(away, dctx, none);
    CHECK(none.panels.empty());
    CHECK(none.lines3D.size() < run.lines3D.size());
    // Catch marker during the countdown.
    Harness c;
    c.command("control", {"catch"});
    c.tick();
    DrawList cm;
    DrawContext cc{c.settings, c.game, c.profile, c.snap, true, true, 1.f};
    c.mod.draw(view, cc, cm);
    CHECK(cm.lines3D.size() >= 24u);
}
