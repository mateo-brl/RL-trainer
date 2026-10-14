// Pure analysis: metrics at contact, contact part/height, touch type, control window, score, diagnosis, text.

#include <cmath>

#include "tests/firsttouch/ft_test_util.h"

using namespace rlt;
using namespace rlt::ft;
using namespace fttest;

namespace {

TouchMetrics groundMetrics(float carSpeed, float ballSpeed) {
    const Scenario sc = groundPickup(carSpeed, ballSpeed, carSpeed, ballSpeed);
    return computeMetrics(buildTouch(sc), TouchType::Ground);
}

ControlSummary fullControl() {
    ControlSummary c;
    c.ticks = 120;
    c.controlledTicks = 120;
    c.fraction = 1.f;
    c.controlled = true;
    return c;
}

ControlSummary noControl() {
    ControlSummary c;
    c.ticks = 120;
    c.fraction = 0.f;
    c.controlled = false;
    c.lost = true;
    return c;
}

TouchMetrics cleanMetrics() {
    TouchMetrics m;
    m.valid = true;
    m.relSpeed = 50.f;
    m.contactErrDeg = 5.f;
    m.contactDir = Vec3{-0.9f, 0.f, -0.43f};
    m.contactUp = -0.43f;
    m.part = CarPart::Nose;
    return m;
}

std::vector<Tip> diag(TouchType type, const TouchMetrics& m, const ControlSummary& c) {
    const ScoreParams sp;
    const TouchScore s = scoreTouch(m, c, sp);
    return diagnose(type, m, c, s, sp, DiagnosisParams{}, gamepadProfile(), KeyStyle::Auto);
}

// Ground touch at the nose of a car driving +x at `carVx`, ball velocity `ballVel`, metrics computed from the
// synthetic pre/post states (not set by hand), then diagnosed with full control.
struct Probe {
    TouchMetrics m;
    std::vector<Tip> tips;
};
Probe probeGround(float carVx, Vec3 ballVel) {
    Scenario sc = groundPickup(carVx, 0.f, carVx, 0.f);
    sc.ball.vel = ballVel;
    sc.postBallVel = ballVel;
    Probe p;
    p.m = computeMetrics(buildTouch(sc), TouchType::Ground);
    p.tips = diag(TouchType::Ground, p.m, fullControl());
    return p;
}

} // namespace

// ---- Metrics ----------------------------------------------------------------------------------------------

TEST(ft_metrics_speed_diff_sign_car_faster_is_positive) {
    const TouchMetrics m = groundMetrics(1000.f, 800.f);
    CHECK(m.valid);
    CHECK_NEAR(m.speedDiff, 200.f, 1e-2f);
    CHECK_NEAR(m.relSpeed, 200.f, 1e-2f);
    CHECK_NEAR(m.along, 200.f, 1e-2f);
    CHECK_NEAR(m.cross, 0.f, 1e-2f);
    CHECK_NEAR(m.vertDiff, 0.f, 1e-3f);
    CHECK(!m.ballComing);

    const TouchMetrics slow = groundMetrics(600.f, 800.f);
    CHECK_NEAR(slow.speedDiff, -200.f, 1e-2f);
    CHECK_NEAR(slow.along, -200.f, 1e-2f);
}

TEST(ft_metrics_head_on_parity_diff_zero_but_relative_speed_high) {
    Scenario sc = groundPickup(1000.f, -1000.f, 0.f, 0.f);
    const TouchMetrics m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK_NEAR(m.speedDiff, 0.f, 1e-2f);       // FTT parity: difference of magnitudes is blind here
    CHECK_NEAR(m.relSpeed, 2000.f, 1e-1f);     // the game's impulse sees 2000 uu/s
    CHECK(m.ballComing);
    CHECK(m.along < -1900.f);
}

TEST(ft_metrics_crossing_ball_measures_cross_component) {
    Scenario sc = groundPickup(800.f, 0.f, 800.f, 0.f);
    sc.ball.vel = Vec3{0.f, 800.f, 0.f};
    const TouchMetrics m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK_NEAR(m.speedDiff, 0.f, 1e-2f);
    CHECK_NEAR(m.along, -800.f, 1.f);
    CHECK_NEAR(m.cross, 800.f, 1.f);
    CHECK_NEAR(m.approachAngleDeg, 90.f, 0.1f);
}

TEST(ft_metrics_nose_contact_geometry) {
    const TouchMetrics m = groundMetrics(1000.f, 900.f);
    CHECK(!m.fromHitInfo);
    CHECK(m.part == CarPart::Nose);
    CHECK(m.height == ContactHeight::Low);
    CHECK_NEAR(m.contactDir.x, -0.9125f, 0.01f);
    CHECK_NEAR(m.contactDir.z, -0.409f, 0.01f);
    CHECK_NEAR(length(m.contactDir), 1.f, 1e-4f);
    CHECK_NEAR(m.lateralOffset, 0.f, 1e-3f);
    CHECK(m.contactErrDeg < 5.f);              // meets the incoming face slightly below centre: ideal
    CHECK(m.closingSpeed > 0.f);               // car faster: closing
    CHECK(m.heatX < -0.8f);                    // car side of the ball in the approach frame
    CHECK(m.heatZ < 0.f);
}

