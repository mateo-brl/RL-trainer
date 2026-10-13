// Regressions pinned after review: exact long session timer and training totals (no float drift), persisted totals
// beyond 278 h, "today" known in the menu after a restart, a stale GameContext.paused, and the closing speed hint that
// must name whoever (car or ball) makes the approach too fast.

#include <string>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "tests/overlay/OverlayTestUtil.h"

using namespace rlt;
using namespace ovtest;

namespace {
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string speedChat(Harness& h) {
    const Actions a = h.command("speed");
    return a.chat.empty() ? std::string() : a.chat[0].text;
}
} // namespace

// ---- Time accounting in whole ticks -------------------------------------------------------------------------------
TEST(ov_reg_timer_exact_over_two_hours_of_single_ticks) {
    Harness h;
    h.set("rlt_ov_timer_enabled", "1");
    h.tick();   // anchor
    const int perHour = 3600 * 120;
    for (int i = 0; i < perHour; ++i) h.tick();
    CHECK_NEAR(h.mod.sessionSeconds(), 3600.f, 0.5f);
    CHECK(findText(h.draw(makeView({0.f, 0.f, 600.f})), "1h00:00") != nullptr);
    for (int i = 0; i < perHour; ++i) h.tick();
    // A float accumulator of 1/120 s steps read 7177.5 s here.
    CHECK_NEAR(h.mod.sessionSeconds(), 7200.f, 0.5f);
    CHECK_NEAR(h.mod.sessionSeconds(), 7200.f, 1e-3f);   // in fact exact
    CHECK(findText(h.draw(makeView({0.f, 0.f, 600.f})), "2h00:00") != nullptr);
    const OverlayStatus st = h.mod.status();
    CHECK_NEAR(st.totalSeconds, 7200.f, 1e-3f);
    CHECK_NEAR(st.todaySeconds, 7200.f, 1e-3f);
    CHECK_NEAR(st.longestSession, 7200.f, 1e-3f);
    const auto days = h.mod.recentDays(5);
    CHECK_EQ(days.size(), static_cast<size_t>(1));
    if (!days.empty()) CHECK_NEAR(days[0].seconds, 7200.f, 1e-3f);
}

TEST(ov_reg_total_keeps_growing_beyond_72_hours) {
    // 100 h already played: with a float total, 1/120 s rounded to nothing past 72.8 h and the total froze.
    Store st;
    st.setString("overlay.total_seconds", "360000");
    st.setList("overlay.days", {"2026-10-13;7200.0"});
    Harness h;
    h.mod.load(st);
    CHECK_NEAR(h.mod.totalSeconds(), 360000.f, 1e-3f);
    h.tick();
    for (int i = 0; i < 600 * 120; ++i) h.tick();   // 10 minutes, one tick per call
    CHECK_NEAR(h.mod.totalSeconds() - 360000.f, 600.f, 1.f);
    CHECK_NEAR(h.mod.status().todaySeconds, 7800.f, 0.01f);
    Store out;
    h.mod.save(out);
    CHECK_EQ(out.getString("overlay.total_seconds"), std::string("360600.000"));
    CHECK_EQ(out.getList("overlay.days").size(), static_cast<size_t>(1));
    if (!out.getList("overlay.days").empty())
        CHECK_EQ(out.getList("overlay.days")[0], std::string("2026-10-13;7800.000"));

    // Between 36.4 h and 72.8 h a float tick counted 1.875 times: 40 h + 10 min gave 145125 s.
    Store mid;
    mid.setString("overlay.total_seconds", "144000");
    Harness m;
    m.mod.load(mid);
    m.tick();
    for (int i = 0; i < 600 * 120; ++i) m.tick();
    CHECK_NEAR(m.mod.totalSeconds(), 144600.f, 1.f);
}

