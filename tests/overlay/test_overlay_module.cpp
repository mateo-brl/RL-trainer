// OverlayModule behaviour: what is drawn (or not) for each toggle and condition, colours, units, session timer,
// touch zone direction in game and in replays, commands, persistence and activity gating.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "tests/overlay/OverlayTestUtil.h"

using namespace rlt;
using namespace ovtest;

namespace {
// Car 400 uu/s faster than the ball horizontally (bad zone by default).
void setSpeeds(Harness& h, const Vec3& carVel, const Vec3& ballVel) {
    h.snap.car.vel = carVel;
    h.snap.ball.vel = ballVel;
}
float maxDistanceFrom(const DrawList& dl, const Vec3& c) {
    float m = 0.f;
    for (const auto& l : dl.lines3D) m = std::max({m, distance(l.a, c), distance(l.b, c)});
    return m;
}
float minDistanceFrom(const DrawList& dl, const Vec3& c) {
    float m = 1e9f;
    for (const auto& l : dl.lines3D) m = std::min({m, distance(l.a, c), distance(l.b, c)});
    return m;
}
Vec3 lineCentroid(const DrawList& dl) {
    Vec3 s;
    for (const auto& l : dl.lines3D) s += l.a;
    return dl.lines3D.empty() ? s : s / static_cast<float>(dl.lines3D.size());
}
const Vec3 kAirBall{0.f, 0.f, 600.f};
} // namespace

// ---- Defaults -------------------------------------------------------------------------------------------------------
TEST(ov_module_identity_and_defaults_draw_nothing) {
    Harness h;
    CHECK_EQ(std::string(h.mod.id()), std::string("overlay"));
    CHECK_EQ(std::string(h.mod.title()), std::string("Affichage"));
    setSpeeds(h, {1000.f, 0.f, 0.f}, {0.f, 0.f, 0.f});
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK(dl.empty());  // every feature is off by default, like the original
    // But the live status is computed for the UI.
    const OverlayStatus st = h.mod.status();
    CHECK(st.speedAvailable);
    CHECK_NEAR(st.diff, 1000.f, 1e-3f);
    CHECK_EQ(st.zone, static_cast<int>(ovl::SpeedZone::Bad));
}

// ---- Speed differential HUD -----------------------------------------------------------------------------------------
TEST(ov_module_speed_text_position_scale_and_colour) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    setSpeeds(h, {1400.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.texts2D.size(), static_cast<size_t>(1));
    CHECK(dl.lines3D.empty());
    const DrawText2D& t = dl.texts2D[0];
    CHECK_EQ(t.text, std::string("+14.40 KPH"));
    CHECK_NEAR(t.pos.x, 0.4587f * 1920.f, 0.01f);
    CHECK_NEAR(t.pos.y, 0.9524f * 1080.f, 0.01f);
    CHECK_NEAR(t.scale, 3.f, 1e-5f);
    CHECK(t.shadow);
    CHECK(t.align == Align::Left);
    CHECK(sameColor(t.color, colors::Red));  // 400 > 300
}

TEST(ov_module_speed_zone_colours_follow_thresholds) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    struct Case {
        float diff;
        Color expected;
    };
    const Case cases[] = {{0.f, colors::Green},   {149.f, colors::Green}, {-150.f, colors::Green},
                          {200.f, colors::Yellow}, {-300.f, colors::Yellow}, {301.f, colors::Red},
                          {-900.f, colors::Red}};
    for (const Case& c : cases) {
        setSpeeds(h, {1000.f + c.diff, 0.f, 0.f}, {1000.f, 0.f, 0.f});
        h.tick();
        const DrawList dl = h.draw(makeView(kAirBall));
        CHECK_MSG(!dl.texts2D.empty() && sameColor(dl.texts2D[0].color, c.expected), std::to_string(c.diff));
    }
    // Editable thresholds and colours.
    h.set("rlt_ov_good_threshold", "500");
    h.set("rlt_ov_good_color", "#0000FFFF");
    setSpeeds(h, {1400.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK(sameColor(dl.texts2D[0].color, Color(0.f, 0.f, 255.f, 255.f)));
}

TEST(ov_module_units_follow_profile_metric) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    setSpeeds(h, {1447.04f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    h.profile.metric = false;   // game set to imperial: Auto -> MPH, like GetbMetric()
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+10.00 MPH"));
    CHECK(h.mod.status().unit == SpeedUnit::Mph);
    h.profile.metric = true;
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+16.09 KPH"));
    h.set("rlt_ov_speed_unit", "3");
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+447.04 uu/s"));
    h.set("rlt_ov_speed_unit", "1");
    h.profile.metric = false;   // explicit unit wins
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+16.09 KPH"));
    h.set("rlt_ov_speed_unit", "2");
    h.profile.metric = true;
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+10.00 MPH"));
}

TEST(ov_module_deadband_shows_zero) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    setSpeeds(h, {1000.9f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.texts2D[0].text, std::string("0.00 KPH"));
    CHECK(sameColor(dl.texts2D[0].color, colors::Green));
}

TEST(ov_module_xy_plus_z_draws_half_scale_line_to_the_right) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_speed_mode", "2");
    setSpeeds(h, {1100.f, 0.f, 500.f}, {1000.f, 0.f, -300.f});
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.texts2D.size(), static_cast<size_t>(2));
    const DrawText2D* main = findText(dl, "+");
    const DrawText2D* z = findText(dl, "Z : ");
    CHECK(main != nullptr && z != nullptr);
    if (main && z) {
        CHECK_EQ(main->text, std::string("+3.60 KPH"));
        CHECK_EQ(z->text, std::string("Z : +7.20 KPH"));
        CHECK_NEAR(z->scale, 1.5f, 1e-5f);
        CHECK(z->pos.x - main->pos.x >= 225.f - 0.01f);  // original offset at text size 3
        CHECK(z->pos.y > main->pos.y);
        CHECK(sameColor(z->color, main->color));          // zone of the main (XY) value
    }
    // Long values push the Z line further so it never overlaps.
    setSpeeds(h, {-3000.f, 0.f, 0.f}, {2900.f, 0.f, 0.f});
    h.set("rlt_ov_speed_unit", "3");
    h.tick();
    const DrawList dl2 = h.draw(makeView(kAirBall));
    const DrawText2D* main2 = findText(dl2, "+");
    const DrawText2D* z2 = findText(dl2, "Z : ");
    CHECK(main2 && z2);
    if (main2 && z2) CHECK(z2->pos.x - main2->pos.x > 8.f * 3.f * static_cast<float>(main2->text.size()));
}

