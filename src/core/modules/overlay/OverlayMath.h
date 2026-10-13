#pragma once
// Stateless computations of the overlay module, kept apart so every rule that decides what the player sees is unit
// tested: speed differential (4 modes, deadband), speed zones and their colours, chat report, touch zone direction,
// tilt, smoothing and the geometry of the circle drawn ON the ball surface.
//
// Names live in rlt::ovl because all modules are linked into one plugin DLL.

#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/common/Context.h"
#include "core/common/Math.h"
#include "core/common/Units.h"
#include "core/modules/overlay/OverlayConfig.h"

namespace rlt::ovl {

// ---- Speed differential --------------------------------------------------------------------------------------
enum class SpeedMode : int { XY = 0, XYZ = 1, XYPlusZ = 2, Closing = 3 };
enum class SpeedZone : int { Good = 0, Alright = 1, Bad = 2 };

constexpr float kDeadband = 1.f;            // |diff| <= 1 uu/s is shown as 0 (like the original)
constexpr float kDefaultBallRadius = 91.25f;

SpeedMode speedModeFromIndex(int index);
const char* speedModeLabel(SpeedMode mode);   // short French label: "XY", "XYZ", "XY + Z", "approche"

// Values in [-band, band] become 0.
float applyDeadband(float value, float band = kDeadband);

struct SpeedDiff {
    float value = 0.f;        // main differential in uu/s (deadband applied). Positive: the car is faster / closes in
    float z = 0.f;            // |vz_car| - |vz_ball| (XY + Z mode only, deadband applied)
    bool hasZ = false;
    float carApproach = 0.f;  // Closing mode only: the car's OWN speed toward the ball (uu/s, no deadband). The rest
                              // of `value` is the ball moving toward the car; the chat hint blames the main part.
};

// Closing speed along the car -> ball direction: positive when the car closes in faster than the ball moves away.
// Falls back to |v_car| - |v_ball| when both centres coincide.
float closingSpeed(const Vec3& carPos, const Vec3& carVel, const Vec3& ballPos, const Vec3& ballVel);

SpeedDiff computeSpeedDiff(SpeedMode mode, const Vec3& carPos, const Vec3& carVel, const Vec3& ballPos,
                           const Vec3& ballVel);

// |diff| <= good -> Good, <= alright -> Alright, else Bad. A misconfigured alright < good behaves as alright == good.
SpeedZone classifyZone(float diff, float goodThreshold, float alrightThreshold);
Color zoneColor(SpeedZone zone, const OverlayConfig& cfg);
const char* zoneLabel(SpeedZone zone);        // "Bon", "Correct", "Trop d'écart"

// HUD strings: "+12.34 KPH" and "Z : -1.20 KPH".
std::string speedText(float uus, SpeedUnit unit);
std::string zText(float uus, SpeedUnit unit);

// Chat line of the "speed" command, e.g. "Écart de vitesse (XY) : +3.20 KPH, Bon." plus a short hint when the zone
// is bad. `throttleKey` is the player's throttle button label ("RT"), may be empty. In Closing mode the hint names
// whoever makes most of the closing speed: the car ("relâche l'accélération") or the ball ("recule avec elle").
std::string speedReport(SpeedMode mode, const SpeedDiff& diff, SpeedZone zone, SpeedUnit unit,
                        const std::string& throttleKey);

// ---- Touch zone ----------------------------------------------------------------------------------------------
enum class ZoneBehavior : int { Static = 0, VelocityDriven = 1, KeepItUp = 2, Auto = 3 };

ZoneBehavior behaviorFromIndex(int index);
const char* behaviorLabel(ZoneBehavior b);    // French labels of the settings choice

// Auto: velocity driven on air dribble maps or when the horizontal ball speed exceeds `autoSpeed`, else static.
// Other behaviours are returned unchanged.
ZoneBehavior resolveBehavior(ZoneBehavior configured, float ballSpeedXY, MapCategory category, float autoSpeed);

// Tilt from straight down, linear in the horizontal ball speed between minSpeed (0 deg) and maxSpeed (maxTiltDeg),
// clamped to [0, maxTiltDeg] (maxTiltDeg itself clamped to [0, 80]).
float tiltAngleDeg(float ballSpeedXY, float minSpeed, float maxSpeed, float maxTiltDeg);

// Unit direction from the ball centre to the middle of the zone. Starts at (0, 0, -1) and tilts toward -v_xy
// (velocity driven: behind the ball, to keep an air dribble climbing) or +v_xy (keep it up: in front, to brake it).
// `effective` must be a resolved behaviour (never Auto; Auto is treated as Static here).
Vec3 zoneTargetDirection(ZoneBehavior effective, const Vec3& ballVel, float minSpeed, float maxSpeed,
                         float maxTiltDeg);

// Exponential smoothing of a unit direction toward `target` with time constant `tau` (s). tau <= 0 or a degenerate
// state snaps to the target. The result is always a unit vector.
Vec3 smoothDirection(const Vec3& current, const Vec3& target, float dt, float tau);

struct ZoneCircle {
    Vec3 center;
    Vec3 normal;          // == direction, the circle plane is tangent-parallel to the ball surface there
    float radius = 0.f;   // clamped to [1, R - 0.5]
};

// Small circle of radius r ON the sphere (ball centre B, radius R) around direction d:
// centre = B + d * sqrt(R^2 - r^2), normal d. Every point of it is at distance R from B.
ZoneCircle zoneCircle(const Vec3& ballCenter, float ballRadius, const Vec3& dir, float circleRadius);

// Points of the circle, in the same order DrawList::circle3D draws them (segments + 1 points, first == last).
void zoneCirclePoints(const ZoneCircle& c, int segments, std::vector<Vec3>& out);

// "Ball in the air": centre strictly higher than the threshold (the original used ball z > 400).
inline bool ballInAir(float ballZ, float threshold) { return ballZ > threshold; }

} // namespace rlt::ovl
