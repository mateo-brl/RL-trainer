// Primitives, frame building, tips, mechanics table, stats and drill setups.
#include <set>

#include "core/common/Controls.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Stats.h"
#include "core/modules/ballmech/Tips.h"
#include "tests/TestFramework.h"
#include "tests/ballmech/Sim.h"

using namespace rlt;
using namespace rlt::bm;
using namespace bmtest;

namespace {
bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

Snapshot upsideDownUnderBall(int wheels) {
    Snapshot s;
    s.car.valid = s.ball.valid = true;
    orient(s.car, PI / 2, 0, PI);
    s.ball.pos = {0, 0, 900};
    s.car.pos = s.ball.pos - Vec3{0, 0, s.ball.radius + 30.f};
    s.car.wheelContacts = wheels;
    return s;
}
} // namespace

// ---- Surfaces ------------------------------------------------------------------------------------------------
TEST(bm_prim_nearest_surface) {
    const float r = 91.25f;
    SurfaceHit f = nearestSurface({0, 0, 93.f}, r);
    CHECK(f.kind == Surface::Floor);
    CHECK_NEAR(f.gap, 1.75f, 0.01f);
    CHECK_NEAR(f.normal.z, 1.f, 1e-6f);
    SurfaceHit c = nearestSurface({0, 0, 1950.f}, r);
    CHECK(c.kind == Surface::Ceiling);
    CHECK_NEAR(c.normal.z, -1.f, 1e-6f);
    SurfaceHit w = nearestSurface({4000.f, 0, 800.f}, r);
    CHECK(w.kind == Surface::SideWall);
    CHECK_NEAR(w.normal.x, -1.f, 1e-6f);
    SurfaceHit wl = nearestSurface({-4000.f, 0, 800.f}, r);
    CHECK_NEAR(wl.normal.x, 1.f, 1e-6f);
    SurfaceHit bw = nearestSurface({2000.f, 5050.f, 1200.f}, r);
    CHECK(bw.kind == Surface::BackWall);
    CHECK_NEAR(bw.normal.y, -1.f, 1e-6f);
    // Inside the goal mouth there is no back wall.
    SurfaceHit mouth = nearestSurface({0.f, 5050.f, 300.f}, r);
    CHECK(mouth.kind != Surface::BackWall);
    SurfaceHit corner = nearestSurface({3600.f, 4400.f, 800.f}, r);
    CHECK(corner.kind == Surface::Corner);
    CHECK(corner.normal.x < 0.f && corner.normal.y < 0.f);
}

// ---- Wheel contacts ----------------------------------------------------------------------------------------------
TEST(bm_prim_wheel_attribution) {
    Snapshot under = upsideDownUnderBall(4);
    CHECK(ballUnderWheels(under));
    CHECK_EQ(ballWheelContacts(under), 4);
    CHECK_EQ(worldWheelContacts(under), 0);

    Snapshot onRoof;
    onRoof.car.valid = onRoof.ball.valid = true;
    orient(onRoof.car, PI / 2, 0, 0);
    onRoof.car.pos = {0, 0, 17};
    onRoof.car.wheelContacts = 4;
    onRoof.ball.pos = onRoof.car.pos + Vec3{0, 10, 130};
    CHECK(!ballUnderWheels(onRoof));
    CHECK_EQ(worldWheelContacts(onRoof), 4);

    Snapshot far = under;
    far.ball.pos = {0, 2000, 900};
    CHECK_EQ(worldWheelContacts(far), 4);  // contacts with something else count as arena

    // Grazing contact mid-air (ball beside the car, outside the underside cone): the ball's on a standard arena,
    // the arena's on a custom map (unknown geometry, the car may sit on a platform).
    Snapshot graze;
    graze.car.valid = graze.ball.valid = true;
    orient(graze.car, PI / 2, 0, 0);
    graze.car.pos = {0, 0, 900};
    graze.car.wheelContacts = 2;
    graze.ball.pos = hitboxCenter(graze.car) + Vec3{150.f, 0, -40.f};
    CHECK(!ballUnderWheels(graze));
    CHECK_EQ(worldWheelContacts(graze, true), 0);
    CHECK_EQ(worldWheelContacts(graze, false), 2);
    graze.car.pos = {0, 0, 17};  // near the floor: arena contact
    graze.ball.pos = hitboxCenter(graze.car) + Vec3{150.f, 0, 60.f};
    CHECK_EQ(worldWheelContacts(graze, true), 2);

    Snapshot invalid = under;
    invalid.ball.valid = false;
    CHECK_EQ(ballWheelContacts(invalid), 0);
    CHECK_NEAR(undersideAlignment(under.car, under.ball), 1.f, 0.01f);
}

TEST(bm_prim_carry_geometry) {
    Snapshot s;
    s.car.valid = s.ball.valid = true;
    orient(s.car, PI / 2, 0, 0);
    s.car.pos = {0, 0, 17};
    s.car.vel = {0, 1000, 0};
    s.ball.vel = s.car.vel;
    CHECK_NEAR(roofBallHeight(s.car, s.ball.radius), 130.08f, 0.1f);
    s.ball.pos = s.car.pos + Vec3{0, 20, 125};
    CHECK(carryGeometry(s));
    s.ball.vel = s.car.vel + Vec3{0, 400, 0};
    CHECK(!carryGeometry(s));  // sliding off
    s.ball.vel = s.car.vel;
    s.ball.pos = s.car.pos + Vec3{0, 400, 125};
    CHECK(!carryGeometry(s));  // in front, not on the roof
    s.ball.pos = s.car.pos + Vec3{0, 20, 125};
    orient(s.car, PI / 2, 0, PI);
    CHECK(!carryGeometry(s));  // upside down (ceiling): no carry
}

