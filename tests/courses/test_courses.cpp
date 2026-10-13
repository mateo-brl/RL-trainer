// CoursesModule: settings schema, activity gating, countdown, rings runs (finish, miss, strict, timeout, reset),
// air dribble ball path, personal bests, adaptive difficulty, commands.

#include "tests/TestFramework.h"

#include <algorithm>
#include <cctype>

#include "tests/courses/CoursesTestUtil.h"

using namespace rlt;
using namespace ct;

namespace {

const AttemptResult* lastResult(const Harness& h) {
    for (auto it = h.log.rbegin(); it != h.log.rend(); ++it)
        if (!it->results.empty()) return &it->results.back();
    return nullptr;
}

bool logHasToast(const Harness& h, const std::string& needle) {
    for (const auto& a : h.log)
        for (const auto& t : a.toasts)
            if (t.title.find(needle) != std::string::npos || t.text.find(needle) != std::string::npos) return true;
    return false;
}

bool logHasChat(const Harness& h, const std::string& needle) {
    for (const auto& a : h.log)
        if (hasChat(a, needle)) return true;
    return false;
}

// Plays the current course along its ring path at `speed`, returns the number of ticks.
int playCourse(Harness& h, float speed) {
    h.runCountdown();
    return h.followPath(h.ringPath(h.carCentre()), speed, false);
}

std::string code42(courses::CourseType t = courses::CourseType::Aerial, int d = 50) {
    return courses::encodeCourseCode(42, t, d);
}

} // namespace

TEST(courses_has_id_and_title) {
    CoursesModule m;
    CHECK(std::string(m.id()) == "courses");
    CHECK(std::string(m.title()) == "Parcours");
}

TEST(courses_schema_defaults_parse_and_match_config) {
    SettingsSchema schema;
    CoursesModule::describeSettings(schema);
    const auto defs = schema.forModule("courses");
    CHECK(defs.size() >= 25u);
    Settings s;
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_co_", 0) == 0, d->cvar);
        CHECK_MSG(!d->label.empty() && !d->tooltip.empty(), d->cvar);
        CHECK_MSG(d->apply && d->apply(s, d->defaultValue), d->cvar);
        CHECK_MSG(d->read && d->read(s) == d->defaultValue, d->cvar);
        CHECK_MSG(d->label.find("\xE2\x80\x94") == std::string::npos && d->tooltip.find("\xE2\x80\x94") == std::string::npos, d->cvar);
    }
    // Schema defaults equal the struct defaults.
    const CoursesConfig def;
    CHECK(s.courses.enabled == def.enabled);
    CHECK_EQ(s.courses.difficultyMode, def.difficultyMode);
    CHECK_EQ(s.courses.manualDifficulty, def.manualDifficulty);
    CHECK_NEAR(s.courses.countdown, def.countdown, 1e-6);
    CHECK_EQ(s.courses.missPolicy, def.missPolicy);
    CHECK_NEAR(s.courses.missPenalty, def.missPenalty, 1e-6);
    CHECK_EQ(s.courses.boostStart, def.boostStart);
    CHECK(s.courses.nextColor == def.nextColor);
    CHECK_NEAR(s.courses.hudX, 0.01f, 1e-6);
    CHECK_NEAR(s.courses.hudY, 0.12f, 1e-6);
    CHECK_EQ(s.courses.lookahead, def.lookahead);
    // Sections in French, the expert knobs hidden.
    const auto sections = schema.sections("courses");
    CHECK(std::find(sections.begin(), sections.end(), "Anneaux") != sections.end());
    CHECK(std::find(sections.begin(), sections.end(), "Contrôle") != sections.end());
    CHECK(schema.find("rlt_co_timeout_factor")->advanced);
    CHECK(!schema.find("rlt_co_difficulty_mode")->advanced);
    // Clamping of out-of-range values.
    CHECK(schema.apply(s, "rlt_co_difficulty", "250"));
    CHECK_EQ(s.courses.manualDifficulty, 100);
    CHECK(!schema.apply(s, "rlt_co_countdown", "abc"));
}

