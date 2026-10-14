#include "core/common/Hold.h"

#include <algorithm>
#include <cmath>

namespace rlt {

void applyHold(const HoldPose& hold, const Snapshot* snap, Actions& out) {
    CarPlacement c = hold.car;
    c.vel = {};
    c.angVel = {};
    if (hold.boost) c.boost = hold.boost;
    if (hold.keepSettledHeight && snap && snap->car.valid && hold.car.pos.z <= kSettleMaxZ &&
        basisFromRot(hold.car.rot).up.z > 0.9f && length2D(snap->car.pos - hold.car.pos) <= 30.f) {
        c.pos.z = clampf(snap->car.pos.z, hold.car.pos.z - kSettleBand, hold.car.pos.z + kSettleBand);
    }
    out.setCar = c;
    if (hold.ball) {
        BallPlacement b = *hold.ball;
        b.vel = {};
        b.angVel = {};
        out.setBall = b;
    }
    if (hold.boost) out.setBoost = *hold.boost;
    if (hold.freeze) out.freezeCar = true;
}

void ReadyPhase::start(float now, const Options& options) {
    opt_ = options;
    if (!(opt_.countdown >= 0.f)) opt_.countdown = 0.f;
    armed_ = false;
    lastMessage_ = -1;
    if (opt_.waitForThrottle) {
        phase_ = Phase::Waiting;
        goTime_ = now + opt_.countdown;  // provisional, moved when the player presses
    } else {
        phase_ = Phase::Countdown;
        goTime_ = now + opt_.countdown;
    }
}

bool ReadyPhase::update(const Snapshot& s, Actions& out) {
    const float now = s.time;
    if (phase_ == Phase::Waiting) {
        if (s.in.throttle < kReadyThrottleRelease) armed_ = true;
        if (!(armed_ && s.in.throttle >= kReadyThrottle)) return false;
        phase_ = Phase::Countdown;
        goTime_ = now + opt_.countdown;
    }
    if (phase_ != Phase::Countdown) return false;
    const float left = goTime_ - now;
    if (left <= 1e-4f) {
        if (opt_.waitThrottleRelease && s.in.throttle > kReadyThrottleRelease) return false;  // still held: no GO
        phase_ = Phase::Done;
        if (opt_.nativeMessages) out.goMessage = true;
        return true;
    }
    if (opt_.nativeMessages) {
        const int n = static_cast<int>(std::ceil(left - 1e-4f));
        if (n >= 1 && n != lastMessage_) {
            out.countdownMessage = n;
            lastMessage_ = n;
        }
    }
    return false;
}

float ReadyPhase::remaining(float now) const {
    switch (phase_) {
    case Phase::Waiting: return opt_.countdown;
    case Phase::Countdown: return std::max(0.f, goTime_ - now);
    default: return 0.f;
    }
}

std::string ReadyPhase::hudLine(float now) const {
    if (phase_ == Phase::Waiting) return "Appuie sur accélérer quand tu es prêt";
    if (waitingRelease(now)) return "Lâche l'accélérateur pour partir";
    if (phase_ == Phase::Countdown) {
        const int n = std::max(1, static_cast<int>(std::ceil(goTime_ - now - 1e-4f)));
        return "Départ dans " + std::to_string(n);
    }
    return {};
}

} // namespace rlt
