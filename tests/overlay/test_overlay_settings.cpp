// Settings schema of the overlay: defaults equal the original First Touch Trainer values, struct defaults equal the
// schema defaults, naming and French texts, sections, clamping and section reset.

#include <map>
#include <set>
#include <string>

#include "tests/overlay/OverlayTestUtil.h"

using namespace rlt;
using namespace ovtest;

namespace {
SettingsSchema overlaySchema() {
    SettingsSchema s;
    OverlayModule::describeSettings(s);
    return s;
}
bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }
} // namespace

TEST(ov_settings_naming_and_texts) {
    const SettingsSchema s = overlaySchema();
    const auto defs = s.forModule("overlay");
    CHECK(defs.size() >= 35);
    CHECK_EQ(defs.size(), s.all().size());
    std::set<std::string> cvars;
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_ov_", 0) == 0, d->cvar);
        CHECK_MSG(!d->label.empty(), d->cvar);
        CHECK_MSG(d->tooltip.size() > 15, d->cvar);
        CHECK_MSG(!hasEmDash(d->label) && !hasEmDash(d->tooltip) && !hasEmDash(d->section), d->cvar);
        for (const auto& c : d->choices) CHECK_MSG(!hasEmDash(c), d->cvar);
        CHECK(d->apply != nullptr && d->read != nullptr);
        CHECK(d->persist);
        cvars.insert(d->cvar);
    }
    CHECK_EQ(cvars.size(), defs.size());
}

TEST(ov_settings_sections_mirror_the_original) {
    const SettingsSchema s = overlaySchema();
    const std::vector<std::string> expected = {OverlayModule::kSectionActivation, OverlayModule::kSectionSpeed,
                                               OverlayModule::kSectionTimer,      OverlayModule::kSectionZone,
                                               OverlayModule::kSectionSphere,     OverlayModule::kSectionColors};
    CHECK(s.sections("overlay") == expected);
    CHECK_EQ(std::string(OverlayModule::kSectionSpeed), std::string("Écart de vitesse"));
    CHECK_EQ(std::string(OverlayModule::kSectionZone), std::string("Cercle de contact"));
    CHECK_EQ(std::string(OverlayModule::kSectionSphere), std::string("Sphère centrale"));
    CHECK_EQ(std::string(OverlayModule::kSectionColors), std::string("Couleurs des zones"));
    // The tab relies on these cvars.
    CHECK(s.find(OverlayModule::kCvarZoneColor) && s.find(OverlayModule::kCvarZoneColor)->type == SettingType::Color);
    CHECK(s.find(OverlayModule::kCvarZoneMatch) && s.find(OverlayModule::kCvarZoneMatch)->type == SettingType::Bool);
    CHECK(s.find(OverlayModule::kCvarSphereColor) != nullptr);
    CHECK(s.find(OverlayModule::kCvarSphereMatch) != nullptr);
}

TEST(ov_settings_defaults_equal_original_plugin) {
    const SettingsSchema s = overlaySchema();
    const std::map<std::string, std::string> original = {
        {"rlt_ov_speed_enabled", "0"},   {"rlt_ov_timer_enabled", "0"},      {"rlt_ov_zone_enabled", "0"},
        {"rlt_ov_sphere_enabled", "0"},  {"rlt_ov_speed_mode", "0"},         {"rlt_ov_speed_unit", "0"},
        {"rlt_ov_text_scale", "3.0"},    {"rlt_ov_shadow", "1"},             {"rlt_ov_timer_color", "#FFFFFFFF"},
        {"rlt_ov_zone_behavior", "0"},   {"rlt_ov_zone_radius", "47.0"},     {"rlt_ov_zone_thickness", "1"},
        {"rlt_ov_zone_color", "#FF0000FF"}, {"rlt_ov_zone_match", "1"},      {"rlt_ov_only_air", "1"},
        {"rlt_ov_air_height", "400.0"},  {"rlt_ov_car_below", "1"},          {"rlt_ov_sphere_radius", "5.0"},
        {"rlt_ov_sphere_color", "#FF0000FF"}, {"rlt_ov_sphere_match", "1"}, {"rlt_ov_good_color", "#00FF00FF"},
        {"rlt_ov_alright_color", "#FFFF00FF"}, {"rlt_ov_bad_color", "#FF0000FF"},
        {"rlt_ov_good_threshold", "150.0"}, {"rlt_ov_alright_threshold", "300.0"},
        {"rlt_ov_zone_min_speed", "800.0"}, {"rlt_ov_zone_max_speed", "2100.0"},
    };
    for (const auto& kv : original) {
        const SettingDef* d = s.find(kv.first);
        CHECK_MSG(d != nullptr, kv.first);
        if (d) CHECK_MSG(d->defaultValue == kv.second, kv.first + " = " + d->defaultValue);
    }
    // Positions: the original pixel defaults W/2.18, H/1.05, W/2.13, H/108 as screen fractions.
    auto frac = [&](const char* cvar) {
        float f = -1.f;
        const SettingDef* d = s.find(cvar);
        if (d) parseFloatValue(d->defaultValue, f);
        return f;
    };
    CHECK_NEAR(frac("rlt_ov_speed_x"), 1.f / 2.18f, 1e-4f);
    CHECK_NEAR(frac("rlt_ov_speed_y"), 1.f / 1.05f, 1e-4f);
    CHECK_NEAR(frac("rlt_ov_timer_x"), 1.f / 2.13f, 1e-4f);
    CHECK_NEAR(frac("rlt_ov_timer_y"), 1.f / 108.f, 1e-4f);
    // Slider ranges of the original.
    CHECK_EQ(s.find("rlt_ov_text_scale")->minValue, 1.f);
    CHECK_EQ(s.find("rlt_ov_text_scale")->maxValue, 10.f);
    CHECK_EQ(s.find("rlt_ov_zone_radius")->minValue, 30.f);
    CHECK_EQ(s.find("rlt_ov_zone_radius")->maxValue, 60.f);
    CHECK_EQ(s.find("rlt_ov_zone_thickness")->maxValue, 5.f);
    CHECK_EQ(s.find("rlt_ov_sphere_radius")->minValue, 1.f);
    CHECK_EQ(s.find("rlt_ov_sphere_radius")->maxValue, 10.f);
    // The NEW max tilt of the circle (advanced).
    CHECK_EQ(s.find("rlt_ov_zone_max_tilt")->defaultValue, std::string("35.0"));
    CHECK(s.find("rlt_ov_zone_max_tilt")->advanced);
    CHECK(!s.find("rlt_ov_zone_radius")->advanced);
    // Choice labels.
    const SettingDef* mode = s.find("rlt_ov_speed_mode");
    CHECK_EQ(mode->choices.size(), static_cast<size_t>(4));
    CHECK_EQ(mode->choices[3], std::string("Vitesse d'approche"));
    const SettingDef* beh = s.find("rlt_ov_zone_behavior");
    CHECK(beh->choices == std::vector<std::string>({"Statique", "Pilotée par la vitesse", "Garder en l'air", "Auto"}));
    CHECK_EQ(s.find("rlt_ov_speed_unit")->choices.size(), static_cast<size_t>(4));
}