TEST(courses_start_refused_without_activity) {
    Harness h;
    h.host.allow = false;
    const Actions a = h.command("rings", {"air"});
    CHECK(hasChat(a, "indisponible"));
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    const Actions t = h.tick();
    CHECK(!t.setCar && !t.setBall && !t.freezeCar && !t.countdownMessage);
}

TEST(courses_start_refused_when_world_control_not_allowed) {
    Harness h;
    h.game.mode = GameMode::Workshop;
    h.game.worldControlAllowed = false;
    const Actions a = h.command("rings", {"ground"});
    CHECK(hasChat(a, "indisponible"));
    CHECK_EQ(h.host.requests, 0);
    CHECK(h.mod.phase() == CoursesPhase::Idle);
}

TEST(courses_idle_on_workshop_course_maps) {
    for (MapCategory cat : {MapCategory::Rings, MapCategory::Dribble, MapCategory::AirDribble}) {
        Harness h;
        h.game.mode = GameMode::Workshop;
        h.game.category = cat;
        h.game.worldControlAllowed = true;  // even with the advanced option on
        const Actions a = h.command("control", {"dribble"});
        CHECK(hasChat(a, "map workshop"));
        CHECK(h.mod.phase() == CoursesPhase::Idle);
        CHECK_EQ(h.host.requests, 0);
    }
}

TEST(courses_countdown_freezes_places_and_releases) {
    Harness h;
    const Actions start = h.command("rings", {"air", code42()});
    CHECK(hasChat(start, "RLT1-A050"));
    CHECK(h.host.ownsActivity(&h.mod));
    CHECK_EQ(h.host.name, std::string("Anneaux"));
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    const auto& course = h.mod.course();
    const Actions first = h.tick();
    CHECK(first.freezeCar);
    CHECK(first.setCar.has_value());
    CHECK(first.countdownMessage && *first.countdownMessage == 2);
    if (first.setCar) {
        CHECK_NEAR(first.setCar->pos.x, course.start.carPos.x, 0.5);
        CHECK_NEAR(first.setCar->pos.y, course.start.carPos.y, 0.5);
        CHECK_NEAR(length(first.setCar->vel), 0.f, 1e-3);  // frozen during the countdown
        CHECK(first.setCar->boost && *first.setCar->boost == 100.f);
    }
    CHECK(first.setBall.has_value());  // car course: ball parked in a top corner
    if (first.setBall) CHECK(first.setBall->pos.z > 1900.f);
    int ones = 0, ticks = 1;
    Actions last;
    while (h.mod.phase() != CoursesPhase::Running && ticks < 400) {
        last = h.tick();
        if (last.countdownMessage && *last.countdownMessage == 1) ++ones;
        ++ticks;
    }
    CHECK_EQ(ones, 1);
    CHECK_EQ(ticks, 241);  // 2 s at 120 Hz, then the release tick
    CHECK(last.goMessage);
    CHECK(!last.freezeCar);
    CHECK(last.setCar && length(last.setCar->vel) > 700.f);  // rolling start towards the first ring
}