TEST(ft_metrics_roof_side_back_under_parts) {
    const CarState car = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{});
    // Roof: ball centre right above the hitbox centre.
    {
        TouchEvent t;
        t.pre = makeSnap(10, car, makeBall(Vec3{13.88f, 0.f, 148.f}, Vec3{}));
        t.post = t.pre;
        const TouchMetrics m = computeMetrics(t, TouchType::Bounce);
        CHECK(m.part == CarPart::Roof);
        CHECK(m.height == ContactHeight::Low);
        CHECK_NEAR(m.contactDir.z, -1.f, 1e-3f);
    }
    // Side (right).
    {
        TouchEvent t;
        t.pre = makeSnap(10, car, makeBall(Vec3{13.88f, 125.37f, kBallZ}, Vec3{}));
        t.post = t.pre;
        const TouchMetrics m = computeMetrics(t, TouchType::Ground);
        CHECK(m.part == CarPart::Side);
        CHECK(m.lateralOffset > 100.f);
    }
    // Side (left), car facing +y: right axis is -x.
    {
        const CarState c2 = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}, 90.f);
        TouchEvent t;
        t.pre = makeSnap(10, c2, makeBall(Vec3{125.37f, 13.88f, kBallZ}, Vec3{}));
        t.post = t.pre;
        const TouchMetrics m = computeMetrics(t, TouchType::Ground);
        CHECK(m.part == CarPart::Side);
        CHECK(m.lateralOffset < -100.f);
    }
    // Back.
    {
        TouchEvent t;
        t.pre = makeSnap(10, car, makeBall(Vec3{-128.4f, 0.f, kBallZ}, Vec3{}));
        t.post = t.pre;
        const TouchMetrics m = computeMetrics(t, TouchType::Ground);
        CHECK(m.part == CarPart::Back);
    }
    // Under: car in the air above the ball, contact at the top of the ball.
    {
        const CarState air = airCar(Vec3{0.f, 0.f, 300.f}, Vec3{});
        TouchEvent t;
        t.pre = makeSnap(10, air, makeBall(Vec3{13.88f, 0.f, 210.f}, Vec3{}));
        t.post = t.pre;
        const TouchMetrics m = computeMetrics(t, TouchType::Air);
        CHECK(m.part == CarPart::Under);
        CHECK(m.height == ContactHeight::High);
    }
}

TEST(ft_metrics_contact_height_middle) {
    // Car in the air at ball height: hits the ball at its equator.
    const CarState air = airCar(Vec3{0.f, 0.f, 200.f}, Vec3{500.f, 0.f, 0.f});
    TouchEvent t;
    const float hbZ = 200.f + 20.75f;
    t.pre = makeSnap(10, air, makeBall(Vec3{72.88f + 91.f, 0.f, hbZ}, Vec3{}));
    t.post = t.pre;
    const TouchMetrics m = computeMetrics(t, TouchType::Air);
    CHECK(m.height == ContactHeight::Middle);
    CHECK(m.part == CarPart::Nose);
}

TEST(ft_metrics_uses_hit_info_when_plausible) {
    Scenario sc = groundPickup(1000.f, 900.f, 1000.f, 960.f);
    sc.hasHit = true;
    sc.hitLocation = sc.ball.pos + Vec3{0.f, 0.f, -91.25f};
    TouchMetrics m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK(m.fromHitInfo);
    CHECK_NEAR(m.contactDir.z, -1.f, 1e-4f);
    CHECK(m.part == CarPart::Roof);

    // Implausible location and no normal: hitbox geometry.
    sc.hitLocation = sc.ball.pos + Vec3{1000.f, 0.f, 0.f};
    sc.hitNormal = Vec3{};
    m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK(!m.fromHitInfo);
    CHECK(m.part == CarPart::Nose);

    // Implausible location but valid normal (car -> ball).
    sc.hitNormal = Vec3{0.f, 0.f, 1.f};
    m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK(m.fromHitInfo);
    CHECK_NEAR(m.contactDir.z, -1.f, 1e-4f);
}

TEST(ft_metrics_post_touch_pop_and_separation) {
    Scenario sc = groundPickup(1000.f, 900.f, 1000.f, 1000.f);
    sc.postBallVel = Vec3{1100.f, 0.f, 400.f};
    sc.postCarVel = Vec3{1000.f, 0.f, 0.f};
    const TouchMetrics m = computeMetrics(buildTouch(sc), TouchType::Ground);
    CHECK_NEAR(m.ballPop, 400.f, 1e-2f);
    CHECK_NEAR(m.separation, std::sqrt(100.f * 100.f + 400.f * 400.f), 0.5f);
}

TEST(ft_metrics_boost_at_contact_needs_boost_left) {
    Scenario sc = groundPickup(1000.f, 900.f, 1000.f, 960.f);
    TouchEvent t = buildTouch(sc);
    t.pre.in.boost = true;
    CHECK(computeMetrics(t, TouchType::Ground).boostAtContact);
    t.pre.car.boost = 0.f;
    CHECK(!computeMetrics(t, TouchType::Ground).boostAtContact);
}

TEST(ft_metrics_invalid_states) {
    TouchEvent t;
    CHECK(!computeMetrics(t, TouchType::Ground).valid);
    t.pre.car = groundCar(Vec3{}, Vec3{});
    CHECK(!computeMetrics(t, TouchType::Ground).valid);  // ball invalid
}

