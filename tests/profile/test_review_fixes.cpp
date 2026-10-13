// Tests pinning the fixes of the review: half-flip readiness needs an air roll, bindings of the device in use that
// are not readable give one Info line instead of false advice, FOV range is the single value 110.
#include <string>
#include <vector>

#include "tests/TestFramework.h"
#include "tests/profile/ProfileTestUtil.h"

#include "core/modules/profile/ProfileAnalysis.h"
#include "core/modules/profile/ProfileModule.h"

using namespace rlt;
using namespace rlt::pf;
using namespace rlt_pf_test;

namespace {

const MechanicReadiness* findMechanic(const std::vector<MechanicReadiness>& v, const std::string& name) {
    for (const auto& m : v)
        if (m.name == name) return &m;
    return nullptr;
}

const AuditItem* auditItem(const std::vector<AuditItem>& v, const std::string& code) {
    for (const auto& a : v)
        if (a.code == code) return &a;
    return nullptr;
}

int adviceItems(const std::vector<AuditItem>& v) {
    int n = 0;
    for (const auto& a : v)
        if (a.status == CheckStatus::Advice) ++n;
    return n;
}

const SettingCheck* fovCheck(const std::vector<SettingCheck>& v) {
    for (const auto& c : v)
        if (c.id == "camera.fov") return &c;
    return nullptr;
}

// Pad player with jump, boost and powerslide only: no air roll at all.
PlayerProfile padWithoutAirRoll() {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.carBodyId = 23;
    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"}};
    return p;
}

// Keyboard player whose PC list only holds gamepad keys (older BakkesMod: "KBM bindings not supported yet").
PlayerProfile keyboardWithPadKeysOnly() {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = false;
    p.carBodyId = 23;
    p.carBodyName = "Octane";
    p.pcBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"},
                    {"SecondaryCamera", "XboxTypeS_Y"}, {"ThrottleForward", "XboxTypeS_RightTriggerAxis"}};
    return p;
}

// Pad player whose gamepad list is empty while the PC list only holds keyboard and mouse keys.
PlayerProfile padWithKeyboardKeysOnly() {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.carBodyId = 23;
    p.carBodyName = "Octane";
    p.pcBindings = {{"Jump", "RightMouseButton"}, {"Boost", "LeftMouseButton"}, {"Handbrake", "LeftShift"},
                    {"SecondaryCamera", "SpaceBar"}, {"ThrottleForward", "W"}};
    return p;
}

Actions sendProfile(ProfileModule& m, Harness& h, const PlayerProfile& p) {
    h.profile = p;
    m.onProfileChanged(p);
    GameEvent e;
    e.kind = GameEventKind::ProfileChanged;
    Actions out;
    m.onGameEvent(e, h.ctx(), out);
    return out;
}

} // namespace

