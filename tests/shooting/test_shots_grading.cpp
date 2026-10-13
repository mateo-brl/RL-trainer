// Shot tracking to the goal plane (both teams, hit / miss / short / goal event / early prediction), grading of
// flicks, targets and power shots (what the player is told), French how-to texts, statistics and persistence.

#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Units.h"
#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

// Runs a tracker on a ballistic flight, with the car parked far away. Returns the outcome (if any).
std::optional<ShotOutcome> track(ShotTracker& tr, const std::vector<BallState>& flight, int64_t frame0 = 100) {
    const CarState car = carAt({0.f, 0.f, 17.f});
    for (size_t i = 1; i < flight.size(); ++i)
        if (auto o = tr.update(snapOf(frame0 + static_cast<int64_t>(i), car, flight[i]))) return o;
    return std::nullopt;
}

// Point where the independent integrator crosses y = planeY (linear interpolation between ticks).
Vec3 crossing(const std::vector<BallState>& flight, float planeY) {
    const float s = planeY > 0.f ? 1.f : -1.f;
    for (size_t i = 1; i < flight.size(); ++i) {
        const float a = s * flight[i - 1].pos.y, b = s * flight[i].pos.y;
        if (a < std::fabs(planeY) && b >= std::fabs(planeY)) {
            const float f = (std::fabs(planeY) - a) / (b - a);
            return flight[i - 1].pos + (flight[i].pos - flight[i - 1].pos) * f;
        }
    }
    return Vec3{0.f, 0.f, -1.f};
}

FlickReport goodReport() {
    FlickReport r;
    r.cls.base = FlickType::Front;
    r.category = FlickType::Front;
    r.contact = true;
    r.power = 2500.f;
    r.gain = 2000.f;
    r.f.dodgeFwd = 1.f;
    r.f.delay = 0.2f;
    r.f.launchFwd = 0.95f;
    r.f.vertFrac = 0.3f;
    r.f.keptFrac = 1.f;
    r.carryTime = 0.5f;
    r.relSpeedAtJump = 20.f;
    r.roofOffset = {15.f, 0.f, 0.f};
    r.elevationDeg = 20.f;
    r.launchVel = {0.f, 2400.f, 700.f};
    return r;
}

AimResult aimInGoal() { return scoreAim(goalMouthRect(), true, {100.f, 250.f}); }

} // namespace

// ---- Shot tracker -------------------------------------------------------------------------------------------
TEST(shooting_tracker_blue_top_left_hit) {
    const ZoneRect tl = zoneRect(Zone::TopLeft, 0.5f);
    const GoalFrame g = GoalFrame::forTeam(0);
    const float x = g.toWorld(tl.u, tl.z).x;
    CHECK(x > 0.f);
    const auto flight = ballistic({x, 4000.f, 540.f}, {0.f, 3000.f, 0.f}, 200);
    ShotTracker tr;
    tr.start(flight[0], g, 100, 3000.f);
    CHECK(tr.active());
    CHECK(tr.hasPrediction());
    const auto o = track(tr, flight);
    CHECK(o.has_value());
    if (!o) return;
    CHECK(o->end == ShotEnd::Crossed);
    CHECK(o->reached);
    const Vec3 expect = crossing(flight, 5120.f);
    CHECK_NEAR(o->hitWorld.x, expect.x, 3.f);
    CHECK_NEAR(o->hitWorld.z, expect.z, 3.f);
    CHECK_NEAR(o->hit.x, tl.u, 3.f);
    CHECK(tl.contains(o->hit));
    CHECK(scoreAim(tl, o->reached, o->hit).inZone);
    CHECK_NEAR(o->shotSpeed, 3000.f, 1e-3f);
    CHECK(o->flightTime > 0.3f && o->flightTime < 0.45f);
    CHECK(!tr.active());
}