TEST(courses_air_course_finish_result_and_personal_best) {
    Harness h;
    h.set("rlt_co_chain", "1");  // always the same course
    h.keepLog = true;
    h.command("rings", {"air", code42()});
    const std::string code = h.mod.currentCode();
    const int n = static_cast<int>(h.mod.course().rings.size());
    playCourse(h, 2000.f);
    CHECK(h.mod.phase() == CoursesPhase::Result);
    const AttemptResult* r = lastResult(h);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK_EQ(r->module, std::string("courses"));
    CHECK_EQ(r->skill, std::string("course.rings.air"));
    CHECK_EQ(r->title, std::string("Anneaux aériens"));
    CHECK(r->success);
    CHECK(r->score >= 45.f && r->score <= 100.f);
    CHECK(r->verdict != Verdict::Fail);
    const Metric* rings = findMetric(*r, "rings");
    CHECK(rings && static_cast<int>(rings->value) == n);
    const Metric* misses = findMetric(*r, "misses");
    CHECK(misses && misses->value == 0.f);
    const Metric* time = findMetric(*r, "time");
    CHECK(time && time->value > 1.f);
    CHECK(findMetric(*r, "avg_speed") && findMetric(*r, "boost_used"));
    CHECK(logHasChat(h, "Premier chrono"));
    const auto pbs = h.mod.personalBests();
    CHECK_EQ(pbs.size(), 1u);
    const float firstTime = time ? time->value : 0.f;
    if (!pbs.empty()) {
        CHECK_EQ(pbs[0].code, code);
        CHECK_NEAR(pbs[0].time, firstTime, 1e-3);
        CHECK_EQ(pbs[0].rings, n);
        CHECK_EQ(pbs[0].finishes, 1);
        CHECK_EQ(pbs[0].date, std::string("2026-10-13"));
    }
    // Slower run: no record. Same course thanks to the chain setting.
    h.log.clear();
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    CHECK_EQ(h.mod.currentCode(), code);
    playCourse(h, 1400.f);
    const AttemptResult* slow = lastResult(h);
    CHECK(slow && slow->success);
    const float slowScore = slow ? slow->score : 0.f;
    CHECK(!logHasToast(h, "record"));
    CHECK(h.mod.lastAttempt().headline.find("record") != std::string::npos);
    CHECK(!h.mod.lastAttempt().newRecord);
    CHECK(slow && findMetric(*slow, "pb_delta") && findMetric(*slow, "pb_delta")->value > 0.f);
    // Faster run: new record, toast and chat.
    h.log.clear();
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    playCourse(h, 2600.f);
    CHECK(logHasToast(h, "Nouveau record"));
    CHECK(logHasChat(h, "Record battu"));
    CHECK(h.mod.lastAttempt().newRecord);
    const auto pbs2 = h.mod.personalBests();
    CHECK(!pbs2.empty() && pbs2[0].time < firstTime);
    CHECK(!pbs2.empty() && pbs2[0].attempts == 3);
    // Faster attempts score higher.
    CHECK(h.mod.lastAttempt().score > slowScore);
}

TEST(courses_split_delta_against_personal_best) {
    Harness h;
    h.set("rlt_co_chain", "1");
    h.command("rings", {"air", code42()});
    playCourse(h, 2000.f);
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    // Second run: slower, stop right after ring 1 and read the HUD delta line.
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre(), courses::toF(rings[0].center), courses::toF(rings[1].center),
                          courses::toF(rings[1].center + rings[1].normal * 50.0)};
    h.followPath(pts, 1500.f, false);
    CHECK_EQ(h.mod.run().nextRing(), 2);
    ViewContext view;
    view.valid = true;
    view.camPos = {0.f, -5000.f, 500.f};
    view.camBasis = basisFromRot(rotFromYawDeg(90.f));
    DrawContext dctx{h.settings, h.game, h.profile, h.snap, true, true, 1.f};
    DrawList dl;
    h.mod.draw(view, dctx, dl);
    CHECK_EQ(dl.panels.size(), 1u);
    bool deltaLine = false;
    for (const auto& l : dl.panels[0].lines)
        if (l.text.find("Écart record +") != std::string::npos && l.color == colors::Red) deltaLine = true;
    CHECK(deltaLine);
}

