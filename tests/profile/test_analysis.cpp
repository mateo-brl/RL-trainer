// Tests of the pure analysis helpers (hitbox, camera, deadzones, bindings audit, signatures).
#include <algorithm>
#include <cmath>

#include "tests/TestFramework.h"
#include "tests/profile/ProfileTestUtil.h"

#include "core/modules/profile/ProfileAnalysis.h"

using namespace rlt;
using namespace rlt::pf;
using namespace rlt_pf_test;

namespace {
const SettingCheck* findCheck(const std::vector<SettingCheck>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}
CheckStatus cameraStatus(const std::string& id, CameraProfile cam, float margin = 0.f) {
    const auto checks = analyzeCamera(cam, margin);
    const SettingCheck* c = findCheck(checks, id);
    return c ? c->status : CheckStatus::Info;
}
const AuditItem* findAudit(const std::vector<AuditItem>& v, const std::string& code) {
    for (const auto& a : v)
        if (a.code == code) return &a;
    return nullptr;
}
} // namespace

// ---- Hitbox ------------------------------------------------------------------------------------------------
TEST(analysis_hitbox_every_preset_exact) {
    for (const auto& p : hitboxPresets()) {
        const HitboxMatch m = classifyHitbox(p.size, p.offset);
        CHECK_MSG(m.family == p.family, p.name);
        CHECK(m.quality == MatchQuality::Exact);
        CHECK(m.distance < 0.01f);
        CHECK(!m.rocketSimScale);
        CHECK(m.margin > 4.f);  // nearest other preset is clearly further
    }
}

TEST(analysis_hitbox_rocketsim_sizes_and_half_extents) {
    for (const auto& p : hitboxPresets()) {
        const Vec3 rs = p.size + Vec3{kRocketSimPad, kRocketSimPad, kRocketSimPad};
        const HitboxMatch m = classifyHitbox(rs, p.offset);
        CHECK_MSG(m.family == p.family, p.name);
        CHECK(m.quality == MatchQuality::Exact);
        CHECK(m.rocketSimScale);
        // Half extents (raw SDK values) are doubled.
        const HitboxMatch h = classifyHitbox(p.size * 0.5f, p.offset);
        CHECK_MSG(h.family == p.family, p.name);
        CHECK_NEAR(h.size.x, p.size.x, 1e-3f);
    }
}

TEST(analysis_hitbox_dominus_vs_plank_with_noise) {
    const HitboxPreset* dom = findPreset(HitboxFamily::Dominus);
    const HitboxPreset* plank = findPreset(HitboxFamily::Plank);
    CHECK(dom && plank);
    if (!dom || !plank) return;
    const HitboxMatch a = classifyHitbox(dom->size + Vec3{0.5f, -0.4f, 0.3f}, dom->offset + Vec3{0.3f, 0.f, -0.4f});
    CHECK(a.family == HitboxFamily::Dominus);
    CHECK(a.quality == MatchQuality::Exact);
    const HitboxMatch b = classifyHitbox(plank->size + Vec3{-0.5f, 0.4f, 0.3f}, plank->offset + Vec3{0.f, 0.f, 0.5f});
    CHECK(b.family == HitboxFamily::Plank);
    CHECK(b.runnerUp == HitboxFamily::Dominus);
}

