// End-to-end scenarios of the overlay with synthetic 120 Hz sequences: a controlled first touch, a rushed one, and an
// air dribble where the touch zone must follow the ball and disappear when the conditions stop holding.

#include <string>

#include "tests/overlay/OverlayTestUtil.h"

using namespace rlt;
using namespace ovtest;

namespace {
// Car chasing a ball rolling along +x; returns the HUD text colour on the last frame before the touch.
Color approach(Harness& h, float carSpeed, float ballSpeed, int ticks) {
    h.snap.ball.pos = {0.f, 0.f, 93.15f};
    h.snap.car.pos = {-1500.f, 0.f, 17.f};
    Color last = colors::White;
    for (int i = 0; i < ticks; ++i) {
        h.snap.car.vel = {carSpeed, 0.f, 0.f};
        h.snap.ball.vel = {ballSpeed, 0.f, 0.f};
        h.snap.car.pos.x += carSpeed / 120.f;
        h.snap.ball.pos.x += ballSpeed / 120.f;
        h.tick();
        const DrawList dl = h.draw(makeView(h.snap.ball.pos, h.snap.ball.vel, h.snap.car.pos, h.snap.car.vel));
        if (!dl.texts2D.empty()) last = dl.texts2D[0].color;
    }
    h.touch(h.snap);
    return last;
}
} // namespace

TEST(ov_scenario_controlled_first_touch_is_green) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_speed_last_touch", "1");
    h.changeContext(GameMode::Freeplay, "Park_P");
    CHECK(sameColor(approach(h, 1150.f, 1050.f, 120), colors::Green));
    const DrawList dl = h.draw(makeView(h.snap.ball.pos));
    const DrawText2D* c = findText(dl, "Contact : ");
    CHECK(c != nullptr && sameColor(c->color, colors::Green));
    const Actions a = h.command("speed");
    CHECK(!a.chat.empty() && a.chat[0].text.find(", Bon.") != std::string::npos);
    CHECK(h.mod.status().lastTouchZone == static_cast<int>(ovl::SpeedZone::Good));
}

TEST(ov_scenario_rushed_touch_is_red_with_a_hint) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.changeContext(GameMode::Freeplay, "Park_P");
    CHECK(sameColor(approach(h, 2300.f, 600.f, 60), colors::Red));
    const Actions a = h.command("speed");
    CHECK(!a.chat.empty() && a.chat[0].text.find("relâche l'accélération") != std::string::npos);
    CHECK(h.mod.status().lastTouchZone == static_cast<int>(ovl::SpeedZone::Bad));
    // Too slow is red too, with the opposite advice.
    Harness s;
    s.set("rlt_ov_speed_enabled", "1");
    CHECK(sameColor(approach(s, 300.f, 1400.f, 60), colors::Red));
    const Actions b = s.command("speed");
    CHECK(!b.chat.empty() && b.chat[0].text.find("accélère") != std::string::npos);
}

TEST(ov_scenario_air_dribble_zone_follows_the_ball) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_sphere_enabled", "1");
    h.set("rlt_ov_zone_behavior", "3");          // Auto
    h.game.category = MapCategory::AirDribble;
    h.changeContext(GameMode::Workshop, "AirDribbleChallenge");
    // Ball climbing forward at 1600 uu/s, car right below it.
    Vec3 ball{0.f, 0.f, 300.f}, car{0.f, 0.f, 180.f};
    const Vec3 vel{1600.f, 0.f, 250.f};
    int framesWithZone = 0;
    float lastCentreDx = 0.f;
    for (int i = 0; i < 240; ++i) {
        ball = ball + vel / 120.f;
        car = car + vel / 120.f;
        h.snap.ball.pos = ball;
        h.snap.ball.vel = vel;
        h.snap.car.pos = car;
        h.snap.car.vel = vel;
        h.tick();
        const DrawList dl = h.draw(makeView(ball, vel, car, vel));
        const OverlayStatus st = h.mod.status();
        CHECK(st.effectiveBehavior == static_cast<int>(ovl::ZoneBehavior::VelocityDriven));
        if (st.zoneVisible) {
            ++framesWithZone;
            CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(32 + 36));
            Vec3 sum;
            for (size_t k = 0; k < 32; ++k) sum += dl.lines3D[k].a;
            lastCentreDx = sum.x / 32.f - ball.x;
            CHECK(sameColor(dl.lines3D[0].color, colors::Green));   // same speed as the ball
        } else {
            CHECK(ball.z <= 400.f);   // only hidden before the ball is high enough
        }
    }
    CHECK(framesWithZone >= 191 && framesWithZone <= 193);   // every frame once the ball is above 400 uu
    CHECK(lastCentreDx < -20.f);   // behind the ball: hit it there to keep it climbing forward
    // Car jumps above the ball: circle gone, sphere stays.
    car.z = ball.z + 150.f;
    h.snap.car.pos = car;
    h.tick();
    DrawList dl = h.draw(makeView(ball, vel, car, vel));
    CHECK(!h.mod.status().zoneVisible);
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(36));
    // Ball dropped back to the ground (reset): nothing at all.
    h.event(GameEventKind::ResetShot);
    ball = {0.f, 0.f, 93.15f};
    h.snap.ball.pos = ball;
    h.tick();
    dl = h.draw(makeView(ball, {}, {0.f, -300.f, 17.f}));
    CHECK(dl.lines3D.empty());
    CHECK(!h.worldControlSeen);
}