// ---- Half-flip needs an air roll ------------------------------------------------------------------------------
TEST(fix_half_flip_needs_air_roll) {
    const auto none = mechanicsReadiness(padWithoutAirRoll(), KeyStyle::Auto);
    CHECK_EQ(none.size(), static_cast<size_t>(9));
    const MechanicReadiness* wave = findMechanic(none, "Wavedash");
    const MechanicReadiness* half = findMechanic(none, "Half-flip");
    CHECK(wave && wave->ready);
    CHECK(wave && wave->why == "Seulement le saut et le stick.");
    CHECK(half && !half->ready);  // would land on the roof
    CHECK(half && containsText(half->why, "Assigne un air roll"));
    CHECK(findMechanic(none, "Wavedash et half-flip") == nullptr);  // old merged line is gone

    // Free air roll (default pad bindings: ToggleRoll on X).
    const auto def = mechanicsReadiness(gamepadProfile(), KeyStyle::Auto);
    const MechanicReadiness* halfDef = findMechanic(def, "Half-flip");
    CHECK(halfDef && halfDef->ready);
    CHECK(halfDef && containsText(halfDef->why, "air roll (X)"));
    CHECK(halfDef && containsText(halfDef->why, "roues"));
    // PlayStation names.
    const auto ps = mechanicsReadiness(gamepadProfile(), KeyStyle::PlayStation);
    const MechanicReadiness* halfPs = findMechanic(ps, "Half-flip");
    CHECK(halfPs && containsText(halfPs->why, "Carré"));

    // Directional air roll only.
    PlayerProfile dir = padWithoutAirRoll();
    dir.gamepadBindings.push_back({"RollRight", "XboxTypeS_RightShoulder"});
    const auto d = mechanicsReadiness(dir, KeyStyle::Auto);
    const MechanicReadiness* halfDir = findMechanic(d, "Half-flip");
    CHECK(halfDir && halfDir->ready);
    CHECK(halfDir && containsText(halfDir->why, "RB"));

    // No jump: neither is ready (boost keeps the device readable).
    PlayerProfile noJump = dir;
    noJump.gamepadBindings.erase(noJump.gamepadBindings.begin());
    const auto nj = mechanicsReadiness(noJump, KeyStyle::Auto);
    const MechanicReadiness* halfNj = findMechanic(nj, "Half-flip");
    const MechanicReadiness* waveNj = findMechanic(nj, "Wavedash");
    CHECK(halfNj && !halfNj->ready && halfNj->why == "Il faut une touche de saut.");
    CHECK(waveNj && !waveNj->ready);
    for (const auto& m : nj) CHECK(!hasEmDash(m.why));
}

// ---- Bindings of the device in use not readable -----------------------------------------------------------------
TEST(fix_device_bindings_readable_rules) {
    CHECK(deviceBindingsReadable(gamepadProfile()));
    CHECK(deviceBindingsReadable(keyboardProfile()));
    CHECK(!deviceBindingsReadable(PlayerProfile{}));
    CHECK(!deviceBindingsReadable(keyboardWithPadKeysOnly()));
    CHECK(!deviceBindingsReadable(padWithKeyboardKeysOnly()));

    // The same lists are readable for the other device.
    PlayerProfile kbAsPad = keyboardWithPadKeysOnly();
    kbAsPad.usingGamepad = true;  // pad keys in the PC list count for a pad player
    CHECK(deviceBindingsReadable(kbAsPad));
    PlayerProfile padAsKb = padWithKeyboardKeysOnly();
    padAsKb.usingGamepad = false;
    CHECK(deviceBindingsReadable(padAsKb));

    // Any one of jump, boost or throttle is enough.
    PlayerProfile throttleOnly;
    throttleOnly.usingGamepad = true;
    throttleOnly.gamepadBindings = {{"ThrottleForward", "XboxTypeS_RightTriggerAxis"}};
    CHECK(deviceBindingsReadable(throttleOnly));
    PlayerProfile boostOnly;
    boostOnly.usingGamepad = false;
    boostOnly.pcBindings = {{"Boost", "LeftMouseButton"}};
    CHECK(deviceBindingsReadable(boostOnly));
    // Other actions alone do not prove the list is the right one.
    PlayerProfile camOnly;
    camOnly.usingGamepad = false;
    camOnly.pcBindings = {{"SecondaryCamera", "SpaceBar"}, {"ToggleScoreboard", "Tab"}};
    CHECK(!deviceBindingsReadable(camOnly));
}

TEST(fix_audit_keyboard_with_pad_keys_only) {
    const PlayerProfile p = keyboardWithPadKeysOnly();
    const auto audit = auditBindings(p, KeyStyle::Auto);
    CHECK_EQ(audit.size(), static_cast<size_t>(1));
    const AuditItem* info = auditItem(audit, "pf.device_bindings_unknown");
    CHECK(info && info->status == CheckStatus::Info);
    CHECK(info && containsText(info->text, "clavier et souris"));
    CHECK(info && !hasEmDash(info->text));
    CHECK_EQ(adviceItems(audit), 0);
    for (const char* code : {"pf.no_jump", "pf.no_boost", "pf.no_directional_air_roll", "pf.no_powerslide",
                             "pf.no_free_air_roll", "pf.no_ball_cam"})
        CHECK_MSG(auditItem(audit, code) == nullptr, code);
    CHECK(mechanicsReadiness(p, KeyStyle::Auto).empty());
    // Nothing read at all keeps its own message.
    PlayerProfile empty;
    empty.valid = true;
    const auto a0 = auditBindings(empty, KeyStyle::Auto);
    CHECK_EQ(a0.size(), static_cast<size_t>(1));
    CHECK(auditItem(a0, "pf.bindings_unknown") != nullptr);
    CHECK(mechanicsReadiness(empty, KeyStyle::Auto).empty());
}