TEST(ov_module_closing_mode_in_hud) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_speed_mode", "3");
    h.snap.car.pos = {0.f, 0.f, 17.f};
    h.snap.ball.pos = {0.f, 1000.f, 17.f};
    setSpeeds(h, {0.f, 1100.f, 0.f}, {0.f, 1000.f, 0.f});
    h.tick();
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("+3.60 KPH"));
    setSpeeds(h, {0.f, 1000.f, 0.f}, {0.f, 1500.f, 0.f});
    h.tick();
    CHECK_EQ(h.draw(makeView(kAirBall)).texts2D[0].text, std::string("-18.00 KPH"));
}

TEST(ov_module_text_size_hud_scale_and_shadow) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_text_scale", "5");
    h.set("rlt_ov_shadow", "0");
    h.set("rlt_ov_speed_x", "0.25");
    h.set("rlt_ov_speed_y", "0.5");
    h.tick();
    ViewContext v = makeView(kAirBall);
    v.screenW = 2560.f;
    v.screenH = 1440.f;
    const DrawList dl = h.draw(v, 1440.f / 1080.f);
    CHECK_NEAR(dl.texts2D[0].scale, 5.f * 1440.f / 1080.f, 1e-4f);
    CHECK(!dl.texts2D[0].shadow);
    CHECK_NEAR(dl.texts2D[0].pos.x, 640.f, 0.01f);   // fractions survive resolution changes
    CHECK_NEAR(dl.texts2D[0].pos.y, 720.f, 0.01f);
}

TEST(ov_module_speed_source_snapshot_then_view_never_in_replay) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    // Snapshot preferred over the view.
    setSpeeds(h, {1200.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    CHECK_EQ(h.draw(makeView(kAirBall, {0.f, 0.f, 0.f}, {}, {0.f, 0.f, 0.f})).texts2D[0].text,
             std::string("+7.20 KPH"));
    // No snapshot yet (map just loaded): from the view.
    h.hasSnapshot = false;
    CHECK_EQ(h.draw(makeView(kAirBall, {1000.f, 0.f, 0.f}, {}, {1100.f, 0.f, 0.f})).texts2D[0].text,
             std::string("+3.60 KPH"));
    // Snapshot with an invalid car: falls back to the view too.
    h.hasSnapshot = true;
    h.snap.car.valid = false;
    CHECK_EQ(h.draw(makeView(kAirBall, {1000.f, 0.f, 0.f}, {}, {1100.f, 0.f, 0.f})).texts2D[0].text,
             std::string("+3.60 KPH"));
    // Nothing valid: no text.
    ViewContext noCar = makeView(kAirBall);
    noCar.carValid = false;
    CHECK(h.draw(noCar).texts2D.empty());
    CHECK(!h.mod.status().speedAvailable);
    // Replay: never a speed differential, even if the view has a car.
    h.snap.car.valid = true;
    h.game.mode = GameMode::Replay;
    CHECK(h.draw(makeView(kAirBall, {1000.f, 0.f, 0.f}, {}, {1100.f, 0.f, 0.f})).texts2D.empty());
    CHECK(h.mod.status().replay);
}

TEST(ov_module_speed_handles_nan_input) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_sphere_enabled", "1");
    h.snap.car.vel = {std::nanf(""), 0.f, 0.f};
    h.tick();
    ViewContext v = makeView({std::nanf(""), 0.f, 600.f});
    const DrawList dl = h.draw(v);
    CHECK(dl.texts2D.empty());
    CHECK(dl.lines3D.empty());
    CHECK(!h.mod.status().ballValid);
}

TEST(ov_module_last_touch_line) {
    Harness h;
    h.set("rlt_ov_speed_enabled", "1");
    h.set("rlt_ov_speed_last_touch", "1");
    h.tick();
    CHECK(findText(h.draw(makeView(kAirBall)), "Contact") == nullptr);  // no touch yet
    Snapshot pre = h.snap;
    pre.car.vel = {1100.f, 0.f, 0.f};
    pre.ball.vel = {1000.f, 0.f, 0.f};
    h.touch(pre);
    const DrawList dl = h.draw(makeView(kAirBall));
    const DrawText2D* c = findText(dl, "Contact : ");
    CHECK(c != nullptr);
    if (c) {
        CHECK_EQ(c->text, std::string("Contact : +3.60 KPH"));
        CHECK(sameColor(c->color, colors::Green));
        const DrawText2D* mainText = findText(dl, "0.00 KPH");
        CHECK(mainText != nullptr && c->pos.y < mainText->pos.y);  // above the value (bottom of the screen)
        CHECK_NEAR(c->scale, 1.5f, 1e-5f);
    }
    CHECK(h.mod.status().lastTouchAvailable);
    CHECK_NEAR(h.mod.status().lastTouchDiff, 100.f, 1e-3f);
    // A continuing contact (dribble) does not overwrite the first touch.
    Snapshot cont = pre;
    cont.car.vel = {3000.f, 0.f, 0.f};
    h.touch(cont, false);
    h.draw(makeView(kAirBall));
    CHECK_NEAR(h.mod.status().lastTouchDiff, 100.f, 1e-3f);
    // Re-evaluated with the current mode.
    h.set("rlt_ov_speed_unit", "3");
    CHECK(findText(h.draw(makeView(kAirBall)), "Contact : +100.00 uu/s") != nullptr);
    // Map change forgets it.
    h.changeContext(GameMode::Freeplay, "Stadium_P");
    h.draw(makeView(kAirBall));
    CHECK(!h.mod.status().lastTouchAvailable);
    // Line disabled: not drawn.
    h.touch(pre);
    h.set("rlt_ov_speed_last_touch", "0");
    CHECK(findText(h.draw(makeView(kAirBall)), "Contact") == nullptr);
}