TEST(courses_missed_ring_costs_penalty_and_goes_on) {
    Harness h;
    h.set("rlt_co_chain", "1");
    h.keepLog = true;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre()};
    // Pass 150 uu outside the rim of ring 0 (a near miss), then every other ring through the centre.
    pts.push_back(courses::toF(rings[0].center + rings[0].up * (rings[0].radius + 150.0)));
    for (size_t i = 1; i < rings.size(); ++i) pts.push_back(courses::toF(rings[i].center));
    pts.push_back(courses::toF(rings.back().center + rings.back().normal * 400.0));
    h.followPath(pts, 2000.f, false);
    const AttemptResult* r = lastResult(h);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->success);
    CHECK(findMetric(*r, "misses") && findMetric(*r, "misses")->value == 1.f);
    CHECK(hasTip(*r, "courses.rings_missed"));
    CHECK_NEAR(h.mod.run().penaltyTime(), 2.0, 1e-6);
    CHECK(h.mod.run().ringMissed(0));
    CHECK_EQ(h.mod.run().ringsPassed(), static_cast<int>(rings.size()) - 1);
}

TEST(courses_strict_policy_fails_on_miss) {
    Harness h;
    h.set("rlt_co_miss_policy", "2");
    h.keepLog = true;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre(), courses::toF(rings[0].center + rings[0].up * (rings[0].radius + 150.0)),
                          courses::toF(rings[1].center)};
    h.followPath(pts, 2000.f, false);
    CHECK(h.mod.phase() == CoursesPhase::Result);
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success);
    CHECK(r && r->verdict == Verdict::Fail);
    CHECK(r && r->score <= 40.f);
    CHECK(h.mod.lastAttempt().headline.find("Raté") == 0);
}

TEST(courses_timeout_fails) {
    Harness h;
    h.keepLog = true;
    h.command("rings", {"ground", courses::encodeCourseCode(7, courses::CourseType::GroundWall, 30)});
    h.runCountdown();
    const float limit = h.mod.timeLimit();
    CHECK(limit > 10.f);
    h.snap.car.vel = {0.f, 0.f, 0.f};
    int ticks = 0;
    while (h.mod.phase() == CoursesPhase::Running && ticks < 200000) {
        h.tick();
        ++ticks;
    }
    CHECK_NEAR(static_cast<float>(ticks) / 120.f, limit, 0.05);
    const AttemptResult* r = lastResult(h);
    CHECK(r && !r->success && hasTip(*r, "courses.rings_timeout"));
    CHECK(r && r->skill == "course.rings.ground");
}

TEST(courses_reset_shot_restarts_the_run) {
    Harness h;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    std::vector<Vec3> pts{h.carCentre(), courses::toF(rings[0].center), courses::toF(rings[1].center)};
    h.followPath(pts, 2000.f, false);
    CHECK(h.mod.run().nextRing() >= 1);
    h.event(GameEventKind::ResetShot);
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    const Actions a = h.tick();
    CHECK(a.freezeCar && a.setCar);
    CHECK(h.mod.phase() == CoursesPhase::Countdown);
    CHECK_EQ(h.mod.run().nextRing(), 0);
    // A demolition respawn while running restarts too.
    h.runCountdown();
    h.event(GameEventKind::CarSpawned);
    CHECK(h.mod.phase() == CoursesPhase::Setup);
}

TEST(courses_teleport_is_never_a_pass) {
    Harness h;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    const auto& r0 = h.mod.course().rings[0];
    // Jump from behind ring 0 to beyond it in one tick: a teleport, not a pass.
    h.carAt(courses::toF(r0.center - r0.normal * 300.0), courses::toF(r0.normal * 500.0));
    h.tick();
    h.carAt(courses::toF(r0.center + r0.normal * 300.0), courses::toF(r0.normal * 500.0));
    h.tick();
    CHECK_EQ(h.mod.run().nextRing(), 0);
    // A run that used a teleport still finishes, but never sets a record.
    Harness t;
    t.command("rings", {"air", code42()});
    t.runCountdown();
    const auto& q0 = t.mod.course().rings[0];
    t.carAt(courses::toF(q0.center - q0.normal * 200.0), {0.f, 0.f, 0.f});  // jump next to the first ring
    t.tick();
    t.followPath(t.ringPath(t.carCentre()), 2000.f, false);
    CHECK(t.mod.phase() == CoursesPhase::Result);
    CHECK(t.mod.lastAttempt().success);
    CHECK(!t.mod.lastAttempt().newRecord);
    CHECK(t.mod.lastAttempt().headline.find("téléportation") != std::string::npos);
    const auto pbs = t.mod.personalBests();
    CHECK(pbs.size() == 1u && pbs[0].time == 0.f && pbs[0].finishes == 1);
}

