// CoursesModule: the compact HUD panel (at most 3 visible lines, chrono and record split kept, help and stats as
// detail lines, no milliseconds or degrees in what stays visible).

#include "tests/TestFramework.h"

#include "core/common/HudLayout.h"
#include "core/modules/courses/CourseRun.h"
#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

std::string code42(courses::CourseType t = courses::CourseType::Aerial, int d = 50) {
    return courses::encodeCourseCode(42, t, d);
}

ViewContext hudView() {
    ViewContext view;
    view.valid = true;
    view.screenW = 1920.f;
    view.screenH = 1080.f;
    view.camPos = {0.f, -5000.f, 500.f};
    view.camBasis = basisFromRot(rotFromYawDeg(90.f));
    return view;
}

// The panel as the compact HUD shows it (detail lines dropped by the engine).
DrawPanel compactPanel(Harness& h) {
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f, true};
    DrawList dl;
    h.mod.draw(hudView(), dctx, dl);
    CHECK_EQ(dl.panels.size(), 1u);
    if (dl.panels.empty()) return DrawPanel{};
    DrawPanel p = dl.panels[0];
    styleHudPanel(p, true, 1.f, 0.85f);
    return p;
}

bool has(const DrawPanel& p, const std::string& needle) {
    for (const auto& l : p.lines)
        if (l.text.find(needle) != std::string::npos) return true;
    return false;
}

// At most 3 lines, no technical units.
void checkCompact(const DrawPanel& p) {
    CHECK(!p.lines.empty());
    CHECK(p.lines.size() <= 3u);
    for (const auto& l : p.lines) {
        CHECK(l.text.find(" ms") == std::string::npos);
        CHECK(l.text.find("°") == std::string::npos);
        CHECK(l.text.find(" deg") == std::string::npos);
    }
}

} // namespace

TEST(courses_hud_compact_countdown_keeps_start_goal_and_tip) {
    Harness h;
    h.command("rings", {"air", code42()});
    h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Setup || h.mod.phase() == CoursesPhase::Countdown);
    DrawPanel p = compactPanel(h);
    checkCompact(p);
    // First attempt: the HUD asks for the throttle, says the goal and the first thing to do.
    CHECK(has(p, "Appuie sur accélérer quand tu es prêt"));
    CHECK(has(p, "anneaux dans l'ordre"));
    CHECK(has(p, "Saute et lève le nez"));
    CHECK(!has(p, "Code "));  // detail: the code is in the tab
    h.snap.in.throttle = 1.f;
    h.tick();
    p = compactPanel(h);
    checkCompact(p);
    CHECK(has(p, "Prêt ?"));  // the game counts 3, 2, 1: no second countdown in the panel
    CHECK(!has(p, "Départ dans"));
    // Ground arches: the ball cam tip.
    Harness g;
    g.command("rings", {"ground", courses::encodeCourseCode(42, courses::CourseType::GroundWall, 0)});
    g.tick();
    p = compactPanel(g);
    checkCompact(p);
    CHECK(has(p, "caméra balle"));
    // The full HUD still has everything.
    DrawContext full{h.settings, h.game, h.profile, h.snap, true, true, 1.f, false};
    DrawList dl;
    h.mod.draw(hudView(), full, dl);
    bool code = false;
    for (const auto& l : dl.panels[0].lines) code = code || l.text.find("Code ") != std::string::npos;
    CHECK(code);
}

TEST(courses_hud_compact_running_keeps_chrono_and_record_split) {
    Harness h;
    h.set("rlt_co_chain", "1");
    h.command("rings", {"air", code42()});
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 2000.f, false);
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    // Second run, slower: stop right after ring 1.
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre(), courses::toF(rings[0].center), courses::toF(rings[1].center),
                          courses::toF(rings[1].center + rings[1].normal * 50.0)};
    h.followPath(pts, 1500.f, false);
    CHECK(h.mod.phase() == CoursesPhase::Running);
    const DrawPanel p = compactPanel(h);
    checkCompact(p);
    CHECK(p.lines.size() == 3u);
    CHECK(p.lines[0].text.find("Temps ") == 0);
    CHECK(has(p, "Écart record +"));
    CHECK(!has(p, "Recommencer"));  // help line: detail
    CHECK(!has(p, "Boost "));       // the game shows the boost gauge
}

TEST(courses_hud_compact_flash_replaces_the_ring_count) {
    Harness h;
    h.command("control", {"slalom", courses::encodeCourseCode(11, courses::CourseType::DribbleGates, 30)});
    h.runCountdown();
    const Vec3 b = h.snap.ball.pos;
    const float dirY = h.mod.course().rings[0].center.y > b.y ? 1.f : -1.f;
    const auto& g = h.mod.course().rings[0];
    const Vec3 out = courses::toF(g.center + courses::cross(g.up, g.normal) * (g.radius + 150.0));
    h.followPathCarry({b, Vec3{out.x, out.y - dirY * 300.f, b.z}, Vec3{out.x, out.y + dirY * 50.f, b.z}}, 900.f,
                      Harness::Carry::BallOnRoof);
    CHECK_EQ(h.mod.run().misses(), 1);
    const DrawPanel p = compactPanel(h);
    checkCompact(p);
    CHECK(p.lines[0].text.find("Temps ") == 0);
    CHECK(has(p, "Porte 1 ratée"));
    CHECK(!has(p, "Ratés : "));  // the count is a detail, the flash says it
}