TEST(ov_reg_big_totals_survive_save_load_exactly) {
    // Above 999999 s, Store::setFloat ("%.6g") lost up to 5 s per reload; the tick precise text keeps every tick.
    Store st;
    st.setString("overlay.total_seconds", "3600000.25");     // 1000 h and 30 ticks
    st.setString("overlay.longest_session", "12345.675");
    st.setInt("overlay.sessions", 4321);
    OverlayModule a;
    a.load(st);
    Store s1;
    a.save(s1);
    CHECK_EQ(s1.getString("overlay.total_seconds"), std::string("3600000.250"));
    CHECK_EQ(s1.getString("overlay.longest_session"), std::string("12345.675"));
    OverlayModule b;
    b.load(s1);
    Store s2;
    b.save(s2);
    CHECK(s2.raw() == s1.raw());
    for (int round = 0; round < 50; ++round) {   // repeated reloads never drift
        OverlayModule c;
        c.load(s2);
        s2 = Store{};
        c.save(s2);
    }
    CHECK_EQ(s2.getString("overlay.total_seconds"), std::string("3600000.250"));
    CHECK_EQ(b.status().sessions, 4321);

    // Values written by older versions still load: "%.6g" totals and "%.1f" days.
    Store old;
    old.setFloat("overlay.total_seconds", 1234567.f);   // "1.23457e+06"
    old.setFloat("overlay.longest_session", 3725.5f);
    old.setList("overlay.days", {"2026-10-12;3600.5"});
    OverlayModule o;
    o.load(old);
    CHECK_NEAR(o.totalSeconds(), 1234570.f, 0.5f);
    CHECK_NEAR(o.status().longestSession, 3725.5f, 1e-3f);
    const auto days = o.recentDays(3);
    CHECK(days.size() == 1 && days[0].date == "2026-10-12");
    if (!days.empty()) CHECK_NEAR(days[0].seconds, 3600.5f, 1e-3f);
}

TEST(ov_reg_seconds_text_and_parser) {
    CHECK_EQ(OverlayModule::secondsText(0), std::string("0.000"));
    CHECK_EQ(OverlayModule::secondsText(1), std::string("0.008"));
    CHECK_EQ(OverlayModule::secondsText(60), std::string("0.500"));
    CHECK_EQ(OverlayModule::secondsText(120), std::string("1.000"));
    CHECK_EQ(OverlayModule::secondsText(119), std::string("0.992"));
    CHECK_EQ(OverlayModule::secondsText(-120), std::string("-1.000"));
    CHECK_EQ(OverlayModule::secondsText(int64_t(432000000) * 120), std::string("432000000.000"));
    // Every tick count survives text -> ticks.
    int bad = 0;
    for (int64_t t = 0; t < 5000; ++t) {
        int64_t back = -1;
        if (!OverlayModule::parseSecondsText(OverlayModule::secondsText(t), back) || back != t) ++bad;
    }
    for (int64_t t : {int64_t(123456789), int64_t(431999999999), int64_t(120) * 1000000000}) {
        int64_t back = -1;
        if (!OverlayModule::parseSecondsText(OverlayModule::secondsText(t), back) || back != t) ++bad;
    }
    CHECK_EQ(bad, 0);
    int64_t t = 0;
    CHECK(OverlayModule::parseSecondsText("12", t) && t == 1440);
    CHECK(OverlayModule::parseSecondsText("12.", t) && t == 1440);
    CHECK(OverlayModule::parseSecondsText("0.25", t) && t == 30);
    CHECK(OverlayModule::parseSecondsText("1.2e+06", t) && t == int64_t(144000000));
    CHECK(OverlayModule::parseSecondsText("-5", t) && t == -600);   // load() then clamps it to 0
    CHECK(!OverlayModule::parseSecondsText("", t));
    CHECK(!OverlayModule::parseSecondsText("abc", t));
    CHECK(!OverlayModule::parseSecondsText("12,5", t));
    CHECK(!OverlayModule::parseSecondsText("nan", t));
    CHECK_EQ(OverlayModule::secondsToTicks(1.0 / 120.0), int64_t(1));
    CHECK_EQ(OverlayModule::secondsToTicks(-1.0), int64_t(-120));
    CHECK_NEAR(OverlayModule::ticksToSeconds(int64_t(360000) * 120 + 1), 360000.f, 0.05f);
}