// ---- Session timer --------------------------------------------------------------------------------------------------
TEST(ov_module_timer_counts_game_time_from_ticks) {
    Harness h;
    h.set("rlt_ov_timer_enabled", "1");
    h.tick();
    CHECK_NEAR(h.mod.sessionSeconds(), 0.f, 1e-6f);  // the first tick only anchors the frame
    h.ticks(120 * 65);
    CHECK_NEAR(h.mod.sessionSeconds(), 65.f, 0.01f);
    const DrawList dl = h.draw(makeView(kAirBall));
    const DrawText2D* t = findText(dl, "1:05");
    CHECK(t != nullptr);
    if (t) {
        CHECK_NEAR(t->pos.x, 0.4695f * 1920.f, 0.01f);
        CHECK_NEAR(t->pos.y, 0.0093f * 1080.f, 0.01f);
        CHECK(sameColor(t->color, colors::White));
        CHECK_NEAR(t->scale, 3.f, 1e-5f);   // uses the speed text size and shadow, like the original
        CHECK(t->shadow);
    }
}

TEST(ov_module_timer_stops_in_pause_and_caps_gaps) {
    Harness h;
    h.tick();
    h.ticks(120);
    CHECK_NEAR(h.mod.sessionSeconds(), 1.f, 0.01f);
    h.snap.paused = true;
    h.ticks(240);
    CHECK_NEAR(h.mod.sessionSeconds(), 1.f, 0.01f);
    h.snap.paused = false;
    // A stale GameContext.paused (only refreshed with the map/mode/date) must not stop the timer: the snapshot's own
    // flag is the reference.
    h.game.paused = true;
    h.ticks(240);
    CHECK_NEAR(h.mod.sessionSeconds(), 3.f, 0.01f);
    h.game.paused = false;
    h.tick(10000);   // long gap (loading): counted as at most 0.25 s
    CHECK_NEAR(h.mod.sessionSeconds(), 3.25f, 0.01f);
    // Frame counter going back (new map without event): re-anchors without counting.
    h.snap.frame = 5;
    h.tick();
    CHECK_NEAR(h.mod.sessionSeconds(), 3.25f, 0.01f);
    h.tick();
    CHECK_NEAR(h.mod.sessionSeconds(), 3.25f + 1.f / 120.f, 0.001f);
    // No snapshot: nothing counted.
    h.hasSnapshot = false;
    h.ticks(120);
    CHECK_NEAR(h.mod.sessionSeconds(), 3.25f + 1.f / 120.f, 0.001f);
}

TEST(ov_module_timer_has_no_hourly_wrap) {
    Harness h;
    h.set("rlt_ov_timer_enabled", "1");
    h.tick();
    for (int i = 0; i < 3700 * 4; ++i) h.tick(30);  // 0.25 s per call
    CHECK_NEAR(h.mod.sessionSeconds(), 3700.f, 0.5f);
    CHECK(findText(h.draw(makeView(kAirBall)), "1h01:40") != nullptr);
}

TEST(ov_module_timer_resets_on_map_change_and_command) {
    Harness h;
    h.changeContext(GameMode::Freeplay, "Park_P");
    h.tick();
    h.ticks(120 * 40);
    CHECK_NEAR(h.mod.sessionSeconds(), 40.f, 0.01f);
    // Same map, other context detail (category, date): keeps counting.
    h.game.category = MapCategory::Dribble;
    h.event(GameEventKind::ContextChanged, "Park_P");
    CHECK_NEAR(h.mod.sessionSeconds(), 40.f, 0.01f);
    // Another map: new session; the finished one is counted (>= 30 s) and a save is requested.
    const Actions a = h.changeContext(GameMode::Freeplay, "Stadium_P");
    CHECK_NEAR(h.mod.sessionSeconds(), 0.f, 1e-6f);
    CHECK(a.requestSave);
    CHECK_EQ(h.mod.status().sessions, 1);
    CHECK_NEAR(h.mod.status().longestSession, 40.f, 0.01f);
    // Same map but another mode (replay of it) is another session too.
    h.tick();
    h.ticks(120);
    h.changeContext(GameMode::Replay, "Stadium_P");
    CHECK_NEAR(h.mod.sessionSeconds(), 0.f, 1e-6f);
    CHECK_EQ(h.mod.status().sessions, 1);   // 1 s session not counted
    // Command.
    h.changeContext(GameMode::Freeplay, "Stadium_P");
    h.tick();
    h.ticks(120 * 5);
    const Actions r = h.command("reset_timer");
    CHECK_NEAR(h.mod.sessionSeconds(), 0.f, 1e-6f);
    CHECK_EQ(r.toasts.size(), static_cast<size_t>(1));
    CHECK(r.chat.empty());
    // Map unloaded.
    h.tick();
    h.ticks(120 * 3);
    h.event(GameEventKind::MapUnloaded);
    CHECK_NEAR(h.mod.sessionSeconds(), 0.f, 1e-6f);
    CHECK(!hasWorldControl(r));
}