TEST(courses_air_dribble_ball_path) {
    Harness h;
    h.keepLog = true;
    h.command("rings", {"airdribble", courses::encodeCourseCode(42, courses::CourseType::AirDribble, 50)});
    CHECK(h.mod.kind() == CoursesKind::RingsAirDribble);
    const Actions first = h.tick();
    CHECK(first.setBall.has_value());  // ball on the roof (default start)
    if (first.setBall && first.setCar) CHECK(first.setBall->pos.z > first.setCar->pos.z + 100.f);
    h.runCountdown();
    // The car flies through the rings but the ball does not: nothing counts.
    const Vec3 ballStart = h.snap.ball.pos;
    h.followPath(h.ringPath(h.carCentre()), 1500.f, false);
    CHECK_EQ(h.mod.run().nextRing(), 0);
    CHECK(h.mod.phase() == CoursesPhase::Running);
    // Restart, then the ball goes through every ring.
    h.event(GameEventKind::ResetShot);
    h.runCountdown();
    CHECK(distance(h.snap.ball.pos, ballStart) < 1.f);
    h.followPath(h.ringPath(h.snap.ball.pos), 1500.f, true);
    const AttemptResult* ok = lastResult(h);
    CHECK(ok && ok->success);
    CHECK(ok && ok->skill == "course.rings.airdribble");
    // Popped start: the ball starts in front of the car with its launch velocity.
    Harness p;
    p.set("rlt_co_ad_start", "1");
    p.command("rings", {"airdribble", courses::encodeCourseCode(42, courses::CourseType::AirDribble, 50)});
    p.runCountdown();
    const auto& st = p.mod.course().start;
    CHECK(distance(p.snap.ball.pos, courses::toF(st.ballPos)) < 1.f);
    CHECK(distance(p.snap.ball.vel, courses::toF(st.ballVel)) < 1.f);
}

TEST(courses_ground_floor_ball_fails_air_dribble) {
    Harness h;
    h.keepLog = true;
    h.command("rings", {"airdribble"});
    h.runCountdown();
    for (int i = 0; i < 30; ++i) h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Running);
    h.snap.ball.pos = {h.snap.ball.pos.x, h.snap.ball.pos.y, 93.f};
    h.snap.ball.vel = {0.f, 0.f, -10.f};
    h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Result);
    CHECK(h.mod.lastAttempt().headline.find("sol") != std::string::npos);
}

TEST(courses_adaptive_staircase_only_for_generated_courses) {
    Harness h;
    const int before = h.mod.difficultyFor(CoursesKind::RingsAir);
    h.set("rlt_co_chain", "3");  // stop at the end
    h.command("rings", {"air"});
    CHECK(!h.mod.status().fromCode);
    CHECK_EQ(h.mod.status().difficulty, before);
    playCourse(h, 2500.f);
    CHECK(h.mod.lastAttempt().success);
    const int after = h.mod.difficultyFor(CoursesKind::RingsAir);
    CHECK(after > before);
    while (h.mod.phase() != CoursesPhase::Idle) h.tick();
    CHECK_EQ(h.host.releases, 1);  // normal end of the exercise releases the activity
    // From a code: the difficulty is the code's and the staircase does not move.
    h.command("play", {code42(courses::CourseType::Aerial, 80)});
    CHECK(h.mod.status().fromCode);
    CHECK_EQ(h.mod.status().difficulty, 80);
    playCourse(h, 2500.f);
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), after);
    // Fixed presets.
    h.set("rlt_co_difficulty_mode", "1");
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::RingsAir), 15);
    h.set("rlt_co_difficulty_mode", "4");
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::Catch), 90);
    h.set("rlt_co_difficulty_mode", "5");
    h.set("rlt_co_difficulty", "33");
    CHECK_EQ(h.mod.difficultyFor(CoursesKind::Slalom), 33);
}