// ---- "Today" in the menu after a restart ----------------------------------------------------------------------------
TEST(ov_reg_today_known_in_menu_before_any_tick) {
    Store st;
    st.setString("overlay.total_seconds", "3600.0");
    st.setList("overlay.days", {"2026-10-13;3600.0"});
    // Load, then the glue's first context (main menu): no physics tick at all.
    Harness h;
    h.mod.load(st);
    h.changeContext(GameMode::Menu, "MENU_Main_p");
    OverlayStatus s = h.mod.status();
    CHECK_NEAR(s.todaySeconds, 3600.f, 1e-3f);
    CHECK_NEAR(s.totalSeconds, 3600.f, 1e-3f);
    // Context first, then load: same answer (load does not forget the date).
    Harness g;
    g.changeContext(GameMode::Menu, "MENU_Main_p");
    g.mod.load(st);
    CHECK_NEAR(g.mod.status().todaySeconds, 3600.f, 1e-3f);
    // Midnight in the menu: the new day reads 0:00 and no empty day is recorded just by looking.
    h.game.localDate = "2026-10-14";
    h.event(GameEventKind::ContextChanged, "MENU_Main_p");
    s = h.mod.status();
    CHECK_NEAR(s.todaySeconds, 0.f, 1e-6f);
    CHECK_NEAR(s.totalSeconds, 3600.f, 1e-3f);
    CHECK_EQ(h.mod.recentDays(10).size(), static_cast<size_t>(1));
    // Playing on the new day creates it.
    h.changeContext(GameMode::Freeplay, "Park_P");
    h.tick();
    h.ticks(120 * 10);
    CHECK_NEAR(h.mod.status().todaySeconds, 10.f, 1e-3f);
    CHECK_EQ(h.mod.recentDays(10).size(), static_cast<size_t>(2));
    // An invalid date from the glue is ignored (keeps the last valid one).
    h.game.localDate = "";
    h.event(GameEventKind::ContextChanged, "Park_P");
    CHECK_NEAR(h.mod.status().todaySeconds, 10.f, 1e-3f);
}

// ---- Stale GameContext.paused ---------------------------------------------------------------------------------------
TEST(ov_reg_stale_context_pause_does_not_freeze_the_timer) {
    // setContext ran while the game was paused (midnight in the pause menu, plugin reload): GameContext.paused stays
    // true after the pause ends, until the next map change.
    Harness h;
    h.game.paused = true;
    h.changeContext(GameMode::Freeplay, "Park_P");
    h.tick();
    h.ticks(120 * 30);
    CHECK_NEAR(h.mod.sessionSeconds(), 30.f, 1e-3f);
    CHECK_NEAR(h.mod.status().todaySeconds, 30.f, 1e-3f);
    // The snapshot's own flag still stops it.
    h.snap.paused = true;
    h.ticks(120 * 5);
    CHECK_NEAR(h.mod.sessionSeconds(), 30.f, 1e-3f);
}