// ---- Goal projection ---------------------------------------------------------------------------------------------
TEST(bm_prim_goal_projection_teams) {
    const float r = 91.25f;
    // Blue attacks +Y: a straight shot from midfield is on target.
    GoalProjection blue = projectToGoal({0, 1000, 300}, {0, 3000, 200}, 0, r, true);
    CHECK(blue.valid && blue.reaches && blue.onTarget);
    CHECK(blue.angleErrDeg < 5.f);
    CHECK(blue.pointCount >= 2);
    CHECK(accuracyScore(blue) >= 0.8f);
    // Same shot for orange goes toward its own goal: it never reaches the opponent goal line.
    GoalProjection orangeWrong = projectToGoal({0, 1000, 300}, {0, 3000, 200}, 1, r, true);
    CHECK(!orangeWrong.reaches && !orangeWrong.onTarget);
    CHECK(orangeWrong.angleErrDeg > 170.f);
    CHECK(accuracyScore(orangeWrong) < 0.1f);
    // Mirrored shot for orange is on target.
    GoalProjection orange = projectToGoal({0, -1000, 300}, {0, -3000, 200}, 1, r, true);
    CHECK(orange.onTarget);
    // Wide and over the bar.
    CHECK(!projectToGoal({0, 3000, 300}, {1500, 2000, 0}, 0, r, true).onTarget);
    CHECK(!projectToGoal({0, 4000, 900}, {0, 2500, 400}, 0, r, true).onTarget);
    // A ball bouncing into the goal is accepted.
    CHECK(projectToGoal({0, 2000, 500}, {0, 2500, -600}, 0, r, true).onTarget);
    // Custom map: unknown arena.
    GoalProjection custom = projectToGoal({0, 1000, 300}, {0, 3000, 200}, 0, r, false);
    CHECK(!custom.valid);
    CHECK_NEAR(accuracyScore(custom), 0.7f, 1e-6f);
}

// ---- Frame builder -----------------------------------------------------------------------------------------------
TEST(bm_prim_frame_dodge_detection) {
    Snapshot a;
    a.frame = 10;
    a.time = 10.f / 120.f;
    a.car.valid = a.ball.valid = true;
    a.ball.pos = {0, 3000, 93};
    a.car.pos = {0, 0, 300};
    a.car.hasFlip = true;
    orient(a.car, PI / 2, 0, 0);
    Snapshot b = a;
    b.frame = 11;
    b.time = 11.f / 120.f;
    b.in.jump = true;
    b.in.pitch = -1.f;  // stick forward: front flip
    Snapshot c = b;
    c.frame = 12;
    c.time = 12.f / 120.f;
    c.car.dodging = true;  // the game confirms one tick later
    c.car.dodgeDir = {1, 0, 0};
    FrameBuilder fb;
    BuildOptions o;
    const Frame f0 = fb.build(a, nullptr, &b, &c, nullptr, false, false, o);
    const Frame f1 = fb.build(b, &a, &c, nullptr, nullptr, false, false, o);
    const Frame f2 = fb.build(c, &b, nullptr, nullptr, nullptr, false, false, o);
    CHECK(!f0.dodgeStart);
    CHECK(f1.dodgeStart);
    CHECK(f1.jumpEdge);
    CHECK_NEAR(f1.dodgeDir.x, 1.f, 1e-4f);
    CHECK(!f2.dodgeStart);  // same dodge (refractory)
    CHECK(f1.freeFlight && !f1.grounded);

    // No stick: double jump.
    Snapshot d = b;
    d.in.pitch = 0.f;
    FrameBuilder fb2;
    const Frame dj = fb2.build(d, &a, nullptr, nullptr, nullptr, false, false, o);
    CHECK(dj.doubleJumpStart && !dj.dodgeStart);
    // No stick but the game reports a dodge next tick: it is a dodge, direction from the game.
    FrameBuilder fb3;
    const Frame g = fb3.build(d, &a, &c, nullptr, nullptr, false, false, o);
    CHECK(g.dodgeStart && !g.doubleJumpStart);
    CHECK_NEAR(g.dodgeDir.x, 1.f, 1e-4f);
    // Without the flip, a jump press in the air does nothing.
    Snapshot noFlip = a;
    noFlip.car.hasFlip = false;
    FrameBuilder fb4;
    const Frame nf = fb4.build(b, &noFlip, nullptr, nullptr, nullptr, false, false, o);
    CHECK(!nf.dodgeStart && !nf.doubleJumpStart);
    // Jump from the ground is a jump, not a dodge.
    Snapshot ground = a;
    ground.car.wheelContacts = 4;
    FrameBuilder fb5;
    const Frame jf = fb5.build(b, &ground, nullptr, nullptr, nullptr, false, false, o);
    CHECK(jf.jumpEdge && !jf.dodgeStart);
    // Custom deadzone.
    BuildOptions strict;
    strict.dodgeDeadzone = 1.5f;
    FrameBuilder fb6;
    CHECK(fb6.build(b, &a, nullptr, nullptr, nullptr, false, false, strict).doubleJumpStart);
}

