#include "core/modules/shooting/ShootGeometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace rlt::shoot {

// ---- Zones ------------------------------------------------------------------------------------------------
const char* zoneId(Zone z) {
    switch (z) {
    case Zone::TopLeft: return "tl";
    case Zone::TopRight: return "tr";
    case Zone::LowLeft: return "bl";
    case Zone::LowRight: return "br";
    case Zone::Center: return "center";
    default: return "?";
    }
}

const char* zoneLabel(Zone z) {
    switch (z) {
    case Zone::TopLeft: return "lucarne gauche";
    case Zone::TopRight: return "lucarne droite";
    case Zone::LowLeft: return "poteau bas gauche";
    case Zone::LowRight: return "poteau bas droit";
    case Zone::Center: return "centre";
    default: return "";
    }
}

bool parseZone(const std::string& text, int& out) {
    std::string s;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '-' || u == ' ')
            s.push_back('_');
        else
            s.push_back(static_cast<char>(std::tolower(u)));
    }
    struct Alias {
        const char* name;
        int zone;
    };
    static const Alias kAliases[] = {
        {"tl", 0},
        {"topleft", 0},
        {"top_left", 0},
        {"lg", 0},
        {"lucarne_gauche", 0},
        {"tr", 1},
        {"topright", 1},
        {"top_right", 1},
        {"ld", 1},
        {"lucarne_droite", 1},
        {"bl", 2},
        {"lowleft", 2},
        {"low_left", 2},
        {"bottom_left", 2},
        {"pbg", 2},
        {"poteau_bas_gauche", 2},
        {"br", 3},
        {"lowright", 3},
        {"low_right", 3},
        {"bottom_right", 3},
        {"pbd", 3},
        {"poteau_bas_droit", 3},
        {"center", 4},
        {"centre", 4},
        {"c", 4},
        {"mid", 4},
        {"random", kZoneRandom},
        {"rand", kZoneRandom},
        {"aleatoire", kZoneRandom},
        {"alea", kZoneRandom},
        {"any", kZoneRandom},
    };
    for (const auto& a : kAliases) {
        if (s == a.name) {
            out = a.zone;
            return true;
        }
    }
    return false;
}

float ZoneRect::normDistance(const Vec2& p) const {
    const float nu = hw > 1e-3f ? std::fabs(p.x - u) / hw : 0.f;
    const float nz = hh > 1e-3f ? std::fabs(p.y - z) / hh : 0.f;
    return std::max(nu, nz);
}

float ZoneRect::edgeDistance(const Vec2& p) const {
    const float eu = std::max(0.f, std::fabs(p.x - u) - hw);
    const float ez = std::max(0.f, std::fabs(p.y - z) - hh);
    return std::sqrt(eu * eu + ez * ez);
}

float ZoneRect::centreDistance(const Vec2& p) const {
    const float du = p.x - u, dz = p.y - z;
    return std::sqrt(du * du + dz * dz);
}

ZoneRect zoneRect(Zone zone, float level) {
    const float k = lerpf(1.25f, 0.75f, clamp01(level));
    ZoneRect r;
    switch (zone) {
    case Zone::TopLeft:
    case Zone::TopRight: {
        const float side = zone == Zone::TopLeft ? -1.f : 1.f;
        r.hw = 170.f * k;
        r.hh = 120.f * k;
        r.u = side * (kOnTargetHalfU - r.hw);
        r.z = kOnTargetTopZ - r.hh;
        break;
    }
    case Zone::LowLeft:
    case Zone::LowRight: {
        const float side = zone == Zone::LowLeft ? -1.f : 1.f;
        r.hw = 170.f * k;
        r.hh = 110.f * k;
        r.u = side * (kOnTargetHalfU - r.hw);
        r.z = kZoneFloorZ + r.hh;
        break;
    }
    case Zone::Center:
    default:
        r.hw = 230.f * k;
        r.hh = 170.f * k;
        r.u = 0.f;
        r.z = 300.f;
        break;
    }
    return r;
}

ZoneRect goalMouthRect() {
    ZoneRect r;
    r.u = 0.f;
    r.hw = kOnTargetHalfU;
    r.hh = (kOnTargetTopZ - kZoneFloorZ) * 0.5f;
    r.z = kZoneFloorZ + r.hh;
    return r;
}

bool onGoal(const Vec2& p) { return std::fabs(p.x) <= kOnTargetHalfU && p.y <= kOnTargetTopZ; }

AimResult scoreAim(const ZoneRect& zone, bool reached, const Vec2& hit) {
    AimResult a;
    a.reached = reached;
    if (!reached) return a;
    a.hit = hit;
    a.onGoal = onGoal(hit);
    a.inZone = zone.contains(hit);
    a.du = hit.x - zone.u;
    a.dz = hit.y - zone.z;
    a.centreDist = zone.centreDistance(hit);
    a.edgeDist = zone.edgeDistance(hit);
    if (a.inZone)
        a.score01 = 0.70f + 0.30f * clamp01(1.f - zone.normDistance(hit));
    else if (a.onGoal)
        a.score01 = 0.45f + 0.20f * clamp01(1.f - a.edgeDist / 400.f);
    else
        a.score01 = 0.40f * clamp01(1.f - a.edgeDist / 800.f);
    return a;
}

