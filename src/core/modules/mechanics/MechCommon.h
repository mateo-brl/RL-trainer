#pragma once
// Shared foundations of the movement mechanics detectors: physics constants (RocketSim src/RLConst.h unless a
// comment says otherwise), small math helpers, surfaces, dodge kinds, the internal grade type and the
// detection configuration. Everything lives in rlt::mech so it never clashes with another module's types.
//
// Conventions: one tick = one 120 Hz physics frame (Snapshot::frame). Inputs follow ControllerInput:
// pitch +1 = nose up (stick pulled back), yaw/steer +1 = right, roll +1 = roll right. A dodge direction is
// local (x forward, y right); its angle atan2(y, x) is 0 forward, +90 right, +/-180 back.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Math.h"
#include "core/common/Snapshot.h"
#include "core/modules/mechanics/MechCatalog.h"

namespace rlt::mech {

// ---- Physics constants ---------------------------------------------------------------------------------
namespace k {
inline constexpr float kTickRateF = 120.f;
inline constexpr float kDt = 1.f / 120.f;
inline constexpr float kMsPerTick = 1000.f / 120.f;
inline constexpr float kGravityZ = -650.f;
inline constexpr float kJumpImpulse = 875.f / 3.f;          // 291.667 uu/s, also the double jump
inline constexpr float kJumpAccel = 4375.f / 3.f;           // while jump is held
inline constexpr float kJumpPreMinAccelScale = 0.62f;
inline constexpr float kJumpMinTime = 0.025f;               // 3 ticks
inline constexpr float kJumpMaxTime = 0.2f;                 // 24 ticks
inline constexpr float kDoubleJumpMaxDelay = 1.25f;         // after the jump has finished
inline constexpr float kDodgeDeadzoneDefault = 0.5f;
inline constexpr float kFlipInitialVelScale = 500.f;
inline constexpr float kFlipTorqueTime = 0.65f;             // 78 ticks
inline constexpr float kFlipTorqueX = 260.f;                // roll (side dodge) rad/s^2
inline constexpr float kFlipTorqueY = 224.f;                // pitch (front/back dodge) rad/s^2
inline constexpr float kFlipZDamp120 = 0.35f;
inline constexpr float kFlipZDampStart = 0.15f;
inline constexpr float kFlipZDampEnd = 0.21f;
inline constexpr float kFlipPitchlockExtraTime = 0.3f;
inline constexpr float kFlipForwardImpulseMaxSpeedScale = 1.f;
inline constexpr float kFlipSideImpulseMaxSpeedScale = 1.9f;
inline constexpr float kFlipBackwardImpulseMaxSpeedScale = 2.5f;
inline constexpr float kFlipBackwardImpulseScaleX = 16.f / 15.f;
inline constexpr float kCarMaxSpeed = 2300.f;
inline constexpr float kSupersonicStartSpeed = 2200.f;
inline constexpr float kSupersonicMaintainMinSpeed = 2100.f;
inline constexpr float kBoostAccelGround = 2975.f / 3.f;
inline constexpr float kBoostAccelAir = 3175.f / 3.f;
inline constexpr float kBoostUsedPerSecond = 100.f / 3.f;
inline constexpr float kBoostMinTime = 0.1f;
inline constexpr float kThrottleAirAccel = 200.f / 3.f;
inline constexpr float kCoastDecel = 525.f;                 // RLBot wiki
inline constexpr float kBrakeDecel = 3500.f;                // RLBot wiki
inline constexpr float kCarMaxAngSpeed = 5.5f;
inline constexpr float kCarTorqueScale = 0.0958738f;        // 2*pi/65536*1000
inline constexpr float kAirTorquePitch = 130.f, kAirTorqueYaw = 95.f, kAirTorqueRoll = 400.f;
inline constexpr float kAirDampPitch = 30.f, kAirDampYaw = 20.f, kAirDampRoll = 50.f;
inline constexpr float kCarRestZ = 17.f;
inline constexpr int kGroundedWheels = 3;                   // RocketSim: on ground = >= 3 wheels in contact
} // namespace k

// ---- Math helpers --------------------------------------------------------------------------------------
inline Vec3 tangent(const Vec3& v, const Vec3& n) { return v - n * dot(v, n); }
inline Vec3 clampLen(const Vec3& v, float maxLen) {
    const float l = length(v);
    return l > maxLen ? v * (maxLen / l) : v;
}
inline float safeAsinDeg(float s) { return std::asin(clampf(s, -1.f, 1.f)) * kRadToDeg; }
inline float safeAcosDeg(float c) { return std::acos(clampf(c, -1.f, 1.f)) * kRadToDeg; }
// Unsigned angle (deg) between the projections of a and b on the plane of normal n (0 when degenerate).
inline float planarAngleDeg(const Vec3& a, const Vec3& b, const Vec3& n) {
    const Vec3 pa = normalize(tangent(a, n)), pb = normalize(tangent(b, n));
    if (length(pa) < 0.5f || length(pb) < 0.5f) return 0.f;
    return safeAcosDeg(dot(pa, pb));
}
// Points 0..100: 100 at or past `full`, 0 at or past `zero` (either direction).
inline float lerpPts(float x, float full, float zero) {
    if (full == zero) return x == full ? 100.f : 0.f;
    return 100.f * clamp01((x - zero) / (full - zero));
}
// Points 0..100: 100 inside [lo, hi], falling linearly to 0 at zlo (below) and zhi (above).
inline float bandPts(float x, float lo, float hi, float zlo, float zhi) {
    if (x < lo) return lerpPts(x, lo, zlo);
    if (x > hi) return lerpPts(x, hi, zhi);
    return 100.f;
}
inline float stickMag(const Inputs& in) { return std::fabs(in.pitch) + std::fabs(in.yaw) + std::fabs(in.roll); }
inline bool grounded(const CarState& c) { return c.wheelContacts >= k::kGroundedWheels; }
inline float ticksToMs(float ticks) { return ticks * k::kMsPerTick; }

// Contact normal of the surface under the wheels. GetGroundNormal may be stale or zero (UNVERIFIED on walls):
// fall back to the car's up vector, which matches the surface while the wheels touch.
inline Vec3 contactNormal(const CarState& c) {
    const Vec3 n = normalize(c.groundNormal);
    if (length(n) < 0.5f) return normalize(c.basis.up, {0.f, 0.f, 1.f});
    if (c.wheelContacts > 0 && dot(n, c.basis.up) < 0.5f) return normalize(c.basis.up, {0.f, 0.f, 1.f});
    return n;
}

// ---- Surfaces and dodges -------------------------------------------------------------------------------
enum class Surface : uint8_t { None, Floor, Wall, Ceiling };
inline Surface classifySurface(const Vec3& n) {
    if (n.z > 0.7f) return Surface::Floor;
    if (n.z < -0.7f) return Surface::Ceiling;
    return Surface::Wall;
}

struct Dir2 {
    float x = 0.f, y = 0.f;
};

// Local dodge direction from stick inputs (RocketSim Car::_UpdateDoubleJumpOrFlip). Zero = stall.
inline Dir2 dodgeDirFromInputs(const Inputs& in) {
    const float x = -in.pitch, y = in.yaw + in.roll;
    if (std::fabs(y) < 0.1f && std::fabs(x) < 0.1f) return {};
    const float l = std::sqrt(x * x + y * y);
    return {x / l, y / l};
}

enum class DodgeKind : uint8_t { Stall, Forward, DiagForward, Side, DiagBack, Back };
inline DodgeKind dodgeKindFromAngle(float angleDeg, bool zero) {
    if (zero) return DodgeKind::Stall;
    const float a = std::fabs(angleDeg);
    if (a <= 15.f) return DodgeKind::Forward;
    if (a <= 75.f) return DodgeKind::DiagForward;
    if (a < 105.f) return DodgeKind::Side;
    if (a < 165.f) return DodgeKind::DiagBack;
    return DodgeKind::Back;
}
inline const char* dodgeVariant(DodgeKind kd, float angleDeg) {
    switch (kd) {
    case DodgeKind::Stall: return "stall";
    case DodgeKind::Forward: return "forward";
    case DodgeKind::DiagForward: return angleDeg > 0 ? "diag-forward-right" : "diag-forward-left";
    case DodgeKind::Side: return angleDeg > 0 ? "side-right" : "side-left";
    case DodgeKind::DiagBack: return angleDeg > 0 ? "diag-back-right" : "diag-back-left";
    case DodgeKind::Back: return "back";
    }
    return "forward";
}

// World velocity change of a dodge (RocketSim formula, impulse in the car's yaw plane).
inline Vec3 dodgeDeltaV(Dir2 dir, const Vec3& vel, const Vec3& fwd, const Vec3& right) {
    if (std::fabs(dir.x) < 0.1f) dir.x = 0.f;
    if (std::fabs(dir.y) < 0.1f) dir.y = 0.f;
    if (dir.x == 0.f && dir.y == 0.f) return {};
    const Vec3 f2 = normalize(Vec3{fwd.x, fwd.y, 0.f}, {1.f, 0.f, 0.f});
    const Vec3 r2 = normalize(Vec3{right.x, right.y, 0.f}, {0.f, 1.f, 0.f});
    const float fwdSpeed = dot(vel, fwd);
    const float s = std::fabs(fwdSpeed) / k::kCarMaxSpeed;
    const bool backwards = std::fabs(fwdSpeed) < 100.f ? dir.x < 0.f : ((dir.x >= 0.f) != (fwdSpeed >= 0.f));
    float vx = dir.x * k::kFlipInitialVelScale;
    float vy = dir.y * k::kFlipInitialVelScale;
    const float sx = backwards ? k::kFlipBackwardImpulseMaxSpeedScale : k::kFlipForwardImpulseMaxSpeedScale;
    vx *= (sx - 1.f) * s + 1.f;
    vy *= (k::kFlipSideImpulseMaxSpeedScale - 1.f) * s + 1.f;
    if (backwards) vx *= k::kFlipBackwardImpulseScaleX;
    return f2 * vx + r2 * vy;
}

// ---- Internal grade ------------------------------------------------------------------------------------
// Keys and codes are string literals (no allocation per tick).
struct MechMetric {
    const char* key = "";
    float value = 0.f;
};

struct MechError {
    const char* code = "";  // e.g. "SF_LATE_CANCEL"
    float weight = 1.f;     // points the mistake cost (ranks the tips)
};

struct MechGrade {
    Mech mech = Mech::Count;
    std::string variant;     // "left", "side-right", "wall-diag-forward-left", "fast", ...
    bool success = false;
    float score = 0.f;       // 0..100, rounded
    std::vector<MechMetric> metrics;
    std::vector<MechError> errors;  // sorted by weight (heaviest first) by finalizeGrade
    int64_t tickStart = 0, tickEnd = 0;
    int64_t dodgeTick = -1;          // dodge that produced this grade (arbitration), -1 when none
    bool recoveryLanding = false;    // a dash that ended a long aerial phase (recovery arbitration)

