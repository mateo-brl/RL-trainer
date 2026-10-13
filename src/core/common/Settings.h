#pragma once
// Settings: one plain struct per module, aggregated in `Settings`. Every field is described once in a
// SettingsSchema (cvar name, type, default, range, French label/tooltip). The glue registers one BakkesMod
// cvar per entry (saved to config.cfg) and the UI draws controls from the same schema.
//
// Cvar naming: rlt_<module>_<name>, e.g. rlt_ov_speed_enabled (prefixes in docs/ARCHITECTURE.md). Never rename a cvar once released
// (it would reset the player's saved value).

#include <functional>
#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/modules/ballmech/BallMechConfig.h"
#include "core/modules/coach/CoachConfig.h"
#include "core/modules/courses/CoursesConfig.h"
#include "core/modules/firsttouch/FirstTouchConfig.h"
#include "core/modules/kickoff/KickoffConfig.h"
#include "core/modules/maps/MapsConfig.h"
#include "core/modules/mechanics/MechanicsConfig.h"
#include "core/modules/overlay/OverlayConfig.h"
#include "core/modules/prediction/PredictionConfig.h"
#include "core/modules/profile/ProfileConfig.h"
#include "core/modules/shooting/ShootingConfig.h"

namespace rlt {

struct GeneralConfig {
    bool enabled = true;              // master switch
    float hudScale = 1.f;             // multiplies every HUD text/box size
    bool hudAccents = false;          // send UTF-8 accents to the canvas (off = folded to ASCII)
    bool showInReplays = true;        // overlays (touch zone, sphere) also in replays
    bool controlInWorkshop = false;   // allow drills to move ball/car on workshop maps
    bool controlInTraining = false;   // allow drills to move ball/car in custom training
    bool chatFeedback = true;         // global switch for chat messages
    bool toastFeedback = true;        // global switch for toasts
};

struct Settings {
    GeneralConfig general;
    OverlayConfig overlay;
    FirstTouchConfig firstTouch;
    PredictionConfig prediction;
    MechanicsConfig mechanics;
    BallMechConfig ballmech;
    KickoffConfig kickoff;
    CoursesConfig courses;
    ShootingConfig shooting;
    MapsConfig maps;
    CoachConfig coach;
    ProfileConfig profile;
};

enum class SettingType : int { Bool, Int, Float, Color, Choice, Text };

struct SettingDef {
    std::string cvar;
    std::string module;    // module id owning the setting ("general", "overlay", ...)
    std::string section;   // UI group title (French)
    std::string label;     // French label
    std::string tooltip;   // French help text
    SettingType type = SettingType::Bool;
    std::string defaultValue;  // cvar string format
    float minValue = 0.f, maxValue = 0.f;
    int decimals = 2;          // Float display precision
    std::vector<std::string> choices;  // Choice labels (French), stored as index
    bool persist = true;       // saved to config.cfg
    bool advanced = false;     // hidden unless "advanced settings" is on
    std::function<bool(Settings&, const std::string&)> apply;  // parse + store; false on bad input
    std::function<std::string(const Settings&)> read;          // current value as cvar string
};

template <class Cfg>
class SchemaWriter;

class SettingsSchema {
public:
    const std::vector<SettingDef>& all() const { return defs_; }
    const SettingDef* find(const std::string& cvar) const;
    // Applies a cvar string value. Returns false when unknown cvar or unparsable value.
    bool apply(Settings& s, const std::string& cvar, const std::string& value) const;
    void applyDefaults(Settings& s) const;
    std::vector<const SettingDef*> forModule(const std::string& module) const;
    std::vector<std::string> sections(const std::string& module) const;  // in declaration order

    void add(SettingDef def);

    template <class Cfg>
    SchemaWriter<Cfg> section(Cfg Settings::*member, const std::string& module, const std::string& sectionTitle);

private:
    std::vector<SettingDef> defs_;
};

// Value parsing shared by the schema and tests.
bool parseBoolValue(const std::string& s, bool& out);
bool parseIntValue(const std::string& s, int& out);
bool parseFloatValue(const std::string& s, float& out);
std::string boolToCvar(bool v);
std::string intToCvar(int v);
std::string floatToCvar(float v);

// Fluent helper used by modules in their `describeSettings(SettingsSchema&)`:
//   auto w = schema.section(&Settings::overlay, "overlay", "Écart de vitesse");
//   w.toggle("rlt_ov_speed_enabled", &OverlayConfig::speedEnabled, false, "Afficher", "...");
template <class Cfg>
class SchemaWriter {
public:
    SchemaWriter(SettingsSchema& schema, Cfg Settings::*member, std::string module, std::string section)
        : schema_(schema), member_(member), module_(std::move(module)), section_(std::move(section)) {}

    SchemaWriter& advanced(bool on = true) { advanced_ = on; return *this; }
    SchemaWriter& persist(bool on = true) { persist_ = on; return *this; }