TEST(bm_prim_frame_touch_and_events) {
    Snapshot s = upsideDownUnderBall(4);
    TouchMark m;
    m.frame = s.frame;
    m.newContact = false;
    m.hasHit = true;
    m.hitLocation = {1, 2, 3};
    FrameBuilder fb;
    const Frame f = fb.build(s, nullptr, nullptr, nullptr, &m, true, true, BuildOptions{});
    CHECK(f.touched && !f.touchNew && f.hasHit);
    CHECK_NEAR(f.hitLocation.z, 3.f, 1e-6f);
    CHECK(f.sdkReset && f.goalEvent);
    CHECK_EQ(f.ballWheels, 4);
    CHECK_EQ(f.worldWheels, 0);
    CHECK(!f.freeFlight);  // 4 wheels on the ball: "grounded" on it, but not on the arena
    CHECK(!f.grounded);
}

// ---- Tips --------------------------------------------------------------------------------------------------------
TEST(bm_tips_codes_and_buttons) {
    PlayerProfile p;
    p.usingGamepad = true;
    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"AirRollLeft", "XboxTypeS_LeftShoulder"},
                         {"AirRollRight", "XboxTypeS_RightShoulder"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"}};
    // Key labels come from the shared Controls helper: build the expectations from it.
    const std::string jump = actionKeyLabel(p, GameAction::Jump);
    CHECK(!jump.empty());
    CHECK(fillButtons("Saute avec {jump}.", p, KeyStyle::Auto) == "Saute avec le saut (" + jump + ").");
    CHECK(airRollPrompt(p, KeyStyle::Auto) == "l'air roll gauche (" + actionKeyLabel(p, GameAction::AirRollLeft) +
                                                  ") ou droit (" + actionKeyLabel(p, GameAction::AirRollRight) + ")");
    CHECK(fillButtons("{boost} / {slide}", p, KeyStyle::Auto) ==
          "le boost (" + actionKeyLabel(p, GameAction::Boost) + ") / la glissade (" +
              actionKeyLabel(p, GameAction::Powerslide) + ")");
    CHECK(fillButtons("{jump}", p, KeyStyle::PlayStation) ==
          "le saut (" + actionKeyLabel(p, GameAction::Jump, KeyStyle::PlayStation) + ")");
    CHECK(buildTips({"FR_NO_RESET"}, p, KeyStyle::PlayStation).size() == 1u);
    PlayerProfile none;
    CHECK(fillButtons("{jump}", none, KeyStyle::Auto) == "le saut");
    CHECK(airRollPrompt(none, KeyStyle::Auto) == "l'air roll");
    PlayerProfile freeRoll;
    freeRoll.pcBindings = {{"Handbrake", "LeftShift"}};
    CHECK(airRollPrompt(freeRoll, KeyStyle::Auto) ==
          "l'air roll (" + actionKeyLabel(freeRoll, GameAction::Powerslide) + ")");

    CHECK(tipCode("FR_NO_RESET") == "bm.fr_no_reset");
    const auto tips = buildTips({"FR_OFF_TARGET", "FR_NO_RESET", "FR_NO_RESET", "UNKNOWN_CODE"}, p, KeyStyle::Auto);
    CHECK_EQ(tips.size(), 2u);
    if (tips.size() == 2) {
        CHECK(tips[0].code == "bm.fr_no_reset");  // heavier first
        CHECK(tips[0].weight >= tips[1].weight);
    }
    // Every error code a detector or drill can emit has a French tip.
    const char* codes[] = {"FR_NO_RESET", "FR_POOR_ALIGNMENT", "FR_TOO_FAST_CONTACT", "FR_RESET_WASTED",
                           "FR_FLIP_MISSED_BALL", "FR_WEAK_SHOT", "FR_OFF_TARGET", "CEIL_JUMPED_OFF", "CEIL_NO_FLIP_SHOT",
                           "CEIL_FLIP_WASTED", "CEIL_MISSED", "FL_JUMP_UNSETTLED", "FL_LOST_BALL", "FL_FLIP_TOO_EARLY",
                           "FL_FLIP_TOO_LATE", "FL_UNDER_ROTATED", "FL_OVER_ROTATED", "FL_MUSTY_NOSE_NOT_DOWN",
                           "FL_BREEZI_NO_SPIN", "FL_SUPERSONIC_START", "FL_NO_CONTACT", "FL_WEAK", "FL_OFF_TARGET",
                           "DT_LANDED", "DT_LATE_FOLLOW", "DT_LOW_BACKBOARD", "DT_NO_SECOND_TOUCH", "DT_OFF_TARGET",
                           "AD_SHORT", "AD_BALL_TOO_FAR", "AD_UNDER_BALL", "AD_BOOST_OUT", "AD_NO_FINISH",
                           "AE_OFF_TARGET", "AE_HIT_SIDE", "AE_OFF_CENTER", "AE_WEAK", "AE_ROLL_MIXED", "AE_NO_AIR_ROLL",
                           "PI_NOT_PINCHED", "PI_TIMING", "PI_OFF_TARGET", "PS_WEAK", "PS_FLIP_EARLY", "PS_SIDE_HIT",
                           "PS_SLIDE_HELD", "PS_OFF_TARGET", "DRILL_TIMEOUT", "DRILL_LANDED", "DRILL_BALL_LOST",
                           "DRILL_WRONG_MECH", "DRILL_NEED_POWER", "DRILL_NEED_TARGET", "DRILL_NEED_DURATION",
                           "DRILL_NEED_CONVERT", "DRILL_NEED_SCORE"};
    for (const char* c : codes) {
        const char* t = tipTemplate(c);
        CHECK_MSG(t != nullptr, c);
        if (!t) continue;
        const std::string filled = fillButtons(t, p, KeyStyle::Auto);
        CHECK_MSG(filled.find('{') == std::string::npos, c);
        CHECK_MSG(!hasEmDash(t), c);
        CHECK_MSG(std::string(t).size() < 120, c);  // one short sentence
    }
}

