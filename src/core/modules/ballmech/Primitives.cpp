#include "core/modules/ballmech/Primitives.h"

#include <algorithm>
#include <cmath>

namespace rlt::bm {

namespace {
constexpr float kInvSqrt2 = 0.70710678f;
} // namespace

// ---- Surfaces ----------------------------------------------------------------------------------------------
SurfaceHit nearestSurface(const Vec3& p, float r) {
    using namespace arena;
    SurfaceHit s;
    auto test = [&s](float gap, Vec3 n, Surface kind) {
        if (gap < s.gap) {
            s.gap = gap;
            s.normal = n;
            s.kind = kind;
        }
    };
    test(p.z - r, {0.f, 0.f, 1.f}, Surface::Floor);
    test(kCeilingZ - p.z - r, {0.f, 0.f, -1.f}, Surface::Ceiling);
    test(kSideWallX - std::fabs(p.x) - r, {p.x >= 0.f ? -1.f : 1.f, 0.f, 0.f}, Surface::SideWall);
    const bool goalMouth = std::fabs(p.x) < kGoalHalfWidth && p.z < kGoalHeight;
    if (!goalMouth) test(kBackWallY - std::fabs(p.y) - r, {0.f, p.y >= 0.f ? -1.f : 1.f, 0.f}, Surface::BackWall);
    const float corner = (kCornerSum - (std::fabs(p.x) + std::fabs(p.y))) * kInvSqrt2 - r;
    test(corner, {(p.x >= 0.f ? -1.f : 1.f) * kInvSqrt2, (p.y >= 0.f ? -1.f : 1.f) * kInvSqrt2, 0.f}, Surface::Corner);
    return s;
}

// ---- Wheel contacts ---------------------------------------------------------------------------------------
bool ballUnderWheels(const Snapshot& s) {
    const CarState& c = s.car;
    const BallState& b = s.ball;
    if (!c.valid || !b.valid) return false;
    const Vec3 d = b.pos - hitboxCenter(c);
    const float dist = length(d);
    const float halfH = c.hitboxExtent.z * 0.5f;
    if (dist < 1.f || dist > b.radius + halfH + 90.f) return false;
    return dot(-c.basis.up, d) > 0.5f * dist;
}

int ballWheelContacts(const Snapshot& s, bool standardArena) {
    const CarState& c = s.car;
    if (c.wheelContacts <= 0 || !c.valid || !s.ball.valid) return 0;
    if (ballUnderWheels(s)) return c.wheelContacts;
    if (standardArena && nearestSurface(c.pos, 0.f).gap > 160.f &&
        distance(hitboxCenter(c), s.ball.pos) < s.ball.radius + 110.f)
        return c.wheelContacts;
    return 0;
}

int worldWheelContacts(const Snapshot& s, bool standardArena) {
    return std::max(0, s.car.wheelContacts - ballWheelContacts(s, standardArena));
}

float undersideAlignment(const CarState& c, const BallState& b) {
    const Vec3 d = normalize(b.pos - hitboxCenter(c));
    return dot(-c.basis.up, d);
}

// ---- Carry --------------------------------------------------------------------------------------------------
float roofBallHeight(const CarState& c, float ballRadius) {
    return c.hitboxOffset.z + c.hitboxExtent.z * 0.5f + ballRadius;
}

bool carryGeometry(const Snapshot& s, Vec3* localOut) {
    const CarState& c = s.car;
    const BallState& b = s.ball;
    const Vec3 L = c.basis.toLocal(b.pos - c.pos);
    if (localOut) *localOut = L;
    if (!c.valid || !b.valid || c.basis.up.z < 0.6f) return false;  // dribbles happen on the floor, roof up
    const float halfLen = c.hitboxExtent.x * 0.5f;
    const float halfWid = c.hitboxExtent.y * 0.5f;
    const float roof = roofBallHeight(c, b.radius);
    const float xMin = c.hitboxOffset.x - (halfLen + 50.f);
    const float xMax = c.hitboxOffset.x + halfLen + 137.f;
    const Vec3 rel = b.vel - c.vel;
    const Vec3 relPlane = rel - c.basis.up * dot(rel, c.basis.up);
    return L.x >= xMin && L.x <= xMax && std::fabs(L.y) <= halfWid + 128.f && L.z >= roof - 70.f &&
           L.z <= roof + 150.f && length(relPlane) <= 300.f;
}

// ---- Goal projection ----------------------------------------------------------------------------------------
GoalProjection projectToGoal(const Vec3& p, const Vec3& v, int team, float r, bool standardArena, float horizon) {
    using namespace arena;
    GoalProjection g;
    const float sgn = attackSign(team);
    const float lineY = sgn * kBackWallY;
    const Vec3 toGoal = Vec3{0.f, lineY, kGoalHeight * 0.5f} - p;
    g.angleErrDeg = length2D(v) < 1.f ? 180.f : angleBetweenDeg(flat(v), flat(toGoal));
    g.valid = standardArena;
    if (!standardArena) return g;

    constexpr float dt = 1.f / 60.f;
    const int steps = std::max(1, static_cast<int>(horizon * 60.f));
    const int sampleEvery = std::max(1, steps / static_cast<int>(g.points.size() - 1));
    Vec3 pos = p, vel = v;
    g.points[0] = pos;
    g.pointCount = 1;
    for (int i = 1; i <= steps; ++i) {
        vel.z += kGravityZ * dt;
        vel *= (1.f - kBallDrag * dt);
        pos += vel * dt;
        if (pos.z < r) {
            pos.z = r;
            if (vel.z < 0.f) {
                // Friction impulse bounded by mu (1 + e) |vn| (mu 0.35) and by the slide-to-roll change (2/7 of vt).
                const float vn = -vel.z;
                const float vt = length2D(vel);
                if (vt > 1.f) {
                    const float loss = std::min(0.35f * (1.f + kBallRestitution) * vn, vt * (2.f / 7.f));
                    const float k = (vt - loss) / vt;
                    vel.x *= k;
                    vel.y *= k;
                }
                vel.z = vn * kBallRestitution;
            }
        }
        if (pos.z > kCeilingZ - r) {
            pos.z = kCeilingZ - r;
            if (vel.z > 0.f) vel.z = -vel.z * kBallRestitution;
        }
        if (std::fabs(pos.x) > kSideWallX - r) {
            pos.x = (pos.x > 0.f ? 1.f : -1.f) * (kSideWallX - r);
            vel.x = -vel.x * kBallRestitution;
        }
        const bool sample = i % sampleEvery == 0 && g.pointCount < static_cast<int>(g.points.size());
        if (pos.y * sgn >= kBackWallY - r) {
            g.reaches = true;
            g.x = pos.x;
            g.z = pos.z;
            g.t = static_cast<float>(i) * dt;
            g.onTarget = std::fabs(pos.x) <= kGoalHalfWidth - 0.4f * r && pos.z <= kGoalHeight - 0.3f * r;
            if (g.pointCount < static_cast<int>(g.points.size())) g.points[g.pointCount++] = pos;
            else g.points[g.points.size() - 1] = pos;
            break;
        }
        if (pos.y * sgn <= -(kBackWallY - r)) break;  // reached our own back wall
        if (sample) g.points[g.pointCount++] = pos;
    }
    return g;
}

float accuracyScore(const GoalProjection& g) {
    if (!g.valid) return 0.7f;
    const float a = score01(g.angleErrDeg, 30.f, 5.f);
    return g.onTarget ? std::max(a, 0.8f) : a * 0.6f;
}

Metric makeMetric(const char* key, const char* label, float value, const char* unit) {
    Metric m;
    m.key = key;
    m.label = label;
    m.value = value;
    m.unit = unit;
    return m;
}

Metric makeMetric(const char* key, const char* label, float value, const char* unit, float ideal) {
    Metric m = makeMetric(key, label, value, unit);
    m.ideal = ideal;
    m.hasIdeal = true;
    return m;
}

// ---- Frames -------------------------------------------------------------------------------------------------
Vec3 dodgeDirectionFromInputs(const Inputs& in) {
    Vec3 d{in.dodgeForward, in.dodgeStrafe, 0.f};
    if (length(d) < 0.1f) d = {-in.pitch, in.yaw + in.roll, 0.f};
    return d;
}

Frame FrameBuilder::build(const Snapshot& s, const Snapshot* prev, const Snapshot* next1, const Snapshot* next2,
                          const TouchMark* touch, bool sdkReset, bool goalEvent, const BuildOptions& opt) {
    Frame f;
    f.s = s;
    f.t = s.time;
    f.frame = s.frame;
    f.team = s.car.team;
    f.sign = attackSign(s.car.team);
    f.standardArena = opt.standardArena;
    f.ballWheels = ballWheelContacts(s, opt.standardArena);
    f.worldWheels = std::max(0, s.car.wheelContacts - f.ballWheels);
    f.grounded = f.worldWheels >= 3;
    f.freeFlight = f.worldWheels == 0 && s.car.wheelContacts < 3;
    if (touch) {
        f.touched = true;
        f.touchNew = touch->newContact;
        f.hasHit = touch->hasHit;
        f.hitLocation = touch->hitLocation;
        f.hitNormal = touch->hitNormal;
    }
    f.sdkReset = sdkReset;
    f.goalEvent = goalEvent;
    if (!prev) return f;

    f.jumpEdge = s.in.jump && !prev->in.jump;
    const bool prevFree = worldWheelContacts(*prev, opt.standardArena) == 0 && prev->car.wheelContacts < 3;
    const bool canDodge = prevFree && prev->car.hasFlip && !prev->car.dodging;
    const bool gameDodgeSoon = !s.car.dodging && ((next1 && next1->car.dodging) ||
                                                  (next1 && next2 && !next1->car.dodging && next2->car.dodging));
    bool start = false;
    bool doubleJump = false;
    if (f.jumpEdge && canDodge) {
        if (stickMagnitude(s.in) >= opt.dodgeDeadzone || gameDodgeSoon) start = true;
        else doubleJump = true;
    }
    if (s.car.dodging && !prev->car.dodging) {
        start = true;
        doubleJump = false;
    }
    if (start && s.time - lastDodgeT_ < 0.3f) start = false;  // input edge then game flag: one dodge
    if (start) {
        lastDodgeT_ = s.time;
        f.dodgeStart = true;
        Vec3 d = dodgeDirectionFromInputs(s.in);
        if (length(d) < 0.1f) {
            const Snapshot* cands[3] = {&s, next1, next2};
            for (const Snapshot* c : cands) {
                if (c && c->car.dodging && length(c->car.dodgeDir) > 0.1f) {
                    d = c->car.dodgeDir;
                    break;
                }
            }
        }
        f.dodgeDir = normalize(Vec3{d.x, d.y, 0.f});
    } else if (doubleJump) {
        f.doubleJumpStart = true;
    }
    return f;
}

} // namespace rlt::bm