TEST(ft_metrics_degenerate_ball_inside_hitbox) {
    TouchEvent t;
    t.pre = makeSnap(1, groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{}), makeBall(Vec3{0.f, 0.f, kCarZ + 20.f}, Vec3{}));
    t.post = t.pre;
    const TouchMetrics m = computeMetrics(t, TouchType::Ground);
    CHECK(m.valid);
    CHECK(std::isfinite(m.contactDir.x) && std::isfinite(m.contactErrDeg));
    CHECK_NEAR(length(m.contactDir), 1.f, 1e-3f);
}

// ---- Type classification ----------------------------------------------------------------------------------

TEST(ft_classify_touch_types) {
    const CarState car = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{1000.f, 0.f, 0.f});
    Snapshot s = makeSnap(1, car, makeBall(Vec3{156.f, 0.f, kBallZ}, Vec3{800.f, 0.f, 0.f}));
    CHECK(classifyTouch(s, true, 1 << 30, false) == TouchType::Ground);
    CHECK(classifyTouch(s, true, 1 << 30, true) == TouchType::Bounce);     // just bounced
    CHECK(classifyTouch(s, false, 2, false) == TouchType::Dribble);        // continued contact
    CHECK(classifyTouch(s, true, 60, false) == TouchType::Dribble);        // follow-up contact (< 1 s)
    CHECK(classifyTouch(s, true, 121, false) == TouchType::Ground);        // new first touch
    CHECK(classifyTouch(s, true, 60, false, 40) == TouchType::Ground);     // shorter gap setting

    s.ball.pos.z = 180.f;
    s.ball.vel.z = -400.f;
    CHECK(classifyTouch(s, true, 1 << 30, false) == TouchType::Bounce);    // descending near ground

    // Ball on the roof: dribble right after a touch, catch (bounce) otherwise.
    Snapshot roof = makeSnap(1, car, makeBall(Vec3{13.88f, 0.f, 148.f}, Vec3{1000.f, 0.f, -200.f}));
    CHECK(classifyTouch(roof, true, 30, false) == TouchType::Dribble);
    CHECK(classifyTouch(roof, true, 1 << 30, false) == TouchType::Bounce);

    // Air: car airborne, ball high.
    Snapshot air = makeSnap(1, airCar(Vec3{0.f, 0.f, 500.f}, Vec3{0.f, 0.f, 300.f}), makeBall(Vec3{50.f, 0.f, 640.f}, Vec3{}));
    CHECK(classifyTouch(air, true, 1 << 30, false) == TouchType::Air);
    // Car jumping into a rolling ball: classified by the ball (ground).
    Snapshot jump = makeSnap(1, airCar(Vec3{0.f, 0.f, 60.f}, Vec3{900.f, 0.f, 200.f}), makeBall(Vec3{156.f, 0.f, kBallZ}, Vec3{800.f, 0.f, 0.f}));
    CHECK(classifyTouch(jump, true, 1 << 30, false) == TouchType::Ground);

    // Wall: engine flag, or wheels down with a horizontal up axis.
    CarState wall = car;
    wall.onWall = true;
    CHECK(classifyTouch(makeSnap(1, wall, s.ball), true, 1 << 30, false) == TouchType::Wall);
    CarState wall2 = groundCar(Vec3{4000.f, 0.f, 500.f}, Vec3{}, 0.f);
    wall2.rot = Rot{0, 0, 16384};
    wall2.basis = basisFromRot(wall2.rot);
    CHECK(carOnWall(wall2));
    CHECK(classifyTouch(makeSnap(1, wall2, s.ball), true, 1 << 30, false) == TouchType::Wall);
}

TEST(ft_classify_ball_carried_on_roof_is_dribble_not_catch) {
    const CarState car = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{1000.f, 0.f, 0.f});
    // Ball put on the roof ("ballontop") or carried: it moves with the car.
    const Snapshot carry = makeSnap(1, car, makeBall(Vec3{13.88f, 0.f, 148.f}, Vec3{1000.f, 0.f, -40.f}));
    CHECK(ballOnRoof(carry.car, carry.ball));
    CHECK(ballCarriedOnRoof(carry.car, carry.ball));
    CHECK(classifyTouch(carry, true, 1 << 30, false) == TouchType::Dribble);
    // Ball falling onto the roof: a catch, graded as a bounce.
    Snapshot fall = carry;
    fall.ball.vel.z = -400.f;
    CHECK(ballOnRoof(fall.car, fall.ball));
    CHECK(!ballCarriedOnRoof(fall.car, fall.ball));
    CHECK(classifyTouch(fall, true, 1 << 30, false) == TouchType::Bounce);
    // Not on the roof: ball at the nose, ball far above, ball behind the car.
    CHECK(!ballOnRoof(car, makeBall(Vec3{156.f, 0.f, kBallZ}, Vec3{})));
    CHECK(!ballOnRoof(car, makeBall(Vec3{13.88f, 0.f, 400.f}, Vec3{})));
    CHECK(!ballOnRoof(car, makeBall(Vec3{-200.f, 0.f, 148.f}, Vec3{})));
    BallState invalid;
    CHECK(!ballCarriedOnRoof(car, invalid));
    // Airborne car rising into a ball above its roof at nearly the same speed: an air touch, not a carry.
    const Snapshot rising = makeSnap(1, airCar(Vec3{0.f, 0.f, 500.f}, Vec3{0.f, 0.f, 300.f}),
                                     makeBall(Vec3{13.88f, 0.f, 630.1f}, Vec3{0.f, 0.f, 200.f}));
    CHECK(ballOnRoof(rising.car, rising.ball));
    CHECK(!ballCarriedOnRoof(rising.car, rising.ball));
    CHECK(classifyTouch(rising, true, 1 << 30, false) == TouchType::Air);
}

