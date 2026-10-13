#pragma once
// Geometry of the virtual courses: rings (disks), gates (rectangles) and the pass test between two physics
// ticks. Everything works in double precision on a small local vector type, because course generation must
// be bit-identical on every platform (course codes are shared between players) and the pass test benefits
// from the extra precision. Ported from the tested research reference (rings_ref.hpp).
//
// World frame = Rocket League: X towards the side walls (+-4096), Y towards the goals (+-5120), Z up.

#include <cmath>
#include <cstdint>

#include "core/common/Math.h"

namespace rlt::courses {

namespace phys {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTickRate = 120.0;
constexpr double kTickDt = 1.0 / kTickRate;
constexpr double kGravity = 650.0;                  // uu/s^2 (down)
constexpr double kBoostAccelAir = 3175.0 / 3.0;     // 1058.33 uu/s^2
constexpr double kBoostPerSecond = 100.0 / 3.0;     // boost units used per second
constexpr double kBallRadius = 91.25;
constexpr double kBallRestZ = 93.15;
constexpr double kBallDrag = 0.03;                  // v *= 1 - dt * drag per tick
constexpr double kBallMaxSpeed = 6000.0;
constexpr double kCarRestZ = 17.0;                  // car origin height at rest
constexpr double kArenaX = 4096.0;                  // side walls
constexpr double kArenaY = 5120.0;                  // back walls
constexpr double kCeilingZ = 2044.0;
constexpr double kCornerSum = 8064.0;               // corner chamfer plane |x| + |y| = 8064
constexpr double kCarPassRadius = 50.0;             // "radius" of the car for a clean pass (half width + 7)
} // namespace phys

struct DVec3 {
    double x = 0, y = 0, z = 0;
    constexpr DVec3() = default;
    constexpr DVec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    constexpr DVec3 operator+(const DVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr DVec3 operator-(const DVec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr DVec3 operator-() const { return {-x, -y, -z}; }
    constexpr DVec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr DVec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    DVec3& operator+=(const DVec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    DVec3& operator-=(const DVec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr bool operator==(const DVec3&) const = default;
};

constexpr DVec3 operator*(double s, const DVec3& v) { return v * s; }
constexpr double dot(const DVec3& a, const DVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr DVec3 cross(const DVec3& a, const DVec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const DVec3& v) { return std::sqrt(dot(v, v)); }
inline DVec3 normalize(const DVec3& v) {
    const double l = length(v);
    return l > 1e-12 ? v / l : DVec3{0, 0, 0};
}
inline double dlerp(double a, double b, double t) { return a + (b - a) * t; }
inline double dclamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline double degToRad(double d) { return d * phys::kPi / 180.0; }
inline double radToDeg(double r) { return r * 180.0 / phys::kPi; }
// Wraps an angle to (-pi, pi].
inline double wrapAngle(double a) {
    a = std::fmod(a + phys::kPi, 2.0 * phys::kPi);
    if (a < 0) a += 2.0 * phys::kPi;
    return a - phys::kPi;
}
// Direction from yaw/pitch (radians), Unreal convention (pitch > 0 = nose up).
inline DVec3 dirFromYawPitch(double yaw, double pitch) {
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}
// Any orthonormal pair (u, w) perpendicular to unit n; u is horizontal unless n is nearly vertical.
inline void perpBasis(const DVec3& n, DVec3& u, DVec3& w) {
    const DVec3 ref = std::fabs(n.z) < 0.9 ? DVec3{0, 0, 1} : DVec3{1, 0, 0};
    u = normalize(cross(ref, n));
    w = cross(n, u);
}

inline DVec3 toD(const Vec3& v) { return {v.x, v.y, v.z}; }
inline Vec3 toF(const DVec3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

// ---- Rings ---------------------------------------------------------------------------------------------------
enum class RingShape : uint8_t { Disk, Gate };
enum class Surface : uint8_t { Air, Floor, WallPosX, WallNegX };
constexpr uint8_t kFlagNone = 0;
constexpr uint8_t kFlagCheckpoint = 1;
constexpr uint8_t kFlagBoostRefill = 2;
constexpr uint8_t kFlagFinal = 4;

struct Ring {
    DVec3 center;
    DVec3 normal{0, 1, 0};   // unit, direction of travel through the ring
    DVec3 up{0, 0, 1};       // unit, perpendicular to normal (gate: vertical axis; disk: drawing reference)
    double radius = 300;     // disk radius, or gate half-width
    double halfHeight = 0;   // gate only
    RingShape shape = RingShape::Disk;
    Surface surface = Surface::Air;
    uint8_t flags = kFlagNone;
};

Ring makeDisk(const DVec3& c, const DVec3& n, double r, Surface s = Surface::Air);
Ring makeGate(const DVec3& c, const DVec3& n, double halfWidth, double halfHeight);

enum class PassKind : uint8_t {
    None,      // the segment does not cross the ring plane
    Clean,     // crossed forward, whole object inside the opening
    Grazed,    // crossed forward, object centre inside the opening but the body overlaps the rim
    Outside,   // crossed the plane forward, outside the opening (a miss when close to the ring)
    Backward   // crossed the opening in the wrong direction (never counts)
};
inline bool passCounts(PassKind k) { return k == PassKind::Clean || k == PassKind::Grazed; }

struct PassResult {
    PassKind kind = PassKind::None;
    double t = 0;         // segment parameter in [0,1] of the plane crossing
    DVec3 point;          // crossing point on the plane
    double radial = 0;    // disk: distance from the centre; gate: |lateral offset|
    double vertical = 0;  // gate: vertical offset from the centre (signed)
};

// Minimum distance between the line {X + s*dir} (dir unit) and the rim circle of a disk ring.
// Perpendicular crossing at radial distance rho: R - rho. Through the centre at angle theta: R cos(theta).
double rimClearance(const Ring& r, const DVec3& x, const DVec3& dir);

// Segment p0 -> p1 (object centre at two consecutive samples) against a ring, with the object radius used
// for the Clean / Grazed distinction. Half-open rule: a sample lying exactly on the plane counts once.
PassResult testSegment(const Ring& ring, const DVec3& p0, const DVec3& p1, double objRadius);

// Cubic Hermite position at s in [0,1] between (p0, v0) and (p1, v1) over dt seconds.
DVec3 hermite(const DVec3& p0, const DVec3& v0, const DVec3& p1, const DVec3& v1, double dt, double s);
// Same test on the Hermite arc, split into ~1 tick pieces (for long gaps between samples, e.g. a hitch).
PassResult testHermite(const Ring& ring, const DVec3& p0, const DVec3& v0, const DVec3& p1, const DVec3& v1,
                       double dt, double objRadius, int maxSub = 32);

// True when an Outside crossing happened close enough to the ring to be a real miss. A crossing of the
// (infinite) ring plane far from the ring is ignored.
bool isNearMiss(const Ring& ring, const PassResult& pr);

// Extent of a ring along the unit axis (disk: R sqrt(1 - (a.n)^2), gate: half sizes projected).
double ringExtent(const Ring& r, const DVec3& axis);
// True if the whole ring (minus the part buried in the surface it stands on) is inside the arena box with
// the corner chamfers, keeping `clearance` uu from every other surface.
bool ringInsideArena(const Ring& r, double clearance);

} // namespace rlt::courses