TEST(ov_settings_struct_defaults_equal_schema_defaults) {
    SettingsSchema s = overlaySchema();
    const Settings untouched;               // OverlayConfig member initialisers
    Settings applied;
    s.applyDefaults(applied);
    for (const SettingDef* d : s.forModule("overlay")) {
        CHECK_MSG(d->read(untouched) == d->defaultValue, d->cvar + ": struct " + d->read(untouched));
        CHECK_MSG(d->read(applied) == d->defaultValue, d->cvar);
        // Every default parses back to itself.
        Settings tmp;
        CHECK_MSG(d->apply(tmp, d->defaultValue), d->cvar);
        CHECK_MSG(d->read(tmp) == d->defaultValue, d->cvar);
    }
}

TEST(ov_settings_values_are_clamped) {
    SettingsSchema s = overlaySchema();
    Settings st;
    CHECK(s.apply(st, "rlt_ov_zone_radius", "100"));
    CHECK_EQ(st.overlay.zoneRadius, 60.f);
    CHECK(s.apply(st, "rlt_ov_text_scale", "0"));
    CHECK_EQ(st.overlay.textScale, 1.f);
    CHECK(s.apply(st, "rlt_ov_zone_behavior", "7"));
    CHECK_EQ(st.overlay.zoneBehavior, 3);
    CHECK(s.apply(st, "rlt_ov_speed_x", "1.5"));
    CHECK_EQ(st.overlay.speedX, 1.f);
    CHECK(s.apply(st, "rlt_ov_zone_thickness", "9"));
    CHECK_EQ(st.overlay.zoneThickness, 5);
    CHECK(!s.apply(st, "rlt_ov_zone_color", "pas une couleur"));
    CHECK(st.overlay.zoneColor == colors::Red);
    CHECK(s.apply(st, "rlt_ov_zone_color", "#0000FF80"));
    CHECK(sameColor(st.overlay.zoneColor, Color(0.f, 0.f, 255.f, 128.f)));
}

TEST(ov_settings_section_reset_restores_defaults) {
    SettingsSchema s = overlaySchema();
    Settings st;
    s.applyDefaults(st);
    s.apply(st, "rlt_ov_text_scale", "7");
    s.apply(st, "rlt_ov_speed_x", "0.1");
    s.apply(st, "rlt_ov_shadow", "0");
    s.apply(st, "rlt_ov_zone_radius", "33");
    // What ui::resetToDefaults does for one section.
    for (const SettingDef* d : s.forModule("overlay"))
        if (d->section == OverlayModule::kSectionSpeed) s.apply(st, d->cvar, d->defaultValue);
    CHECK_EQ(st.overlay.textScale, 3.f);
    CHECK_EQ(st.overlay.speedX, 0.4587f);
    CHECK(st.overlay.shadow);
    CHECK_EQ(st.overlay.zoneRadius, 33.f);  // other sections untouched
    // "preset_original" = every overlay setting back to its default.
    for (const SettingDef* d : s.forModule("overlay")) s.apply(st, d->cvar, d->defaultValue);
    CHECK_EQ(st.overlay.zoneRadius, 47.f);
}

TEST(ov_settings_misordered_thresholds_are_fixed_by_the_module) {
    Harness h;
    h.set("rlt_ov_good_threshold", "400");
    h.set("rlt_ov_alright_threshold", "200");
    CHECK_EQ(h.mod.config().alrightThreshold, 400.f);
    CHECK_EQ(h.mod.config().goodThreshold, 400.f);
}