    SchemaWriter& toggle(const std::string& cvar, bool Cfg::*field, bool def, const std::string& label,
                         const std::string& tip) {
        SettingDef d = base(cvar, label, tip, SettingType::Bool, boolToCvar(def));
        d.minValue = 0.f;
        d.maxValue = 1.f;
        auto m = member_;
        d.apply = [m, field](Settings& s, const std::string& v) {
            bool b;
            if (!parseBoolValue(v, b)) return false;
            (s.*m).*field = b;
            return true;
        };
        d.read = [m, field](const Settings& s) { return boolToCvar((s.*m).*field); };
        schema_.add(std::move(d));
        return *this;
    }

    SchemaWriter& integer(const std::string& cvar, int Cfg::*field, int def, int lo, int hi, const std::string& label,
                          const std::string& tip) {
        SettingDef d = base(cvar, label, tip, SettingType::Int, intToCvar(def));
        d.minValue = static_cast<float>(lo);
        d.maxValue = static_cast<float>(hi);
        auto m = member_;
        d.apply = [m, field, lo, hi](Settings& s, const std::string& v) {
            int i;
            if (!parseIntValue(v, i)) return false;
            (s.*m).*field = i < lo ? lo : (i > hi ? hi : i);
            return true;
        };
        d.read = [m, field](const Settings& s) { return intToCvar((s.*m).*field); };
        schema_.add(std::move(d));
        return *this;
    }

    SchemaWriter& number(const std::string& cvar, float Cfg::*field, float def, float lo, float hi,
                         const std::string& label, const std::string& tip, int decimals = 2) {
        SettingDef d = base(cvar, label, tip, SettingType::Float, floatToCvar(def));
        d.minValue = lo;
        d.maxValue = hi;
        d.decimals = decimals;
        auto m = member_;
        d.apply = [m, field, lo, hi](Settings& s, const std::string& v) {
            float f;
            if (!parseFloatValue(v, f)) return false;
            (s.*m).*field = f < lo ? lo : (f > hi ? hi : f);
            return true;
        };
        d.read = [m, field](const Settings& s) { return floatToCvar((s.*m).*field); };
        schema_.add(std::move(d));
        return *this;
    }

    SchemaWriter& color(const std::string& cvar, Color Cfg::*field, Color def, const std::string& label,
                        const std::string& tip) {
        SettingDef d = base(cvar, label, tip, SettingType::Color, toHex(def));
        auto m = member_;
        d.apply = [m, field](Settings& s, const std::string& v) {
            Color c;
            if (!parseColor(v, c)) return false;
            (s.*m).*field = c;
            return true;
        };
        d.read = [m, field](const Settings& s) { return toHex((s.*m).*field); };
        schema_.add(std::move(d));
        return *this;
    }

    SchemaWriter& choice(const std::string& cvar, int Cfg::*field, int def, std::vector<std::string> options,
                         const std::string& label, const std::string& tip) {
        SettingDef d = base(cvar, label, tip, SettingType::Choice, intToCvar(def));
        const int n = static_cast<int>(options.size());
        d.minValue = 0.f;
        d.maxValue = static_cast<float>(n > 0 ? n - 1 : 0);
        d.choices = std::move(options);
        auto m = member_;
        d.apply = [m, field, n](Settings& s, const std::string& v) {
            int i;
            if (!parseIntValue(v, i)) return false;
            (s.*m).*field = i < 0 ? 0 : (i >= n ? n - 1 : i);
            return true;
        };
        d.read = [m, field](const Settings& s) { return intToCvar((s.*m).*field); };
        schema_.add(std::move(d));
        return *this;
    }

    SchemaWriter& text(const std::string& cvar, std::string Cfg::*field, const std::string& def,
                       const std::string& label, const std::string& tip) {
        SettingDef d = base(cvar, label, tip, SettingType::Text, def);
        auto m = member_;
        d.apply = [m, field](Settings& s, const std::string& v) {
            (s.*m).*field = v;
            return true;
        };
        d.read = [m, field](const Settings& s) { return (s.*m).*field; };
        schema_.add(std::move(d));
        return *this;
    }

private:
    SettingDef base(const std::string& cvar, const std::string& label, const std::string& tip, SettingType t,
                    const std::string& def) const {
        SettingDef d;
        d.cvar = cvar;
        d.module = module_;
        d.section = section_;
        d.label = label;
        d.tooltip = tip;
        d.type = t;
        d.defaultValue = def;
        d.persist = persist_;
        d.advanced = advanced_;
        return d;
    }

    SettingsSchema& schema_;
    Cfg Settings::*member_;
    std::string module_;
    std::string section_;
    bool advanced_ = false;
    bool persist_ = true;
};

template <class Cfg>
SchemaWriter<Cfg> SettingsSchema::section(Cfg Settings::*member, const std::string& module,
                                          const std::string& sectionTitle) {
    return SchemaWriter<Cfg>(*this, member, module, sectionTitle);
}

// Settings of the engine itself (module "general").
void describeGeneralSettings(SettingsSchema& schema);

} // namespace rlt