// ---- Mechanics table ---------------------------------------------------------------------------------------------
TEST(bm_mechanics_table) {
    std::set<std::string> ids, skills;
    for (int i = 0; i < kMechCount; ++i) {
        const MechInfo& m = mechInfo(i);
        CHECK(static_cast<int>(m.mech) == i);
        ids.insert(m.id);
        skills.insert(m.skill);
        CHECK(std::string(m.skill) == std::string("ball.") + m.id);
        CHECK(std::string(m.title).size() > 3 && std::string(m.what).size() > 10 && std::string(m.key).size() > 10);
        CHECK(!hasEmDash(m.what) && !hasEmDash(m.key) && !hasEmDash(m.drillHint) && !hasEmDash(m.title));
        CHECK(m.drillTimeout >= 5.f);
        Mech back;
        CHECK(mechFromId(m.id, back) && back == m.mech);
    }
    CHECK_EQ(ids.size(), static_cast<size_t>(kMechCount));
    CHECK_EQ(skills.size(), static_cast<size_t>(kMechCount));
    const char* expected[] = {"flipreset", "ceiling", "flick", "musty", "breezi", "doubletap",
                              "airdribble", "airroll", "aerial", "pinch", "powershot"};
    for (const char* e : expected) CHECK_MSG(ids.count(e) == 1, e);
    Mech m;
    CHECK(mechFromId("dt", m) && m == Mech::DoubleTap);
    CHECK(!mechFromId("nope", m));

    MechGrade g;
    g.family = Family::Flick;
    g.variant = "musty";
    CHECK(mechOf(g) == Mech::Musty);
    g.variant = "breezi";
    CHECK(mechOf(g) == Mech::Breezi);
    g.variant = "45_delayed";
    CHECK(mechOf(g) == Mech::Flick);
    CHECK(gradeTitle(g) == "Flick en diagonale retardé");
    g.family = Family::Aerial;
    g.variant = "air_roll_shot";
    CHECK(mechOf(g) == Mech::AirRoll);
    CHECK(gradeTitle(g) == "Air roll shot");
    g.variant = "redirect";
    CHECK(mechOf(g) == Mech::Aerial);
    g.family = Family::Pinch;
    g.variant = "wall_pinch_kuxir";
    CHECK(gradeTitle(g) == "Pinch mural (Kuxir)");
    g.family = Family::FlipReset;
    g.variant = "used";
    CHECK(gradeTitle(g) == "Flip reset : double touche");
    g.family = Family::AirDribble;
    g.variant = "wall_to_air_carry";
    CHECK(gradeTitle(g) == "Air dribble depuis le mur (porté)");
}

// ---- Stats -------------------------------------------------------------------------------------------------------
TEST(bm_stats_record_and_bounds) {
    MechStats m;
    for (int i = 0; i < 40; ++i) m.record(static_cast<float>(i * 2), i % 2 == 0);
    CHECK_EQ(m.attempts, 40);
    CHECK_EQ(m.successes, 20);
    CHECK_EQ(m.recent.size(), MechStats::kRecent);
    CHECK_NEAR(m.recent.back(), 78.f, 1e-4f);
    CHECK_NEAR(m.best, 78.f, 1e-4f);
    CHECK_NEAR(m.rate(), 0.5f, 1e-6f);
    CHECK_NEAR(m.average(), 39.f, 1e-3f);
    m.record(250.f, true);  // clamped
    CHECK_NEAR(m.best, 100.f, 1e-4f);
}

TEST(bm_stats_save_load_and_garbage) {
    Stats a;
    a.at(Mech::Flick).record(80.f, true);
    a.at(Mech::Flick).record(40.f, false);
    a.records.flipResets = 12;
    a.records.flickSpeed = 2500.f;
    a.records.airDribbleSeconds = 3.5f;
    a.sessionResets = 3;
    Store st;
    a.save(st);
    for (const auto& k : st.keysWithPrefix("")) CHECK_MSG(k.rfind("ballmech.", 0) == 0, k);
    Stats b;
    b.load(st);
    CHECK_EQ(b.at(Mech::Flick).attempts, 2);
    CHECK_EQ(b.at(Mech::Flick).successes, 1);
    CHECK_NEAR(b.at(Mech::Flick).best, 80.f, 0.01f);
    CHECK_EQ(b.at(Mech::Flick).recent.size(), 2u);
    CHECK_EQ(b.records.flipResets, 12);
    CHECK_NEAR(b.records.flickSpeed, 2500.f, 0.1f);
    CHECK_EQ(b.sessionResets, 0);  // session counters are not persisted

    Store bad;
    bad.setInt("ballmech.stats.flick.attempts", -5);
    bad.setInt("ballmech.stats.flick.successes", 999);
    bad.setString("ballmech.stats.flick.best", "nan");
    bad.setString("ballmech.records.flick_speed", "1e30");
    std::vector<std::string> many(100, "50");
    many[3] = "abc";
    bad.setList("ballmech.stats.aerial.recent", many);
    Stats c;
    c.load(bad);
    CHECK_EQ(c.at(Mech::Flick).attempts, 0);
    CHECK_EQ(c.at(Mech::Flick).successes, 0);
    CHECK(c.at(Mech::Flick).best >= 0.f && c.at(Mech::Flick).best <= 100.f);
    CHECK(c.records.flickSpeed <= 6000.f);
    CHECK(c.at(Mech::Aerial).recent.size() <= MechStats::kRecent);
}