TEST(ov_module_timer_hidden_in_replays_and_when_off) {
    Harness h;
    h.tick();
    h.ticks(120);
    CHECK(h.draw(makeView(kAirBall)).texts2D.empty());
    h.set("rlt_ov_timer_enabled", "1");
    h.set("rlt_ov_timer_color", "#FF8000FF");
    const DrawList dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.texts2D.size(), static_cast<size_t>(1));
    CHECK(sameColor(dl.texts2D[0].color, Color(255.f, 128.f, 0.f, 255.f)));
    h.game.mode = GameMode::Replay;
    CHECK(h.draw(makeView(kAirBall)).texts2D.empty());
}

// ---- Touch zone -----------------------------------------------------------------------------------------------------
TEST(ov_module_zone_drawn_on_the_ball_surface) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.tick();
    const DrawList dl = h.draw(makeView(kAirBall, {}, {0.f, 0.f, 17.f}));
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(32));
    CHECK(dl.texts2D.empty());
    CHECK_NEAR(maxDistanceFrom(dl, kAirBall), 91.25f, 0.5f);
    CHECK_NEAR(minDistanceFrom(dl, kAirBall), 91.25f, 0.5f);
    // Static: centred under the ball, 78.2 uu below its centre.
    const Vec3 c = lineCentroid(dl);
    CHECK_NEAR(c.x, 0.f, 0.5f);
    CHECK_NEAR(c.y, 0.f, 0.5f);
    CHECK_NEAR(c.z, 600.f - 78.2f, 0.5f);
    // Occluder: the ball (slightly smaller so the circle on the surface stays visible).
    for (const auto& l : dl.lines3D) {
        CHECK(l.occluder.enabled);
        CHECK_NEAR(l.occluder.center.z, 600.f, 1e-4f);
        CHECK(l.occluder.radius < 91.25f && l.occluder.radius > 85.f);
        CHECK_NEAR(l.width, 1.f, 1e-6f);
    }
}

TEST(ov_module_zone_conditions) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.tick();
    // Ball on the ground / below the height threshold: nothing.
    CHECK(h.draw(makeView({0.f, 0.f, 300.f})).lines3D.empty());
    CHECK(h.draw(makeView({0.f, 0.f, 400.f})).lines3D.empty());   // strictly above 400
    CHECK(!h.draw(makeView({0.f, 0.f, 401.f})).lines3D.empty());
    // Custom threshold.
    h.set("rlt_ov_air_height", "200");
    CHECK(!h.draw(makeView({0.f, 0.f, 300.f})).lines3D.empty());
    h.set("rlt_ov_air_height", "400");
    // Condition off: drawn at any height.
    h.set("rlt_ov_only_air", "0");
    CHECK(!h.draw(makeView({0.f, 0.f, 93.f})).lines3D.empty());
    h.set("rlt_ov_only_air", "1");
    // Car above the ball centre: nothing; condition off: drawn.
    CHECK(h.draw(makeView(kAirBall, {}, {0.f, 0.f, 700.f})).lines3D.empty());
    CHECK(!h.mod.status().zoneVisible);
    CHECK(h.mod.status().carKnown && !h.mod.status().carBelowBall);
    h.set("rlt_ov_car_below", "0");
    CHECK(!h.draw(makeView(kAirBall, {}, {0.f, 0.f, 700.f})).lines3D.empty());
    h.set("rlt_ov_car_below", "1");
    // Unknown car (no local car, no snapshot): the car test is skipped.
    ViewContext noCar = makeView(kAirBall);
    noCar.carValid = false;
    h.hasSnapshot = false;
    CHECK(!h.draw(noCar).lines3D.empty());
    CHECK(!h.mod.status().carKnown);
    // Snapshot car used when the view has no car.
    h.hasSnapshot = true;
    h.snap.car.pos = {0.f, 0.f, 800.f};
    CHECK(h.draw(noCar).lines3D.empty());
    // Ball invalid: nothing, never a crash.
    ViewContext noBall = makeView(kAirBall);
    noBall.ballValid = false;
    CHECK(h.draw(noBall).lines3D.empty());
    CHECK(!h.mod.status().ballValid);
}

TEST(ov_module_zone_style_options) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_zone_thickness", "4");
    h.set("rlt_ov_zone_segments", "48");
    h.set("rlt_ov_zone_radius", "30");
    h.tick();
    DrawList dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(48));
    CHECK_NEAR(dl.lines3D[0].width, 4.f, 1e-6f);
    CHECK_NEAR(maxDistanceFrom(dl, kAirBall), 91.25f, 0.5f);
    CHECK_NEAR(lineCentroid(dl).z, 600.f - std::sqrt(91.25f * 91.25f - 900.f), 0.5f);
    // Occlusion off: plain lines.
    h.set("rlt_ov_zone_occlusion", "0");
    dl = h.draw(makeView(kAirBall));
    CHECK(!dl.lines3D[0].occluder.enabled);
    // Hidden part in transparency: faint full circle + occluded circle.
    h.set("rlt_ov_zone_occlusion", "1");
    h.set("rlt_ov_zone_hidden_alpha", "0.5");
    dl = h.draw(makeView(kAirBall));
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(96));
    CHECK(!dl.lines3D[0].occluder.enabled);
    CHECK_NEAR(dl.lines3D[0].color.a, 127.5f, 0.6f);
    CHECK(dl.lines3D[95].occluder.enabled);
    CHECK_NEAR(dl.lines3D[95].color.a, 255.f, 0.6f);
}

