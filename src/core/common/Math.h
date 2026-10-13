#pragma once
// Small, dependency-free math used by the whole core.
// Conventions follow Rocket League / Unreal: X forward (towards orange goal for blue), Y right, Z up,
// distances in unreal units (uu), speeds in uu/s, rotations in Unreal rotator units (65536 = 360 deg).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rlt {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.f * kPi;
constexpr float kDegToRad = kPi / 180.f;
constexpr float kRadToDeg = 180.f / kPi;
constexpr float kUnrToRad = kPi / 32768.f;   // Unreal rotator unit -> radians
constexpr float kRadToUnr = 32768.f / kPi;

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
};

constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline constexpr float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline constexpr float lengthSq(const Vec3& v) { return dot(v, v); }
inline float length2D(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline Vec3 normalize(const Vec3& v, const Vec3& fallback = {0.f, 0.f, 0.f}) {
    const float l = length(v);
    return l > 1e-6f ? v / l : fallback;
}
inline constexpr Vec3 flat(const Vec3& v) { return {v.x, v.y, 0.f}; }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float clamp01(float v) { return clampf(v, 0.f, 1.f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sgn(float v) { return v > 0.f ? 1.f : (v < 0.f ? -1.f : 0.f); }

// Wraps an angle in radians to (-pi, pi].
inline float wrapPi(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.f) a += kTwoPi;
    return a - kPi;
}

// Angle in degrees between two vectors (0..180). Returns 0 for degenerate input.
inline float angleBetweenDeg(const Vec3& a, const Vec3& b) {
    const float la = length(a), lb = length(b);
    if (la < 1e-6f || lb < 1e-6f) return 0.f;
    return std::acos(clampf(dot(a, b) / (la * lb), -1.f, 1.f)) * kRadToDeg;
}

// Linear score helper: 1 at `best`, 0 at `worst` (works for best < worst or best > worst), clamped.
inline float rampScore(float v, float best, float worst) {
    if (best == worst) return v == best ? 1.f : 0.f;
    return clamp01((v - worst) / (best - worst));
}

// Score 1 inside [lo, hi], falling linearly to 0 at lo - tol and hi + tol.
inline float bandScore(float v, float lo, float hi, float tol) {
    if (v >= lo && v <= hi) return 1.f;
    if (tol <= 0.f) return 0.f;
    const float d = v < lo ? lo - v : v - hi;
    return clamp01(1.f - d / tol);
}

// Unreal rotator (pitch, yaw, roll) in rotator units.
struct Rot {
    int pitch = 0, yaw = 0, roll = 0;
    constexpr bool operator==(const Rot& o) const { return pitch == o.pitch && yaw == o.yaw && roll == o.roll; }
};

// Orthonormal basis of an actor: forward, right, up in world space.
struct Basis {
    Vec3 fwd{1.f, 0.f, 0.f};
    Vec3 right{0.f, 1.f, 0.f};
    Vec3 up{0.f, 0.f, 1.f};

    Vec3 toWorld(const Vec3& local) const { return fwd * local.x + right * local.y + up * local.z; }
    Vec3 toLocal(const Vec3& world) const { return {dot(world, fwd), dot(world, right), dot(world, up)}; }
};

// Basis from an Unreal rotator. Matches the engine (left-handed, Z up): yaw turns X towards Y,
// positive pitch raises the nose, positive roll tilts the right side down.
inline Basis basisFromRot(const Rot& r) {
    const float p = r.pitch * kUnrToRad, y = r.yaw * kUnrToRad, ro = r.roll * kUnrToRad;
    const float sp = std::sin(p), cp = std::cos(p);
    const float sy = std::sin(y), cy = std::cos(y);
    const float sr = std::sin(ro), cr = std::cos(ro);
    Basis b;
    b.fwd = {cp * cy, cp * sy, sp};
    b.right = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    b.up = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
    return b;
}

inline Rot rotFromYawDeg(float yawDeg) {
    return {0, static_cast<int>(std::lround(yawDeg * 65536.f / 360.f)), 0};
}

// Rotator that points the forward axis along `dir` (roll 0).
inline Rot rotFromDirection(const Vec3& dir) {
    const Vec3 d = normalize(dir, {1.f, 0.f, 0.f});
    const float yaw = std::atan2(d.y, d.x);
    const float pitch = std::atan2(d.z, length2D(d));
    return {static_cast<int>(std::lround(pitch * kRadToUnr)), static_cast<int>(std::lround(yaw * kRadToUnr)), 0};
}

// Builds any unit vector perpendicular to n, and a second one completing the frame.
inline void perpendicularFrame(const Vec3& n, Vec3& u, Vec3& w) {
    const Vec3 nn = normalize(n, {0.f, 0.f, 1.f});
    const Vec3 helper = std::fabs(nn.z) < 0.9f ? Vec3{0.f, 0.f, 1.f} : Vec3{1.f, 0.f, 0.f};
    u = normalize(cross(helper, nn));
    w = cross(nn, u);
}

// Rotates v around unit axis k by angle (radians), Rodrigues formula.
inline Vec3 rotateAround(const Vec3& v, const Vec3& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.f - c));
}

} // namespace rlt
