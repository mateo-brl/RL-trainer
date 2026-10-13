#include "core/modules/overlay/OverlayMath.h"

#include <algorithm>
#include <cmath>

#include "core/common/Text.h"

namespace rlt::ovl {

// ---- Speed differential --------------------------------------------------------------------------------------
SpeedMode speedModeFromIndex(int index) {
    switch (index) {
    case 1: return SpeedMode::XYZ;
    case 2: return SpeedMode::XYPlusZ;
    case 3: return SpeedMode::Closing;
    default: return SpeedMode::XY;
    }
}

const char* speedModeLabel(SpeedMode mode) {
    switch (mode) {
    case SpeedMode::XYZ: return "XYZ";
    case SpeedMode::XYPlusZ: return "XY + Z";
    case SpeedMode::Closing: return "approche";
    case SpeedMode::XY:
    default: return "XY";
    }
}

float applyDeadband(float value, float band) {
    if (!std::isfinite(value)) return 0.f;
    return std::fabs(value) <= band ? 0.f : value;
}

float closingSpeed(const Vec3& carPos, const Vec3& carVel, const Vec3& ballPos, const Vec3& ballVel) {
    const Vec3 toBall = ballPos - carPos;
    const float dist = length(toBall);
    if (dist < 1e-3f) return length(carVel) - length(ballVel);
    return dot(carVel - ballVel, toBall / dist);
}

SpeedDiff computeSpeedDiff(SpeedMode mode, const Vec3& carPos, const Vec3& carVel, const Vec3& ballPos,
                           const Vec3& ballVel) {
    SpeedDiff d;
    switch (mode) {
    case SpeedMode::XYZ: d.value = length(carVel) - length(ballVel); break;
    case SpeedMode::Closing: {
        d.value = closingSpeed(carPos, carVel, ballPos, ballVel);
        const Vec3 toBall = ballPos - carPos;
        const float dist = length(toBall);
        d.carApproach = dist < 1e-3f ? d.value : dot(carVel, toBall / dist);
        if (!std::isfinite(d.carApproach)) d.carApproach = 0.f;
        break;
    }
    case SpeedMode::XYPlusZ:
        d.value = length2D(carVel) - length2D(ballVel);
        d.z = applyDeadband(std::fabs(carVel.z) - std::fabs(ballVel.z));
        d.hasZ = true;
        break;
    case SpeedMode::XY:
    default: d.value = length2D(carVel) - length2D(ballVel); break;
    }
    d.value = applyDeadband(d.value);
    return d;
}

SpeedZone classifyZone(float diff, float goodThreshold, float alrightThreshold) {
    const float a = std::fabs(diff);
    const float good = std::max(0.f, goodThreshold);
    const float alright = std::max(good, alrightThreshold);
    if (a <= good) return SpeedZone::Good;
    if (a <= alright) return SpeedZone::Alright;
    return SpeedZone::Bad;
}

Color zoneColor(SpeedZone zone, const OverlayConfig& cfg) {
    switch (zone) {
    case SpeedZone::Good: return cfg.goodColor;
    case SpeedZone::Alright: return cfg.alrightColor;
    case SpeedZone::Bad:
    default: return cfg.badColor;
    }
}

const char* zoneLabel(SpeedZone zone) {
    switch (zone) {
    case SpeedZone::Good: return "Bon";
    case SpeedZone::Alright: return "Correct";
    case SpeedZone::Bad:
    default: return "Trop d'écart";
    }
}

std::string speedText(float uus, SpeedUnit unit) { return formatSignedSpeed(uus, unit, 2, kDeadband); }

std::string zText(float uus, SpeedUnit unit) { return "Z : " + speedText(uus, unit); }

std::string speedReport(SpeedMode mode, const SpeedDiff& diff, SpeedZone zone, SpeedUnit unit,
                        const std::string& throttleKey) {
    std::string s = "Écart de vitesse (";
    s += speedModeLabel(mode);
    s += ") : ";
    s += speedText(diff.value, unit);
    if (diff.hasZ) s += ", " + zText(diff.z, unit);
    s += ", ";
    s += zoneLabel(zone);
    s += ".";
    if (zone == SpeedZone::Bad) {
        const std::string key = throttleKey.empty() ? std::string() : " (" + throttleKey + ")";
        if (mode == SpeedMode::Closing) {
            // value = car part + ball part. The car is the main contributor when its own part is at least half of
            // the total (same sign): only then is the throttle the thing to change.
            const bool carMain = diff.value > 0.f ? diff.carApproach >= 0.5f * diff.value
                                                  : diff.carApproach <= 0.5f * diff.value;
            if (diff.value > 0.f)
                s += carMain ? " Tu fonces sur la balle : relâche l'accélération" + key + "."
                             : std::string(" La balle arrive trop vite sur toi : recule avec elle pour amortir.");
            else
                s += carMain ? std::string(" Tu t'éloignes de la balle : reviens vers elle.")
                             : " La balle s'éloigne de toi : accélère" + key + ".";
        } else if (diff.value > 0.f) {
            s += " Tu vas plus vite que la balle : relâche l'accélération" + key + ".";
        } else {
            s += " La balle va plus vite que toi : accélère" + key + ".";
        }
    }
    return s;
}

// ---- Touch zone ----------------------------------------------------------------------------------------------
ZoneBehavior behaviorFromIndex(int index) {
    switch (index) {
    case 1: return ZoneBehavior::VelocityDriven;
    case 2: return ZoneBehavior::KeepItUp;
    case 3: return ZoneBehavior::Auto;
    default: return ZoneBehavior::Static;
    }
}

const char* behaviorLabel(ZoneBehavior b) {
    switch (b) {
    case ZoneBehavior::VelocityDriven: return "Pilotée par la vitesse";
    case ZoneBehavior::KeepItUp: return "Garder en l'air";
    case ZoneBehavior::Auto: return "Auto";
    case ZoneBehavior::Static:
    default: return "Statique";
    }
}

ZoneBehavior resolveBehavior(ZoneBehavior configured, float ballSpeedXY, MapCategory category, float autoSpeed) {
    if (configured != ZoneBehavior::Auto) return configured;
    if (category == MapCategory::AirDribble) return ZoneBehavior::VelocityDriven;
    return ballSpeedXY > autoSpeed ? ZoneBehavior::VelocityDriven : ZoneBehavior::Static;
}

float tiltAngleDeg(float ballSpeedXY, float minSpeed, float maxSpeed, float maxTiltDeg) {
    const float maxTilt = clampf(std::isfinite(maxTiltDeg) ? maxTiltDeg : 0.f, 0.f, 80.f);
    if (!std::isfinite(ballSpeedXY) || ballSpeedXY <= minSpeed) return 0.f;
    if (maxSpeed <= minSpeed) return maxTilt;  // degenerate ramp: step at minSpeed
    return maxTilt * clamp01((ballSpeedXY - minSpeed) / (maxSpeed - minSpeed));
}

Vec3 zoneTargetDirection(ZoneBehavior effective, const Vec3& ballVel, float minSpeed, float maxSpeed,
                         float maxTiltDeg) {
    const Vec3 down{0.f, 0.f, -1.f};
    if (effective != ZoneBehavior::VelocityDriven && effective != ZoneBehavior::KeepItUp) return down;
    const float speedXY = length2D(ballVel);
    const float tilt = tiltAngleDeg(speedXY, minSpeed, maxSpeed, maxTiltDeg) * kDegToRad;
    if (tilt <= 0.f || speedXY < 1e-3f) return down;
    const Vec3 h = flat(ballVel) / speedXY;
    const float side = effective == ZoneBehavior::VelocityDriven ? -1.f : 1.f;
    return down * std::cos(tilt) + h * (side * std::sin(tilt));
}

Vec3 smoothDirection(const Vec3& current, const Vec3& target, float dt, float tau) {
    const Vec3 t = normalize(target, {0.f, 0.f, -1.f});
    const float lc = length(current);
    if (tau <= 0.f || !std::isfinite(lc) || lc < 0.5f) return t;
    if (!(dt > 0.f)) return current / lc;
    const float alpha = 1.f - std::exp(-dt / tau);
    const Vec3 c = current / lc;
    // Opposite directions: the blend could pass through zero, snap instead.
    if (dot(c, t) < -0.99f) return t;
    return normalize(c + (t - c) * alpha, t);
}

ZoneCircle zoneCircle(const Vec3& ballCenter, float ballRadius, const Vec3& dir, float circleRadius) {
    const float R = (std::isfinite(ballRadius) && ballRadius > 10.f) ? ballRadius : kDefaultBallRadius;
    const float r = clampf(std::isfinite(circleRadius) ? circleRadius : 47.f, 1.f, R - 0.5f);
    const Vec3 d = normalize(dir, {0.f, 0.f, -1.f});
    ZoneCircle c;
    c.center = ballCenter + d * std::sqrt(R * R - r * r);
    c.normal = d;
    c.radius = r;
    return c;
}

void zoneCirclePoints(const ZoneCircle& c, int segments, std::vector<Vec3>& out) {
    out.clear();
    if (segments < 3) segments = 3;
    Vec3 u, w;
    perpendicularFrame(c.normal, u, w);
    out.reserve(static_cast<size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        out.push_back(c.center + (u * std::cos(a) + w * std::sin(a)) * c.radius);
    }
}

} // namespace rlt::ovl