// ---- Drill setups ------------------------------------------------------------------------------------------------
TEST(bm_drill_setups_inside_the_arena) {
    CarState car;
    for (int team = 0; team <= 1; ++team) {
        for (int i = 0; i < kMechCount; ++i) {
            for (float level : {0.f, 0.5f, 1.f}) {
                for (uint64_t seed = 1; seed <= 6; ++seed) {
                    Rng rng(seed);
                    const DrillSetup d = makeDrillSetup(static_cast<Mech>(i), level, team, car, 91.25f, rng);
                    const std::string what = std::string(mechInfo(i).id) + " team " + std::to_string(team);
                    auto inside = [](const Vec3& p) {
                        return std::fabs(p.x) < 4096.f && std::fabs(p.y) < 5120.f && p.z > 0.f && p.z < 2044.f &&
                               std::fabs(p.x) + std::fabs(p.y) < 8064.f;
                    };
                    CHECK_MSG(inside(d.car.pos), "car " + what);
                    CHECK_MSG(inside(d.ball.pos), "ball " + what);
                    CHECK_MSG(length(d.ball.vel) < 4000.f && length(d.car.vel) < 2400.f, "speeds " + what);
                    CHECK_MSG(d.car.boost.has_value(), "boost " + what);
                    CHECK_MSG(!d.label.empty(), "label " + what);
                }
            }
        }
    }
}

TEST(bm_drill_setup_details) {
    CarState car;
    Rng rng(42);
    // Flick: ball resting on the roof centre, same velocity as the car.
    const DrillSetup f = makeDrillSetup(Mech::Flick, 0.3f, 0, car, 91.25f, rng);
    const Basis fb = basisFromRot(f.car.rot);
    const Vec3 local = fb.toLocal(f.ball.pos - f.car.pos);
    CHECK_NEAR(local.z, roofBallHeight(car, 91.25f) + 2.f, 0.5f);
    CHECK_NEAR(local.x, car.hitboxOffset.x, 0.5f);
    CHECK(length(f.ball.vel - f.car.vel) < 1e-3f);
    CHECK(f.flickTarget.empty());
    Rng rng2(42);
    CHECK(makeDrillSetup(Mech::Musty, 0.3f, 0, car, 91.25f, rng2).flickTarget == "musty");
    Rng rngv(42);
    CHECK(makeDrillSetup(Mech::Flick, 0.3f, 0, car, 91.25f, rngv, "45").flickTarget == "45");

    // Flip reset: car below the ball, closing in, flip burned by a scripted double jump. Level 1: the car is turned a
    // quarter turn around its nose (a passive contact is a side touch), the hint names the air roll that fixes it.
    Rng rng3(7);
    const DrillSetup r = makeDrillSetup(Mech::FlipReset, 0.f, 0, car, 91.25f, rng3);
    CHECK(r.burnFlip);
    CHECK(r.car.pos.z < r.ball.pos.z);
    const Basis rb = basisFromRot(r.car.rot);
    const Vec3 toBall = normalize(r.ball.pos - r.car.pos);
    CHECK(std::fabs(dot(-rb.up, toBall)) < 0.2f);
    CHECK(r.airRollHint != 0);
    Rot back = r.car.rot;
    back.roll += static_cast<int>(std::lround(r.airRollHint * 90.f * 65536.f / 360.f));
    CHECK(dot(-basisFromRot(back).up, toBall) > 0.95f);
    const Vec3 launchVel = r.car.vel + rb.up * arena::kJumpImpulse;  // after the scripted double jump
    CHECK(dot(launchVel - r.ball.vel, toBall) > 200.f);

    // Orange: mirrored toward -Y, yaw turned around.
    Rng a(9), b(9);
    const DrillSetup blue = makeDrillSetup(Mech::PowerShot, 0.2f, 0, car, 91.25f, a);
    const DrillSetup orange = makeDrillSetup(Mech::PowerShot, 0.2f, 1, car, 91.25f, b);
    CHECK_NEAR(orange.ball.pos.y, -blue.ball.pos.y, 1e-3f);
    CHECK_NEAR(orange.car.pos.x, -blue.car.pos.x, 1e-3f);
    CHECK_NEAR(orange.ball.vel.y, -blue.ball.vel.y, 1e-3f);
    const Basis ob = basisFromRot(orange.car.rot);
    CHECK(ob.fwd.y < -0.9f);

    // Double tap: read mode at low level (ball sent to the backboard), full mode at high level.
    Rng d1(3), d2(3);
    const DrillSetup dtLow = makeDrillSetup(Mech::DoubleTap, 0.1f, 0, car, 91.25f, d1);
    CHECK(dtLow.doubleTapRead);
    GoalProjection p = projectToGoal(dtLow.ball.pos, dtLow.ball.vel, 0, 91.25f, true);
    CHECK(p.reaches && !p.onTarget);  // toward the backboard, above the goal
    CHECK(!makeDrillSetup(Mech::DoubleTap, 0.9f, 0, car, 91.25f, d2).doubleTapRead);

    // Ballistic helper.
    Vec3 p0, v0;
    ballisticStart({0, 1000, 800}, {0, 300, -100}, 1.2f, p0, v0);
    const Vec3 p1 = p0 + v0 * 1.2f + Vec3{0, 0, 0.5f * arena::kGravityZ * 1.44f};
    CHECK(distance(p1, {0, 1000, 800}) < 0.5f);
    CHECK_EQ(rotDeg(0.f, 270.f, 0.f).yaw, rotDeg(0.f, -90.f, 0.f).yaw);
}