TEST(shooting_tracker_orange_top_left_hit_is_mirrored) {
    const ZoneRect tl = zoneRect(Zone::TopLeft, 0.5f);
    const GoalFrame g = GoalFrame::forTeam(1);
    const float x = g.toWorld(tl.u, tl.z).x;
    CHECK(x < 0.f);
    const auto flight = ballistic({x, -4000.f, 540.f}, {0.f, -3000.f, 0.f}, 200);
    ShotTracker tr;
    tr.start(flight[0], g, 100, 3000.f);
    const auto o = track(tr, flight);
    CHECK(o.has_value());
    if (!o) return;
    CHECK(o->reached);
    CHECK(tl.contains(o->hit));
    CHECK(!zoneRect(Zone::TopRight, 0.5f).contains(o->hit));
    CHECK_NEAR(o->hitWorld.y, -5120.f, 1e-2f);
}

TEST(shooting_tracker_wide_shot_into_the_back_wall) {
    // Outside the goal mouth: the centre never reaches the goal plane, the front plane still gives a hit point.
    const GoalFrame g = GoalFrame::forTeam(0);
    const auto flight = ballistic({-1400.f, 3500.f, 300.f}, {0.f, 2500.f, 300.f}, 200);
    ShotTracker tr;
    tr.start(flight[0], g, 100, 2500.f);
    const auto o = track(tr, flight);
    CHECK(o.has_value());
    if (!o) return;
    CHECK(o->reached);
    CHECK_NEAR(o->hit.x, 1400.f, 2.f); // to the attacker's right (x < 0 for blue)
    const AimResult a = scoreAim(zoneRect(Zone::TopLeft, 0.5f), o->reached, o->hit);
    CHECK(!a.onGoal && !a.inZone && a.du > 0.f);
}

TEST(shooting_tracker_short_shot_and_dribble) {
    const GoalFrame g = GoalFrame::forTeam(0);
    ShotTracker tr;
    BallState b = ballAt({0.f, 1000.f, kBallRestZ}, {0.f, -600.f, 0.f}); // rolling back towards the player
    tr.start(b, g, 0, 600.f);
    std::optional<ShotOutcome> o;
    int ticks = 0;
    const CarState car = carAt({0.f, -2000.f, 17.f});
    for (int i = 1; i < 200 && !o; ++i) {
        b.pos += b.vel * kTickDt;
        o = tr.update(snapOf(i, car, b));
        ticks = i;
    }
    CHECK(o.has_value());
    if (o) {
        CHECK(o->end == ShotEnd::Short);
        CHECK(!o->reached);
    }
    CHECK(ticks >= ShotTracker::kAwayTicks && ticks <= ShotTracker::kAwayTicks + 2);

    // Same ball but the car is right next to it (dribbling): never "short".
    ShotTracker d;
    BallState b2 = ballAt({0.f, 1000.f, 150.f}, {0.f, -600.f, 0.f});
    d.start(b2, g, 0, 600.f);
    bool ended = false;
    for (int i = 1; i < 150; ++i) {
        b2.pos += b2.vel * kTickDt;
        const CarState near = carAt({b2.pos.x, b2.pos.y - 50.f, 17.f});
        if (d.update(snapOf(i, near, b2))) ended = true;
    }
    CHECK(!ended);
}

TEST(shooting_tracker_goal_event_teleport_timeout) {
    const GoalFrame g = GoalFrame::forTeam(0);
    const auto flight = ballistic({0.f, 2000.f, 300.f}, {0.f, 2500.f, 500.f}, 30);
    ShotTracker tr;
    tr.start(flight[0], g, 0, 2500.f);
    CHECK(!track(tr, flight).has_value());
    CHECK(tr.hasPrediction());
    const Vec2 pred = tr.predicted();
    const auto o = tr.onGoalEvent(40);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->end == ShotEnd::Goal && o->reached);
        CHECK_NEAR(o->hit.x, pred.x, 1e-3f);
    }
    CHECK(!tr.onGoalEvent(41).has_value()); // inactive now

    ShotTracker tp;
    tp.start(flight[0], g, 0, 2500.f);
    BallState jumped = flight[1];
    jumped.pos.x += 1500.f;
    const auto t = tp.update(snapOf(1, carAt({0.f, 0.f, 17.f}), jumped));
    CHECK(t.has_value() && t->end == ShotEnd::Aborted);

    ShotTracker slow;
    BallState b = ballAt({0.f, 0.f, kBallRestZ}, {0.f, 300.f, 0.f});
    slow.start(b, g, 0, 300.f);
    std::optional<ShotOutcome> so;
    for (int i = 1; i < 1000 && !so; ++i) {
        b.pos += b.vel * kTickDt;
        so = slow.update(snapOf(i, carAt({3000.f, 0.f, 17.f}), b));
    }
    CHECK(so.has_value() && so->end == ShotEnd::Timeout);
}