TEST(courses_smart_chain_new_course_after_success_in_auto) {
    Harness h;
    h.set("rlt_co_miss_policy", "2");
    h.command("rings", {"air"});
    const std::string first = h.mod.currentCode();
    playCourse(h, 2500.f);
    CHECK(h.mod.lastAttempt().success);
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    CHECK(h.mod.currentCode() != first);  // success in auto mode: a new course
    // Failure: the same course again.
    const std::string second = h.mod.currentCode();
    h.runCountdown();
    const auto& rings = h.mod.course().rings;
    h.followPath({h.carCentre(), courses::toF(rings[0].center + rings[0].up * (rings[0].radius + 100.0)),
                  courses::toF(rings[1].center)},
                 2000.f, false);
    CHECK(h.mod.phase() == CoursesPhase::Result);
    CHECK(!h.mod.lastAttempt().success);
    while (h.mod.phase() == CoursesPhase::Result) h.tick();
    CHECK_EQ(h.mod.currentCode(), second);
}

TEST(courses_boost_settings) {
    Harness h;
    h.set("rlt_co_unlimited_boost", "1");
    h.command("rings", {"air", code42()});
    h.runCountdown();
    h.snap.car.boost = 40.f;
    const Actions a = h.tick();
    CHECK(a.setBoost && *a.setBoost == 100.f);
    // Limited boost with refills: the refill ring puts the tank back to 100.
    Harness g;
    g.set("rlt_co_boost_start", "60");
    g.keepLog = true;
    g.command("rings", {"air", code42()});
    const Actions first = g.tick();
    CHECK(first.setCar && first.setCar->boost && *first.setCar->boost == 60.f);
    const auto& rings = g.mod.course().rings;
    size_t refill = rings.size();
    for (size_t i = 0; i < rings.size(); ++i)
        if (rings[i].flags & courses::kFlagBoostRefill) {
            refill = i;
            break;
        }
    CHECK(refill < rings.size());  // seed 42 at 50 needs ~300 boost: refills exist
    g.runCountdown();
    g.log.clear();
    std::vector<Vec3> pts{g.carCentre()};
    for (size_t i = 0; i <= refill && i < rings.size(); ++i) pts.push_back(courses::toF(rings[i].center));
    if (refill < rings.size()) pts.push_back(courses::toF(rings[refill].center + rings[refill].normal * 60.0));
    g.followPath(pts, 2000.f, false);
    bool refilled = false;
    for (const auto& a2 : g.log) refilled = refilled || (a2.setBoost && *a2.setBoost == 100.f);
    CHECK(refilled);
}

TEST(courses_ring_count_setting_goes_into_the_code) {
    Harness h;
    h.set("rlt_co_ring_count", "10");
    h.set("rlt_co_difficulty_mode", "2");
    h.command("rings", {"air"});
    CHECK_EQ(h.mod.course().rings.size(), 10u);
    const auto dec = courses::decodeCourseCode(h.mod.currentCode());
    CHECK(dec.has_value());
    if (dec) CHECK_EQ(courses::seedRingCount(dec->seed), 10);
    // Replaying the code elsewhere reproduces the same course whatever the local setting.
    Harness other;
    other.command("play", {h.mod.currentCode()});
    CHECK_EQ(other.mod.course().rings.size(), 10u);
    CHECK_EQ(courses::courseHash(other.mod.course()), courses::courseHash(h.mod.course()));
}