TEST(bm_drill_judge_levels) {
    MechGrade g;
    g.family = Family::PowerShot;
    g.success = true;
    g.score = 0.8f;
    g.metrics = {makeMetric("exit_speed", "", 2600.f, "uu/s")};
    CHECK(judgeDrill(Mech::PowerShot, g, 0.f, 50, 4000.f).success);
    DrillVerdict hard = judgeDrill(Mech::PowerShot, g, 1.f, 50, 4000.f);  // needs 3200 uu/s
    CHECK(!hard.success);
    CHECK(std::find(hard.extraErrors.begin(), hard.extraErrors.end(), "DRILL_NEED_POWER") != hard.extraErrors.end());
    DrillVerdict lowScore = judgeDrill(Mech::PowerShot, g, 0.f, 90, 4000.f);
    CHECK(!lowScore.success);
    CHECK(lowScore.extraErrors.front() == "DRILL_NEED_SCORE");

    // Flip reset: acquiring is enough at low level, converting needed above.
    MechGrade fr;
    fr.family = Family::FlipReset;
    fr.variant = "wasted";
    fr.success = false;
    fr.score = 0.6f;
    CHECK(judgeDrill(Mech::FlipReset, fr, 0.2f, 50, 4000.f).success);
    DrillVerdict mid = judgeDrill(Mech::FlipReset, fr, 0.5f, 50, 4000.f);
    CHECK(!mid.success);
    CHECK(std::find(mid.extraErrors.begin(), mid.extraErrors.end(), "DRILL_NEED_CONVERT") != mid.extraErrors.end());
    fr.variant = "no_reset";
    CHECK(!judgeDrill(Mech::FlipReset, fr, 0.f, 50, 4000.f).success);
    fr.variant = "used";
    fr.success = true;
    fr.projection.valid = true;
    fr.projection.onTarget = false;
    CHECK(judgeDrill(Mech::FlipReset, fr, 0.5f, 50, 4000.f).success);
    CHECK(!judgeDrill(Mech::FlipReset, fr, 0.8f, 50, 4000.f).success);  // must be on target at high level

    // Air roll drill: an aerial shot without air roll fails with the air roll tip.
    MechGrade ae;
    ae.family = Family::Aerial;
    ae.variant = "aerial_shot";
    ae.success = true;
    ae.score = 0.9f;
    ae.metrics = {makeMetric("ball_speed_after", "", 2500.f, "uu/s")};
    DrillVerdict ar = judgeDrill(Mech::AirRoll, ae, 0.f, 50, 4000.f);
    CHECK(!ar.success);
    CHECK(std::find(ar.extraErrors.begin(), ar.extraErrors.end(), "AE_NO_AIR_ROLL") != ar.extraErrors.end());
    ae.variant = "air_roll_shot";
    CHECK(judgeDrill(Mech::AirRoll, ae, 0.f, 50, 4000.f).success);
    CHECK(gradeMatchesDrill(Mech::AirRoll, ae) && gradeMatchesDrill(Mech::Aerial, ae));
    CHECK(!gradeMatchesDrill(Mech::Flick, ae));

    // Air dribble: duration grows with the level.
    MechGrade ad;
    ad.family = Family::AirDribble;
    ad.success = true;
    ad.score = 0.7f;
    ad.metrics = {makeMetric("duration", "", 1.5f, "s")};
    CHECK(judgeDrill(Mech::AirDribble, ad, 0.2f, 50, 4000.f).success);
    CHECK(!judgeDrill(Mech::AirDribble, ad, 0.9f, 50, 4000.f).success);
}

// ---- Pedagogy: difficulty order, plain HUD texts, beginner levels --------------------------------------------------
namespace {
bool hasDash(const std::string& s) {
    return s.find("\xE2\x80\x94") != std::string::npos || s.find("\xE2\x80\x93") != std::string::npos;
}
bool technicalText(const std::string& s) {
    return s.find("°") != std::string::npos || s.find(" ms") != std::string::npos || s.find(" uu") != std::string::npos;
}
} // namespace

TEST(bm_mechanics_difficulty_order) {
    // Every mechanic once, basics first, tags never going down along the order.
    const auto& order = learningOrder();
    std::set<int> seen;
    for (Mech m : order) seen.insert(static_cast<int>(m));
    CHECK_EQ(seen.size(), static_cast<size_t>(kMechCount));
    for (size_t i = 1; i < order.size(); ++i)
        CHECK(static_cast<int>(mechInfo(order[i - 1]).difficulty) <= static_cast<int>(mechInfo(order[i]).difficulty));
    CHECK(mechInfo(order[0]).difficulty == Difficulty::Beginner);
    CHECK(mechInfo(Mech::PowerShot).difficulty == Difficulty::Beginner);
    CHECK(mechInfo(Mech::Aerial).difficulty == Difficulty::Beginner);
    CHECK(mechInfo(Mech::FlipReset).difficulty == Difficulty::Advanced);
    CHECK(mechInfo(Mech::Musty).difficulty == Difficulty::Advanced);
    CHECK(std::string(difficultyLabel(Difficulty::Beginner)) == "Débutant");
    CHECK(std::string(difficultyLabel(Difficulty::Intermediate)) == "Intermédiaire");
    CHECK(std::string(difficultyLabel(Difficulty::Advanced)) == "Avancé");
    for (int i = 0; i < kMechCount; ++i) {
        const MechInfo& mi = mechInfo(i);
        int steps = 0;
        for (const char* s : mi.steps) {
            if (!s || !*s) continue;
            ++steps;
            CHECK_MSG(!hasDash(s), s);
        }
        CHECK_MSG(steps >= 3, mi.id);
    }
}