TEST(ov_module_zone_colour_match_and_fallback) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    // Match on (default): speed zone colour.
    setSpeeds(h, {1050.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    CHECK(sameColor(h.draw(makeView(kAirBall)).lines3D[0].color, colors::Green));
    setSpeeds(h, {1250.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    CHECK(sameColor(h.draw(makeView(kAirBall)).lines3D[0].color, colors::Yellow));
    // Match off: fixed colour.
    h.set("rlt_ov_zone_match", "0");
    h.set("rlt_ov_zone_color", "#00FFFFFF");
    CHECK(sameColor(h.draw(makeView(kAirBall)).lines3D[0].color, Color(0.f, 255.f, 255.f, 255.f)));
    // Match on but no speed differential (replay): fixed colour.
    h.set("rlt_ov_zone_match", "1");
    h.game.mode = GameMode::Replay;
    CHECK(sameColor(h.draw(makeView(kAirBall)).lines3D[0].color, Color(0.f, 255.f, 255.f, 255.f)));
}

TEST(ov_module_zone_behaviours_place_the_circle) {
    const Vec3 vel{2100.f, 0.f, 0.f};
    struct Case {
        const char* behavior;
        int expectedSign;  // sign of (circle centre x - ball x)
    };
    const Case cases[] = {{"0", 0}, {"1", -1}, {"2", 1}};
    for (const Case& c : cases) {
        Harness h;
        h.set("rlt_ov_zone_enabled", "1");
        h.set("rlt_ov_zone_behavior", c.behavior);
        h.tick();
        const DrawList dl = h.draw(makeView(kAirBall, vel));
        const Vec3 centre = lineCentroid(dl);
        CHECK_NEAR(maxDistanceFrom(dl, kAirBall), 91.25f, 0.5f);
        if (c.expectedSign == 0) CHECK_NEAR(centre.x, 0.f, 0.5f);
        else CHECK_MSG(centre.x * static_cast<float>(c.expectedSign) > 20.f, c.behavior);
        CHECK(centre.z < kAirBall.z);
        const OverlayStatus st = h.mod.status();
        CHECK_NEAR(st.tiltDeg, c.expectedSign == 0 ? 0.f : 35.f, 0.1f);
    }
}

TEST(ov_module_zone_auto_behaviour) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_zone_behavior", "3");
    h.tick();
    h.draw(makeView(kAirBall, {500.f, 0.f, 0.f}));
    CHECK_EQ(h.mod.status().effectiveBehavior, static_cast<int>(ovl::ZoneBehavior::Static));
    h.draw(makeView(kAirBall, {1800.f, 0.f, 0.f}));
    CHECK_EQ(h.mod.status().effectiveBehavior, static_cast<int>(ovl::ZoneBehavior::VelocityDriven));
    h.game.category = MapCategory::AirDribble;
    h.draw(makeView(kAirBall, {100.f, 0.f, 0.f}));
    CHECK_EQ(h.mod.status().effectiveBehavior, static_cast<int>(ovl::ZoneBehavior::VelocityDriven));
}

TEST(ov_module_zone_smoothing_in_game_uses_physics_time) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_zone_behavior", "1");
    h.tick();
    h.draw(makeView(kAirBall));                         // first frame: straight down
    CHECK_NEAR(h.mod.status().tiltDeg, 0.f, 1e-3f);
    const Vec3 vel{2100.f, 0.f, 0.f};
    h.tick();
    h.draw(makeView(kAirBall, vel));                    // 1/120 s later: barely moved
    const float early = h.mod.status().tiltDeg;
    CHECK(early > 0.f && early < 5.f);
    h.draw(makeView(kAirBall, vel));                    // same physics time (paused / high fps): unchanged
    CHECK_NEAR(h.mod.status().tiltDeg, early, 1e-4f);
    for (int i = 0; i < 120; ++i) {
        h.tick();
        h.draw(makeView(kAirBall, vel));
    }
    CHECK_NEAR(h.mod.status().tiltDeg, 35.f, 0.2f);     // converged within one second
    CHECK(h.mod.zoneDirection().x < 0.f);
    // Instant smoothing setting.
    h.set("rlt_ov_zone_smoothing", "0");
    h.tick();
    h.draw(makeView(kAirBall));
    CHECK_NEAR(h.mod.status().tiltDeg, 0.f, 1e-3f);
}

TEST(ov_module_zone_snaps_on_teleport_reset_and_context_change) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_zone_behavior", "2");
    h.tick();
    h.draw(makeView(kAirBall));
    const Vec3 vel{0.f, 2100.f, 0.f};
    // Teleport (ball jumps 2000 uu): snapped to the new target at once.
    h.tick();
    h.draw(makeView({0.f, 2000.f, 600.f}, vel));
    CHECK_NEAR(h.mod.status().tiltDeg, 35.f, 0.1f);
    CHECK(h.mod.zoneDirection().y > 0.f);
    // Reset shot: next frame snaps even without a jump.
    h.tick();
    h.draw(makeView({0.f, 2000.f, 600.f}));
    CHECK(h.mod.status().tiltDeg > 30.f);  // smoothing toward down, not there yet
    h.event(GameEventKind::ResetShot);
    h.tick();
    h.draw(makeView({0.f, 2000.f, 600.f}));
    CHECK_NEAR(h.mod.status().tiltDeg, 0.f, 1e-3f);
    // Map change: transient state reset, first frame snaps.
    h.tick();
    h.draw(makeView({0.f, 2000.f, 600.f}, vel));
    h.changeContext(GameMode::Freeplay, "Stadium_P");
    CHECK(!h.mod.status().ballValid);
    h.tick();
    h.draw(makeView({0.f, 2010.f, 600.f}, vel));
    CHECK_NEAR(h.mod.status().tiltDeg, 35.f, 0.1f);
}