TEST(shooting_tracker_survives_frame_hitches) {
    // 10 frames skipped at a time with a fast ball: a hitch, not a teleport.
    const GoalFrame g = GoalFrame::forTeam(0);
    const auto flight = ballistic({0.f, 1000.f, 400.f}, {0.f, 4000.f, 300.f}, 200);
    ShotTracker tr;
    tr.start(flight[0], g, 0, 4000.f);
    std::optional<ShotOutcome> o;
    const CarState car = carAt({0.f, 0.f, 17.f});
    for (size_t i = 10; i < flight.size() && !o; i += 10)
        o = tr.update(snapOf(static_cast<int64_t>(i), car, flight[i]));
    CHECK(o.has_value());
    if (!o) return;
    CHECK(o->end == ShotEnd::Crossed);
    const Vec3 real = crossing(flight, 5120.f);
    CHECK_NEAR(o->hit.y, real.z, 12.f);
}

TEST(shooting_tracker_early_prediction_accuracy) {
    // The prediction made right after the touch must match the real crossing (independent integrator).
    const GoalFrame g = GoalFrame::forTeam(0);
    const auto flight = ballistic({300.f, 1500.f, 200.f}, {0.f, 2200.f, 700.f}, 400);
    ShotTracker tr;
    tr.start(flight[0], g, 0, 2300.f);
    const auto o = track(tr, flight, 0);
    CHECK(o.has_value());
    if (!o) return;
    CHECK(o->hadEarlyPrediction);
    const Vec3 real = crossing(flight, 5120.f);
    CHECK_NEAR(o->earlyPrediction.x, g.toPlane(real).x, 5.f);
    CHECK_NEAR(o->earlyPrediction.y, real.z, 5.f);
    CHECK_NEAR(o->hit.y, real.z, 3.f);
}

// ---- Grading: targets -----------------------------------------------------------------------------------------
TEST(shooting_grade_target_hit_and_misses) {
    TargetGradeParams p;
    p.zone = Zone::TopLeft;
    p.rect = zoneRect(Zone::TopLeft, 0.5f);
    p.setup = TargetSetup::Rolling;
    const AttemptResult hit = gradeTarget(scoreAim(p.rect, true, {p.rect.u, p.rect.z}), 2500.f, p);
    CHECK(hit.success);
    CHECK(hit.skill == "shoot.target.tl");
    CHECK(hit.module == "shooting");
    CHECK(hit.verdict == Verdict::Perfect);
    CHECK(hit.tips.empty());
    CHECK_NEAR(metricOf(hit, "accuracy"), 100.f, 1e-3f);
    CHECK_NEAR(metricOf(hit, "speed"), 90.f, 1e-2f);
    CHECK(hit.hasRecordValue);

    const AttemptResult right =
        gradeTarget(scoreAim(p.rect, true, {p.rect.u + p.rect.hw + 300.f, p.rect.z}), 2500.f, p);
    CHECK(!right.success);
    CHECK(!right.tips.empty() && right.tips.front().code == "shooting.target_right");
    CHECK(metricOf(right, "offset_h") > 0.f);

    const AttemptResult left = gradeTarget(scoreAim(p.rect, true, {p.rect.u - p.rect.hw - 300.f, p.rect.z}), 2500.f, p);
    CHECK(!left.tips.empty() && left.tips.front().code == "shooting.target_left");

    const AttemptResult high = gradeTarget(scoreAim(p.rect, true, {p.rect.u, 900.f}), 2500.f, p);
    CHECK(!high.success);
    CHECK(hasTip(high, "shooting.target_high"));
    CHECK(high.verdict == Verdict::Fail); // over the crossbar

    const AttemptResult low = gradeTarget(scoreAim(p.rect, true, {p.rect.u, 120.f}), 2500.f, p);
    CHECK(hasTip(low, "shooting.target_low"));
    CHECK(low.verdict == Verdict::Ok); // a goal, but not in the zone

    const AttemptResult shortShot = gradeTarget(scoreAim(p.rect, false, {}), 800.f, p);
    CHECK(!shortShot.success && shortShot.score == 0.f && shortShot.verdict == Verdict::Fail);
    CHECK(shortShot.tips.front().code == "shooting.target_short");

    const AttemptResult edge = gradeTarget(scoreAim(p.rect, true, {p.rect.u + p.rect.hw * 0.9f, p.rect.z}), 2500.f, p);
    CHECK(edge.success && hasTip(edge, "shooting.target_edge"));
}

