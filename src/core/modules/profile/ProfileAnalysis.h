#pragma once
// Pure analysis of the player's Rocket League configuration (PlayerProfile): hitbox family, camera and
// deadzone advice, bindings audit, mechanics readiness, change signatures. No state, no SDK: fully testable.
// Advice texts are French, short, and phrased as suggestions (the player decides).

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Math.h"

namespace rlt::pf {

// ---- Hitbox ------------------------------------------------------------------------------------------------
enum class HitboxFamily : int { Unknown = 0, Octane, Dominus, Plank, Breakout, Hybrid, Merc, Count };

struct HitboxPreset {
    HitboxFamily family = HitboxFamily::Unknown;
    const char* name = "";       // "Octane"
    Vec3 size;                   // full size L x W x H (uu), as the game reports it (x2 of the half extents)
    Vec3 offset;                 // hitbox centre relative to the car origin (x forward, z up)
    const char* examples = "";   // a few bodies using it
    const char* traits = "";     // one short French description
};

// The six standard presets (physics.md 7.1, community table; RocketSim sizes are about +2.5 uu per axis).
const std::vector<HitboxPreset>& hitboxPresets();
const HitboxPreset* findPreset(HitboxFamily f);
const char* hitboxFamilyName(HitboxFamily f);  // "Octane"... or "Inconnue"

enum class MatchQuality : int { None = 0, Exact = 1, Close = 2, Far = 3 };

constexpr float kHitboxExactTol = 2.f;  // uu (presets are >= 4.4 uu apart once the offset is included)
constexpr float kHitboxCloseTol = 8.f;
constexpr float kRocketSimPad = 2.5f;   // RocketSim sizes = community sizes + ~2.5 uu per axis

struct HitboxMatch {
    HitboxFamily family = HitboxFamily::Unknown;    // nearest preset (Unknown when the hitbox is not readable)
    MatchQuality quality = MatchQuality::None;
    float distance = 0.f;                           // uu to the nearest preset (size and offset)
    HitboxFamily runnerUp = HitboxFamily::Unknown;  // second nearest
    float margin = 0.f;                             // distance gap between nearest and runner-up
    bool rocketSimScale = false;                    // matched the RocketSim (+2.5 uu) sizes
    Vec3 size;                                      // measured full size
    Vec3 offset;
    float roofAboveOrigin = 0.f;                    // offset.z + H/2
    float noseAhead = 0.f;                          // offset.x + L/2
    float tailBehind = 0.f;                         // L/2 - offset.x
    int bodyId = 0;
    std::string bodyName;                           // from the profile, else the stock id table, else empty
};

// `extent` is the full size (half extents, x < 90, are doubled automatically). Components < 1 = not readable.
HitboxMatch classifyHitbox(const Vec3& extent, const Vec3& offset, int bodyId = 0, const std::string& bodyName = "");
HitboxMatch classifyHitbox(const PlayerProfile& p);

// Name of a stock car body product id (deprecated SDK CARBODY table), nullptr when unknown.
const char* bodyNameFromId(int bodyId);

// "Octane", "Fennec (hitbox Octane)", "Voiture inconnue (hitbox proche Dominus)", "Voiture pas encore lue".
std::string carLabel(const HitboxMatch& m);

// ---- Checks (camera, controller) -----------------------------------------------------------------------------
enum class CheckStatus : int { Ok = 0, Advice = 1, Info = 2 };

struct SettingCheck {
    std::string id;           // stable id: "camera.fov", "pad.dodge_deadzone"
    std::string label;        // French label: "FOV", "Distance"
    float value = 0.f;
    float lo = 0.f, hi = 0.f; // usual range (inclusive, compared on the displayed rounded value)
    int decimals = 0;
    bool isFlag = false;      // on/off value (ball cam): `flag` holds it
    bool flag = false;
    CheckStatus status = CheckStatus::Ok;
    std::string advice;       // one short French sentence (Advice/Info), empty when Ok
    std::string rangeText;    // "260 à 290"
    std::string valueText;    // "270", "0.50", "Activée"
};

// Common pro ranges: FOV 108..110, distance 260..290, height 90..110, angle -5..-3, stiffness 0.35..0.6,
// swivel 3..6, transition 1..1.3, ball cam on. `margin` widens every range by that fraction of its width.
std::vector<SettingCheck> analyzeCamera(const CameraProfile& cam, float margin = 0.f);

// Dodge deadzone 0.5..0.7, controller deadzone 0.05..0.12 (gamepad only; Info lines on keyboard), plus the
// sensitivities as Info lines.
std::vector<SettingCheck> analyzeController(const PlayerProfile& p, float margin = 0.f);

// One sentence on how the dodge deadzone changes flip detection ("un stick incliné à moins de 50 %...").
std::string dodgeDeadzoneExplanation(const PlayerProfile& p);

// ---- Bindings ------------------------------------------------------------------------------------------------
struct BindingRow {
    GameAction action = GameAction::Jump;
    std::string name;         // "Saut"
    std::string keys;         // labels on the device in use, "A / RB"; empty when unbound
    std::string otherKeys;    // labels on the other device
    bool bound = false;
};
std::vector<BindingRow> bindingRows(const PlayerProfile& p, KeyStyle style);

struct AuditItem {
    std::string code;         // stable id "pf.no_directional_air_roll"
    CheckStatus status = CheckStatus::Ok;
    std::string title;        // "Air roll directionnel"
    std::string text;         // French explanation / suggestion
};
std::vector<AuditItem> auditBindings(const PlayerProfile& p, KeyStyle style);

struct MechanicReadiness {
    std::string name;         // "Tornado spin"
    bool ready = false;
    std::string why;          // "Air roll gauche sur LB." / "Assigne un air roll directionnel."
};
std::vector<MechanicReadiness> mechanicsReadiness(const PlayerProfile& p, KeyStyle style);

// "Manette" / "Clavier et souris".
const char* deviceLabel(const PlayerProfile& p);
// Short summary for toasts: "manette, Octane, caméra balle".
std::string detectionSummary(const PlayerProfile& p, const HitboxMatch& m);

// ---- Change signatures (toast once per change) -----------------------------------------------------------------
struct ProfileSignature {
    std::string car;          // "23|Octane", empty while the car is unknown (body id 0)
    std::string bindings;     // hash of both binding lists, empty while no binding is known
    std::string controller;   // deadzones and sensitivities
    std::string camera;       // camera values (ball cam excluded: it is a live toggle)
};
ProfileSignature profileSignature(const PlayerProfile& p);

// 64-bit FNV-1a, hex encoded (stable on every platform).
std::string stableHash(const std::string& text);

// Formats a value with `decimals` digits ("0.50", "270").
std::string formatValue(float v, int decimals);

} // namespace rlt::pf