TEST(ft_ball_bounced_recently_scans_history) {
    TickHistory h(600);
    // Ball falls, bounces at frame 40, rises.
    for (int f = 0; f <= 60; ++f) {
        Snapshot s = makeSnap(f, groundCar(Vec3{}, Vec3{}), makeBall(Vec3{500.f, 0.f, kBallZ + 10.f}, Vec3{}));
        s.ball.vel.z = f < 40 ? -500.f : 300.f;
        s.ball.pos.z = f < 40 ? kBallZ + (40 - f) * 4.f : kBallZ + (f - 40) * 2.5f;
        h.push(s);
    }
    CHECK(ballBouncedRecently(h, 45));
    CHECK(ballBouncedRecently(h, 40));
    CHECK(!ballBouncedRecently(h, 30));
    CHECK(!ballBouncedRecently(h, 60, 10));  // bounce older than the window
}

// ---- Control window ---------------------------------------------------------------------------------------

TEST(ft_control_window_success_when_ball_stays) {
    const Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1040.f);
    const TouchEvent t = buildTouch(sc);
    ControlWindow w;
    w.start(t, 120, 350.f, 450.f, 0.6f);
    CHECK(w.active());
    CHECK(w.startFrame() == sc.frame);
    Snapshot s = t.post;
    bool done = false;
    int n = 0;
    while (!done && n < 200) {
        s = advance(s);
        done = w.feed(s);
        ++n;
    }
    CHECK(done);
    CHECK(n == 118);  // post1 and post already counted
    const ControlSummary c = w.summary();
    CHECK_NEAR(c.fraction, 1.f, 1e-4f);
    CHECK(c.controlled);
    CHECK(!c.lost);
    CHECK(c.maxDistance < 350.f);
}

TEST(ft_control_window_failure_when_ball_runs_away) {
    const Scenario sc = groundPickup(1600.f, 800.f, 1500.f, 2100.f);
    const TouchEvent t = buildTouch(sc);
    ControlWindow w;
    w.start(t, 120, 350.f, 450.f, 0.6f);
    Snapshot s = t.post;
    for (int i = 0; i < 130; ++i) {
        s = advance(s);
        w.feed(s);
    }
    const ControlSummary c = w.summary();
    CHECK(w.complete());
    CHECK(c.fraction < 0.1f);
    CHECK(!c.controlled);
    CHECK(c.lost);
}

TEST(ft_control_window_partial_and_regained) {
    // Ball escapes for a while then comes back: not "lost" at the end.
    const Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1000.f);
    const TouchEvent t = buildTouch(sc);
    ControlWindow w;
    w.start(t, 120, 350.f, 450.f, 0.6f);
    Snapshot s = t.post;
    for (int i = 0; i < 118; ++i) {
        s = advance(s);
        Snapshot fed = s;
        if (i >= 20 && i < 60) fed.ball.vel.x = 1600.f;  // relative speed too high for 40 ticks
        w.feed(fed);
    }
    const ControlSummary c = w.summary();
    CHECK(c.fraction > 0.6f && c.fraction < 0.75f);
    CHECK(c.controlled);
    CHECK(!c.lost);
}

TEST(ft_control_window_ignores_paused_invalid_and_old_frames) {
    const Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1000.f);
    const TouchEvent t = buildTouch(sc);
    ControlWindow w;
    w.start(t, 60, 350.f, 450.f, 0.6f);
    const int base = w.summary().ticks;
    CHECK(base == 2);
    CHECK(!w.feed(t.post));  // same frame: ignored
    CHECK(w.summary().ticks == base);
    Snapshot s = advance(t.post);
    s.paused = true;
    w.feed(s);
    CHECK(w.summary().ticks == base);
    s = advance(s);
    s.paused = false;
    s.car.valid = false;
    w.feed(s);
    CHECK(w.summary().ticks == base);
    s = advance(s);
    s.car.valid = true;
    w.feed(s);
    CHECK(w.summary().ticks == base + 1);
    // A jump in frames completes the window.
    s.frame += 500;
    CHECK(w.feed(s));
    CHECK(w.complete());
    w.cancel();
    CHECK(!w.active());
    CHECK(!w.feed(advance(s)));
}

