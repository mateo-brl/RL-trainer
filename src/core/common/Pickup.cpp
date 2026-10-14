#include "core/common/Pickup.h"

#include <algorithm>
#include <cmath>

namespace rlt::pickup {

Vec3 rollingSpin(const Vec3& vel, float ballRadius) {
    if (!(ballRadius > 1.f)) ballRadius = 91.25f;
    const Vec3 w{-vel.y / ballRadius, vel.x / ballRadius, 0.f};
    const float l = length(w);
    return l > kMaxSpin ? w * (kMaxSpin / l) : w;
}

Setup makeSetup(const Vec3& carPos, float yawDeg, int niveau, Rng& rng, float ballRadius) {
    if (!(ballRadius > 1.f)) ballRadius = 91.25f;
    Setup su;
    su.car.pos = carPos;
    su.car.rot = rotFromYawDeg(yawDeg);
    su.car.vel = {};
    su.car.angVel = {};
    const Vec3 fwd{std::cos(yawDeg * kDegToRad), std::sin(yawDeg * kDegToRad), 0.f};
    const float restZ = ballRadius + kArenaMargin;
    su.rolling = niveau >= kRollNiveau;
    if (su.rolling) {
        su.rollSpeed = rng.range(kRollSpeedMin, kRollSpeedMax);
        su.ball.pos = Vec3{carPos.x, carPos.y, 0.f} + fwd * kRollAhead;
        su.ball.pos.z = restZ;
        su.ball.vel = fwd * -su.rollSpeed;
        su.ball.angVel = rollingSpin(su.ball.vel, ballRadius);
    } else {
        su.ball.pos = Vec3{carPos.x, carPos.y, 0.f} + fwd * kRestAhead;
        su.ball.pos.z = restZ;
    }
    su.heldBall.pos = su.ball.pos;
    return su;
}

Setup makeChaseSetup(const Vec3& carPos, float yawDeg, int niveau, Rng& rng, float ballRadius) {
    if (!(ballRadius > 1.f)) ballRadius = 91.25f;
    const float t = clamp01(static_cast<float>(std::clamp(niveau, 1, 10) - 1) / 9.f);
    Setup su;
    su.chase = true;
    su.car.pos = carPos;
    su.car.rot = rotFromYawDeg(yawDeg);
    su.car.angVel = {};
    const Vec3 fwd{std::cos(yawDeg * kDegToRad), std::sin(yawDeg * kDegToRad), 0.f};
    const Vec3 right{fwd.y, -fwd.x, 0.f};
    const float ballSpeed = lerpf(kChaseBallSpeedMin, kChaseBallSpeedMax, t);
    su.carSpeed = ballSpeed + lerpf(kChaseRelMin, kChaseRelMax, t);
    su.car.vel = fwd * su.carSpeed;
    const float offset = niveau >= kChaseOffsetNiveau ? rng.range(-1.f, 1.f) * kChaseOffsetMax * t : 0.f;
    su.ball.pos = Vec3{carPos.x, carPos.y, 0.f} + fwd * lerpf(kChaseGapMin, kChaseGapMax, t) + right * offset;
    su.ball.pos.z = ballRadius + kArenaMargin;
    su.ball.vel = fwd * ballSpeed;
    su.ball.angVel = rollingSpin(su.ball.vel, ballRadius);
    su.rollSpeed = ballSpeed;
    su.heldBall.pos = su.ball.pos;
    return su;
}

// ---- Geometry ----------------------------------------------------------------------------------------------
bool ballOnRoof(const Snapshot& s, float tolUp, float margin) {
    const CarState& c = s.car;
    const BallState& b = s.ball;
    if (!c.valid || !b.valid || c.basis.up.z < 0.6f) return false;
    const Vec3 l = c.basis.toLocal(b.pos - c.pos);
    const float rest = c.hitboxOffset.z + c.hitboxExtent.z * 0.5f + b.radius;
    if (l.z < rest - 15.f || l.z > rest + tolUp) return false;
    return std::fabs(l.x - c.hitboxOffset.x) <= c.hitboxExtent.x * 0.5f + margin &&
           std::fabs(l.y - c.hitboxOffset.y) <= c.hitboxExtent.y * 0.5f + margin;
}

float relativePlanarSpeed(const Snapshot& s) {
    const Vec3 rel = s.ball.vel - s.car.vel;
    const Vec3 up = s.car.basis.up;
    return length(rel - up * dot(rel, up));
}

float roofOffsetX(const Snapshot& s) {
    return dot(s.ball.pos - s.car.pos, s.car.basis.fwd) - s.car.hitboxOffset.x;
}

bool ballSettled(const Snapshot& s, bool onRoof) {
    return onRoof && relativePlanarSpeed(s) < kSettleRelSpeed && std::fabs(roofOffsetX(s)) <= kSettleCentre;
}

// ---- Steps -------------------------------------------------------------------------------------------------
namespace {
bool roofTest(const Options& o, const Snapshot& s) { return o.onRoof ? o.onRoof(s) : ballOnRoof(s); }
} // namespace

Options options(bool rolling) {
    Options o;
    if (rolling) {
        o.roofLabel = kRoofLabelRolling;
        o.roofTip = kRoofTipRolling;
    }
    return o;
}

Options chaseOptions() {
    Options o;
    o.roofLabel = kRoofLabelChase;
    o.roofTip = kRoofTipChase;
    return o;
}

Options optionsFor(const Setup& su) { return su.chase ? chaseOptions() : options(su.rolling); }

Step roofStep(const Options& o) {
    Step st;
    st.id = kStepRoof;
    st.label = o.roofLabel;
    st.check = [o](const StepInput& in) {
        return roofTest(o, in.snap) ? StepVerdict::done() : StepVerdict::pending();
    };
    st.hold = kRoofHold;
    st.timeout = o.roofTimeout;
    st.failCode = o.roofCode;
    st.tip = o.roofTip;
    st.group = kGroup;
    return st;
}

Step settleStep(const Options& o) {
    Step st;
    st.id = kStepSettle;
    st.label = o.settleLabel;
    st.check = [o](const StepInput& in) {
        const Snapshot& s = in.snap;
        if (!o.jumpCode.empty() && s.car.valid && (s.in.jump || s.car.jumped))
            return StepVerdict::fail(o.jumpCode, o.jumpTip);
        if (o.failOnDrop && s.ball.valid && s.ball.pos.z < s.ball.radius + kDropZ)
            return StepVerdict::fail(o.dropCode, o.dropTip);
        return ballSettled(s, roofTest(o, s)) ? StepVerdict::done() : StepVerdict::pending();
    };
    st.hold = kSettleHold;
    st.timeout = o.settleTimeout;
    st.failCode = o.settleCode;
    st.tip = o.settleTip;
    st.group = kGroup;
    return st;
}

void addSteps(StepTracker& t, const Options& o) {
    t.add(roofStep(o));
    t.add(settleStep(o));
}

// ---- Own staircase -----------------------------------------------------------------------------------------
int Progress::update(const StepTracker& t) {
    if (recorded_) return 0;
    if (t.groupDone(kGroup)) {
        stairs_.record(true);
        recorded_ = true;
        return 1;
    }
    if (t.failed() && t.failure().group == kGroup) {
        stairs_.record(false);
        recorded_ = true;
        return -1;
    }
    return 0;
}

int Progress::niveau() const { return 1 + static_cast<int>(std::lround(clamp01(stairs_.level()) * 9.f)); }

} // namespace rlt::pickup