TEST(ov_module_zone_in_replays_uses_view_and_frame_dt) {
    Harness h;
    h.set("rlt_ov_zone_enabled", "1");
    h.set("rlt_ov_zone_behavior", "1");
    h.game.mode = GameMode::Replay;
    h.hasSnapshot = false;
    ViewContext v = makeView(kAirBall);
    v.carValid = false;   // no local car in replays: the car test is skipped
    DrawList dl = h.draw(v);  // first frame: frameDt 0, direction snapped
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(32));
    CHECK(dl.texts2D.empty());
    v.ballVel = {-2100.f, 0.f, 0.f};
    v.frameDt = 0.05f;
    h.draw(v);
    const float mid = h.mod.status().tiltDeg;
    CHECK(mid > 5.f && mid < 30.f);
    v.frameDt = 1.f / 60.f;
    for (int i = 0; i < 60; ++i) dl = h.draw(v);
    CHECK_NEAR(h.mod.status().tiltDeg, 35.f, 0.2f);
    CHECK(lineCentroid(dl).x > 20.f);   // opposite to a ball travelling toward -x
    // Long frame (the glue caps frameDt at 0.25 s; anything larger is clamped again): no blow-up.
    v.ballVel = {2100.f, 0.f, 0.f};
    v.frameDt = 1000.f;
    h.draw(v);
    CHECK(std::isfinite(h.mod.status().tiltDeg));
    CHECK(h.mod.status().tiltDeg < 35.f + 1e-3f);
}

TEST(ov_module_zone_smoothing_steps) {
    // The smoothing step is the real frame time in replays (ViewContext::frameDt) and the physics time of the newest
    // snapshot in a live game (frozen in pause, whatever the real frame time).
    const Vec3 vel{-2100.f, 0.f, 0.f};
    {
        // Replay: same frames give the same result (deterministic), frameDt 0 does not move the zone.
        Harness a, b;
        for (Harness* h : {&a, &b}) {
            h->set("rlt_ov_zone_enabled", "1");
            h->set("rlt_ov_zone_behavior", "1");
            h->game.mode = GameMode::Replay;
            h->hasSnapshot = false;
        }
        ViewContext v = makeView(kAirBall);
        a.draw(v);
        b.draw(v);
        v.ballVel = vel;
        for (int i = 0; i < 5; ++i) a.draw(v);   // frameDt 0: still straight down
        CHECK_NEAR(a.mod.status().tiltDeg, 0.f, 1e-3f);
        v.frameDt = 0.02f;
        for (int i = 0; i < 3; ++i) {
            a.draw(v);
            b.draw(v);
        }
        CHECK_EQ(a.mod.status().tiltDeg, b.mod.status().tiltDeg);
        const float before = a.mod.status().tiltDeg;
        CHECK(before > 1.f && before < 34.f);
        // Invalid frame times never step the smoothing.
        for (float bad : {-0.1f, std::nanf(""), INFINITY}) {
            v.frameDt = bad;
            a.draw(v);
            CHECK_NEAR(a.mod.status().tiltDeg, before, 1e-4f);
        }
        // A long frame (0.25 s, the glue's cap) is clamped to a 0.1 s step.
        v.frameDt = 0.25f;
        a.draw(v);
        v.frameDt = 0.1f;
        b.draw(v);
        CHECK_NEAR(a.mod.status().tiltDeg, b.mod.status().tiltDeg, 1e-3f);
        CHECK(a.mod.status().tiltDeg < 34.f);
    }
    {
        // Live game in pause: no tick, so no smoothing even with real frame time passing.
        Harness h;
        h.set("rlt_ov_zone_enabled", "1");
        h.set("rlt_ov_zone_behavior", "1");
        h.tick();
        ViewContext v = makeView(kAirBall);
        v.frameDt = 0.016f;
        h.draw(v);
        v.ballVel = vel;
        h.snap.ball.vel = vel;
        h.tick();
        h.draw(v);
        const float moving = h.mod.status().tiltDeg;
        CHECK(moving > 0.5f && moving < 20.f);
        v.frameDt = 0.25f;
        for (int i = 0; i < 30; ++i) h.draw(v);
        CHECK_NEAR(h.mod.status().tiltDeg, moving, 1e-4f);
        // Physics resumes: smoothing resumes from the snapshot time, not from the frame time.
        h.tick();
        h.draw(v);
        CHECK(h.mod.status().tiltDeg > moving);
        CHECK(h.mod.status().tiltDeg < moving + 5.f);
    }
}