// ---- Hitbox / roof ----------------------------------------------------------------------------------------
Hitbox hitboxOf(const CarState& car) {
    Hitbox hb;
    const Vec3& e = car.hitboxExtent;
    if (e.x > 50.f && e.y > 30.f && e.z > 10.f && e.x < 400.f && e.y < 300.f && e.z < 200.f) {
        hb.extent = e;
        hb.offset = car.hitboxOffset;
    }
    return hb;
}

Vec3 roofCenter(const Vec3& carPos, const Basis& b, const Hitbox& hb) {
    return carPos + b.fwd * hb.offset.x + b.right * hb.offset.y + b.up * hb.roofZ();
}

Vec3 roofSpawn(const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius, float gap) {
    return carPos + b.fwd * hb.offset.x + b.right * hb.offset.y + b.up * (hb.roofZ() + ballRadius + gap);
}

Vec3 roofOffset(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius) {
    const Vec3 l = b.toLocal(ballPos - carPos);
    return {l.x - hb.offset.x, l.y - hb.offset.y, l.z - (hb.roofZ() + ballRadius)};
}

bool ballOnRoof(const Vec3& ballPos, const Vec3& carPos, const Basis& b, const Hitbox& hb, float ballRadius,
                float tolDown, float tolUp, float margin) {
    const Vec3 o = roofOffset(ballPos, carPos, b, hb, ballRadius);
    if (o.z < -tolDown || o.z > tolUp) return false;
    return std::fabs(o.x) <= hb.extent.x * 0.5f + margin && std::fabs(o.y) <= hb.extent.y * 0.5f + margin;
}

// ---- Prediction -------------------------------------------------------------------------------------------
std::optional<PlaneCrossing> predictToPlaneY(Vec3 p, Vec3 v, Vec3 w, float planeY, float maxTime, float radius) {
    const float dt = kTickDt;
    const float s = planeY >= 0.f ? 1.f : -1.f;
    const float target = std::fabs(planeY);
    if (s * p.y >= target) return std::nullopt; // already beyond the plane
    if (!(radius > 1.f)) radius = kBallRadius;
    const int steps = std::clamp(static_cast<int>(maxTime * kTickRate), 1, 1200);
    const float wallX = kArenaHalfX - kBallRestZ;
    const float ceilZ = kCeilingZ - kBallRestZ;
    const float inertiaLever = 0.4f * radius; // I / (m R) for a solid sphere
    int bounces = 0;
    for (int i = 0; i < steps; ++i) {
        v = v * (1.f - kBallDrag * dt);
        v.z -= kGravity * dt;
        // Floor contact (look-ahead), with restitution, Coulomb friction and spin (rolls without slipping).
        if (v.z < 0.f && p.z + v.z * dt < kBallRestZ) {
            const float vn = v.z;
            const bool bounce = -vn > 15.f; // Bullet restitution threshold (10 uu/s) + one tick of gravity
            const float vnOut = bounce ? -kBallRestitution * vn : 0.f;
            const float jn = vnOut - vn;
            const Vec3 slip{v.x - radius * w.y, v.y + radius * w.x, 0.f};
            const float sl = length(slip);
            if (sl > 1e-4f) {
                const float mag = std::min(sl * (2.f / 7.f), kBallFriction * jn);
                const Vec3 dvt = slip * (-mag / sl);
                v.x += dvt.x;
                v.y += dvt.y;
                w.x += dvt.y / inertiaLever;
                w.y -= dvt.x / inertiaLever;
            }
            v.z = vnOut;
            if (bounce) ++bounces;
        }
        // Side walls and ceiling: plain reflection (good enough for shots, which rarely use them).
        if (std::fabs(p.x + v.x * dt) > wallX && v.x * p.x > 0.f) {
            v.x = -kBallRestitution * v.x;
            ++bounces;
        }
        if (v.z > 0.f && p.z + v.z * dt > ceilZ) {
            v.z = -kBallRestitution * v.z;
            ++bounces;
        }
        const float sp = length(v);
        if (sp > kBallMaxSpeed) v = v * (kBallMaxSpeed / sp);
        const float spin = length(w);
        if (spin > kBallMaxSpin) w = w * (kBallMaxSpin / spin);

        Vec3 np = p + v * dt;
        if (np.z < kBallRestZ) np.z = kBallRestZ;
        const float a = target - s * p.y; // > 0: still on the field side
        const float b = target - s * np.y;
        if (b <= 0.f) {
            const float f = a / (a - b);
            PlaneCrossing c;
            c.pos = p + (np - p) * f;
            c.pos.y = planeY;
            c.vel = v;
            c.time = (static_cast<float>(i) + f) * dt;
            c.bounces = bounces;
            return c;
        }
        // Rolling away from the plane (or stopped) on the floor: it will never come back without walls.
        if (s * v.y <= 0.f && np.z <= kBallRestZ + 0.5f && std::fabs(v.z) < 1.f) break;
        p = np;
    }
    return std::nullopt;
}

} // namespace rlt::shoot