TEST(fix_audit_pad_with_keyboard_keys_only) {
    const PlayerProfile p = padWithKeyboardKeysOnly();
    const auto audit = auditBindings(p, KeyStyle::Auto);
    CHECK_EQ(audit.size(), static_cast<size_t>(1));
    const AuditItem* info = auditItem(audit, "pf.device_bindings_unknown");
    CHECK(info && info->status == CheckStatus::Info && containsText(info->text, "manette"));
    CHECK_EQ(adviceItems(audit), 0);
    CHECK(mechanicsReadiness(p, KeyStyle::Auto).empty());
    // Readable device: the normal audit runs (advice for the missing directional air roll).
    PlayerProfile ok = p;
    ok.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}};
    const auto a2 = auditBindings(ok, KeyStyle::Auto);
    CHECK(auditItem(a2, "pf.device_bindings_unknown") == nullptr);
    CHECK(auditItem(a2, "pf.no_directional_air_roll") != nullptr);
    CHECK_EQ(mechanicsReadiness(ok, KeyStyle::Auto).size(), static_cast<size_t>(9));
}

TEST(fix_module_no_false_advice_in_toast_and_report) {
    Harness h;
    ProfileModule m;
    m.onSettingsChanged(h.settings);
    const Actions a = sendProfile(m, h, keyboardWithPadKeysOnly());
    const ProfileReport& r = m.report();
    CHECK(r.valid);
    CHECK(!r.deviceBindingsReadable);
    CHECK(r.mechanics.empty());
    CHECK_EQ(r.adviceCount, 0);  // default camera, keyboard deadzones are Info only
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) {
        CHECK_EQ(a.toasts[0].text, "Config détectée : clavier-souris, Octane, caméra balle");
        CHECK(!containsText(a.toasts[0].text, "conseil"));
    }
    // Once the keyboard keys are read, the real audit comes back.
    PlayerProfile fixed = keyboardProfile();
    sendProfile(m, h, fixed);
    CHECK(m.report().deviceBindingsReadable);
    CHECK(!m.report().mechanics.empty());
    CHECK(m.report().adviceCount > 0);  // default keyboard: no directional air roll, air roll on powerslide
}

// ---- FOV 110 ----------------------------------------------------------------------------------------------------
TEST(fix_fov_single_value_range) {
    CameraProfile cam;
    cam.fov = 110.f;
    const auto ok = analyzeCamera(cam);  // keep the vector alive: fovCheck returns a pointer into it
    const SettingCheck* c = fovCheck(ok);
    CHECK(c && c->status == CheckStatus::Ok && c->rangeText == "110");
    cam.fov = 109.f;
    const auto low = analyzeCamera(cam);
    const SettingCheck* l = fovCheck(low);
    CHECK(l && l->status == CheckStatus::Advice && containsText(l->advice, "110"));
    // The advanced margin still widens a single-value range (5 % of 110 = 5.5, margin 0.5 -> 2.75).
    cam.fov = 108.f;
    const auto m1 = analyzeCamera(cam, 0.5f);
    CHECK(fovCheck(m1) && fovCheck(m1)->status == CheckStatus::Ok);
    cam.fov = 107.f;
    const auto m2 = analyzeCamera(cam, 0.5f);
    CHECK(fovCheck(m2) && fovCheck(m2)->status == CheckStatus::Advice);
    // Other ranges keep their "lo à hi" text.
    CameraProfile def;
    for (const auto& s : analyzeCamera(def))
        if (s.id == "camera.distance") CHECK_EQ(s.rangeText, "260 à 290");
}