TEST(bm_drill_player_levels) {
    CHECK_EQ(playerLevel(0.f), 1);
    CHECK_EQ(playerLevel(1.f), 10);
    CHECK_EQ(playerLevel(0.5f), 6);
    for (int n = 1; n <= 10; ++n) CHECK_EQ(playerLevel(levelOfPlayer(n)), n);
    // Lower bar and more time at level 1, higher bar at level 10.
    CHECK_EQ(drillMinScore(50, 0.f), 25);
    CHECK_EQ(drillMinScore(50, 1.f), 70);
    CHECK_EQ(drillMinScore(100, 1.f), 100);
    CHECK_EQ(drillMinScore(0, 0.f), 0);
    CHECK_NEAR(drillTimeFactor(0.f), 1.5f, 1e-6f);
    CHECK_NEAR(drillTimeFactor(1.f), 1.f, 1e-6f);
}

TEST(bm_drill_texts_plain) {
    // HUD instructions: short step lists in words, no measures. Tab texts: no dash characters.
    const char* variants[] = {"", "front", "45", "90", "backflip", "side", "reverse"};
    for (int i = 0; i < kMechCount; ++i) {
        const Mech m = static_cast<Mech>(i);
        for (int n = 1; n <= 10; ++n) {
            for (const char* v : variants) {
                const std::string a = drillAction(m, levelOfPlayer(n), v);
                CHECK_MSG(!a.empty() && a.size() <= 70, a);
                CHECK_MSG(!technicalText(a) && !hasDash(a) && a.find('{') == std::string::npos, a);
            }
            const std::string c = drillCriteria(m, levelOfPlayer(n), 50, 4000.f, SpeedUnit::Kph);
            CHECK_MSG(!c.empty() && !hasDash(c), c);
            CHECK(!hasDash(drillLevelNote(m, levelOfPlayer(n))));
        }
        const std::string p = drillProgression(m);
        CHECK_MSG(p.size() > 20 && !hasDash(p), mechInfo(m).id);
    }
    // The flip reset drill names its first step: the reset alone.
    CHECK(drillAction(Mech::FlipReset, 0.f).find("Flippe") == std::string::npos);
    CHECK(drillAction(Mech::FlipReset, levelOfPlayer(kFrConvertLevel)).find("Flippe") != std::string::npos);
    CHECK(drillCriteria(Mech::PowerShot, 0.f, 50, 4000.f, SpeedUnit::Kph).find("72 KPH") != std::string::npos);
}

TEST(bm_tips_hud_text) {
    // Every tip has a one-line HUD version: short, in words, no button placeholder, no measure.
    const char* codes[] = {
        "FR_NO_RESET", "FR_POOR_ALIGNMENT", "FR_TOO_FAST_CONTACT", "FR_RESET_WASTED", "FR_FLIP_MISSED_BALL",
        "FR_WEAK_SHOT", "FR_OFF_TARGET", "CEIL_JUMPED_OFF", "CEIL_NO_FLIP_SHOT", "CEIL_FLIP_WASTED", "CEIL_MISSED",
        "FL_JUMP_UNSETTLED", "FL_LOST_BALL", "FL_FLIP_TOO_EARLY", "FL_FLIP_TOO_LATE", "FL_UNDER_ROTATED",
        "FL_OVER_ROTATED", "FL_MUSTY_NOSE_NOT_DOWN", "FL_BREEZI_NO_SPIN", "FL_SUPERSONIC_START", "FL_NO_CONTACT",
        "FL_WEAK", "FL_OFF_TARGET", "DT_LANDED", "DT_LATE_FOLLOW", "DT_LOW_BACKBOARD", "DT_NO_SECOND_TOUCH",
        "DT_OFF_TARGET", "AD_SHORT", "AD_BALL_TOO_FAR", "AD_UNDER_BALL", "AD_BOOST_OUT", "AD_NO_FINISH",
        "AE_OFF_TARGET", "AE_HIT_SIDE", "AE_OFF_CENTER", "AE_WEAK", "AE_ROLL_MIXED", "AE_NO_AIR_ROLL",
        "PI_NOT_PINCHED", "PI_TIMING", "PI_OFF_TARGET", "PS_WEAK", "PS_FLIP_EARLY", "PS_SIDE_HIT", "PS_SLIDE_HELD",
        "PS_OFF_TARGET", "DRILL_TIMEOUT", "DRILL_LANDED", "DRILL_BALL_LOST", "DRILL_WRONG_MECH", "DRILL_NEED_POWER",
        "DRILL_NEED_TARGET", "DRILL_NEED_DURATION", "DRILL_NEED_CONVERT", "DRILL_NEED_SCORE",
        // Steps of the full sequences and the level overrides.
        "CEIL_STEP_WALL", "CEIL_STEP_CEILING", "CEIL_STEP_FELL_WALL", "CEIL_TOO_FAST", "CEIL_STEP_DROP",
        "FR_STEP_TAKEOFF", "FR_STEP_UNDER", "FL_STEP_JUMP", "BZ_STEP_HOLD", "AD_NO_CATCH", "AD_LOW_POP",
        "AD_STEP_POP", "AD_STEP_TOUCH", "AE_STEP_TAKEOFF", "AE_STEP_AXIS", "PI_STEP_SPEED", "PS_STEP_ALIGN",
        "PS_NO_FLIP", "PU_NO_ROOF", "PU_UNSTABLE", "PU_DROPPED"};
    PlayerProfile profile;
    for (const char* c : codes) {
        const auto tips = buildTips({c}, profile, KeyStyle::Xbox);
        CHECK_MSG(tips.size() == 1u, c);
        if (tips.empty()) continue;
        const std::string h = hudTipText(tips[0]);
        CHECK_MSG(!h.empty() && h.size() <= 60 && h != tips[0].text, c);
        CHECK_MSG(h.find('{') == std::string::npos && !technicalText(h) && !hasDash(h), c);
    }
    Tip other;
    other.code = "ft.too_fast";
    other.text = "Texte complet";
    CHECK(hudTipText(other) == "Texte complet");
}