TEST(courses_commands_and_messages) {
    Harness h;
    CHECK(hasChat(h.command("code"), "Aucun parcours"));
    CHECK(hasChat(h.command("play", {"RLT1-A050-000000000001A-0"}), "invalide"));
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    CHECK(hasChat(h.command("rings", {"banana"}), "Type inconnu"));
    CHECK(hasChat(h.command("control", {"air"}), "Type inconnu"));
    CHECK(hasChat(h.command("whatever"), "Commandes"));
    // A ground code given to "rings air" plays the code's type.
    const std::string g = courses::encodeCourseCode(5, courses::CourseType::GroundWall, 20);
    h.command("rings", {"air", g});
    CHECK(h.mod.kind() == CoursesKind::RingsGround);
    CHECK(hasChat(h.command("code"), g));
    // Lower case and spaces are accepted.
    std::string messy = " " + g + " ";
    for (char& c : messy) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    h.command("play", {messy});
    CHECK_EQ(h.mod.currentCode(), g);
    // "rings <code>" works too, and "code <code>" plays it.
    h.command("rings", {code42()});
    CHECK(h.mod.kind() == CoursesKind::RingsAir);
    h.command("code", {g});
    CHECK(h.mod.kind() == CoursesKind::RingsGround);
    // next: new seed, same kind.
    h.command("next");
    CHECK(h.mod.kind() == CoursesKind::RingsGround);
    CHECK(h.mod.currentCode() != g);
    // stop releases.
    const int rel = h.host.releases;
    CHECK(hasChat(h.command("stop"), "arrêté"));
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    CHECK_EQ(h.host.releases, rel + 1);
    // restart when idle replays the last course.
    const std::string lastCode = h.mod.currentCode();
    h.command("restart");
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    CHECK_EQ(h.mod.currentCode(), lastCode);
}

TEST(courses_context_change_and_activity_loss) {
    Harness h;
    h.event(GameEventKind::ContextChanged);  // first context seen
    h.command("rings", {"air", code42()});
    h.runCountdown();
    h.game.mapName = "Stadium_P";
    h.event(GameEventKind::ContextChanged);
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    CHECK(!h.host.ownsActivity(&h.mod));
    // Another module takes the activity: stop at once, no more world control.
    h.command("rings", {"air", code42()});
    h.runCountdown();
    CoursesModule other;
    h.host.requestActivity(&other, "Kickoff");
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    const Actions a = h.tick();
    CHECK(!a.setCar && !a.setBall && !a.freezeCar && !a.setBoost);
    // Map unloaded while running.
    h.host.owner = nullptr;
    h.command("control", {"dribble"});
    CHECK(h.mod.phase() == CoursesPhase::Setup);
    h.event(GameEventKind::MapUnloaded);
    CHECK(h.mod.phase() == CoursesPhase::Idle);
}

TEST(courses_disabled_and_paused) {
    Harness h;
    h.command("rings", {"air", code42()});
    h.runCountdown();
    // Paused: no tick logic, no time.
    const float t0 = h.mod.runTime();
    h.snap.paused = true;
    for (int i = 0; i < 50; ++i) h.tick();
    CHECK_NEAR(h.mod.runTime(), t0, 1e-6);
    h.snap.paused = false;
    h.tick();
    CHECK_NEAR(h.mod.runTime(), t0 + 1.f / 120.f, 1e-4);  // paused frames never count
    // Invalid car: waits.
    h.snap.car.valid = false;
    const Actions inv = h.tick();
    CHECK(!inv.setCar);
    h.snap.car.valid = true;
    // Disabling the module stops the exercise and releases the activity.
    h.set("rlt_co_enabled", "0");
    h.tick();
    CHECK(h.mod.phase() == CoursesPhase::Idle);
    CHECK(!h.host.ownsActivity(&h.mod));
    CHECK(hasChat(h.command("rings", {"air"}), "désactivé"));
}