TEST(shooting_grade_target_min_speed_and_flick_title) {
    TargetGradeParams p;
    p.zone = Zone::Center;
    p.rect = zoneRect(Zone::Center, 0.2f);
    p.minSpeed = 2000.f;
    const AimResult in = scoreAim(p.rect, true, {p.rect.u, p.rect.z});
    const AttemptResult slow = gradeTarget(in, 1500.f, p);
    CHECK(!slow.success);
    CHECK(hasTip(slow, "shooting.target_slow"));
    CHECK(slow.verdict == Verdict::Ok);
    CHECK(gradeTarget(in, 2100.f, p).success);
    FlickReport r = goodReport();
    r.category = FlickType::Musty;
    p.flick = &r;
    CHECK(gradeTarget(in, 2100.f, p).title.find("Musty") != std::string::npos);
    CHECK(gradeTarget(in, 2100.f, p).skill == "shoot.target.center");
}

// ---- Grading: flicks ------------------------------------------------------------------------------------------
TEST(shooting_grade_flick_good) {
    FlickGradeParams p;
    p.minPower = flickMinPower(0.f);
    const AttemptResult a = gradeFlick(goodReport(), aimInGoal(), p, PlayerProfile{});
    CHECK(a.success);
    CHECK(a.skill == "shoot.flick.front");
    CHECK(a.verdict >= Verdict::Good);
    CHECK(a.score >= 70.f);
    CHECK_NEAR(a.valueForRecord, 90.f, 1e-2f);
    CHECK(a.hasRecordValue && !a.lowerIsBetter);
    CHECK_NEAR(metricOf(a, "power"), 90.f, 1e-2f);
    CHECK_NEAR(metricOf(a, "delay"), 0.2f, 1e-4f);
    CHECK(metricOf(a, "accuracy") >= 45.f);
    for (const auto& t : a.tips) CHECK(t.weight <= 3.f); // only minor remarks
}

TEST(shooting_grade_flick_weak_late_and_off_centre) {
    FlickGradeParams p;
    p.minPower = flickMinPower(0.5f);
    FlickReport late = goodReport();
    late.power = 1000.f;
    late.f.delay = 0.7f; // front flick band 0.10..0.40
    const AttemptResult a = gradeFlick(late, aimInGoal(), p, PlayerProfile{});
    CHECK(!a.success);
    CHECK(!a.tips.empty());
    CHECK(a.tips.front().code == "shooting.flick_late");
    CHECK(a.tips.front().text == "Lance le dodge plus tôt pour plus de puissance.");

    FlickReport side = goodReport();
    side.power = 1000.f;
    side.roofOffset = {10.f, 50.f, 0.f};
    const AttemptResult b = gradeFlick(side, aimInGoal(), p, PlayerProfile{});
    CHECK(b.tips.front().code == "shooting.flick_roof_side");
    CHECK(b.tips.front().text == "Garde la balle au centre du toit avant de flick.");

    FlickReport back = goodReport();
    back.power = 1000.f;
    back.roofOffset = {-40.f, 0.f, 0.f};
    CHECK(gradeFlick(back, aimInGoal(), p, PlayerProfile{}).tips.front().code == "shooting.flick_roof_back");

    FlickReport plain = goodReport();
    plain.power = 1000.f;
    CHECK(gradeFlick(plain, aimInGoal(), p, PlayerProfile{}).tips.front().code == "shooting.flick_weak");
    // Tips are sorted by weight.
    for (size_t i = 1; i < a.tips.size(); ++i) CHECK(a.tips[i - 1].weight >= a.tips[i].weight);
}