    void add(const char* key, float v) { metrics.push_back({key, v}); }
    void err(const char* code, float weight) { errors.push_back({code, std::max(1.f, weight)}); }
    float metric(std::string_view key, float fallback = -9999.f) const {
        for (const auto& m : metrics)
            if (key == m.key) return m.value;
        return fallback;
    }
    bool hasMetric(std::string_view key) const { return metric(key, -1e30f) > -1e29f; }
    bool hasError(std::string_view code) const {
        for (const auto& e : errors)
            if (code == e.code) return true;
        return false;
    }
    const char* topError() const { return errors.empty() ? nullptr : errors.front().code; }
};

// Rounds and clamps the score, sorts the errors by weight (stable: detection order breaks ties).
inline void finalizeGrade(MechGrade& g, float score) {
    g.score = std::round(clampf(score, 0.f, 100.f));
    std::stable_sort(g.errors.begin(), g.errors.end(),
                     [](const MechError& a, const MechError& b) { return a.weight > b.weight; });
}

// ---- Detection configuration ---------------------------------------------------------------------------
struct DetectConfig {
    float speedflipTargetAngleDeg = 30.f;  // SpeedFlipTrainer default (+/-30)
    int speedflipCancelGreenTicks = 6;     // <= 50 ms
    int speedflipCancelRedTicks = 14;
    int chainMaxGapTicks = 24;             // landing -> next takeoff to keep a chain (UNVERIFIED)
    float recoveryMinHeight = 200.f;       // aerial phases lower than this are not recoveries
    int speedWindowTicks = 2400;           // boost efficiency summary period
    float dodgeDeadzone = k::kDodgeDeadzoneDefault;  // PlayerProfile::dodgeDeadzone
    // Intent: the player said what they train (focus mode or drill). A detector then also grades failed
    // attempts that would otherwise look like an ordinary move (a plain backflip in half-flip focus...).
    Focus focus = Focus::None;
    bool recoveryEnabled = true;           // arbitration: a dash ending an aerial is reported as a recovery
    bool expect(Focus f) const { return focus == f; }
};

} // namespace rlt::mech