TEST(bm_drill_beginner_attempts_pass_level_1) {
    // A rough flick (weak, late, under-rotated, off target): a success at level 1, not at level 10.
    MechGrade fl;
    fl.family = Family::Flick;
    fl.variant = "45";
    fl.success = true;  // the detector saw a flick that pushed the ball (>= 700 uu/s)
    fl.score = 0.32f;
    fl.metrics = {makeMetric("ball_dv", "", 820.f, "uu/s"), makeMetric("delay", "", 0.62f, "s")};
    fl.errors = {"FL_FLIP_TOO_LATE", "FL_UNDER_ROTATED", "FL_OFF_TARGET"};
    fl.projection.valid = true;
    fl.projection.onTarget = false;
    CHECK(judgeDrill(Mech::Flick, fl, 0.f, drillMinScore(50, 0.f), 4000.f).success);
    CHECK(!judgeDrill(Mech::Flick, fl, 1.f, drillMinScore(50, 1.f), 4000.f).success);
    // Even with a good score, level 10 asks for power and an on-target shot.
    fl.score = 0.8f;
    const DrillVerdict hard = judgeDrill(Mech::Flick, fl, 1.f, drillMinScore(50, 1.f), 4000.f);
    CHECK(!hard.success);
    CHECK(std::find(hard.extraErrors.begin(), hard.extraErrors.end(), "DRILL_NEED_POWER") != hard.extraErrors.end());

    // Flip reset: wheels on the ball but no flip after it (the usual first try) passes the first step.
    MechGrade fr;
    fr.family = Family::FlipReset;
    fr.variant = "wasted";
    fr.success = false;
    fr.score = 0.45f;
    fr.errors = {"FR_RESET_WASTED", "FR_POOR_ALIGNMENT"};
    CHECK(judgeDrill(Mech::FlipReset, fr, 0.f, drillMinScore(50, 0.f), 4000.f).success);
    const DrillVerdict frHard = judgeDrill(Mech::FlipReset, fr, 1.f, drillMinScore(50, 1.f), 4000.f);
    CHECK(!frHard.success);
    CHECK(std::find(frHard.extraErrors.begin(), frHard.extraErrors.end(), "DRILL_NEED_CONVERT") != frHard.extraErrors.end());

    // Double tap read mode: touching the rebound off target is the first step (levels 1 and 2 only).
    MechGrade dt;
    dt.family = Family::DoubleTap;
    dt.variant = "read";
    dt.hasShot = true;
    dt.success = false;  // off target
    dt.score = 0.35f;
    dt.metrics = {makeMetric("ball_speed_after", "", 950.f, "uu/s")};
    dt.projection.valid = true;
    dt.projection.onTarget = false;
    CHECK(judgeDrill(Mech::DoubleTap, dt, 0.f, drillMinScore(50, 0.f), 4000.f).success);
    CHECK(judgeDrill(Mech::DoubleTap, dt, levelOfPlayer(2), drillMinScore(50, levelOfPlayer(2)), 4000.f).success);
    CHECK(!judgeDrill(Mech::DoubleTap, dt, levelOfPlayer(kDtTargetLevel), drillMinScore(50, levelOfPlayer(3)), 4000.f).success);
    CHECK(!judgeDrill(Mech::DoubleTap, dt, 1.f, drillMinScore(50, 1.f), 4000.f).success);
    dt.score = 0.15f;  // a very poor touch still fails the first step, with the score advice
    const DrillVerdict poor = judgeDrill(Mech::DoubleTap, dt, 0.f, drillMinScore(50, 0.f), 4000.f);
    CHECK(!poor.success);
    CHECK(std::find(poor.extraErrors.begin(), poor.extraErrors.end(), "DRILL_NEED_SCORE") != poor.extraErrors.end());

    // Soft but on-target aerial: enough for level 1, too soft for level 10.
    MechGrade ae;
    ae.family = Family::Aerial;
    ae.variant = "aerial_shot";
    ae.success = true;
    ae.score = 0.4f;
    ae.metrics = {makeMetric("ball_speed_after", "", 1300.f, "uu/s")};
    CHECK(judgeDrill(Mech::Aerial, ae, 0.f, drillMinScore(50, 0.f), 4000.f).success);
    CHECK(!judgeDrill(Mech::Aerial, ae, 1.f, drillMinScore(50, 1.f), 4000.f).success);
}