TEST(shooting_grade_flick_wrong_type_is_capped) {
    FlickGradeParams p;
    p.requested = static_cast<int>(FlickType::Musty);
    p.minPower = 1000.f;
    FlickReport r = goodReport();
    r.cls.base = FlickType::Backflip;
    r.category = FlickType::Backflip;
    r.f.dodgeFwd = -1.f;
    const AttemptResult a = gradeFlick(r, aimInGoal(), p, PlayerProfile{});
    CHECK(!a.success);
    CHECK(a.score <= 55.f);
    CHECK(a.verdict <= Verdict::Ok);
    CHECK(hasTip(a, "shooting.flick_musty_nose"));
    CHECK(a.skill == "shoot.flick.backflip");
    CHECK(a.title.find("demandé") != std::string::npos);

    p.requested = static_cast<int>(FlickType::Flick45);
    r.cls.base = FlickType::Diagonal;
    r.category = FlickType::Diagonal;
    r.f.dodgeSide = 0.7f;
    r.f.yawTurnDeg = 10.f;
    CHECK(hasTip(gradeFlick(r, aimInGoal(), p, PlayerProfile{}), "shooting.flick_under_rotated"));
    r.f.yawTurnDeg = 80.f;
    r.cls.base = FlickType::Flick90;
    CHECK(hasTip(gradeFlick(r, aimInGoal(), p, PlayerProfile{}), "shooting.flick_over_rotated"));

    p.requested = static_cast<int>(FlickType::Delayed);
    CHECK(hasTip(gradeFlick(goodReport(), aimInGoal(), p, PlayerProfile{}), "shooting.flick_not_delayed"));
    // The delayed tip quotes the configured threshold (rlt_sh_delayed_sec), French decimal comma.
    p.delayedSec = 0.75f;
    {
        const AttemptResult d = gradeFlick(goodReport(), aimInGoal(), p, PlayerProfile{});
        bool quoted = false;
        for (const auto& t : d.tips)
            if (t.code == "shooting.flick_not_delayed") quoted = t.text.find("(0,75 s ou plus)") != std::string::npos;
        CHECK(quoted);
    }
    p.delayedSec = 0.6f;

    // The diagonal dodge tip points the dodge at the target, not at the ball.
    p.requested = static_cast<int>(FlickType::Flick45);
    {
        FlickReport f = goodReport();
        f.f.dodgeSide = 0.f;
        const AttemptResult d = gradeFlick(f, aimInGoal(), p, PlayerProfile{});
        CHECK(hasTip(d, "shooting.flick_diagonal_dodge"));
        for (const auto& t : d.tips) CHECK(t.text.find("vers la balle") == std::string::npos);
    }

    // The requested type performed: success.
    p.requested = static_cast<int>(FlickType::Front);
    CHECK(gradeFlick(goodReport(), aimInGoal(), p, PlayerProfile{}).success);
}

TEST(shooting_grade_flick_no_contact_target_and_height) {
    FlickGradeParams p;
    FlickReport lost = goodReport();
    lost.contact = false;
    lost.lostBall = true;
    lost.power = 0.f;
    const AttemptResult a = gradeFlick(lost, scoreAim(goalMouthRect(), false, {}), p, PlayerProfile{});
    CHECK(!a.success && a.score <= 15.f && a.verdict == Verdict::Fail);
    CHECK(a.tips.front().code == "shooting.flick_lost_ball");
    CHECK(!a.hasRecordValue);
    lost.lostBall = false;
    CHECK(gradeFlick(lost, scoreAim(goalMouthRect(), false, {}), p, PlayerProfile{}).tips.front().code ==
          "shooting.flick_no_contact");

    // Off target: fails only when a shot on target is required.
    const AimResult wide = scoreAim(goalMouthRect(), true, {1500.f, 200.f});
    p.requireOnTarget = true;
    const AttemptResult off = gradeFlick(goodReport(), wide, p, PlayerProfile{});
    CHECK(!off.success && hasTip(off, "shooting.flick_off_target"));
    p.requireOnTarget = false;
    CHECK(gradeFlick(goodReport(), wide, p, PlayerProfile{}).success);

    // Popped too high: never reaches the goal plane.
    FlickReport high = goodReport();
    high.elevationDeg = 75.f;
    CHECK(
        hasTip(gradeFlick(high, scoreAim(goalMouthRect(), false, {}), p, PlayerProfile{}), "shooting.flick_too_high"));

    FlickReport fast = goodReport();
    fast.cls.base = FlickType::Musty;
    fast.category = FlickType::Musty;
    fast.supersonicStart = true;
    fast.f.dodgeFwd = -1.f;
    fast.f.pitchDeg = -70.f;
    CHECK(hasTip(gradeFlick(fast, aimInGoal(), p, PlayerProfile{}), "shooting.flick_supersonic"));

    FlickReport unsettled = goodReport();
    unsettled.carryTime = 0.05f;
    CHECK(hasTip(gradeFlick(unsettled, aimInGoal(), p, PlayerProfile{}), "shooting.flick_unsettled"));

    CHECK(gradeLostBall(static_cast<int>(FlickType::Musty), 1.f).skill == "shoot.flick.musty");
    const AttemptResult l = gradeLostBall(-1, 1.f);
    CHECK(l.skill == "shoot.flick.any" && !l.success && l.tips.front().code == "shooting.flick_lost_ball");
}

