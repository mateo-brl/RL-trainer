#include "tests/TestFramework.h"

#include "core/common/HudLayout.h"
#include "core/engine/Engine.h"

using namespace rlt;

namespace {
bool overlap(const HudRect& a, const HudRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
} // namespace

TEST(hud_layout_stacks_left_column) {
    std::vector<HudRect> r = {{20, 130, 300, 300}, {20, 430, 280, 200}, {20, 450, 250, 120}};
    layoutHudPanels(r, 1920, 1080);
    CHECK(!overlap(r[0], r[1]));
    CHECK(!overlap(r[1], r[2]));
    CHECK(!overlap(r[0], r[2]));
    CHECK_NEAR(r[0].y, 130.f, 0.01f);  // the first panel does not move
    CHECK(r[2].y + r[2].h <= 1080.f);
}

TEST(hud_layout_keeps_off_boost_gauge) {
    // First-touch panel anchored right at 70 % of the height: right on the boost gauge.
    std::vector<HudRect> r = {{1600, 756, 300, 150}};
    layoutHudPanels(r, 1920, 1080);
    CHECK(r[0].y + r[0].h <= 0.70f * 1080.f);
    CHECK(r[0].x + r[0].w <= 1920.f);
}

TEST(hud_layout_pushes_column_up_together) {
    // A coach card above the first-touch panel: both move up, still without overlap.
    std::vector<HudRect> r = {{1600, 600, 300, 160}, {1600, 700, 300, 150}};
    layoutHudPanels(r, 1920, 1080);
    CHECK(!overlap(r[0], r[1]));
    CHECK(r[1].y + r[1].h <= 0.70f * 1080.f);
    CHECK(r[0].y >= 0.f);
}

TEST(hud_layout_avoids_scoreboard) {
    std::vector<HudRect> r = {{860, 10, 200, 40}};
    layoutHudPanels(r, 1920, 1080);
    CHECK(r[0].y >= 0.11f * 1080.f);
}

TEST(hud_layout_clamps_inside_screen) {
    std::vector<HudRect> r = {{1850, 300, 200, 100}};
    layoutHudPanels(r, 1920, 1080);
    CHECK(r[0].x + r[0].w <= 1920.f);
}

TEST(hud_contrast_formula) {
    CHECK_NEAR(contrastRatio(colors::White, colors::Black), 21.f, 0.05f);
    CHECK_NEAR(contrastRatio(Color(119, 119, 119), colors::White), 4.48f, 0.02f);
}

TEST(hud_readable_lightens_dark_colours_only) {
    const float alpha = 0.85f * 255.f;
    const Color worst{255.f * 0.15f, 255.f * 0.15f, 255.f * 0.15f};
    const Color darkBlue{30, 60, 140, 200};
    const Color fixedBlue = readableOnPanel(darkBlue, alpha);
    CHECK(contrastRatio(fixedBlue, worst) >= 4.5f);
    CHECK_NEAR(fixedBlue.a, 200.f, 0.01f);
    CHECK(readableOnPanel(colors::White, alpha) == colors::White);
    const Color red = readableOnPanel(colors::Red, alpha);
    CHECK(contrastRatio(red, worst) >= 4.5f);
    CHECK(red.r >= 254.f && red.g > 0.f);  // still red, lightened
}

TEST(hud_style_compact_and_opacity) {
    DrawPanel p;
    p.scale = 1.f;
    p.minWidth = 100.f;
    PanelLine a;
    a.text = "Note 72/100";
    PanelLine b;
    b.text = "Session : 12 touches";
    b.detail = true;
    p.lines = {a, b};
    DrawPanel full = p;
    styleHudPanel(p, true, 1.5f, 0.9f);
    CHECK_EQ(p.lines.size(), static_cast<size_t>(1));
    CHECK_NEAR(p.scale, 1.5f, 1e-5f);
    CHECK_NEAR(p.minWidth, 150.f, 1e-3f);
    CHECK_NEAR(p.background.a, 0.9f * 255.f, 0.01f);
    styleHudPanel(full, false, 1.f, 0.5f);
    CHECK_EQ(full.lines.size(), static_cast<size_t>(2));
}

TEST(hud_focus_prefers_specific_result) {
    HudFocus f;
    f.note("ballmech", 10.f);
    f.note("firsttouch", 10.2f);  // the flick's touch: keep the ball-mechanics panel
    CHECK(f.module == "ballmech");
    f.note("firsttouch", 11.5f);  // a later, separate touch
    CHECK(f.module == "firsttouch");
    f.note("mechanics", 11.6f);
    CHECK(f.module == "mechanics");
    f.note("firsttouch", 2.f);  // time went back (new map): accept
    CHECK(f.module == "firsttouch");
    f.clear();
    CHECK(f.module.empty());
}

TEST(hud_settings_registered) {
    Engine e;
    CHECK(e.applySetting("rlt_hud_compact", "0"));
    CHECK(!e.settings().general.hudCompact);
    CHECK(e.applySetting("rlt_hud_text", "9"));
    CHECK_NEAR(e.settings().general.hudTextSize, 2.5f, 1e-5f);
    CHECK(e.applySetting("rlt_menu_key", "F7"));
    CHECK(e.settings().general.menuKey == "F7");
    CHECK(e.settingValue("rlt_hud_focus") == "1");
}

// Compact HUD: a panel left with only its title once the detail lines are dropped is not drawn (an empty box).
// Example: the shooting panel on "Toujours" in freeplay before any shot.
TEST(hud_compact_drops_title_only_panels) {
    for (bool compact : {true, false}) {
        Engine e;
        CHECK(e.applySetting("rlt_hud_focus", "0"));
        CHECK(e.applySetting("rlt_hud_compact", compact ? "1" : "0"));
        CHECK(e.applySetting("rlt_sh_hud_mode", "1"));
        Actions a;
        GameContext ctx;
        ctx.mode = GameMode::Freeplay;
        ctx.mapName = "Stadium_P";
        e.setContext(ctx, a);
        Snapshot s;
        s.frame = 1;
        s.car.valid = true;
        s.car.onGround = true;
        s.car.wheelContacts = 4;
        s.ball.valid = true;
        s.ball.pos = {0.f, 0.f, 93.f};
        e.tick(s, a);
        ViewContext view;
        view.valid = true;
        DrawList dl;
        e.draw(view, dl);
        bool titleOnly = false;
        for (const auto& p : dl.panels) titleOnly = titleOnly || p.lines.empty();
        // The full HUD still draws that panel (title only): the scenario is the one the compact HUD must drop.
        CHECK_MSG(compact ? !titleOnly : titleOnly, compact ? "compact HUD drew a panel with no line" : "no scenario");
    }
}