TEST(ft_control_window_pop_height_and_steer) {
    Scenario sc = groundPickup(1000.f, 950.f, 1000.f, 1000.f);
    sc.postBallVel = Vec3{1000.f, 0.f, 500.f};
    sc.ballGravity = true;
    const TouchEvent t = buildTouch(sc);
    ControlWindow w;
    w.start(t, 120, 350.f, 450.f, 0.6f);
    Snapshot s = t.post;
    for (int i = 0; i < 118; ++i) {
        s = advance(s, 1, true);
        s.in.steer = i < 30 ? 0.9f : 0.f;
        w.feed(s);
    }
    const ControlSummary c = w.summary();
    CHECK(c.popHeight > 150.f && c.popHeight < 200.f);  // v^2 / 2g = 192 uu
    CHECK_NEAR(c.maxSteerAfter, 0.9f, 1e-4f);
}

// ---- Score ------------------------------------------------------------------------------------------------

TEST(ft_score_speed_subscore_is_monotonic) {
    const ScoreParams sp;
    float prev = 2.f;
    for (float rel : {0.f, 100.f, 150.f, 200.f, 300.f, 450.f, 600.f, 900.f}) {
        TouchMetrics m = cleanMetrics();
        m.relSpeed = rel;
        const TouchScore s = scoreTouch(m, fullControl(), sp);
        CHECK(s.sSpeed <= prev);
        if (rel > 150.f && rel <= 600.f) CHECK(s.sSpeed < prev);
        prev = s.sSpeed;
    }
    TouchMetrics a = cleanMetrics(), b = cleanMetrics();
    a.relSpeed = 180.f;
    b.relSpeed = 420.f;
    CHECK(scoreTouch(a, fullControl(), sp).score > scoreTouch(b, fullControl(), sp).score);
    CHECK_NEAR(scoreTouch(cleanMetrics(), fullControl(), sp).score, 100.f, 1e-4f);
}

TEST(ft_score_contact_and_control_parts) {
    const ScoreParams sp;
    TouchMetrics m = cleanMetrics();
    m.contactErrDeg = 50.f;
    const TouchScore s = scoreTouch(m, fullControl(), sp);
    CHECK_NEAR(s.sContact, 0.5f, 1e-4f);
    CHECK_NEAR(s.score, std::round(35.f + 12.5f + 40.f), 1e-3f);

    ControlSummary half = fullControl();
    half.fraction = 0.5f;
    CHECK_NEAR(scoreTouch(cleanMetrics(), half, sp).score, 80.f, 1e-3f);

    // Lost ball caps the score.
    CHECK(scoreTouch(cleanMetrics(), noControl(), sp).score <= 40.f);

    // Weights are normalised: doubling all of them changes nothing.
    ScoreParams sp2 = sp;
    sp2.wSpeed = 70.f;
    sp2.wContact = 50.f;
    sp2.wControl = 80.f;
    TouchMetrics x = cleanMetrics();
    x.relSpeed = 300.f;
    CHECK_NEAR(scoreTouch(x, half, sp).score, scoreTouch(x, half, sp2).score, 1e-3f);
    // Degenerate weights fall back to the defaults.
    ScoreParams zero = sp;
    zero.wSpeed = zero.wContact = zero.wControl = 0.f;
    CHECK_NEAR(scoreTouch(x, half, zero).score, scoreTouch(x, half, sp).score, 1e-3f);
    CHECK(verdictFromScore(scoreTouch(cleanMetrics(), fullControl(), sp).score) == Verdict::Perfect);
}

TEST(ft_score_headlines) {
    CHECK(std::string(scoreHeadline(95.f)) == "Touche parfaite !");
    CHECK(std::string(scoreHeadline(86.f)) == "Belle touche !");
    CHECK(std::string(scoreHeadline(72.f)) == "Bonne touche");
    CHECK(std::string(scoreHeadline(50.f)) == "Touche correcte");
    CHECK(std::string(scoreHeadline(10.f)) == "Touche ratée");
}

// ---- Diagnosis --------------------------------------------------------------------------------------------

TEST(ft_diagnose_perfect_touch_has_no_tip) {
    CHECK(diag(TouchType::Ground, cleanMetrics(), fullControl()).empty());
    CHECK(diag(TouchType::Dribble, cleanMetrics(), noControl()).empty());
    TouchMetrics invalid;
    CHECK(diag(TouchType::Ground, invalid, noControl()).empty());
}

// Tips are plain words: the size of the mistake is said ("beaucoup trop vite"), the measure stays in the tab.
TEST(ft_diagnose_too_fast_uses_real_boost_button_and_plain_words) {
    TouchMetrics m = cleanMetrics();
    m.along = 500.f;
    m.relSpeed = 500.f;
    auto tips = diag(TouchType::Ground, m, fullControl());
    CHECK(hasTip(tips, "ft.too_fast"));
    CHECK(contains(tips.front().text, "Boost (B)"));
    CHECK(contains(tips.front().text, "Tu arrives trop vite"));
    CHECK(!hasTip(tips, "ft.too_slow"));
    m.along = 700.f;
    m.relSpeed = 700.f;
    tips = diag(TouchType::Ground, m, fullControl());
    CHECK(!tips.empty() && contains(tips.front().text, "beaucoup trop vite"));
}