TEST(shooting_grade_flick_delayed_title_and_uncertain) {
    FlickReport r = goodReport();
    r.cls.delayed = true;
    r.cls.uncertain = true;
    r.category = FlickType::Delayed;
    r.f.delay = 0.8f;
    const AttemptResult a = gradeFlick(r, aimInGoal(), FlickGradeParams{}, PlayerProfile{});
    CHECK(a.skill == "shoot.flick.delayed");
    CHECK(a.title.find("(?)") != std::string::npos);
    CHECK(a.title.find("Flick avant") != std::string::npos);
    CHECK(!hasTip(a, "shooting.flick_late")); // a long delay is the point of a delayed flick
}

TEST(shooting_flick_min_power_by_level) {
    CHECK_NEAR(flickMinPower(0.f), 1300.f, 1e-3f);
    CHECK_NEAR(flickMinPower(1.f), 2400.f, 1e-3f);
    CHECK(flickMinPower(0.5f) > flickMinPower(0.2f));
}

// ---- Grading: power -----------------------------------------------------------------------------------------
TEST(shooting_grade_power) {
    PowerShot good;
    good.speed = 120.f / kUusToKph;
    good.carSpeed = 2200.f;
    good.flip = true;
    good.dodgeToTouch = 0.1f;
    good.onGoal = true;
    const AttemptResult a = gradePower(good, 90.f / kUusToKph, 3.f);
    CHECK(a.success && a.skill == "shoot.power");
    CHECK_NEAR(a.valueForRecord, 120.f, 0.01f);
    CHECK_NEAR(a.score, std::round(100.f * (120.f - 40.f) / 90.f), 1.f);
    CHECK(a.tips.empty());

    PowerShot weak;
    weak.speed = 60.f / kUusToKph;
    weak.carSpeed = 900.f;
    const AttemptResult b = gradePower(weak, 90.f / kUusToKph, 3.f);
    CHECK(!b.success && b.verdict <= Verdict::Ok);
    CHECK(b.tips.front().code == "shooting.power_no_flip");
    CHECK(hasTip(b, "shooting.power_slow_car"));
    CHECK(hasTip(b, "shooting.power_off_target"));

    PowerShot early = weak;
    early.flip = true;
    early.dodgeToTouch = 0.45f;
    early.carSpeed = 2000.f;
    early.sideHit = true;
    early.onGoal = true;
    const AttemptResult c = gradePower(early, 90.f / kUusToKph, 3.f);
    CHECK(c.tips.front().code == "shooting.power_flip_early");
    CHECK(hasTip(c, "shooting.power_side_hit"));
    CHECK(powerScore01(40.f) == 0.f && powerScore01(130.f) == 1.f);
}