// ---- Centre sphere --------------------------------------------------------------------------------------------------
TEST(ov_module_centre_sphere) {
    Harness h;
    h.set("rlt_ov_sphere_enabled", "1");
    setSpeeds(h, {1000.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    DrawList dl = h.draw(makeView(kAirBall, {}, {0.f, 0.f, 900.f}));  // car above: the sphere ignores it
    CHECK_EQ(dl.lines3D.size(), static_cast<size_t>(36));
    CHECK_NEAR(maxDistanceFrom(dl, kAirBall), 5.f, 0.01f);
    CHECK_NEAR(minDistanceFrom(dl, kAirBall), 5.f, 0.01f);
    CHECK(!dl.lines3D[0].occluder.enabled);  // inside the ball: never occluded
    CHECK(sameColor(dl.lines3D[0].color, colors::Green));
    // Radius, colour, match.
    h.set("rlt_ov_sphere_radius", "9");
    h.set("rlt_ov_sphere_match", "0");
    h.set("rlt_ov_sphere_color", "#FF00FFFF");
    dl = h.draw(makeView(kAirBall));
    CHECK_NEAR(maxDistanceFrom(dl, kAirBall), 9.f, 0.01f);
    CHECK(sameColor(dl.lines3D[0].color, colors::Magenta));
    // Ball low: hidden; condition off: shown.
    CHECK(h.draw(makeView({0.f, 0.f, 200.f})).lines3D.empty());
    h.set("rlt_ov_only_air", "0");
    CHECK_EQ(h.draw(makeView({0.f, 0.f, 200.f})).lines3D.size(), static_cast<size_t>(36));
    CHECK(h.mod.status().sphereVisible);
}

TEST(ov_module_everything_on_stays_within_line_budget) {
    Harness h;
    for (const char* cv : {"rlt_ov_speed_enabled", "rlt_ov_timer_enabled", "rlt_ov_zone_enabled",
                           "rlt_ov_sphere_enabled", "rlt_ov_speed_last_touch"})
        h.set(cv, "1");
    h.set("rlt_ov_speed_mode", "2");
    h.set("rlt_ov_zone_segments", "64");
    h.set("rlt_ov_zone_hidden_alpha", "0.3");
    h.tick();
    h.touch(h.snap);
    size_t total = 0;
    for (int frame = 0; frame < 10; ++frame) {
        h.tick();
        const DrawList dl = h.draw(makeView(kAirBall, {1500.f, 300.f, 200.f}));
        total = dl.lines3D.size();
        CHECK(dl.lines3D.size() < 1500);
        CHECK_EQ(dl.texts2D.size(), static_cast<size_t>(4));   // value, Z line, last touch, timer
        for (const auto& l : dl.lines3D)
            CHECK(std::isfinite(l.a.x) && std::isfinite(l.a.y) && std::isfinite(l.a.z) && std::isfinite(l.b.z));
    }
    CHECK_EQ(total, static_cast<size_t>(64 * 2 + 36));
}

// ---- Context --------------------------------------------------------------------------------------------------------
TEST(ov_module_menu_context_clears_live_status) {
    Harness h;
    h.tick();
    h.draw(makeView(kAirBall));
    CHECK(h.mod.status().speedAvailable);
    h.changeContext(GameMode::Menu, "MENU_Main_p");
    const OverlayStatus st = h.mod.status();
    CHECK(!st.speedAvailable);
    CHECK(!st.ballValid);
    CHECK(!st.zoneVisible);
}

// ---- Commands -------------------------------------------------------------------------------------------------------
TEST(ov_module_speed_command_chat) {
    Harness h;
    KeyBinding b;
    b.action = "Throttle";
    b.key = "XboxTypeS_RightTrigger";
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings.push_back(b);
    setSpeeds(h, {1500.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    Actions a = h.command("speed");
    CHECK_EQ(a.chat.size(), static_cast<size_t>(1));
    CHECK(a.toasts.empty());
    if (!a.chat.empty()) {
        const std::string& t = a.chat[0].text;
        CHECK(t.rfind("Écart de vitesse (XY) : +18.00 KPH, Trop d'écart.", 0) == 0);
        // The player's own throttle button, in the chosen button style.
        const std::string key = actionKeyLabel(h.profile, GameAction::Throttle);
        CHECK(!key.empty());
        CHECK(t.find("(" + key + ")") != std::string::npos);
        // PlayStation names when the player chose them in "Ma config".
        h.settings.profile.buttonStyle = 2;
        const Actions ps = h.command("speed");
        const std::string psKey = actionKeyLabel(h.profile, GameAction::Throttle, KeyStyle::PlayStation);
        CHECK(!ps.chat.empty() && ps.chat[0].text.find("(" + psKey + ")") != std::string::npos);
        h.settings.profile.buttonStyle = 0;
        CHECK(foldAccents(t).find("Ecart de vitesse") == 0);  // what the chat shows without accents
    }
    setSpeeds(h, {1020.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    a = h.command("speed");
    CHECK(!a.chat.empty() && a.chat[0].text == "Écart de vitesse (XY) : +0.72 KPH, Bon.");
    // Unavailable: no snapshot, invalid ball, replay.
    h.hasSnapshot = false;
    a = h.command("speed");
    CHECK(!a.chat.empty() && a.chat[0].text.find("indisponible") != std::string::npos);
    h.hasSnapshot = true;
    h.snap.ball.valid = false;
    a = h.command("speed");
    CHECK(!a.chat.empty() && a.chat[0].text.find("indisponible") != std::string::npos);
}

TEST(ov_module_other_commands) {
    Harness h;
    {
        // Every overlay setting back to its default: executed by the glue through the cvars.
        const Actions p = h.command("preset_original");
        CHECK_EQ(p.resetSettings.size(), static_cast<size_t>(1));
        CHECK(!p.resetSettings.empty() && p.resetSettings[0] == "overlay");
        CHECK_EQ(p.toasts.size(), static_cast<size_t>(1));
        CHECK(!p.toasts.empty() && p.toasts[0].title == "Préréglage d'origine");
        CHECK(p.setCvars.empty());
        CHECK(p.chat.empty());
        CHECK(!p.requestSave);   // progress is untouched (the glue marks the config dirty itself)
        CHECK(!hasWorldControl(p));
    }
    CHECK(h.command("inconnue").empty());
    h.tick();
    h.ticks(120 * 50);
    h.command("reset_timer");
    CHECK_EQ(h.mod.status().sessions, 1);
    CHECK(h.mod.status().totalSeconds > 49.f);
    const Actions a = h.command("reset_stats");
    CHECK(a.requestSave);
    CHECK_EQ(h.mod.status().sessions, 0);
    CHECK_NEAR(h.mod.status().totalSeconds, 0.f, 1e-6f);
    CHECK(h.mod.recentDays(10).empty());
}

// ---- Persistence ----------------------------------------------------------------------------------------------------
TEST(ov_module_save_load_roundtrip) {
    Harness h;
    h.tick();
    h.ticks(120 * 40);                      // 40 s on 2026-10-13
    h.game.localDate = "2026-10-14";
    h.ticks(120 * 20);                      // 20 s on the next day
    h.changeContext(GameMode::Freeplay, "Stadium_P");   // ends a 60 s session
    Store st;
    h.mod.save(st);
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("overlay.", 0) == 0, kv.first);

    OverlayModule other;
    other.load(st);
    const OverlayStatus a = h.mod.status(), b = other.status();
    CHECK_NEAR(b.totalSeconds, a.totalSeconds, 0.01f);
    CHECK_NEAR(b.totalSeconds, 60.f, 0.05f);
    CHECK_EQ(b.sessions, 1);
    CHECK_NEAR(b.longestSession, 60.f, 0.05f);
    const auto days = other.recentDays(10);
    CHECK_EQ(days.size(), static_cast<size_t>(2));
    if (days.size() == 2) {
        CHECK_EQ(days[0].date, std::string("2026-10-13"));
        CHECK_NEAR(days[0].seconds, 40.f, 0.1f);
        CHECK_EQ(days[1].date, std::string("2026-10-14"));
        CHECK_NEAR(days[1].seconds, 20.f, 0.1f);
    }
    CHECK_EQ(other.recentDays(1).size(), static_cast<size_t>(1));
    CHECK_EQ(other.recentDays(1)[0].date, std::string("2026-10-14"));
    // Loaded module keeps counting into the right day.
    Store st2;
    other.save(st2);
    CHECK(st2.raw() == st.raw());
}

TEST(ov_module_days_are_bounded_and_load_is_defensive) {
    Harness h;
    h.tick();
    for (int d = 1; d <= 90; ++d) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "2026-%02d-%02d", 1 + (d - 1) / 28, 1 + (d - 1) % 28);
        h.game.localDate = buf;
        h.tick();
    }
    const auto days = h.mod.recentDays(1000);
    CHECK_EQ(days.size(), static_cast<size_t>(OverlayModule::kMaxDays));
    CHECK_EQ(days.back().date, std::string("2026-04-06"));   // the newest is kept
    for (size_t i = 1; i < days.size(); ++i) CHECK(days[i - 1].date < days[i].date);

    Store bad;
    bad.setFloat("overlay.total_seconds", -5.f);
    bad.setString("overlay.longest_session", "nan");
    bad.setString("overlay.sessions", "-3");
    bad.setList("overlay.days", {"2026-10-13;120.5", "pas une date;10", "2026-13;5", "2026-10-12;abc", "2026-10-11;-4",
                                 "2026-10-10;1e12", "2026-10-13;60", "2026-10-09"});
    OverlayModule m;
    m.load(bad);
    const OverlayStatus st = m.status();
    CHECK_NEAR(st.totalSeconds, 0.f, 1e-6f);
    CHECK_NEAR(st.longestSession, 0.f, 1e-6f);
    CHECK_EQ(st.sessions, 0);
    const auto loaded = m.recentDays(100);
    CHECK_EQ(loaded.size(), static_cast<size_t>(3));   // 10-10 (clamped to 0), 10-11 (0), 10-13 (last wins)
    if (loaded.size() == 3) {
        CHECK_EQ(loaded[2].date, std::string("2026-10-13"));
        CHECK_NEAR(loaded[2].seconds, 60.f, 1e-3f);
        CHECK_NEAR(loaded[0].seconds, 0.f, 1e-6f);
    }
    // Empty store: zero state.
    OverlayModule empty;
    empty.load(Store{});
    CHECK(empty.recentDays(5).empty());
}

// ---- Activity gating ------------------------------------------------------------------------------------------------
TEST(ov_module_never_takes_world_control) {
    Harness h;
    for (const char* cv :
         {"rlt_ov_speed_enabled", "rlt_ov_timer_enabled", "rlt_ov_zone_enabled", "rlt_ov_sphere_enabled"})
        h.set(cv, "1");
    h.changeContext(GameMode::Freeplay, "Park_P");
    for (int i = 0; i < 240; ++i) {
        h.snap.ball.pos = {0.f, 0.f, 200.f + static_cast<float>(i)};
        h.tick();
        h.draw(makeView(h.snap.ball.pos, {1500.f, 0.f, 0.f}));
    }
    h.touch(h.snap);
    for (const char* c : {"speed", "reset_timer", "preset_original", "reset_stats"}) h.command(c);
    h.event(GameEventKind::ResetShot);
    h.event(GameEventKind::Goal);
    h.event(GameEventKind::Paused);
    h.event(GameEventKind::MapUnloaded);
    h.mod.onActivityStopped();
    CHECK_EQ(h.host.requests, 0);
    CHECK_EQ(h.host.releases, 0);
    CHECK(!h.worldControlSeen);
    // Works the same when world control is not allowed (workshop / analysis only).
    Harness w;
    w.game.mode = GameMode::Workshop;
    w.game.worldControlAllowed = false;
    w.host.allow = false;
    w.set("rlt_ov_zone_enabled", "1");
    w.tick();
    CHECK(!w.draw(makeView(kAirBall)).lines3D.empty());
    CHECK_EQ(w.host.requests, 0);
}

// ---- Compact HUD ------------------------------------------------------------------------------------------------------
// The overlay draws plain texts (no panel, so nothing for the compact HUD to filter) and the same thing with the
// compact HUD on or off.
TEST(ov_module_no_panel_and_same_drawing_in_compact_hud) {
    Harness h;
    for (const char* cvar : {"rlt_ov_speed_enabled", "rlt_ov_timer_enabled", "rlt_ov_zone_enabled",
                             "rlt_ov_sphere_enabled", "rlt_ov_speed_last_touch"})
        h.set(cvar, "1");
    setSpeeds(h, {1400.f, 0.f, 0.f}, {1000.f, 0.f, 0.f});
    h.tick();
    const ViewContext view = makeView(kAirBall);
    const DrawList full = h.draw(view);
    DrawContext compact = h.dc();
    compact.compactHud = true;
    DrawList small;
    h.mod.draw(view, compact, small);
    CHECK(full.panels.empty());
    CHECK(small.panels.empty());
    CHECK(!full.texts2D.empty());
    CHECK_EQ(small.texts2D.size(), full.texts2D.size());
    CHECK_EQ(small.lines3D.size(), full.lines3D.size());
}