TEST(ft_diagnose_tips_have_no_numbers) {
    const std::vector<TouchMetrics> cases = [] {
        std::vector<TouchMetrics> v;
        TouchMetrics fast = cleanMetrics();
        fast.along = 700.f;
        fast.relSpeed = 700.f;
        fast.boostAtContact = true;
        v.push_back(fast);
        TouchMetrics slow = cleanMetrics();
        slow.along = -600.f;
        slow.relSpeed = 600.f;
        slow.ballAway = true;
        v.push_back(slow);
        TouchMetrics head = slow;
        head.ballAway = false;
        head.ballComing = true;
        v.push_back(head);
        TouchMetrics side = cleanMetrics();
        side.cross = 500.f;
        side.lateralOffset = 80.f;
        v.push_back(side);
        return v;
    }();
    for (const auto& m : cases)
        for (TouchType t : {TouchType::Ground, TouchType::Bounce, TouchType::Air})
            for (const auto& tip : diag(t, m, noControl())) {
                CHECK_MSG(tip.text.find_first_of("0123456789") == std::string::npos, tip.text);
                CHECK_MSG(!contains(tip.text, "km/h") && !contains(tip.text, "°"), tip.text);
                CHECK_MSG(noEmDash(tip.text), tip.text);
            }
}

TEST(ft_diagnose_too_slow_and_head_on) {
    TouchMetrics m = cleanMetrics();
    m.along = -500.f;
    m.relSpeed = 500.f;
    m.ballAway = true;
    auto tips = diag(TouchType::Ground, m, fullControl());
    CHECK(hasTip(tips, "ft.too_slow"));
    CHECK(contains(tips.front().text, "Accélère"));
    m.ballAway = false;
    m.ballComing = true;
    tips = diag(TouchType::Ground, m, fullControl());
    CHECK(hasTip(tips, "ft.head_on"));
    CHECK(!hasTip(tips, "ft.too_slow"));
    CHECK(contains(tips.front().text, "freine"));
}