// ---- How-to texts with the player's buttons ---------------------------------------------------------------
TEST(shooting_howto_uses_player_buttons) {
    PlayerProfile pad;
    pad.valid = true;
    pad.usingGamepad = true;
    pad.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"AirRollLeft", "XboxTypeS_LeftShoulder"}};
    const KeyStyle xb = KeyStyle::Auto;
    const std::string h = flickHowTo(FlickType::Flick45, pad, xb, 0.6f);
    CHECK(h.find("Saute (A)") != std::string::npos);
    // Only the directional air roll is bound: that is the one shown.
    CHECK(airRollPrompt(pad, xb) == actionPrompt(pad, GameAction::AirRollLeft));
    CHECK(airRollPrompt(pad, xb).find("Air roll gauche (") == 0);
    CHECK(h.find(airRollPrompt(pad, xb)) != std::string::npos);
    CHECK(flickCue(FlickType::Musty, pad, xb, 0.6f) ==
          actionPrompt(pad, GameAction::Jump) + ", nez en bas, puis backflip");
    CHECK(flickCue(FlickType::Breezi, pad, xb, 0.6f).find(airRollPrompt(pad, xb)) != std::string::npos);
    for (int i = 0; i < kFlickTypeCount; ++i) CHECK(flickCue(static_cast<FlickType>(i), pad, xb, 0.6f).size() < 80);
    const PlayerProfile none;
    CHECK(flickCue(FlickType::Front, none, xb, 0.6f) == "Saut, puis dodge avant");
    const std::string n = flickHowTo(FlickType::Musty, none, xb, 0.6f);
    CHECK(n.find("Saute,") != std::string::npos);
    CHECK(n.find("()") == std::string::npos);
    for (int i = 0; i < kFlickTypeCount; ++i) {
        const std::string t = flickHowTo(static_cast<FlickType>(i), pad, xb, 0.6f);
        CHECK(!t.empty());
        CHECK(t.find("\xE2\x80\x94") == std::string::npos); // no em dash
        // With the ball on the roof the dodge direction decides where the ball goes: never "vers la balle".
        CHECK_MSG(t.find("vers la balle") == std::string::npos, t);
    }
    // The delayed threshold is quoted from the setting.
    CHECK(flickCue(FlickType::Delayed, pad, xb, 0.8f).find("garde la balle 0,8 s") != std::string::npos);
    CHECK(flickHowTo(FlickType::Delayed, pad, xb, 0.75f).find("au moins 0,75 s") != std::string::npos);
    CHECK(frenchDecimal(0.6f) == "0,6");
    CHECK(frenchDecimal(1.f) == "1");
    CHECK(frenchDecimal(1.25f) == "1,25");
    CHECK(frenchDecimal(-0.001f) == "0");
}

TEST(shooting_howto_honours_playstation_style) {
    PlayerProfile pad;
    pad.valid = true;
    pad.usingGamepad = true;
    pad.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"AirRoll", "XboxTypeS_LeftShoulder"}};
    const KeyStyle ps = KeyStyle::PlayStation;
    const std::string cue = flickCue(FlickType::Musty, pad, ps, 0.6f);
    CHECK_MSG(cue == actionPrompt(pad, GameAction::Jump, ps) + ", nez en bas, puis backflip", cue);
    CHECK_MSG(cue.find("(A)") == std::string::npos, cue);
    CHECK(actionPrompt(pad, GameAction::Jump, ps) != actionPrompt(pad, GameAction::Jump, KeyStyle::Xbox));
    const std::string how = flickHowTo(FlickType::Breezi, pad, ps, 0.6f);
    CHECK(how.find(airRollPrompt(pad, ps)) != std::string::npos);
    CHECK(airRollPrompt(pad, ps) == actionPrompt(pad, GameAction::AirRoll, ps));
    CHECK(how.find("(LB)") == std::string::npos);
    CHECK(flickCue(FlickType::Breezi, pad, ps, 0.6f).find(airRollPrompt(pad, ps)) != std::string::npos);
}