TEST(courses_hud_compact_result_shows_verdict_score_and_tip) {
    Harness h;
    h.set("rlt_co_chain", "1");
    h.command("rings", {"air", code42()});
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 2000.f, false);
    CHECK(h.mod.phase() == CoursesPhase::Result);
    const DrawPanel p = compactPanel(h);
    checkCompact(p);
    CHECK(p.lines[0].text.find("Terminé en") == 0);
    CHECK(p.lines.size() >= 2u);
    if (p.lines.size() >= 2u) {
        const std::string& v = p.lines[1].text;
        CHECK(v.find(" : ") != std::string::npos && v.find("/100") != std::string::npos);
    }
    CHECK(!has(p, "Suite dans"));
    CHECK(!has(p, "Recommencer"));
}

TEST(courses_hud_compact_control_drills) {
    // Roof dribble: held time first, then the speed target.
    Harness d;
    d.command("control", {"dribble"});
    d.runCountdown();
    d.tick();
    DrawPanel p = compactPanel(d);
    checkCompact(p);
    CHECK(p.lines[0].text.find("Tenue ") == 0);
    CHECK(has(p, "Balle au centre du toit"));  // front / back gauge
    CHECK(!has(p, "Vitesse moy."));            // niveau 1: no minimum speed
    // From niveau 2 the speed target shows.
    Harness d2;
    d2.set("rlt_co_difficulty_mode", "5");
    d2.set("rlt_co_difficulty", "50");
    d2.command("control", {"dribble"});
    d2.runCountdown();
    d2.tick();
    p = compactPanel(d2);
    checkCompact(p);
    CHECK(has(p, "Vitesse moy."));
    // Catch: time left and time on the roof, the touch count is a detail.
    Harness c;
    c.command("control", {"catch"});
    c.runCountdown();
    c.tick();
    p = compactPanel(c);
    checkCompact(p);
    CHECK(p.lines[0].text.find("Temps ") == 0);
    CHECK(has(p, "Sur le toit"));
    CHECK(!has(p, "Touches"));
}

TEST(courses_new_player_starts_at_niveau_1_on_a_course_a_beginner_can_finish) {
    CHECK_EQ(CoursesModule::levelNumber(0), 1);
    CHECK_EQ(CoursesModule::levelNumber(100), 10);
    CHECK_EQ(CoursesModule::levelNumber(-5), 1);
    // Easiest first: the two Débutant exercises lead, the Avancé ones close the list.
    const auto order = CoursesModule::kindsByTier();
    CHECK_EQ(order.size(), static_cast<size_t>(kCoursesKindCount));
    if (order.size() == static_cast<size_t>(kCoursesKindCount)) {
        CHECK(order[0] == CoursesKind::RingsGround && order[1] == CoursesKind::Dribble);
        CHECK(std::string(CoursesModule::kindTierLabel(order[0])) == "Débutant");
        CHECK(std::string(CoursesModule::kindTierLabel(order[2])) == "Intermédiaire");
        CHECK(std::string(CoursesModule::kindTierLabel(order[5])) == "Avancé");
        CHECK(order[4] == CoursesKind::RingsAir && order[5] == CoursesKind::RingsAirDribble);
    }
    // A save of the previous version: every exercise stored at its old start (0.25) without being played.
    Store old;
    old.setFloat("courses.stair.ground.level", 0.25f);
    old.setInt("courses.stair.ground.attempts", 0);
    Harness h;
    h.mod.load(old);
    for (int i = 0; i < kCoursesKindCount; ++i) CHECK_EQ(h.mod.difficultyFor(static_cast<CoursesKind>(i)), 0);
    for (const auto& l : h.mod.levels()) CHECK_EQ(l.levelNumber, 1);
    // Niveau 1 of the ground rings: few big arches, and a slow drive (well under the 1200 uu/s of the target
    // time) through every one of them finishes in time.
    h.set("rlt_co_chain", "3");
    h.command("rings", {"ground"});
    h.tick();
    CHECK_EQ(h.mod.status().difficulty, 0);
    CHECK(h.mod.course().rings.size() <= 8u);
    for (const auto& r : h.mod.course().rings) CHECK(r.radius >= 400.0);
    const DrawPanel p = compactPanel(h);
    CHECK_MSG(p.title.find("niveau 1/10") != std::string::npos, p.title);
    h.runCountdown();
    h.followPath(h.ringPath(h.carCentre()), 700.f, false);
    CHECK(h.mod.lastAttempt().valid && h.mod.lastAttempt().success);
    CHECK_EQ(h.mod.lastAttempt().kind, CoursesKind::RingsGround);
}