// ---- Closing speed hint ---------------------------------------------------------------------------------------------
TEST(ov_reg_closing_hint_names_the_ball_when_the_car_stands_still) {
    Harness h;
    h.set("rlt_ov_speed_mode", "3");
    KeyBinding b;
    b.action = "Throttle";
    b.key = "XboxTypeS_RightTrigger";
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings.push_back(b);
    // Car stopped at the origin, ball 500 uu ahead rolling at it at 1200 uu/s.
    h.snap.car.vel = {};
    h.snap.ball.vel = {0.f, -1200.f, 0.f};
    h.tick();
    std::string t = speedChat(h);
    CHECK(contains(t, "Écart de vitesse (approche) : +"));
    CHECK(contains(t, "Trop d'écart"));
    CHECK(contains(t, "La balle arrive trop vite sur toi"));
    CHECK(!contains(t, "relâche"));
    CHECK(!contains(t, "Tu fonces"));

    // Car charging a ball that stands still: the throttle is the cause.
    h.snap.car.vel = {0.f, 1200.f, 0.f};
    h.snap.ball.vel = {};
    h.tick();
    t = speedChat(h);
    CHECK(contains(t, "Tu fonces sur la balle : relâche l'accélération ("));
    // Both move: the bigger part decides. Car 700 toward, ball 500 toward -> the car.
    h.snap.car.vel = {0.f, 700.f, 0.f};
    h.snap.ball.vel = {0.f, -500.f, 0.f};
    h.tick();
    CHECK(contains(speedChat(h), "Tu fonces"));
    // Car 500 toward, ball 700 toward -> the ball.
    h.snap.car.vel = {0.f, 500.f, 0.f};
    h.snap.ball.vel = {0.f, -700.f, 0.f};
    h.tick();
    CHECK(contains(speedChat(h), "La balle arrive trop vite"));

    // Opening up: the car backing away from a still ball, or the ball rolling away from a still car.
    h.snap.car.vel = {0.f, -1200.f, 0.f};
    h.snap.ball.vel = {};
    h.tick();
    t = speedChat(h);
    CHECK(contains(t, "Tu t'éloignes de la balle"));
    CHECK(!contains(t, "La balle s'éloigne"));
    h.snap.car.vel = {};
    h.snap.ball.vel = {0.f, 1200.f, 0.f};
    h.tick();
    t = speedChat(h);
    CHECK(contains(t, "La balle s'éloigne de toi : accélère ("));

    // Other modes keep their wording (the difference of speeds is the car's own business).
    h.set("rlt_ov_speed_mode", "0");
    h.snap.car.vel = {0.f, 1500.f, 0.f};
    h.snap.ball.vel = {};
    h.tick();
    CHECK(contains(speedChat(h), "Tu vas plus vite que la balle : relâche"));
}

TEST(ov_reg_closing_diff_splits_car_part) {
    using namespace rlt::ovl;
    const Vec3 car{0.f, 0.f, 0.f}, ball{1000.f, 0.f, 0.f};
    SpeedDiff d = computeSpeedDiff(SpeedMode::Closing, car, {}, ball, {-1200.f, 0.f, 0.f});
    CHECK_NEAR(d.value, 1200.f, 1e-2f);
    CHECK_NEAR(d.carApproach, 0.f, 1e-3f);
    d = computeSpeedDiff(SpeedMode::Closing, car, {800.f, 300.f, 0.f}, ball, {-200.f, 0.f, 0.f});
    CHECK_NEAR(d.value, 1000.f, 1e-2f);
    CHECK_NEAR(d.carApproach, 800.f, 1e-2f);
    // Other modes leave it at 0; coincident centres fall back to the whole value.
    CHECK_NEAR(computeSpeedDiff(SpeedMode::XY, car, {800.f, 0.f, 0.f}, ball, {}).carApproach, 0.f, 1e-6f);
    d = computeSpeedDiff(SpeedMode::Closing, car, {900.f, 0.f, 0.f}, car, {100.f, 0.f, 0.f});
    CHECK_NEAR(d.carApproach, d.value, 1e-3f);
    // Exactly half from each side counts as the car (hint about the throttle).
    SpeedDiff half;
    half.value = 1000.f;
    half.carApproach = 500.f;
    CHECK(contains(speedReport(SpeedMode::Closing, half, SpeedZone::Bad, SpeedUnit::Kph, "RT"), "Tu fonces"));
    half.carApproach = 499.f;
    CHECK(contains(speedReport(SpeedMode::Closing, half, SpeedZone::Bad, SpeedUnit::Kph, "RT"),
                   "La balle arrive trop vite sur toi : recule avec elle pour amortir."));
    // Not bad: no hint at all.
    CHECK(!contains(speedReport(SpeedMode::Closing, half, SpeedZone::Alright, SpeedUnit::Kph, "RT"), "balle arrive"));
}