// ---- Statistics ---------------------------------------------------------------------------------------------
TEST(shooting_stats_records_streaks_and_history) {
    ShootingStats st;
    auto f1 = st.addFlick(FlickType::Musty, 2000.f, 60.f, true);
    CHECK(!f1.powerAll && f1.powerSession); // the first one is not announced as a record
    auto f2 = st.addFlick(FlickType::Musty, 2200.f, 50.f, false);
    CHECK(f2.powerAll);
    auto f3 = st.addFlick(FlickType::Musty, 2100.f, 80.f, true);
    CHECK(!f3.powerAll && f3.accuracyAll);
    const FlickRecord& m = st.flickAll[static_cast<size_t>(FlickType::Musty)];
    CHECK_NEAR(m.bestPower, 2200.f, 1e-3f);
    CHECK_NEAR(m.bestAccuracy, 80.f, 1e-3f);
    CHECK_EQ(m.count, 3);
    CHECK_EQ(m.successes, 2);

    CHECK(!st.addTarget(Zone::TopLeft, true, 90.f));
    st.addTarget(Zone::TopLeft, true, 80.f);
    st.addTarget(Zone::TopLeft, false, 30.f);
    st.addTarget(Zone::TopRight, true, 75.f);
    CHECK_EQ(st.streak, 1);
    CHECK_EQ(st.bestStreakSession, 2);
    CHECK_EQ(st.bestStreakAll, 2);
    st.addTarget(Zone::TopRight, true, 75.f);
    CHECK(st.addTarget(Zone::TopRight, true, 75.f)); // 3 in a row beats 2
    const ZoneStats& z = st.zoneSession[0];
    CHECK_EQ(z.attempts, 3);
    CHECK_EQ(z.hits, 2);
    CHECK_NEAR(z.hitRate(), 2.f / 3.f, 1e-4f);
    CHECK_NEAR(z.avgScore(), 200.f / 3.f, 1e-3f);

    auto p1 = st.addPower(3000.f, true);
    CHECK(!p1.all && p1.session);
    CHECK(st.addPower(3100.f, true).all);
    for (int i = 0; i < 100; ++i) st.addPower(1000.f, false);
    CHECK_EQ(st.shotPowersKph.size(), ShootingStats::kHistory);
    CHECK_EQ(st.powerAll.count, 102);

    st.resetSession();
    CHECK_EQ(st.zoneSession[0].attempts, 0);
    CHECK_EQ(st.zoneAll[0].attempts, 3);
    CHECK(st.shotPowersKph.empty());
    st.resetAll();
    CHECK_EQ(st.zoneAll[0].attempts, 0);
    CHECK_EQ(st.powerAll.best, 0.f);
}

TEST(shooting_stats_save_load_roundtrip_and_sanitize) {
    ShootingStats st;
    st.addFlick(FlickType::Flick45, 2600.f, 77.f, true);
    st.addFlick(FlickType::Breezi, 2100.f, 40.f, false);
    st.addTarget(Zone::LowRight, true, 88.f);
    st.addTarget(Zone::LowRight, false, 20.f);
    st.addPower(3300.f, true);
    Store store;
    st.save(store);
    for (const auto& kv : store.raw()) CHECK(kv.first.rfind("shooting.", 0) == 0);
    ShootingStats back;
    back.load(store);
    CHECK_NEAR(back.flickAll[static_cast<size_t>(FlickType::Flick45)].bestPower, 2600.f, 0.01f);
    CHECK_NEAR(back.flickAll[static_cast<size_t>(FlickType::Flick45)].bestAccuracy, 77.f, 0.01f);
    CHECK_EQ(back.flickAll[static_cast<size_t>(FlickType::Breezi)].count, 1);
    CHECK_EQ(back.zoneAll[3].attempts, 2);
    CHECK_EQ(back.zoneAll[3].hits, 1);
    CHECK_NEAR(back.zoneAll[3].scoreSum, 108.f, 0.01f);
    CHECK_EQ(back.zonePrev[3].attempts, 2); // this session becomes the "previous session"
    CHECK_EQ(back.zoneSession[3].attempts, 0);
    CHECK_NEAR(back.powerAll.best, 3300.f, 0.01f);
    // A new session without targets keeps the loaded previous session.
    Store again;
    back.save(again);
    ShootingStats third;
    third.load(again);
    CHECK_EQ(third.zonePrev[3].attempts, 2);

    Store bad;
    bad.setString("shooting.zone.tl.attempts", "-5");
    bad.setString("shooting.zone.tl.hits", "999");
    bad.setString("shooting.flick.45.best", "nan");
    bad.setString("shooting.power.best", "1e30");
    ShootingStats s2;
    s2.load(bad);
    CHECK_EQ(s2.zoneAll[0].attempts, 0);
    CHECK_EQ(s2.zoneAll[0].hits, 0);
    CHECK(std::isfinite(s2.flickAll[static_cast<size_t>(FlickType::Flick45)].bestPower));
    CHECK(s2.powerAll.best <= 10000.f);
}