TEST(analysis_hitbox_close_and_unknown) {
    const HitboxPreset* oct = findPreset(HitboxFamily::Octane);
    CHECK(oct != nullptr);
    if (!oct) return;
    const HitboxMatch close = classifyHitbox(oct->size + Vec3{4.f, 0.f, 0.f}, oct->offset);
    CHECK(close.quality == MatchQuality::Close);
    CHECK(close.family == HitboxFamily::Octane);

    const HitboxMatch far = classifyHitbox({200.f, 110.f, 60.f}, {0.f, 0.f, 30.f}, 0, "");
    CHECK(far.quality == MatchQuality::Far);
    CHECK(far.family == HitboxFamily::Unknown);
    CHECK(far.runnerUp != HitboxFamily::Unknown);
    CHECK(containsText(carLabel(far), "non standard"));

    const HitboxMatch none = classifyHitbox({0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 23);
    CHECK(none.quality == MatchQuality::None);
    CHECK(none.family == HitboxFamily::Unknown);
    CHECK_EQ(none.bodyName, "Octane");  // from the id table
    CHECK(containsText(carLabel(none), "pas encore lue"));

    const HitboxMatch nan = classifyHitbox({std::nanf(""), 84.f, 36.f}, {0.f, 0.f, 0.f});
    CHECK(nan.quality == MatchQuality::None);
}

TEST(analysis_hitbox_geometry_and_labels) {
    const HitboxPreset* oct = findPreset(HitboxFamily::Octane);
    CHECK(oct != nullptr);
    if (!oct) return;
    const HitboxMatch m = classifyHitbox(oct->size, oct->offset, 23, "");
    CHECK_NEAR(m.roofAboveOrigin, 38.83f, 0.02f);  // physics.md 7.1
    CHECK_NEAR(m.noseAhead, 72.88f, 0.02f);
    CHECK_NEAR(m.tailBehind, 45.13f, 0.02f);
    CHECK_EQ(carLabel(m), "Octane");
    const HitboxMatch f = classifyHitbox(oct->size, oct->offset, 4284, "Fennec");
    CHECK_EQ(carLabel(f), "Fennec (hitbox Octane)");
    const HitboxMatch anon = classifyHitbox(oct->size, oct->offset, 0, "");
    CHECK_EQ(carLabel(anon), "Octane");
    CHECK_EQ(std::string(hitboxFamilyName(HitboxFamily::Breakout)), "Breakout");
    CHECK_EQ(std::string(hitboxFamilyName(HitboxFamily::Unknown)), "Inconnue");
    CHECK(bodyNameFromId(403) != nullptr && std::string(bodyNameFromId(403)) == "Dominus");
    CHECK(bodyNameFromId(803) != nullptr && std::string(bodyNameFromId(803)) == "Batmobile");
    CHECK(bodyNameFromId(123456) == nullptr);
    // Default PlayerProfile hitbox is the Octane.
    PlayerProfile p;
    CHECK(classifyHitbox(p).family == HitboxFamily::Octane);
}

// ---- Camera ------------------------------------------------------------------------------------------------
TEST(analysis_camera_defaults_are_ok) {
    CameraProfile cam;  // 110 / 100 / -4 / 270 / 0.5 / 5 / 1 / ball cam
    const auto checks = analyzeCamera(cam);
    CHECK_EQ(checks.size(), static_cast<size_t>(8));
    for (const auto& c : checks) {
        CHECK_MSG(c.status == CheckStatus::Ok, c.id);
        CHECK(c.advice.empty());
        CHECK(!c.rangeText.empty());
        CHECK(!hasEmDash(c.advice));
    }
}

TEST(analysis_camera_boundaries) {
    CameraProfile cam;
    auto with = [&](auto setter) {
        CameraProfile c = cam;
        setter(c);
        return c;
    };
    // Distance 260..290.
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 260.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 290.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 259.f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 291.f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 259.6f; })) == CheckStatus::Ok);  // shows 260
    // FOV 110 (single value).
    CHECK(cameraStatus("camera.fov", with([](CameraProfile& c) { c.fov = 110.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.fov", with([](CameraProfile& c) { c.fov = 109.6f; })) == CheckStatus::Ok);  // shows 110
    CHECK(cameraStatus("camera.fov", with([](CameraProfile& c) { c.fov = 109.f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.fov", with([](CameraProfile& c) { c.fov = 108.f; })) == CheckStatus::Advice);
    // Height 90..110.
    CHECK(cameraStatus("camera.height", with([](CameraProfile& c) { c.height = 90.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.height", with([](CameraProfile& c) { c.height = 110.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.height", with([](CameraProfile& c) { c.height = 89.f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.height", with([](CameraProfile& c) { c.height = 111.f; })) == CheckStatus::Advice);
    // Angle -5..-3.
    CHECK(cameraStatus("camera.angle", with([](CameraProfile& c) { c.pitch = -5.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.angle", with([](CameraProfile& c) { c.pitch = -3.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.angle", with([](CameraProfile& c) { c.pitch = -6.f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.angle", with([](CameraProfile& c) { c.pitch = -2.f; })) == CheckStatus::Advice);
    // Stiffness 0.35..0.6, with float noise on the boundary.
    CHECK(cameraStatus("camera.stiffness", with([](CameraProfile& c) { c.stiffness = 0.35f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.stiffness", with([](CameraProfile& c) { c.stiffness = 0.3499999f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.stiffness", with([](CameraProfile& c) { c.stiffness = 0.6f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.stiffness", with([](CameraProfile& c) { c.stiffness = 0.34f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.stiffness", with([](CameraProfile& c) { c.stiffness = 0.61f; })) == CheckStatus::Advice);
    // Swivel 3..6.
    CHECK(cameraStatus("camera.swivel", with([](CameraProfile& c) { c.swivelSpeed = 3.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.swivel", with([](CameraProfile& c) { c.swivelSpeed = 6.f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.swivel", with([](CameraProfile& c) { c.swivelSpeed = 2.9f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.swivel", with([](CameraProfile& c) { c.swivelSpeed = 6.1f; })) == CheckStatus::Advice);
    // Transition 1..1.3.
    CHECK(cameraStatus("camera.transition", with([](CameraProfile& c) { c.transitionSpeed = 1.3f; })) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.transition", with([](CameraProfile& c) { c.transitionSpeed = 0.9f; })) == CheckStatus::Advice);
    CHECK(cameraStatus("camera.transition", with([](CameraProfile& c) { c.transitionSpeed = 1.4f; })) == CheckStatus::Advice);
    // Ball cam.
    CHECK(cameraStatus("camera.ball_cam", with([](CameraProfile& c) { c.ballCam = false; })) == CheckStatus::Advice);
    // Margin widens the ranges (distance span 30, margin 0.2 -> +-6).
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 255.f; }), 0.2f) == CheckStatus::Ok);
    CHECK(cameraStatus("camera.distance", with([](CameraProfile& c) { c.distance = 253.f; }), 0.2f) == CheckStatus::Advice);
}

TEST(analysis_camera_advice_direction_texts) {
    CameraProfile cam;
    cam.distance = 220.f;
    cam.height = 140.f;
    const auto checks = analyzeCamera(cam);
    const SettingCheck* d = findCheck(checks, "camera.distance");
    const SettingCheck* h = findCheck(checks, "camera.height");
    CHECK(d && containsText(d->advice, "proche"));
    CHECK(h && containsText(h->advice, "haute"));
    CHECK(d && d->valueText == "220");
    CHECK(d && d->rangeText == "260 à 290");
    const SettingCheck* s = findCheck(checks, "camera.stiffness");
    CHECK(s && s->valueText == "0.50");
}

// ---- Controller ------------------------------------------------------------------------------------------
TEST(analysis_controller_deadzone_boundaries) {
    PlayerProfile p = gamepadProfile();
    auto status = [&](float dodge, float dz, const std::string& id) {
        PlayerProfile q = p;
        q.dodgeDeadzone = dodge;
        q.controllerDeadzone = dz;
        const auto checks = analyzeController(q);
        const SettingCheck* c = findCheck(checks, id);
        return c ? c->status : CheckStatus::Info;
    };
    CHECK(status(0.5f, 0.08f, "pad.dodge_deadzone") == CheckStatus::Ok);
    CHECK(status(0.7f, 0.08f, "pad.dodge_deadzone") == CheckStatus::Ok);
    CHECK(status(0.49f, 0.08f, "pad.dodge_deadzone") == CheckStatus::Advice);
    CHECK(status(0.71f, 0.08f, "pad.dodge_deadzone") == CheckStatus::Advice);
    CHECK(status(0.6f, 0.05f, "pad.deadzone") == CheckStatus::Ok);
    CHECK(status(0.6f, 0.12f, "pad.deadzone") == CheckStatus::Ok);
    CHECK(status(0.6f, 0.04f, "pad.deadzone") == CheckStatus::Advice);
    CHECK(status(0.6f, 0.13f, "pad.deadzone") == CheckStatus::Advice);

    PlayerProfile low = p;
    low.dodgeDeadzone = 0.2f;
    const auto lowChecks = analyzeController(low);
    const SettingCheck* d = findCheck(lowChecks, "pad.dodge_deadzone");
    CHECK(d && containsText(d->advice, "double saut"));
    CHECK(findCheck(lowChecks, "pad.steering") != nullptr);
    CHECK(containsText(dodgeDeadzoneExplanation(p), "50 %"));
}

TEST(analysis_controller_keyboard_is_info_only) {
    PlayerProfile kb = keyboardProfile();
    kb.dodgeDeadzone = 0.1f;  // irrelevant on keyboard
    const auto checks = analyzeController(kb);
    CHECK(!checks.empty());
    for (const auto& c : checks) CHECK(c.status == CheckStatus::Info);
    CHECK(containsText(dodgeDeadzoneExplanation(kb), "100 %"));
}

// ---- Bindings ------------------------------------------------------------------------------------------------
TEST(analysis_audit_default_gamepad) {
    const PlayerProfile p = gamepadProfile();
    const auto audit = auditBindings(p, KeyStyle::Auto);
    const AuditItem* dir = findAudit(audit, "pf.no_directional_air_roll");
    CHECK(dir && dir->status == CheckStatus::Advice);
    const AuditItem* shared = findAudit(audit, "pf.air_roll_shared");
    CHECK(shared && shared->status == CheckStatus::Advice && containsText(shared->text, "(X)"));
    const AuditItem* cam = findAudit(audit, "pf.ball_cam");
    CHECK(cam && cam->status == CheckStatus::Ok && containsText(cam->text, "Y"));
    const AuditItem* reset = findAudit(audit, "pf.reset");
    CHECK(reset && containsText(reset->text, "RB"));
    const AuditItem* thumb = findAudit(audit, "pf.thumb_jump_boost");
    CHECK(thumb && thumb->status == CheckStatus::Info);
    for (const auto& a : audit) CHECK(!hasEmDash(a.text));
    // PlayStation names in the texts.
    const auto ps = auditBindings(p, KeyStyle::PlayStation);
    const AuditItem* psShared = findAudit(ps, "pf.air_roll_shared");
    CHECK(psShared && containsText(psShared->text, "Carré"));
}

TEST(analysis_audit_pro_gamepad) {
    const PlayerProfile p = proGamepadProfile();
    const auto audit = auditBindings(p, KeyStyle::Auto);
    CHECK(findAudit(audit, "pf.no_directional_air_roll") == nullptr);
    const AuditItem* dir = findAudit(audit, "pf.directional_air_roll");
    CHECK(dir && dir->status == CheckStatus::Ok && containsText(dir->text, "gauche") && containsText(dir->text, "LB"));
    const AuditItem* sep = findAudit(audit, "pf.air_roll_separate");
    CHECK(sep && sep->status == CheckStatus::Ok);
    CHECK(findAudit(audit, "pf.air_roll_shared") == nullptr);
    const auto rows = bindingRows(p, KeyStyle::Auto);
    CHECK_EQ(rows.size(), static_cast<size_t>(GameAction::Count));
    CHECK_EQ(rows[static_cast<int>(GameAction::AirRollLeft)].keys, "LB");
    CHECK(rows[static_cast<int>(GameAction::AirRollLeft)].bound);
    CHECK(!rows[static_cast<int>(GameAction::AirRollRight)].bound);
    CHECK_EQ(rows[static_cast<int>(GameAction::Jump)].otherKeys, "Clic droit");
}

TEST(analysis_audit_missing_things) {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    CHECK(findAudit(auditBindings(p, KeyStyle::Auto), "pf.bindings_unknown") != nullptr);

    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"},
                         {"RollRight", "XboxTypeS_RightShoulder"}};
    const auto audit = auditBindings(p, KeyStyle::Auto);
    CHECK(findAudit(audit, "pf.no_ball_cam") != nullptr);
    const AuditItem* noFree = findAudit(audit, "pf.no_free_air_roll");
    CHECK(noFree && noFree->status == CheckStatus::Info);  // directional air roll replaces it
    const AuditItem* dir = findAudit(audit, "pf.directional_air_roll");
    CHECK(dir && containsText(dir->text, "droit"));
    const AuditItem* reset = findAudit(audit, "pf.no_reset");
    CHECK(reset && reset->status == CheckStatus::Info);

    PlayerProfile q;
    q.valid = true;
    q.usingGamepad = true;
    q.gamepadBindings = {{"Jump", "XboxTypeS_A"}};
    const auto a2 = auditBindings(q, KeyStyle::Auto);
    CHECK(findAudit(a2, "pf.no_boost") != nullptr);
    CHECK(findAudit(a2, "pf.no_powerslide") != nullptr);
    const AuditItem* roll = findAudit(a2, "pf.no_free_air_roll");
    CHECK(roll && roll->status == CheckStatus::Advice);
}

TEST(analysis_mechanics_readiness) {
    const auto def = mechanicsReadiness(gamepadProfile(), KeyStyle::Auto);
    CHECK_EQ(def.size(), static_cast<size_t>(9));
    CHECK_EQ(def[0].name, "Tornado spin");
    CHECK(!def[0].ready);
    CHECK(def[2].ready);  // flip reset possible with the free air roll
    CHECK(def[4].ready);  // dribble: ball cam bound
    const auto pro = mechanicsReadiness(proGamepadProfile(), KeyStyle::Auto);
    CHECK(pro[0].ready);
    CHECK(containsText(pro[0].why, "LB"));
    for (const auto& m : pro) CHECK(!hasEmDash(m.why));
}

TEST(analysis_summary_and_device) {
    PlayerProfile p = gamepadProfile();
    const HitboxMatch m = classifyHitbox(p);
    CHECK_EQ(detectionSummary(p, m), "manette, Octane, caméra balle");
    p.camera.ballCam = false;
    p.usingGamepad = false;
    CHECK_EQ(detectionSummary(p, m), "clavier-souris, Octane, caméra libre");
    PlayerProfile nocar;
    nocar.usingGamepad = true;
    CHECK_EQ(detectionSummary(nocar, classifyHitbox(nocar)), "manette, caméra balle");
    CHECK_EQ(std::string(deviceLabel(gamepadProfile())), "Manette");
    CHECK_EQ(std::string(deviceLabel(keyboardProfile())), "Clavier et souris");
}

// ---- Signatures ------------------------------------------------------------------------------------------------
TEST(analysis_signature_rules) {
    PlayerProfile a = gamepadProfile();
    PlayerProfile b = a;
    std::reverse(b.gamepadBindings.begin(), b.gamepadBindings.end());
    CHECK_EQ(profileSignature(a).bindings, profileSignature(b).bindings);  // order-independent
    b.gamepadBindings.push_back({"RollLeft", "XboxTypeS_LeftShoulder"});
    CHECK(profileSignature(a).bindings != profileSignature(b).bindings);

    PlayerProfile c = a;
    c.camera.ballCam = !c.camera.ballCam;  // live toggle: ignored
    CHECK_EQ(profileSignature(a).camera, profileSignature(c).camera);
    c.camera.distance = 280.f;
    CHECK(profileSignature(a).camera != profileSignature(c).camera);

    PlayerProfile d = a;
    d.carBodyId = 0;
    CHECK(profileSignature(d).car.empty());
    CHECK_EQ(profileSignature(a).car, "23|Octane");
    PlayerProfile e;
    CHECK(profileSignature(e).bindings.empty());

    CHECK_EQ(stableHash(""), "cbf29ce484222325");  // FNV-1a 64 offset basis
    CHECK(stableHash("a") != stableHash("b"));
    CHECK_EQ(formatValue(-0.001f, 2), "0.00");
    CHECK_EQ(formatValue(0.5f, 2), "0.50");
    CHECK_EQ(formatValue(-4.f, 0), "-4");
    CHECK_EQ(formatValue(std::nanf(""), 1), "?");
}
