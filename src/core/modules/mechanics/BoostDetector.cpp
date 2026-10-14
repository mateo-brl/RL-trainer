#include "core/modules/mechanics/BoostDetector.h"

namespace rlt::mech {

void SpeedManagementAnalyzer::onTick(const Ctx& c, std::vector<MechGrade>& out) {
    const Snapshot& s = c.s;
    if (c.ev.reset) {
        clear();
        return;
    }
    if (!c.ev.valid) return;
    const Vec3 n = contactNormal(s.car);
    const float speed = grounded(s.car) ? length(tangent(s.car.vel, n)) : length(s.car.vel);
    const bool boosting = s.in.boost && s.car.boost > 0.f;
    ticks_++;
    if (boosting) {
        boostTicks_++;
        if (speed >= k::kCarMaxSpeed - 10.f) wasted_++;
        curHold_++;
    } else if (curHold_ > 0) {
        holds_++;
        holdSum_ += curHold_;
        curHold_ = 0;
    }
    if (grounded(s.car) && speed >= k::kSupersonicMaintainMinSpeed) {
        atSpeed_++;
        if (s.in.throttle < 0.5f && !boosting) coast_++;
    }
    const bool super = s.car.supersonic || speed >= k::kSupersonicStartSpeed;
    if (super) super_++;
    if (super && firstSuper_ < 0) firstSuper_ = ticks_ - 1;
    if (firstSuper_ >= 0) {
        afterTicks_++;
        if (super) superAfter_++;
    }
    if (ticks_ < c.cfg.speedWindowTicks) return;
    if (boostTicks_ > 24 || atSpeed_ > 60) emit(c, out);
    clear();
}

void SpeedManagementAnalyzer::flush(const Ctx& c, std::vector<MechGrade>& out, bool force) {
    if (ticks_ >= 120 && (force || boostTicks_ > 24 || atSpeed_ > 60)) emit(c, out);
    clear();
}

void SpeedManagementAnalyzer::emit(const Ctx& c, std::vector<MechGrade>& out) {
    MechGrade g;
    g.mech = Mech::BoostEfficiency;
    g.variant = "window";
    g.tickStart = c.s.frame - ticks_;
    g.tickEnd = c.s.frame;
    const float waste = boostTicks_ ? static_cast<float>(wasted_) / static_cast<float>(boostTicks_) : 0.f;
    const float coast = atSpeed_ ? static_cast<float>(coast_) / static_cast<float>(atSpeed_) : 0.f;
    const float avgHold = holds_ ? static_cast<float>(holdSum_) / static_cast<float>(holds_) : static_cast<float>(curHold_);
    const float superRatio = ticks_ ? static_cast<float>(super_) / static_cast<float>(ticks_) : 0.f;
    g.add("boostUsed", static_cast<float>(boostTicks_) * k::kBoostUsedPerSecond * k::kDt);
    g.add("boostWastedAtMax", static_cast<float>(wasted_) * k::kBoostUsedPerSecond * k::kDt);
    g.add("wasteRatio", waste);
    g.add("supersonicRatio", superRatio);
    g.add("coastAtSpeedRatio", coast);
    g.add("avgBoostHoldTicks", avgHold);
    g.add("windowSeconds", static_cast<float>(ticks_) * k::kDt);
    // Time from the start of the window to the first supersonic tick (99 when never), and how much of the time
    // after that the car stayed supersonic (boost drill).
    g.add("timeToSupersonic", firstSuper_ >= 0 ? static_cast<float>(firstSuper_) * k::kDt : 99.f);
    g.add("supersonicAfterRatio", afterTicks_ ? static_cast<float>(superAfter_) / static_cast<float>(afterTicks_) : 0.f);
    const float wasteS = lerpPts(waste, 0.05f, 0.5f), coastS = lerpPts(coast, 0.05f, 0.5f);
    const float score = 0.6f * wasteS + 0.4f * coastS;
    if (waste > 0.2f) g.err("SM_BOOST_AT_MAX", 0.6f * (100.f - wasteS));
    if (coast > 0.2f) g.err("SM_NO_THROTTLE", 0.4f * (100.f - coastS));
    if (waste > 0.2f && avgHold > 60.f) g.err("SM_LONG_HOLDS", 6.f);
    g.success = score >= 70.f;
    finalizeGrade(g, score);
    out.push_back(std::move(g));
}

} // namespace rlt::mech
