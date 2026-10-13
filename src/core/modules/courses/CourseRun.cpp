#include "core/modules/courses/CourseRun.h"

#include <algorithm>

namespace rlt::courses {

void CourseRun::reset(const Course* course, MissPolicy policy, bool grazeCounts, double penaltySeconds) {
    course_ = course;
    policy_ = policy;
    grazeCounts_ = grazeCounts;
    penaltyPerMiss_ = std::max(0.0, penaltySeconds);
    next_ = misses_ = grazes_ = wrongWays_ = 0;
    penalty_ = 0;
    finished_ = failed_ = false;
    comeBack_ = false;
    splits_.clear();
    if (course_) splits_.reserve(course_->rings.size());
    last_ = {};
}

int CourseRun::ringsPassed() const {
    int n = 0;
    for (const auto& s : splits_)
        if (!s.missed) ++n;
    return n;
}

bool CourseRun::ringMissed(int i) const {
    for (const auto& s : splits_)
        if (s.ring == i) return s.missed;
    return false;
}

RunEvent CourseRun::pass(int ring, double tCross, const PassResult& pr) {
    const Ring& r = course_->rings[static_cast<size_t>(ring)];
    RunSplit sp;
    sp.ring = ring;
    sp.time = tCross + penalty_;
    sp.kind = pr.kind;
    sp.centring = r.radius > 0 ? pr.radial / r.radius : 0.0;
    splits_.push_back(sp);
    if (pr.kind == PassKind::Grazed) ++grazes_;
    comeBack_ = false;
    next_ = ring + 1;
    if (next_ >= ringCount()) {
        finished_ = true;
        return RunEvent::Finished;
    }
    return RunEvent::Passed;
}

RunEvent CourseRun::giveUp(int ring, double tCross, const PassResult& pr) {
    const Ring& r = course_->rings[static_cast<size_t>(ring)];
    penalty_ += penaltyPerMiss_;
    RunSplit sp;
    sp.ring = ring;
    sp.time = tCross + penalty_;
    sp.kind = pr.kind;
    sp.missed = true;
    sp.centring = r.radius > 0 ? pr.radial / r.radius : 0.0;
    splits_.push_back(sp);
    next_ = ring + 1;
    if (next_ >= ringCount()) {
        finished_ = true;
        return RunEvent::Finished;
    }
    return RunEvent::Missed;
}

RunEvent CourseRun::step(const DVec3& p0, const DVec3& p1, double t0, double t1, const DVec3* v0, const DVec3* v1) {
    if (!active() || next_ >= ringCount()) return RunEvent::None;
    const double dt = std::max(0.0, t1 - t0);
    auto test = [&](const Ring& r) {
        if (v0 && v1 && dt > 1.5 * phys::kTickDt) return testHermite(r, p0, *v0, p1, *v1, dt, course_->objRadius);
        return testSegment(r, p0, p1, course_->objRadius);
    };
    const PassResult pr = test(course_->rings[static_cast<size_t>(next_)]);
    last_ = pr;
    const double tCross = t0 + pr.t * dt;
    const bool counted = pr.kind == PassKind::Clean || (grazeCounts_ && pr.kind == PassKind::Grazed);
    if (counted) return pass(next_, tCross, pr);
    if (pr.kind == PassKind::Backward) {
        // Going back through the missed ring to take it again (ComeBack) is the way back, not a mistake.
        if (comeBack_) return RunEvent::None;
        ++wrongWays_;
        return RunEvent::WrongWay;
    }
    const bool rimNotCounted = pr.kind == PassKind::Grazed;  // grazes do not count with this setting
    if (rimNotCounted || isNearMiss(course_->rings[static_cast<size_t>(next_)], pr)) {
        ++misses_;
        switch (policy_) {
        case MissPolicy::Strict: failed_ = true; return RunEvent::Failed;
        case MissPolicy::ComeBack: comeBack_ = true; return RunEvent::Missed;
        case MissPolicy::Penalty: return giveUp(next_, tCross, pr);
        }
        return RunEvent::Missed;
    }
    // Skipped the next ring and went through the one after it?
    if (next_ + 1 < ringCount()) {
        const PassResult p2 = test(course_->rings[static_cast<size_t>(next_ + 1)]);
        if (passCounts(p2.kind) && (grazeCounts_ || p2.kind == PassKind::Clean)) {
            ++misses_;
            last_ = p2;
            const double t2 = t0 + p2.t * dt;
            switch (policy_) {
            case MissPolicy::Strict: failed_ = true; return RunEvent::Failed;
            case MissPolicy::ComeBack: comeBack_ = true; return RunEvent::Skipped;
            case MissPolicy::Penalty: {
                PassResult skipped;
                skipped.kind = PassKind::None;
                skipped.radial = course_->rings[static_cast<size_t>(next_)].radius * 2.0;
                const RunEvent e = giveUp(next_, t2, skipped);
                if (e == RunEvent::Finished) return e;
                const RunEvent e2 = pass(next_, t2, p2);
                return e2 == RunEvent::Finished ? e2 : RunEvent::Skipped;
            }
            }
            return RunEvent::Skipped;
        }
    }
    return RunEvent::None;
}

} // namespace rlt::courses
