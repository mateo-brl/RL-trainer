#include "core/modules/ballmech/Pipeline.h"

#include <algorithm>

namespace rlt::bm {

Pipeline::Pipeline() { enabled_.fill(true); }

Detector* Pipeline::detector(Family f) {
    switch (f) {
    case Family::FlipReset: return &fr_;
    case Family::Ceiling: return &ceiling_;
    case Family::DoubleTap: return &dt_;
    case Family::Pinch: return &pinch_;
    case Family::Flick: return &flick_;
    case Family::AirDribble: return &ad_;
    case Family::Aerial: return &aerial_;
    case Family::PowerShot: return &power_;
    default: return nullptr;
    }
}

void Pipeline::setEnabled(Family f, bool on) {
    const size_t i = static_cast<size_t>(f);
    if (i >= enabled_.size() || enabled_[i] == on) return;
    enabled_[i] = on;
    if (Detector* d = detector(f)) d->reset();
}

void Pipeline::restart() {
    resetDetectors();
    arbiter_.reset();
    builder_.reset();
    hasPrevFrame_ = false;
    lastProcessed_ = INT64_MIN;
    ignoreUntil_ = INT64_MIN;
    ballMoveUntil_ = INT64_MIN;
    goalPending_ = false;
    armDtRead_ = false;
    armCeiling_ = false;
    markCount_ = 0;
    sdkCount_ = 0;
}

void Pipeline::resetDetectors() {
    for (int i = 0; i < kFamilyCount; ++i)
        if (Detector* d = detector(static_cast<Family>(i))) d->reset();
}

void Pipeline::addTouch(const TouchMark& t) {
    if (markCount_ >= static_cast<int>(marks_.size())) {
        std::rotate(marks_.begin(), marks_.begin() + 1, marks_.end());
        markCount_ = static_cast<int>(marks_.size()) - 1;
    }
    marks_[static_cast<size_t>(markCount_++)] = t;
}

void Pipeline::addSdkReset(int64_t frame) {
    if (sdkCount_ >= static_cast<int>(sdk_.size())) {
        std::rotate(sdk_.begin(), sdk_.begin() + 1, sdk_.end());
        sdkCount_ = static_cast<int>(sdk_.size()) - 1;
    }
    sdk_[static_cast<size_t>(sdkCount_++)] = frame;
}

void Pipeline::process(const TickHistory& h, bool hasSnapshot, Output& out) {
    if (!hasSnapshot || h.size() < static_cast<size_t>(kDelay + 1)) return;
    // Frame counter went backwards (map reload, new game): start over.
    if (lastProcessed_ != INT64_MIN && h.back(0).frame + 1 < lastProcessed_) restart();
    const int maxBack = static_cast<int>(std::min<size_t>(h.size() - 1, 12));
    for (int i = maxBack; i >= kDelay; --i) {
        const Snapshot& s = h.back(static_cast<size_t>(i));
        if (s.frame <= lastProcessed_) continue;
        const Snapshot* prev = static_cast<size_t>(i + 1) < h.size() ? &h.back(static_cast<size_t>(i + 1)) : nullptr;
        feed(s, prev, &h.back(static_cast<size_t>(i - 1)), &h.back(static_cast<size_t>(i - 2)), out);
    }
}

void Pipeline::feed(const Snapshot& s, const Snapshot* prev, const Snapshot* next1, const Snapshot* next2, Output& out) {
    lastProcessed_ = s.frame;

    // ---- Game events attached to this tick
    bool sdk = false;
    {
        int keep = 0;
        for (int i = 0; i < sdkCount_; ++i) {
            const int64_t e = sdk_[static_cast<size_t>(i)];
            if (e <= s.frame + 1) {
                if (e >= s.frame - 120) sdk = true;  // older ones are stale
            } else {
                sdk_[static_cast<size_t>(keep++)] = e;
            }
        }
        sdkCount_ = keep;
    }
    const bool goal = goalPending_;
    goalPending_ = false;
    TouchMark mark;
    bool touched = false;
    {
        int keep = 0;
        for (int i = 0; i < markCount_; ++i) {
            const TouchMark& m = marks_[static_cast<size_t>(i)];
            if (m.frame <= s.frame) {
                if (m.frame >= s.frame - 3 && (!touched || m.frame > mark.frame)) {
                    if (touched) {
                        const bool wasNew = mark.newContact;
                        mark = m;
                        mark.newContact = mark.newContact || wasNew;
                    } else {
                        mark = m;
                    }
                    touched = true;
                }
            } else {
                marks_[static_cast<size_t>(keep++)] = m;
            }
        }
        markCount_ = keep;
    }

    if (s.paused) return;
    if (!s.car.valid || !s.ball.valid) {
        if (hasPrevFrame_) resetDetectors();
        hasPrevFrame_ = false;
        return;
    }

    Frame f = builder_.build(s, prev, next1, next2, touched ? &mark : nullptr, sdk, goal, opts_);
    bool broken = !hasPrevFrame_ || s.frame - prevFrame_.frame > kMaxGap || s.frame <= ignoreUntil_;
    if (!broken) {
        const float dt = std::max(kTickDt, f.t - prevFrame_.t);
        const Snapshot& p = prevFrame_.s;
        broken = distance(s.car.pos, p.car.pos + p.car.vel * dt) > kTeleport ||
                 (s.frame > ballMoveUntil_ && distance(s.ball.pos, p.ball.pos + p.ball.vel * dt) > kTeleport);
    }
    if (broken) {
        resetDetectors();
        prevFrame_ = std::move(f);
        hasPrevFrame_ = true;
        return;
    }
    if (armDtRead_) {
        armDtRead_ = false;
        dt_.armReadMode(f.t);
    }
    if (armCeiling_) {
        armCeiling_ = false;
        ceiling_.armOnCeiling(f.t);
    }

    for (int i = 0; i < kFamilyCount; ++i) {
        if (!enabled_[static_cast<size_t>(i)]) continue;
        Detector* d = detector(static_cast<Family>(i));
        if (!d) continue;
        if (auto g = d->update(prevFrame_, f, params_)) arbiter_.submit(std::move(*g), f.t);
    }
    if (enabled(Family::FlipReset) && fr_.acquiredSource() != 0)
        out.acquisitions.push_back({f.frame, f.t, fr_.acquiredSource()});

    EngagedState es;
    for (int i = 0; i < kFamilyCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        if (!enabled_[k]) continue;
        if (Detector* d = detector(static_cast<Family>(i))) es.on[k] = d->engaged(es.since[k]);
    }
    arbiter_.release(f.t, es, out.released);
    prevFrame_ = std::move(f);
}

} // namespace rlt::bm