// Regression (review): the travel direction came from any ball speed >= 0.5 uu/s while "ball coming" needed
// > 200 uu/s, so a car charging into a slow incoming ball, a nearly still ball or a crossing pass was told
// "Accélère : la balle va plus vite que toi". Everything below goes through computeMetrics.
TEST(ft_diagnose_end_to_end_incoming_still_and_crossing_balls_are_never_too_slow) {
    // Car 800 charging into a ball rolling at it at 100 uu/s: head-on.
    {
        const Probe p = probeGround(800.f, Vec3{-100.f, 0.f, 0.f});
        CHECK(p.m.ballComing);
        CHECK(!p.m.ballAway);
        CHECK_NEAR(p.m.along, -900.f, 1.f);
        CHECK(p.m.speedDiff > 0.f);  // chat: car faster, consistent with the tip
        CHECK(hasTip(p.tips, "ft.head_on"));
        CHECK(!hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.too_fast"));
    }
    // Nearly still ball (residual roll either way, below 50 uu/s) and ball exactly at rest: same verdict.
    for (float v : {-3.f, 0.f, 3.f, -45.f, 45.f}) {
        const Probe p = probeGround(800.f, Vec3{v, 0.f, 0.f});
        CHECK(!p.m.ballComing);
        CHECK(!p.m.ballAway);
        CHECK(p.m.along > 700.f);
        CHECK(hasTip(p.tips, "ft.too_fast"));
        CHECK(!hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.head_on"));
    }
    // Crossing pass (700 uu/s sideways), car faster (1000): angle tip only, no along-track speed tip.
    {
        const Probe p = probeGround(1000.f, Vec3{0.f, 700.f, 0.f});
        CHECK(!p.m.ballComing);
        CHECK(!p.m.ballAway);
        CHECK(p.m.speedDiff > 0.f);
        CHECK(hasTip(p.tips, "ft.angle"));
        CHECK(!hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.too_fast"));
        CHECK(!hasTip(p.tips, "ft.head_on"));
    }
    // Faster incoming ball (above the old 200 uu/s threshold): still head-on.
    {
        const Probe p = probeGround(600.f, Vec3{-250.f, 0.f, 0.f});
        CHECK(hasTip(p.tips, "ft.head_on"));
        CHECK(!hasTip(p.tips, "ft.too_slow"));
    }
    // Ball incoming at an angle (30 degrees off the car line): still coming.
    {
        const Probe p = probeGround(800.f, Vec3{-173.f, 100.f, 0.f});
        CHECK(p.m.ballComing);
        CHECK(!hasTip(p.tips, "ft.too_slow"));
    }
}

TEST(ft_diagnose_end_to_end_genuine_too_slow_cases) {
    // Ball running away from the car faster than it: chase harder.
    {
        const Probe p = probeGround(400.f, Vec3{900.f, 0.f, 0.f});
        CHECK(p.m.ballAway);
        CHECK(!p.m.ballComing);
        CHECK_NEAR(p.m.along, -500.f, 1.f);
        CHECK(hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.head_on"));
        CHECK(!hasTip(p.tips, "ft.too_fast"));
    }
    // Ball running away but slower than the car: too fast, not too slow.
    {
        const Probe p = probeGround(1300.f, Vec3{900.f, 0.f, 0.f});
        CHECK(p.m.ballAway);
        CHECK(hasTip(p.tips, "ft.too_fast"));
        CHECK(!hasTip(p.tips, "ft.too_slow"));
    }
    // Crossing pass much faster than a nearly stopped car: the ball really goes faster than the car.
    {
        const Probe p = probeGround(100.f, Vec3{0.f, 900.f, 0.f});
        CHECK(!p.m.ballComing && !p.m.ballAway);
        CHECK(p.m.speedDiff < -300.f);
        CHECK(hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.head_on"));
    }
    // Small mismatches stay silent.
    {
        const Probe p = probeGround(900.f, Vec3{800.f, 0.f, 0.f});
        CHECK(!hasTip(p.tips, "ft.too_slow"));
        CHECK(!hasTip(p.tips, "ft.too_fast"));
        CHECK(!hasTip(p.tips, "ft.head_on"));
    }
}

TEST(ft_diagnose_uses_chosen_button_style) {
    const ScoreParams sp;
    TouchMetrics m = cleanMetrics();
    m.boostAtContact = true;
    const TouchScore s = scoreTouch(m, fullControl(), sp);
    const auto ps = diagnose(TouchType::Ground, m, fullControl(), s, sp, DiagnosisParams{}, gamepadProfile(),
                             KeyStyle::PlayStation);
    CHECK(hasTip(ps, "ft.boost"));
    CHECK(!ps.empty() && ps.front().text == "Relâche Boost (Rond) avant le contact.");
    const auto xb = diagnose(TouchType::Ground, m, fullControl(), s, sp, DiagnosisParams{}, gamepadProfile(),
                             KeyStyle::Xbox);
    CHECK(!xb.empty() && xb.front().text == "Relâche Boost (B) avant le contact.");

    TouchMetrics fast = cleanMetrics();
    fast.along = 500.f;
    fast.relSpeed = 500.f;
    const TouchScore fs = scoreTouch(fast, fullControl(), sp);
    const auto tips = diagnose(TouchType::Ground, fast, fullControl(), fs, sp, DiagnosisParams{}, gamepadProfile(),
                               KeyStyle::PlayStation);
    CHECK(hasTip(tips, "ft.too_fast"));
    CHECK(!tips.empty() && contains(tips.front().text, "Boost (Rond)"));
    for (const auto& t : tips) CHECK(!contains(t.text, "(B)"));
}

TEST(ft_diagnose_angle_contact_side_boost) {
    TouchMetrics m = cleanMetrics();
    m.cross = 500.f;
    m.relSpeed = 500.f;
    CHECK(hasTip(diag(TouchType::Ground, m, fullControl()), "ft.angle"));

    TouchMetrics high = cleanMetrics();
    high.contactUp = 0.4f;
    high.contactDir = Vec3{-0.9f, 0.f, 0.4f};
    high.contactErrDeg = 60.f;
    CHECK(hasTip(diag(TouchType::Ground, high, fullControl()), "ft.contact_high"));
    high.part = CarPart::Roof;  // a roof contact can't be "too high" on the ball
    CHECK(!hasTip(diag(TouchType::Ground, high, fullControl()), "ft.contact_high"));

    TouchMetrics side = cleanMetrics();
    side.part = CarPart::Side;
    side.lateralOffset = -80.f;
    const auto st = diag(TouchType::Ground, side, fullControl());
    CHECK(hasTip(st, "ft.side"));
    CHECK(contains(st.front().text, "gauche"));
    side.lateralOffset = 80.f;
    CHECK(contains(diag(TouchType::Ground, side, fullControl()).front().text, "droite"));

    TouchMetrics boost = cleanMetrics();
    boost.boostAtContact = true;
    const auto bt = diag(TouchType::Ground, boost, fullControl());
    CHECK(hasTip(bt, "ft.boost"));
    CHECK(bt.front().text == "Relâche Boost (B) avant le contact.");
    CHECK(!hasTip(diag(TouchType::Air, boost, fullControl()), "ft.boost"));
}

TEST(ft_diagnose_contact_low_vs_ball_popped) {
    TouchMetrics low = cleanMetrics();
    low.part = CarPart::Nose;
    low.contactUp = -0.75f;
    low.ballPop = 600.f;
    auto tips = diag(TouchType::Bounce, low, fullControl());
    CHECK(hasTip(tips, "ft.contact_low"));
    CHECK(!hasTip(tips, "ft.ball_popped"));

    TouchMetrics roof = cleanMetrics();
    roof.part = CarPart::Roof;
    roof.contactUp = -1.f;
    roof.ballPop = 600.f;
    tips = diag(TouchType::Bounce, roof, fullControl());
    CHECK(hasTip(tips, "ft.ball_popped"));
    CHECK(!hasTip(tips, "ft.contact_low"));

    ControlSummary popped = fullControl();
    popped.popHeight = 400.f;
    CHECK(hasTip(diag(TouchType::Bounce, cleanMetrics(), popped), "ft.ball_popped"));
}

TEST(ft_diagnose_drop_speed_air_and_control) {
    TouchMetrics drop = cleanMetrics();
    drop.vertDiff = 700.f;
    drop.relSpeed = 700.f;
    CHECK(hasTip(diag(TouchType::Bounce, drop, fullControl()), "ft.drop_speed"));
    CHECK(!hasTip(diag(TouchType::Ground, drop, fullControl()), "ft.drop_speed"));

    TouchMetrics air = cleanMetrics();
    air.contactErrDeg = 55.f;
    air.noseAimErrDeg = 40.f;
    auto tips = diag(TouchType::Air, air, fullControl());
    CHECK(hasTip(tips, "ft.air_contact"));
    CHECK(hasTip(tips, "ft.nose_aim"));

    TouchMetrics fastAir = cleanMetrics();
    fastAir.relSpeed = 700.f;
    fastAir.closingSpeed = 600.f;
    CHECK(hasTip(diag(TouchType::Air, fastAir, fullControl()), "ft.too_fast"));

    ControlSummary lost = noControl();
    lost.maxSteerAfter = 0.95f;
    tips = diag(TouchType::Ground, cleanMetrics(), lost);
    CHECK(hasTip(tips, "ft.lost_control"));
    CHECK(hasTip(tips, "ft.oversteer"));
    tips = diag(TouchType::Air, cleanMetrics(), lost);
    CHECK(hasTip(tips, "ft.lost_control"));
    CHECK(!hasTip(tips, "ft.oversteer"));
}

// One tip on the HUD: a ball lost because the player came in too fast gets the cause ("trop vite"), not the
// consequence ("reste collé à la balle"). Without a cause, the control tip stays first.
TEST(ft_diagnose_cause_comes_before_lost_control) {
    TouchMetrics ram = cleanMetrics();
    ram.along = 800.f;
    ram.relSpeed = 800.f;
    auto tips = diag(TouchType::Ground, ram, noControl());
    CHECK(hasTip(tips, "ft.lost_control"));
    CHECK(!tips.empty() && tips.front().code == "ft.too_fast");
    tips = diag(TouchType::Ground, cleanMetrics(), noControl());
    CHECK(!tips.empty() && tips.front().code == "ft.lost_control");
}

TEST(ft_diagnose_tips_sorted_by_weight_and_clean_text) {
    TouchMetrics m = cleanMetrics();
    m.along = 900.f;
    m.relSpeed = 900.f;
    m.boostAtContact = true;
    m.part = CarPart::Side;
    m.lateralOffset = 60.f;
    const auto tips = diag(TouchType::Ground, m, noControl());
    CHECK(tips.size() >= 4);
    for (size_t i = 1; i < tips.size(); ++i) CHECK(tips[i - 1].weight >= tips[i].weight);
    for (const auto& t : tips) {
        CHECK(!t.text.empty());
        CHECK(noEmDash(t.text));
        CHECK(t.code.rfind("ft.", 0) == 0);
        CHECK(t.weight > 0.f);
        CHECK(t.text.size() < 120);
    }
}

// ---- Text -------------------------------------------------------------------------------------------------

TEST(ft_text_speed_zone_and_chat_lines) {
    CHECK(speedZone(150.f, 150.f, 300.f) == SpeedZone::Good);
    CHECK(speedZone(-150.f, 150.f, 300.f) == SpeedZone::Good);
    CHECK(speedZone(151.f, 150.f, 300.f) == SpeedZone::Ok);
    CHECK(speedZone(-300.f, 150.f, 300.f) == SpeedZone::Ok);
    CHECK(speedZone(301.f, 150.f, 300.f) == SpeedZone::Bad);
    CHECK(std::string(zoneTag(SpeedZone::Good)) == "[BON]");
    CHECK(std::string(zoneTag(SpeedZone::Ok)) == "[MOYEN]");
    CHECK(std::string(zoneTag(SpeedZone::Bad)) == "[MAUVAIS]");

    CHECK(formatSpeedValue(88.9f, SpeedUnit::Kph) == "+3.2 km/h");
    CHECK(formatSpeedValue(-44.704f, SpeedUnit::Mph) == "-1.0 mph");
    CHECK(formatSpeedValue(0.5f, SpeedUnit::Kph) == "0.0 km/h");
    CHECK(formatSpeedValue(150.f, SpeedUnit::Uus) == "+150 uu/s");
    CHECK(formatSpeedValue(88.9f, SpeedUnit::Kph, 1, false) == "3.2 km/h");

    CHECK(chatTouchLine(88.9f, 84.f, SpeedZone::Good, SpeedUnit::Kph, true) == "[BON] Écart +3.2 km/h, note 84");
    CHECK(chatTouchLine(88.9f, 84.f, SpeedZone::Good, SpeedUnit::Kph, false) == "[BON] Écart +3.2 km/h");
    CHECK(chatTouchLine(-400.f, 12.4f, SpeedZone::Bad, SpeedUnit::Mph, true) == "[MAUVAIS] Écart -8.9 mph, note 12");
    CHECK(chatFlipResetLine(-200.f, SpeedZone::Ok, SpeedUnit::Kph) == "[FLIP RESET] Écart -7.2 km/h [MOYEN]");
}

TEST(ft_text_labels_are_french) {
    CHECK(std::string(touchTypeLabel(TouchType::Air)) == "aérien");
    CHECK(std::string(touchTypeId(TouchType::Bounce)) == "bounce");
    CHECK(std::string(carPartLabel(CarPart::Side)) == "côté");
    CHECK(std::string(contactHeightLabel(ContactHeight::High)) == "haut");
    for (int i = 0; i < kTouchTypeCount; ++i) {
        CHECK(noEmDash(touchTypeTitle(static_cast<TouchType>(i))));
        CHECK(std::string(touchTypeId(static_cast<TouchType>(i))).size() > 2);
    }
}
